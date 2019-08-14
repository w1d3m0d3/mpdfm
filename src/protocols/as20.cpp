/*
 * This file is part of mpdfm.
 *
 * mpdfm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mpdfm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mpdfm.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <protocols/as20.hpp>

#include <algorithm>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/filesystem.hpp>
#include <boost/system/system_error.hpp>
#include <ctime>
#include <fstream>
#include <gsl/span>
#include <http_client.hpp>
#include <iterator>
#include <openssl/md5.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string_view>
#include <tao/json.hpp>
#include <tao/json/contrib/traits.hpp>
#include <uris.hpp>

namespace bc = boost::container;

namespace mpdfm {
    //! \brief Generic response wrapper
    struct response {
        std::string message;
        int error = 0;
    };

    //! \brief Token response wrapper
    struct token_response : public response {
        std::string token;
    };

    //! \brief Wrapper around one session
    struct single_session {
        std::string key;
        unsigned subscriber = 0;
        std::string name;
    };

    //! \brief Session response wrapper
    struct session_response : public response {
        single_session session;
    };

}  // namespace mpdfm

namespace tao::json {
    template<>
    struct traits<mpdfm::response>
        : public binding::basic_object<
              binding::for_unknown_key::skip,
              binding::for_nothing_value::suppress,
              TAO_JSON_BIND_OPTIONAL("message", &mpdfm::response::message),
              TAO_JSON_BIND_OPTIONAL("error", &mpdfm::response::error)> {};

    template<>
    struct traits<mpdfm::token_response>
        : public binding::basic_object<
              binding::for_unknown_key::skip,
              binding::for_nothing_value::suppress,
              binding::inherit<traits<mpdfm::response>>,
              TAO_JSON_BIND_OPTIONAL("token", &mpdfm::token_response::token)> {
    };

    template<>
    struct traits<mpdfm::session_response>
        : public binding::basic_object<
              binding::for_unknown_key::fail,
              binding::for_nothing_value::suppress,
              binding::inherit<traits<mpdfm::response>>,
              TAO_JSON_BIND_REQUIRED("session",
                                     &mpdfm::session_response::session)> {};
    template<>
    struct traits<mpdfm::single_session>
        : public binding::basic_object<
              binding::for_unknown_key::fail,
              binding::for_nothing_value::suppress,
              TAO_JSON_BIND_REQUIRED("key", &mpdfm::single_session::key),
              TAO_JSON_BIND_REQUIRED("subscriber",
                                     &mpdfm::single_session::subscriber),
              TAO_JSON_BIND_REQUIRED("name", &mpdfm::single_session::name)> {};
}  // namespace tao::json

namespace {
    auto hex_digits = "0123456789abcdef";

    /*!
     * \brief Handles formation of AS20 requests
     */
    struct audioscrobbler_request {
        /*!
         * \brief Creates a new as20 request that will be signed with the
         *        \p secret
         *
         * \param secret The secret to use to sign the request
         */
        explicit audioscrobbler_request(std::string secret)
            : m_api_secret(std::move(secret)) {}

        //! \brief MD5 signs the request
        [[nodiscard]] std::string sign() const {
            auto ctx = std::make_unique<MD5_CTX>();

            if (!MD5_Init(ctx.get())) {  // NOLINT C API
                throw std::runtime_error("md5 init failure");
            }

            for (auto &p : m_params) {
                auto &f = p.first;
                auto &s = p.second;
                // append each KV-pair
                // NOLINTNEXTLINE C API
                if (!(MD5_Update(ctx.get(), f.c_str(), f.length())
                      // NOLINTNEXTLINE C API
                      && MD5_Update(ctx.get(), s.c_str(), s.length()))) {
                    // if at least one is false
                    throw std::runtime_error("request digest failed");
                }
            }

            // append api secret
            // NOLINTNEXTLINE C API
            if (!MD5_Update(
                    ctx.get(), m_api_secret.c_str(), m_api_secret.length())) {
                throw std::runtime_error("request digest failed");
            }

            // convert to hex string
            std::array<uint8_t, MD5_DIGEST_LENGTH> digest = { 0 };
            if (MD5_Final(digest.data(), ctx.get()) == 0) {
                throw std::runtime_error("request digest finalization failed");
            }

            std::array<char, MD5_DIGEST_LENGTH * 2 + 1> digest_str = { 0 };
            for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
                // safe code
                digest_str[i * 2] = hex_digits[digest[i] >> 4];       // NOLINT
                digest_str[i * 2 + 1] = hex_digits[digest[i] & 0xf];  // NOLINT
            }
            return std::string(digest_str.data());
        }

        //! \brief Gets the parameter \p key using std::map operator[]
        std::string &operator[](const std::string &key) {
            return m_params[key];
        }

        //! \brief Joins and signs all the parameters
        std::string form() {
            std::stringstream s;
            for (auto &p : m_params) {
                s << "&" << mpdfm::urlencode(p.first) << "="
                  << mpdfm::urlencode(p.second);
            }
            s << "&format=json";
            s << "&api_sig=" << sign();
            return s.str();
        }

        //! \brief Helper for adding all track information to a request
        inline void add_track(const mpdfm::scrobble_entry &s,
                              const std::string &suffix = "") {
            add_tag(s.artist, "artist" + suffix);
            add_tag(s.track, "track" + suffix);
            add_tag(s.album, "album" + suffix);
            add_tag(s.track_number, "trackNumber" + suffix);
            add_tag(s.mbid, "mbid" + suffix);
            add_tag(s.album_artist, "albumArtist" + suffix);
            (*this)["duration" + suffix] = std::to_string(s.duration);
        }

    private:
        inline void add_tag(const std::string &value,
                            const std::string &field) {
            if (!value.empty()) {
                (*this)[field] = value;
            }
        }
        bc::flat_map<std::string, std::string> m_params;
        std::string m_api_secret;
    };
}  // namespace

