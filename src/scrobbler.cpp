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
#include <scrobbler.hpp>

void mpdfm::scrobbler::scrobble(const mpdfm::scrobble_entry &song) {
    do_send_scrobble(song);
}

void mpdfm::scrobbler::now_playing(const mpdfm::scrobble_entry &song) {
    do_send_now_playing(song);
}

bool mpdfm::scrobbler::check_preconditions(const mpdfm::scrobble_entry &song) {
    return do_check_preconditions(song);
}

mpdfm::scrobble_entry::scrobble_entry(const song &s)
    : artist(s.tag(MPD_TAG_ARTIST)),
      track(s.tag(MPD_TAG_TITLE)),
      album(s.tag(MPD_TAG_ALBUM)),
      track_number(s.tag(MPD_TAG_TRACK)),
      mbid(s.tag(MPD_TAG_MUSICBRAINZ_TRACKID)),
      album_artist(s.tag(MPD_TAG_ALBUM_ARTIST)),
      duration(s.duration()) {}

gsl::owner<mpdfm::scrobbler *> mpdfm::scrobbler_factory::
    operator()(const mpdfm::config_section &section) {
    return do_fabrication(section);
}

void mpdfm::scrobbler_factory::authenticate(int argc, const char **argv) {
    return do_authenticate(argc, argv);
}
