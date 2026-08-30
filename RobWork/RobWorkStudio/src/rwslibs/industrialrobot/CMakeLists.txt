# Industrial robot redesign target skeleton (WP-01-T02).
# Owner: WP-01. Contract: work-packages/WP-01 §2/§3/§4; architecture/public-interfaces.md.
#
# Declares the 12 build options of §4.1 (11 IRD_* options plus CMake's builtin
# BUILD_TESTING), defines the 10 sdurws_ird_* module targets from anchor sources,
# enforces the §2.2 one-way dependency table at configure time (reverse or
# unregistered dependencies fail the configure step) and computes the disable
# closure so no half targets are produced when options are turned off.
#
# The link topology below is declared EXPLICITLY with real target names so that
# the boundary scanner (WP-01 §7) sees every link site; the factory in
# cmake/IndustrialRobotTargets.cmake owns source collection, the dependency
# assertion, the include install whitelist and the CTest registration.
#
# Case note: cache options are uppercase (IRD_BUILD_CORE) while module names are
# lowercase (core); option lookups use the uppercase form directly.

set(IRD_MODULES core project evidence runtime policy execution diagnostics io reporting ui)

# ---- Options (§4.1) ----
option(IRD_BUILD_CORE "Build sdurws_ird_core (WP-03 domain core)" ON)
option(IRD_BUILD_PROJECT "Build sdurws_ird_project (WP-04)" ON)
option(IRD_BUILD_EVIDENCE "Build sdurws_ird_evidence (WP-05)" ON)
option(IRD_BUILD_RUNTIME "Build sdurws_ird_runtime (WP-06)" ON)
option(IRD_BUILD_POLICY "Build sdurws_ird_policy (WP-07)" ON)
option(IRD_BUILD_EXECUTION "Build sdurws_ird_execution (WP-08)" ON)
option(IRD_BUILD_DIAGNOSTICS "Build sdurws_ird_diagnostics (WP-09)" ON)
option(IRD_BUILD_IO "Build sdurws_ird_io (WP-11)" ON)
option(IRD_BUILD_REPORTING "Build sdurws_ird_reporting (WP-12)" ON)
if(WITH_RWS)
    option(IRD_BUILD_UI "Build sdurws_ird_ui (WP-10; follows WITH_RWS)" ON)
else()
    option(IRD_BUILD_UI "Build sdurws_ird_ui (forced OFF: follows WITH_RWS=OFF)" OFF)
endif()
option(IRD_BUILD_BUSINESS_PLUGINS "Build business plugins (phase B onwards)" OFF)
# BUILD_TESTING: CMake builtin option (include(CTest) in the root scope); the
# per-module CTest targets honour it below. It is the 12th option of §4.1.

# sdurws_ird_ui links the RobWorkStudio plugin surface, so UI requires RWS.
if(IRD_BUILD_UI AND NOT WITH_RWS)
    message(FATAL_ERROR
        "IRD option conflict: IRD_BUILD_UI=ON requires WITH_RWS=ON "
        "(sdurws_ird_ui depends on the RobWorkStudio plugin surface).")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(IndustrialRobotTargets)

# ---- Disable closure (§4.1: no half targets) ----
# Turning a module off also turns off every module that depends on it,
# directly or transitively; the closure runs to a fixed point.
set(IRD_DISABLED_BY_CLOSURE "")
set(IRD_CLOSURE_CHANGED TRUE)
while(IRD_CLOSURE_CHANGED)
    set(IRD_CLOSURE_CHANGED FALSE)
    foreach(_module ${IRD_MODULES})
        string(TOUPPER ${_module} _moduleU)
        if(IRD_BUILD_${_moduleU})
            ird_get_allowed_deps(MODULE ${_module} OUT_VAR _deps)
            foreach(_dep ${_deps})
                string(TOUPPER ${_dep} _depU)
                if(NOT IRD_BUILD_${_depU})
                    set(IRD_BUILD_${_moduleU} OFF)
                    list(APPEND IRD_DISABLED_BY_CLOSURE ${_module})
                    set(IRD_CLOSURE_CHANGED TRUE)
                    break()
                endif()
            endforeach()
        endif()
    endforeach()
endwhile()
foreach(_module ${IRD_DISABLED_BY_CLOSURE})
    message(STATUS "IRD module disabled by dependency closure: sdurws_ird_${_module}")
endforeach()

# ---- Module targets with explicit §2.2 link topology ----
# core: no ird dependencies (WP-03).
if(IRD_BUILD_CORE)
    ird_assert_module_deps(MODULE core)
    ird_add_module(MODULE core)