mpdfm::as20::as20(std::string sk,
                  std::string as,
                  std::string ak,
                  const std::string &tu,
                  std::string sp)
    : m_session_key(std::move(sk)),
      m_api_key(std::move(ak)),
      m_api_secret(std::move(as)),
      m_target(tu),
      m_path(std::move(sp)) {
    spdlog::debug("uri target: {}", m_target.source());
    try {
        if (!m_path.empty()) {
            m_cache =
                tao::json::parse_file(m_path).template as<decltype(m_cache)>();
        }
    } catch (const std::exception &e) {
        spdlog::error(
            "couldnt read cache (ignoring): "
            "if missing, the file will be made once the program ends\n{}",
            e.what());
    }
}

mpdfm::as20::~as20() {
    try {
        if (m_path.empty()) {
            return;
        }
        std::ofstream str(m_path);
        if (str.good()) {
            tao::json::to_stream(str, tao::json::value(m_cache));
        } else {
            spdlog::error("cannot write cache to {}", m_path);
        }
    } catch (const std::exception &e) {
        spdlog::error("cannot write cache to {}: {}", m_path, e.what());
    }
}

bool mpdfm::as20::do_check_preconditions(const scrobble_entry &s) {
    auto played = std::min<unsigned>(240, s.duration / 2);  // NOLINT magic num
    return s.duration > 30 && s.elapsed > played;           // NOLINT magic num
}

void mpdfm::as20::do_send_now_playing(const scrobble_entry &s) {
    if (m_fail_flag) {
        throw std::runtime_error("one (or more) previous scrobbles failed");
    }

    audioscrobbler_request req(m_api_secret);
    req["method"]  = "track.updateNowPlaying";
    req["api_key"] = m_api_key;
    req["sk"]      = m_session_key;
    req.add_track(s);

    using boost::beast::http::string_body;
    using boost::beast::http::verb;
    auto http = http_request<string_body, string_body>::make(
        m_target, io_context(), ssl_context());

    http->request().body() = req.form();
    http->request().method(verb::post);

    http->run([](auto http, auto ec) {
        if (ec) {
            spdlog::error("request error when sending now playing: {}", ec);
            return;
        }
        auto code = http->response().result_int();
        // NOLINTNEXTLINE non-success codes
        if (code < 200 || code > 299) {
            spdlog::error("now playing send failed, status: {}", code);
        }
    });
}

