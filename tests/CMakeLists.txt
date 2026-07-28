find_package(GTest CONFIG REQUIRED)

# SqliteNodeCache (src/platform, not part of MegaExplorerCore -- same reason
# src/mega isn't) is fully offline-testable, unlike MegaSdkClient which needs
# a live account, so it gets its own real adapter test here instead of a
# mock-only substitute. That pulls two things into this otherwise Qt-free
# test target: the sqlite3 link (unofficial-sqlite3 is find_package'd in the
# root CMakeLists.txt, before this add_subdirectory) and src/app/Logging.cpp,
# since SqliteNodeCache.cpp logs through its lcCache category and that
# category's actual Q_LOGGING_CATEGORY definition lives there.
add_executable(MegaExplorerTests
    FolderNavigationServiceTest.cpp
    AuthServiceTest.cpp
    SearchServiceTest.cpp
    DownloadServiceTest.cpp
    ThumbnailServiceTest.cpp
    SqliteNodeCacheTest.cpp
    WindowsSessionStoreTest.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/SqliteNodeCache.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/WindowsSessionStore.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Logging.cpp
    MockMegaClient.h
    MockNodeCache.h
    MockSessionStore.h
)

target_link_libraries(MegaExplorerTests PRIVATE
    MegaExplorerCore
    unofficial::sqlite3::sqlite3
    crypt32
    Qt6::Core
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(MegaExplorerTests)
