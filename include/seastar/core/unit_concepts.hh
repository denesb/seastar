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
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 */

#pragma once

#include <type_traits>

#include <seastar/util/gcc6-concepts.hh>

namespace seastar {

GCC6_CONCEPT(
template <typename Size>
concept bool QueueSize = std::is_default_constructible<Size>::value && requires(Size s1, Size s2) {
    s1 = s2;
    { s1 == s2 } -> bool;
    { s1 < s2 } -> bool;
    { s1 <= s2 } -> bool;
    { s1 > s2 } -> bool;
    { s1 >= s2 } -> bool;
    { s1 + s2 } -> Size;
    s1 += s2;
    { s1 - s2 } -> Size;
    s1 -= s2;
};
)

GCC6_CONCEPT(
template <typename Measurer, typename Size, typename T>
concept bool QueueItemMeasurer = requires(Measurer m, const T& t) {
    { m(t) } -> Size;
};
)

template <typename Size, typename T, typename = typename std::enable_if<std::is_arithmetic<Size>::value>::type>
struct counting_measurer {
    constexpr Size operator()(const T&) const { return Size{1}; }
};

} // namespace seastar
