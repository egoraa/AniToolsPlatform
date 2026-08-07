// SPDX-License-Identifier: Apache-2.0
#include "interpreter.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "ctx_type.hpp"
#include "self_module.hpp"

namespace atp::bridge {
namespace {

PyObject* g_package = nullptr;
bool g_tried = false;

constexpr char path_separator() {
#if defined(_WIN32)
    return ';';
#else
    return ':';
#endif
}

void split_into(const char* value, std::vector<std::string>& paths) {
    const std::string text(value);
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(path_separator(), start);
        const std::string piece = text.substr(start, end == std::string::npos ? end : end - start);
        if (!piece.empty()) {
            paths.push_back(piece);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

void put_package_on_path() {
    PyObject* sys_path = PySys_GetObject("path");
    if (sys_path == nullptr) {
        return;
    }
    const std::string directory = (self_directory() / "python").string();
    PyObject* entry = PyUnicode_FromStringAndSize(directory.c_str(), static_cast<Py_ssize_t>(directory.size()));
    if (entry != nullptr) {
        PyList_Insert(sys_path, 0, entry);
        Py_DECREF(entry);
    }
}

}  // namespace

std::vector<std::string> scan_paths() {
    std::vector<std::string> paths;
#if defined(_MSC_VER)
    char* env = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&env, &length, "ATP_PYTHON_PATH") == 0 && env != nullptr) {
        split_into(env, paths);
        std::free(env);
    }
#else
    if (const char* env = std::getenv("ATP_PYTHON_PATH"); env != nullptr) {
        split_into(env, paths);
    }
#endif
    if (const std::filesystem::path own = self_directory(); !own.empty()) {
        paths.push_back((own / "python").string());
    }
    return paths;
}

bool interpreter_ready() {
    if (g_tried) {
        return g_package != nullptr;
    }
    g_tried = true;
    pin_self();
    if (Py_IsInitialized() == 0) {
        if (PyImport_AppendInittab("_atp", &create_ctx_module) != 0) {
            std::fprintf(stderr, "atp_python_bridge: the _atp module could not be registered\n");
            return false;
        }
        Py_Initialize();
        // Py_Initialize leaves the GIL held by the thread that called it, and that thread is whichever
        // one happened to load the plugin. Every later entry point takes the GIL through gil_lock from
        // the module's own thread, so holding it here would deadlock the runner on its first iterate —
        // a failure no single-threaded test can reach. The saved state is deliberately dropped: the
        // interpreter is never finalised, so there is no later point that would restore it.
        PyEval_SaveThread();
    }
    gil_lock gil;
    // Appending to the inittab only registers the factory; the module — and with it the Ctx type the
    // instances are built from — comes into being on the first import, and nothing in the package has
    // a reason to ask for it. So the bridge asks, rather than leaving the type to be created by
    // whoever happens to import _atp first, or never.
    PyObject* primitives = PyImport_ImportModule("_atp");
    if (primitives == nullptr) {
        PyErr_Print();
        std::fprintf(stderr, "atp_python_bridge: the built-in _atp module could not be created\n");
        return false;
    }
    Py_DECREF(primitives);
    put_package_on_path();
    g_package = PyImport_ImportModule("atp");
    if (g_package == nullptr) {
        PyErr_Print();
        const std::filesystem::path expected = self_directory() / "python" / "atp";
        std::fprintf(stderr, "atp_python_bridge: the atp package was not importable, expected it at %s\n",
                     expected.string().c_str());
        return false;
    }
    return true;
}

PyObject* package() {
    return g_package;
}

}  // namespace atp::bridge
