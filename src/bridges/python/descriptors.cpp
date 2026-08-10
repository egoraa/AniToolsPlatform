// SPDX-License-Identifier: Apache-2.0
#include "descriptors.hpp"

#include <cstdint>
#include <deque>
#include <string>

#include "instance.hpp"
#include "interpreter.hpp"
#include "module_slot.hpp"
#include "self_module.hpp"

namespace atp::bridge {
namespace {

std::deque<module_slot>& storage() {
    static std::deque<module_slot> value;
    return value;
}

std::deque<std::vector<atp_module_desc>>& batches() {
    static std::deque<std::vector<atp_module_desc>> value;
    return value;
}

std::vector<atp_module_desc>& batch() {
    if (batches().empty()) {
        batches().emplace_back();
    }
    return batches().back();
}

long long int_at(PyObject* sequence, Py_ssize_t index) {
    PyObject* item = PySequence_GetItem(sequence, index);
    if (item == nullptr) {
        PyErr_Clear();
        return 0;
    }
    const long long value = PyLong_AsLongLong(item);
    Py_DECREF(item);
    if (value == -1 && PyErr_Occurred() != nullptr) {
        PyErr_Clear();
        return 0;
    }
    return value;
}

std::string text_of(PyObject* value) {
    if (value == nullptr) {
        return {};
    }
    Py_ssize_t size = 0;
    const char* data = PyUnicode_AsUTF8AndSize(value, &size);
    if (data == nullptr) {
        PyErr_Clear();
        return {};
    }
    return std::string(data, static_cast<std::size_t>(size));
}

long long int_of(PyObject* dict, const char* key) {
    PyObject* value = PyDict_GetItemString(dict, key);
    if (value == nullptr) {
        return 0;
    }
    const long long number = PyLong_AsLongLong(value);
    if (number == -1 && PyErr_Occurred() != nullptr) {
        PyErr_Clear();
        return 0;
    }
    return number;
}

const char* keep(module_slot& slot, PyObject* value) {
    slot.texts.push_back(text_of(value));
    return slot.texts.back().c_str();
}

const char* keep_at(module_slot& slot, PyObject* row, Py_ssize_t index) {
    PyObject* item = PySequence_GetItem(row, index);
    const char* kept = keep(slot, item);
    Py_XDECREF(item);
    return kept;
}

void read_inputs(module_slot& slot, PyObject* rows) {
    const Py_ssize_t count = rows == nullptr ? 0 : PySequence_Size(rows);
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PySequence_GetItem(rows, i);
        if (row == nullptr) {
            continue;
        }
        atp_input_desc desc{};
        desc.name = keep_at(slot, row, 0);
        desc.kind = static_cast<atp_kind>(int_at(row, 1));
        desc.flavor = static_cast<atp_flavor>(int_at(row, 2));
        desc.capacity = static_cast<std::uint32_t>(int_at(row, 3));
        desc.overflow = static_cast<atp_overflow>(int_at(row, 4));
        slot.inputs.push_back(desc);
        slot.input_kinds.push_back(desc.kind);
        Py_DECREF(row);
    }
}

void read_outputs(module_slot& slot, PyObject* rows) {
    const Py_ssize_t count = rows == nullptr ? 0 : PySequence_Size(rows);
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PySequence_GetItem(rows, i);
        if (row == nullptr) {
            continue;
        }
        atp_output_desc desc{};
        desc.name = keep_at(slot, row, 0);
        desc.kind = static_cast<atp_kind>(int_at(row, 1));
        slot.outputs.push_back(desc);
        slot.output_kinds.push_back(desc.kind);
        Py_DECREF(row);
    }
}

