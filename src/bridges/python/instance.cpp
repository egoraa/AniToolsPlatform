// SPDX-License-Identifier: Apache-2.0
#include "instance.hpp"

#include <new>
#include <string>

#include "ctx_type.hpp"
#include "errors.hpp"
#include "interpreter.hpp"
#include "module_slot.hpp"

namespace atp::bridge {
namespace {

struct instance {
    PyObject* self = nullptr;
    PyObject* ctx_object = nullptr;
    const atp_api* api = nullptr;
    atp_ctx* ctx = nullptr;
    std::string scratch;
    bool has_initialize = false;
    bool has_start = false;
    bool has_stop = false;
};

bool defines(PyObject* object, const char* method) {
    PyObject* attribute = PyObject_GetAttrString(object, method);
    if (attribute == nullptr) {
        PyErr_Clear();
        return false;
    }
    Py_DECREF(attribute);
    return true;
}

atp_status call_lifecycle(instance* state, bool present, const char* method) {
    if (!present) {
        return ATP_OK;
    }
    gil_lock gil;
    PyObject* answer = PyObject_CallMethod(state->self, method, nullptr);
    if (answer == nullptr) {
        report_error(*state->api, state->ctx, method);
        return 1;
    }
    Py_DECREF(answer);
    return ATP_OK;
}

}  // namespace

extern "C" void* instance_create(const atp_api* api, atp_ctx* ctx, void* user_data) {
    const auto* slot = static_cast<const module_slot*>(user_data);
    gil_lock gil;
    auto* state = new (std::nothrow) instance{};
    if (state == nullptr) {
        return nullptr;
    }
    state->api = api;
    state->ctx = ctx;
    state->ctx_object = make_ctx(api, ctx, slot, &state->scratch);
    if (state->ctx_object == nullptr) {
        PyErr_Print();
        delete state;
        return nullptr;
    }
    state->self = PyObject_CallMethod(package(), "_create", "(LO)", slot->python_index, state->ctx_object);
    if (state->self == nullptr) {
        PyErr_Print();
        Py_DECREF(state->ctx_object);
        delete state;
        return nullptr;
    }
    state->has_initialize = defines(state->self, "initialize");
    state->has_start = defines(state->self, "start");
    state->has_stop = defines(state->self, "stop");
    return state;
}

extern "C" void instance_destroy(void* self) {
    auto* state = static_cast<instance*>(self);
    {
        gil_lock gil;
        Py_XDECREF(state->self);
        Py_XDECREF(state->ctx_object);
    }
    delete state;
}

extern "C" atp_status instance_initialize(void* self) {
    auto* state = static_cast<instance*>(self);
    return call_lifecycle(state, state->has_initialize, "initialize");
}

extern "C" atp_status instance_start(void* self) {
    auto* state = static_cast<instance*>(self);
    return call_lifecycle(state, state->has_start, "start");
}

extern "C" atp_status instance_stop(void* self) {
    auto* state = static_cast<instance*>(self);
    return call_lifecycle(state, state->has_stop, "stop");
}

extern "C" atp_work instance_iterate(void* self) {
    auto* state = static_cast<instance*>(self);
    gil_lock gil;
    PyObject* answer = PyObject_CallMethod(state->self, "iterate", nullptr);
    if (answer == nullptr) {
        report_error(*state->api, state->ctx, "iterate");
        return ATP_WORK_ERROR;
    }
    const long long value = PyLong_AsLongLong(answer);
    Py_DECREF(answer);
    if (value == -1 && PyErr_Occurred() != nullptr) {
        report_error(*state->api, state->ctx, "iterate");
        return ATP_WORK_ERROR;
    }
    return value == 0 ? ATP_WORK_BUSY : ATP_WORK_IDLE;
}

}  // namespace atp::bridge
