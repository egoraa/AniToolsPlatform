// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP
#define ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP

#include "kit/config_tree.hpp"
#include "kit/expose_editor.hpp"
#include "kit/property_grid.hpp"
#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <functional>
#include <string>
#include <vector>

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <atp/config/node.hpp>

namespace atp::studio::ui {

/// Inspector: what the canvas selection is — a module with its properties, a group with its thread
/// and its exports, and the current group itself when nothing is selected. The form is rebuilt only
/// when its identity changes — a different selection or a different module description; an edit of a
/// value merely pushes the new values into the widgets already on screen.
class inspector_widget final : public QWidget {
   public:
    inspector_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the form from the current selection and project.
    void refresh();

    /// Commits the config editor when it loses focus. Text is parsed there and a project edit happens
    /// only if it parsed and actually differs, so nothing malformed and nothing unchanged reaches the
    /// document — an editor holding a half-typed object must not push a snapshot onto undo.
    ///
    /// The module is addressed by the group and name captured when the editor was built, never by the
    /// current selection: hiding the editor in clear_body() delivers this event, and by then the
    /// selection has already moved to what is about to be shown. Reading it here would commit the old
    /// text against the new group — silently lost where no such module exists, and written into the
    /// wrong module where the new group happens to hold that name.
    ///
    /// The same filter serves the shared-block editor, which is a second editor and a second target:
    /// its text goes to the document's "configs" and the module's own node is left holding the
    /// reference.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    [[nodiscard]] std::string form_key() const;

    void rebuild();

    void sync();

    void sync_config();

    [[nodiscard]] const atp::config::node* effective_config() const;

    void apply_lock();

    void clear_body();

    style::section add_section(const QString& title);

    /// Runs an edit of the project, turning a refusal into an error message rather than a crash.
    ///
    /// @param context prefix of the message, naming the edit that failed
    /// @param operation the edit itself
    /// @return whether it went through — a caller that dropped state of its own in anticipation has to
    ///         put it back, since a refused edit leaves the form standing and still in use
    bool guard(const char* context, const std::function<void()>& operation);

    void commit_rename(const std::string& old_name, QLineEdit* edit);

    void build_module_section(const runtime::module_node& m);

    /// One editor for the config that actually reaches the module, plus a source row saying where it
    /// comes from: "(inline)" or the name of a shared block. The reference is deliberately **not**
    /// shown as the editor's text — a module's node holding "printing" told the reader nothing about
    /// what "printing" is and offered no way to find out, which made the reference form a dead end in
    /// the GUI even though the model has carried it all along.
    ///
    /// What the node stores is untouched by any of this: an inline config stays an object and a
    /// reference stays a string through save, reopen and undo. Only the source row rewrites it, and
    /// the two directions are not symmetrical — see change_config_source.
    ///
    /// **A file is a file however it is reached.** A shared block may itself be a "file:" string, so
    /// the read-only treatment is decided after following the reference, not from the module's own
    /// spelling: otherwise a module naming such a block would get a writable editor holding the
    /// reference as text, and one commit would replace the file reference with an object — for every
    /// module naming that block, which is the damage the read-only rule exists to prevent.
    void build_config_section(const runtime::module_node& m);

    /// Repoints the module at a shared block, or detaches it from one.
    ///
    /// Detaching **copies** the block into the module's node rather than clearing it, so what the
    /// module was being given survives the change, and leaves the block declared: it belongs to the
    /// document and other modules may name it. Attaching drops the module's own object, which the
    /// share row exists to prevent being an accident — anyone who wanted to keep it names it first.
    ///
    /// Refreshes itself instead of waiting to be told: the editor beside the row is now showing the
    /// wrong side of the change, and the host's project_changed is not guaranteed to come back here.
    /// The editor pointer is dropped first, so the focus-out that the rebuild delivers finds nothing
    /// to commit — it would otherwise write the pre-switch text over the switch that just happened.
    ///
    /// **A refused edit puts the pointer back**, because then no rebuild follows: the form key still
    /// describes what is on screen, so refresh() only syncs, and an editor left unreachable would look
    /// exactly like a working one while silently discarding everything typed into it afterwards.
    void change_config_source(const QString& choice);

