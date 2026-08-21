// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_JSON_CODEC_HPP
#define ATP_RUNTIME_JSON_CODEC_HPP

#include <optional>
#include <string>
#include <string_view>

#include <atp/config/node.hpp>

namespace atp::runtime {

/// Reads JSON text into the platform's own tree.
///
/// This header names no document library, and that is its entire point: it is included by the runtime
/// umbrella, so an inline body would put a library back into every translation unit that wanted a
/// pipeline. The implementation therefore lives in json_codec.cpp — the one file in the runtime that
/// names one — and replacing the library means rewriting that file and nothing else.
///
/// @param text the document, UTF-8, BOM tolerated
/// @throws config_error for text that does not parse, and for a number too large to hold as an integer
[[nodiscard]] atp::config::node json_parse(std::string_view text);

/// json_parse for a caller with nowhere to put an exception — the value typed into a cell of the
/// studio inspector, or a property's own string form on its way back to a scalar. A failure there is
/// the user still typing, not an error to report.
/// @return nullopt for anything json_parse would have thrown on
[[nodiscard]] std::optional<atp::config::node> try_json_parse(std::string_view text);

/// Writes the tree back as JSON text.
///
/// Object keys come out **sorted**, whatever order the tree holds them in, because the writer behind
/// this is a std::map-backed node. That is deliberate rather than incidental: it is the form every
/// document in this repository was saved in before, so no config file churns on the first save.
///
/// @param value tree to write
/// @param indent spaces per level, or -1 for the compact form
[[nodiscard]] std::string json_dump(const atp::config::node& value, int indent = -1);

}  // namespace atp::runtime

#endif
