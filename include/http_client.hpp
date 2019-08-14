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
#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <algorithm>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <cctype>
#include <charconv>
#include <functional>
#include <gsl/gsl>
#include <memory>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <uris.hpp>

namespace mpdfm {
    //! \brief Callback type for protocols
    using proto_callback_t = std::function<void(boost::system::error_code)>;
    //! \brief TCP resolver results
    using resolve_result   = boost::asio::ip::tcp::resolver::results_type;
    //! \brief TCP endpoints
    using tcp_endpoint     = boost::asio::ip::tcp::endpoint;

    /*!
     * \brief Implements a protocol polymorphiclly in order to support both
     *        HTTP and HTTPS.
     *
     * This abstract class provides methods for writing and reading to asio
     * streams
     *
     * \tparam ReqBody the request body type
     * \tparam ResBody the response body type
     */
    template<typename ReqBody, typename ResBody>
    struct protocol {  // NOLINT virtual destructor
        using request  = boost::beast::http::request<ReqBody>;
        using response = boost::beast::http::response<ResBody>;

        virtual void connect(const resolve_result &res,
                             proto_callback_t callback)             = 0;
        virtual void connect(const tcp_endpoint &res,
                             proto_callback_t callback)             = 0;
        virtual void write(request &req, proto_callback_t callback) = 0;
        virtual void read(response &res, proto_callback_t callback) = 0;
        virtual std::string_view default_port()                     = 0;

        virtual ~protocol() = default;
    };

    //

    /*!
     * \brief Converts a std::string_view into the boost equivalent
     */
    inline boost::string_view to_boost_view(const std::string_view &view) {
        return { view.data(), view.size() };
    }

    template<typename ReqBody, typename ResBody>
    gsl::owner<protocol<ReqBody, ResBody> *>
        get_proto(const mpdfm::uri &uri,
                  boost::asio::io_context &io,
                  boost::asio::ssl::context &ssl);