void mpdfm::as20::do_send_scrobble(const scrobble_entry &s) {
    {
        std::unique_lock lock(m_cache_mutex);
        m_cache.insert(s);
    }
    send_scrobbles_coalesced();
}

void mpdfm::as20::send_scrobbles_coalesced() {
    const auto batch_size = 50;
    if (m_fail_flag) {
        throw std::runtime_error("one (or more) previous scrobbles failed");
    }
    std::unique_lock l(m_cache_mutex);
    if (m_cache.empty()) {
        return;
    }

    audioscrobbler_request req(m_api_secret);
    req["method"]  = "track.scrobble";
    req["api_key"] = m_api_key;
    req["sk"]      = m_session_key;

    auto to_send = std::make_shared<std::vector<scrobble_entry>>();
    to_send->reserve(batch_size);

    for (int i = 0; i < batch_size && !m_cache.empty(); i++) {
        auto &x = to_send->emplace_back(
            std::move(m_cache.extract(m_cache.begin()).value()));

        std::string suffix = '[' + std::to_string(i) + ']';
        req.add_track(x, suffix);
        req["timestamp" + suffix] = std::to_string(x.timestamp);
    }

    using boost::beast::http::string_body;
    using boost::beast::http::verb;
    auto http = http_request<string_body, string_body>::make(
        m_target, io_context(), ssl_context());

    http->request().body() = req.form();
    http->request().method(verb::post);
    http->run([this, to_send](auto http, auto ec) {
        using tao::json::from_string;
        try {
            if (ec) {
                throw boost::system::system_error(ec, "http failure");
            }
            auto &r        = http->response();
            const auto val = from_string(r.body()).template as<response>();
            if (!val.message.empty()) {
                switch (val.error) {
                default: {
                    std::unique_lock l(m_cache_mutex);
                    m_fail_flag = true;
                }
                // fall through
                case 11:  // NOLINT service offline
                case 16:  // NOLINT temp unavailable
                    throw std::runtime_error("api returned an error: "
                                             + val.message);
                    break;
                }
            }

            try {
                // continue sending scrobbles until another error occurs,
                // or there are no scrobbles left to send
                send_scrobbles_coalesced();
            } catch (...) {
                // ignore exceptions. they will be rethrown just the same
                // next time
            }
        } catch (const std::exception &e) {
            // for the case of a JSON parse error it's fair to assume the
            // same as cases 11 and 16: the API is malfunctioning
            std::unique_lock l(m_cache_mutex);
            m_cache.insert(to_send->begin(), to_send->end());
            spdlog::error("scrobble fail: {}", e.what());
        }
    });
}

// factory

namespace {
    // hard coded: these are apparently public everywhere.
    const std::string_view default_api_secret =
        "da9cf6b88d9a7262517958d7535e61e0";
    const std::string_view default_api_key =
        "72f47a2e17a2c43d4e284d35939c791f";
    const std::string_view default_target =
        "https://ws.audioscrobbler.com/2.0/";
}  // namespace

mpdfm::scrobbler *mpdfm::as20::factory::do_fabrication(
    const mpdfm::config_section &section) {
    // required
    std::string session_key = section.value("session");
    auto path               = section.value("store", {});

    auto target     = section.value("url", std::string(default_target));
    std::string api_key(default_api_key);
    std::string api_secret(default_api_secret);

    if (section.has_value("api_secret") || section.has_value("api_key")) {
        // require either none or both of these, since providing one is usually
        // an error on the user's part
        api_secret = section.value("api_secret");
        api_key    = section.value("api_key");
    }

    return new as20(session_key, api_secret, api_key, target, path);
}

