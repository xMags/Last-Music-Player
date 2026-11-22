# The single QML module of the application. C++ types registered for QML and
# every QML file belong to this target.
qt_add_library(lmp_ui STATIC)

# Bundled fonts live in Assets/ with the other shipped files. The alias gives
# them a short path inside the module's resources (FontLoader reads it).
set_source_files_properties("${CMAKE_SOURCE_DIR}/Assets/fonts/MrDeHaviland-Regular.ttf"
    PROPERTIES QT_RESOURCE_ALIAS "fonts/MrDeHaviland-Regular.ttf")
# The design gallery's sample cover: the application logo.
set_source_files_properties("${CMAKE_SOURCE_DIR}/Assets/Square44x44Logo.targetsize-256.png"
    PROPERTIES QT_RESOURCE_ALIAS "dev/sample-cover.png")

# Every QML file of the module. File names are unique: each is a type name.
set(LMP_UI_QML_FILES
    qml/Main.qml
    qml/controls/AccentSwatch.qml
    qml/controls/ActionMenu.qml
    qml/controls/ActionMenuItem.qml
    qml/controls/AppButton.qml
    qml/controls/AppScrollBar.qml
    qml/controls/AppSelect.qml
    qml/controls/AppSlider.qml
    qml/controls/AppSwitch.qml
    qml/controls/ArtTile.qml
    qml/controls/BoxShadow.qml
    qml/controls/ButtonSurface.qml
    qml/controls/Chip.qml
    qml/controls/FocusRing.qml
    qml/controls/Icon.qml
    qml/controls/IconButton.qml
    qml/controls/IconShapePath.qml
    qml/controls/PlayButton.qml
    qml/controls/PromptTextField.qml
    qml/controls/Ripple.qml
    qml/controls/SearchField.qml
    qml/controls/SegmentedControl.qml
    qml/controls/Skeleton.qml
    qml/controls/SkeletonGrid.qml
    qml/controls/SkeletonRows.qml
    qml/controls/SkeletonSweep.qml
    qml/controls/SliderBubble.qml
    qml/controls/TextPromptDialog.qml
    qml/controls/Toast.qml
    qml/controls/ToastStack.qml
    qml/dev/Gallery.qml
    qml/dev/GalleryArtwork.qml
    qml/dev/GalleryControls.qml
    qml/dev/GalleryIcons.qml
    qml/dev/GalleryOverlays.qml
    qml/dev/GallerySection.qml
    qml/dev/GalleryTokens.qml
    qml/shell/CaptionButton.qml
    qml/shell/PageHost.qml
    qml/shell/PlaceholderPage.qml
    qml/shell/Shell.qml
    qml/shell/TitleStrip.qml)

qt_add_qml_module(lmp_ui
    URI LastMusicPlayer
    VERSION 1.0
    # QML tooling expects the output folder to end with the module path.
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/LastMusicPlayer"
    SOURCES
        app/AppInfo.cpp
        app/AppInfo.h
        dev/GalleryViewModel.cpp
        dev/GalleryViewModel.h
        dev/ThemeTokenListModel.cpp
        dev/ThemeTokenListModel.h
        icons/IconData.h
        icons/IconDataLine.cpp
        icons/IconDataLogos.cpp
        icons/IconDataSolid.cpp
        icons/IconPart.cpp
        icons/IconPart.h
        icons/IconSet.cpp
        icons/IconSet.h
        menus/MenuItemListModel.cpp
        menus/MenuItemListModel.h
        navigation/Navigator.cpp
        navigation/Navigator.h
        navigation/Route.cpp
        navigation/Route.h
        navigation/RouteRules.cpp
        navigation/RouteRules.h
        shell/AppRoot.cpp
        shell/AppRoot.h
        shell/ShellLayoutViewModel.cpp
        shell/ShellLayoutViewModel.h
        shell/WindowChromeViewModel.cpp
        shell/WindowChromeViewModel.h
        theme/ColorMix.cpp
        theme/ColorMix.h
        theme/FontLoader.cpp
        theme/FontLoader.h
        theme/Motion.cpp
        theme/Motion.h
        theme/ShadowLayer.cpp
        theme/ShadowLayer.h
        theme/TextStyles.cpp
        theme/TextStyles.h
        theme/Theme.cpp
        theme/Theme.h
        theme/ThemeColors.cpp
        theme/ThemeColors.h
        theme/ThemeConstants.cpp
        theme/ThemeConstants.h
        theme/ThemePalette.cpp
        theme/ThemePalette.h
        theme/ThemeTypography.cpp
        theme/ThemeTypography.h
        toasts/ToastQueue.cpp
        toasts/ToastQueue.h
    QML_FILES ${LMP_UI_QML_FILES}
    RESOURCES
        "${CMAKE_SOURCE_DIR}/Assets/fonts/MrDeHaviland-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/Assets/Square44x44Logo.targetsize-256.png")

# Every folder that holds a header with a QML-registered type. The generated
# registration code includes those headers by file name only, so each folder
# must be on the include path and header file names must be unique in src/ui.
set(LMP_UI_TYPE_FOLDERS
    app
    dev
    icons
    menus
    navigation
    shell
    theme
    toasts)

target_include_directories(lmp_ui
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE ${LMP_UI_TYPE_FOLDERS})

target_link_libraries(lmp_ui
    PUBLIC lmp_core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

lmp_enable_warnings(lmp_ui)

# The QML compiler turns each QML file into C++ that reads results of C++
# methods through QJSEngine::fromVariant<T>(). For bool, int, double and
# QString that template ends in a branch the compiler can never reach, and
# MSVC reports C4702 inside the Qt header. Silence that one warning in those
# generated files only; hand-written code keeps it.
if(MSVC)
    foreach(qml_file IN LISTS LMP_UI_QML_FILES)
        string(REGEX REPLACE "\\.qml$" "" qml_stem "${qml_file}")
        set_source_files_properties(
            "${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmlcache/lmp_ui_${qml_stem}_qml.cpp"
            TARGET_DIRECTORY lmp_ui
            PROPERTIES COMPILE_OPTIONS /wd4702)
    endforeach()
endif()
