# The vendored TagLib 2.3.2 (upstream/, unchanged from the release tarball
# taglib-2.3.2.tar.gz, sha256 3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc;
# its tests, examples, bindings and docs left out). Downloads get their cover
# and lyrics written into the file with it.
#
# The options below are plain variables of this directory, so they reach
# TagLib's option() calls (policy CMP0077) without changing the app's own
# settings: BUILD_SHARED_LIBS and BUILD_TESTING stay as they are elsewhere.
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_BINDINGS OFF)
set(ENABLE_CCACHE OFF)
# Only the formats downloads can come in: MP4 (.m4a), MPEG (.mp3, always
# built), FLAC, Ogg and Opus (Vorbis), and WAV (RIFF).
set(WITH_APE OFF)
set(WITH_ASF OFF)
set(WITH_DSF OFF)
set(WITH_MATROSKA OFF)
set(WITH_MOD OFF)
set(WITH_MP4 ON)
set(WITH_RIFF ON)
set(WITH_SHORTEN OFF)
set(WITH_TRUEAUDIO OFF)
set(WITH_VORBIS ON)
# Compressed ID3v2 frames are rare and only matter when reading.
set(WITH_ZLIB OFF)

add_subdirectory(upstream EXCLUDE_FROM_ALL)

# Third-party code: its warnings are not the app's.
if(MSVC)
    target_compile_options(tag PRIVATE /w)
else()
    target_compile_options(tag PRIVATE -w)
endif()
set_target_properties(tag PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# TagLib's own include folders reach only its own sources (an installed
# TagLib flattens them into one); the app includes its headers by name
# ("mp4file.h") through this target. Included as system headers.
set(lmp_taglib_root "${CMAKE_CURRENT_SOURCE_DIR}/upstream/taglib")
add_library(lmp_taglib INTERFACE)
target_link_libraries(lmp_taglib INTERFACE tag)
target_include_directories(lmp_taglib SYSTEM INTERFACE
    "${lmp_taglib_root}"
    "${lmp_taglib_root}/toolkit"
    "${lmp_taglib_root}/mpeg"
    "${lmp_taglib_root}/mpeg/id3v2"
    "${lmp_taglib_root}/mpeg/id3v2/frames"
    "${lmp_taglib_root}/mpeg/id3v1"
    "${lmp_taglib_root}/ogg"
    "${lmp_taglib_root}/ogg/flac"
    "${lmp_taglib_root}/ogg/vorbis"
    "${lmp_taglib_root}/ogg/opus"
    "${lmp_taglib_root}/flac"
    "${lmp_taglib_root}/mp4"
    "${lmp_taglib_root}/riff"
    "${lmp_taglib_root}/riff/wav"
    # taglib_config.h, made when TagLib is configured.
    "${CMAKE_CURRENT_BINARY_DIR}/upstream")
