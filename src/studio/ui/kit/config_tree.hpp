// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_CONFIG_TREE_HPP
#define ATP_STUDIO_UI_CONFIG_TREE_HPP

#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include <atp/config/node.hpp>
#include <atp/module/module_config.hpp>

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

/// What of @p stored the rows could not carry back: every declared field the document fills with
/// something other than the form it was declared in, said in the very line runtime::load_fields would
/// say about it — "path: expected integer, found real".
///
/// It is deliberately **not** the same question as "did load_fields have anything to say", and the
/// difference is the whole reason this exists. A required field nobody filled and a key no field
/// declares are both worth reporting and both survive a save: the first is drawn as an empty cell,
/// the second is carried across by carry_unknown. A value of the wrong form survives nothing —
/// load_fields leaves the field unset, save_fields writes nothing for an unset field, and the value
/// is gone from the document the moment anything else on the form is edited. That document is
/// therefore not offered as rows at all.
///
/// The forms accepted here are the ones load_fields accepts, including the one widening it does: a
/// whole number fills a real field, and never the other way round. A declared value set counts as part
/// of the form: a name outside it leaves the field unset exactly as a wrong form does, so it is lost on
/// the next save just the same.
/// @param shape the config as the module declares it
/// @param stored the config as the document holds it
[[nodiscard]] std::vector<std::string> config_misfits(const atp::module_config& shape, const atp::config::node& stored);

