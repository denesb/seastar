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
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 */
#pragma once

#include <seastar/core/sstring.hh>
#include <unordered_map>
#include <exception>
#include <iosfwd>
#include <atomic>
#include <mutex>
#include <boost/lexical_cast.hpp>
#include <fmt/format.h>


/// \addtogroup logging
/// @{

namespace seastar {

/// \brief log level used with \see {logger}
/// used with the logger.do_log method.
/// Levels are in increasing order. That is if you want to see debug(3) logs you
/// will also see error(0), warn(1), info(2).
///
enum class log_level {
    error,
    warn,
    info,
    debug,
    trace,
};

std::ostream& operator<<(std::ostream& out, log_level level);
std::istream& operator>>(std::istream& in, log_level& level);
}

// Boost doesn't auto-deduce the existence of the streaming operators for some reason

namespace boost {
template<>
seastar::log_level lexical_cast(const std::string& source);

}

namespace seastar {

class logger;
class logger_registry;

/// A buffer to format log messages into.
///
/// It was designed to allow formatting the entire message into it, without any
/// intermediary buffers. To minimize the amount of reallocations it supports
/// using an external buffer. When this is full it moves to using buffers
/// allocated by itself.
/// To accommodate the most widely used way of formatting messages -- fmt --,
/// it provides an output iterator interface for writing into it.
class log_buf {
    char* _begin;
    char* _end;
    char* _current;
    bool _own_buf;

private:
    void free_buffer() noexcept;
    void realloc_buffer();

public:
    class inserter_iterator {
    public:
        using iterator_category = std::output_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = char;
        using pointer = char*;
        using reference = char&;

    private:
        log_buf* _buf;

    public:
        explicit inserter_iterator(log_buf& buf) : _buf(&buf) { }
        inserter_iterator(const inserter_iterator& o) : _buf(o._buf) { }

        reference operator*() {
            if (__builtin_expect(_buf->_current == _buf->_end, false)) {
                _buf->realloc_buffer();
            }
            return *_buf->_current;
        }
        inserter_iterator& operator++() {
            ++_buf->_current;
            return *this;
        }
        inserter_iterator operator++(int) {
            inserter_iterator o(*this);
            ++(*this);
            return o;
        }
    };

    /// Default ctor.
    ///
    /// Allocates an internal buffer of 512 bytes.
    log_buf();
    /// External buffer ctor.
    ///
    /// Use the external buffer until its full, then switch to internally
    /// allocated buffer. log_buf doesn't take ownership of the buffer.
    log_buf(char* external_buf, size_t size);
    ~log_buf();
    /// Create an output iterator which allows writing into the buffer.
    /// The iterator is stateless, that is all instances have the same position.
    inserter_iterator back_insert_begin() { return inserter_iterator(*this); }
    /// The amount of data written so far.
    const size_t size() const { return _current - _begin; }
    /// The size of the buffer.
    const size_t capacity() const { return _end - _begin; }
    /// Read only pointer to the buffer.
    /// Note that the buffer is not guaranteed to be null terminated. The writer
    /// has to ensure that, should it wish to.
    const char* data() { return _begin; }
    /// A view of the buffer content.
    std::string_view view() const { return std::string_view(_begin, size()); }
};

/// \brief Logger class for ostream or syslog.
///
/// Java style api for logging.
/// \code {.cpp}
/// static seastar::logger logger("lsa-api");
/// logger.info("Triggering compaction");
/// \endcode
/// The output format is: (depending on level)
/// DEBUG  %Y-%m-%d %T,%03d [shard 0] - "your msg" \n
///
class logger {
    sstring _name;
    std::atomic<log_level> _level = { log_level::info };
    static std::ostream* _out;
    static std::atomic<bool> _ostream;
    static std::atomic<bool> _syslog;

public:
    class log_writer {
    public:
        virtual ~log_writer() = default;
        virtual void operator()(log_buf::inserter_iterator) = 0;
    };
    template <typename Func>
    class lambda_log_writer : public log_writer {
        Func _func;
    public:
        lambda_log_writer(Func&& func) : _func(std::forward<Func>(func)) { }
        virtual ~lambda_log_writer() override = default;
        virtual void operator()(log_buf::inserter_iterator it) override { _func(it); }
    };

private:

