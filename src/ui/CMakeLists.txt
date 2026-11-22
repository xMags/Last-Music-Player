# The single QML module of the application. C++ types registered for QML and
# every QML file belong to this target.
qt_add_library(lmp_ui STATIC)

# Bundled fonts live in Assets/ with the other shipped files. The alias gives
# them a short path inside the module's resources (FontLoader reads it).
set_source_files_properties("${CMAKE_SOURCE_DIR}/Assets/fonts/MrDeHaviland-Regular.ttf"
    PROPERTIES QT_RESOURCE_ALIAS "fonts/MrDeHaviland-Regular.ttf")

qt_add_qml_module(lmp_ui
    URI LastMusicPlayer
    VERSION 1.0
    # QML tooling expects the output folder to end with the module path.
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/LastMusicPlayer"
    SOURCES
        app/AppInfo.cpp
        app/AppInfo.h
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
        theme/ThemeConstants.cpp
        theme/ThemeConstants.h
        theme/ThemePalette.cpp
        theme/ThemePalette.h
    QML_FILES
        qml/Main.qml
        qml/controls/VersionLabel.qml
    RESOURCES
        "${CMAKE_SOURCE_DIR}/Assets/fonts/MrDeHaviland-Regular.ttf")

# Every folder that holds a header with a QML-registered type. The generated
# registration code includes those headers by file name only, so each folder
# must be on the include path and header file names must be unique in src/ui.
set(LMP_UI_TYPE_FOLDERS
    app
    theme)

target_include_directories(lmp_ui
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE ${LMP_UI_TYPE_FOLDERS})

target_link_libraries(lmp_ui
    PUBLIC lmp_core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

lmp_enable_warnings(lmp_ui)
