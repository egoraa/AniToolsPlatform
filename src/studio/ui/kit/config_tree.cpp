// SPDX-License-Identifier: Apache-2.0
#include "kit/config_tree.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <atp/runtime/config_binding.hpp>

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

QStringList key_step(const QStringList& prefix, std::string_view name) {
    QStringList steps = prefix;
    steps << QLatin1String("k:") + QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    return steps;
}

QStringList index_step(const QStringList& prefix, std::size_t i) {
    QStringList steps = prefix;
    steps << QLatin1String("i:") + QString::number(i);
    return steps;
}

std::string joined(const std::string& prefix, std::string_view name) {
    return prefix.empty() ? std::string(name) : prefix + "." + std::string(name);
}

std::string indexed(const std::string& prefix, std::size_t i) {
    return prefix + "[" + std::to_string(i) + "]";
}

atp::config::node* parent_at(atp::config::node& root, const QStringList& steps) {
    atp::config::node* here = &root;
    for (qsizetype i = 0; i + 1 < steps.size(); ++i) {
        const QString& step = steps.at(i);
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
        here = here->find(body.toStdString());
        if (here == nullptr) {
            return nullptr;
        }
    }
    return here;
}

bool erase_at(atp::config::node& root, const QStringList& steps) {
    if (steps.isEmpty()) {
        return false;
    }
    atp::config::node* parent = parent_at(root, steps);
    if (parent == nullptr) {
        return false;
    }
    const QString& last = steps.back();
    if (last.startsWith(QLatin1String("i:"))) {
        return parent->erase(static_cast<std::size_t>(last.mid(2).toULongLong()));
    }
    return parent->erase(last.mid(2).toStdString());
}

bool same_shape(const atp::module_config& a, const atp::module_config& b) {
    const std::span<const atp::module_config::entry> left = a.entries();
    const std::span<const atp::module_config::entry> right = b.entries();
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].name() != right[i].name() || left[i].kind() != right[i].kind() ||
            left[i].element() != right[i].element()) {
            return false;
        }
        if (left[i].kind() == atp::field_kind::object && !same_shape(left[i].group(), right[i].group())) {
            return false;
        }
        if (left[i].kind() == atp::field_kind::array && left[i].element() == atp::field_kind::object &&
            !same_shape(left[i].element_shape(), right[i].element_shape())) {
            return false;
        }
    }
    return true;
}

void carry_unknown(const atp::module_config& shape, const atp::config::node& from, atp::config::node& into) {
    if (!from.is_object() || !into.is_object()) {
        return;
    }
    for (const auto& [key, value] : from.entries()) {
        const atp::module_config::entry* field = shape.find(key);
        if (field == nullptr) {
            into[key] = value;
            continue;
        }
        if (field->kind() == atp::field_kind::object) {
            atp::config::node* nested = into.find(key);
            atp::config::node merged =
                nested == nullptr ? atp::config::node(atp::config::node::object_type{}) : *nested;
            carry_unknown(field->group(), value, merged);
            if (!merged.entries().empty()) {
                into[key] = std::move(merged);
            }
            continue;
        }
        if (field->kind() != atp::field_kind::array || field->element() != atp::field_kind::object) {
            continue;
        }
        atp::config::node* items = into.find(key);
        if (items == nullptr || !items->is_array() || !value.is_array()) {
            continue;
        }
        const std::size_t count = items->size() < value.size() ? items->size() : value.size();
        for (std::size_t i = 0; i < count; ++i) {
            carry_unknown(field->group_at(i), value[i], (*items)[i]);
        }
    }
}

std::string mismatch(const std::string& path, atp::field_kind expected, const atp::config::node& found) {
    return runtime::detail::config_binding_mismatch(path, runtime::detail::field_kind_name(expected), found);
}

bool listed(const atp::module_config::entry& field, const std::string& text) {
    return field.options().empty() || std::ranges::find(field.options(), text) != field.options().end();
}

bool holds(atp::field_kind kind, const atp::config::node& n) {
    switch (kind) {
        case atp::field_kind::boolean:
            return n.is_bool();
        case atp::field_kind::integer:
            return n.is_int();
        case atp::field_kind::real:
            return n.is_int() || n.is_double();
        case atp::field_kind::string:
            return n.is_string();
        case atp::field_kind::object:
            return n.is_object();
        case atp::field_kind::array:
            return n.is_array();
    }
    return false;
}

