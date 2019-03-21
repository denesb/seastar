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

#include <seastar/core/circular_buffer.hh>
#include <seastar/core/future.hh>
#include <queue>
#include <seastar/util/std-compat.hh>

namespace seastar {

GCC6_CONCEPT(
template <typename T, typename LimitPolicy>
concept bool QueueLimitPolicy = requires(LimitPolicy p, const T& item, typename LimitPolicy::size_type& actual,
        const typename LimitPolicy::size_type& max) {
    // Is the queue full, according to max?
    { p.full(actual, max) } -> bool;
    // Update actual to reflect `item` added to the queue.
    p.add(actual, item);
    // Update actual to reflect `item` removed from the queue.
    p.substract(actual, item);
};
)

template <typename T>
/// Default limit policy for queue.
///
/// Uses the number of items to determine when the queue is full.
class ItemCountLimitPolicy {
public:
    using size_type = size_t;

public:
    bool full(const size_type& actual, const size_type& max) const { return actual >= max; }
    void add(size_type& actual, const T& item) { ++actual; }
    void substract(size_type& actual, const T& item) { --actual; }
};

/// Asynchronous single-producer single-consumer queue with limited capacity.
/// There can be at most one producer-side and at most one consumer-side operation active at any time.
/// Operations returning a future are considered to be active until the future resolves.
/// \tparam T the type of items.
/// \tparam LimitPolicy the policy that determines when the queue is considered
///     full. The `LimitPolicy` has to comply with the `QueueLimitPolicy`
///     concept. By default ItemCountLimitPolicy is used, a policy that only
///     considers the number of items in the queue is used.
///
/// \see ItemCountLimitPolicy
template <typename T, typename LimitPolicy = ItemCountLimitPolicy<T>>
GCC6_CONCEPT(
    requires QueueLimitPolicy<T, LimitPolicy>
)
class queue : private LimitPolicy {
public:
    using size_type = typename LimitPolicy::size_type;

private:
    std::queue<T, circular_buffer<T>> _q;
    size_type _actual = {};
    size_type _max;
    compat::optional<promise<>> _not_empty;
    compat::optional<promise<>> _not_full;
    std::exception_ptr _ex = nullptr;
private:
    void notify_not_empty();
    void notify_not_full();
public:
    /// Can only be used if `LimitPolicy` is default constructible.
    explicit queue(size_type size);
    explicit queue(size_type size, LimitPolicy limit_policy);

    /// \brief Push an item.
    ///
    /// Returns false if the queue was full and the item was not pushed.
    bool push(T&& a);

    /// \brief Pop an item.
    ///
    /// Popping from an empty queue will result in undefined behavior.
    T pop();

    /// Consumes items from the queue, passing them to @func, until @func
    /// returns false or the queue it empty
    ///
    /// Returns false if func returned false.
    template <typename Func>
    bool consume(Func&& func);

    /// Returns true when the queue is empty.
    bool empty() const;

    /// Returns true when the queue is full.
    bool full() const;

    /// Returns a future<> that becomes available when pop() or consume()
    /// can be called.
    /// A consumer-side operation. Cannot be called concurrently with other consumer-side operations.
    future<> not_empty();

    /// Returns a future<> that becomes available when push() can be called.
    /// A producer-side operation. Cannot be called concurrently with other producer-side operations.
    future<> not_full();

    /// Pops element now or when there is some. Returns a future that becomes
    /// available when some element is available.
    /// If the queue is, or already was, abort()ed, the future resolves with
    /// the exception provided to abort().
    /// A consumer-side operation. Cannot be called concurrently with other consumer-side operations.
    future<T> pop_eventually();

    /// Pushes the element now or when there is room. Returns a future<> which
    /// resolves when data was pushed.
    /// If the queue is, or already was, abort()ed, the future resolves with
    /// the exception provided to abort().
    /// A producer-side operation. Cannot be called concurrently with other producer-side operations.
    future<> push_eventually(T&& data);

    /// Returns the current size of the queue.
    size_type size() const { return _actual; }

    /// Returns the size limit imposed on the queue during its construction
    /// or by a call to set_max_size(). If the queue is full according to
    /// max size, further items cannot be pushed until some are popped.
    size_type max_size() const { return _max; }

    /// Set the maximum size to a new value. If the queue's max size is reduced,
    /// items already in the queue will not be expunged and the queue will be temporarily
    /// bigger than its max_size.
    void set_max_size(size_type max) {
        _max = max;
        if (!full()) {
            notify_not_full();
        }
    }

