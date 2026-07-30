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
    FolderTreeServiceTest.cpp
    WindowsSessionStoreTest.cpp
    FileListModelTest.cpp
    FileActionResolverTest.cpp
    TabsControllerTest.cpp
    FolderTreeModelTest.cpp
    QuickAccessServiceTest.cpp
    QuickAccessModelTest.cpp
    ${CMAKE_SOURCE_DIR}/src/platform/WindowsSessionStore.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Logging.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/FileListModel.h
    ${CMAKE_SOURCE_DIR}/src/qml/FileListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/FolderNavigationController.h
    ${CMAKE_SOURCE_DIR}/src/qml/FolderNavigationController.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/ThumbnailController.h
    ${CMAKE_SOURCE_DIR}/src/qml/ThumbnailController.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/NotificationController.h
    ${CMAKE_SOURCE_DIR}/src/qml/NotificationController.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/TabsController.h
    ${CMAKE_SOURCE_DIR}/src/qml/TabsController.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/FolderTreeModel.h
    ${CMAKE_SOURCE_DIR}/src/qml/FolderTreeModel.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/QuickAccessModel.h
    ${CMAKE_SOURCE_DIR}/src/qml/QuickAccessModel.cpp
    MockMegaClient.h
    MockSessionStore.h
    MockPinnedFolderStore.h
    TestApp.h
)

# Qt6::Qml (not just Core) is needed here solely for
# TabsController.cpp's QQmlEngine::setObjectOwnership call -- no separate
# find_package() needed, Qt6::Qml is already resolved as a transitive
# dependency of the root CMakeLists.txt's find_package(Qt6 COMPONENTS Quick).
target_link_libraries(MegaExplorerTests PRIVATE
    MegaExplorerCore
    crypt32
    Qt6::Core
    Qt6::Qml
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(MegaExplorerTests)
