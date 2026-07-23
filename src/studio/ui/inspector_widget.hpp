#ifndef ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP
#define ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP

#include "app_state.hpp"

#include <functional>
#include <string>

#include <QVBoxLayout>
#include <QWidget>

namespace atp::studio::ui {

// Инспектор: свойства выбранного ребёнка текущей группы + раздел документа
// (потоки, раскладка). Перестраивается целиком на смену выбора/документа —
// формы маленькие, состояние набора живёт только между перестройками.
class inspector_widget final : public QWidget {
   public:
    inspector_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    void refresh();

   private:
    void add_header(const QString& text);

    QWidget* add_row();

    void guard(const char* context, const std::function<void()>& operation);

    void build_module_section(const runtime::module_node& m);

    void build_group_section(const std::string& name);

    void build_expose_editor(const std::string& child, bool inputs);

    void build_document_section();

    app_state& state_;
    ui_callbacks& callbacks_;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP
