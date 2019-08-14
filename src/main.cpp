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
#include "spdlog/common.h"
#include <algorithm>
#include <directory_helper.hpp>
#include <filesystem>
#include <gsl/gsl>
#include <http_client.hpp>
#include <iostream>
#include <mpc.hpp>
#include <protocols/as20.hpp>
#include <scrobbler.hpp>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>
#include <tao/pegtl/parse_error.hpp>

namespace {
    bool interrupted    = false;
    using scrobbler_vec = std::vector<std::unique_ptr<mpdfm::scrobbler>>;

    template<typename Task>
    void run_scrobbler_task(scrobbler_vec &scrobblers, Task task) {
        for (auto i = scrobblers.begin(); i != scrobblers.end();) {
            try {
                task(*i);
                i++;
            } catch (const std::exception &e) {
                spdlog::error("scrobbler operation failed: ", e.what());
                i = scrobblers.erase(i);
            }
        }
        if (scrobblers.empty()) {
            throw std::runtime_error("no scrobblers left");
        }
    }

    class state_tracker {
        mpdfm::song m_song { nullptr };
        time_t m_start = 0;

        // time keeping
        time_t m_last_play = 0;
        time_t m_elapsed   = 0;
        bool m_paused      = false;

    public:
        void pause() {
            if (!m_paused) {
                m_paused = true;
                m_elapsed += time(nullptr) - m_last_play;
            }
        }

        void play() {
            if (m_paused) {
                m_paused    = false;
                m_last_play = time(nullptr);
            }
        }

        void new_song(mpdfm::song &song) {
            m_song  = song;
            m_start = time(nullptr);

            m_elapsed   = 0;
            m_last_play = 0;
            play();
        }

        time_t elapsed() { return m_elapsed; }
        time_t start() { return m_start; }
        const mpdfm::song &song() { return m_song; }

        void set_elapsed(time_t elapsed) { m_elapsed = elapsed; }
    };

    void handle_player_event(mpdfm::mpd_connection &conn,
                             state_tracker &last,
                             scrobbler_vec &scrobblers) {
        auto status  = conn.run_status();
        auto current = conn.run_current_song();

        if (status.state() == MPD_STATE_PLAY) {
            last.play();
        } else {
            last.pause();
        }

        // on song change
        if (current != last.song()) {
            if (last.song()) {
                last.pause();
                mpdfm::scrobble_entry scr(last.song());
                scr.timestamp = last.start();
                scr.elapsed   = last.elapsed();

                run_scrobbler_task(scrobblers, [&scr](auto &x) {
                    if (x->check_preconditions(scr)) {
                        x->scrobble(scr);
                    }
                });
            }

            if (current) {
                mpdfm::scrobble_entry entry { current };
                last.new_song(current);
                run_scrobbler_task(
                    scrobblers, [&entry](auto &x) { x->now_playing(entry); });
            }
        }
    }

    using factory_ptr = std::unique_ptr<mpdfm::scrobbler_factory>;
    mpdfm::scrobbler_factory &get_factory(const std::string &name) {
        static std::map<std::string, factory_ptr> factories;
        if (factories.empty()) {
            // NOLINTNEXTLINE clear ownership passing
            factories.emplace("as20", new mpdfm::as20::factory());
        }
        return (*factories.at(name));
    }

    namespace io = boost::asio;

    void run_scrobblers(const std::string &host,
                        short port,
                        scrobbler_vec &scrobblers) {
        try {
            mpdfm::mpd_connection conn(host, port);

            state_tracker last;

            // graceful exits
            io::signal_set signals(mpdfm::io_context(), SIGINT, SIGTERM);
            signals.async_wait([&conn](auto ec, auto /*signal*/) {
                interrupted = true;
                if (!ec) {
                    // if the signal set is aborted that means conn fell out of
                    // scope
                    conn.send_noidle();
                }
            });

            auto song   = conn.run_current_song();
            auto status = conn.run_status();
            if (song && status.state() == MPD_STATE_PLAY) {
                last.set_elapsed(status.elapsed_time());
                last.new_song(song);
                mpdfm::scrobble_entry entry { song };
                run_scrobbler_task(
                    scrobblers, [&entry](auto &x) { x->now_playing(entry); });
            }

            while (!interrupted) {
                if (auto ev = conn.run_idle_mask(MPD_IDLE_PLAYER)) {
                    if ((ev & MPD_IDLE_PLAYER) != 0) {
                        spdlog::debug("received player event");
                        handle_player_event(conn, last, scrobblers);
                    } else {
                        spdlog::error("received unknown event: {:x}", ev);
                    }
                }
            }
        } catch (const std::exception &e) {
            spdlog::error("fatal error: {}", e.what());
        }
    }
}  // namespace

int main(int arg_count,
         const char **arg_vec) {  // NOLINT let exceptions terminate
    using namespace std::string_view_literals;
    if (arg_count > 1 && arg_vec[1] == "-v"sv) {  // NOLINT
        arg_count--;
        arg_vec++;  // NOLINT
        spdlog::set_level(spdlog::level::debug);
    } else {
        spdlog::set_level(spdlog::level::info);
    }

    gsl::span<const char *> args(arg_vec, arg_count);
    std::thread worker { []() { mpdfm::io_context().run(); } };
    auto guard       = io::make_work_guard(mpdfm::io_context());
    using guard_type = decltype(guard);

    class raii_guard {  // NOLINT adding a cp/mv contructor/assignment is
                        // useless
        guard_type &m_guard;
        std::thread &m_worker;

    public:
        ~raii_guard() {
            m_guard.reset();
            m_worker.join();
        }

        raii_guard(guard_type &g, std::thread &w) : m_guard(g), m_worker(w) {}
    } raii_guard(guard, worker);

    if (args.size() >= 3) {
        std::string_view arg(args[1]);
        if (arg == "auth") {
            try {
                // NOLINTNEXTLINE main-like interface
                get_factory(args[2]).authenticate(arg_count - 2, arg_vec + 2);
            } catch (const std::exception &e) {
                spdlog::error("authentication process failure: {}", e.what());
            }
        } else {
            spdlog::error("invalid command");
            return 1;
        }
    } else {
        // parse config
        mpdfm::config_file cfg;
        std::string host;
        int port;

        try {
            // find location of config file
            boost::filesystem::path path;
            if (args.size() >= 2) {
                path = args[1];
            } else {
                path = mpdfm::get_config_path() / "mpdfm/mpdfm.cfg";
            }

            cfg  = mpdfm::config_file(path.native());
            port = std::stoi(cfg.root_section().value("mpd_port", "6600"));
            host = cfg.root_section().value("mpd_host", "localhost");
        } catch (const std::system_error &e) {
            spdlog::error("failed to open configuration file: {}", e.what());
            return 1;
        } catch (const tao::pegtl::parse_error &e) {
            spdlog::error("config parse error: {}", e.what());
            return 1;
        } catch (const std::exception &e) {
            spdlog::error("exception raised while loading configs: {}",
                          e.what());
            return 1;
        }

        // construct all the scrobblers
        scrobbler_vec scrobblers;
        for (auto &sec : cfg.sections()) {
            try {
                // NOLINTNEXTLINE unique_ptr is owning
                scrobblers.emplace_back(get_factory(sec.name())(sec));
            } catch (const std::exception &e) {
                spdlog::error("got an error while setting up scrobbler: ",
                              e.what());
            }
        }

        if (scrobblers.empty()) {
            spdlog::error("no scrobblers set up");
            return 1;
        }

        run_scrobblers(host, gsl::narrow_cast<short>(port), scrobblers);
    }
}
