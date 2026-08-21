// SPDX-License-Identifier: Apache-2.0
#include "kit/config_tree.hpp"

#include <exception>

#include <QCheckBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <atp/runtime/json_codec.hpp>
#include <atp/studio/config_shape.hpp>

namespace atp::studio::ui {

namespace {

constexpr int name_column = 0;
constexpr int value_column = 1;
constexpr int path_role = Qt::UserRole + 1;
constexpr int steps_role = Qt::UserRole + 2;

QString widget_name(const QString& prefix, const std::string& path) {
    QString name = prefix + QLatin1Char('_') + QString::fromStdString(path);
    name.replace(QLatin1Char('.'), QLatin1Char('_'));
    name.replace(QLatin1Char('['), QLatin1Char('_'));
    name.remove(QLatin1Char(']'));
    return name;
}

QStringList key_step(const QStringList& prefix, const std::string& name) {
    QStringList steps = prefix;
    steps << QLatin1String("k:") + QString::fromStdString(name);
    return steps;
}

QStringList index_step(const QStringList& prefix, std::size_t i) {
    QStringList steps = prefix;
    steps << QLatin1String("i:") + QString::number(i);
    return steps;
}

std::string joined(const std::string& prefix, const std::string& name) {
    return prefix.empty() ? name : prefix + "." + name;
}

std::string indexed(const std::string& prefix, std::size_t i) {
    return prefix + "[" + std::to_string(i) + "]";
}

const config::field_declaration* find_decl(const std::vector<config::field_declaration>& schema,
                                           const std::string& name) {
    for (const config::field_declaration& d : schema) {
        if (d.name == name) {
            return &d;
        }
    }
    return nullptr;
}

atp::config::node* at_path(atp::config::node& root, const QStringList& steps) {
    atp::config::node* here = &root;
    for (const QString& step : steps) {
        const QString body = step.mid(2);
        if (step.startsWith(QLatin1String("i:"))) {
            const std::size_t index = static_cast<std::size_t>(body.toULongLong());
            if (!here->is_array() || index >= here->size()) {
                return nullptr;
            }
            here = &(*here)[index];
            continue;
        }
        if (!here->is_object()) {
            return nullptr;
        }
        atp::config::node* found = here->find(body.toStdString());
        if (found == nullptr) {
            return nullptr;
        }
        here = found;
    }
    return here;
}

std::string path_of(const QStringList& steps) {
    std::string path;
    for (const QString& step : steps) {
        if (step.startsWith(QLatin1String("i:"))) {
            path += "[" + step.mid(2).toStdString() + "]";
            continue;
        }
        if (!path.empty()) {
            path += ".";
        }
        path += step.mid(2).toStdString();
    }
    return path;
}

/// Whether two objects have the same set of keys and the same array lengths all the way down — the
/// only difference a tree of widgets cannot absorb without being built again.
bool same_shape(const atp::config::node& a, const atp::config::node& b) {
    if (a.kind() != b.kind()) {
        return false;
    }
    if (a.is_object()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (const auto& [key, value] : a.entries()) {
            const atp::config::node* other = b.find(key);
            if (other == nullptr || !same_shape(value, *other)) {
                return false;
            }
        }
        return true;
    }
    if (a.is_array()) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!same_shape(a[i], b[i])) {
                return false;
            }
        }
    }
    return true;
}

QString shown(const atp::config::node& value) {
    if (value.is_string()) {
        return QString::fromStdString(value.as_string());
    }
    if (value.is_int() || value.is_double() || value.is_bool()) {
        return QString::fromStdString(runtime::json_dump(value));
    }
    return QString();
}

}  // namespace

QWidget* value_only_delegate::createEditor(QWidget* parent,
                                           const QStyleOptionViewItem& option,
                                           const QModelIndex& index) const {
    if (index.column() != value_column) {
        return nullptr;
    }
    editing_ = true;
    return QStyledItemDelegate::createEditor(parent, option, index);
}

void value_only_delegate::destroyEditor(QWidget* editor, const QModelIndex& index) const {
    editing_ = false;
    QStyledItemDelegate::destroyEditor(editor, index);
}

config_tree::config_tree(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("config_tree"));
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({QStringLiteral("field"), QStringLiteral("value")});
    tree_->setRootIsDecorated(true);
    tree_->setIndentation(12);
    delegate_ = new value_only_delegate(tree_);
    tree_->setItemDelegate(delegate_);

    layout->addWidget(tree_);
    QObject::connect(tree_, &QTreeWidget::itemChanged, this,
                     [this](QTreeWidgetItem* item, int column) { on_changed(item, column); });
}

