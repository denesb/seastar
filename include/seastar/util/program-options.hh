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
 * Copyright (C) 2017 ScyllaDB
 */

#pragma once

#include <seastar/core/sstring.hh>
#include <seastar/core/print.hh>

#include <boost/any.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/program_options.hpp>

#include <string>
#include <unordered_map>
#include <vector>

/// \defgroup program-options Program Options
///
/// \brief Infrastructure for configuring a seastar application
///
/// The program-options infrastructure allows configuring seastar both by C++
/// code and by command-line and/or config files. This is achieved by
/// providing a set of self-describing and self-validating value types as well
/// as option groups to allow grouping them into arbitrary tree structures.
/// Seastar modules expose statically declared option structs, which derive from
/// \ref option_group and contain various concrete \ref basic_value members
/// comprising the required configuration. These structs are self-describing, and
/// self-validating, the name of the option group as well as the list of its
/// \ref basic_value member can be queried run-time.

namespace seastar {

namespace program_options {

///
/// \brief Wrapper for command-line options with arbitrary string associations.
///
/// This type, to be used with Boost.Program_options, will result in an option that stores an arbitrary number of
/// string associations.
///
/// Values are specified in the form "key0=value0:[key1=value1:...]". Options of this type can be specified multiple
/// times, and the values will be merged (with the last-provided value for a key taking precedence).
///
/// \note We need a distinct type (rather than a simple type alias) for overload resolution in the implementation, but
/// advertizing our inheritance of \c std::unordered_map would introduce the possibility of memory leaks since STL
/// containers do not declare virtual destructors.
///
class string_map final : private std::unordered_map<sstring, sstring> {
private:
    using base = std::unordered_map<sstring, sstring>;
public:
    using base::value_type;
    using base::key_type;
    using base::mapped_type;

    using base::base;
    using base::at;
    using base::find;
    using base::count;
    using base::emplace;
    using base::clear;
    using base::operator[];
    using base::begin;
    using base::end;

    friend bool operator==(const string_map&, const string_map&);
    friend bool operator!=(const string_map&, const string_map&);
};

inline bool operator==(const string_map& lhs, const string_map& rhs) {
    return static_cast<const string_map::base&>(lhs) == static_cast<const string_map::base&>(rhs);
}

inline bool operator!=(const string_map& lhs, const string_map& rhs) {
    return !(lhs == rhs);
}

///
/// \brief Query the value of a key in a \c string_map, or a default value if the key doesn't exist.
///
sstring get_or_default(const string_map&, const sstring& key, const sstring& def = sstring());

std::istream& operator>>(std::istream& is, string_map&);
std::ostream& operator<<(std::ostream& os, const string_map&);

/// \cond internal

//
// Required implementation hook for Boost.Program_options.
//
void validate(boost::any& out, const std::vector<std::string>& in, string_map*, int);

using list_base_hook = boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>;

/// \endcond

/// \addtogroup program-options
/// @{

/// A tag type used to construct unused \ref option_group and \ref basic_value objects.
struct unused {};

class basic_value;

/// A group of options.
///
/// \ref option_group is the basis for organizing options. It can hold a number
/// of \ref basic_value objects. These are typically also its members:
///
///     struct my_option_group : public option_group {
///         value<> opt1;
///         value<bool> opt2;
///         ...
///
///         my_option_group()
///             : option_group(nullptr, "My option group")
///             , opt1(this, "opt1", ...
///             , opt2(this, "opt2", ...
///             , ...
///         { }
///     };
///
/// Option groups can also be nested. As long as the parents of groups are
/// correctly set, calling \ref add_to() on the top-level option group adds
/// the entire option tree to the \p boost::program_options::options_description
/// object, similarly calling \ref extract_from() will extract the values for
/// the entire tree from the \p boost::program_options::variables_map object.
class option_group : public list_base_hook {
    friend class basic_value;

public:
    using value_list_type = boost::intrusive::list<
            basic_value,
            boost::intrusive::base_hook<list_base_hook>,
            boost::intrusive::constant_time_size<false>>;

