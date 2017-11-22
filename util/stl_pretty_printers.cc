/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright 2017 ScyllaDB
 */

#include "util/stl_pretty_printers.hh"

#include <exception>
#include <cxxabi.h>
#include "util/log.hh"

namespace std {

std::ostream& operator<<(std::ostream& out, const std::exception_ptr& eptr) {
    if (!eptr) {
        out << "<no exception>";
        return out;
    }
    try {
        std::rethrow_exception(eptr);
    } catch(...) {
        auto tp = abi::__cxa_current_exception_type();
        if (tp) {
            out << seastar::pretty_type_name(*tp);
        } else {
            // This case shouldn't happen...
            out << "<unknown exception>";
        }
        // Print more information on some familiar exception types
        try {
            throw;
        } catch(const std::system_error &e) {
            out << " (error " << e.code() << ", " << e.code().message() << ")";
        } catch(const std::exception& e) {
            out << " (" << e.what() << ")";
            try {
                std::rethrow_if_nested(e);
            } catch (...) {
                out << ": " << std::current_exception();
            }
        } catch(...) {
            // no extra info
        }
    }
    return out;
}

std::ostream& operator<<(std::ostream& out, const std::exception& e) {
    return out << seastar::pretty_type_name(typeid(e)) << " (" << e.what() << ")";
}

std::ostream& operator<<(std::ostream& out, const std::system_error& e) {
    return out << seastar::pretty_type_name(typeid(e)) << " (error " << e.code() << ", " << e.code().message() << ")";
}

} // namespace std
