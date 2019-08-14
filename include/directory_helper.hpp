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
#ifndef DIRECTORY_HELPER_HPP
#define DIRECTORY_HELPER_HPP

#include <boost/filesystem.hpp>

namespace mpdfm {
    /*!
     * \brief Returns the path to the configuration directory
     *
     * On XDG systems this is equivalent to
     *     ${XDG_CONFIG_HOME:-$HOME/.config}
     */
    boost::filesystem::path get_config_path();
}  // namespace mpdfm

#endif // DIRECTORY_HELPER_HPP
