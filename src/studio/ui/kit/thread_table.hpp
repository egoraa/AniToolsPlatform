// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_THREAD_TABLE_HPP
#define ATP_STUDIO_UI_THREAD_TABLE_HPP

#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <string>
#include <vector>

#include <QTableWidget>
#include <QToolButton>
#include <QWidget>

namespace atp::studio::ui {

/// Runner threads of the project together with their live statistics. Declaring a thread and
/// watching what it does are the same question, so they share one table: the period is edited right
/// next to the load it produces. Editing is project editing, hence read-only while the pipeline
/// runs — the numbers keep updating.
class thread_table final : public QWidget {
   public:
    thread_table(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Called from the runtime panel's poll: rebuilds the rows when the thread list changed and
    /// refreshes the counters otherwise.
    void refresh();

   private:
    void rebuild();

    void update_stats();

    void add_thread();

    void remove_selected();

    [[nodiscard]] std::string signature() const;

    app_state& state_;
    ui_callbacks& callbacks_;
    QTableWidget* table_ = nullptr;
    QToolButton* add_ = nullptr;
    QToolButton* remove_ = nullptr;
    std::string signature_;
    bool filling_ = false;
    std::vector<pipeline_runner::thread_stats> previous_;
};

}  // namespace atp::studio::ui

#endif