/// Editor of a module's declared config: the config **object**, drawn as a tree.
///
/// It edits an object of the module's own config type, taken from the very factory that would build
/// the module, and holds no document tree of its own. Everything this widget used to know — what a
/// default is, which fields exist, what the zero of a list element looks like, what is worth writing
/// down — is a question the object answers instead: runtime::load_fields fills it from the document,
/// module_config::entry says what each field holds and whether anybody wrote it, and
/// runtime::save_fields decides what goes back. Its predecessor re-implemented all three, and losing
/// data was what that cost.
///
/// A key no field declares is **kept but not shown**: there is no row to draw for something the object
/// does not know, and dropping a config written by hand or by a newer plugin would be data loss. Such
/// a key is carried through every save, and the problem load_fields reports about it is put on the
/// tree — the document is wrong, and saying so is not the same as deleting it.
///
/// Built only for an **inline** config of a module that declared a schema. A shared block belongs to
/// the document and may be named by modules whose schemas differ, a "file:" config is somebody else's
/// file, and a module that declared nothing has no fields to draw — all three stay on the JSON editor
/// in the inspector.
///
/// It also **refuses** a document whose values are not of the declared forms, and says so through
/// rebuild()/sync() rather than drawing what it can: the rows are the only thing that would write
/// that document back, and a field they could not read is a field they would erase. The caller then
/// shows the document as text with declined() over it, which is what lets the reader see what is
/// wrong with it and fix it — the form comes back by itself once the document fits again.
class config_tree final : public QWidget {
   public:
    config_tree(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the tree for one module.
    /// @param group_path path of the group holding the module
    /// @param child name of the module inside that group
    /// @param stored the config as the document holds it
    /// @param schema the config the palette holds for this module, which the object taken from the
    ///        registry has to agree with — they part company when the document names a module the
    ///        registry answers differently for, and drawing rows against the wrong declaration would
    ///        rewrite the config into another module's shape. It is held for as long as the widget
    ///        lives, since every later load has to agree with it too, and shared rather than pointed
    ///        at because the palette may re-describe the module while these rows stand
    /// @return whether the rows were drawn; false leaves the widget empty and means the caller has to
    ///         show the document some other way, declined() saying why
    bool rebuild(const std::string& group_path,
                 const std::string& child,
                 const atp::config::node& stored,
                 std::shared_ptr<const atp::module_config> schema);

    /// Follows the document when it changes behind the tree — an undo, an MCP-side edit.
    ///
    /// It takes a **fresh** object from the factory rather than loading into the one on screen:
    /// runtime::load_fields leaves every field the document says nothing about exactly as it found
    /// it, so loading twice into one object would keep the previous document's values behind the new
    /// one's silence.
    /// @return whether the tree still holds the document; false means the edit that arrived is one the
    ///         rows cannot carry, and the caller has to put the text editor in their place
    bool sync(const atp::config::node& stored);

    /// Why the last rebuild() or sync() refused, one problem per line; empty while the rows stand.
    [[nodiscard]] const QString& declined() const {
        return declined_;
    }

   private:
    /// One editable place in the config: the entry that declares it and, for an element of an array
    /// of scalars, which element. It points into the object on screen, so a slot is valid only until
    /// the next load.
    struct slot {
        atp::module_config::entry* field = nullptr;
        std::size_t index = 0;
        bool element = false;
    };

    /// The factory behind the module being edited, found through the document rather than remembered:
    /// the widget is handed a group and a child, and which factory that pair names is the document's
    /// answer, not the palette's.
    [[nodiscard]] const module_factory_base* factory_of() const;

    /// Takes a fresh config from the factory, checks that both the palette's declaration and the
    /// document agree with it, fills it from @p stored and shows whatever load_fields had to say.
    ///
    /// Every load goes through here, sync() included: the two checks are about the pair of things
    /// being brought together and neither of them is settled once. A document that arrived from
    /// somewhere else — an undo, an edit over MCP — is a different document, and the registry may have
    /// been rescanned since the rows were drawn.
    /// @return whether there is an object to draw; false sets declined_ and leaves none
    bool adopt(const atp::config::node& stored);

    /// Builds every row from the object.
    void fill();

    void add_fields(QTreeWidgetItem* parent,
                    atp::module_config& cfg,
                    const std::string& prefix,
                    const QStringList& steps);

    void add_field(QTreeWidgetItem* parent,
                   atp::module_config::entry& field,
                   const std::string& prefix,
                   const QStringList& steps);

    void add_list(QTreeWidgetItem* item,
                  atp::module_config::entry& field,
                  const std::string& path,
                  const QStringList& steps);

    /// Draws a field with a declared value set as a drop-down of exactly those values.
    ///
    /// It is the same rule the property grid keeps, and it is what makes an enumeration editable at
    /// all: typed into a line the name would be checked only after the fact, and every typo would
    /// travel as a refused edit. Which values exist is the entry's business — an enum's name table, or
    /// the set the module listed — so a plain string field with a set gets the drop-down too.
    /// @param current the value to show, empty for a required field nobody has written
    void add_choice(QTreeWidgetItem* item,
                    const atp::module_config::entry& field,
                    const std::string& path,
                    const QStringList& steps,
                    const QString& current);

    /// Finds the place a row addresses.
    ///
    /// Two ways of naming the same place travel with every row. The path ("channels[0].index") is the
    /// human one and names widgets; the steps are the same route as a list of "k:key"/"i:index"
    /// tokens, and every lookup uses those. They are not interchangeable: a declared name may itself
    /// contain '.' or '[', so the human form is ambiguous — parsing it back was how a field called
    /// "a.b" became uneditable and one called "b[c" crashed the process out of a Qt slot.
    [[nodiscard]] slot resolve(const QStringList& steps) const;

    /// The document this config would be saved as: what save_fields thinks is worth writing, plus the
    /// keys no field declares, carried over from what was loaded.
    [[nodiscard]] atp::config::node document() const;

    /// Writes @p next into the project and reloads the object from it.
    /// @return whether it went through; a caller showing a row has to put its value back if not
    bool commit(const atp::config::node& next);

    /// Puts what the object holds back into a row — after a refused edit, and after an accepted one,
    /// since the object is what decides how a value reads.
    void show(QTreeWidgetItem* item);

    void set_boolean(const QStringList& steps, bool on);

    void set_choice(const QStringList& steps, const QString& text);

    void on_changed(QTreeWidgetItem* item, int column);

    void add_element(const QStringList& steps);

    void remove_element(const QStringList& steps);

    app_state& state_;
    ui_callbacks& callbacks_;
    QTreeWidget* tree_ = nullptr;
    value_only_delegate* delegate_ = nullptr;
    std::string group_path_;
    std::string child_;
    const module_factory_base* factory_ = nullptr;

    /// The declaration the palette answered for this module, which every object the registry hands out
    /// has to agree with.
    std::shared_ptr<const atp::module_config> schema_;

    /// The config on screen. Not the document: what the document gets is save_fields of this, and the
    /// difference between the two is the whole reason they are separate.
    config_ptr own_;

    /// The document as it was last read or written, which is both what sync compares against and where
    /// the keys no field declares are kept.
    atp::config::node stored_;

    /// Why there are no rows, empty while there are. Kept rather than only shown, because the widget
    /// that ends up showing it is not this one — a refusal is answered by the inspector putting the
    /// text editor here instead.
    QString declined_;

    bool filling_ = false;
};

}  // namespace atp::studio::ui

#endif
