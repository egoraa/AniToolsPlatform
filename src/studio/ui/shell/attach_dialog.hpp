#ifndef ATP_STUDIO_UI_ATTACH_DIALOG_HPP
#define ATP_STUDIO_UI_ATTACH_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>

namespace atp::studio::ui {

/// Asks where to attach. It remembers nothing of its own — the last endpoint lives in the settings,
/// and the caller both seeds the fields from there and writes them back, so the dialog stays a
/// question and not a piece of state.
class attach_dialog final : public QDialog {
   public:
    /// @param host address to offer first
    /// @param port port to offer first; 0 means "no port remembered yet"
    /// @param parent owner widget
    attach_dialog(const QString& host, int port, QWidget* parent = nullptr);

    /// Address the person entered.
    [[nodiscard]] QString host() const;

    /// Port the person entered.
    [[nodiscard]] int port() const;

   private:
    QLineEdit* host_ = nullptr;
    QSpinBox* port_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
