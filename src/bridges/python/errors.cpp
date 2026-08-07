// SPDX-License-Identifier: Apache-2.0
#include "errors.hpp"

#include <string>

#include "python_api.hpp"

namespace atp::bridge {
namespace {

void append(std::string& text, PyObject* line) {
    Py_ssize_t size = 0;
    const char* data = PyUnicode_AsUTF8AndSize(line, &size);
    if (data == nullptr) {
        PyErr_Clear();
        return;
    }
    text.append(data, static_cast<std::size_t>(size));
}

std::string formatted(PyObject* type, PyObject* value, PyObject* traceback) {
    PyObject* module = PyImport_ImportModule("traceback");
    if (module == nullptr) {
        PyErr_Clear();
        return {};
    }
    PyObject* lines =
        PyObject_CallMethod(module, "format_exception", "(OOO)", type == nullptr ? Py_None : type,
                            value == nullptr ? Py_None : value, traceback == nullptr ? Py_None : traceback);
    Py_DECREF(module);
    if (lines == nullptr) {
        PyErr_Clear();
        return {};
    }
    std::string text;
    const Py_ssize_t count = PySequence_Size(lines);
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* line = PySequence_GetItem(lines, i);
        if (line != nullptr) {
            append(text, line);
            Py_DECREF(line);
        }
    }
    Py_DECREF(lines);
    return text;
}

}  // namespace

void report_error(const atp_api& api, atp_ctx* ctx, const char* where) {
    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    std::string text = std::string(where) + " raised: " + formatted(type, value, traceback);
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
    api.set_error(ctx, text.c_str(), text.size());
}

}  // namespace atp::bridge