    /*!
     * \brief Performs a HTTP(S) request
     * \tparam ReqBody the request body type
     * \tparam ResBody the response body type
     */
    template<typename ReqBody, typename ResBody>
    class http_request
        : public std::enable_shared_from_this<http_request<ReqBody, ResBody>> {
        using this_type  = http_request<ReqBody, ResBody>;
        using io_context = boost::asio::io_context;
        using ssl_context = boost::asio::ssl::context;
        using error_code = boost::system::error_code;

    public:
        /*!
         * \brief Convenience wrapper around std::make_shared
         * \returns A new shared_ptr of the request
         */
        template<typename... ReqArgs>
        static auto make(ReqArgs &&... Argss) {
            return std::make_shared<this_type>(std::forward<ReqArgs>(Argss)...);
        }

        /*!
         * \brief Creates a http_request and it's protocol
         *
         * \param uri Target URI
         * \param io io_context to use
         * \param ssl ssl::context to use for https
         */
        http_request(const uri &uri, io_context &io, ssl_context &ssl)
            : http_request(
                  uri, io, get_proto<ReqBody, ResBody>(uri, io, ssl)) {}

        /*!
         * \brief Creates a http_request using a user-provided protocol
         *
         * \param uri Target URI
         * \param io io_context to use
         * \param proto Protocol to use for connecting and rw
         */
        http_request(uri uri,
                     io_context &io,
                     gsl::owner<protocol<ReqBody, ResBody> *> proto)
            : m_uri(std::move(uri)),
              m_proto(proto),
              m_resolver(io) {
            using boost::beast::http::verb;
            using namespace std::string_view_literals;
            m_req.method(verb::get);

            auto target = m_uri.path_and_query();
            if (target.empty()) {
                m_req.target(to_boost_view("/"sv));
            } else {
                m_req.target(to_boost_view(target));
            }

            using boost::beast::http::field;
            m_req.set(field::host, m_uri.host());
            m_req.set(field::user_agent, "mpdfm");
        }

        //! \brief Request getter
        auto &request() { return m_req; }

        //! \brief Response getter
        auto &response() { return m_res; }

        /*!
         * \brief Runs the http_request and calls the callback upon completion
         *
         * NOTE: The callback is stored in a std::function. KEEP THIS IN MIND.
         *       Do NOT give run() a function object that holds a shared_ptr to
         *       the request, it will lead to a memory error, as the cleanup of
         *       a request fully depends on std::shared_ptr.
         *
         * \tparam CallbackType The type of the callback (derived)
         * \param ct Callback
         */
        template<typename CallbackType>
        void run(CallbackType ct) {
            m_callback = std::move(ct);
            m_req.prepare_payload();
            spdlog::debug("DEBUG(http_client):\n{}", m_req);

            auto port = m_uri.port();
            if (port.empty()) {
                port = m_proto->default_port();
            }
            using boost::asio::ip::make_address;

            if (m_uri.is_ip()) {
                unsigned short result;
                auto [p, ec] =
                    std::from_chars(port.data(),
                                    port.data() + port.size(),  // NOLINT safe
                                    result);
                if (ec != std::errc()) {
                    throw std::runtime_error("port parse failed");
                }
                m_proto->connect({ make_address(m_uri.host()), result },
                                 [http = this->shared_from_this()](auto ec) {
                                     http->connect_callback(ec);
                                 });
            } else {
                m_resolver.async_resolve(  // formatter, please
                    m_uri.host(),
                    port,
                    [this, http = this->shared_from_this()](auto err,
                                                            auto result) {
                        if (err) {
                            m_callback(http, err);
                        } else {
                            http->m_proto->connect(result, [http](auto ec) {
                                http->connect_callback(ec);
                            });
                        }
                    });
            }
        }

        //! \brief Gets the URI this request was constructed with
        [[nodiscard]] const mpdfm::uri &get_uri() const { return m_uri; }

    private:
        void connect_callback(error_code ec) {
            auto http = this->shared_from_this();
            if (ec) {
                m_callback(std::move(http), ec);
            } else {
                m_proto->write(m_req, [http = std::move(http)](auto ec) {
                    http->handle_read(ec);
                });
            }
        }

        void handle_read(error_code ec) {
            auto http = this->shared_from_this();
            if (ec) {
                m_callback(std::move(http), ec);
            } else {
                m_proto->read(m_res, [http = std::move(http)](auto ec) {
                    spdlog::debug("DEBUG(http_client):\n{}", http->response());
                    http->m_callback(std::move(http), ec);
                });
            }
        }

        mpdfm::uri m_uri;
        std::function<void(std::shared_ptr<this_type>, error_code)> m_callback;
        std::unique_ptr<protocol<ReqBody, ResBody>> m_proto;
        boost::asio::ip::tcp::resolver m_resolver;

        boost::beast::http::request<ReqBody> m_req;
        boost::beast::http::response<ResBody> m_res;
    };

    //

    template<typename ReqBody, typename ResBody>
    struct https_protocol  // NOLINT virtual
        : public protocol<ReqBody, ResBody> {
        using request  = boost::beast::http::request<ReqBody>;
        using response = boost::beast::http::response<ResBody>;

        https_protocol(boost::asio::io_context &io,
                       boost::asio::ssl::context &ssl,
                       std::string host)
            : m_stream(io, ssl), m_host(std::move(host)) {}

        void connect(const resolve_result &res, proto_callback_t cb) override {
            connect_impl(res, std::move(cb));
        };

        void connect(const tcp_endpoint &res, proto_callback_t cb) override {
            connect_impl(res, std::move(cb));
        };

        template<typename ConnectParam>
        void connect_impl(const ConnectParam &cp, proto_callback_t cb) {
            if (!SSL_set_tlsext_host_name(m_stream.native_handle(),
                                          m_host.c_str())) {
                cb({ static_cast<int>(ERR_get_error()),
                     boost::asio::error::get_ssl_category() });
                return;
            }

            auto callback = [cb = std::move(cb), this](auto err) mutable {
                if (err) {
                    cb(err);
                } else {
                    do_handshake(std::move(cb));
                }
            };

            using boost::asio::async_connect;
            if constexpr (std::is_same<ConnectParam, tcp_endpoint>()) {
                m_stream.next_layer().async_connect(cp, callback);
            } else {
                async_connect(
                    m_stream.next_layer(),
                    cp,
                    [callback = std::move(callback)](
                        auto ec, auto /*endpoint*/) mutable { callback(ec); });
            }
        }

        void write(request &req, proto_callback_t callback) override {
            boost::beast::http::async_write(
                m_stream,
                req,
                [cb = std::move(callback)](auto ec, auto /*size*/) {
                    cb(ec);
                });
        }

        void read(response &res, proto_callback_t callback) override {
            boost::beast::http::async_read(
                m_stream,
                m_buf,
                res,
                [cb = std::move(callback)](auto ec, auto /*size*/) {
                    cb(ec);
                });
        }

        std::string_view default_port() override {
            using namespace std::string_view_literals;
            return "443"sv;
        }

        template<typename... Params>
        static gsl::owner<protocol<ReqBody, ResBody> *>
            make(Params &&... Paramss) {
            return new https_protocol<ReqBody, ResBody>(
                std::forward<Params>(Paramss)...);
        }

        ~https_protocol() override = default;

    private:
        void do_handshake(proto_callback_t cb) {
            m_stream.async_handshake(
                boost::asio::ssl::stream_base::client,
                [cb = std::move(cb)](auto err) { cb(err); });
        }

        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_stream;
        boost::beast::flat_buffer m_buf;
        std::string m_host;
    };