    using option_group_list_type = boost::intrusive::list<
            option_group,
            boost::intrusive::base_hook<list_base_hook>,
            boost::intrusive::constant_time_size<false>>;

private:
    option_group* _parent;
    bool _used = true;
    std::string _name;
    value_list_type _values;
    option_group_list_type _subgroups;

public:
    /// Construct an option group.
    ///
    /// \param parent - the parent option-group, this option group will become a
    /// sub option group of the parent group
    /// \param name - the name of the option group
    explicit option_group(option_group* parent, std::string name);
    /// Construct an unused option group.
    ///
    /// \param parent - the parent option-group, this option group will become a
    /// sub option group of the parent group
    /// \param name - the name of the option group
    ///
    /// This option group is unused, it will not be included in the boost
    /// program-options output after calling \ref add_to(), and it will not be
    /// extracted from the boost variables-map after calling \ref extract_from().
    /// Note that this also affects all sub-groups and contained values, even if
    /// they are not marked unused individually.
    explicit option_group(option_group* parent, std::string name, unused);
    option_group(option_group&&);
    option_group(const option_group&) = delete;
    virtual ~option_group() = default;

    option_group& operator=(option_group&&) = delete;
    option_group& operator=(const option_group&) = delete;

    /// Does the option group has any values contained in it?
    operator bool () const { return !_values.empty(); }
    bool used() const { return _used; }
    const std::string& name() const { return _name; }
    const value_list_type& values() const { return _values; }
    value_list_type& values() { return _values; }
    /// Add this option group along with all its sub groups and contained values to \p opts
    ///
    /// This is the integration point if the seastar-options infrastructure into
    /// boost program options. It allows exposing an option-tree to the command
    /// line or a configuration file parser.
    void add_to(boost::program_options::options_description& opts);
    /// Extract the values of this option group along all its sub groups and contained values from \p vm
    ///
    /// This is the integration point if the seastar-options infrastructure into
    /// boost program options. It allows extracting the values of an option-tree
    /// from a \p boost::program_options::variables_map obtained from the
    /// command-line and/or a configuration file.
    void extract_from(const boost::program_options::variables_map& vm);
};

/// A basic configuration option value.
///
/// This serves as the common base-class of all the concrete value types.
class basic_value : public list_base_hook {
    friend class option_group;

public:
    option_group* _group;
    bool _used = true;
    std::string _name;
    std::string _short_name;
    std::string _description;

protected:
    std::string combined_name() const {
        if (_short_name.empty()) {
            return _name;
        }
        return fmt::format("{},{}", _name, _short_name);
    }

public:
    basic_value(option_group& group, bool used, std::string name, std::string short_name, std::string description);
    basic_value(basic_value&&);
    basic_value(const basic_value&) = delete;
    virtual ~basic_value() = default;

    basic_value& operator=(basic_value&&) = delete;
    basic_value& operator=(const basic_value&) = delete;

    bool used() const { return _used; }
    const std::string& name() const { return _name; }
    /// The short-name for values that have a long as well as a short notation on the command line.
    ///
    /// Example: \p --memory|-m
    /// In the example above, \p memory is the name (\ref name()) and \p m is the short-name.
    const std::string& short_name() const { return _short_name; }
    const std::string& description() const { return _description; }

    /// \see option_group::add_to()
    virtual void add_to(boost::program_options::options_description&) = 0;
    /// \see option_group::extract_from()
    virtual void extract_from(const boost::program_options::variables_map&) = 0;
};

/// \brief A value mapping policy which exposes the type directly.
template <typename T>
struct direct_mapping_policy {
    using is_multi_value = std::false_type;
    using target_type = T;
    using raw_type = T;

    raw_type target_to_raw(const target_type& v) { return v; }
    target_type raw_to_target(const raw_type& v) { return v; }
};

/// \brief A value mapping policy which allows exposing enums to the command-line.
///
/// The enum is exposed as a string, and the conversion is done based on a vector
/// of enum-value value-name pairs.
template <typename Enum>
//requires std::is_enum_v<Enum>;
struct enum_mapping_policy {
    using is_multi_value = std::false_type;
    using target_type = Enum;
    using raw_type = std::string;

