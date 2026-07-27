cmake_minimum_required(VERSION 3.20)

project(MegaExplorer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.10)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

# MEGA C++ SDK (vendored as a submodule at third_party/sdk); requires vcpkg's
# toolchain file to be passed via CMAKE_TOOLCHAIN_FILE at configure time.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party/sdk)

# SDKlib's own FFmpeg lookup (sdklib_libraries.cmake) resolves through Qt's
# FindFFmpeg.cmake module (found ahead of vcpkg's FFmpeg config on the module
# path) and only requests AVCODEC/AVFORMAT/AVUTIL, so FFmpeg::swscale is never
# created even though vcpkg builds swscale.lib. SDKlib's FreeImage backend
# calls sws_* directly, so link it in ourselves.
find_package(FFmpeg COMPONENTS SWSCALE)
if(TARGET FFmpeg::swscale)
    target_link_libraries(SDKlib PRIVATE FFmpeg::swscale)
endif()

# SDK-free domain logic, shared between appMegaExplorer and MegaExplorerTests.
add_library(MegaExplorerCore STATIC
    src/core/IMegaClient.h
    src/core/FileEntry.h
    src/core/Result.h
    src/core/FileKind.h
    src/core/FileKind.cpp
    src/core/FileListingService.h
    src/core/FileListingService.cpp
    src/core/FolderNavigationService.h
    src/core/FolderNavigationService.cpp
    src/core/SearchService.h
    src/core/SearchService.cpp
    src/core/DownloadService.h
    src/core/DownloadService.cpp
    src/core/ThumbnailService.h
    src/core/ThumbnailService.cpp
)
target_include_directories(MegaExplorerCore PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)

enable_testing()
add_subdirectory(tests)

qt_add_executable(appMegaExplorer
    main.cpp
    src/app/Logging.h
    src/app/Logging.cpp
    src/mega/MegaSdkClient.h
    src/mega/MegaSdkClient.cpp
    src/mega/MegaSdkLogger.h
    src/mega/MegaSdkLogger.cpp
    src/qml/FileListModel.h
    src/qml/FileListModel.cpp
    src/qml/FolderNavigationController.h
    src/qml/FolderNavigationController.cpp
    src/qml/DownloadController.h
    src/qml/DownloadController.cpp
    src/qml/ThumbnailController.h
    src/qml/ThumbnailController.cpp
    src/qml/NotificationController.h
    src/qml/NotificationController.cpp
)

qt_add_qml_module(appMegaExplorer
    URI MegaExplorer
    QML_FILES
        qml/Main.qml
        qml/components/DownloadSnackbar.qml
        qml/components/ErrorToast.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appMegaExplorer PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appMegaExplorer
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    # Back on as of Phase 1 step 5: QML UI wiring returned (QQmlApplicationEngine
    # in main.cpp), so the app is GUI-subsystem again. While it was step 3's
    # console-only composition-root smoke test, this was FALSE so qDebug()/
    # qWarning() output would reach the launching console (GUI-subsystem
    # processes never attach to it on Windows).
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appMegaExplorer
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src
)

# MSVC defaults to /W1; raise our own code to /W4 so warning-sweep tooling (see
# CLAUDE.md's Build section) has something to catch. Scoped to this target only
# so SDKlib/third_party don't start emitting noise under the stricter level.
target_compile_options(appMegaExplorer PRIVATE /W4)

target_link_libraries(appMegaExplorer
    PRIVATE Qt6::Quick
    PRIVATE MEGA::SDKlib
    PRIVATE MegaExplorerCore
)

install(TARGETS appMegaExplorer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
