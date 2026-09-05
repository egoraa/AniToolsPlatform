// SPDX-License-Identifier: Apache-2.0
#include "descriptors.hpp"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>
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
    return {data, static_cast<std::size_t>(size)};
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

void skipped(const std::string& source, const char* reason) {
    std::fprintf(stderr, "atp: %s was not read and is skipped: %s\n", source.empty() ? "?" : source.c_str(),
                 reason == nullptr ? "?" : reason);
}

std::string bad_kind(const char* what, const char* name, long long value) {
    return std::string(what) + " '" + (name == nullptr ? "?" : name) + "' has kind " + std::to_string(value) +
           ", which is outside atp_kind";
}

std::optional<atp_kind> kind_at(PyObject* row, Py_ssize_t index) {
    switch (int_at(row, index)) {
        case ATP_KIND_I32:
            return ATP_KIND_I32;
        case ATP_KIND_I64:
            return ATP_KIND_I64;
        case ATP_KIND_F64:
            return ATP_KIND_F64;
        case ATP_KIND_BOOL:
            return ATP_KIND_BOOL;
        case ATP_KIND_TEXT:
            return ATP_KIND_TEXT;
        case ATP_KIND_BLOB:
            return ATP_KIND_BLOB;
        default:
            return std::nullopt;
    }
}

atp_config_field_kind field_kind_at(PyObject* row, Py_ssize_t index) {
    switch (int_at(row, index)) {
        case ATP_FIELD_BOOL:
            return ATP_FIELD_BOOL;
        case ATP_FIELD_INT:
            return ATP_FIELD_INT;
        case ATP_FIELD_REAL:
            return ATP_FIELD_REAL;
        case ATP_FIELD_OBJECT:
            return ATP_FIELD_OBJECT;
        case ATP_FIELD_ARRAY:
            return ATP_FIELD_ARRAY;
        default:
            return ATP_FIELD_STRING;
    }
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

void read_inputs(module_slot& slot, PyObject* rows, std::string& why) {
    const Py_ssize_t count = rows == nullptr ? 0 : PySequence_Size(rows);
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PySequence_GetItem(rows, i);
        if (row == nullptr) {
            continue;
        }
        atp_input_desc desc{};
        desc.name = keep_at(slot, row, 0);
        if (const std::optional<atp_kind> kind = kind_at(row, 1)) {
            desc.kind = *kind;
        } else if (why.empty()) {
            why = bad_kind("input", desc.name, int_at(row, 1));
        }
        desc.flavor = static_cast<atp_flavor>(int_at(row, 2));
        desc.capacity = static_cast<std::uint32_t>(int_at(row, 3));
        desc.overflow = static_cast<atp_overflow>(int_at(row, 4));
        slot.inputs.push_back(desc);
        slot.input_kinds.push_back(desc.kind);
        Py_DECREF(row);
    }
}

void read_outputs(module_slot& slot, PyObject* rows, std::string& why) {
    const Py_ssize_t count = rows == nullptr ? 0 : PySequence_Size(rows);
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PySequence_GetItem(rows, i);
        if (row == nullptr) {
            continue;
        }
        atp_output_desc desc{};
        desc.name = keep_at(slot, row, 0);
        if (const std::optional<atp_kind> kind = kind_at(row, 1)) {
            desc.kind = *kind;
        } else if (why.empty()) {
            why = bad_kind("output", desc.name, int_at(row, 1));
        }
        slot.outputs.push_back(desc);
        slot.output_kinds.push_back(desc.kind);
        Py_DECREF(row);
    }
}

void read_properties(module_slot& slot, PyObject* rows, std::string& why) {
    const Py_ssize_t count = rows == nullptr ? 0 : PySequence_Size(rows);
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PySequence_GetItem(rows, i);
        if (row == nullptr) {
            continue;
        }
        atp_property_desc desc{};
        desc.name = keep_at(slot, row, 0);
        if (const std::optional<atp_kind> kind = kind_at(row, 1)) {
            desc.kind = *kind;
        } else if (why.empty()) {
            why = bad_kind("property", desc.name, int_at(row, 1));
        }
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

std::vector<atp_config_field_desc>* read_config_fields(module_slot& slot, PyObject* rows) {
    const Py_ssize_t count = rows == nullptr || rows == Py_None ? 0 : PySequence_Size(rows);
    if (count <= 0) {
        return nullptr;
    }
    std::vector<atp_config_field_desc>& into = slot.config_fields.emplace_back();
    into.reserve(static_cast<std::size_t>(count));
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PySequence_GetItem(rows, i);
        if (row == nullptr) {
            continue;
        }
        atp_config_field_desc desc{};
        desc.name = keep_at(slot, row, 0);
        desc.kind = field_kind_at(row, 1);
        PyObject* fallback = PySequence_GetItem(row, 2);
        desc.default_value = fallback == nullptr || fallback == Py_None ? nullptr : keep(slot, fallback);
        Py_XDECREF(fallback);

        PyObject* options = PySequence_GetItem(row, 3);
        const Py_ssize_t option_count = options == nullptr ? 0 : PySequence_Size(options);
        std::vector<const char*>& pointers = slot.option_pointers.emplace_back();
        for (Py_ssize_t o = 0; o < option_count; ++o) {
            pointers.push_back(keep_at(slot, options, o));
        }
        Py_XDECREF(options);
        desc.option_count = static_cast<std::uint32_t>(option_count);
        desc.options = pointers.empty() ? nullptr : pointers.data();

        PyObject* element = PySequence_GetItem(row, 4);
        const bool has_element = element != nullptr && element != Py_None;
        Py_XDECREF(element);
        desc.element = has_element ? field_kind_at(row, 4) : ATP_FIELD_STRING;

        PyObject* children = PySequence_GetItem(row, 5);
        if (const std::vector<atp_config_field_desc>* nested = read_config_fields(slot, children)) {
            desc.fields = nested->data();
            desc.field_count = static_cast<std::uint32_t>(nested->size());
        }
        Py_XDECREF(children);

        into.push_back(desc);
        Py_DECREF(row);
    }
    return &into;
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
    std::string why;
    read_inputs(slot, PyDict_GetItemString(row, "inputs"), why);
    read_outputs(slot, PyDict_GetItemString(row, "outputs"), why);
    read_properties(slot, PyDict_GetItemString(row, "properties"), why);
    if (!why.empty()) {
        skipped(slot.source, ("module '" + slot.name + "': " + why).c_str());
        storage().pop_back();
        return;
    }
    if (const std::vector<atp_config_field_desc>* fields =
            read_config_fields(slot, PyDict_GetItemString(row, "config"))) {
        desc.config_fields = fields->data();
        desc.config_field_count = static_cast<std::uint32_t>(fields->size());
    }
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