    /// Adds the name field and the Share button, which belong to both bodies: a declared config is
    /// shareable exactly like a hand-written one. Skipped for a config that already names a block or a
    /// file, where there is nothing of this module's own to declare.
    void build_share_row(const style::section& s);

    /// Declares the editor's current object under a new name and points the module at it.
    ///
    /// A name already in "configs" is refused rather than overwritten: that block may be reaching
    /// other modules, and joining it is what the source row is for. A refusal from the model — a name
    /// the document will not take, or text that parses but is no object — restores the editor pointer
    /// for the reason spelled out on change_config_source.
    void share_config();

    /// Writes the editor to whichever side the source row names — the module's own node, or the
    /// shared block, which reaches **every** module naming it rather than only this one. A config that
    /// names a file is skipped: there is nothing here to write, and the file is not ours.
    ///
    /// Emptying the editor always clears **this module's** config and never the block it named, even
    /// though the text being emptied is the block's. Deleting a block is a document-wide edit made
    /// from one module's panel, and it leaves every other module naming it with a reference to
    /// nothing; detaching says the same thing about this module and says nothing about the rest. The
    /// block therefore stays declared, exactly as it does when the source row goes back to inline.
    void commit_config();

    /// Content of the file a "file:" config names, or the reason it cannot be read.
    ///
    /// Read through runtime::load_module_config, the same call the builder makes, and against
    /// app_state::saved_dir() rather than config_dir(): an unsaved project has no directory to resolve a
    /// relative path against, and saying so is more useful than reading a file from wherever studio was
    /// launched.
    [[nodiscard]] std::string config_file_preview() const;

    void build_group_section(const std::string& group_path, const std::string& name, bool renameable);

    app_state& state_;
    ui_callbacks& callbacks_;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
    std::string form_key_;
    property_grid* properties_ = nullptr;
    expose_editor* expose_inputs_ = nullptr;
    expose_editor* expose_outputs_ = nullptr;
    std::vector<QWidget*> property_rows_;
    /// The config object as a tree, built instead of the editor below when the module declared a
    /// schema **and** its config is an inline object. A reference to a shared block and a "file:"
    /// string stay on the text editor: a block belongs to the document and may be named by modules
    /// whose schemas differ, so drawing it by one module's schema would let that module rewrite
    /// fields it cannot show.
    config_tree* config_tree_ = nullptr;
    QPlainTextEdit* config_edit_ = nullptr;
    QComboBox* config_source_ = nullptr;
    QLineEdit* share_name_ = nullptr;
    std::string config_group_;
    std::string config_module_;

    /// Name of the block the editor is bound to; empty means the module's own node. Kept beside the
    /// widgets rather than read from the project on each commit, for the same reason the group is:
    /// the commit arrives on focus-out, by which time the selection may already have moved.
    std::string shared_name_;

    /// The "file:<path>" string the config is, or empty when it is not one. A third source beside the
    /// two above, and **not** a block name — a name may not contain ':' at all, so a file reference
    /// landing in shared_name_ used to show up as a block that could not be declared.
    ///
    /// The editor is read-only while this is set: what it shows is the content of somebody else's file,
    /// in a format the platform may not even parse, and writing it back is not studio's business. The
    /// preview is produced by the very call the run uses, so a file that will not resolve says here
    /// exactly what it would say then.
    std::string config_file_;

    /// Tall enough for an object worth reading without turning the inspector into a text editor.
    ///
    /// It used to be 120, which was enough while a shared block appeared here as its name — one line.
    /// Now the editor shows the block itself, so the reference form no longer makes the text shorter
    /// than an inline config; it makes it exactly as long, and 120 cut a four-key object in half.
    static constexpr int config_editor_height = 220;
};

}  // namespace atp::studio::ui

#endif
