// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_UI_TESTS_QT_APP_HPP
#define ATP_UI_TESTS_QT_APP_HPP

#include <QApplication>

namespace atp_ui_tests {

/// The one QApplication the widget tests share. Offscreen is chosen here rather than left to the
/// environment, so the suite runs under a plain ctest with no display.
inline QApplication& ensure_app() {
    static int argc = 1;
    static char arg0[] = "atp_ui_tests";
    static char* argv[] = {arg0, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication instance(argc, argv);
    return instance;
}

}  // namespace atp_ui_tests

#endif
