# lmp_add_test(<name> SOURCES <files...> [LIBS <targets...>])
# Builds one Qt Test executable and registers it with CTest.
function(lmp_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    lmp_enable_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

lmp_add_test(tst_app_identity
    SOURCES core/tst_app_identity.cpp
    LIBS lmp_core)

lmp_add_test(tst_private_config
    SOURCES core/tst_private_config.cpp
    LIBS lmp_core)

lmp_add_test(tst_theme
    SOURCES ui/tst_theme.cpp
    LIBS lmp_ui)

lmp_add_test(tst_motion
    SOURCES ui/tst_motion.cpp
    LIBS lmp_ui)

lmp_add_test(tst_fonts
    SOURCES ui/tst_fonts.cpp
    LIBS lmp_ui)

lmp_add_test(tst_icon_set
    SOURCES ui/tst_icon_set.cpp
    LIBS lmp_ui Qt6::Svg)

lmp_add_test(tst_toast_queue
    SOURCES ui/tst_toast_queue.cpp
    LIBS lmp_ui)

lmp_add_test(tst_menu_item_list_model
    SOURCES ui/tst_menu_item_list_model.cpp
    LIBS lmp_ui)

lmp_add_test(tst_artwork_placeholder
    SOURCES core/tst_artwork_placeholder.cpp
    LIBS lmp_core)

lmp_add_test(tst_windows_single_instance
    SOURCES platform/tst_windows_single_instance.cpp
    LIBS lmp_platform_windows)

lmp_add_test(tst_navigator
    SOURCES ui/tst_navigator.cpp
    LIBS lmp_ui)

lmp_add_test(tst_shell_layout
    SOURCES ui/tst_shell_layout.cpp
    LIBS lmp_ui)

lmp_add_test(tst_player_bar_view_model
    SOURCES ui/tst_player_bar_view_model.cpp
    LIBS lmp_ui)

lmp_add_test(tst_result
    SOURCES core/tst_result.cpp
    LIBS lmp_core)

lmp_add_test(tst_request_scope
    SOURCES core/tst_request_scope.cpp
    LIBS lmp_core)

lmp_add_test(tst_app_paths
    SOURCES core/tst_app_paths.cpp
    LIBS lmp_core)

lmp_add_test(tst_strict_utf8
    SOURCES core/tst_strict_utf8.cpp
    LIBS lmp_core)

lmp_add_test(tst_settings_store
    SOURCES core/tst_settings_store.cpp
    LIBS lmp_core)

lmp_add_test(tst_remote_url_policy
    SOURCES core/tst_remote_url_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_track
    SOURCES core/tst_track.cpp
    LIBS lmp_core)

lmp_add_test(tst_database_open
    SOURCES core/tst_database_open.cpp
    LIBS lmp_core)

lmp_add_test(tst_schema
    SOURCES core/tst_schema.cpp
    LIBS lmp_core)

lmp_add_test(tst_provider_url_credential
    SOURCES core/tst_provider_url_credential.cpp
    LIBS lmp_core)

lmp_add_test(tst_library_grouping_sql
    SOURCES core/tst_library_grouping_sql.cpp
    LIBS lmp_core)

lmp_add_test(tst_track_repository
    SOURCES core/tst_track_repository.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_collection_repository
    SOURCES core/tst_collection_repository.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_library_queries
    SOURCES core/tst_library_queries.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_account_repository
    SOURCES core/tst_account_repository.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_account_data_lifecycle
    SOURCES core/tst_account_data_lifecycle.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_followed_artists
    SOURCES core/tst_followed_artists.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_account_outbox_repository
    SOURCES core/tst_account_outbox_repository.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_account_outbox_migration
    SOURCES core/tst_account_outbox_migration.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_account_track_json
    SOURCES core/tst_account_track_json.cpp
    LIBS lmp_core)

lmp_add_test(tst_artwork_url
    SOURCES core/tst_artwork_url.cpp
    LIBS lmp_core)

lmp_add_test(tst_inline_profile_image
    SOURCES core/tst_inline_profile_image.cpp
    LIBS lmp_core)

lmp_add_test(tst_artist_identity
    SOURCES core/tst_artist_identity.cpp
    LIBS lmp_core)
