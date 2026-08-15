// Entry point for the qml/ tests. quick_test_main() supplies the
// QGuiApplication and the QML engine, so tests/TestMain.cpp (a QCoreApplication
// plus RUN_ALL_TESTS) cannot be shared with this target.
//
// The tests here reach nothing that main.cpp registers as a context property,
// which is why this is QUICK_TEST_MAIN and not QUICK_TEST_MAIN_WITH_SETUP: no
// engine hook is needed. See tst_ActionCatalog.qml for the one place that
// costs coverage.
#include <QtCore/qtenvironmentvariables.h>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

// Forces the type registration in, exactly as main.cpp does. Linking
// MegaExplorerQmlplugin is the other required half; neither works alone.
Q_IMPORT_QML_PLUGIN(MegaExplorerPlugin)

// QUICK_TEST_MAIN expanded by hand, so the platform can be chosen before
// quick_test_main() builds the QGuiApplication: Qt Quick Test shows a real
// QQuickView per .qml file, and on the desktop platform that steals focus a
// dozen times per run. Setting it in ctest's ENVIRONMENT instead only covers
// `ctest` -- a by-hand `MegaExplorerQmlTests -o -,tap`, which is what debugging
// a failing case actually uses, still flashed the windows. An explicit
// QT_QPA_PLATFORM in the environment still wins, for the rare case of wanting
// to watch a test run.
int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QTEST_SET_MAIN_SOURCE_PATH
    return quick_test_main(argc, argv, "megaexplorer_qml", QUICK_TEST_SOURCE_DIR);
}
