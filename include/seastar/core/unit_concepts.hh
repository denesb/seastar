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

template <typename Size, typename T, typename = std::enable_if_t<std::is_arithmetic_v<Size>>>
struct counting_measurer {
    constexpr Size operator()(const T&) const { return Size{1}; }
};

template <typename Unit, typename = std::enable_if_t<std::is_arithmetic_v<Unit>>>
struct unit_traits {
    using signed_unit = std::make_signed_t<Unit>;
    using unsigned_unit = std::make_unsigned_t<Unit>;
    static constexpr signed_unit signed_max() { return std::numeric_limits<signed_unit>::max(); }
    static constexpr unsigned_unit unsigned_one() { return 1; }
};

GCC6_CONCEPT(
template <typename Unit>
concept bool SemaphoreUnit = QueueSize<unit_traits<Unit>::signed_unit> && QueueSize<unit_traits<Unit>::unsigned_unit> && requires() {
    { unit_traits<Unit>::signed_max() } -> typename unit_traits<Unit>::signed_unit;
    { unit_traits<Unit>::unsigned_one() } -> typename unit_traits<Unit>::unsigned_unit;
};
)

} // namespace seastar
