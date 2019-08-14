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
#ifndef SCROBBLER_HPP
#define SCROBBLER_HPP

#include "mpc.hpp"

#include <config/config_file.hpp>
#include <gsl/gsl>
#include <tao/json/binding.hpp>

namespace mpdfm {
    /*!
     * \brief Trivial wrapper around songs to add a timestamp
     */
    struct scrobble_entry {
        /*!
         * \brief Constructs an empty scrobble_entry object.
         */
        scrobble_entry() = default;

        /*!
         * \brief Fills out this scrobble entry with info from song
         */
        explicit scrobble_entry(const song &song);

        /*!
         * \brief Track artist
         */
        std::string artist;

        /*!
         * \brief Track title
         */
        std::string track;

        /*!
         * \brief Track album
         */
        std::string album;

        /*!
         * \brief Track number
         */
        std::string track_number;

        /*!
         * \brief MusicBrainz ID
         */
        std::string mbid;

        /*!
         * \brief Album artist
         */
        std::string album_artist;

        /*!
         * \brief How long is the track (seconds)
         */
        time_t duration = 0;

        /*!
         * \brief When the song started playing
         */
        time_t timestamp = 0;

        /*!
         * \brief how long the song was playing for
         */
        time_t elapsed = 0;
    };

    /*!
     * \brief scrobbler client
     * This class is meant to be inherited from to implement the underlying
     * scrobbler protocol.
     */
    struct scrobbler {  // NOLINT interface class
        virtual ~scrobbler() = default;

        /*!
         * \brief Sends/saves a scrobble
         * \param song Song to scrobble
         */
        void scrobble(const scrobble_entry &song);

        /*!
         * \brief updates the scrobble server with the currently playing song
         *
         * For last.fm this endpoint is optional but recommended.
         * Failed requests should only be debug logged.
         *
         * \param song The song to send playing now for
         */
        void now_playing(const scrobble_entry &song);

        /*!
         * \brief Checks whether the scribble conditions have been met yet
         * \param song The song to check for
         * \return true if they have
         */
        bool check_preconditions(const scrobble_entry &song);

    protected:
        /*!
         * \brief updates the scrobble server with the currently playing song
         *
         * For last.fm this endpoint is optional but recommended.
         * Failed requests should only be debug logged.
         */
        virtual void do_send_now_playing(const scrobble_entry &s) = 0;

        /*!
         * \brief Sends a scrobble to the scrobble server
         *
         * It is called by new_song only if do_check_preconditions returns true
         * This endpoint is required by last.fm and the implementer is expected
         * to provide caching or another persistence form in the case requests
         * fail.
         */
        virtual void do_send_scrobble(const scrobble_entry &s) = 0;

        /*!
         * \brief Checks whether the scribble conditions have been met yet
         * \param s The song to check for
         * \return true if they have
         */
        virtual bool do_check_preconditions(const scrobble_entry &s) = 0;
    };

    /*!
     * \brief Facility for creating and authenticating scrobblers
     */
    struct scrobbler_factory {  // NOLINT virtual default constructor
        virtual ~scrobbler_factory() = default;

        /*!
         * \brief Constructs and returns a new scrobbler on the heap
         * \param section The configuration section for the scrobbler
         */
        gsl::owner<scrobbler *> operator()(const config_section &section);

        /*! \brief Runs authentication for the scrobbler built by this factory
         * \param argc Argument count
         * \param argv Argument vector (argv[0] is the name of the
         *             authenticator)
         */
        void authenticate(int argc, const char **argv);

    protected:
        virtual gsl::owner<scrobbler *>
            do_fabrication(const config_section &)                = 0;
        virtual void do_authenticate(int argc, const char **argv) = 0;
    };
}  // namespace mpdfm

namespace tao::json {
    template<>
    struct traits<mpdfm::scrobble_entry>
        : binding::basic_object<
              binding::for_unknown_key::skip,
              binding::for_nothing_value::suppress,
              TAO_JSON_BIND_OPTIONAL("artist", &mpdfm::scrobble_entry::artist),
              TAO_JSON_BIND_OPTIONAL("track", &mpdfm::scrobble_entry::track),
              TAO_JSON_BIND_OPTIONAL("album", &mpdfm::scrobble_entry::album),
              TAO_JSON_BIND_OPTIONAL("track_number",
                                     &mpdfm::scrobble_entry::track_number),
              TAO_JSON_BIND_OPTIONAL("mbid", &mpdfm::scrobble_entry::mbid),
              TAO_JSON_BIND_OPTIONAL("album_artist",
                                     &mpdfm::scrobble_entry::album_artist),
              TAO_JSON_BIND_OPTIONAL("duration",
                                     &mpdfm::scrobble_entry::duration),
              TAO_JSON_BIND_OPTIONAL("timestamp",
                                     &mpdfm::scrobble_entry::timestamp),
              TAO_JSON_BIND_OPTIONAL("elapsed",
                                     &mpdfm::scrobble_entry::elapsed)> {};
}  // namespace tao::json

#endif // SCROBBLER_HPP
