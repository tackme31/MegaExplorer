cmake_minimum_required(VERSION 3.20)

project(MegaExplorer VERSION 0.1.1 LANGUAGES CXX)

# The distribution is a zip you unpack and double-click, not a Unix prefix: the
# exe, its DLLs and the notices all sit at the root, with only plugins/ and qml/
# below. windeployqt reads CMAKE_INSTALL_BINDIR through QT_DEPLOY_BIN_DIR, so
# pinning it here is what flattens Qt's half of the layout too. Must precede
# GNUInstallDirs, which only fills in defaults it does not already find, and
# must be a normal variable rather than a cache one: a cache entry from an
# earlier configure would win over a non-FORCE set() and quietly restore "bin".
if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
endif()

# CMAKE_INSTALL_BINDIR below would otherwise rely on Qt's package including this
# transitively.
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Gui is for QWindowKit, which links Qt's Core/Gui/Quick *private* modules.
# QuickTest backs tests/qml, which runs the .qml TestCases in-process.
# Multimedia backs qml/components/VideoViewer.qml. Nothing in C++ calls it, but
# linking it is what puts Qt6Multimedia.dll and the media backend plugin in front
# of windeployqt -- the QML import scan alone finds the module, not its plugin.
find_package(Qt6 REQUIRED COMPONENTS Gui Multimedia Network Quick QuickTest)

qt_standard_project_setup(REQUIRES 6.10)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

