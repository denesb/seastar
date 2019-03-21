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
// The limit object is responsible for keeping track of the current
// elements w.r.t. the to-be imposed limit.
// Upon each addition and removal of an element to/from the queue it
// is passed to `add()` and `substract()` respectively. Te limit
// object is then allowed to decide when the queue is full or not.
template <typename Limit, typename T>
concept bool QueueLimit = requires (Limit l1, Limit l2, const T& t) {
    // Update `l1` with the limit value contained in `l2`.
    // Any progress tracking (of how the current elements fare against
    // the limit) should be left unchanged.
    l1.update_to(std::move(l2));
    // Register an element being added to the queue.
    l1.add(t);
    // Register an element being removed from the queue.
    l1.substract(t);
    // Is the queue full according to this limit?
    { l1.full() } -> bool;
};
)

/// Asynchronous single-producer single-consumer queue with limited capacity.
/// There can be at most one producer-side and at most one consumer-side operation active at any time.
/// Operations returning a future are considered to be active until the future resolves.
template <typename T, typename Limit>
GCC6_CONCEPT(
    requires QueueLimit<Limit, T>
)
class basic_queue {
    std::queue<T, circular_buffer<T>> _q;
    Limit _limit;
    compat::optional<promise<>> _not_empty;
    compat::optional<promise<>> _not_full;
    std::exception_ptr _ex = nullptr;
private:
    void notify_not_empty();
    void notify_not_full();
public:
    explicit basic_queue(Limit limit);

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

    /// Returns the number of items currently in the queue.
    size_t size() const { return _q.size(); }

    /// Returns the limit imposed on the queue during its construction
    /// or by a call to set_limit(). If the queue is full according to the
    /// limit, further items cannot be pushed until some are popped.
    Limit limit() const { return _limit; }

    /// Set the limit to a new value. If the limit is reduced, items already in
    /// the queue will not be expunged and the queue will be temporarily bigger
    /// than the specified limit.
    void set_limit(Limit limit) {
        _limit.update_to(std::move(limit));
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

template <typename T, typename Limit>
inline
basic_queue<T, Limit>::basic_queue(Limit limit)
    : _limit(std::move(limit)) {
}

template <typename T, typename Limit>
inline
void basic_queue<T, Limit>::notify_not_empty() {
    if (_not_empty) {
        _not_empty->set_value();
        _not_empty = compat::optional<promise<>>();
    }
}

template <typename T, typename Limit>
inline
void basic_queue<T, Limit>::notify_not_full() {
    if (_not_full) {
        _not_full->set_value();
        _not_full = compat::optional<promise<>>();
    }
}

template <typename T, typename Limit>
inline
bool basic_queue<T, Limit>::push(T&& data) {
    if (!_limit.full()) {
        _q.push(std::move(data));
        _limit.add(_q.back());
        notify_not_empty();
        return true;
    } else {
        return false;
    }
}

template <typename T, typename Limit>
inline
T basic_queue<T, Limit>::pop() {
    if (_limit.full()) {
        notify_not_full();
    }
    T data = std::move(_q.front());
    _limit.substract(data);
    _q.pop();
    return data;
}

template <typename T, typename Limit>
inline
future<T> basic_queue<T, Limit>::pop_eventually() {
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

template <typename T, typename Limit>
inline
future<> basic_queue<T, Limit>::push_eventually(T&& data) {
    if (_ex) {
        return make_exception_future<>(_ex);
    }
    if (full()) {
        return not_full().then([this, data = std::move(data)] () mutable {
            _q.push(std::move(data));
            notify_not_empty();
        });
    } else {
        _q.push(std::move(data));
        notify_not_empty();
        return make_ready_future<>();
    }
}

template <typename T, typename Limit>
template <typename Func>
inline
bool basic_queue<T, Limit>::consume(Func&& func) {
    if (_ex) {
        std::rethrow_exception(_ex);
    }
    bool running = true;
    while (!_q.empty() && running) {
        running = func(std::move(_q.front()));
        _q.pop();
    }
    if (!full()) {
        notify_not_full();
    }
    return running;
}

template <typename T, typename Limit>
inline
bool basic_queue<T, Limit>::empty() const {
    return _q.empty();
}

template <typename T, typename Limit>
inline
bool basic_queue<T, Limit>::full() const {
    return _limit.full();
}

template <typename T, typename Limit>
inline
future<> basic_queue<T, Limit>::not_empty() {
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

template <typename T, typename Limit>
inline
future<> basic_queue<T, Limit>::not_full() {
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

namespace internal {

template <typename T>
class size_limit {
    size_t _items = 0;

public:
    size_t max;

public:
    explicit size_limit(size_t max) : max(max) { }
    void update_to(size_limit&& o) { max = o.max; }
    void add(const T&) { ++_items; }
    void substract(const T&) { --_items; }
    bool full() const { return _items >= max; }
};

}

template <typename T>
class queue : public basic_queue<T, internal::size_limit<T>> {
public:
    explicit queue(size_t size) : basic_queue<T, internal::size_limit<T>>(internal::size_limit<T>{size}) { }

    /// Returns the size limit imposed on the queue during its construction
    /// or by a call to set_max_size(). If the queue contains max_size()
    /// items (or more), further items cannot be pushed until some are popped.
    size_t max_size() const { return this->limit().max; }

    /// Set the maximum size to a new value. If the queue's max size is reduced,
    /// items already in the queue will not be expunged and the queue will be temporarily
    /// bigger than its max_size.
    void set_max_size(size_t max) { this->set_limit(internal::size_limit<T>{max}); }
};

}