void misfits_into(const atp::module_config& shape,
                  const atp::config::node& stored,
                  const std::string& prefix,
                  std::vector<std::string>& found) {
    for (const atp::module_config::entry& field : shape.entries()) {
        const atp::config::node* value = stored.find(field.name());
        if (value == nullptr || value->is_null()) {
            continue;
        }
        const std::string path = prefix + std::string(field.name());
        if (!holds(field.kind(), *value)) {
            found.push_back(mismatch(path, field.kind(), *value));
            continue;
        }
        if (field.kind() == atp::field_kind::string && !listed(field, value->as_string())) {
            found.push_back(runtime::detail::config_binding_not_allowed(path, value->as_string(), field.options()));
            continue;
        }
        if (field.kind() == atp::field_kind::object) {
            misfits_into(field.group(), *value, path + ".", found);
            continue;
        }
        if (field.kind() != atp::field_kind::array) {
            continue;
        }
        for (std::size_t i = 0; i < value->size(); ++i) {
            const atp::config::node& item = (*value)[i];
            const std::string at = path + "[" + std::to_string(i) + "]";
            if (!holds(field.element(), item)) {
                found.push_back(mismatch(at, field.element(), item));
                continue;
            }
            if (field.element() == atp::field_kind::string && !listed(field, item.as_string())) {
                found.push_back(runtime::detail::config_binding_not_allowed(at, item.as_string(), field.options()));
                continue;
            }
            if (field.element() == atp::field_kind::object) {
                misfits_into(field.element_shape(), item, at + ".", found);
            }
        }
    }
}

QString lines_of(const std::vector<std::string>& problems) {
    QString text;
    for (const std::string& problem : problems) {
        if (!text.isEmpty()) {
            text += QLatin1Char('\n');
        }
        text += QString::fromStdString(problem);
    }
    return text;
}

QString cell_text(const atp::module_config::entry& field) {
    if (field.required() && !field.is_set()) {
        return {};
    }
    return QString::fromStdString(field.to_string());
}

}  // namespace

std::vector<std::string> config_misfits(const atp::module_config& shape, const atp::config::node& stored) {
    if (!stored.is_object() && !stored.is_null()) {
        return {"the config itself: expected object, found " +
                std::string(atp::config::node::kind_name(stored.kind()))};
    }
    std::vector<std::string> found;
    misfits_into(shape, stored, {}, found);
    return found;
}

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

bool config_tree::rebuild(const std::string& group_path,
                          const std::string& child,
                          const atp::config::node& stored,
                          std::shared_ptr<const atp::module_config> schema) {
    group_path_ = group_path;
    child_ = child;
    schema_ = std::move(schema);
    factory_ = factory_of();
    if (!adopt(stored)) {
        return false;
    }
    fill();
    return true;
}

bool config_tree::sync(const atp::config::node& stored) {
    if (stored == stored_) {
        return true;
    }
    if (delegate_ != nullptr && delegate_->editing()) {
        return true;
    }
    if (!adopt(stored)) {
        return false;
    }
    fill();
    return true;
}

const module_factory_base* config_tree::factory_of() const {
    const runtime::group_node* group = state_.doc.group_at(group_path_);
    if (group == nullptr) {
        return nullptr;
    }
    for (const runtime::child_node& c : group->modules) {
        if (!c.module || c.module->name != child_) {
            continue;
        }
        return c.module->factory_version ? state_.manager.registry().find(c.module->factory, *c.module->factory_version)
                                         : state_.manager.registry().find(c.module->factory);
    }
    return nullptr;
}

bool config_tree::adopt(const atp::config::node& stored) {
    stored_ = stored;
    own_.reset();
    declined_.clear();
    if (factory_ == nullptr) {
        declined_ = QStringLiteral("the registry no longer answers for this module");
        return false;
    }
    own_ = factory_->make_config();
    if (own_ == nullptr) {
        declined_ = QStringLiteral("this module takes no config");
        return false;
    }
    if (schema_ == nullptr || !same_shape(*own_, *schema_)) {
        own_.reset();
        declined_ = QStringLiteral("the registered module declares a different config");
        return false;
    }
    const std::vector<std::string> misfits = config_misfits(*own_, stored_);
    if (!misfits.empty()) {
        own_.reset();
        declined_ = lines_of(misfits);
        return false;
    }
    const std::vector<std::string> problems = runtime::load_fields(*own_, {stored_, {}, {}, false});
    if (problems.empty()) {
        style::clear_error(tree_);
        return true;
    }
    style::mark_error(tree_, lines_of(problems));
    return true;
}

