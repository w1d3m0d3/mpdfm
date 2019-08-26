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
#include "uris.hpp"

#include "gsl/gsl_util"
#include "tao/pegtl/memory_input.hpp"
#include <algorithm>
#include <cctype>
#include <gsl/gsl>
#include <iomanip>
#include <sstream>
#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/uri.hpp>

namespace {
    bool to_replace(const char x) {
        return !(bool(std::isalnum(x)) || x == '-' || x == '_' || x == '.'
                 || x == '~');
    }
}  // namespace

std::string mpdfm::urlencode(const std::string &str) {
    std::ostringstream result;
    result.fill('0');
    for (auto &orig : str) {
        auto x = static_cast<uint8_t>(orig);
        if (x == ' ') {
            result << '+';
        } else if (to_replace(x)) {
            result << std::hex << '%' << std::setw(2) << static_cast<int>(x)
                   << std::setw(0);
        } else {
            result << x;
        }
    }
    return result.str();
}

namespace mpdfm::uri_parsing {
    namespace puri = tao::pegtl::uri;

    template<view uri::*Property>
    struct set {
        template<typename Input>
        static void apply(const Input &in, uri &uri) {
            auto off =
                gsl::narrow_cast<size_t>(in.begin() - uri.source().data());
            auto size     = gsl::narrow_cast<size_t>(in.size());
            uri.*Property = { off, size };
        }
    };

    template<>
    struct action<puri::scheme> : set<&uri::m_scheme> {};
    template<>
    struct action<puri::authority> : set<&uri::m_authority> {};
    template<>
    struct action<puri::host> : set<&uri::m_host> {};
    template<>
    struct action<puri::port> : set<&uri::m_port> {};
    template<>
    struct action<puri::path_noscheme> : set<&uri::m_path> {};
    template<>
    struct action<puri::path_rootless> : set<&uri::m_path> {};
    template<>
    struct action<puri::path_absolute> : set<&uri::m_path> {};
    template<>
    struct action<puri::path_abempty> : set<&uri::m_path> {};
    template<>
    struct action<puri::query> : set<&uri::m_query> {
        template<typename Input>
        static void apply(const Input &in, uri &uri) {
            uri.m_has_query = true;
            set<&uri::m_query>::apply(in, uri);
        }
    };
    template<>
    struct action<puri::fragment> : set<&uri::m_fragment> {};

    struct set_ip_flag {
        template<typename Input>
        static void apply(const Input & /*unused*/, uri &uri) {
            uri.m_is_ip = true;
        }
    };

    template<>
    struct action<puri::IPv4address> : set_ip_flag {};

    template<>
    struct action<puri::IP_literal> : set_ip_flag {};

    template<>
    struct action<puri::opt_userinfo> {
        template<typename Input>
        static void apply(const Input &in, uri &uri) {
            if (!in.empty()) {
                auto off =
                    gsl::narrow_cast<size_t>(in.begin() - uri.source().data());
                auto size     = gsl::narrow_cast<size_t>(in.size());
                uri.m_userinfo = { off, size };
            }
        }
    };

    using grammar = tao::pegtl::must<puri::URI, tao::pegtl::eof>;
}  // namespace mpdfm::uri_parsing

std::string_view
    mpdfm::uri_parsing::view::make_view(const std::string &str) const {
    if (m_off > str.size() || m_off + m_size > str.size()) {
        return {};
    }
    // NOLINTNEXTLINE checked
    return { str.data() + m_off, m_size };
}

mpdfm::uri_parsing::view::view(size_t offset, size_t size)
    : m_off(offset), m_size(size) {}

size_t mpdfm::uri_parsing::view::offset() const {
    return m_off;
}

size_t mpdfm::uri_parsing::view::size() const {
    return m_size;
}

mpdfm::uri::uri(std::string src) : m_source(std::move(src)) {
    using tao::pegtl::memory_input;
    using tao::pegtl::parse;
    memory_input input(m_source, "source");
    parse<uri_parsing::grammar, uri_parsing::action>(input, *this);
}

std::string_view mpdfm::uri::scheme() const {
    return m_scheme.make_view(m_source);
}

std::string_view mpdfm::uri::authority() const {
    return m_authority.make_view(m_source);
}

std::string_view mpdfm::uri::userinfo() const {
    return m_userinfo.make_view(m_source);
}

std::string_view mpdfm::uri::host() const {
    return m_host.make_view(m_source);
}

std::string_view mpdfm::uri::port() const {
    return m_port.make_view(m_source);
}

std::string_view mpdfm::uri::path() const {
    return m_path.make_view(m_source);
}

std::string_view mpdfm::uri::query() const {
    return m_query.make_view(m_source);
}

std::string_view mpdfm::uri::path_and_query() const {
    if (!m_has_query) {
        return path();
    }
    auto new_size = m_path.size() + m_query.size() + 1;
    if (m_path.offset() > m_source.size()
        || m_path.offset() + new_size > m_source.size()) {
        return {};
    }
    // NOLINTNEXTLINE checked
    return { m_source.data() + m_path.offset(), new_size };
}

std::string_view mpdfm::uri::fragment() const {
    return m_fragment.make_view(m_source);
}

const std::string &mpdfm::uri::source() const {
    return m_source;
}

bool mpdfm::uri::is_ip() const {
    return m_is_ip;
}

bool mpdfm::uri::has_query() const {
    return m_has_query;
}
