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
 * Copyright (C) 2019 ScyllaDB, Ltd.
 */

#pragma once

namespace seastar {

class tag_base {
    tag_base* _prev_tag;

public:
    uint64_t memory = 0;

public:
    virtual ~tag_base() = default;
    virtual void on_enter() noexcept = 0;
    virtual void on_leave() noexcept = 0;

    friend void push_tag(tag_base&) noexcept;
    friend tag_base& pop_tag() noexcept;
};

class default_tag : public tag_base {
public:
    virtual void on_enter() noexcept override;
    virtual void on_leave() noexcept override;
};

tag_base* current_tag() noexcept;
void push_tag(tag_base& t) noexcept;
tag_base& pop_tag() noexcept;

class tag_context {
    tag_base* _t;
public:
    tag_context(tag_base* t) noexcept : _t(t) {
        if (_t) {
            push_tag(*_t);
            _t->on_enter();
        }
    }
    tag_context(tag_base& t) noexcept : _t(&t) {
        push_tag(*_t);
        _t->on_enter();
    }
    ~tag_context() {
        if (_t) {
            _t->on_leave();
            pop_tag();
        }
    }
};

template <typename Func>
auto with_tag(tag_base& t, Func&& f) {
    tag_context _{t};
    return f();
}

} // namespace seastar
