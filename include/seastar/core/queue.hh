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
template <typename T, typename Measurer>
concept bool ItemMeasurer = requires(const T& item, Measurer& measurer) {
    // Measure the weight of the item.
    { measurer(item) } -> size_t;
};
)

template <typename T>
/// Default measurer for queue.
///
/// Returns one for each item.
struct measure_as_one {
    size_t operator()(const T&) const { return 1; }
};

/// Asynchronous single-producer single-consumer queue with limited capacity.
/// There can be at most one producer-side and at most one consumer-side operation active at any time.
/// Operations returning a future are considered to be active until the future resolves.
/// \tparam T the type of items.
/// \tparam Measurer a functor type that maps `T` to `size_t`, that is measures the contribution of
///     each queue item towards the queue's max size. The `Measurer` has to comply with the
///     `ItemMeasurer` concept, that is be conceptually equivalent to an `std::function<size_t(const T&)>`.
///     By default \ref measure_as_one is used, which maps each item to 1.
template <typename T, typename Measurer = measure_as_one<T>>
GCC6_CONCEPT(
    requires ItemMeasurer<T, Measurer>
)
class queue {
    std::queue<T, circular_buffer<T>> _q;
    Measurer _measurer;
    size_t _actual = {};
    size_t _max;
    compat::optional<promise<>> _not_empty;
    compat::optional<promise<>> _not_full;
    std::exception_ptr _ex = nullptr;
private:
    void notify_not_empty();
    void notify_not_full();
public:
    explicit queue(size_t size, Measurer measurer = {});

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

    /// Returns the current number of items in the queue.
    size_t item_count() const { return _q.size(); }

    /// Returns the current size of the queue.
    size_t size() const { return _actual; }

    /// Returns the size limit imposed on the queue during its construction
    /// or by a call to set_max_size(). If the queue is full according to
    /// max size, further items cannot be pushed until some are popped.
    size_t max_size() const { return _max; }

    /// Set the maximum size to a new value. If the queue's max size is reduced,
    /// items already in the queue will not be expunged and the queue will be temporarily
    /// bigger than its max_size.
    void set_max_size(size_t max) {
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

template <typename T, typename Measurer>
inline
queue<T, Measurer>::queue(size_t size, Measurer measurer)
    : _measurer(std::move(measurer))
    , _max(size) {
}

template <typename T, typename Measurer>
inline
void queue<T, Measurer>::notify_not_empty() {
    if (_not_empty) {
        _not_empty->set_value();
        _not_empty = compat::optional<promise<>>();
    }
}

template <typename T, typename Measurer>
inline
void queue<T, Measurer>::notify_not_full() {
    if (_not_full) {
        _not_full->set_value();
        _not_full = compat::optional<promise<>>();
    }
}

template <typename T, typename Measurer>
inline
bool queue<T, Measurer>::push(T&& data) {
    if (_actual < _max) {
        _q.push(std::move(data));
        _actual += _measurer(_q.back());
        notify_not_empty();
        return true;
    } else {
        return false;
    }
}

template <typename T, typename Measurer>
inline
T queue<T, Measurer>::pop() {
    T data = std::move(_q.front());
    _q.pop();
    _actual -= _measurer(data);
    if (_actual < _max) {
        notify_not_full();
    }
    return data;
}

template <typename T, typename Measurer>
inline
future<T> queue<T, Measurer>::pop_eventually() {
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

template <typename T, typename Measurer>
inline
future<> queue<T, Measurer>::push_eventually(T&& data) {
    if (_ex) {
        return make_exception_future<>(_ex);
    }
    if (full()) {
        return not_full().then([this, data = std::move(data)] () mutable {
            _actual += _measurer(data);
            _q.push(std::move(data));
            notify_not_empty();
        });
    } else {
        _actual += _measurer(data);
        _q.push(std::move(data));
        notify_not_empty();
        return make_ready_future<>();
    }
}

template <typename T, typename Measurer>
template <typename Func>
inline
bool queue<T, Measurer>::consume(Func&& func) {
    if (_ex) {
        std::rethrow_exception(_ex);
    }
    bool running = true;
    while (!_q.empty() && running) {
        _actual -= _measurer(_q.front());
        running = func(std::move(_q.front()));
        _q.pop();
    }
    if (!full()) {
        notify_not_full();
    }
    return running;
}

template <typename T, typename Measurer>
inline
bool queue<T, Measurer>::empty() const {
    return _q.empty();
}

template <typename T, typename Measurer>
inline
bool queue<T, Measurer>::full() const {
    return _actual >= _max;
}

template <typename T, typename Measurer>
inline
future<> queue<T, Measurer>::not_empty() {
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

template <typename T, typename Measurer>
inline
future<> queue<T, Measurer>::not_full() {
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