endif()

# project -> core (WP-04).
if(IRD_BUILD_PROJECT)
    ird_assert_module_deps(MODULE project DEPS core)
    ird_add_module(MODULE project)
    target_link_libraries(sdurws_ird_project PUBLIC sdurws_ird_core)
    target_link_libraries(sdurws_ird_project_test PRIVATE sdurws_ird_project)
endif()

# evidence -> core, project (WP-05).
if(IRD_BUILD_EVIDENCE)
    ird_assert_module_deps(MODULE evidence DEPS core project)
    ird_add_module(MODULE evidence)
    target_link_libraries(sdurws_ird_evidence PUBLIC sdurws_ird_core sdurws_ird_project)
    target_link_libraries(sdurws_ird_evidence_test PRIVATE sdurws_ird_evidence)
endif()

# runtime -> core (WP-06).
if(IRD_BUILD_RUNTIME)
    ird_assert_module_deps(MODULE runtime DEPS core)
    ird_add_module(MODULE runtime)
    target_link_libraries(sdurws_ird_runtime PUBLIC sdurws_ird_core)
    target_link_libraries(sdurws_ird_runtime_test PRIVATE sdurws_ird_runtime)
endif()

# policy -> core, runtime (WP-07).
if(IRD_BUILD_POLICY)
    ird_assert_module_deps(MODULE policy DEPS core runtime)
    ird_add_module(MODULE policy)
    target_link_libraries(sdurws_ird_policy PUBLIC sdurws_ird_core sdurws_ird_runtime)
    target_link_libraries(sdurws_ird_policy_test PRIVATE sdurws_ird_policy)
endif()

# execution -> core, project, evidence (WP-08).
if(IRD_BUILD_EXECUTION)
    ird_assert_module_deps(MODULE execution DEPS core project evidence)
    ird_add_module(MODULE execution)
    target_link_libraries(sdurws_ird_execution PUBLIC sdurws_ird_core sdurws_ird_project sdurws_ird_evidence)
    target_link_libraries(sdurws_ird_execution_test PRIVATE sdurws_ird_execution)
endif()

# diagnostics -> core (WP-09).
if(IRD_BUILD_DIAGNOSTICS)
    ird_assert_module_deps(MODULE diagnostics DEPS core)
    ird_add_module(MODULE diagnostics)
    target_link_libraries(sdurws_ird_diagnostics PUBLIC sdurws_ird_core)
    target_link_libraries(sdurws_ird_diagnostics_test PRIVATE sdurws_ird_diagnostics)
endif()

# io -> core, diagnostics (WP-11).
if(IRD_BUILD_IO)
    ird_assert_module_deps(MODULE io DEPS core diagnostics)
    ird_add_module(MODULE io)
    target_link_libraries(sdurws_ird_io PUBLIC sdurws_ird_core sdurws_ird_diagnostics)
    target_link_libraries(sdurws_ird_io_test PRIVATE sdurws_ird_io)
endif()

# reporting -> evidence, diagnostics (WP-12).
if(IRD_BUILD_REPORTING)
    ird_assert_module_deps(MODULE reporting DEPS evidence diagnostics)
    ird_add_module(MODULE reporting)
    target_link_libraries(sdurws_ird_reporting PUBLIC sdurws_ird_evidence sdurws_ird_diagnostics)
    target_link_libraries(sdurws_ird_reporting_test PRIVATE sdurws_ird_reporting)
endif()

# ui -> all nine ird modules (WP-10; §2.1 top of the topology).
if(IRD_BUILD_UI)
    ird_assert_module_deps(MODULE ui DEPS core project evidence runtime policy execution diagnostics io reporting)
    ird_add_module(MODULE ui)
    target_link_libraries(sdurws_ird_ui PUBLIC
        sdurws_ird_core sdurws_ird_project sdurws_ird_evidence sdurws_ird_runtime
        sdurws_ird_policy sdurws_ird_execution sdurws_ird_diagnostics sdurws_ird_io sdurws_ird_reporting)
    target_link_libraries(sdurws_ird_ui_test PRIVATE sdurws_ird_ui)
endif()

# ---- Configure-time link validation (reverse/unregistered dependencies) ----
ird_validate_module_links(MODULES ${IRD_MODULES})

# ---- Business plugins placeholder (§4.1: phase B onwards) ----
if(IRD_BUILD_BUSINESS_PLUGINS)
    message(STATUS "IRD_BUILD_BUSINESS_PLUGINS=ON: business plugin directories are delivered by phase B task cards; nothing to add yet.")
endif()
