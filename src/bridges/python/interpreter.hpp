// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_INTERPRETER_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_INTERPRETER_HPP

#include <string>
#include <vector>

#include "python_api.hpp"

namespace atp::bridge {

/// Holds the GIL for a scope.
///
/// Every entry point of the plugin begins with one, because the platform creates a module on the
/// thread that builds the tree and iterates it on the runner's own thread, and PyGILState is the only
/// acquisition that is correct on both.
class gil_lock {
   public:
    gil_lock() : state_(PyGILState_Ensure()) {}

    ~gil_lock() {
        PyGILState_Release(state_);
    }

    gil_lock(const gil_lock&) = delete;
    gil_lock& operator=(const gil_lock&) = delete;
    gil_lock(gil_lock&&) = delete;
    gil_lock& operator=(gil_lock&&) = delete;

   private:
    PyGILState_STATE state_;
};

/// Boots CPython once per process and imports the atp package.
///
/// Py_Finalize is never called: with extension modules in the process it is unsafe, and this library
/// is pinned anyway. The GIL is released before returning, so every caller takes it through gil_lock.
/// @return false, after saying why on stderr, if the interpreter or the package could not be had
[[nodiscard]] bool interpreter_ready();

/// The imported atp package, borrowed. Valid only after interpreter_ready() answered true.
[[nodiscard]] PyObject* package();

/// Directories to scan for scripts: ATP_PYTHON_PATH first, then python/ next to this library.
[[nodiscard]] std::vector<std::string> scan_paths();

}  // namespace atp::bridge

#endif