namespace {
    std::string get_token(const mpdfm::uri &uri, const std::string &api_key) {
        using boost::beast::http::empty_body;
        using boost::beast::http::string_body;
        using boost::beast::http::verb;
        using mpdfm::http_request;
        using mpdfm::io_context;
        using mpdfm::ssl_context;
        using mpdfm::token_response;

        auto encoded_key = mpdfm::urlencode(api_key);
        std::promise<token_response> result_promise;
        auto future = result_promise.get_future();

        std::string target;
        // "&method=auth.getToken&format=json&api_key=" == 9 chars
        constexpr size_t param_len = 9;
        target.reserve(uri.path_and_query().size() + encoded_key.size()
                       + param_len);
        target += uri.path_and_query();
        target += uri.has_query() ? '&' : '?';
        target += "method=auth.getToken&format=json&api_key=" + encoded_key;

        auto http = http_request<empty_body, string_body>::make(
            uri, io_context(), ssl_context());

        http->request().target(target);
        http->run(
            [&result_promise](auto http, auto ec) {
                using tao::json::from_string;
                try {
                    if (ec) {
                        throw boost::system::system_error(
                            ec, "token request get failed");
                    }
                    auto v = from_string(http->response().body())
                                 .template as<token_response>();
                    if (!v.message.empty()) {
                        throw std::runtime_error("last.fm api error: "
                                                 + v.message);
                    }
                    result_promise.set_value(std::move(v));
                } catch (...) {
                    result_promise.set_exception(std::current_exception());
                }
            });

        return future.get().token;
    }

    std::string get_session(const mpdfm::uri &uri,
                            const std::string &api_key,
                            const std::string &api_secret,
                            const std::string &token) {
        using boost::beast::http::string_body;
        using boost::beast::http::verb;
        using mpdfm::http_request;
        using mpdfm::io_context;
        using mpdfm::session_response;
        using mpdfm::ssl_context;

        auto encoded_key = mpdfm::urlencode(api_key);
        std::promise<session_response> result_promise;
        auto future = result_promise.get_future();

        audioscrobbler_request req(api_secret);
        req["method"]  = "auth.getSession";
        req["api_key"] = api_key;
        req["token"]   = token;

        auto http = http_request<string_body, string_body>::make(
            uri, io_context(), ssl_context());

        http->request().method(verb::post);
        http->request().body() = req.form();
        http->run(
            [&result_promise](auto http, auto ec) {
                using tao::json::from_string;
                try {
                    if (ec) {
                        throw boost::system::system_error(
                            ec, "failed to get session");
                    }
                    auto v = from_string(http->response().body())
                                 .template as<session_response>();
                    if (!v.message.empty()) {
                        throw std::runtime_error("last.fm api error: "
                                                 + v.message);
                    }
                    result_promise.set_value(std::move(v));
                } catch (...) {
                    result_promise.set_exception(std::current_exception());
                }
            });

        return future.get().session.key;
    }
}  // namespace

void mpdfm::as20::factory::do_authenticate(int argc, const char **argv) {
    gsl::span<const char *> args(argv, argc);
    spdlog::debug("starting auth process");
    std::string target(default_target);
    std::string api_key(default_api_key);
    std::string api_secret(default_api_secret);

    switch (argc) {
    case 4:
        api_key    = args[2];
        api_secret = args[3];
        // fall through
    case 2:
        target = args[1];
        // fall through
    case 1:
        break;
    default:
        spdlog::error(
            "invalid auth usage (wrong argument count)"
            "{} [target_url] [api_key api_secret]",
            args[0]);
        return;
    }
    mpdfm::uri target_uri(target);

    std::string token;
    try {
        token = get_token(target_uri, api_key);
    } catch (const std::exception &e) {
        spdlog::error("failed to get token: {}", e.what());
        return;
    }

    spdlog::info(
        "to authenticate, open "
        "https://www.last.fm/api/auth?api_key={}&token={}\n"
        "and press enter\n"
        "NOTE: your URL could be different, depending on your service",
        api_key,
        token);

    // requires waiting for the user to finish authentication
    std::cin.get();

    try {
        auto session = get_session(target_uri, api_key, api_secret, token);
        spdlog::info("your session: {}", session);
    } catch (const std::exception &e) {
        spdlog::error(
            "failed to get session: {}\n"
            "your token was: {}",
            e.what(),
            token);
    }
}
