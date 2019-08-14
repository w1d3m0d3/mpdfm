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
#ifndef URIS_HPP
#define URIS_HPP

#include <string>
#include <string_view>

namespace mpdfm {
    namespace uri_parsing {
        template<typename Rule>
        struct action {};
        struct set_ip_flag;

        struct view {
            view() = default;
            view(size_t offset, size_t size);
            [[nodiscard]] std::string_view
                make_view(const std::string &str) const;

            [[nodiscard]] size_t offset() const;
            [[nodiscard]] size_t size() const;

        private:
            size_t m_off  = 0;
            size_t m_size = 0;
        };
    }  // namespace uri_parsing

    /*!
     * \brief URL (percent) encodes a string regardless of it's encoding
     */
    std::string urlencode(const std::string &str);

    /*!
     * URI representation and parser
     */
    struct uri {
        explicit uri(std::string src);
        //! \returns A const-reference into the source string
        [[nodiscard]] const std::string &source() const;

        //! \returns A string view into the scheme part of the URI
        [[nodiscard]] std::string_view scheme() const;
        //! \returns A string view into the authority part of the URI
        [[nodiscard]] std::string_view authority() const;
        //! \returns A string view into the user info part of the URI
        [[nodiscard]] std::string_view userinfo() const;
        //! \returns A string view into the host part of the URI
        [[nodiscard]] std::string_view host() const;
        //! \returns A string view into the port part of the URI
        [[nodiscard]] std::string_view port() const;
        //! \returns A string view into the path part of the URI
        [[nodiscard]] std::string_view path() const;
        //! \returns A string view into the query part of the URI
        [[nodiscard]] std::string_view query() const;
        //! \returns A string view into the path and query parts of the URI
        [[nodiscard]] std::string_view path_and_query() const;
        //! \returns A string view into the fragment part of the URI
        [[nodiscard]] std::string_view fragment() const;

        //! \returns true if the host is an IP address
        [[nodiscard]] bool is_ip() const;
        //! \returns true if there's a query string (even if it's empty)
        [[nodiscard]] bool has_query() const;

    private:
        std::string m_source;

        uri_parsing::view m_scheme;
        uri_parsing::view m_authority;
        uri_parsing::view m_userinfo;
        uri_parsing::view m_host;
        uri_parsing::view m_port;
        uri_parsing::view m_path;
        uri_parsing::view m_query;
        uri_parsing::view m_fragment;

        bool m_is_ip = false;
        bool m_has_query = false;

        template<typename T>
        friend struct uri_parsing::action;
        friend struct uri_parsing::set_ip_flag;
    };
}  // namespace mpdfm

#endif // URIS_HPP
