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
        EliteSelector.cpp
        HybridOptimizer.cpp
        LocalSearch.cpp
        FinalValidationPlan.cpp
        IndependentFinalVerifier.cpp
        CandidateEvaluationScheduler.cpp
        OptimizationRunStateMachine.cpp
        OptimizationCheckpoint.cpp
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
        StructureOptimizationMigration.cpp
        OptimizationRunSnapshot.cpp
        OptimizationRunJson.cpp
        OptimizationRunStore.cpp
        StructureOptimizationWorkflowResolver.cpp
        OptimizationPreflight.cpp
        Phase8Acceptance.cpp
        Phase8PerformanceAudit.cpp
        Phase8ResourceAudit.cpp
        Phase8ReleaseManifest.cpp
        StructureOptimizationDocument.cpp
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
        EliteSelector.hpp
        HybridOptimizer.hpp
        LocalSearch.hpp
        FinalValidationPlan.hpp
        IndependentFinalVerifier.hpp
        CandidateEvaluationScheduler.hpp
        OptimizationRunStateMachine.hpp
        OptimizationCheckpoint.hpp
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
        StructureOptimizationMigration.hpp
        OptimizationRunSnapshot.hpp
        OptimizationRunJson.hpp
        OptimizationRunStore.hpp
        StructureOptimizationWorkflowResolver.hpp
        OptimizationPreflight.hpp
        Phase8Acceptance.hpp
        Phase8PerformanceAudit.hpp
        Phase8ResourceAudit.hpp
        Phase8ReleaseManifest.hpp
        StructureOptimizationDocument.hpp
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
        add_test(
            NAME sdurws_structureoptimizer_elite_selector_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> elite_selector
        )
        set_tests_properties(
            sdurws_structureoptimizer_elite_selector_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_hybrid_optimizer_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> hybrid_optimizer
        )
        set_tests_properties(
            sdurws_structureoptimizer_hybrid_optimizer_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_local_search_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> local_search
        )
        set_tests_properties(
            sdurws_structureoptimizer_local_search_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_independent_final_verifier_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> independent_final_verifier
        )
        set_tests_properties(
            sdurws_structureoptimizer_independent_final_verifier_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_candidate_evaluation_scheduler_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> candidate_evaluation_scheduler
        )
        add_test(
            NAME sdurws_structureoptimizer_optimization_checkpoint_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> optimization_checkpoint
        )
        set_tests_properties(
            sdurws_structureoptimizer_optimization_checkpoint_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_controller_state_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> controller_state
        )
        set_tests_properties(
            sdurws_structureoptimizer_controller_state_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_current_json_envelope_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> current_json_envelope
        )
        set_tests_properties(
            sdurws_structureoptimizer_current_json_envelope_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        add_test(
            NAME sdurws_structureoptimizer_legacy_json_migration_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> legacy_json_migration
        )
        add_test(
            NAME sdurws_structureoptimizer_run_snapshot_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> run_snapshot
        )
        add_test(
            NAME sdurws_structureoptimizer_run_store_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> run_store
        )
        add_test(
            NAME sdurws_structureoptimizer_workflow_resolver_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> workflow_resolver
        )
        add_test(
            NAME sdurws_structureoptimizer_preflight_core_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> preflight_core
        )
        add_test(
            NAME sdurws_structureoptimizer_phase6_integration_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> phase6_integration
        )
        # Phase 8 发布门：四个独立审计加一个聚合门，均使用同一绝对目标文件。
        add_test(
            NAME sdurws_structureoptimizer_phase8_acceptance_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> phase8_acceptance
        )
        add_test(
            NAME sdurws_structureoptimizer_phase8_performance_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> phase8_performance
        )
        add_test(
            NAME sdurws_structureoptimizer_phase8_resource_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> phase8_resource
        )
        add_test(
            NAME sdurws_structureoptimizer_phase8_manifest_test
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> phase8_manifest
        )
        add_test(
            NAME sdurws_structureoptimizer_phase8_release_gate
            COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> phase8_release_gate
        )
        set_tests_properties(
            sdurws_structureoptimizer_run_snapshot_test
            sdurws_structureoptimizer_run_store_test
            sdurws_structureoptimizer_workflow_resolver_test
            sdurws_structureoptimizer_preflight_core_test
            sdurws_structureoptimizer_phase6_integration_test
            sdurws_structureoptimizer_phase8_acceptance_test
            sdurws_structureoptimizer_phase8_performance_test
            sdurws_structureoptimizer_phase8_resource_test
            sdurws_structureoptimizer_phase8_manifest_test
            sdurws_structureoptimizer_phase8_release_gate
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
                ENVIRONMENT "QT_QPA_PLATFORM=${STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM}"
        )
        set_tests_properties(
            sdurws_structureoptimizer_legacy_json_migration_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
        )
        set_tests_properties(
            sdurws_structureoptimizer_candidate_evaluation_scheduler_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
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

        # Phase 7 每个 UI/报告门使用独立进程，Windows 下统一明确使用 windows 平台插件。
        add_test(NAME sdurws_structureoptimizer_phase7_variable_table_test
                 COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> variable_table)
        add_test(NAME sdurws_structureoptimizer_phase7_variable_actions_test
                 COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> variable_actions)
        add_test(NAME sdurws_structureoptimizer_phase7_controller_state_test
                 COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> controller_state)
        add_test(NAME sdurws_structureoptimizer_phase7_candidate_comparison_test
                 COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> phase1_core)
        add_test(NAME sdurws_structureoptimizer_phase7_preview_test
                 COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> preview)
        add_test(NAME sdurws_structureoptimizer_phase7_report_test
                 COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> report)
        add_test(NAME sdurws_structureoptimizer_phase7_gui_integration_test
                 COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> widget)
        set_tests_properties(
            sdurws_structureoptimizer_phase7_variable_table_test
            sdurws_structureoptimizer_phase7_variable_actions_test
            sdurws_structureoptimizer_phase7_controller_state_test
            sdurws_structureoptimizer_phase7_candidate_comparison_test
            sdurws_structureoptimizer_phase7_preview_test
            sdurws_structureoptimizer_phase7_report_test
            sdurws_structureoptimizer_phase7_gui_integration_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_structureoptimizer_test>
                ENVIRONMENT "QT_QPA_PLATFORM=${STRUCTUREOPTIMIZER_TEST_QPA_PLATFORM}"
        )
    endif()
endif()
