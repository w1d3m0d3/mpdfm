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
#include <http_client.hpp>

boost::asio::io_context &mpdfm::io_context() {
    static boost::asio::io_context ctx;
    return ctx;
}

namespace ssl = boost::asio::ssl;

namespace {
    struct ssl_context_wrapper {
        ssl::context ctx;
        ssl_context_wrapper() : ctx(ssl::context::tls_client) {
            // NOLINTNEXTLINE intended use
            ctx.set_options(ssl::context::no_sslv2 | ssl::context::no_sslv3
                            | ssl::context::no_tlsv1
                            | ssl::context::no_tlsv1_1);
            ctx.set_verify_mode(ssl::context::verify_peer);
            ctx.set_default_verify_paths();
        }
    };
}  // namespace

ssl::context &mpdfm::ssl_context() {
    static ssl_context_wrapper ctx;
    return ctx.ctx;
}
