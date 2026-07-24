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

qt_add_executable(appMegaExplorer
    main.cpp
    src/core/IMegaClient.h
    src/core/FileEntry.h
    src/core/Result.h
    src/mega/MegaSdkClient.h
    src/mega/MegaSdkClient.cpp
)

qt_add_qml_module(appMegaExplorer
    URI MegaExplorer
    QML_FILES
        Main.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appMegaExplorer PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appMegaExplorer
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    # Temporarily off (Phase 1 step 3): GUI-subsystem processes never attach
    # to the launching console on Windows, even when run from one, so
    # qDebug()/qWarning() output from the composition-root smoke test was
    # going nowhere. Revert to TRUE once QML UI wiring returns in step 4.
    WIN32_EXECUTABLE FALSE
)

target_include_directories(appMegaExplorer
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(appMegaExplorer
    PRIVATE Qt6::Quick
    PRIVATE MEGA::SDKlib
)

install(TARGETS appMegaExplorer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