    // We can't use an std::function<> as it potentially allocates.
    void do_log(log_level level, log_writer& writer);
    void failed_to_log(std::exception_ptr ex) noexcept;
public:
    explicit logger(sstring name);
    logger(logger&& x);
    ~logger();

    bool is_shard_zero() noexcept;

    /// Test if desired log level is enabled
    ///
    /// \param level - enum level value (info|error...)
    /// \return true if the log level has been enabled.
    bool is_enabled(log_level level) const noexcept {
        return __builtin_expect(level <= _level.load(std::memory_order_relaxed), false);
    }

    /// logs to desired level if enabled, otherwise we ignore the log line
    ///
    /// \param fmt - {fmt} style format
    /// \param args - args to print string
    ///
    template <typename... Args>
    void log(log_level level, const char* fmt, Args&&... args) noexcept {
        if (is_enabled(level)) {
            try {
                lambda_log_writer writer([&] (log_buf::inserter_iterator it) {
                    fmt::format_to(it, fmt, std::forward<Args>(args)...);
                });
                do_log(level, writer);
            } catch (...) {
                failed_to_log(std::current_exception());
            }
        }
    }

    /// logs to desired level if enabled, otherwise we ignore the log line
    ///
    /// \param writer a function which writes directly to the underlying log buffer
    ///
    /// This is a low level method for use cases where it is very important to
    /// avoid any allocations. The \arg writer will be passed a
    /// log_buf::inserter_iterator that allows it to write into the log buffer
    /// directly, avoiding the use of any intermediary buffers.
    void log(log_level level, log_writer& writer) noexcept {
        if (is_enabled(level)) {
            try {
                do_log(level, writer);
            } catch (...) {
                failed_to_log(std::current_exception());
            }
        }
    }

    /// Log with error tag:
    /// ERROR  %Y-%m-%d %T,%03d [shard 0] - "your msg" \n
    ///
    /// \param fmt - {fmt} style format
    /// \param args - args to print string
    ///
    template <typename... Args>
    void error(const char* fmt, Args&&... args) noexcept {
        log(log_level::error, fmt, std::forward<Args>(args)...);
    }
    /// Log with warning tag:
    /// WARN  %Y-%m-%d %T,%03d [shard 0] - "your msg" \n
    ///
    /// \param fmt - {fmt} style format
    /// \param args - args to print string
    ///
    template <typename... Args>
    void warn(const char* fmt, Args&&... args) noexcept {
        log(log_level::warn, fmt, std::forward<Args>(args)...);
    }
    /// Log with info tag:
    /// INFO  %Y-%m-%d %T,%03d [shard 0] - "your msg" \n
    ///
    /// \param fmt - {fmt} style format
    /// \param args - args to print string
    ///
    template <typename... Args>
    void info(const char* fmt, Args&&... args) noexcept {
        log(log_level::info, fmt, std::forward<Args>(args)...);
    }
    /// Log with info tag on shard zero only:
    /// INFO  %Y-%m-%d %T,%03d [shard 0] - "your msg" \n
    ///
    /// \param fmt - {fmt} style format
    /// \param args - args to print string
    ///
    template <typename... Args>
    void info0(const char* fmt, Args&&... args) noexcept {
        if (is_shard_zero()) {
            log(log_level::info, fmt, std::forward<Args>(args)...);
        }
    }
    /// Log with debug tag:
    /// DEBUG  %Y-%m-%d %T,%03d [shard 0] - "your msg" \n
    ///
    /// \param fmt - {fmt} style format
    /// \param args - args to print string
    ///
    template <typename... Args>
    void debug(const char* fmt, Args&&... args) noexcept {
        log(log_level::debug, fmt, std::forward<Args>(args)...);
    }
    /// Log with trace tag:
    /// TRACE  %Y-%m-%d %T,%03d [shard 0] - "your msg" \n
    ///
    /// \param fmt - {fmt} style format
    /// \param args - args to print string
    ///
    template <typename... Args>
    void trace(const char* fmt, Args&&... args) noexcept {
        log(log_level::trace, fmt, std::forward<Args>(args)...);
    }