void config_tree::rebuild(const std::string& group_path,
                          const std::string& child,
                          const atp::config::node& stored,
                          const std::vector<config::field_declaration>& schema) {
    group_path_ = group_path;
    child_ = child;
    schema_ = schema;
    full_ = materialise(schema_, stored);

    const QSignalBlocker block(tree_);
    filling_ = true;
    tree_->clear();
    declared_.clear();
    add_object(nullptr, full_, schema_, {}, {});
    tree_->expandAll();
    tree_->resizeColumnToContents(name_column);
    filling_ = false;
}

void config_tree::sync(const atp::config::node& stored) {
    const atp::config::node next = materialise(schema_, stored);
    if (next == full_) {
        return;
    }
    if (delegate_ != nullptr && delegate_->editing()) {
        return;
    }
    if (!same_shape(next, full_)) {
        rebuild(group_path_, child_, stored, schema_);
        return;
    }
    full_ = next;
    const QSignalBlocker block(tree_);
    for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
        QTreeWidgetItem* item = *it;
        const QStringList steps = item->data(name_column, steps_role).toStringList();
        if (steps.isEmpty() || item->childCount() != 0) {
            continue;
        }
        if (const atp::config::node* here = at_path(full_, steps);
            here != nullptr && !here->is_object() && !here->is_array()) {
            item->setText(value_column, shown(*here));
        }
    }
}

void config_tree::add_object(QTreeWidgetItem* parent,
                             const atp::config::node& value,
                             const std::vector<config::field_declaration>& schema,
                             const std::string& prefix,
                             const QStringList& steps) {
    if (!value.is_object()) {
        return;
    }
    for (const auto& [key, child] : value.entries()) {
        add_entry(parent, key, joined(prefix, key), key_step(steps, key), child, find_decl(schema, key));
    }
}

void config_tree::add_entry(QTreeWidgetItem* parent,
                            const std::string& name,
                            const std::string& path,
                            const QStringList& steps,
                            const atp::config::node& value,
                            const config::field_declaration* decl) {
    auto* item = parent == nullptr ? new QTreeWidgetItem(tree_) : new QTreeWidgetItem(parent);
    item->setText(name_column, QString::fromStdString(name));
    item->setData(name_column, path_role, QString::fromStdString(path));
    item->setData(name_column, steps_role, steps);
    if (decl != nullptr) {
        declared_.emplace(path, *decl);
    }

    if (value.is_object()) {
        add_object(item, value, decl != nullptr ? decl->children : std::vector<config::field_declaration>{}, path,
                   steps);
        return;
    }
    if (value.is_array()) {
        auto* add = new QPushButton(QStringLiteral("add"), tree_);
        add->setObjectName(widget_name(QStringLiteral("config_add"), path));
        QObject::connect(add, &QPushButton::clicked, this, [this, steps] { add_element(steps); });
        tree_->setItemWidget(item, value_column, add);
        for (std::size_t i = 0; i < value.size(); ++i) {
            const std::string element = indexed(path, i);
            const QStringList element_steps = index_step(steps, i);
            const atp::config::node& entry = value[i];
            auto* row = new QTreeWidgetItem(item);
            row->setText(name_column, QString::number(i));
            row->setData(name_column, path_role, QString::fromStdString(element));
            row->setData(name_column, steps_role, element_steps);
            auto* remove = new QPushButton(QStringLiteral("remove"), tree_);
            remove->setObjectName(widget_name(QStringLiteral("config_remove"), element));
            QObject::connect(remove, &QPushButton::clicked, this,
                             [this, element_steps] { remove_element(element_steps); });
            tree_->setItemWidget(row, value_column, remove);
            if (entry.is_object() && decl != nullptr && !decl->children.empty()) {
                add_object(row, entry, decl->children, element, element_steps);
            } else if (entry.is_object()) {
                add_object(row, entry, {}, element, element_steps);
            } else {
                auto* leaf = new QTreeWidgetItem(row);
                leaf->setText(name_column, QStringLiteral("value"));
                leaf->setData(name_column, path_role, QString::fromStdString(element));
                leaf->setData(name_column, steps_role, element_steps);
                leaf->setText(value_column, shown(entry));
                leaf->setFlags(leaf->flags() | Qt::ItemIsEditable);
                if (decl != nullptr) {
                    config::field_declaration item_decl = *decl;
                    item_decl.kind = decl->element;
                    item_decl.required = false;
                    item_decl.children.clear();
                    declared_.insert_or_assign(element, item_decl);
                }
            }
        }
        return;
    }

    if (decl != nullptr && decl->kind == config::field_kind::boolean) {
        auto* check = new QCheckBox(tree_);
        check->setObjectName(widget_name(QStringLiteral("config_edit"), path));
        check->setChecked(value.is_bool() && value.as_bool());
        QObject::connect(check, &QCheckBox::toggled, this, [this, steps](bool on) {
            atp::config::node edited = full_;
            if (atp::config::node* slot = at_path(edited, steps)) {
                *slot = on;
                (void)commit(edited);
            }
        });
        tree_->setItemWidget(item, value_column, check);
        return;
    }
    item->setText(value_column, shown(value));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
}