    struct enum_value {
        Enum enum_value;
        std::string enum_name;
    };
    std::vector<enum_value> enum_values;

    enum_mapping_policy(std::vector<enum_value> enum_values) : enum_values(std::move(enum_values)) { }

    raw_type target_to_raw(const target_type& v) {
        auto it = std::find_if(enum_values.begin(), enum_values.end(), [&v] (const enum_value& ev) {
            return ev.enum_value == v;
        });
        if (it == enum_values.end()) {
            throw std::invalid_argument(fmt::format("unknown enum value for enum: {}", v));
        }
        return it->enum_name;
    }
    target_type raw_to_target(const raw_type& v) {
        auto it = std::find_if(enum_values.begin(), enum_values.end(), [&v] (const enum_value& ev) {
            return ev.enum_name == v;
        });
        if (it == enum_values.end()) {
            throw std::invalid_argument(fmt::format("unknown name for enum: {}", v));
        }
        return it->enum_value;
    }
};

/// A configuration option value.
///
/// Usable from C++ code but can also be exposed to the command-line or a
/// configuration file. It can contain a value of type \p T.
/// Some types like enums, collections or other custom types cannot be directly
/// exposed to the command-line. For these types one can provide an appropriate
/// \ref ValueMappingPolicy type which contains the glue code to map to a
/// primitive type that can in turn exposed. There are two kinds of mapping
/// policies:
///
///     // maps a single value
///     struct my_value_mapping_policy {
///         using is_multi_value = std::false_type; // designates the type - maps a single value
///         using target_type = my_type; // the C++ target type
///         using raw_type = std::string; // the type exposed to the command line (a primitive type supported by boost::program_options)
///
///         raw_type target_to_raw(const target_type& v) { return v; }
///         target_type raw_to_target(const raw_type& v) { return v; }
///     };
///
///     // maps a collection value
///     struct my_value_mapping_policy {
///         using is_multi_value = std::true_type; // designates the type - maps a collection
///         using target_type = std::map<std::string, my_type>; // the C++ target collection type
///         using raw_type = std::string; // the *element* type exposed to the command line (a primitive type supported by boost::program_options)
///
///         std::vector<raw_type> target_to_raw(const target_type& v) { return v; }
///         target_type raw_to_target(const std::vector<raw_type>& v) { return v; }
///     };
///
/// There are two mapping policies provided out-of-the-box:
/// \ref direct_mapping_policy and \ref enum_mapping_policy.
///
/// \tparam T the type of the contained value.
/// \tparam ValueMappingPolicy the policy dictating how to map the C++ target
///   value to the raw value obtained from the command line or config file. For
///   most values this is a direct mapping (\ref direct_mapping_policy), the
///   target type is obtained directly. For some this might involve some
///   conversions, e.g. for enums. The \p ValueMappingPolicy allows customising
///  this for any types.
template <typename T = std::monostate, typename ValueMappingPolicy = direct_mapping_policy<T>>
class value : public basic_value {
    using is_multi_value = typename ValueMappingPolicy::is_multi_value;
    using target_type = typename ValueMappingPolicy::target_type;
    using raw_type = typename ValueMappingPolicy::raw_type;

private:
    std::optional<T> _value;
    bool _defaulted = true;
    ValueMappingPolicy _policy;

private:
    void do_set_value(T value, bool defaulted) {
        _value = std::move(value);
        _defaulted = defaulted;
    }