void read_properties(module_slot& slot, PyObject* rows) {
    const Py_ssize_t count = rows == nullptr ? 0 : PySequence_Size(rows);
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PySequence_GetItem(rows, i);
        if (row == nullptr) {
            continue;
        }
        atp_property_desc desc{};
        desc.name = keep_at(slot, row, 0);
        desc.kind = static_cast<atp_kind>(int_at(row, 1));
        desc.default_value = keep_at(slot, row, 2);
        PyObject* options = PySequence_GetItem(row, 3);
        const Py_ssize_t option_count = options == nullptr ? 0 : PySequence_Size(options);
        std::vector<const char*>& pointers = slot.option_pointers.emplace_back();
        pointers.reserve(static_cast<std::size_t>(option_count));
        for (Py_ssize_t o = 0; o < option_count; ++o) {
            pointers.push_back(keep_at(slot, options, o));
        }
        Py_XDECREF(options);
        desc.option_count = static_cast<std::uint32_t>(option_count);
        desc.options = pointers.empty() ? nullptr : pointers.data();
        PyObject* persistent = PySequence_GetItem(row, 4);
        desc.persistent = persistent == nullptr ? 1 : PyObject_IsTrue(persistent);
        Py_XDECREF(persistent);
        slot.properties.push_back(desc);
        slot.property_kinds.push_back(desc.kind);
        Py_DECREF(row);
    }
}

void build_one(PyObject* row) {
    module_slot& slot = storage().emplace_back();
    slot.name = text_of(PyDict_GetItemString(row, "name"));
    slot.source = text_of(PyDict_GetItemString(row, "source"));
    slot.python_index = int_of(row, "index");

    atp_module_desc desc{};
    desc.struct_size = static_cast<std::uint32_t>(sizeof(atp_module_desc));
    desc.name = slot.name.c_str();
    desc.source = slot.source.empty() ? nullptr : slot.source.c_str();
    if (PyObject* version = PyDict_GetItemString(row, "version"); version != nullptr) {
        const Py_ssize_t parts = PySequence_Size(version);
        const Py_ssize_t used = parts > 4 ? 4 : parts;
        for (Py_ssize_t i = 0; i < used; ++i) {
            desc.version[i] = static_cast<std::uint32_t>(int_at(version, i));
        }
        desc.version_count = static_cast<std::uint32_t>(used);
    }
    read_inputs(slot, PyDict_GetItemString(row, "inputs"));
    read_outputs(slot, PyDict_GetItemString(row, "outputs"));
    read_properties(slot, PyDict_GetItemString(row, "properties"));
    desc.inputs = slot.inputs.empty() ? nullptr : slot.inputs.data();
    desc.input_count = static_cast<std::uint32_t>(slot.inputs.size());
    desc.outputs = slot.outputs.empty() ? nullptr : slot.outputs.data();
    desc.output_count = static_cast<std::uint32_t>(slot.outputs.size());
    desc.properties = slot.properties.empty() ? nullptr : slot.properties.data();
    desc.property_count = static_cast<std::uint32_t>(slot.properties.size());
    desc.user_data = &slot;
    desc.create = &instance_create;
    desc.destroy = &instance_destroy;
    desc.initialize = &instance_initialize;
    desc.start = &instance_start;
    desc.stop = &instance_stop;
    desc.iterate = &instance_iterate;
    batch().push_back(desc);
}

PyObject* path_list() {
    PyObject* list = PyList_New(0);
    if (list == nullptr) {
        return nullptr;
    }
    for (const std::filesystem::path& path : scan_paths()) {
        const std::string text = to_utf8(path);
        PyObject* item = PyUnicode_FromStringAndSize(text.c_str(), static_cast<Py_ssize_t>(text.size()));
        if (item != nullptr) {
            PyList_Append(list, item);
            Py_DECREF(item);
        }
    }
    return list;
}

}  // namespace

const std::vector<atp_module_desc>& last_batch() {
    return batch();
}

const std::vector<atp_module_desc>& discover() {
    batches().emplace_back();
    if (!interpreter_ready()) {
        return batch();
    }
    gil_lock gil;
    PyObject* paths = path_list();
    if (paths == nullptr) {
        PyErr_Print();
        return batch();
    }
    PyObject* rows = PyObject_CallMethod(package(), "_discover", "(O)", paths);
    Py_DECREF(paths);
    if (rows == nullptr) {
        PyErr_Print();
        return batch();
    }
    const Py_ssize_t count = PySequence_Size(rows);
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PySequence_GetItem(rows, i);
        if (row != nullptr) {
            build_one(row);
            Py_DECREF(row);
        }
    }
    Py_DECREF(rows);
    return batch();
}

}  // namespace atp::bridge