    /// Destroy any items in the queue, and pass the provided exception to any
    /// waiting readers or writers - or to any later read or write attempts.
    void abort(std::exception_ptr ex) {
        while (!_q.empty()) {
            _q.pop();
        }
        _ex = ex;
        if (_not_full) {
            _not_full->set_exception(ex);
            _not_full= compat::nullopt;
        }
        if (_not_empty) {
            _not_empty->set_exception(std::move(ex));
            _not_empty = compat::nullopt;
        }
    }
};

template <typename T, typename LimitPolicy>
inline
queue<T, LimitPolicy>::queue(size_type size)
    : _max(size) {
}

template <typename T, typename LimitPolicy>
inline
queue<T, LimitPolicy>::queue(size_type size, LimitPolicy limit_policy)
    : LimitPolicy(std::move(limit_policy))
    , _max(size) {
}

template <typename T, typename LimitPolicy>
inline
void queue<T, LimitPolicy>::notify_not_empty() {
    if (_not_empty) {
        _not_empty->set_value();
        _not_empty = compat::optional<promise<>>();
    }
}

template <typename T, typename LimitPolicy>
inline
void queue<T, LimitPolicy>::notify_not_full() {
    if (_not_full) {
        _not_full->set_value();
        _not_full = compat::optional<promise<>>();
    }
}

template <typename T, typename LimitPolicy>
inline
bool queue<T, LimitPolicy>::push(T&& data) {
    if (!LimitPolicy::full(_actual, _max)) {
        _q.push(std::move(data));
        LimitPolicy::add(_actual, _q.back());
        notify_not_empty();
        return true;
    } else {
        return false;
    }
}

template <typename T, typename LimitPolicy>
inline
T queue<T, LimitPolicy>::pop() {
    T data = std::move(_q.front());
    _q.pop();
    LimitPolicy::substract(_actual, data);
    if (!LimitPolicy::full(_actual, _max)) {
        notify_not_full();
    }
    return data;
}

template <typename T, typename LimitPolicy>
inline
future<T> queue<T, LimitPolicy>::pop_eventually() {
    if (_ex) {
        return make_exception_future<T>(_ex);
    }
    if (empty()) {
        return not_empty().then([this] {
            if (_ex) {
                return make_exception_future<T>(_ex);
            } else {
                return make_ready_future<T>(pop());
            }
        });
    } else {
        return make_ready_future<T>(pop());
    }
}

template <typename T, typename LimitPolicy>
inline
future<> queue<T, LimitPolicy>::push_eventually(T&& data) {
    if (_ex) {
        return make_exception_future<>(_ex);
    }
    if (full()) {
        return not_full().then([this, data = std::move(data)] () mutable {
            LimitPolicy::add(_actual, data);
            _q.push(std::move(data));
            notify_not_empty();
        });
    } else {
        LimitPolicy::add(_actual, data);
        _q.push(std::move(data));
        notify_not_empty();
        return make_ready_future<>();
    }
}

template <typename T, typename LimitPolicy>
template <typename Func>
inline
bool queue<T, LimitPolicy>::consume(Func&& func) {
    if (_ex) {
        std::rethrow_exception(_ex);
    }
    bool running = true;
    while (!_q.empty() && running) {
        LimitPolicy::substract(_actual, _q.front());
        running = func(std::move(_q.front()));
        _q.pop();
    }
    if (!full()) {
        notify_not_full();
    }
    return running;
}

template <typename T, typename LimitPolicy>
inline
bool queue<T, LimitPolicy>::empty() const {
    return _q.empty();
}

template <typename T, typename LimitPolicy>
inline
bool queue<T, LimitPolicy>::full() const {
    return LimitPolicy::full(_actual, _max);
}

template <typename T, typename LimitPolicy>
inline
future<> queue<T, LimitPolicy>::not_empty() {
    if (_ex) {
        return make_exception_future<>(_ex);
    }
    if (!empty()) {
        return make_ready_future<>();
    } else {
        _not_empty = promise<>();
        return _not_empty->get_future();
    }
}

template <typename T, typename LimitPolicy>
inline
future<> queue<T, LimitPolicy>::not_full() {
    if (_ex) {
        return make_exception_future<>(_ex);
    }
    if (!full()) {
        return make_ready_future<>();
    } else {
        _not_full = promise<>();
        return _not_full->get_future();
    }
}

}