    template <typename Value>
    void app_option(boost::program_options::options_description& opts, Value&& val) {
        opts.add_options()(combined_name().c_str(), std::move(val), description().c_str());
    }

public:
    /// Construct a value.
    ///
    /// \param group - the group containing this value
    /// \param name - the name of this value
    /// \param short_name - the short name of this value (see \ref basic_value::short_name())
    /// \param default_value - the default value, can be unset
    /// \param policy - the mapping policy (if it has a non-default constructor)
    /// \param description - the description of the value
    value(option_group& group, std::string name, std::string short_name, std::optional<T> default_value, ValueMappingPolicy policy,
            std::string description)
        : basic_value(group, true, std::move(name), std::move(short_name), std::move(description))
        , _value(std::move(default_value))
        , _policy(std::move(policy))
    { }
    value(option_group& group, std::string name, std::optional<T> default_value, ValueMappingPolicy policy, std::string description)
        : value(group, std::move(name), {}, std::move(default_value), std::move(policy), std::move(description))
    { }
    value(option_group& group, std::string name, std::string short_name, std::optional<T> default_value, std::string description)
        : value(group, std::move(name), std::move(short_name), std::move(default_value), {}, std::move(description))
    { }
    value(option_group& group, std::string name, std::optional<T> default_value, std::string description)
        : value(group, std::move(name), {}, std::move(default_value), {}, std::move(description))
    { }
    /// Construct an unused value.
    ///
    /// \ref add_to() and \ref extract_from() is no-op.
    value(option_group& group, std::string name, unused)
        : basic_value(group, false, std::move(name), {}, {})
    { }
    value(value&&) = default;
    /// Is there a contained value?
    operator bool () const { return bool(_value); }
    /// Does this value still contain a default-value?
    bool defaulted() const { return _defaulted; }
    /// Return the contained value, assumes there is one, see \ref operator bool().
    const T& get_value() const { return *_value; }
    void set_default_value(T value) { do_set_value(std::move(value), true); }
    void set_value(T value) { do_set_value(std::move(value), false); }
    virtual void add_to(boost::program_options::options_description& opts) override {
        if constexpr (std::is_same_v<is_multi_value, std::true_type>) {
            app_option(opts, boost::program_options::value<std::vector<raw_type>>());
        } else {
            if (_value) {
                app_option(opts, boost::program_options::value<raw_type>()->default_value(_policy.target_to_raw(*_value)));
            } else {
                app_option(opts, boost::program_options::value<raw_type>());
            }
        }
    }
    virtual void extract_from(const boost::program_options::variables_map& vm) override {
        auto it = vm.find(name());
        if (it == vm.end()) {
            return;
        }
        if constexpr (std::is_same_v<is_multi_value, std::true_type>) {
            _value = _policy.raw_to_target(it->second.template as<std::vector<raw_type>>());
        } else {
            _value = _policy.raw_to_target(it->second.template as<raw_type>());
        }
        _defaulted = false;
    }
};

/// A switch-style configuration option value.
///
/// Contains no value, can be set or not.
template <>
class value<std::monostate> : public basic_value {
    bool _set = false;

public:
    /// Construct a value.
    ///
    /// \param group - the group containing this value
    /// \param name - the name of this value
    /// \param short_name - the short name of this value (see \ref basic_value::short_name())
    /// \param description - the description of the value
    value(option_group& group, std::string name, std::string short_name, std::string description)
        : basic_value(group, true, std::move(name), std::move(short_name), std::move(description))
    { }
    value(option_group& group, std::string name, std::string description)
        : value(group, std::move(name), {}, std::move(description))
    { }
    /// Construct an unused value.
    ///
    /// \ref add_to() and \ref extract_from() is no-op.
    value(option_group& group, std::string name, unused)
        : basic_value(group, false, std::move(name), {}, {})
    { }
    /// Is the option set?
    operator bool () const { return _set; }
    void set_value() { _set = true; }
    void unset_value() { _set = false; }
    virtual void add_to(boost::program_options::options_description& opts) override {
        opts.add_options()(combined_name().c_str(), description().c_str());
    }
    virtual void extract_from(const boost::program_options::variables_map& vm) override {
        _set = vm.count(name());
    }
};

/// A selection value, allows selection from multiple candidates.
///
/// The candidates objects are of an opaque type which may not accessible to
/// whoever is choosing between the available candidates. This allows the user
/// selecting between seastar internal types without exposing them.
/// Each candidate has a name, which is what the users choose based on. Each
/// candidate can also have an associated \ref option_group containing related
/// candidate-specific configuration options, allowing further configuring the
/// selected candidate. The code exposing the candidates should document the
/// concrete types these can be down-casted to.
template <typename T = std::monostate>
class selection_value : public basic_value {
public:
    using deleter = std::function<void(T*)>;
    using value_handle = std::unique_ptr<T, deleter>;
    struct candidate {
        std::string name;
        value_handle value;
        std::unique_ptr<option_group> opts;
    };
    using candidates = std::vector<candidate>;

private:
    static constexpr size_t no_selected_candidate = -1;

private:
    candidates _candidates;
    size_t _selected_candidate = no_selected_candidate;
    bool _defaulted = true;

private:
    size_t find_candidate(const std::string& candidate_name) {
        auto it = find_if(_candidates.begin(), _candidates.end(), [&] (const auto& candidate) {
            return candidate.name == candidate_name;
        });
        if (it == _candidates.end()) {
            throw std::invalid_argument(fmt::format("find_candidate(): failed to find candidate {}", candidate_name));
        }
        return it - _candidates.begin();
    }

