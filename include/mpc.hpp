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
#ifndef MPC_HPP
#define MPC_HPP

#include <memory>
#include <mpd/client.h>
#include <string>

namespace mpdfm {
    /*!
     * \brief Wrapper around mpd status
     */
    struct status {
        /*!
         * \brief Ownership taking constructor
         */
        explicit status(::mpd_status *status);

        /*!
         * \returns Player state
         */
        [[nodiscard]] mpd_state state() const;

        /*!
         * \returns Elapsed time of the current song
         */
        [[nodiscard]] unsigned elapsed_time() const;

    private:
        std::shared_ptr<::mpd_status> m_status;
    };

    /*!
     * \brief Wrapper around songs
     */
    struct song {
        /*!
         * \brief Ownership taking constructor
         */
        explicit song(mpd_song *song);

        /*!
         * \brief Retrieves the requested tag from the server
         * \param type Which tag to retrieve
         * \param idx Used for iterating over multiple instances under the same
         *            tag.
         */
        [[nodiscard]] std::string tag(mpd_tag_type type,
                                      unsigned idx = 0) const;

        /**
         * \brief Returns the id of this song
         */
        [[nodiscard]] unsigned id() const;

        /*!
         * \brief Returns the song's duration
         */
        [[nodiscard]] unsigned duration() const;

        /*!
         * \brief Returns the song's position
         */
        [[nodiscard]] unsigned pos() const;

        /*!
         * \brief Checks whether the song is valid
         */
        [[nodiscard]] explicit operator bool() const;

        /*!
         * \brief Compares IDs of two songs as a simple is-same check.
         * \returns other.id() == id()
         */
        bool operator==(const song &other) const;

        /*!
         * \returns !operator==(other)
         */
        bool operator!=(const song &other) const;

    private:
        std::shared_ptr<::mpd_song> m_song;
    };
    /*!
     * \brief Wrapper around a libmpdclient connection
     */
    struct mpd_connection {
        /*!
         * \brief Creates an mpd_connection and connects
         *
         * The three arguments present here can be left out for their default
         * values as described by mpd_connection_new
         *
         * \param address The address the MPD server can be found on. If left
         *                blank it will resolve to the MPD_HOST environmental
         *                variable.
         * \param port The port the MPD server can be found on, or zero to
         *             resolve to $MPD_PORT.
         * \param timeout Connection timeout
         */
        explicit mpd_connection(const std::string &address = std::string(),
                                unsigned port              = 0,
                                unsigned timeout           = 0);
        ~mpd_connection() = default;

        mpd_connection(const mpd_connection &other) = delete;
        mpd_connection &operator=(const mpd_connection &other) = delete;

        mpd_connection(mpd_connection &&other) = default;
        mpd_connection &operator=(mpd_connection &&other) = default;

        /*!
         * \brief Goes into idle mode and waits for an event
         * \param mask Event mask
         */
        mpd_idle run_idle_mask(mpd_idle mask);

        /*!
         * \brief Sends noidle to MPD to interrupt waiting
         */
        void send_noidle();

        /*!
         * \brief Retrieves the currently playing song from the server
         */
        [[nodiscard]] song run_current_song() const;

        /*!
         * \brief Retrieves the current status of the player from the server
         */
        [[nodiscard]] status run_status() const;

        // NOTE add methods as they are needed
    private:
        std::shared_ptr<::mpd_connection> m_connection = nullptr;
    };
}  // namespace mpdfm

#endif // MPC_HPP
