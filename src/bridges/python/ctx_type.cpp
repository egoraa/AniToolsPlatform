// SPDX-License-Identifier: Apache-2.0
#include "ctx_type.hpp"

#include <cstdint>
#include <vector>

#include "values.hpp"

namespace atp::bridge {
namespace {

struct ctx_object {
    PyObject_HEAD const atp_api* api;
    atp_ctx* ctx;
    const module_slot* slot;
    std::string* scratch;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PyObject* g_type = nullptr;

bool index_of(const std::vector<atp_kind>& kinds, long long index, atp_kind& kind) {
    if (index < 0 || static_cast<std::size_t>(index) >= kinds.size()) {
        PyErr_SetString(PyExc_IndexError, "port index out of range");
        return false;
    }
    kind = kinds[static_cast<std::size_t>(index)];
    return true;
}

ctx_object* self_of(PyObject* self) {
    return reinterpret_cast<ctx_object*>(self);
}

PyObject* read_port(PyObject* self, PyObject* args, bool consuming) {
    ctx_object* object = self_of(self);
    long long index = 0;
    if (PyArg_ParseTuple(args, "L", &index) == 0) {
        return nullptr;
    }
    atp_kind kind = ATP_KIND_I32;
    if (!index_of(object->slot->input_kinds, index, kind)) {
        return nullptr;
    }
    atp_value value{};
    const std::uint32_t port = static_cast<std::uint32_t>(index);
    const int answer = consuming ? object->api->take_input(object->ctx, port, &value)
                                 : object->api->get_input(object->ctx, port, &value);
    if (answer != 1) {
        Py_RETURN_NONE;
    }
    return to_python(value);
}

PyObject* read_property(PyObject* self, PyObject* args, bool consuming) {
    ctx_object* object = self_of(self);
    long long index = 0;
    if (PyArg_ParseTuple(args, "L", &index) == 0) {
        return nullptr;
    }
    atp_kind kind = ATP_KIND_I32;
    if (!index_of(object->slot->property_kinds, index, kind)) {
        return nullptr;
    }
    atp_value value{};
    const std::uint32_t port = static_cast<std::uint32_t>(index);
    const int answer = consuming ? object->api->take_property(object->ctx, port, &value)
                                 : object->api->get_property(object->ctx, port, &value);
    if (answer != 1) {
        Py_RETURN_NONE;
    }
    return to_python(value);
}

PyObject* ctx_get(PyObject* self, PyObject* args) {
    return read_port(self, args, false);
}

PyObject* ctx_take(PyObject* self, PyObject* args) {
    return read_port(self, args, true);
}

PyObject* ctx_prop_get(PyObject* self, PyObject* args) {
    return read_property(self, args, false);
}

PyObject* ctx_prop_take(PyObject* self, PyObject* args) {
    return read_property(self, args, true);
}

PyObject* ctx_write(PyObject* self, PyObject* args) {
    ctx_object* object = self_of(self);
    long long index = 0;
    PyObject* payload = nullptr;
    if (PyArg_ParseTuple(args, "LO", &index, &payload) == 0) {
        return nullptr;
    }
    atp_kind kind = ATP_KIND_I32;
    if (!index_of(object->slot->output_kinds, index, kind)) {
        return nullptr;
    }
    atp_value value{};
    if (!from_python(kind, payload, value, *object->scratch)) {
        return nullptr;
    }
    if (object->api->write_output(object->ctx, static_cast<std::uint32_t>(index), &value) != 1) {
        PyErr_SetString(PyExc_ValueError, "the host refused the value written to this output");
        return nullptr;
    }
    Py_RETURN_NONE;
}

PyObject* ctx_prop_set(PyObject* self, PyObject* args) {
    ctx_object* object = self_of(self);
    long long index = 0;
    PyObject* payload = nullptr;
    if (PyArg_ParseTuple(args, "LO", &index, &payload) == 0) {
        return nullptr;
    }
    atp_kind kind = ATP_KIND_I32;
    if (!index_of(object->slot->property_kinds, index, kind)) {
        return nullptr;
    }
    atp_value value{};
    if (!from_python(kind, payload, value, *object->scratch)) {
        return nullptr;
    }
    if (object->api->set_property(object->ctx, static_cast<std::uint32_t>(index), &value) != 1) {
        PyErr_SetString(PyExc_ValueError, "the host refused the value written to this property");
        return nullptr;
    }
    Py_RETURN_NONE;
}

PyObject* ctx_log(PyObject* self, PyObject* args) {
    ctx_object* object = self_of(self);
    int level = 0;
    const char* text = nullptr;
    Py_ssize_t size = 0;
    if (PyArg_ParseTuple(args, "is#", &level, &text, &size) == 0) {
        return nullptr;
    }
    object->api->log(object->ctx, static_cast<atp_log_level>(level), text, static_cast<std::size_t>(size));
    Py_RETURN_NONE;
}

PyObject* ctx_wake(PyObject* self, PyObject*) {
    ctx_object* object = self_of(self);
    object->api->wake(object->ctx);
    Py_RETURN_NONE;
}

PyObject* ctx_stop_requested(PyObject* self, PyObject*) {
    ctx_object* object = self_of(self);
    return PyBool_FromLong(object->api->stop_requested(object->ctx));
}

void ctx_dealloc(PyObject* self) {
    PyTypeObject* type = Py_TYPE(self);
    PyObject_Free(self);
    Py_DECREF(type);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PyMethodDef g_methods[] = {
    {"get", &ctx_get, METH_VARARGS, "Read a state input without consuming it."},
    {"take", &ctx_take, METH_VARARGS, "Take the next value of an input."},
    {"write", &ctx_write, METH_VARARGS, "Write a value to an output."},
    {"prop_get", &ctx_prop_get, METH_VARARGS, "Read a property value."},
    {"prop_take", &ctx_prop_take, METH_VARARGS, "Read a property value if it changed since the last take."},
    {"prop_set", &ctx_prop_set, METH_VARARGS, "Write a property value."},
    {"log", &ctx_log, METH_VARARGS, "Write one line to the platform's log."},
    {"wake", &ctx_wake, METH_NOARGS, "Ask this module's thread to iterate now."},
    {"stop_requested", &ctx_stop_requested, METH_NOARGS, "Whether the pipeline is stopping."},
    {nullptr, nullptr, 0, nullptr},
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PyType_Slot g_slots[] = {
    {Py_tp_methods, g_methods},
    {Py_tp_dealloc, reinterpret_cast<void*>(&ctx_dealloc)},
    {0, nullptr},
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PyType_Spec g_spec = {"_atp.Ctx", static_cast<int>(sizeof(ctx_object)), 0, Py_TPFLAGS_DEFAULT, g_slots};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PyModuleDef g_module_def = {
    PyModuleDef_HEAD_INIT,
    "_atp",
    "Primitives of the AniToolsPlatform Python bridge.",
    -1,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

extern "C" PyObject* create_ctx_module() {
    PyObject* module = PyModule_Create(&g_module_def);
    if (module == nullptr) {
        return nullptr;
    }
    g_type = PyType_FromSpec(&g_spec);
    if (g_type == nullptr) {
        Py_DECREF(module);
        return nullptr;
    }
    Py_INCREF(g_type);
    if (PyModule_AddObject(module, "Ctx", g_type) != 0) {
        Py_DECREF(g_type);
        Py_DECREF(module);
        return nullptr;
    }
    return module;
}

PyObject* make_ctx(const atp_api* api, atp_ctx* ctx, const module_slot* slot, std::string* scratch) {
    if (g_type == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "the _atp module was never initialised");
        return nullptr;
    }
    auto* object = PyObject_New(ctx_object, reinterpret_cast<PyTypeObject*>(g_type));
    if (object == nullptr) {
        return nullptr;
    }
    object->api = api;
    object->ctx = ctx;
    object->slot = slot;
    object->scratch = scratch;
    return reinterpret_cast<PyObject*>(object);
}

}  // namespace atp::bridge