    option_group* do_select_candidate(std::string candidate_name, bool defaulted) {
        _selected_candidate = find_candidate(candidate_name);
        _defaulted = defaulted;
        return _candidates.at(_selected_candidate).opts.get();
    }

public:
    /// Construct a value.
    ///
    /// \param group - the group containing this value
    /// \param name - the name of this value
    /// \param short_name - the short name of this value (see \ref basic_value::short_name())
    /// \param candidates - the available candidates
    /// \param default_candidates - the name of the default candidate
    /// \param description - the description of the value
    selection_value(option_group& group, std::string name, candidates candidates, std::string default_candidate, std::string description)
        : basic_value(group, true, std::move(name), {}, std::move(description))
        , _candidates(std::move(candidates))
        , _selected_candidate(find_candidate(default_candidate))
    { }
    selection_value(option_group& group, std::string name, candidates candidates, std::string description)
        : selection_value(group, std::move(name), std::move(candidates), {}, std::move(description))
    { }
    /// Construct an unused value.
    ///
    /// \ref add_to() and \ref extract_from() is no-op.
    selection_value(option_group& group, std::string name, unused)
        : basic_value(group, false, std::move(name), {}, {})
    { }
    /// Was there a candidate selected (default also counts)?
    operator bool () const { return _selected_candidate != no_selected_candidate; }
    /// Is the currently selected candidate the default one?
    bool defaulted() const { return _defaulted; }
    /// Get the name of the currently selected candidate (assumes there is one selected, see \operator bool()).
    const std::string& get_selected_candidate_name() const { return _candidates.at(_selected_candidate).name; }
    /// Get the options of the currently selected candidate (assumes there is one selected, see \operator bool()).
    const option_group* get_selected_candidate_opts() const { return _candidates.at(_selected_candidate).opts.get(); }
    /// Get the options of the currently selected candidate (assumes there is one selected, see \operator bool()).
    option_group* get_selected_candidate_opts() { return _candidates.at(_selected_candidate).opts.get(); }
    T& get_selected_candidate() const { return *_candidates.at(_selected_candidate).value; }
    virtual void add_to(boost::program_options::options_description& opts) override {
        auto value_type = boost::program_options::value<std::string>();
        if (_selected_candidate == no_selected_candidate) {
            opts.add_options()(combined_name().c_str(), std::move(value_type), description().c_str());
        } else {
            opts.add_options()(combined_name().c_str(), value_type->default_value(get_selected_candidate_name()), description().c_str());
        }
        for (auto& candidate : _candidates) {
            if (candidate.opts) {
                candidate.opts->add_to(opts);
            }
        }
    }
    virtual void extract_from(const boost::program_options::variables_map& vm) override {
        if (vm.count(name())) {
            select_candidate(vm[name()].template as<std::string>());
            _defaulted = false;
        }
        for (auto& candidate : _candidates) {
            if (candidate.opts) {
                candidate.opts->extract_from(vm);
            }
        }
    }
    /// Select a candidate.
    ///
    /// \param candidate_name - the name of the to-be-selected candidate.
    option_group* select_candidate(std::string candidate_name) { return do_select_candidate(candidate_name, false); }
    /// Select a candidate and make it the default.
    ///
    /// \param candidate_name - the name of the to-be-selected candidate.
    option_group* select_default_candidate(std::string candidate_name) { return do_select_candidate(candidate_name, true); }
};

/// @}

}

}
