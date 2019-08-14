/*
 * This file is part of mpdf.
 *
 * mpdf is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mpdf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mpdf.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef CONFIG_CONFIG_FILE_HPP
#define CONFIG_CONFIG_FILE_HPP

#include <map>
#include <string>
#include <vector>

namespace mpdfm {

    /*!
     * \brief Holds a key-value map for a section
     */
    struct config_section {
        /*!
         * \brief Checks whether a key exists in this section
         */
        [[nodiscard]] bool has_value(const std::string &key) const;

        /*!
         * \brief Gets the value associated with the given key
         */
        [[nodiscard]] const std::string &value(const std::string &key) const;

        /*!
         * \brief Gets the value associated with the given key, or a default
         *        value
         *
         * \param key The key to look up
         * \param def The value to return on failure
         *
         * \return A copy of the value or the default
         */
        [[nodiscard]] std::string value(const std::string &key,
                                        const std::string &def) const;

        /*!
         * \brief Equivalent to value(key);
         */
        [[nodiscard]] const std::string &
            operator[](const std::string &key) const;

        /*!
         * \brief Retrieves section name
         *
         * For the input:
         * ```
         * example {}
         * ```
         * The section name will be `example`
         */
        [[nodiscard]] const std::string &name() const;

    private:
        explicit config_section(std::string name);
        template<typename Value>
        void insert_or_fail(const std::string &name, Value &&value) {
            if (has_value(name)) {
                throw std::runtime_error("duplicate key: " + name);
            }
            m_values.emplace(name, std::forward<Value>(value));
        }
        std::string m_name;
        std::map<std::string, std::string> m_values;
        friend struct config_file;
    };

    /*!
     * \brief Configuration file abstraction
     *
     * config_file reads from a file and parses it's content using PEGTL,
     * reporting any errors found along the way using the error() function.
     *
     * The configuration format is trivial and involves of kv-pairs in addition
     * to sections. These sections have a special meaning as each one relates
     * to a scrobbler. Hence there can be multiple of the same "name" section.
     *
     *  ```
     *  mpd_port = "6600"
     *  mpd_host = "127.0.0.1"
     *  audioscrobbler20 {
     *      store = "~/.cache/mpdfm/last.fm"
     *      url = "https://ws.audioscrobbler.com/2.0/"
     *      session != "pass mpdfm-lastfm-session"
     *  }
     *  ```
     * This configuration file will contain a scrobbler implemented using
     * last.fm's protocol posting to `https://ws.audioscrobbler.com/2.0/` with
     * the session key pulled from pass.
     *
     * What in this example looks like a section name, namely `last.fm`, is
     * actually the protocol the scrobbler described by that section will be
     * using, and hence you can have as many `last.fm` sections as you want.
     *
     * Supported scrobbling protocols can be found in include/protocols
     */
    struct config_file {
        /*!
         * \brief Constructs an empty config file
         */
        config_file();
        /*!
         * \brief Loads and parses the given file
         * \param filename THe file to open and check
         */
        explicit config_file(const std::string &filename);

        /*!
         * \return The root section
         */
        [[nodiscard]] const config_section &root_section() const;

        /*!
         * \return A vector of all the sections parsed out of the file
         */
        [[nodiscard]] const std::vector<config_section> &sections() const;

    private:
        config_section m_root;
        std::vector<config_section> m_sections;
    };
}  // namespace mpdfm

#endif // CONFIG_CONFIG_FILE_HPP
