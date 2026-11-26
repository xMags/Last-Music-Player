# lmp_add_test(<name> SOURCES <files...> [LIBS <targets...>])
# Builds one Qt Test executable and registers it with CTest.
function(lmp_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    lmp_enable_strict_warnings(${name})
    # Every test links the static core and UI libraries, so a full Debug PDB
    # copies their whole debug info into each test (about 1 GB per test).
    # FASTLINK PDBs reference the object files' debug info in the build tree
    # instead; debugging a test still works while that build tree exists.
    # Incremental linking is off as well: its .ilk databases cost about 35 MB
    # per test, and a change to those libraries relinks every test in full.
    if(MSVC)
        target_link_options(${name} PRIVATE
            "$<$<CONFIG:Debug>:/DEBUG:FASTLINK>"
            "$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    endif()
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

lmp_add_test(tst_effect_ownership
    SOURCES ui/tst_effect_ownership.cpp
    LIBS lmp_ui Qt6::Quick)

lmp_add_test(tst_player_bar_view_model
    SOURCES ui/tst_player_bar_view_model.cpp core/FakeConnectSocket.h core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h
    LIBS lmp_ui)

lmp_add_test(tst_fullscreen_player_view_model
    SOURCES ui/tst_fullscreen_player_view_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_queue_list_model
    SOURCES ui/tst_queue_list_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_queue_rail_view_model
    SOURCES ui/tst_queue_rail_view_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h
    LIBS lmp_ui)

lmp_add_test(tst_queue_row_drag
    SOURCES ui/tst_queue_row_drag.cpp
    LIBS lmp_ui)

lmp_add_test(tst_ambient_accent_sampler
    SOURCES ui/tst_ambient_accent_sampler.cpp core/ArtworkFixtures.h core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_artwork_accent
    SOURCES core/tst_artwork_accent.cpp
    LIBS lmp_core)

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

lmp_add_test(tst_secret_store
    SOURCES core/tst_secret_store.cpp core/FakeCredentialStore.h
    LIBS lmp_core)

lmp_add_test(tst_windows_credential_store
    SOURCES platform/tst_windows_credential_store.cpp
    LIBS lmp_platform_windows advapi32)

lmp_add_test(tst_json
    SOURCES core/tst_json.cpp
    LIBS lmp_core)

lmp_add_test(tst_http_client
    SOURCES core/tst_http_client.cpp
    LIBS lmp_core)

lmp_add_test(tst_track_search_policy
    SOURCES core/tst_track_search_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_detail_sort_policy
    SOURCES core/tst_detail_sort_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_browse_grouping_policy
    SOURCES core/tst_browse_grouping_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_playlist_suggestion_policy
    SOURCES core/tst_playlist_suggestion_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_genre_artwork_recipe
    SOURCES core/tst_genre_artwork_recipe.cpp
    LIBS lmp_core)

lmp_add_test(tst_recent_search_store
    SOURCES core/tst_recent_search_store.cpp
    LIBS lmp_core)

lmp_add_test(tst_user_data_operation_gate
    SOURCES core/tst_user_data_operation_gate.cpp
    LIBS lmp_core)

lmp_add_test(tst_embedded_artwork
    SOURCES core/tst_embedded_artwork.cpp core/ArtworkFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_resume_position_policy
    SOURCES core/tst_resume_position_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_playback_history_qualifier
    SOURCES core/tst_playback_history_qualifier.cpp
    LIBS lmp_core)

lmp_add_test(tst_download_policy
    SOURCES core/tst_download_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_played_when
    SOURCES core/tst_played_when.cpp
    LIBS lmp_core)

lmp_add_test(tst_offline_collections
    SOURCES core/tst_offline_collections.cpp
    LIBS lmp_core)

lmp_add_test(tst_auto_sync_policy
    SOURCES core/tst_auto_sync_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_queue_engine
    SOURCES core/tst_queue_engine.cpp
    LIBS lmp_core)

lmp_add_test(tst_player_controller
    SOURCES core/tst_player_controller.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_player_preferences
    SOURCES core/tst_player_preferences.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_player_controller_recovery
    SOURCES core/tst_player_controller_recovery.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_windows_audio_player
    SOURCES platform/tst_windows_audio_player.cpp
    LIBS lmp_platform_windows)

lmp_add_test(tst_playback_state_file
    SOURCES core/tst_playback_state_file.cpp
    LIBS lmp_core)

lmp_add_test(tst_resume_position_tracker
    SOURCES core/tst_resume_position_tracker.cpp core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_playback_state_persistence
    SOURCES core/tst_playback_state_persistence.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_media_controls_throttle
    SOURCES core/tst_media_controls_throttle.cpp core/FakePlaybackClock.h
    LIBS lmp_core)

lmp_add_test(tst_media_controls_bridge
    SOURCES core/tst_media_controls_bridge.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_playback_key_guard
    SOURCES ui/tst_playback_key_guard.cpp
    LIBS lmp_ui)

lmp_add_test(tst_account_url_policy
    SOURCES core/tst_account_url_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_pkce
    SOURCES core/tst_pkce.cpp
    LIBS lmp_core)

lmp_add_test(tst_loopback_request
    SOURCES core/tst_loopback_request.cpp
    LIBS lmp_core)

lmp_add_test(tst_loopback_callback
    SOURCES core/tst_loopback_callback.cpp
    LIBS lmp_core)

lmp_add_test(tst_retry_after
    SOURCES core/tst_retry_after.cpp
    LIBS lmp_core)

lmp_add_test(tst_user_messages
    SOURCES core/tst_user_messages.cpp
    LIBS lmp_core)

lmp_add_test(tst_external_link_policy
    SOURCES core/tst_external_link_policy.cpp
    LIBS lmp_core)

lmp_add_test(tst_account_client
    SOURCES core/tst_account_client.cpp core/FakeHttpTransport.h
    LIBS lmp_core)

lmp_add_test(tst_account_session_restore
    SOURCES core/tst_account_session_restore.cpp core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h
    LIBS lmp_core)

lmp_add_test(tst_account_session_sign_in
    SOURCES core/tst_account_session_sign_in.cpp core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h
    LIBS lmp_core)

lmp_add_test(tst_provider_client
    SOURCES core/tst_provider_client.cpp core/FakeHttpTransport.h
    LIBS lmp_core)

lmp_add_test(tst_capabilities
    SOURCES core/tst_capabilities.cpp
    LIBS lmp_core)

lmp_add_test(tst_mode_service
    SOURCES core/tst_mode_service.cpp core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_mode_service_api_key
    SOURCES core/tst_mode_service_api_key.cpp core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_appearance_settings
    SOURCES core/tst_appearance_settings.cpp
    LIBS lmp_core)

lmp_add_test(tst_preference_defaults
    SOURCES core/tst_preference_defaults.cpp
    LIBS lmp_core)

lmp_add_test(tst_settings_view_model
    SOURCES ui/tst_settings_view_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_theme_settings_binding
    SOURCES ui/tst_theme_settings_binding.cpp
    LIBS lmp_ui)

lmp_add_test(tst_source_mode_view_model
    SOURCES ui/tst_source_mode_view_model.cpp core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_source_choice
    SOURCES core/tst_source_choice.cpp
    LIBS lmp_core)

lmp_add_test(tst_first_run_view_model
    SOURCES ui/tst_first_run_view_model.cpp core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_profile_identity
    SOURCES core/tst_profile_identity.cpp
    LIBS lmp_core)

lmp_add_test(tst_profile_view_model
    SOURCES ui/tst_profile_view_model.cpp core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_library_scanner
    SOURCES core/tst_library_scanner.cpp core/FakeTagReader.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_library_settings_view_model
    SOURCES ui/tst_library_settings_view_model.cpp core/FakeTagReader.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_display_title
    SOURCES core/tst_display_title.cpp
    LIBS lmp_core)

lmp_add_test(tst_track_list_model
    SOURCES ui/tst_track_list_model.cpp
    LIBS lmp_ui)

lmp_add_test(tst_same_song
    SOURCES core/tst_same_song.cpp
    LIBS lmp_core)

lmp_add_test(tst_track_actions
    SOURCES core/tst_track_actions.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_track_menu_builder
    SOURCES ui/tst_track_menu_builder.cpp
    LIBS lmp_ui)

lmp_add_test(tst_library_view_model
    SOURCES ui/tst_library_view_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h core/FakeTagReader.h
    LIBS lmp_ui)

lmp_add_test(tst_offline_view_model
    SOURCES ui/tst_offline_view_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_collection_view_model
    SOURCES ui/tst_collection_view_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_collection_editor
    SOURCES ui/tst_collection_editor.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_home_mix_builder
    SOURCES core/tst_home_mix_builder.cpp
    LIBS lmp_core)

lmp_add_test(tst_home_view_model
    SOURCES ui/tst_home_view_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h core/FakeTagReader.h
    LIBS lmp_ui)

lmp_add_test(tst_search_view_model
    SOURCES ui/tst_search_view_model.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_user_data_cleanup
    SOURCES core/tst_user_data_cleanup.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/ModeServiceFixture.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h core/FakeTagReader.h
    LIBS lmp_core)

lmp_add_test(tst_track_keys
    SOURCES core/tst_track_keys.cpp
    LIBS lmp_core)

lmp_add_test(tst_remote_track_json
    SOURCES core/tst_remote_track_json.cpp
    LIBS lmp_core)

lmp_add_test(tst_account_music_contract
    SOURCES core/tst_account_music_contract.cpp
    LIBS lmp_core)

lmp_add_test(tst_account_music_calls
    SOURCES core/tst_account_music_calls.cpp
    LIBS lmp_core)

lmp_add_test(tst_account_playback
    SOURCES core/tst_account_playback.cpp
    LIBS lmp_core)

lmp_add_test(tst_artwork_service
    SOURCES core/tst_artwork_service.cpp
    LIBS lmp_core)

lmp_add_test(tst_artwork_disk_cache
    SOURCES core/tst_artwork_disk_cache.cpp
    LIBS lmp_core)

lmp_add_test(tst_artwork_image_provider
    SOURCES ui/tst_artwork_image_provider.cpp core/ModeServiceFixture.h core/FakePlaybackClock.h
    LIBS lmp_ui)

lmp_add_test(tst_music_sync_service
    SOURCES core/tst_music_sync_service.cpp core/ModeServiceFixture.h core/FakePlaybackClock.h
    LIBS lmp_core)

lmp_add_test(tst_catalog_page_view_model
    SOURCES ui/tst_catalog_page_view_model.cpp core/CatalogFixtures.h core/ModeServiceFixture.h core/FakePlaybackClock.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h core/PlayerFixture.h core/FakeAudioPlayer.h
    LIBS lmp_ui)

lmp_add_test(tst_welcome_view_model
    SOURCES ui/tst_welcome_view_model.cpp core/ModeServiceFixture.h core/FakePlaybackClock.h core/AccountSessionFixture.h core/FakeCredentialStore.h core/FakeHttpTransport.h core/TempLibrary.h
    LIBS lmp_ui)

lmp_add_test(tst_account_library_editor
    SOURCES core/tst_account_library_editor.cpp core/ModeServiceFixture.h core/FakePlaybackClock.h
    LIBS lmp_core)

lmp_add_test(tst_account_artists
    SOURCES ui/tst_account_artists.cpp core/ModeServiceFixture.h
    LIBS lmp_ui)

lmp_add_test(tst_account_sign_out
    SOURCES core/tst_account_sign_out.cpp core/AccountSessionFixture.h
    LIBS lmp_core)

lmp_add_test(tst_welcome_model
    SOURCES core/tst_welcome_model.cpp
    LIBS lmp_core)

lmp_add_test(tst_catalog_parser
    SOURCES core/tst_catalog_parser.cpp core/CatalogFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_catalog_detail
    SOURCES core/tst_catalog_detail.cpp core/CatalogFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_catalog_paging
    SOURCES core/tst_catalog_paging.cpp
    LIBS lmp_core)

lmp_add_test(tst_catalog_service
    SOURCES core/tst_catalog_service.cpp core/CatalogFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_content_languages
    SOURCES core/tst_content_languages.cpp core/CatalogFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_home_model
    SOURCES core/tst_home_model.cpp core/CatalogFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_chart_art
    SOURCES ui/tst_chart_art.cpp
    LIBS lmp_ui Qt6::Svg)

lmp_add_test(tst_catalog_routes
    SOURCES ui/tst_catalog_routes.cpp
    LIBS lmp_ui)

lmp_add_test(tst_lyrics_service
    SOURCES core/tst_lyrics_service.cpp core/CatalogFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_autoplay_service
    SOURCES core/tst_autoplay_service.cpp
    LIBS lmp_core)

lmp_add_test(tst_share_links
    SOURCES core/tst_share_links.cpp core/CatalogFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_provider_media_addresses
    SOURCES core/tst_provider_media_addresses.cpp
    LIBS lmp_core)

lmp_add_test(tst_remote_music_service
    SOURCES core/tst_remote_music_service.cpp core/ModeServiceFixture.h
    LIBS lmp_core)

lmp_add_test(tst_stream_cache
    SOURCES core/tst_stream_cache.cpp
    LIBS lmp_core)

lmp_add_test(tst_provider_playback
    SOURCES core/tst_provider_playback.cpp core/ModeServiceFixture.h
    LIBS lmp_core)

lmp_add_test(tst_api_key_mode
    SOURCES core/tst_api_key_mode.cpp core/ModeServiceFixture.h core/CatalogFixtures.h
    LIBS lmp_core)

lmp_add_test(tst_download_manager
    SOURCES core/tst_download_manager.cpp
    LIBS lmp_core)

lmp_add_test(tst_audio_output_view_model
    SOURCES ui/tst_audio_output_view_model.cpp core/FakeAudioPlayer.h
    LIBS lmp_ui)

lmp_add_test(tst_presence_protocol
    SOURCES core/tst_presence_protocol.cpp
    LIBS lmp_core)

lmp_add_test(tst_presence_card
    SOURCES core/tst_presence_card.cpp
    LIBS lmp_core)

lmp_add_test(tst_connect_protocol
    SOURCES core/tst_connect_protocol.cpp
    LIBS lmp_core)

lmp_add_test(tst_connect_client
    SOURCES core/tst_connect_client.cpp core/FakeConnectSocket.h core/FakePlaybackClock.h
    LIBS lmp_core)

lmp_add_test(tst_connect_sync
    SOURCES core/tst_connect_sync.cpp core/FakeConnectSocket.h core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_cast
    SOURCES core/tst_cast.cpp
    LIBS lmp_core)

lmp_add_test(tst_player_sink
    SOURCES core/tst_player_sink.cpp core/PlayerFixture.h core/FakeAudioPlayer.h core/FakePlaybackClock.h core/TempLibrary.h
    LIBS lmp_core)

lmp_add_test(tst_history_import
    SOURCES core/tst_history_import.cpp
    LIBS lmp_core)

lmp_add_test(tst_equalizer_view_model
    SOURCES ui/tst_equalizer_view_model.cpp
    LIBS lmp_ui)

lmp_add_test(tst_cover_flight
    SOURCES ui/tst_cover_flight.cpp
    LIBS lmp_ui)

lmp_add_test(tst_page_cascade
    SOURCES ui/tst_page_cascade.cpp
    LIBS lmp_ui)

lmp_add_test(tst_ambient_motion
    SOURCES ui/tst_ambient_motion.cpp
    LIBS lmp_ui)

lmp_add_test(tst_motion_effects
    SOURCES ui/tst_motion_effects.cpp
    LIBS lmp_ui)

lmp_add_test(tst_focus_place
    SOURCES ui/tst_focus_place.cpp
    LIBS lmp_ui)

lmp_add_test(tst_keyed_list_model
    SOURCES ui/tst_keyed_list_model.cpp
    LIBS lmp_ui)

lmp_add_test(tst_cover_wash
    SOURCES ui/tst_cover_wash.cpp
    LIBS lmp_ui)

lmp_add_test(tst_staged_build
    SOURCES ui/tst_staged_build.cpp
    LIBS lmp_ui)

lmp_add_test(tst_art_tile
    SOURCES ui/tst_art_tile.cpp
    LIBS lmp_ui lmp_uiplugin Qt6::Quick)

lmp_add_test(tst_wheel_scroll
    SOURCES ui/tst_wheel_scroll.cpp
    LIBS lmp_ui lmp_uiplugin Qt6::Quick)

# A manual check, built but not registered with CTest: see tests/manual/userdata_probe.cpp.
qt_add_executable(userdata_probe manual/userdata_probe.cpp)
target_link_libraries(userdata_probe PRIVATE lmp_core)
set_target_properties(userdata_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
lmp_enable_strict_warnings(userdata_probe)