    /// \return name of the logger. Usually one logger per module
    ///
    const sstring& name() const noexcept {
        return _name;
    }

    /// \return current log level for this logger
    ///
    log_level level() const noexcept {
        return _level.load(std::memory_order_relaxed);
    }

    /// \param level - set the log level
    ///
    void set_level(log_level level) noexcept {
        _level.store(level, std::memory_order_relaxed);
    }

    /// Set output stream, default is std::cerr
    static void set_ostream(std::ostream& out) noexcept;

    /// Also output to ostream. default is true
    static void set_ostream_enabled(bool enabled) noexcept;

    /// Also output to stdout. default is true
    [[deprecated("Use set_ostream_enabled instead")]]
    static void set_stdout_enabled(bool enabled) noexcept;

    /// Also output to syslog. default is false
    ///
    /// NOTE: syslog() can block, which will stall the reactor thread.
    ///       this should be rare (will have to fill the pipe buffer
    ///       before syslogd can clear it) but can happen.
    static void set_syslog_enabled(bool enabled) noexcept;
};

/// \brief used to keep a static registry of loggers
/// since the typical use case is to do:
/// \code {.cpp}
/// static seastar::logger("my_module");
/// \endcode
/// this class is used to wrap around the static map
/// that holds pointers to all logs
///
class logger_registry {
    mutable std::mutex _mutex;
    std::unordered_map<sstring, logger*> _loggers;
public:
    /// loops through all registered loggers and sets the log level
    /// Note: this method locks
    ///
    /// \param level - desired level: error,info,...
    void set_all_loggers_level(log_level level);

    /// Given a name for a logger returns the log_level enum
    /// Note: this method locks
    ///
    /// \return log_level for the given logger name
    log_level get_logger_level(sstring name) const;

    /// Sets the log level for a given logger
    /// Note: this method locks
    ///
    /// \param name - name of logger
    /// \param level - desired level of logging
    void set_logger_level(sstring name, log_level level);

    /// Returns a list of registered loggers
    /// Note: this method locks
    ///
    /// \return all registered loggers
    std::vector<sstring> get_all_logger_names();

    /// Registers a logger with the static map
    /// Note: this method locks
    ///
    void register_logger(logger* l);
    /// Unregisters a logger with the static map
    /// Note: this method locks
    ///
    void unregister_logger(logger* l);
    /// Swaps the logger given the from->name() in the static map
    /// Note: this method locks
    ///
    void moved(logger* from, logger* to);
};

logger_registry& global_logger_registry();

enum class logger_timestamp_style {
    none,
    boot,
    real,
};

enum class logger_ostream_type {
    none,
    stdout,
    stderr,
};

struct logging_settings final {
    std::unordered_map<sstring, log_level> logger_levels;
    log_level default_level;
    bool stdout_enabled;
    bool syslog_enabled;
    logger_timestamp_style stdout_timestamp_style = logger_timestamp_style::real;
    logger_ostream_type logger_ostream = logger_ostream_type::stderr;
};

/// Shortcut for configuring the logging system all at once.
///
void apply_logging_settings(const logging_settings&);

/// \cond internal

extern thread_local uint64_t logging_failures;

sstring pretty_type_name(const std::type_info&);

sstring level_name(log_level level);

template <typename T>
class logger_for : public logger {
public:
    logger_for() : logger(pretty_type_name(typeid(T))) {}
};

/// \endcond
} // end seastar namespace

// Pretty-printer for exceptions to be logged, e.g., std::current_exception().
namespace std {
std::ostream& operator<<(std::ostream&, const std::exception_ptr&);
std::ostream& operator<<(std::ostream&, const std::exception&);
std::ostream& operator<<(std::ostream&, const std::system_error&);
}

/// @}
