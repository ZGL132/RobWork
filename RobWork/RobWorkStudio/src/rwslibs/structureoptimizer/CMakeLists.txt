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
    if(DEFINED Qt6Core_VERSION)
        find_package(Qt6 COMPONENTS Concurrent QUIET)
    else()
        find_package(Qt5 COMPONENTS Concurrent QUIET)
    endif()

    # 核心库 — 纯数据类型、验证逻辑、设计变量突变、目标评分、模型工厂，无 UI 依赖。
    set(CoreSrcFiles
        EvaluationPlan.cpp
        EvaluationStage.cpp
        TaskEvaluationStage.cpp
        EstimatedWorkspaceStage.cpp
        VerifiedRegionStage.cpp
        OrientationCoverageStage.cpp
        KinematicMetricAggregator.cpp
        CandidateResult.cpp
        QuickScreeningPolicy.cpp
        CacheKey.cpp
        EvaluationCache.cpp
        DeterministicSeed.cpp
        InitialSampler.cpp
        MetricRegistry.cpp
        ConstraintObjective.cpp
        AdapterRegistry.cpp
        CandidatePatch.cpp
        CandidatePatchMerge.cpp
        CandidatePatchApply.cpp
        CandidateCompiler.cpp
        CanonicalBaselineEvaluationBridge.cpp
        RobotModelSpecProjectionAdapter.cpp
        EvaluationDeviceBuilder.cpp
        CanonicalForwardKinematics.cpp
        CanonicalKinematicModel.cpp
        CanonicalModelShadowService.cpp
        DesignVariable.cpp
        DesignSpaceRegistry.cpp
        DesignTemplateApplication.cpp
        DerivedExpression.cpp
        DependencyGraph.cpp
        DesignSpaceCompiler.cpp
        DesignVector.cpp
        DhProjection.cpp
        KinematicBaselineSnapshot.cpp
        KinematicFingerprint.cpp
        KinematicModelImporter.cpp
        PoseDelta.cpp
        BasePlacementAdapter.cpp
        FlangePoseAdapter.cpp
        TcpPoseAdapter.cpp
        JointAxisAdapter.cpp
        JointLimitAdapter.cpp
        JointOriginAdapter.cpp
        JointZeroAdapter.cpp
        LegacyDesignSpaceAdapter.cpp
        ParameterizedLinkAdapter.cpp
        ParameterizedGeometryAdapter.cpp
        ParameterizedCollisionAdapter.cpp
        MeshTransformAdapter.cpp
        ParameterBinding.cpp
        ParameterizationMode.cpp
        WriteSetValidator.cpp
        KinematicConventions.cpp
        StructureOptimizationContracts.cpp
        StructureOptimizationValidation.cpp
        StructureDesignMutator.cpp
        StructureObjectiveScorer.cpp
        StructureCandidateGenerator.cpp
        StructureCandidateCache.cpp
        CandidateModelFactory.cpp
        StructureCandidateEvaluator.cpp
        KinematicEngineeringEvaluator.cpp
        EngineeringEvaluatorPipeline.cpp
        SystemEngineeringOptimizer.cpp
        HybridStructureOptimizer.cpp
        StructureSensitivityAnalyzer.cpp
        StructureOptimizationJson.cpp
        StructureOptimizationCsv.cpp
        StructureOptimizationObjectiveProfile.cpp
        StructureCandidateExporter.cpp
        StructureOptimizationProjectAdapter.cpp
        StructureOptimizationReportWriter.cpp
        StructureOptimizationExportService.cpp
        StructureOptimizationProjectFactory.cpp
        StructureOptimizationUiLogic.cpp
        StructureOptimizationTemplate.cpp
        StructureCandidateComparison.cpp
        StructureWorkspaceCoverage.cpp
        RobotModelStalenessChecker.cpp
        EngineeringRequirementArtifactAdapter.cpp
        FrozenRequirementProjectImportService.cpp
    )
    set(CoreHeaderFiles
        EvaluationPlan.hpp
        EvaluationStage.hpp
        TaskEvaluationStage.hpp
        EstimatedWorkspaceStage.hpp
        VerifiedRegionStage.hpp
        OrientationCoverageStage.hpp
        KinematicMetricAggregator.hpp
        CandidateResult.hpp
        QuickScreeningPolicy.hpp
        CacheKey.hpp
        EvaluationCache.hpp
        DeterministicSeed.hpp
        InitialSampler.hpp
        MetricRegistry.hpp
        ConstraintObjective.hpp
        AdapterRegistry.hpp
        CandidateCompilerDiagnostics.hpp
        CandidatePatch.hpp
        CandidatePatchMerge.hpp
        CandidatePatchApply.hpp
        CandidateCompiler.hpp
        CanonicalBaselineEvaluationBridge.hpp
        RobotModelSpecProjectionAdapter.hpp
        EvaluationDeviceBuilder.hpp
        CompiledCandidate.hpp
        CanonicalForwardKinematics.hpp
        CanonicalKinematicModel.hpp
        CanonicalModelShadowService.hpp
        DesignVariable.hpp
        DesignSpaceRegistry.hpp
        DesignTemplateApplication.hpp
        DerivedExpression.hpp
        DependencyGraph.hpp
        CompiledDesignSpace.hpp
        DesignSpaceCompiler.hpp
        DesignVector.hpp
        AdapterCapability.hpp
        DhProjection.hpp
        KinematicBaselineSnapshot.hpp
        KinematicFingerprint.hpp
        KinematicImportResult.hpp
        KinematicModelImporter.hpp
        PoseDelta.hpp
        BasePlacementAdapter.hpp
        FlangePoseAdapter.hpp
        TcpPoseAdapter.hpp
        JointAxisAdapter.hpp
        JointLimitAdapter.hpp
        JointOriginAdapter.hpp
        JointZeroAdapter.hpp
        LegacyDesignSpaceAdapter.hpp
        ModelParameterAdapter.hpp
        ParameterBinding.hpp
        ParameterizedLinkAdapter.hpp
        ParameterizedGeometryAdapter.hpp
        ParameterizedCollisionAdapter.hpp
        MeshTransformAdapter.hpp
        GeometryAdapterSupport.hpp
        ParameterizationMode.hpp
        WriteSetValidator.hpp
        KinematicConventions.hpp
        StructureOptimizationContracts.hpp
        StructureOptimizationTypes.hpp
        StructureOptimizationValidation.hpp
        StructureDesignMutator.hpp
        StructureObjectiveScorer.hpp
        StructureCandidateGenerator.hpp
        StructureCandidateCache.hpp
        CandidateModelFactory.hpp
        StructureCandidateEvaluator.hpp
        KinematicEngineeringEvaluator.hpp
        EngineeringEvaluatorPipeline.hpp
        SystemEngineeringOptimizer.hpp
        StructureOptimizationStrategy.hpp
        HybridStructureOptimizer.hpp
        StructureSensitivityAnalyzer.hpp
        StructureOptimizationJson.hpp
        StructureOptimizationCsv.hpp
        StructureOptimizationObjectiveProfile.hpp
        StructureCandidateExporter.hpp
        StructureOptimizationProjectAdapter.hpp
        StructureOptimizationReportWriter.hpp
        StructureOptimizationExportService.hpp
        StructureOptimizationProjectFactory.hpp
        StructureOptimizationUiLogic.hpp
        StructureOptimizationTemplate.hpp
        StructureCandidateComparison.hpp
        StructureWorkspaceCoverage.hpp
        RobotModelStalenessChecker.hpp
        EngineeringRequirementArtifactAdapter.hpp
        FrozenRequirementProjectImportService.hpp
    )

    rws_add_component(sdurws_structureoptimizer_core ${CoreSrcFiles} ${CoreHeaderFiles})
    rw_add_includes(sdurws_structureoptimizer_core "rwslibs/structureoptimizer" ${CoreHeaderFiles})
    target_link_libraries(sdurws_structureoptimizer_core
        PUBLIC sdurws_robotanalysiscore RW::sdurw_core RW::sdurw_math
               RW::sdurw_kinematics RW::sdurw_models RW::sdurw_proximity
        PRIVATE sdurws_kinematicanalysis_core
                sdurws_robotmodelbuilder
                sdurws_engineeringrequirements_core
                RW::sdurw_loaders
    )
    set_target_properties(sdurws_structureoptimizer_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

    # ── 测试可执行文件 ────────────────────────────────────────────────────
    add_executable(sdurws_structureoptimizer_test
        StructureOptimizationTest.cpp
        StructureOptimizerPlugin.cpp
        StructureOptimizerPlugin.hpp
        StructureVariableTableModel.cpp
        StructureVariableTableModel.hpp
        StructureVariableFilterProxyModel.cpp
        StructureVariableFilterProxyModel.hpp
        OptimizationTaskTableModel.cpp
        OptimizationTaskTableModel.hpp
        StructureCandidateTableModel.cpp
        StructureCandidateTableModel.hpp
        StructureOptimizerWidget.cpp
        StructureOptimizerWidget.hpp
        StructureOptimizationController.cpp
        StructureOptimizationController.hpp
        StructureConstraintTableModel.cpp
        StructureConstraintTableModel.hpp
        CandidatePreviewController.cpp
        CandidatePreviewController.hpp
    )
    target_link_libraries(sdurws_structureoptimizer_test
        PRIVATE
            sdurws_structureoptimizer_core
            sdurws_engineeringrequirements_core
            sdurws_kinematicanalysis
            sdurws_kinematicanalysis_core
            sdurws_robotmodelbuilder
            ${QT_LIBRARIES}
    )
    foreach(qt_concurrent_target Qt6::Concurrent Qt5::Concurrent)
        if(TARGET ${qt_concurrent_target})
            target_link_libraries(sdurws_structureoptimizer_test PRIVATE ${qt_concurrent_target})
        endif()
    endforeach()
    target_include_directories(sdurws_structureoptimizer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(sdurws_structureoptimizer_test PRIVATE
        STRUCTUREOPTIMIZER_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")


    # ── 插件目标 ─────────────────────────────────────────────────────────
    set(SrcFiles
        StructureOptimizerPlugin.cpp
        StructureOptimizerWidget.cpp
        StructureVariableTableModel.cpp
        StructureVariableFilterProxyModel.cpp
        OptimizationTaskTableModel.cpp
        StructureCandidateTableModel.cpp
        StructureOptimizationController.cpp
        StructureConstraintTableModel.cpp
        CandidatePreviewController.cpp
    )
    set(SRC_FILES_HPP
        StructureOptimizerPlugin.hpp
        StructureOptimizerWidget.hpp
        StructureVariableTableModel.hpp
        StructureVariableFilterProxyModel.hpp
        OptimizationTaskTableModel.hpp
        StructureCandidateTableModel.hpp
        StructureOptimizationController.hpp
        StructureConstraintTableModel.hpp
        CandidatePreviewController.hpp
    )
    if(DEFINED Qt6Core_VERSION)
        qt_add_resources(RccSrcFiles resources.qrc)
    else()
        qt5_add_resources(RccSrcFiles resources.qrc)
    endif()

    rws_add_plugin(
        ${SUBSYS_NAME}
        ${RWS_DEFAULT_LIB_TYPE}
        ${SrcFiles}
        ${RccSrcFiles}
    )
    rw_add_includes(${SUBSYS_NAME} "rwslibs/structureoptimizer" ${SRC_FILES_HPP})
    target_link_libraries(${SUBSYS_NAME} ${SUBSYS_DEPS} PUBLIC ${QT_LIBRARIES})
    target_link_libraries(${SUBSYS_NAME} PRIVATE sdurws_structureoptimizer_core)
    foreach(qt_concurrent_target Qt6::Concurrent Qt5::Concurrent)
        if(TARGET ${qt_concurrent_target})
            target_link_libraries(${SUBSYS_NAME} PRIVATE ${qt_concurrent_target})
        endif()
    endforeach()
    target_include_directories(${SUBSYS_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${RWS_ROOT}/src> $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
    )

    # RobWorkStudio 插件元数据
    rws_plugin_load_details(${SUBSYS_NAME} 1 StructureOptimizer false)
    if("${RWS_DEFAULT_LIB_TYPE}" STREQUAL "STATIC")
        set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} ${SUBSYS_NAME} PARENT_SCOPE)
    endif()


    # ── CTest 注册 ───────────────────────────────────────────────────────
    if(BUILD_TESTING)
        if(WIN32)
            set(STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM windows)
        else()
            set(STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM offscreen)
        endif()
        add_test(
            NAME sdurws_structureoptimizer_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test>
        )
        set_tests_properties(
            sdurws_structureoptimizer_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
                ENVIRONMENT "QT_QPA_PLATFORM=${STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM}"
        )
        add_test(
            NAME sdurws_structureoptimizer_test_evaluator_consistency
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> evaluator_consistency
        )
        set_tests_properties(
            sdurws_structureoptimizer_test_evaluator_consistency
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_test_cache
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> cache
        )
        set_tests_properties(
            sdurws_structureoptimizer_test_cache
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_resource_dependencies_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> resource_dependencies
        )
        set_tests_properties(
            sdurws_structureoptimizer_resource_dependencies_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
                ENVIRONMENT "QT_QPA_PLATFORM=${STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM}"
        )
        add_test(
            NAME sdurws_structureoptimizer_project_context_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> project_context
        )
        set_tests_properties(
            sdurws_structureoptimizer_project_context_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_evaluation_plan_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> evaluation_plan
        )
        set_tests_properties(
            sdurws_structureoptimizer_evaluation_plan_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_evaluation_pipeline_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> evaluation_pipeline
        )
        set_tests_properties(
            sdurws_structureoptimizer_evaluation_pipeline_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_constraint_objective_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> constraint_objective
        )
        set_tests_properties(
            sdurws_structureoptimizer_constraint_objective_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_task_evaluation_stage_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> task_evaluation_stage
        )
        set_tests_properties(
            sdurws_structureoptimizer_task_evaluation_stage_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_spatial_evaluation_stages_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> spatial_evaluation_stages
        )
        set_tests_properties(
            sdurws_structureoptimizer_spatial_evaluation_stages_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_kinematic_metric_aggregator_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> kinematic_metric_aggregator
        )
        set_tests_properties(
            sdurws_structureoptimizer_kinematic_metric_aggregator_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_candidate_result_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> candidate_result
        )
        set_tests_properties(
            sdurws_structureoptimizer_candidate_result_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_quick_screening_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> quick_screening
        )
        set_tests_properties(
            sdurws_structureoptimizer_quick_screening_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_cache_key_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> cache_key
        )
        set_tests_properties(
            sdurws_structureoptimizer_cache_key_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_initial_sampler_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> initial_sampler
        )
        set_tests_properties(
            sdurws_structureoptimizer_initial_sampler_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_canonical_baseline_bridge_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> canonical_baseline_bridge
        )
        set_tests_properties(
            sdurws_structureoptimizer_canonical_baseline_bridge_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
                ENVIRONMENT "QT_QPA_PLATFORM=${STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM}"
        )
        set(ROBOT_FILE_300KG_URDF
            "${CMAKE_SOURCE_DIR}/RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/300kg_urdf/output/300kg.urdf")
        if(EXISTS "${ROBOT_FILE_300KG_URDF}")
            add_test(NAME sdurws_structureoptimizer_robot_file_acceptance_test
                     COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> robot_file_acceptance)
            set_tests_properties(sdurws_structureoptimizer_robot_file_acceptance_test
                                 PROPERTIES
                                     WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
                                     ENVIRONMENT "QT_QPA_PLATFORM=${STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM}")
        endif()
    endif()
endif()
