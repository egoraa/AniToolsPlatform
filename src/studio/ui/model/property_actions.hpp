#ifndef ATP_STUDIO_UI_PROPERTY_ACTIONS_HPP
#define ATP_STUDIO_UI_PROPERTY_ACTIONS_HPP

#include "model/app_state.hpp"

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

#include <QString>
#include <QStringList>

#include <atp/runtime/pipeline_builder.hpp>
#include <atp/studio/clipboard.hpp>
#include <atp/studio/node_ref.hpp>

namespace atp::studio::ui {

/// Whether a value from the clipboard may be written into a property the target declares. Sharing
/// the name is not enough: the target may narrow the value set (the platform's invariant is that a
/// property's value is always inside its own set), spell the same setting as another kind, or keep
/// it out of the project altogether. Any of those written through would leave the project in a state
/// no editor can produce and only Run would complain about, long after the gesture.
/// @param declared the target's own declaration of the property
/// @param value the value the clipboard carries
/// @return true if the value is one the target could have been given by hand
[[nodiscard]] inline bool value_fits(const property_info& declared, const nlohmann::json& value) {
    if (!declared.persistent) {
        return false;
    }
    switch (declared.kind) {
        case io::property_kind::number:
            if (!value.is_number()) {
                return false;
            }
            break;
        case io::property_kind::boolean:
            if (!value.is_boolean()) {
                return false;
            }
            break;
        case io::property_kind::text:
            if (!value.is_string()) {
                return false;
            }
            break;
    }
    if (declared.options.empty()) {
        return true;
    }
    return std::ranges::find(declared.options, runtime::detail::scalar_to_string(value)) != declared.options.end();
}

/// Lifts the explicitly set property values off a module into the property clipboard.
/// @param group group holding the module ("" is the root)
/// @param name module name within that group
/// @return true if the clipboard was filled; a module with nothing set is not worth copying
inline bool copy_properties(app_state& state,
                            ui_callbacks& callbacks,
                            const std::string& group,
                            const std::string& name) {
    if (state.view->running()) {
        return false;
    }
    const runtime::group_node* g = state.doc.group_at(group);
    if (g == nullptr) {
        return false;
    }
    for (const runtime::child_node& c : g->modules) {
        if (!c.module || c.module->name != name) {
            continue;
        }
        if (c.module->properties.empty()) {
            callbacks.error(QString("'%1' has no property set to copy").arg(QString::fromStdString(name)));
            return false;
        }
        state.clip_properties = studio::property_clip{c.module->factory, c.module->properties};
        callbacks.error(QString("copied %1 property value(s) from '%2'")
                            .arg(state.clip_properties.values.size())
                            .arg(QString::fromStdString(name)));
        return true;
    }
    return false;
}

/// Applies the property clipboard to a module, matching by name and then by what the target would
/// accept (see value_fits). A value that does not fit is skipped and named in the report rather than
/// making the whole paste fail, which is what makes the gesture useful between two factories that
/// share a setting. A property the clipboard says nothing about is left as it is: the gesture hands
/// over the values it carries, it does not make one module a replica of another.
/// @param group group holding the module ("" is the root)
/// @param name module name within that group
/// @return true if the project changed, which is when the caller owes a refresh
inline bool paste_properties(app_state& state,
                             ui_callbacks& callbacks,
                             const std::string& group,
                             const std::string& name) {
    if (state.view->running() || state.clip_properties.empty()) {
        return false;
    }
    const runtime::group_node* g = state.doc.group_at(group);
    if (g == nullptr) {
        return false;
    }
    const runtime::module_node* target = nullptr;
    for (const runtime::child_node& c : g->modules) {
        if (c.module && c.module->name == name) {
            target = &*c.module;
            break;
        }
    }
    if (target == nullptr) {
        return false;
    }
    const module_info* info = state.describe_cached(target->factory, target->factory_version);
    if (info == nullptr) {
        callbacks.error(QString("paste properties: no factory for '%1'").arg(QString::fromStdString(name)));
        return false;
    }

    std::vector<std::pair<std::string, nlohmann::json>> wanted;
    QStringList skipped;
    for (const auto& [prop, value] : state.clip_properties.values) {
        const auto declared =
            std::ranges::find_if(info->properties, [&](const property_info& p) { return p.name == prop; });
        if (declared != info->properties.end() && value_fits(*declared, value)) {
            wanted.emplace_back(prop, value);
        } else {
            skipped << QString::fromStdString(prop);
        }
    }
    if (wanted.empty()) {
        callbacks.error(QString("paste properties: '%1' takes none of %2")
                            .arg(QString::fromStdString(name), skipped.join(QStringLiteral(", "))));
        return false;
    }

    try {
        const project::edit_scope scope(state.doc);
        for (const auto& [prop, value] : wanted) {
            state.doc.set_property(group, name, prop, value);
        }
    } catch (const std::exception& e) {
        callbacks.error(QString::fromStdString(std::string("paste properties: ") + e.what()));
        return false;
    }
    QString note =
        QString("pasted %1 property value(s) onto '%2'").arg(wanted.size()).arg(QString::fromStdString(name));
    if (!skipped.isEmpty()) {
        note += QString("; skipped %1").arg(skipped.join(QStringLiteral(", ")));
    }
    callbacks.error(note);
    return true;
}

}  // namespace atp::studio::ui

#endif
