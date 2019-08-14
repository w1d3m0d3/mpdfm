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
#include <config/config_file.hpp>

#include <boost/process.hpp>
#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/contrib/unescape.hpp>

namespace mpdfm::config {
    // clang-format off
    using namespace tao::pegtl; // NOLINT this is a PEG

    template<typename Rule>
    struct error_control : normal<Rule> {
        const static std::string_view msg;

        template<typename Input, typename... States>
        static void raise(const Input& in, States&&... /* unused */) {
            throw tao::pegtl::parse_error(std::string(msg), in);
        }
    };

    struct comment          : if_must<one<'#'>, until<eolf>> {};
    struct soc              : sor<space, comment> {};
    template<typename Rule>
    struct ws               : pad<Rule, soc> {};
    struct legal_characters : sor<alnum, one<'.', '_'>> {};
    struct name             : plus<legal_characters> {};
    struct separator        : ws<one<'='>> {};
    struct eval_separator   : ws<string<'!', '='>> {};
    struct quote            : one<'"'> {};
    struct escape_x         : seq<one<'x'>, rep<2, must<xdigit>>> {};
    struct escape_u         : seq<one<'u'>, rep<4, must<xdigit>>> {};
    struct escape_U         : seq<one<'U'>, rep<8, must<xdigit>>> {}; // NOLINT
    struct escape_c         : one<'\'', '"', '?', '\\',
                                  'a', 'b', 'f', 'n', 'r', 't', 'v'> {};
    struct escape           : sor<escape_x, escape_u, escape_U, escape_c> {};
    // NOLINTNEXTLINE magic numbers that already escaped my memory
    struct utf_character    : utf8::ranges<0x20, 0x21, 0x23, 0x10FFFF> {};
    struct string_char      : if_then_else<one<'\\'>, must<escape>,
                                           utf_character> {};
    struct string           : star<string_char> {};
    struct value            : seq<quote, string, quote> {};
    struct open_parens      : ws<one<'{'>> {};
    struct close_parens     : ws<one<'}'>> {};

    struct tkey             : seq<name, separator> {};
    struct tpair            : if_must<tkey, value> {};
    struct ekey             : seq<name, eval_separator> {};
    struct epair            : if_must<ekey, value> {};
    struct kvpair           : ws<sor<epair, tpair>> {};

    struct block_start      : seq<name, open_parens> {};
    struct block            : star<kvpair> {};
    struct section          : if_must<block_start, block, close_parens> {};

    struct parts            : sor<section, kvpair> {};
    struct grammar          : must<until<eof, parts>> {};

    template<>
    const std::string_view error_control<until<eof, parts>>::msg
                                                             = "invalid input";
    template<>
    const std::string_view error_control<block>::msg = "expected {} block";
    template<>
    const std::string_view error_control<open_parens>::msg = "expected '{'";
    template<>
    const std::string_view error_control<close_parens>::msg = "expected '}'";
    template<>
    const std::string_view error_control<value>::msg = "expected string";
    template<>
    const std::string_view error_control<xdigit>::msg = "expected hex digit";
    template<>
    const std::string_view error_control<escape>::msg
                                                  = "invalid escape sequence";
    template<>
    const std::string_view error_control<until<eolf>>::msg = "unreachable";

    template<typename Rule>
    using selector = parse_tree::selector<Rule,
        parse_tree::store_content::on<
            string,
            name
        >,
        parse_tree::remove_content::on<
            tpair,
            epair,
            section,
            block
        >,
        parse_tree::fold_one::on<
            tkey,
            ekey,
            value,
            kvpair
        >
    >;

    template<typename> struct unescape_action {};
    template<> struct unescape_action<escape_u> : unescape::unescape_j {};
    template<> struct unescape_action<escape_U> : unescape::unescape_j {};
    template<> struct unescape_action<escape_x> : unescape::unescape_x {};
    template<> struct unescape_action<escape_c>
        : unescape::unescape_c<escape_c, '\'', '"', '?', '\\',
                               '\a', '\b', '\f', '\n', '\r', '\t', '\v'> {};
    template<> struct unescape_action<utf_character> : unescape::append_all {};