void config_tree::fill() {
    const QSignalBlocker block(tree_);
    filling_ = true;
    tree_->clear();
    if (own_ == nullptr) {
        filling_ = false;
        return;
    }
    add_fields(nullptr, *own_, {}, {});
    tree_->expandAll();
    tree_->resizeColumnToContents(name_column);
    filling_ = false;
}

void config_tree::add_fields(QTreeWidgetItem* parent,
                             atp::module_config& cfg,
                             const std::string& prefix,
                             const QStringList& steps) {
    for (atp::module_config::entry& field : cfg.entries()) {
        add_field(parent, field, prefix, steps);
    }
}

void config_tree::add_field(QTreeWidgetItem* parent,
                            atp::module_config::entry& field,
                            const std::string& prefix,
                            const QStringList& steps) {
    const std::string path = joined(prefix, field.name());
    const QStringList own_steps = key_step(steps, field.name());
    auto* item = parent == nullptr ? new QTreeWidgetItem(tree_) : new QTreeWidgetItem(parent);
    item->setText(name_column, QString::fromStdString(std::string(field.name())));
    item->setData(name_column, path_role, QString::fromStdString(path));
    item->setData(name_column, steps_role, own_steps);

    if (field.kind() != atp::field_kind::object && field.kind() != atp::field_kind::array && !field.options().empty()) {
        add_choice(item, field, path, own_steps, cell_text(field));
        return;
    }

    switch (field.kind()) {
        case atp::field_kind::object:
            add_fields(item, field.group(), path, own_steps);
            return;
        case atp::field_kind::array:
            add_list(item, field, path, own_steps);
            return;
        case atp::field_kind::boolean: {
            auto* check = new QCheckBox(tree_);
            check->setObjectName(widget_name(QStringLiteral("config_edit"), path));
            check->setChecked(field.value<bool>());
            QObject::connect(check, &QCheckBox::toggled, this,
                             [this, own_steps](bool on) { set_boolean(own_steps, on); });
            tree_->setItemWidget(item, value_column, check);
            return;
        }
        case atp::field_kind::integer:
        case atp::field_kind::real:
        case atp::field_kind::string:
            break;
    }
    item->setText(value_column, cell_text(field));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
}

void config_tree::add_list(QTreeWidgetItem* item,
                           atp::module_config::entry& field,
                           const std::string& path,
                           const QStringList& steps) {
    auto* add = new QPushButton(QStringLiteral("add"), tree_);
    add->setObjectName(widget_name(QStringLiteral("config_add"), path));
    QObject::connect(add, &QPushButton::clicked, this, [this, steps] { add_element(steps); });
    tree_->setItemWidget(item, value_column, add);

    for (std::size_t i = 0; i < field.size(); ++i) {
        const std::string element = indexed(path, i);
        const QStringList element_steps = index_step(steps, i);
        auto* row = new QTreeWidgetItem(item);
        row->setText(name_column, QString::number(i));
        row->setData(name_column, path_role, QString::fromStdString(element));
        row->setData(name_column, steps_role, element_steps);
        auto* remove = new QPushButton(QStringLiteral("remove"), tree_);
        remove->setObjectName(widget_name(QStringLiteral("config_remove"), element));
        QObject::connect(remove, &QPushButton::clicked, this, [this, element_steps] { remove_element(element_steps); });
        tree_->setItemWidget(row, value_column, remove);

        if (field.element() == atp::field_kind::object) {
            add_fields(row, field.group_at(i), element, element_steps);
            continue;
        }
        auto* leaf = new QTreeWidgetItem(row);
        leaf->setText(name_column, QStringLiteral("value"));
        leaf->setData(name_column, path_role, QString::fromStdString(element));
        leaf->setData(name_column, steps_role, element_steps);
        if (!field.options().empty()) {
            add_choice(leaf, field, element, element_steps, QString::fromStdString(field.element_string(i)));
            continue;
        }
        leaf->setText(value_column, QString::fromStdString(field.element_string(i)));
        leaf->setFlags(leaf->flags() | Qt::ItemIsEditable);
    }
}

