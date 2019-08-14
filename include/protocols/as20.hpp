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
#ifndef PROTOCOLS_AS20_HPP
#define PROTOCOLS_AS20_HPP

#include "../scrobbler.hpp"

#include <config/config_file.hpp>
#include <mutex>
#include <set>
#include <uris.hpp>

    namespace mpdfm {
    namespace internal {
        struct ts_compare {
            template<typename T>
            bool operator()(const T &lhs, const T &rhs) const {
                return lhs.timestamp < rhs.timestamp;
            }
        };
    }  // namespace internal

    /*!
     * \brief AudioScrobbler 2.0 implementation
     */
    struct as20 : public scrobbler {  // NOLINT virtual destructor
        struct factory                // NOLINT virtual destructor
            : public scrobbler_factory {
            ~factory() override = default;

        protected:
            gsl::owner<scrobbler *>
                do_fabrication(const config_section &section) override;
            void do_authenticate(int argc, const char **argv) override;
        };

        as20(std::string sk,
             std::string as,
             std::string ak,
             const std::string &tu,
             std::string sp);
        ~as20() override;

    protected:
        void do_send_now_playing(const scrobble_entry &s) override;
        void do_send_scrobble(const scrobble_entry &s) override;
        bool do_check_preconditions(const scrobble_entry &s) override;

    private:
        void send_scrobbles_coalesced();
        // gets set to true when send_scrobbles_coalesced has failed fatally
        bool m_fail_flag = false;

        std::string m_session_key;
        std::string m_api_key;
        std::string m_api_secret;  // "shared" secret
        uri m_target;

        std::set<scrobble_entry, internal::ts_compare> m_cache;
        std::mutex m_cache_mutex;
        std::string m_path;
    };
}  // namespace mpdfm

#endif // PROTOCOLS_AS20_HPP