    template<typename ReqBody, typename ResBody>
    struct http_protocol  // NOLINT virtual
        : public protocol<ReqBody, ResBody> {
        using request  = boost::beast::http::request<ReqBody>;
        using response = boost::beast::http::response<ResBody>;
        using http_t   = std::shared_ptr<http_request<ReqBody, ResBody>>;

        explicit http_protocol(boost::asio::io_context &io) : m_socket(io) {}

        void connect(const resolve_result &res, proto_callback_t cb) override {
            boost::asio::async_connect(
                m_socket,
                res,
                [cb = std::move(cb)](auto &ec, auto /*endpoint*/) { cb(ec); });
        };

        void connect(const tcp_endpoint &res, proto_callback_t cb) override {
            m_socket.async_connect(res, cb);
        };

        void write(request &req, proto_callback_t callback) override {
            boost::beast::http::async_write(
                m_socket,
                req,
                [cb = std::move(callback)](auto ec, auto /*size*/) {
                    cb(ec);
                });
        }

        void read(response &res, proto_callback_t callback) override {
            boost::beast::http::async_read(
                m_socket,
                m_buf,
                res,
                [cb = std::move(callback)](auto ec, auto /*size*/) {
                    cb(ec);
                });
        }

        std::string_view default_port() override {
            using namespace std::string_view_literals;
            return "80"sv;
        }

        template<typename... Params>
        static gsl::owner<protocol<ReqBody, ResBody> *>
            make(Params &&... Paramss) {
            return new http_protocol<ReqBody, ResBody>(
                std::forward<Params>(Paramss)...);
        }

        ~http_protocol() override = default;

    private:
        boost::asio::ip::tcp::socket m_socket;
        boost::beast::flat_buffer m_buf;
    };

    /*!
     * \brief Equality compares two strings converted to lowercase
     *
     * \returns true if the strings, when lowercased, are equal
     */
    template<typename StringType>
    bool streq_insensitive(const StringType &a, const StringType &b) {
        return std::equal(
            a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](auto &a, auto &b) {
                return std::tolower(a) == std::tolower(b);
            });
    }

    /*!
     * \brief Protocol factory
     *
     * \param uri The URI to choose the implementation based on
     * \param io the IO context the implementation will use
     * \param ssl the SSL context the implementation may use
     *
     * \return An owning pointer to the protocol, based on uri.scheme()
     */
    template<typename ReqBody, typename ResBody>
    gsl::owner<protocol<ReqBody, ResBody> *>
        get_proto(const mpdfm::uri &uri,
                  boost::asio::io_context &io,
                  boost::asio::ssl::context &ssl) {
        using namespace std::literals::string_view_literals;
        auto proto_str = uri.scheme();
        if (streq_insensitive(proto_str, "https"sv)) {  // NOLINT magic strings
            return new https_protocol<ReqBody, ResBody>(
                io, ssl, std::string(uri.host()));
        }
        if (streq_insensitive(proto_str, "http"sv)) {  // NOLINT magic strings
            return new http_protocol<ReqBody, ResBody>(io);
        }
        throw std::runtime_error("unsupported protocol");
    }

    // singletons

    //! \returns An io_context instance
    boost::asio::io_context &io_context();
    //! \returns A ssl::context instance
    boost::asio::ssl::context &ssl_context();

}  // namespace mpdfm

#endif // HTTP_CLIENT_HPP
