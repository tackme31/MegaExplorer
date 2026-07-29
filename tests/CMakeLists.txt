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
    SearchServiceTest.cpp
    DownloadServiceTest.cpp
    ThumbnailServiceTest.cpp
    WindowsSessionStoreTest.cpp
    FileListModelTest.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/WindowsSessionStore.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Logging.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/FileListModel.h
    ${CMAKE_SOURCE_DIR}/src/qml/FileListModel.cpp
    MockMegaClient.h
    MockSessionStore.h
)

target_link_libraries(MegaExplorerTests PRIVATE
    MegaExplorerCore
    crypt32
    Qt6::Core
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(MegaExplorerTests)
