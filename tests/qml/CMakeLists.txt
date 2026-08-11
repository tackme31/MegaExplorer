# Qt Quick Test instantiates the .qml TestCases inside this process, so the
# MegaExplorer module has to be importable from here. That takes the same
# two-part incantation appMegaExplorer uses (see the root CMakeLists.txt for the
# full story): linking MegaExplorerQml alone lets the linker drop the generated
# type-registration object out of the static library, and every C++ type then
# fails to register with no error until a QML file names one.
#
# add_executable, not qt_add_executable: the latter marks the binary
# WIN32_EXECUTABLE on Windows, and a test runner with no console writes its
# results nowhere.
#
# src/app/Logging.cpp is compiled in for the same reason tests/CMakeLists.txt
# does it: the src/qml classes inside MegaExplorerQml call lcNavigation() and
# friends, and those categories are defined there rather than in a library.
add_executable(MegaExplorerQmlTests
    QmlTestMain.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Logging.cpp
    tst_ToastStack.qml
    tst_ActionCatalog.qml
    tst_DragProxy.qml
    tst_EmptyListingNotice.qml
    tst_NodeDropArea.qml
    tst_FileViewDropArea.qml
    tst_FileViewInput.qml
    tst_FileViewInputPointer.qml
    tst_MainDialogs.qml
    tst_PreviewPane.qml
    tst_ViewLabels.qml
)

# The .qml files are listed above only so they show up in the IDE; they are read
# from the source tree at run time, not compiled or embedded.
set_source_files_properties(tst_ToastStack.qml tst_ActionCatalog.qml tst_DragProxy.qml
                            tst_EmptyListingNotice.qml tst_NodeDropArea.qml
                            tst_FileViewDropArea.qml tst_FileViewInput.qml
                            tst_FileViewInputPointer.qml tst_MainDialogs.qml tst_PreviewPane.qml
                            tst_ViewLabels.qml
                            PROPERTIES HEADER_FILE_ONLY TRUE)

target_link_libraries(MegaExplorerQmlTests PRIVATE
    Qt6::QuickTest
    MegaExplorerQml
    MegaExplorerQmlplugin
    MegaExplorerWarnings
)

# Without this quick_test_main() looks in the current working directory. Each
# add_test below also passes -input explicitly, so this only decides what a bare
# run of the binary picks up -- same trick as MEGAEXPLORER_LICENSE_DIR next door.
target_compile_definitions(MegaExplorerQmlTests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# One ctest entry per file rather than one for the whole binary: ctest then names
# the component that broke, which is as close as Qt Quick Test gets to the
# per-case isolation gtest_discover_tests gives the target next door.
#
# `-o -,tap` is not cosmetic. QtTest's default plain logger, on Windows with no
# console attached to the process -- which is every run under ctest, and every
# run from Git Bash -- diverts its output to OutputDebugStringA and writes
# nothing to stdout, so a failing case reaches ctest as a bare exit code and
# --output-on-failure prints an empty block. The TAP logger has no such branch,
# and reports the failing .qml file and line.
foreach(component ToastStack ActionCatalog DragProxy EmptyListingNotice NodeDropArea
                  FileViewDropArea FileViewInput FileViewInputPointer MainDialogs PreviewPane
                  ViewLabels)
    add_test(NAME QmlTest.${component}
             COMMAND MegaExplorerQmlTests
                     -input ${CMAKE_CURRENT_SOURCE_DIR}/tst_${component}.qml
                     -o -,tap)
    # Qt Quick Test shows a real QQuickView per file, so a ctest run flashes ten
    # windows across the desktop. The offscreen platform renders the same scene
    # without one; synthesized input still reaches it.
    set_tests_properties(QmlTest.${component} PROPERTIES
                         ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
