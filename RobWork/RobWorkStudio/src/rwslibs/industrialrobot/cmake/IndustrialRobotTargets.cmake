# Industrial robot redesign module targets (WP-01-T02).
# Owner: WP-01. Contract: work-packages/WP-01 §2.2/§3; architecture/public-interfaces.md.
#
# ird_get_allowed_deps       — §2.2 allowed-dependency table (single authority).
# ird_assert_module_deps     — configure-time assertion that a module's requested
#                              dependencies are inside its §2.2 allowed set and
#                              that none of them is disabled.
# ird_add_module             — explicit source collection (GLOB forbidden per §3),
#                              include install whitelist, per-module CTest
#                              registration. Link wiring is declared explicitly
#                              in industrialrobot/CMakeLists.txt (real target
#                              names, visible to the boundary scanner).
# ird_validate_module_links  — configure-time final pass over created targets:
#                              reverse or unregistered ird dependencies fail here.

# §2.2 allowed dependency sets (ird part). Qt/RobWork/RWS links are added by the
# owning task cards when real code arrives; the skeleton links ird targets only.
function(ird_get_allowed_deps)
    set(oneValueArgs MODULE OUT_VAR)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "${oneValueArgs}" "")
    set(_deps "")
    if(ARG_MODULE STREQUAL "core")
        set(_deps "")
    elseif(ARG_MODULE STREQUAL "project")
        set(_deps "core")
    elseif(ARG_MODULE STREQUAL "evidence")
        set(_deps "core;project")
    elseif(ARG_MODULE STREQUAL "runtime")
        set(_deps "core")
    elseif(ARG_MODULE STREQUAL "policy")
        set(_deps "core;runtime")
    elseif(ARG_MODULE STREQUAL "execution")
        set(_deps "core;project;evidence")
    elseif(ARG_MODULE STREQUAL "diagnostics")
        set(_deps "core")
    elseif(ARG_MODULE STREQUAL "io")
        set(_deps "core;diagnostics")
    elseif(ARG_MODULE STREQUAL "reporting")
        set(_deps "evidence;diagnostics")
    elseif(ARG_MODULE STREQUAL "ui")
        set(_deps "core;project;evidence;runtime;policy;execution;diagnostics;io;reporting")
    else()
        message(FATAL_ERROR "Unknown IRD module: ${ARG_MODULE}")
    endif()
    set(${ARG_OUT_VAR} ${_deps} PARENT_SCOPE)
endfunction()

function(ird_assert_module_deps)
    set(oneValueArgs MODULE)
    set(multiValueArgs DEPS)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "${oneValueArgs}" "${multiValueArgs}")
    set(_target sdurws_ird_${ARG_MODULE})

    # §2.2 dependency assertion: every requested dependency must be in the
    # module's allowed set and must be enabled (no links into disabled modules).
    ird_get_allowed_deps(MODULE ${ARG_MODULE} OUT_VAR _allowed)
    foreach(_dep ${ARG_DEPS})
        if(NOT _dep IN_LIST _allowed)
            message(FATAL_ERROR
                "IRD dependency assertion failed: ${_target} -> sdurws_ird_${_dep} "
                "is not in the §2.2 allowed dependency set of ${ARG_MODULE} "
                "(reverse or unregistered dependency).")
        endif()
        string(TOUPPER ${_dep} _depU)
        if(NOT IRD_BUILD_${_depU})
            message(FATAL_ERROR
                "IRD dependency assertion failed: ${_target} depends on disabled module sdurws_ird_${_dep}.")
        endif()
    endforeach()
endfunction()

function(ird_add_module)
    set(oneValueArgs MODULE)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "${oneValueArgs}" "")
    set(_module ${ARG_MODULE})
    set(_target sdurws_ird_${_module})
    set(_srcDir ${CMAKE_CURRENT_SOURCE_DIR}/${_module})
    set(_anchor ${_srcDir}/src/${_module}_anchor.cpp)
    set(_smoke ${_srcDir}/test/${_module}_smoke.cpp)

    if(NOT EXISTS ${_anchor})
        message(FATAL_ERROR "IRD module ${_module}: anchor source missing: ${_anchor}")
    endif()

    # Explicit source collection (§3: GLOB forbidden). Link wiring is declared
    # explicitly in industrialrobot/CMakeLists.txt.
    add_library(${_target} STATIC ${_anchor})
    target_include_directories(${_target} PUBLIC $<BUILD_INTERFACE:${_srcDir}/include>)

    # Include install whitelist: only public headers below include/, never
    # private headers, test data or absolute build paths (§7 install rule).
    if(EXISTS ${_srcDir}/include)
        install(DIRECTORY ${_srcDir}/include/
                DESTINATION include/sdurws/ird/${_module}
                FILES_MATCHING
                    PATTERN "*.hpp"
                    PATTERN "*.h"
                    PATTERN ".gitkeep" EXCLUDE)
    endif()

    # Per-module CTest: the smoke executable links the module target in
    # industrialrobot/CMakeLists.txt so every test run exercises the link.
    if(BUILD_TESTING AND EXISTS ${_smoke})
        add_executable(${_target}_test ${_smoke})
        add_test(NAME ${_target}_test COMMAND ${_target}_test)
    endif()
endfunction()

# Configure-time final pass: validates the ACTUAL link interface of every created
# target against the §2.2 table, so hand-injected reverse dependencies (e.g. a
# target_link_libraries(sdurws_ird_core ...) line added after the explicit
# topology) fail the configure step instead of silently building.
function(ird_validate_module_links)
    set(multiValueArgs MODULES)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "" "${multiValueArgs}")
    set(_linkKeywords "PUBLIC;PRIVATE;INTERFACE;DEBUG;OPTIMIZED;GENERAL;LINK_PUBLIC;LINK_PRIVATE;LINK_INTERFACE_LIBRARIES")
    foreach(_module ${ARG_MODULES})
        set(_target sdurws_ird_${_module})
        if(NOT TARGET ${_target})
            continue()
        endif()
        get_target_property(_libs ${_target} LINK_LIBRARIES)
        if(NOT _libs)
            continue()
        endif()
        ird_get_allowed_deps(MODULE ${_module} OUT_VAR _allowed)
        foreach(_lib ${_libs})
            if(_lib IN_LIST _linkKeywords)
                continue()
            endif()
            string(REGEX MATCH "^sdurws_ird_[a-z0-9]+$" _irdDep ${_lib})
            if(NOT _irdDep)
                continue()
            endif()
            string(REPLACE "sdurws_ird_" "" _depName ${_irdDep})
            if(NOT _depName IN_LIST _allowed)
                message(FATAL_ERROR
                    "IRD link validation failed: ${_target} links sdurws_ird_${_depName} "
                    "(reverse or unregistered dependency, §2.2).")
            endif()
        endforeach()
    endforeach()
endfunction()
