// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_SOURCE_HPP
#define ATP_RUNTIME_CONFIG_SOURCE_HPP

#include <string>

#include <atp/config/node.hpp>

namespace atp::runtime {

/// A module's config exactly as the document gave it, before anyone decided what to do with it.
///
/// Separate from every type a module sees, because reading a config and handing one over are two
/// jobs: this one is done by the document layer, and it is the same answer whether the module
/// declares fields, parses the bytes itself, or lives behind the C boundary.
struct config_source {
    /// Parsed tree; null for a file of a format the host does not parse, and for no config at all.
    atp::config::node root;

    /// Bytes of the file this came from, verbatim and including a BOM; empty when it came from none.
    std::string text;

    /// Path of that file, named in messages and used to resolve paths written inside the config.
    std::string origin;

    /// Whether the text is all there is. Not derivable from the rest: a .json holding literally
    /// `null` also leaves an empty tree beside a non-empty text.
    bool opaque = false;
};

}  // namespace atp::runtime

#endif
