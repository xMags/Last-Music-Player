qt_add_executable(lmp_app WIN32
    CloseToTrayController.cpp
    CloseToTrayController.h
    CompositionRoot.cpp
    CompositionRoot.h
    WindowPlacementKeeper.cpp
    WindowPlacementKeeper.h
    main.cpp
    AppIcon.rc
    app.manifest)

# Installed copies and their shortcuts refer to this exact file name.
# The multi-config generator adds the configuration, so the app lands in
# x64\Debug or x64\Release at the repository root.
set_target_properties(lmp_app PROPERTIES
    OUTPUT_NAME "Last_Music_Player"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/x64")

# Copies the Qt libraries, plugins and QML modules next to the exe after each
# link, so x64\<Config> runs on its own without Qt on the PATH. The MSVC
# runtime is not copied here; tools\package.ps1 adds it for the installer.
add_custom_command(TARGET lmp_app POST_BUILD
    COMMAND Qt6::windeployqt
        $<IF:$<CONFIG:Debug>,--debug,--release>
        --qmldir "${CMAKE_SOURCE_DIR}/src/ui/qml"
        --no-translations
        --no-opengl-sw
        --no-compiler-runtime
        --no-system-d3d-compiler
        --skip-plugin-types qmltooling
        --verbose 0
        "$<TARGET_FILE:lmp_app>"
    COMMENT "Deploying Qt next to Last_Music_Player.exe"
    VERBATIM)

target_link_libraries(lmp_app
    PRIVATE lmp_core lmp_platform_windows lmp_ui lmp_uiplugin Qt6::Gui Qt6::Qml Qt6::Quick)

lmp_enable_warnings(lmp_app)
