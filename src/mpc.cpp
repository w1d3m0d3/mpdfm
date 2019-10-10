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
#include <mpc.hpp>

#include <cstdlib>
#include <mpd/client.h>
#include <string>

namespace {
    template<typename Expressed>
    auto check_error(Expressed &&expressed, const mpd_connection *conn) {
        auto err = mpd_connection_get_error(conn);
        if (err != MPD_ERROR_SUCCESS) {
            throw std::runtime_error(
                std::string(mpd_connection_get_error_message(conn)));
        }
        return std::forward<Expressed>(expressed);
    }

    template<typename Expressed>
    auto check_error(Expressed &&expressed,
                     const std::shared_ptr<mpd_connection> &conn) {
        return check_error(std::forward<Expressed>(expressed), conn.get());
    }

    auto check_error(mpd_connection *conn) {
        try {
            return check_error(conn, conn);
        } catch (...) {
            mpd_connection_free(conn);
            throw;
        }
    }
}  // namespace

mpdfm::mpd_connection::mpd_connection(const std::string &address,
                                      unsigned port,
                                      unsigned timeout) {
    auto caddr = address.c_str();
    if (address.empty()) {
        // Assure the use of $MPD_HOST if no value was provideed
        caddr = nullptr;
    }
    m_connection = std::shared_ptr<::mpd_connection>(
        check_error(mpd_connection_new(caddr, port, timeout)),
        mpd_connection_free);
}

mpd_idle mpdfm::mpd_connection::run_idle_mask(mpd_idle mask) {
    return check_error(mpd_run_idle_mask(m_connection.get(), mask),
                       m_connection);
}

mpdfm::song mpdfm::mpd_connection::run_current_song() const {
    return mpdfm::song(
        check_error(mpd_run_current_song(m_connection.get()), m_connection));
}

mpdfm::status mpdfm::mpd_connection::run_status() const {
    return mpdfm::status(
        check_error(mpd_run_status(m_connection.get()), m_connection));
}

bool mpdfm::mpd_connection::run_password(std::string_view pass) const {
    return check_error(mpd_run_password(m_connection.get(), pass.data()),
                       m_connection.get());
}

void mpdfm::mpd_connection::send_noidle() {
    check_error(mpd_send_noidle(m_connection.get()), m_connection.get());
}

mpdfm::song::song(::mpd_song *song)
    : m_song(std::shared_ptr<::mpd_song>(song, [](auto p) {
          if (p) {
              mpd_song_free(p);
          }
      })) {}

unsigned mpdfm::song::id() const {
    return mpd_song_get_id(m_song.get());
}

std::string mpdfm::song::tag(mpd_tag_type type, unsigned idx) const {
    auto val = mpd_song_get_tag(m_song.get(), type, idx);
    if (bool(val)) {
        return val;
    }
    return {};
}

unsigned mpdfm::song::duration() const {
    return mpd_song_get_duration(m_song.get());
}

mpdfm::song::operator bool() const {
    return bool(m_song.get());
}

unsigned mpdfm::song::pos() const {
    return mpd_song_get_pos(m_song.get());
}

bool mpdfm::song::operator==(const song &other) const {
    return (bool(other) == bool(*this)) && (other.id() == id());
}

bool mpdfm::song::operator!=(const song &other) const {
    return !operator==(other);
}

mpdfm::status::status(::mpd_status *status)
    : m_status(std::shared_ptr<::mpd_status>(status, mpd_status_free)) {}

mpd_state mpdfm::status::state() const {
    return mpd_status_get_state(m_status.get());
}

unsigned mpdfm::status::elapsed_time() const {
    return mpd_status_get_elapsed_time(m_status.get());
}
