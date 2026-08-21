// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_CONFIG_TREE_HPP
#define ATP_STUDIO_UI_CONFIG_TREE_HPP

#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <string>
#include <vector>

#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include <atp/config/node.hpp>

namespace atp::studio::ui {

/// Lets the value column be edited and the name column not.
///
/// Qt::ItemIsEditable is a property of the **item**, not of a cell, so setting it to make a value
/// editable makes the field name editable beside it — and a field name is what the module declared,
/// not something a document may rename. Refusing the editor is the only place the distinction can be
/// drawn, since the flag itself cannot express it.
/// It also answers whether a cell editor is open, which QAbstractItemView keeps to itself: sync must
/// not tear the tree down while somebody is typing into it, and the delegate is the one object that
/// sees an editor being created and destroyed.
class value_only_delegate final : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] QWidget* createEditor(QWidget* parent,
                                        const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const override;

    void destroyEditor(QWidget* editor, const QModelIndex& index) const override;

    [[nodiscard]] bool editing() const {
        return editing_;
    }

   private:
    mutable bool editing_ = false;
};

/// Editor of a module's declared config: the config **object**, drawn as a tree.
///
/// The object it shows is materialised — every declared field is in it, taking its default where the
/// document said nothing — so the widget never has to know what a default is or which fields exist. It
/// edits an object and hands the result to strip_defaults, which decides what is worth writing. Both
/// of those live in studio/config_shape.hpp, are free functions over JSON and are tested without Qt;
/// that split is the point of this class, and the reason its predecessor kept losing data was that it
/// had none.
///
/// Built only for an **inline** config of a module that declared a schema. A shared block belongs to
/// the document and may be named by modules whose schemas differ, a "file:" config is somebody else's
/// file, and a module that declared nothing has no types to check against — all three stay on the JSON
/// editor in the inspector.
///
/// A key the schema does not declare is shown like any other, because the tree walks the object rather
/// than the schema. It has no declared type, so it is edited as text and written back in the form it
/// was found in.
class config_tree final : public QWidget {
   public:
    config_tree(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the tree for one module.
    /// @param group_path path of the group holding the module
    /// @param child name of the module inside that group
    /// @param stored the config as the document holds it
    /// @param schema fields the module declared
    void rebuild(const std::string& group_path,
                 const std::string& child,
                 const atp::config::node& stored,
                 const std::vector<config::field_declaration>& schema);

    /// Follows the document when it changes behind the tree — an undo, an MCP-side edit. A change that
    /// alters the shape rebuilds; anything else is pushed into the items that are not being edited.
    void sync(const atp::config::node& stored);

   private:
    /// Adds the children of one object under @p parent, or at the top level when it is nullptr.
    ///
    /// Two ways of naming the same place travel together. @p prefix is the human path ("channels[0].index")
    /// and names widgets; @p steps is the same route as a list of "k:key"/"i:index" tokens and is what
    /// every lookup uses. They are not interchangeable: a key may itself contain '.' or '[', so the
    /// human form is ambiguous — parsing it back was how an undeclared key called "a.b" became
    /// uneditable and one called "b[c" crashed the process out of a Qt slot.
    void add_object(QTreeWidgetItem* parent,
                    const atp::config::node& value,
                    const std::vector<config::field_declaration>& schema,
                    const std::string& prefix,
                    const QStringList& steps);

    /// Adds one entry: a leaf with an editable value, or a branch that recurses.
    void add_entry(QTreeWidgetItem* parent,
                   const std::string& name,
                   const std::string& path,
                   const QStringList& steps,
                   const atp::config::node& value,
                   const config::field_declaration* decl);

    /// Turns what was typed into a value of the entry's declared kind, or nullopt when it does not
    /// parse. An entry with no declaration is typed by the form its value already had.
    ///
    /// Emptying a **required** field yields null rather than the empty value of its type: materialise
    /// shows an unset required field as an empty cell, so an emptied one has to mean the same thing or
    /// the two would look alike and mean differently. A required integer set to 0 is a different case —
    /// the cell is not empty — and is written.
    [[nodiscard]] std::optional<atp::config::node> parse(const std::string& path,
                                                         const QStringList& steps,
                                                         const QString& text) const;

    /// Puts the stored value back into a row whose edit was refused.
    void restore(QTreeWidgetItem* item);

    /// Writes the whole object back through strip_defaults.
    /// @return whether it went through; a caller showing a row has to put its value back if not
    [[nodiscard]] bool commit(const atp::config::node& edited);

    void on_changed(QTreeWidgetItem* item, int column);

    void add_element(const QStringList& steps);

    void remove_element(const QStringList& steps);

    app_state& state_;
    ui_callbacks& callbacks_;
    QTreeWidget* tree_ = nullptr;
    value_only_delegate* delegate_ = nullptr;
    std::string group_path_;
    std::string child_;
    std::vector<config::field_declaration> schema_;

    /// The materialised object on screen. Not the document: what the document gets is strip_defaults
    /// of this, and the difference is the whole reason the two are separate.
    atp::config::node full_;

    /// Declaration behind each path, for the paths that have one.
    std::map<std::string, config::field_declaration> declared_;

    bool filling_ = false;
};

}  // namespace atp::studio::ui

#endif
