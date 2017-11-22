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

#pragma once

#include <ostream>
#include <array>
#include <vector>
#include <deque>
#include <forward_list>
#include <list>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>

namespace seastar {

template <class ForwardIt>
void pretty_print_value_container(std::ostream& os, ForwardIt begin, ForwardIt end) {
    if (begin == end) {
        os << "{ }";
        return;
    }

    os << "{" << *begin;
    while (++begin != end) {
        os << ", " << *begin;
    }
    os << "}";
}

template <class ForwardIt>
void pretty_print_key_value_container(std::ostream& os, ForwardIt begin, ForwardIt end) {
    if (begin == end) {
        os << "{ }";
        return;
    }

    os << "{{" << begin->first << " -> " << begin->second << "}";
    while (++begin != end) {
        os << ", { " << begin->first << " -> " << begin->second << "}";
    }
    os << "}";
}

} // namespace seastar

// Pretty printers for STL stuff.
namespace std {

// Sequence Containers

template <typename T, size_t N>
std::ostream& operator<<(std::ostream& os, const std::array<T, N>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class T, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::vector<T, Allocator>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class T, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::deque<T, Allocator>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class T, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::forward_list<T, Allocator>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class T, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::list<T, Allocator>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

// Associative containers

template <class Key, class Compare, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::set<Key, Compare, Allocator>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class Key, class T, class Compare, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::map<Key, T, Compare, Allocator>& c) {
    ::seastar::pretty_print_key_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class Key, class Compare, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::multiset<Key, Compare, Allocator>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class Key, class T, class Compare, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::multimap<Key, T, Compare, Allocator>& c) {
    ::seastar::pretty_print_key_value_container(os, c.cbegin(), c.cend());
    return os;
}

// Unordered associative containers

template <class Key, class Hash, class KeyEqual, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::unordered_set<Key, Hash, KeyEqual, Allocator>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class Key, class T, class Hash, class KeyEqual, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::unordered_map<Key, T, Hash, KeyEqual, Allocator>& c) {
    ::seastar::pretty_print_key_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class Key, class Hash, class KeyEqual, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::unordered_multiset<Key, Hash, KeyEqual, Allocator>& c) {
    ::seastar::pretty_print_value_container(os, c.cbegin(), c.cend());
    return os;
}

template <class Key, class T, class Hash, class KeyEqual, class Allocator>
std::ostream& operator<<(std::ostream& os, const std::unordered_multimap<Key, T, Hash, KeyEqual, Allocator>& c) {
    ::seastar::pretty_print_key_value_container(os, c.cbegin(), c.cend());
    return os;
}

// Misc stuff

std::ostream& operator<<(std::ostream&, const std::exception_ptr&);
std::ostream& operator<<(std::ostream&, const std::exception&);
std::ostream& operator<<(std::ostream&, const std::system_error&);

} // namespace std