    struct unescaping : seq<config::string, eof> {};

    template<>
    const std::string_view error_control<unescaping>::msg = "bad unescape input";
    // clang-format on

    std::string unescaped(const std::string &input) {
        memory_input sin(input, "string");
        std::string unescaped;
        parse<must<config::unescaping>,
              config::unescape_action,
              config::error_control>(sin, unescaped);
        return unescaped;
    }
}  // namespace mpdfm::config

mpdfm::config_section::config_section(std::string name)
    : m_name(std::move(name)) {}

const std::string &mpdfm::config_section::name() const {
    return m_name;
}

bool mpdfm::config_section::has_value(const std::string &key) const {
    return m_values.find(key) != m_values.end();
}

const std::string &mpdfm::config_section::value(const std::string &key) const {
    return m_values.at(key);
}

std::string mpdfm::config_section::value(const std::string &key,
                                         const std::string &def) const {
    if (!has_value(key)) {
        return def;
    }
    return m_values.at(key);
}

namespace {
    std::string evaluate(const std::string &cmd) {
        boost::process::ipstream is;
        // This is here because boost::process quotes the argument given to
        // system when used in combination with boost::process::shell.
        auto exit =
#ifdef _WIN32
            boost::process::system(
                boost::process::shell(), cmd, boost::process::std_out > is);
#elif defined(__unix__)
            boost::process::system(boost::process::shell(),
                                   "-c",
                                   cmd,
                                   boost::process::std_out > is);
#else
#    error "unsupported operating system"
#endif
        if (exit != 0) {
            throw std::runtime_error("evaluation failure for: " + cmd);
        }

        // trim left
        is >> std::ws;
        std::string res(std::istreambuf_iterator<char>(is), {});

        boost::trim_right(res);
        return res;
    }
}  // namespace

mpdfm::config_file::config_file() : m_root({}) {}

mpdfm::config_file::config_file(const std::string &file) : m_root({}) {
    using tao::pegtl::file_input;
    using tao::pegtl::nothing;
    using tao::pegtl::parse_tree::parse;

    file_input in(file);
    const auto tree = parse<config::grammar,
                            config::selector,
                            nothing,
                            config::error_control>(in);
    if (!tree) {
        throw std::runtime_error("configuration file parse error");
    }

    // TODO(w1d3): possibly clean this up one day, but it works for now in this
    //             foul state
    for (auto &node : tree->children) {
        if (node->is_type<config::tpair>()) {
            auto name  = node->children[0]->string();
            auto value = config::unescaped(node->children[1]->string());
            m_root.insert_or_fail(name, std::move(value));
        } else if (node->is_type<config::epair>()) {
            auto name  = node->children[0]->string();
            auto value = config::unescaped(node->children[1]->string());
            m_root.insert_or_fail(name, evaluate(value));
        } else if (node->is_type<config::section>()) {
            auto name  = node->children[0]->string();
            auto &data = node->children[1];
            config_section sec(name);
            for (auto &node : data->children) {
                if (node->is_type<config::tpair>()) {
                    auto name = node->children[0]->string();
                    auto value =
                        config::unescaped(node->children[1]->string());
                    sec.insert_or_fail(name, std::move(value));
                } else {
                    auto name = node->children[0]->string();
                    auto value =
                        config::unescaped(node->children[1]->string());
                    sec.insert_or_fail(name, evaluate(value));
                }
            }
            m_sections.emplace_back(std::move(sec));
        }
    }
}

const std::vector<mpdfm::config_section> &
    mpdfm::config_file::sections() const {
    return m_sections;
}

const mpdfm::config_section &mpdfm::config_file::root_section() const {
    return m_root;
}
