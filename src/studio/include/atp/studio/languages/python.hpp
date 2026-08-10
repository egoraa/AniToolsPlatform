// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_LANGUAGES_PYTHON_HPP
#define ATP_STUDIO_LANGUAGES_PYTHON_HPP

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include <atp/studio/script_language.hpp>

namespace atp::studio {

/// Class name derived from a module name: PascalCase with a leading `py_` dropped, since a module
/// called `py_averager` is written as class `Averager` — the prefix says which language the module is
/// written in, and repeating it in the class name would say it twice.
/// @param module_name the platform name of the module
/// @return the class name to write into the script
[[nodiscard]] inline std::string python_class_name(std::string_view module_name) {
    std::string_view body = module_name;
    if (body.starts_with("py_") && body.size() > 3) {
        body.remove_prefix(3);
    }
    std::string result;
    bool upper = true;
    for (const char c : body) {
        if (c == '_') {
            upper = true;
            continue;
        }
        result.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        upper = false;
    }
    return result;
}

/// Whether a module name can be written into a script as both an attribute and a class name.
///
/// The second half is why this is not a plain identifier check: the class name is derived, and a name
/// like `_1` is a legal identifier whose derived class would be `1`. Such a name is refused here
/// rather than producing a file Python cannot import.
///
/// `atp` is refused by name, and it is the one collision worth spelling out: the script would land as
/// `python/atp.py` beside the `python/atp/` package, discovery walks the directory sorted and hands
/// the file to `_import_file`, which writes `sys.modules["atp"]` unconditionally — after which every
/// later script's `import atp` resolves to that script instead of the package and the whole folder
/// stops loading. Shadowing works the same way for any importable name, as it does in any directory
/// on `sys.path`; this one is singled out because it takes the bridge's own package down with it.
/// @param name candidate name
/// @return true if the name is usable
[[nodiscard]] inline bool python_name_valid(std::string_view name) {
    if (name.empty() || !detail::name_letter(name.front())) {
        return false;
    }
    if (name == "atp") {
        return false;
    }
    for (const char c : name) {
        if (!detail::name_letter(c) && !detail::name_digit(c)) {
            return false;
        }
    }
    const std::string cls = python_class_name(name);
    return !cls.empty() && std::isalpha(static_cast<unsigned char>(cls.front())) != 0;
}

/// The skeleton script: a module that already runs, so the palette entry is reachable and can be
/// connected before a single line is edited.
/// @param module_name the platform name of the module
/// @return the whole file content
[[nodiscard]] inline std::string render_python_module(std::string_view module_name) {
    static constexpr std::string_view skeleton =
        R"PY("""A platform module written in Python, created by atp_studio.

The bridge reads this file when it loads and builds real platform ports from the class below, so
nothing here is compiled and nothing links the SDK.

Heavy imports belong in initialize rather than at module level: an import at module level runs when
the plugin loads and is paid by every pipeline, including those that never create this module.
"""

import atp


class @CLASS@(atp.Module):
    name = "@NAME@"
    version = (1, 0)

    value = atp.Input(atp.i32)
    result = atp.Output(atp.i32)

    factor = atp.Property(atp.i32, 2)

    def initialize(self):
        self.log("@NAME@ ready")

    def iterate(self):
        value = self.value.take()
        if value is None:
            return atp.IDLE
        self.result.write(value * self.factor.get())
        return atp.BUSY
)PY";
    std::string text(skeleton);
    detail::replace_all(text, "@CLASS@", python_class_name(module_name));
    detail::replace_all(text, "@NAME@", module_name);
    return text;
}

/// What the dialog promises for this language: the file, and the class the script will hold.
/// @param file the script that will be written
/// @param module_name the platform name of the module
/// @return one sentence
[[nodiscard]] inline std::string python_creation_note(const std::filesystem::path& file, std::string_view module_name) {
    return "Creates " + file.string() + " with class " + python_class_name(module_name) +
           ", and puts a bridge and the atp package in the folder if it has none.";
}

/// Python as the studio knows it.
///
/// `missing_dependency_hint` is long because the platform's own message is useless here: both
/// operating systems word a missing dependency as if the plugin itself were broken, and for this
/// bridge the dependency is almost always the CPython runtime. A second loaded copy of this bridge is
/// additionally useless rather than merely redundant — the `Ctx` type lives in a per-DLL static that
/// only the copy winning the `_atp` inittab race fills — but that is a stronger reason for the same
/// rule keep_one_bridge applies to every language, not a trait of its own.
inline constexpr script_language python_language{
    .id = "python",
    .label = "Python",
    .bridge_stem = "atp_python_bridge",
    .scripts_subdir = "python",
    .package_entry = "atp",
    .package_is_directory = true,
    .file_extension = ".py",
    .name_prefix = "py_",
    .path_variable = "ATP_PYTHON_PATH",
    .missing_dependency_hint =
        " — that message is about a library the bridge itself needs, not about the "
        "bridge: the CPython runtime (python3.dll on Windows) has to sit next to the "
        "studio executable or on PATH, and a Python installed for the current user "
        "only often is neither",
    .name_valid = &python_name_valid,
    .render_skeleton = &render_python_module,
    .creation_note = &python_creation_note,
};

}  // namespace atp::studio

#endif
