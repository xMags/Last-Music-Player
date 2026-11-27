# The vendored SQLite amalgamation. Installed versions created their database
# with exactly these compile options, so they must not change.
add_library(lmp_sqlite STATIC sqlite3.c sqlite3.h)

set_source_files_properties(sqlite3.c PROPERTIES LANGUAGE C)

# Plain C: no Qt code generators.
set_target_properties(lmp_sqlite PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(lmp_sqlite PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(lmp_sqlite PRIVATE
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
    SQLITE_OMIT_DEPRECATED
    SQLITE_THREADSAFE=2)

# The database is the app's data layer and is never shipped unoptimised, not
# even in Debug. MSVC's Debug defaults (/Od /Ob0 /RTC1) are dropped for this
# directory only: runtime checks cannot be combined with optimisation.
if(MSVC)
    foreach(flags_variable CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO)
        string(REGEX REPLACE "/RTC[1csu]+" "" ${flags_variable} "${${flags_variable}}")
        string(REGEX REPLACE "/O[bd0-9]+" "" ${flags_variable} "${${flags_variable}}")
    endforeach()
    target_compile_options(lmp_sqlite PRIVATE /O2 /Oi /Ob2 /w)
else()
    target_compile_options(lmp_sqlite PRIVATE -O2 -w)
endif()
