find_package(GTest CONFIG REQUIRED)

# WindowsSessionStore (src/platform, not part of MegaExplorerCore -- same
# reason src/mega isn't) is fully offline-testable, unlike MegaSdkClient
# which needs a live account, so it gets its own real adapter test here
# instead of a mock-only substitute. That pulls src/app/Logging.cpp into this
# otherwise Qt-free test target, since WindowsSessionStore.cpp logs through
# its lcSession category and that category's actual Q_LOGGING_CATEGORY
# definition lives there.
add_executable(MegaExplorerTests
    FolderNavigationServiceTest.cpp
    AuthServiceTest.cpp
    AccountServiceTest.cpp
    SearchServiceTest.cpp
    DownloadServiceTest.cpp
    UploadServiceTest.cpp
    UploadControllerTest.cpp
    ThumbnailServiceTest.cpp
    FolderTreeServiceTest.cpp
    FileOperationServiceTest.cpp
    FolderNavigationControllerTest.cpp
    WindowsSessionStoreTest.cpp
    FileListModelTest.cpp
    MenuActionResolverTest.cpp
    TabsControllerTest.cpp
    FolderTreeModelTest.cpp
    QuickAccessServiceTest.cpp
    QuickAccessModelTest.cpp
    LicenseModelTest.cpp
    ClipboardControllerTest.cpp
    AccountControllerTest.cpp
    NotificationControllerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/WindowsSessionStore.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Logging.cpp
    MockMegaClient.h
    MockSessionStore.h
    MockPinnedFolderStore.h
    TestApp.h
)

# MegaExplorerQml is the QML module's backing library, so linking it is what
# gives the tests the src/qml controllers and models -- before R4-4 this file
# listed those 22 source files by hand and drifted from the root CMakeLists.txt.
# It also carries Qt6::Quick (hence Qt6::Gui) transitively, so tests may use
# anything src/qml already depends on without this file growing a Qt component
# list of its own. No extra DLLs: Qt6Quick/Qt6Gui sit in the same Qt bin
# directory that Qt6Core/Qt6Qml already had to be on PATH from.
target_link_libraries(MegaExplorerTests PRIVATE
    MegaExplorerCore
    MegaExplorerQml
    crypt32
    GTest::gtest_main
    GTest::gmock
    MegaExplorerWarnings
)

# LicenseModelTest reads the working copy's licenses/ directly rather than the
# qrc copy: this target embeds no resources, and the point of the test is to
# check the generated files that qrc is built *from*.
target_compile_definitions(MegaExplorerTests PRIVATE
    MEGAEXPLORER_LICENSE_DIR="${CMAKE_SOURCE_DIR}/licenses")

include(GoogleTest)
gtest_discover_tests(MegaExplorerTests)