std::optional<atp::config::node> config_tree::parse(const std::string& path,
                                                    const QStringList& steps,
                                                    const QString& text) const {
    const auto declared = declared_.find(path);
    if (declared != declared_.end() && declared->second.required && text.trimmed().isEmpty()) {
        return atp::config::node();
    }
    config::field_kind kind = config::field_kind::string;
    if (declared != declared_.end()) {
        kind = declared->second.kind;
    } else if (const atp::config::node* current = at_path(const_cast<atp::config::node&>(full_), steps)) {
        kind = current->is_int()      ? config::field_kind::integer
               : current->is_double() ? config::field_kind::real
               : current->is_bool()   ? config::field_kind::boolean
                                      : config::field_kind::string;
    }
    bool ok = false;
    switch (kind) {
        case config::field_kind::integer: {
            const qlonglong parsed = text.toLongLong(&ok);
            return ok ? std::optional<atp::config::node>(static_cast<std::int64_t>(parsed)) : std::nullopt;
        }
        case config::field_kind::real: {
            const double parsed = text.toDouble(&ok);
            return ok ? std::optional<atp::config::node>(parsed) : std::nullopt;
        }
        case config::field_kind::boolean: {
            const QString lowered = text.trimmed().toLower();
            if (lowered == QLatin1String("true")) {
                return true;
            }
            if (lowered == QLatin1String("false")) {
                return false;
            }
            return std::nullopt;
        }
        default:
            break;
    }
    return atp::config::node(text.toStdString());
}

void config_tree::on_changed(QTreeWidgetItem* item, int column) {
    if (filling_ || column != value_column || item == nullptr) {
        return;
    }
    const std::string path = item->data(name_column, path_role).toString().toStdString();
    const QStringList steps = item->data(name_column, steps_role).toStringList();
    if (steps.isEmpty()) {
        return;
    }
    const std::optional<atp::config::node> value = parse(path, steps, item->text(value_column));
    if (!value) {
        style::mark_error(tree_,
                          QStringLiteral("'%1' is not a value of that field's type").arg(item->text(value_column)));
        restore(item);
        return;
    }
    atp::config::node edited = full_;
    atp::config::node* slot = at_path(edited, steps);
    if (slot == nullptr) {
        return;
    }
    *slot = *value;
    if (!commit(edited)) {
        restore(item);
    }
}

void config_tree::restore(QTreeWidgetItem* item) {
    const QSignalBlocker block(tree_);
    const QStringList steps = item->data(name_column, steps_role).toStringList();
    if (const atp::config::node* stored = at_path(full_, steps)) {
        item->setText(value_column, shown(*stored));
    }
}

bool config_tree::commit(const atp::config::node& edited) {
    if (edited == full_) {
        return true;
    }
    const atp::config::node thin = strip_defaults(schema_, edited);
    try {
        if (thin.is_object() && thin.size() == 0) {
            state_.doc.clear_config(group_path_, child_);
        } else {
            state_.doc.set_config(group_path_, child_, thin);
        }
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("config: ") + e.what()));
        return false;
    }
    full_ = edited;
    style::clear_error(tree_);
    callbacks_.project_changed();
    return true;
}

void config_tree::add_element(const QStringList& steps) {
    atp::config::node edited = full_;
    atp::config::node* slot = at_path(edited, steps);
    if (slot == nullptr || !slot->is_array()) {
        return;
    }
    const std::string path = path_of(steps);
    const auto declared = declared_.find(path);
    if (declared != declared_.end() && !declared->second.children.empty()) {
        slot->push_back(materialise(declared->second.children, atp::config::node(atp::config::node::object_type{})));
    } else if (declared != declared_.end()) {
        slot->push_back(detail::zero_of(declared->second.element));
    } else {
        slot->push_back(atp::config::node(std::string()));
    }
    if (commit(edited)) {
        rebuild(group_path_, child_, strip_defaults(schema_, full_), schema_);
    }
}

void config_tree::remove_element(const QStringList& steps) {
    if (steps.isEmpty() || !steps.back().startsWith(QLatin1String("i:"))) {
        return;
    }
    QStringList owner = steps;
    const std::size_t index = static_cast<std::size_t>(owner.takeLast().mid(2).toULongLong());
    atp::config::node edited = full_;
    atp::config::node* slot = at_path(edited, owner);
    if (slot == nullptr || !slot->is_array() || index >= slot->size()) {
        return;
    }
    slot->erase(index);
    if (commit(edited)) {
        rebuild(group_path_, child_, strip_defaults(schema_, full_), schema_);
    }
}

}  // namespace atp::studio::ui
