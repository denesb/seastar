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
 * Copyright 2014 Cloudius Systems
 */

#pragma once

#include <seastar/core/future.hh>
#include <seastar/util/std-compat.hh>
#include <exception>

namespace seastar {

/// \addtogroup fiber-module
/// @{

/// Exception thrown when a \ref gate object has been closed
/// by the \ref gate::close() method.
class gate_closed_exception : public std::exception {
public:
    virtual const char* what() const noexcept override {
        return "gate closed";
    }
};

/// Facility to stop new requests, and to tell when existing requests are done.
///
/// When stopping a service that serves asynchronous requests, we are faced with
/// two problems: preventing new requests from coming in, and knowing when existing
/// requests have completed.  The \c gate class provides a solution.
class gate {
    size_t _count = 0;
    compat::optional<promise<>> _stopped;
    std::exception_ptr _ex;
private:
    void set_value_or_exception() {
        if (_ex) {
            _stopped->set_exception(std::move(_ex));
        } else {
            _stopped->set_value();
        }
    }
public:
    /// Registers an in-progress request.
    ///
    /// If the gate is not closed, the request is registered.  Otherwise,
    /// a \ref gate_closed_exception is thrown.
    void enter() {
        if (_stopped) {
            throw gate_closed_exception();
        }
        ++_count;
    }
    /// Unregisters an in-progress request.
    ///
    /// If the gate is closed, and if there are no more in-progress requests,
    /// the \ref closed() promise will be fulfilled.
    void leave() {
        --_count;
        if (!_count && _stopped) {
            set_value_or_exception();
        }
    }
    /// Unregisters an in-progress failed request.
    ///
    /// If the gate is closed, and if there are no more in-progress requests,
    /// the \ref closed() promise will be fulfilled.
    /// The exception passed here will be re-raised from `close()`. When
    /// multiple requests fail, only the first exception will be retained.
    void leave_with_exception(std::exception_ptr ex) {
        if (!_ex) {
            _ex = std::move(ex);
        }
        leave();
    }
    /// Potentially stop an in-progress request.
    ///
    /// If the gate is already closed, a \ref gate_closed_exception is thrown.
    /// By using \ref enter() and \ref leave(), the program can ensure that
    /// no further requests are serviced. However, long-running requests may
    /// continue to run. The check() method allows such a long operation to
    /// voluntarily stop itself after the gate is closed, by making calls to
    /// check() in appropriate places. check() with throw an exception and
    /// bail out of the long-running code if the gate is closed.
    void check() {
        if (_stopped) {
            throw gate_closed_exception();
        }
    }
    /// Closes the gate.
    ///
    /// Future calls to \ref enter() will fail with an exception, and when
    /// all current requests call \ref leave(), the returned future will be
    /// made ready.
    /// If any requests left the gate with an exception, it will be
    /// forwarded to the returned future<>. If more then one requests failed,
    /// only the first exception will be returned.
    future<> close() {
        assert(!_stopped && "seastar::gate::close() cannot be called more than once");
        _stopped = compat::make_optional(promise<>());
        if (!_count) {
            set_value_or_exception();
        }
        return _stopped->get_future();
    }

    /// Returns a current number of registered in-progress requests.
    size_t get_count() const {
        return _count;
    }

    /// Returns whether the gate is closed.
    bool is_closed() const {
        return bool(_stopped);
    }
};

/// Executes the function \c func making sure the gate \c g is properly entered
/// and later on, properly left.
///
/// \param func function to be executed
/// \param g the gate. Caller must make sure that it outlives this function.
/// \returns whatever \c func returns
///
/// \relates gate
template <typename Func>
inline
auto
with_gate(gate& g, Func&& func) {
    g.enter();
    return futurize_apply(std::forward<Func>(func)).finally([&g] { g.leave(); });
}

/// Executes the function \c func making sure the gate \c g is properly entered
/// and later on, properly left.
///
/// The result of the function is forwarded to the gate. Any exception thrown
/// from the function will be forwarded to the gate and will be re-thrown from
/// `gate::close()`. Can be used to wait on fibers that are moved to the
/// background, and still be notified if there is any error during their
/// execution.
///
/// \param func function to be executed, must return void or future<>.
/// \param g the gate. Caller must make sure that it outlives this function.
///
/// \relates gate
/// \see gate::leave_with_exception()
template <typename Func>
GCC6_CONCEPT(
    requires std::is_same_v<std::invoke_result_t<Func>, future<>> || std::is_same_v<std::invoke_result_t<Func>, void>
)
inline
void
with_gate_forwarded(gate& g, Func func) {
    g.enter();
    // The result is forwarded to the gate.
    (void)futurize_apply(std::forward<Func>(func)).then_wrapped([&g] (future<>&& f) {
        if (f.failed()) {
            g.leave_with_exception(f.get_exception());
        } else {
            g.leave();
        }
    });
}
/// @}

}
