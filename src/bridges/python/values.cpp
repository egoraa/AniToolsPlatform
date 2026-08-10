// SPDX-License-Identifier: Apache-2.0
#include "values.hpp"

#include <cstdint>
#include <limits>

namespace atp::bridge {

PyObject* to_python(const atp_value& value) {
    switch (value.kind) {
        case ATP_KIND_I32:
            return PyLong_FromLong(value.as.i32);
        case ATP_KIND_I64:
            return PyLong_FromLongLong(value.as.i64);
        case ATP_KIND_F64:
            return PyFloat_FromDouble(value.as.f64);
        case ATP_KIND_BOOL:
            return PyBool_FromLong(value.as.boolean);
        case ATP_KIND_TEXT:
            return PyUnicode_FromStringAndSize(value.as.bytes.data, static_cast<Py_ssize_t>(value.as.bytes.size));
        case ATP_KIND_BLOB:
            return PyBytes_FromStringAndSize(value.as.bytes.data, static_cast<Py_ssize_t>(value.as.bytes.size));
    }
    PyErr_SetString(PyExc_TypeError, "unknown port kind");
    return nullptr;
}

bool from_python(atp_kind kind, PyObject* object, atp_value& out, std::string& scratch) {
    out.kind = kind;
    switch (kind) {
        case ATP_KIND_I32: {
            const long long value = PyLong_AsLongLong(object);
            if (value == -1 && PyErr_Occurred() != nullptr) {
                return false;
            }
            if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
                PyErr_SetString(PyExc_OverflowError, "value does not fit an i32 port");
                return false;
            }
            out.as.i32 = static_cast<std::int32_t>(value);
            return true;
        }
        case ATP_KIND_I64: {
            const long long value = PyLong_AsLongLong(object);
            if (value == -1 && PyErr_Occurred() != nullptr) {
                return false;
            }
            out.as.i64 = value;
            return true;
        }
        case ATP_KIND_F64: {
            const double value = PyFloat_AsDouble(object);
            if (value == -1.0 && PyErr_Occurred() != nullptr) {
                return false;
            }
            out.as.f64 = value;
            return true;
        }
        case ATP_KIND_BOOL: {
            const int value = PyObject_IsTrue(object);
            if (value < 0) {
                return false;
            }
            out.as.boolean = value;
            return true;
        }
        case ATP_KIND_TEXT: {
            Py_ssize_t size = 0;
            const char* data = PyUnicode_AsUTF8AndSize(object, &size);
            if (data == nullptr) {
                return false;
            }
            scratch.assign(data, static_cast<std::size_t>(size));
            out.as.bytes.data = scratch.data();
            out.as.bytes.size = scratch.size();
            return true;
        }
        case ATP_KIND_BLOB: {
            Py_buffer view{};
            if (PyObject_GetBuffer(object, &view, PyBUF_SIMPLE) != 0) {
                return false;
            }
            scratch.assign(static_cast<const char*>(view.buf), static_cast<std::size_t>(view.len));
            PyBuffer_Release(&view);
            out.as.bytes.data = scratch.data();
            out.as.bytes.size = scratch.size();
            return true;
        }
    }
    PyErr_SetString(PyExc_TypeError, "unknown port kind");
    return false;
}

PyObject* config_to_python(const atp_api& api, atp_ctx* ctx, std::uint32_t node) {
    if (api.struct_size < sizeof(atp_api)) {
        PyErr_SetString(PyExc_RuntimeError, "the host is older than the config accessors");
        return nullptr;
    }
    switch (api.config_kind(ctx, node)) {
        case ATP_CONFIG_NULL:
            Py_RETURN_NONE;
        case ATP_CONFIG_ARRAY: {
            const std::uint32_t count = api.config_size(ctx, node);
            PyObject* list = PyList_New(static_cast<Py_ssize_t>(count));
            if (list == nullptr) {
                return nullptr;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                PyObject* item = config_to_python(api, ctx, api.config_child_at(ctx, node, i));
                if (item == nullptr) {
                    Py_DECREF(list);
                    return nullptr;
                }
                if (PyList_SetItem(list, static_cast<Py_ssize_t>(i), item) != 0) {
                    Py_DECREF(list);
                    return nullptr;
                }
            }
            return list;
        }
        case ATP_CONFIG_OBJECT: {
            const std::uint32_t count = api.config_size(ctx, node);
            PyObject* dict = PyDict_New();
            if (dict == nullptr) {
                return nullptr;
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                const char* key = nullptr;
                std::size_t length = 0;
                if (api.config_key_at(ctx, node, i, &key, &length) == 0) {
                    Py_DECREF(dict);
                    PyErr_SetString(PyExc_RuntimeError, "a config entry lost its key");
                    return nullptr;
                }
                PyObject* value = config_to_python(api, ctx, api.config_child_at(ctx, node, i));
                if (value == nullptr) {
                    Py_DECREF(dict);
                    return nullptr;
                }
                const int stored = PyDict_SetItemString(dict, std::string(key, length).c_str(), value);
                Py_DECREF(value);
                if (stored != 0) {
                    Py_DECREF(dict);
                    return nullptr;
                }
            }
            return dict;
        }
        default:
            break;
    }
    atp_value scalar;
    if (api.config_value_of(ctx, node, &scalar) == 0) {
        PyErr_SetString(PyExc_RuntimeError, "a config value could not be read");
        return nullptr;
    }
    return to_python(scalar);
}

}  // namespace atp::bridge
