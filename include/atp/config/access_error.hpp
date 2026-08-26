// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_CONFIG_ACCESS_ERROR_HPP
#define ANITOOLSPLATFORM_CONFIG_ACCESS_ERROR_HPP

#include <stdexcept>
#include <string>

namespace atp::config {

/// Reaching into a config node went wrong: a key is missing, an index is past the end, the value
/// found is of another form, or a path does not parse.
///
/// The message names the key whenever the failing call knew it — int_at("rate") can say which key
/// disappointed it, as_int() on a value already in hand can only name the form it found. A full path
/// through the tree is deliberately absent here: it would require every value to know its parent;
/// raw_config::at, which does know the path, spells it out itself.
///
/// Named access_error rather than error because atp::runtime::config_error is a different failure at
/// a different layer and the two used to be told apart by their namespaces alone: this one means one
/// node or one path is wrong, that one means the host could not read, validate or build a config.
class access_error : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

}  // namespace atp::config

#endif
