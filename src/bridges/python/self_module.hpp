// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_SELF_MODULE_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_SELF_MODULE_HPP

#include <filesystem>
#include <string>

namespace atp::bridge {

/// A path as the bytes that cross out of this plugin: UTF-8, which is what plugin_c.h requires of
/// every string on that boundary and what CPython's own C API expects of a `const char*`.
///
/// Never `path::string()`. On Windows it converts through the process code page and **throws**
/// `filesystem_error` for anything the page cannot represent, and a throw out of `atp_module_count`
/// is precisely what the boundary forbids — a module folder named in Cyrillic would take the whole
/// bridge down rather than fail to be listed.
/// @param path the path to encode
/// @return its UTF-8 bytes
[[nodiscard]] std::string to_utf8(const std::filesystem::path& path);

/// Directory this library was loaded from, which is where the atp package and the scripts live.
/// @return the directory, or an empty path if the platform would not say
[[nodiscard]] std::filesystem::path self_directory();

/// Keeps this library in the process for good.
///
/// Not a precaution but a consequence of never finalising the interpreter: the _atp type object, its
/// methods and every function pointer Python holds live in this library, while the interpreter
/// outlives any single load. module_loader unloads on destruction — correctly, it also withdraws the
/// factories — and the next call from Python would land on freed code. Pinning leaves only the code
/// resident; the registration still comes and goes with the loader.
void pin_self();

}  // namespace atp::bridge

#endif