config_tree::slot config_tree::resolve(const QStringList& steps) const {
    if (own_ == nullptr) {
        return {};
    }
    atp::module_config* cfg = own_.get();
    atp::module_config::entry* field = nullptr;
    std::size_t index = 0;
    bool element = false;

    for (const QString& step : steps) {
        const QString body = step.mid(2);
        if (step.startsWith(QLatin1String("i:"))) {
            if (field == nullptr || element || field->kind() != atp::field_kind::array) {
                return {};
            }
            const std::size_t at = static_cast<std::size_t>(body.toULongLong());
            if (at >= field->size()) {
                return {};
            }
            if (field->element() == atp::field_kind::object) {
                cfg = &field->group_at(at);
                field = nullptr;
                continue;
            }
            index = at;
            element = true;
            continue;
        }
        if (field != nullptr) {
            if (element || field->kind() != atp::field_kind::object) {
                return {};
            }
            cfg = &field->group();
        }
        if (cfg == nullptr) {
            return {};
        }
        field = cfg->find(body.toStdString());
        if (field == nullptr) {
            return {};
        }
        cfg = nullptr;
    }
    return {field, index, element};
}

atp::config::node config_tree::document() const {
    atp::config::node next = runtime::save_fields(*own_);
    carry_unknown(*own_, stored_, next);
    return next;
}

bool config_tree::commit(const atp::config::node& next) {
    if (next == stored_) {
        return true;
    }
    try {
        if (next.is_object() && next.entries().empty()) {
            state_.doc.clear_config(group_path_, child_);
        } else {
            state_.doc.set_config(group_path_, child_, next);
        }
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("config: ") + e.what()));
        (void)adopt(stored_);
        return false;
    }
    (void)adopt(next);
    callbacks_.project_changed();
    return true;
}

void config_tree::show(QTreeWidgetItem* item) {
    const QSignalBlocker block(tree_);
    const slot found = resolve(item->data(name_column, steps_role).toStringList());
    if (found.field == nullptr) {
        return;
    }
    item->setText(value_column, found.element ? QString::fromStdString(found.field->element_string(found.index))
                                              : cell_text(*found.field));
}

void config_tree::add_choice(QTreeWidgetItem* item,
                             const atp::module_config::entry& field,
                             const std::string& path,
                             const QStringList& steps,
                             const QString& current) {
    auto* choice = new QComboBox(tree_);
    choice->setObjectName(widget_name(QStringLiteral("config_edit"), path));
    for (const std::string& option : field.options()) {
        choice->addItem(QString::fromStdString(option));
    }
    choice->setCurrentIndex(choice->findText(current));
    tree_->setItemWidget(item, value_column, choice);
    QObject::connect(choice, &QComboBox::currentIndexChanged, this,
                     [this, steps, choice](int) { set_choice(steps, choice->currentText()); });
}

void config_tree::set_choice(const QStringList& steps, const QString& text) {
    const slot found = resolve(steps);
    if (found.field == nullptr) {
        return;
    }
    const bool parsed = found.element ? found.field->set_element_from_string(found.index, text.toStdString())
                                      : found.field->from_string(text.toStdString());
    if (!parsed) {
        return;
    }
    (void)commit(document());
}

void config_tree::set_boolean(const QStringList& steps, bool on) {
    const slot found = resolve(steps);
    if (found.field == nullptr || found.element) {
        return;
    }
    found.field->set(on);
    (void)commit(document());
}

void config_tree::on_changed(QTreeWidgetItem* item, int column) {
    if (filling_ || column != value_column || item == nullptr || own_ == nullptr) {
        return;
    }
    const QStringList steps = item->data(name_column, steps_role).toStringList();
    if (steps.isEmpty()) {
        return;
    }
    const slot found = resolve(steps);
    if (found.field == nullptr) {
        return;
    }
    const QString text = item->text(value_column);

    if (!found.element && found.field->required() && text.trimmed().isEmpty()) {
        atp::config::node next = document();
        (void)erase_at(next, steps);
        (void)commit(next);
        show(item);
        return;
    }

    const bool parsed = found.element ? found.field->set_element_from_string(found.index, text.toStdString())
                                      : found.field->from_string(text.toStdString());
    if (!parsed) {
        show(item);
        style::mark_error(tree_, QStringLiteral("'%1' is not a value of that field's type").arg(text));
        return;
    }
    (void)commit(document());
    show(item);
}

void config_tree::add_element(const QStringList& steps) {
    const slot found = resolve(steps);
    if (found.field == nullptr || found.element || found.field->kind() != atp::field_kind::array) {
        return;
    }
    found.field->resize(found.field->size() + 1);
    if (commit(document())) {
        fill();
    }
}

void config_tree::remove_element(const QStringList& steps) {
    if (own_ == nullptr || steps.isEmpty() || !steps.back().startsWith(QLatin1String("i:"))) {
        return;
    }
    atp::config::node next = document();
    if (!erase_at(next, steps)) {
        return;
    }
    if (commit(next)) {
        fill();
    }
}

}  // namespace atp::studio::ui
