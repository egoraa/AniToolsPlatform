// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_LOCAL_RUNTIME_HPP
#define ATP_STUDIO_LOCAL_RUNTIME_HPP

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>
#include <vector>

#include <atp/group.hpp>
#include <atp/module_base.hpp>
#include <atp/studio/runtime_view_base.hpp>
#include <atp/studio/session.hpp>

namespace atp::studio {

namespace detail {

/// The module a dotted path names, or nullptr. Every segment but the last names a group — the same
/// descent runtime::find_property does, without its exceptions, because a view answers with an empty
/// list rather than by throwing.
[[nodiscard]] inline module_base* find_module_at(group& root, const std::string& path) {
    group* current = &root;
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = path.find('.', begin);
        if (dot == std::string::npos) {
            break;
        }
        current = current->find_group(path.substr(begin, dot - begin));
        if (current == nullptr) {
            return nullptr;
        }
        begin = dot + 1;
    }
    return current->find_module(path.substr(begin));
}

}  // namespace detail

/// The local pipeline seen as a runtime view: a thin layer over session, whose only real work is
/// turning the live tree into property rows, so that the inspector no longer walks it itself.
class local_runtime final : public runtime_view_base {
   public:
    /// @param run the session this view reports on; it must outlive the view
    explicit local_runtime(session& run) : run_(&run) {}

    [[nodiscard]] bool running() const override {
        return run_->running();
    }

    [[nodiscard]] std::string error_text() const override {
        const std::exception_ptr error = run_->error();
        if (!error) {
            return {};
        }
        try {
            std::rethrow_exception(error);
        } catch (const std::exception& e) {
            return e.what();
        } catch (...) {
            return "unknown error";
        }
    }

    [[nodiscard]] std::vector<pipeline_runner::thread_stats> stats() const override {
        return run_->stats();
    }

    [[nodiscard]] std::vector<runtime::connection_sample> sample_connections() const override {
        return run_->sample_connections();
    }

    [[nodiscard]] std::vector<group::module_stats> module_metrics() const override {
        return run_->module_metrics();
    }

    [[nodiscard]] std::vector<group::port_stats> input_metrics() const override {
        return run_->input_metrics();
    }

    [[nodiscard]] bool metrics_enabled() const override {
        return run_->metrics_enabled();
    }

    bool set_metrics_enabled(bool on) override {
        return run_->set_metrics_enabled(on);
    }

    [[nodiscard]] std::vector<live_property> live_properties(const std::string& module_path) const override {
        std::vector<live_property> out;
        group* root = run_->live_root();
        if (root == nullptr) {
            return out;
        }
        const module_base* m = detail::find_module_at(*root, module_path);
        if (m == nullptr) {
            return out;
        }
        for (const auto& [name, p] : m->properties().entries()) {
            out.push_back({{name, p->kind(), p->default_string(), p->options(), p->persistent()}, p->to_string()});
        }
        std::ranges::sort(out,
                          [](const live_property& a, const live_property& b) { return a.info.name < b.info.name; });
        return out;
    }

    void set_property(const runtime::property_override& o) override {
        run_->set_property(o);
    }

   private:
    session* run_;
};

}  // namespace atp::studio

#endif
