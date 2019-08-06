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
 * Copyright (C) 2019 ScyllaDB
 */

#include <seastar/testing/thread_test_case.hh>

#include <seastar/core/gate.hh>

using namespace seastar;

SEASTAR_THREAD_TEST_CASE(test_gate_enter_leave_close) {
    gate g;

    BOOST_REQUIRE_EQUAL(g.get_count(), 0);
    BOOST_REQUIRE_EQUAL(g.is_closed(), false);

    auto i = 0;
    while (i <= 100) {
        g.enter();
        BOOST_REQUIRE_EQUAL(g.get_count(), ++i);
    }

    BOOST_REQUIRE_EQUAL(g.is_closed(), false);
    BOOST_REQUIRE_NO_THROW(g.check());

    auto f = g.close();

    BOOST_REQUIRE_EQUAL(g.is_closed(), true);
    BOOST_REQUIRE_THROW(g.check(), gate_closed_exception);
    BOOST_REQUIRE_THROW(g.enter(), gate_closed_exception);
    BOOST_REQUIRE_EQUAL(g.get_count(), i);
    BOOST_REQUIRE(!f.available());

    while (i > 1) {
        g.leave();
        BOOST_REQUIRE_EQUAL(g.get_count(), --i);
        BOOST_REQUIRE(!f.available());
    }

    g.leave();
    BOOST_REQUIRE_EQUAL(g.get_count(), 0);
    BOOST_REQUIRE(f.available());
}

SEASTAR_THREAD_TEST_CASE(test_gate_with_gate) {
    gate g;

    BOOST_REQUIRE_EQUAL(g.get_count(), 0);

    auto pr1 = promise<>();
    auto f1 = with_gate(g, [&pr1] {
        return pr1.get_future();
    });

    BOOST_REQUIRE_EQUAL(g.get_count(), 1);

    auto pr2 = promise<>();
    auto f2 = with_gate(g, [&pr2] {
        return pr2.get_future();
    });

    BOOST_REQUIRE_EQUAL(g.get_count(), 2);

    auto f3 = with_gate(g, [] { });
    f3.get();
    BOOST_REQUIRE_EQUAL(g.get_count(), 2);

    auto f = g.close();
    BOOST_REQUIRE(!f.available());

    pr1.set_value();
    f1.get();
    BOOST_REQUIRE_EQUAL(g.get_count(), 1);
    BOOST_REQUIRE(!f.available());

    pr2.set_value();
    f2.get();
    BOOST_REQUIRE_EQUAL(g.get_count(), 0);
    BOOST_REQUIRE(f.available());
}

namespace {

struct first_test_exception : std::runtime_error {
    first_test_exception() : std::runtime_error("first exception") { }
};

struct other_test_exception : std::runtime_error {
    other_test_exception() : std::runtime_error("other exception") { }
};

}

SEASTAR_THREAD_TEST_CASE(test_gate_leave_with_exception) {
    gate g;

    BOOST_REQUIRE_EQUAL(g.get_count(), 0);
    BOOST_REQUIRE_EQUAL(g.is_closed(), false);

    auto i = 0;
    while (i <= 100) {
        g.enter();
        BOOST_REQUIRE_EQUAL(g.get_count(), ++i);
    }

    BOOST_REQUIRE_EQUAL(g.is_closed(), false);
    BOOST_REQUIRE_NO_THROW(g.check());

    auto f = g.close();

    BOOST_REQUIRE_EQUAL(g.is_closed(), true);
    BOOST_REQUIRE_THROW(g.check(), gate_closed_exception);
    BOOST_REQUIRE_THROW(g.enter(), gate_closed_exception);
    BOOST_REQUIRE_EQUAL(g.get_count(), i);
    BOOST_REQUIRE(!f.available());

    g.leave_with_exception(std::make_exception_ptr(first_test_exception{}));
    BOOST_REQUIRE_EQUAL(g.get_count(), --i);
    BOOST_REQUIRE(!f.available());

    while (i > 1) {
        if (i % 2) {
            g.leave();
        } else {
            g.leave_with_exception(std::make_exception_ptr(other_test_exception{}));
        }
        BOOST_REQUIRE_EQUAL(g.get_count(), --i);
        BOOST_REQUIRE(!f.available());
    }

    g.leave();
    BOOST_REQUIRE_EQUAL(g.get_count(), 0);
    BOOST_REQUIRE(f.available());
    BOOST_REQUIRE(f.failed());

    BOOST_REQUIRE_THROW(f.get(), first_test_exception);
}

SEASTAR_THREAD_TEST_CASE(test_gate_with_gate_forwarded) {
    gate g;

    BOOST_REQUIRE_EQUAL(g.get_count(), 0);

    auto pr1 = promise<>();
    with_gate_forwarded(g, [&pr1] {
        return pr1.get_future();
    });

    BOOST_REQUIRE_EQUAL(g.get_count(), 1);

    auto pr2 = promise<>();
    with_gate_forwarded(g, [&pr2] {
        return pr2.get_future();
    });

    BOOST_REQUIRE_EQUAL(g.get_count(), 2);

    auto f3 = with_gate(g, [] { });
    f3.get();
    BOOST_REQUIRE_EQUAL(g.get_count(), 2);

    auto f = g.close();
    BOOST_REQUIRE(!f.available());

    pr1.set_value();
    pr2.set_exception(std::make_exception_ptr(first_test_exception{}));
    BOOST_REQUIRE_THROW(f.get(), first_test_exception);
}