# QWindowKit (vendored at third_party/qwindowkit) -- frameless window with the
# native Win32 behaviour kept (Snap Layout flyout, DWM animations, shadow,
# rounded corners). Static so no extra DLL has to join Qt's/vcpkg's on PATH;
# widgets module off since this app is QGuiApplication-only.
set(QWINDOWKIT_BUILD_QUICK   ON  CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_WIDGETS OFF CACHE BOOL "" FORCE)
set(QWINDOWKIT_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
set(QWINDOWKIT_INSTALL       OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party/qwindowkit)

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

# MSVC defaults to /W1; raise our own code to /W4 so warning-sweep tooling (see
# CLAUDE.md's Build section) has something to catch. Carried by an interface
# target rather than repeated per target, and linked PRIVATE so it never
# propagates: SDKlib/third_party/QWindowKit have no business seeing the stricter
# level, which is also why adding /W4 to CMAKE_CXX_FLAGS is not an option.
#
# /external:W0 is what makes /W4 usable rather than just loud. CMake already
# passes Qt's and GTest's include dirs as /external:I (imported targets are
# SYSTEM by default), but MSVC ignores that designation until an /external:W
# level is set -- so without this, Qt headers instantiated from generated
# moc/qmlcachegen/qmltyperegistration code report C4702 and friends as if they
# were ours. Our own headers are unaffected: they arrive via plain /I.
#
# /wd4702 (unreachable code) is the one warning /external:W0 cannot reach: it is
# emitted by the back end, which has no notion of external headers, so all 51 of
# its hits -- every one inside qjsengine.h/qvariant.h/qjsprimitivevalue.h,
# instantiated from qmlcachegen's AOT output, none in our own code -- survive
# the exclusion above. Leaving it on would mean a full rebuild always ends in 51
# warnings and the sweep this target exists for would be worthless. Nothing in
# src/ triggers C4702 today; if that changes, prefer fixing the code to keeping
# this line.
add_library(MegaExplorerWarnings INTERFACE)
target_compile_options(MegaExplorerWarnings INTERFACE /W4 /external:W0 /wd4702)

# SDK-free domain logic, shared between MegaExplorer and MegaExplorerTests.
add_library(MegaExplorerCore STATIC
    src/core/IMegaClient.h
    src/core/ILocalFileSystem.h
    src/core/ISessionStore.h
    src/core/IPinnedFolderStore.h
    src/core/FileEntry.h
    src/core/MegaErrorCodes.h
    src/core/NodeInfo.h
    src/core/NodeRef.h
    src/core/PinnedFolder.h
    src/core/UploadOutcome.h
    src/core/Result.h
    src/core/SearchFilter.h
    src/core/SortOrder.h
    src/core/PathSegment.h
    src/core/ViewKind.h
    src/core/AccountInfo.h
    src/core/AccountPlan.h
    src/core/FolderInfo.h
    src/core/UserAttribute.h
    src/core/MenuAction.h
    src/core/MenuActionResolver.h
    src/core/MenuActionResolver.cpp
    src/core/FolderNavigationService.h
    src/core/FolderNavigationService.cpp
    src/core/FileOperationService.h
    src/core/FileOperationService.cpp
    src/core/AuthService.h
    src/core/AuthService.cpp
    src/core/SearchService.h
    src/core/SearchService.cpp
    src/core/DownloadService.h
    src/core/DownloadService.cpp
    src/core/UploadService.h
    src/core/UploadService.cpp
    src/core/UploadScanService.h
    src/core/UploadScanService.cpp
    src/core/ThumbnailService.h
    src/core/ThumbnailService.cpp
    src/core/PreviewKind.h
    src/core/PreviewKind.cpp
    src/core/PreviewService.h
    src/core/PreviewService.cpp
    src/core/ZipListing.h
    src/core/ZipListing.cpp
    src/core/FolderTreeService.h
    src/core/FolderTreeService.cpp
    src/core/LocalLinkService.h
    src/core/LocalLinkService.cpp
    src/core/QuickAccessService.h
    src/core/QuickAccessService.cpp
    src/core/AccountService.h
    src/core/AccountService.cpp
    src/core/NodeDetailsService.h
    src/core/NodeDetailsService.cpp
)
target_include_directories(MegaExplorerCore PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)
# Every other target gets C++17 from Qt's interface requirement; this one links
# no Qt at all and would otherwise fall back to MSVC's C++14 default, where
# std::optional doesn't exist.
target_compile_features(MegaExplorerCore PUBLIC cxx_std_17)
target_link_libraries(MegaExplorerCore PRIVATE MegaExplorerWarnings)

# The QML module's backing target. A library rather than MegaExplorer itself
# so that MegaExplorerTests can link the same QML-facing C++ types, and
# MegaExplorerQmlTests can import the MegaExplorer URI at all (R4-5), instead of
# recompiling a hand-copied subset of src/qml -- the same reason
# MegaExplorerCore is a library. Everything below is plain QObject with no QML
# registration; the QML_ELEMENT types are in qt_add_qml_module's SOURCES.
add_library(MegaExplorerQml STATIC
    src/qml/GuiThread.h
    src/qml/BusyState.h
    src/qml/BusyState.cpp
    src/qml/BulkOperationRunner.h
    src/qml/BulkOperationRunner.cpp
    src/qml/FileListModel.h
    src/qml/FileListModel.cpp
    src/qml/FolderNavigationController.h
    src/qml/FolderNavigationController.cpp
    src/qml/FileMutationController.h
    src/qml/FileMutationController.cpp
    src/qml/DownloadController.h
    src/qml/DownloadController.cpp
    src/qml/UploadController.h
    src/qml/UploadController.cpp
    src/qml/ThumbnailController.h
    src/qml/ThumbnailController.cpp
    src/qml/PreviewImageStore.h
    src/qml/PreviewImageProvider.h
    src/qml/PreviewImageProvider.cpp
    src/qml/TextPreviewDecoder.h
    src/qml/TextPreviewDecoder.cpp
    src/qml/ClipboardController.h
    src/qml/ClipboardController.cpp
    src/qml/TabsController.h
    src/qml/TabsController.cpp
    src/qml/FolderTreeModel.h
    src/qml/FolderTreeModel.cpp
    src/qml/QuickAccessModel.h
    src/qml/QuickAccessModel.cpp
    src/qml/LocalFolderController.h
    src/qml/LocalFolderController.cpp
    src/qml/PropertiesController.h
    src/qml/PropertiesController.cpp
    src/qml/TransferListModel.h
    src/qml/TransferListModel.cpp
)

qt_add_executable(MegaExplorer
    main.cpp
    resources/appicon.rc
    src/app/Logging.h
    src/app/Logging.cpp
    src/mega/MegaSdkClient.h
    src/mega/MegaSdkClient.cpp
    src/mega/MegaSdkListeners.h
    src/mega/MegaSdkLogger.h
    src/mega/MegaSdkLogger.cpp
    src/platform/WindowsSessionStore.h
    src/platform/WindowsSessionStore.cpp
    src/platform/QSettingsPinnedFolderStore.h
    src/platform/QSettingsPinnedFolderStore.cpp
    src/platform/QtLocalFileSystem.h
    src/platform/QtLocalFileSystem.cpp
)

# Generated by scripts/gen_third_party_notices.py alongside licenses/manifest.json
# itself, so the set of files embedded below cannot drift from the set the
# manifest references. Re-run that script after bumping any dependency.
include(${CMAKE_CURRENT_LIST_DIR}/licenses/licenses.cmake)

# pragma Singleton alone does not put a `singleton` line in the generated
# qmldir -- this property does, and it has to be set before the module is
# created.
set_source_files_properties(qml/Theme.qml qml/ActionCatalog.qml qml/ViewLabels.qml
                            qml/FileTypeIcons.qml qml/StandardButtonLabels.qml
                            PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(MegaExplorerQml
    URI MegaExplorer
    # Only executable backing targets get the target path appended to their
    # default output directory; for a library it would be CMAKE_CURRENT_BINARY_DIR
    # itself, i.e. qmldir would land in the build root and no import path could
    # resolve MegaExplorer by looking for <path>/MegaExplorer/qmldir. Pinning it
    # keeps the pre-R4-4 on-disk layout. (The *resource* path is unaffected --
    # still :/qt/qml/MegaExplorer/, which is what main.cpp's loadFromModule uses.)
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MegaExplorer
    QML_FILES
        qml/Main.qml
        qml/ActionCatalog.qml
        qml/FileTypeIcons.qml
        qml/StandardButtonLabels.qml
        qml/Theme.qml
        qml/ViewLabels.qml
        qml/components/AboutDialog.qml
        qml/components/AccountMenuHeader.qml
        qml/components/ActionMenu.qml
        qml/components/AddressToolBar.qml
        qml/components/BandSelector.qml
        qml/components/Breadcrumb.qml
        qml/components/CaptionBar.qml
        qml/components/ConfirmEmptyRubbishDialog.qml
        qml/components/ConfirmUploadDialog.qml
        qml/components/ConfirmPermanentDeleteDialog.qml
        qml/components/ConfirmRemoveLinkDialog.qml
        qml/components/ConfirmRubbishDialog.qml
        qml/components/ConflictNameListDialog.qml
        qml/components/CopyConflictDialog.qml
        qml/components/DragAutoScroller.qml
        qml/components/DragProxy.qml
        qml/components/EmptyListingNotice.qml
        qml/components/FileContextMenu.qml
        qml/components/FileIcon.qml
        qml/components/FileViewDropArea.qml
        qml/components/FileViewInput.qml
        qml/components/FolderBackgroundMenu.qml
        qml/components/FolderPinMenu.qml
        qml/components/FolderTreePanel.qml
        qml/components/IconMenuItem.qml
        qml/components/ImageViewer.qml
        qml/components/VideoViewer.qml
        qml/components/InlineRenameField.qml
        qml/components/LicenseDialog.qml
        qml/components/ListingLoadingNotice.qml
        qml/components/MissingPinDialog.qml
        qml/components/NameConflictDialog.qml
        qml/components/NewFolderDialog.qml
        qml/components/NodeDropArea.qml
        qml/components/PreviewPane.qml
        qml/components/PropertiesDialog.qml
        qml/components/QuickAccessSection.qml
        qml/components/SearchFilterPopup.qml
        qml/components/SettingsDialog.qml
        qml/components/SidePanel.qml
        qml/components/SignOutDialog.qml
        qml/components/SpecialViewsSection.qml
        qml/components/StatusBar.qml
        qml/components/TabStrip.qml
        qml/components/ToastStack.qml
        qml/components/TransferFlyout.qml
        qml/components/ViewScrollBar.qml
        qml/views/FileGridView.qml
        qml/views/FileTableView.qml
        qml/views/LoginView.qml
        qml/views/TabContentPane.qml
    SOURCES
        src/qml/AccountController.h
        src/qml/AccountController.cpp
        src/qml/AuthController.h
        src/qml/AuthController.cpp
        src/qml/LicenseModel.h
        src/qml/LicenseModel.cpp
        src/qml/KeyboardState.h
        src/qml/MenuActions.h
        src/qml/MenuActions.cpp
        src/qml/NotificationController.h
        src/qml/NotificationController.cpp
        src/qml/PreviewController.h
        src/qml/PreviewController.cpp
        src/qml/SearchFilterEnums.h
        src/qml/TransferEnums.h
        src/qml/ViewerController.h
        src/qml/ViewerController.cpp
        src/qml/ViewKindEnum.h
        src/qml/WindowAgentForeign.h
    RESOURCES
        ${MEGAEXPLORER_LICENSE_RESOURCES}
        # PNGs rather than the .ico these are also packed into: PNG decoding is
        # built into QtGui, while .ico needs the qico image-format plugin to be
        # deployed alongside the app. setWindowIcon() in main.cpp reads them
        # from here.
        resources/appicon-16.png
        resources/appicon-24.png
        resources/appicon-32.png
        resources/appicon-48.png
        resources/appicon-64.png
        resources/appicon-256.png
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(MegaExplorer PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.MegaExplorer
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

# qt_add_qml_module's generated qmltyperegistrations.cpp #includes QML_ELEMENT
# headers by bare filename with angle brackets (e.g. <AuthController.h>, not
# <qml/AuthController.h>), so src/qml itself has to be on the include path too,
# not just src. Private: only this target's own generated file needs it, and
# src/ already reaches consumers through MegaExplorerCore's PUBLIC include.
target_include_directories(MegaExplorerQml PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/qml)

target_link_libraries(MegaExplorerQml
    # Public: src/qml headers are QObject/QAbstractListModel subclasses exposing
    # MegaExplorerCore types, so consumers need both.
    PUBLIC Qt6::Quick
    PUBLIC MegaExplorerCore
    # Private: only VideoViewer.qml's `import QtMultimedia` needs it (see the
    # find_package comment above), and a static library still forwards this on.
    PRIVATE Qt6::Multimedia
    # Private: only WindowAgentForeign.h's QML_FOREIGN(QWK::QuickWindowAgent)
    # needs it, and static libraries still forward private deps to their linkers.
    PRIVATE QWindowKit::Quick
    PRIVATE MegaExplorerWarnings
)

target_include_directories(MegaExplorer PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

# Target-scoped for the same reason as /W4: SDKlib and third_party have no
# business seeing our definitions. main.cpp forwards this to
# QCoreApplication::setApplicationVersion, which is where QML reads it from.
target_compile_definitions(MegaExplorer PRIVATE
    MEGAEXPLORER_VERSION="${PROJECT_VERSION}")

# MegaExplorerQmlplugin is the static QML plugin qt_add_qml_module generates
# alongside the (static) backing library. Linking it is not optional despite the
# `optional plugin` line in qmldir: the module's type registration lives in a
# generated translation unit that nothing in main.cpp names, so the linker
# discards that object from MegaExplorerQml.lib and every C++ type silently
# fails to register -- the app then dies at startup on the first one Main.qml
# uses ("WindowAgent is not a type"). Verified with dumpbin: without this,
# qml_register_types_MegaExplorer is present in the .lib and absent from the
# .exe. main.cpp's Q_IMPORT_QML_PLUGIN is the other half; neither works alone.
# tests/qml repeats both halves for the same reason.
target_link_libraries(MegaExplorer
    PRIVATE MegaExplorerQml
    PRIVATE MegaExplorerQmlplugin
    PRIVATE MEGA::SDKlib
    PRIVATE MegaExplorerCore
    # crypt32 for src/platform's WindowsSessionStore (DPAPI), which stays here.
    PRIVATE crypt32
    PRIVATE MegaExplorerWarnings
)

# Console CLI over IMegaClient, for MEGA-side fixtures and for the /evolve loop's
# "is the app signed in to the test account?" check (CLAUDE.md, docs/MEGATOOL.md).
# Recompiles src/mega and src/platform rather than linking them: they belong to
# MegaExplorer, and pulling them into a shared library just for this would
# make the app's own layering harder to read for one extra consumer.
add_executable(megatool
    tools/megatool.cpp
    src/app/Logging.h
    src/app/Logging.cpp
    src/mega/MegaSdkClient.h
    src/mega/MegaSdkClient.cpp
    src/mega/MegaSdkListeners.h
    src/mega/MegaSdkLogger.h
    src/mega/MegaSdkLogger.cpp
    src/platform/WindowsSessionStore.h
    src/platform/WindowsSessionStore.cpp
)
target_link_libraries(megatool
    PRIVATE Qt6::Core
    PRIVATE Qt6::Network
    PRIVATE MEGA::SDKlib
    PRIVATE MegaExplorerCore
    PRIVATE crypt32
    PRIVATE MegaExplorerWarnings
)

# After MegaExplorerQml, which tests/CMakeLists.txt links: CMake would resolve
# the name either way, but the dependency is easier to follow in source order.
enable_testing()
add_subdirectory(tests)

install(TARGETS MegaExplorer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# The licenses require their text to accompany the distribution, so the qrc copy
# behind LicenseDialog is not a substitute. Same dir as the exe, which is what
# THIRD-PARTY-NOTICES.txt calls "the root of this distribution".
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# FFmpeg is the only shared library the x64-windows-mega triplet produces, and
# windeployqt cannot see it: that tool walks Qt's dependency graph, while FFmpeg
# enters through SDKlib. Globbed rather than named, so an FFmpeg version bump
# cannot silently drop a DLL.
set(_vcpkg_triplet_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
file(GLOB _vcpkg_release_dlls "${_vcpkg_triplet_dir}/bin/*.dll")
file(GLOB _vcpkg_debug_dlls   "${_vcpkg_triplet_dir}/debug/bin/*.dll")
if(NOT _vcpkg_release_dlls OR NOT _vcpkg_debug_dlls)
    message(FATAL_ERROR "No vcpkg runtime DLLs under ${_vcpkg_triplet_dir}")
endif()
install(FILES ${_vcpkg_release_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel)
install(FILES ${_vcpkg_debug_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Debug)

# Qt's DLLs, its QML modules and the platform/imageformat/tls plugins. Runs at
# install time, so `cmake --install` and `cpack` both get it while a plain
# `cmake --build` does not -- which is why scripts/package.ps1 exists.
qt_generate_deploy_qml_app_script(
    TARGET MegaExplorer
    OUTPUT_SCRIPT MEGAEXPLORER_DEPLOY_SCRIPT
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${MEGAEXPLORER_DEPLOY_SCRIPT})

# NO_COMPILER_RUNTIME above hands the MSVC redistributable to CMake instead:
# windeployqt finds it through %VCINSTALLDIR%, set only inside a Visual Studio
# developer prompt, so from an ordinary shell it ships nothing and the receiving
# machine fails on VCRUNTIME140.dll. This module reads the toolchain CMake
# configured with. Debug's CRT stays out: it may not be redistributed.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
include(InstallRequiredSystemLibraries)

# Zip only: there is no installer, no registry use and nothing to uninstall, so
# unpacking the archive is the install. Configured last because CPack captures
# the variables at include() time.
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME MegaExplorer)
set(CPACK_PACKAGE_VENDOR "MegaExplorer contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Windows-Explorer-like desktop client for MEGA cloud storage")
set(CPACK_PACKAGE_FILE_NAME "MegaExplorer-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)
