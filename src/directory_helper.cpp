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
#include <directory_helper.hpp>

boost::filesystem::path mpdfm::get_config_path() {
    boost::filesystem::path xdg_config_dir { std::getenv("XDG_CONFIG_HOME") };
    if (!xdg_config_dir.empty()) {
        return xdg_config_dir;
    }
    boost::filesystem::path home { std::getenv("HOME") };
    return home / ".config";
}
