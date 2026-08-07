// Entry point for the qml/ tests. quick_test_main() supplies the
// QGuiApplication and the QML engine, so tests/TestMain.cpp (a QCoreApplication
// plus RUN_ALL_TESTS) cannot be shared with this target.
//
// The tests here reach nothing that main.cpp registers as a context property,
// which is why this is QUICK_TEST_MAIN and not QUICK_TEST_MAIN_WITH_SETUP: no
// engine hook is needed. See tst_ActionCatalog.qml for the one place that
// costs coverage.
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

// Forces the type registration in, exactly as main.cpp does. Linking
// MegaExplorerQmlplugin is the other required half; neither works alone.
Q_IMPORT_QML_PLUGIN(MegaExplorerPlugin)

QUICK_TEST_MAIN(megaexplorer_qml)
