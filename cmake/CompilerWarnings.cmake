# Warning levels for code written in this project.
#
# lmp_enable_warnings(<target>)         high warning level
# lmp_enable_strict_warnings(<target>)  the same, and warnings fail the build
#
# The strict form is for targets that contain only hand-written code. Targets
# that also compile generated sources (QML modules) use the plain form.

function(lmp_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(lmp_enable_strict_warnings target)
    lmp_enable_warnings(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /WX)
    else()
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
