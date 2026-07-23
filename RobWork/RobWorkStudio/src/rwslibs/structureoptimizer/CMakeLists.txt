# 结构优化子系统名
set(SUBSYS_NAME sdurws_structureoptimizer)
set(SUBSYS_DESC "Structure optimization plugin")

set(SUBSYS_DEPS
    PUBLIC  sdurws
            RW::sdurw_core
            RW::sdurw_math
            RW::sdurw_kinematics
            RW::sdurw_models
            RW::sdurw_proximity
)

set(build TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    # 核心库 — 纯数据类型、验证逻辑、设计变量突变、目标评分、模型工厂，无 UI 依赖。
    set(CoreSrcFiles
        StructureOptimizationValidation.cpp
        StructureDesignMutator.cpp
        StructureObjectiveScorer.cpp
        StructureCandidateGenerator.cpp
        StructureCandidateCache.cpp
        CandidateModelFactory.cpp
        StructureCandidateEvaluator.cpp
        HybridStructureOptimizer.cpp
        StructureSensitivityAnalyzer.cpp
        StructureOptimizationJson.cpp
        StructureOptimizationCsv.cpp
        StructureCandidateExporter.cpp
    )
    set(CoreHeaderFiles
        StructureOptimizationTypes.hpp
        StructureOptimizationValidation.hpp
        StructureDesignMutator.hpp
        StructureObjectiveScorer.hpp
        StructureCandidateGenerator.hpp
        StructureCandidateCache.hpp
        CandidateModelFactory.hpp
        StructureCandidateEvaluator.hpp
        StructureOptimizationStrategy.hpp
        HybridStructureOptimizer.hpp
        StructureSensitivityAnalyzer.hpp
        StructureOptimizationJson.hpp
        StructureOptimizationCsv.hpp
        StructureCandidateExporter.hpp
    )

    rws_add_component(sdurws_structureoptimizer_core ${CoreSrcFiles} ${CoreHeaderFiles})
    rw_add_includes(sdurws_structureoptimizer_core "rwslibs/structureoptimizer" ${CoreHeaderFiles})
    target_link_libraries(sdurws_structureoptimizer_core
        PUBLIC sdurws_robotanalysiscore RW::sdurw_core RW::sdurw_math
               RW::sdurw_kinematics RW::sdurw_models RW::sdurw_proximity
        PRIVATE sdurws_kinematicanalysis_core
                sdurws_robotmodelbuilder
                RW::sdurw_loaders
    )
    set_target_properties(sdurws_structureoptimizer_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

    # ── 测试可执行文件 ────────────────────────────────────────────────────
    add_executable(sdurws_structureoptimizer_test
        StructureOptimizationTest.cpp
    )
    target_link_libraries(sdurws_structureoptimizer_test
        PRIVATE
            sdurws_structureoptimizer_core
            ${QT_LIBRARIES}
    )
    target_include_directories(sdurws_structureoptimizer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


    # ── 插件目标 (暂不注册为 RobWorkStudio 插件，后续 Task 15 启用) ────
    # 源文件已创建，仅需取消下方注释即可启用。
    # set(SrcFiles
    #     StructureOptimizerPlugin.cpp
    #     StructureOptimizerWidget.cpp
    # )
    # set(SRC_FILES_HPP
    #     StructureOptimizerPlugin.hpp
    #     StructureOptimizerWidget.hpp
    # )
    # if(DEFINED Qt6Core_VERSION)
    #     qt_add_resources(RccSrcFiles resources.qrc)
    # else()
    #     qt5_add_resources(RccSrcFiles resources.qrc)
    # endif()
    #
    # rws_add_plugin(
    #     ${SUBSYS_NAME}
    #     ${RWS_DEFAULT_LIB_TYPE}
    #     ${SrcFiles}
    #     ${RccSrcFiles}
    # )
    # rw_add_includes(${SUBSYS_NAME} "rwslibs/structureoptimizer" ${SRC_FILES_HPP})
    # target_link_libraries(${SUBSYS_NAME} ${SUBSYS_DEPS} PUBLIC ${QT_LIBRARIES})
    # target_link_libraries(${SUBSYS_NAME} PRIVATE sdurws_structureoptimizer_core)
    # target_include_directories(${SUBSYS_NAME}
    #     INTERFACE
    #     $<BUILD_INTERFACE:${RWS_ROOT}/src> $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
    # )


    # ── CTest 注册 ───────────────────────────────────────────────────────
    if(BUILD_TESTING)
        add_test(
            NAME sdurws_structureoptimizer_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test>
        )
        set_tests_properties(
            sdurws_structureoptimizer_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
    endif()
endif()
