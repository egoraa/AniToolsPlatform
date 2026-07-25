#ifndef ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP
#define ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP

#include "app_state.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace atp::studio::ui {

// Редактор значения проперти. Первым делом смотрим на набор: непустой —
// выпадающий список независимо от kind (произвольное значение вводить нельзя,
// вариант выбирается; прецедент комбобокса по фиксированному списку — режимы
// потоков). Без набора решает kind: boolean — чекбокс, остальное — строка
// ввода. Тип определён здесь, а не в .cpp: инспектор владеет редакторами
// вектором unique_ptr, и его деструктору нужен полный тип.
struct property_editor {
    QWidget* widget = nullptr;
    QCheckBox* check = nullptr;  // заполнен для boolean без набора
    QComboBox* combo = nullptr;  // заполнен для любой проперти с набором
    QLineEdit* line = nullptr;   // заполнен для number/text без набора
    io::property_kind kind = io::property_kind::text;

    [[nodiscard]] std::string text() const {
        if (combo != nullptr) {
            return combo->currentText().toStdString();  // текст пункта каноничен: его дал to_string
        }
        if (check != nullptr) {
            return check->isChecked() ? "true" : "false";
        }
        return line->text().toStdString();
    }
};

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

    // Строки пропертей модуля; возвращает виджеты блока (заголовок и строки) —
    // их разрешено править и на ходу, когда остальная форма заперта исполнением.
    std::vector<QWidget*> build_property_rows(const runtime::module_node& m);

    void build_group_section(const std::string& name);

    void build_expose_editor(const std::string& child, bool inputs);

    void build_document_section();

    app_state& state_;
    ui_callbacks& callbacks_;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
    // Редакторы пропертей текущей формы: лямбды connect держат на них
    // указатели, поэтому вектор очищается только при перестройке формы —
    // вместе с самими виджетами и их connect'ами.
    std::vector<std::unique_ptr<property_editor>> property_editors_;
    // Виджеты блока пропертей последней построенной формы — единственные,
    // что остаются доступными при работающем пайплайне.
    std::vector<QWidget*> property_rows_;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP
