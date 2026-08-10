// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_API_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_API_HPP

/// @file
/// The one place <Python.h> is included, because on MSVC including it is not neutral.
///
/// pyconfig.h auto-links an import library through #pragma comment(lib), and with _DEBUG defined it
/// asks for the debug one, which a normal CPython installation does not ship — the bridge would fail
/// to link in every Debug configuration, naming a file nobody deleted. Undefining _DEBUG across the
/// include selects the release import library and leaves the rest of the translation unit a debug
/// build. What that mismatch could break is confined to the CRT's debug allocator, which nothing here
/// touches: no allocation crosses this boundary in either direction.
///
/// PY_SSIZE_T_CLEAN says that the '#' formats of PyArg_ParseTuple write a Py_ssize_t rather than an
/// int, and it has to be defined before this include or not at all. Since CPython 3.10 there is no
/// second option: a '#' format without it raises SystemError instead of parsing, which took down
/// every ctx.log() call — the one call a script makes on paths that have nothing else to fail. It
/// belongs here rather than beside the parse it serves, since a second include of <Python.h> without
/// it would silently disagree about the width of that argument.

#define PY_SSIZE_T_CLEAN

#if defined(_MSC_VER) && defined(_DEBUG)
#define ATP_PYTHON_RESTORE_DEBUG
#undef _DEBUG
#endif

#include <Python.h>

#ifdef ATP_PYTHON_RESTORE_DEBUG
#define _DEBUG 1
#undef ATP_PYTHON_RESTORE_DEBUG
#endif

#endif
