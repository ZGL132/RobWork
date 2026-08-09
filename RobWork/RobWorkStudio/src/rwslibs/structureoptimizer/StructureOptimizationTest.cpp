#include "StructureOptimizationTypes.hpp"
#include "StructureOptimizationValidation.hpp"
#include "StructureDesignMutator.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureCandidateGenerator.hpp"
#include "StructureCandidateCache.hpp"
#include "CandidateModelFactory.hpp"
#include "StructureCandidateEvaluator.hpp"
#include "EngineeringEvaluatorPipeline.hpp"
#include "KinematicEngineeringEvaluator.hpp"
#include "SystemEngineeringOptimizer.hpp"
#include "HybridStructureOptimizer.hpp"
#include "StructureOptimizationStrategy.hpp"
#include "StructureSensitivityAnalyzer.hpp"
#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationCsv.hpp"
#include "StructureVariableTableModel.hpp"
#include "OptimizationTaskTableModel.hpp"
#include "StructureCandidateTableModel.hpp"
#include "StructureOptimizationUiLogic.hpp"
#include "StructureOptimizerPlugin.hpp"
#include "StructureOptimizerWidget.hpp"
#include "StructureOptimizationController.hpp"
#include "StructureOptimizationObjectiveProfile.hpp"
#include "StructureConstraintTableModel.hpp"
#include "StructureOptimizationProjectAdapter.hpp"
#include "StructureOptimizationProjectFactory.hpp"
#include "RobotModelStalenessChecker.hpp"
#include "StructureWorkspaceCoverage.hpp"
#include "StructureOptimizationExportService.hpp"
#include "StructureOptimizationReportWriter.hpp"
#include "StructureCandidateExporter.hpp"
#include "CandidatePreviewController.hpp"
#include "EngineeringRequirementArtifactAdapter.hpp"
#include "FrozenRequirementProjectImportService.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>
#include <rwslibs/engineeringrequirements/RequirementSetJson.hpp>
// 用于构造与校验 v4 工件的执行契约(execution)与执行指纹。
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>

#include <rwslibs/kinematicanalysis/FrozenRequirementKinematicAdapter.hpp>
#include <rwslibs/kinematicanalysis/ConfigurationEvaluator.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisContext.hpp>
#include <rwslibs/kinematicanalysis/RegionCoverageEvaluator.hpp>
#include <rwslibs/kinematicanalysis/TargetEvaluator.hpp>

#include <rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelPublishService.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelProjectPaths.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>
#include <rwslibs/robotmodelbuilder/WorkCellConverter.hpp>

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/ProjectManager.hpp>
#include <rws/RobWorkStudio.hpp>

#include <rw/loaders/WorkCellLoader.hpp>

#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/kinematics/MovableFrame.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/math/RPY.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTableView>
#include <QTabWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// =============================================================================
//  最小化测试框架 (无依赖)
// =============================================================================

static int g_testFailures = 0;

static void require(bool condition, const char* file, int line, const char* expr)
{
    if (!condition)
    {
        std::fprintf(stderr, "  FAIL at %s:%d: %s\n", file, line, expr);
        ++g_testFailures;
    }
}

#define REQUIRE(cond) require((cond), __FILE__, __LINE__, #cond)

// 子套件 ABI:验证结构优化域的几个历史公开入口仍按既有函数指针类型可链接——
// resolveExternalAssetPaths / loadProject / createProblem / check。
// 防止重构(改签名或改名)后旧插件或既有调用方二进制链接失败。
static void testHistoricalStructureOptimizerAbiRemainsLinkable()
{
    using ResolveExternalAssetPaths = void (*)(rws::RobotModelSpec&);
    using LoadProject = bool (*)(const QString&, rws::StructureOptimizationProblem&, int*,
                                 QString*);
    using CreateProblem = bool (*)(const QString&, const rw::models::WorkCell&,
                                   const rw::kinematics::State&,
                                   rws::StructureOptimizationProblem&,
                                   rws::FrozenRequirementValidationResult*, std::string*);
    using CheckStaleness = rws::RobotModelStalenessResult (*)(
        const rws::RobotDesignContext&, const QString&);

    const ResolveExternalAssetPaths resolveExternalAssetPaths =
        static_cast<ResolveExternalAssetPaths>(
            &rws::CandidateModelFactory::resolveExternalAssetPaths);
    const LoadProject loadProject =
        static_cast<LoadProject>(&rws::StructureOptimizationProjectAdapter::loadProject);
    const CreateProblem createProblem = static_cast<CreateProblem>(
        &rws::FrozenRequirementProjectImportService::createProblem);
    const CheckStaleness checkStaleness = &rws::RobotModelStalenessChecker::check;

    REQUIRE(resolveExternalAssetPaths != nullptr);
    REQUIRE(loadProject != nullptr);
    REQUIRE(createProblem != nullptr);
    REQUIRE(checkStaleness != nullptr);
}

static QString sourcePath(const QString& relativePath)
{
    return QDir(QStringLiteral(STRUCTUREOPTIMIZER_TEST_SOURCE_DIR)).filePath(relativePath);
}

// =============================================================================
//  测试用例: 验证默认值和验证逻辑
// =============================================================================

static void testProblemDefaultsAndValidation()
{
    std::printf("testProblemDefaultsAndValidation ... ");

    // 用默认构造的问题
    rws::StructureOptimizationProblem problem;

    // ── 验证默认值 ──────────────────────────────────────────────────────
    REQUIRE(problem.weights.reachability == 0.35);
    REQUIRE(problem.weights.manipulability == 0.20);
    REQUIRE(problem.weights.jointMargin == 0.15);
    REQUIRE(problem.weights.collision == 0.15);
    REQUIRE(problem.weights.compactness == 0.10);
    REQUIRE(problem.weights.preference == 0.05);

    REQUIRE(problem.run.candidateCount == 300);
    REQUIRE(problem.run.eliteCount == 20);
    REQUIRE(problem.run.localEliteCount == 5);

    const std::vector< rws::ObjectiveTerm > legacyObjectives =
        rws::StructureOptimizationObjectiveProfile::legacyObjectives(problem.weights);
    REQUIRE(legacyObjectives.size() == 6);
    REQUIRE(legacyObjectives[0].metricId == "kinematics.reachability.weighted");
    REQUIRE(std::abs(legacyObjectives[0].weight - 0.35) < 1e-12);
    REQUIRE(problem.run.finalVerificationCount == 3);
    REQUIRE(problem.run.maxLocalSweeps == 20);
    REQUIRE(problem.run.gridSteps == 3);
    REQUIRE(problem.run.randomSeed == 1u);

    REQUIRE(problem.evaluation.checkCollision == true);

    // ── 运行验证 (空问题应该产生至少一个警告) ──────────────────────────
    std::vector< rws::AnalysisWarning > warnings =
        rws::StructureOptimizationValidation::validateProblem(problem);

    REQUIRE(!warnings.empty());

    // 确认至少有一个 Context.Invalid 警告
    bool foundContextInvalid = false;
    for (const auto& w : warnings)
    {
        if (w.code == "StructureOptimization.Context.Invalid")
        {
            foundContextInvalid = true;
            break;
        }
    }
    REQUIRE(foundContextInvalid);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 设计变量突变器
// =============================================================================

static void testMutator()
{
    std::printf("testMutator ... ");

    // ── Create a model with enough structure for kinematics sync ─────────
    rws::RobotModelSpec spec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    spec.robotName = "TestRobot";
    // 默认模型的第一个旋转关节是 "Joint1", pos[2] = 0.3

    // ── Create a JointPositionZ variable targeting Joint1 ────────────────
    const double j1z = spec.transformJoints[0].pos[2];  // save for baseline check
    rws::StructureDesignVariable var;
    var.id          = "z1";
    var.targetName  = spec.transformJoints[0].name;  // first joint (typically "Base")
    var.kind        = rws::StructureVariableKind::JointPositionZ;
    var.minimum     = -1.0;
    var.maximum     =  1.0;
    var.enabled     = true;

    std::vector< rws::StructureDesignVariable > vars   = {var};
    std::vector< double >                       values = {0.5};

    // ── Apply ────────────────────────────────────────────────────────────
    rws::StructureMutationResult result =
        rws::StructureDesignMutator::apply(spec, vars, values);

    if (!result.ok) {
        std::printf("\n  Mutator apply failed, warnings:");
        for (const auto& w : result.warnings)
            std::printf(" [%s] %s", w.code.c_str(), w.message.c_str());
        std::printf("\n");
    }
    REQUIRE(result.ok);
    REQUIRE(result.spec.transformJoints[0].pos[2] == 0.5);

    // ── Verify baseline was NOT modified ─────────────────────────────────
    REQUIRE(spec.transformJoints[0].pos[2] == j1z);

    // ── Test with missing target ─────────────────────────────────────────
    rws::StructureDesignVariable badVar;
    badVar.id          = "bad";
    badVar.targetName  = "NonExistent";
    badVar.kind        = rws::StructureVariableKind::JointPositionZ;
    badVar.minimum     = -1.0;
    badVar.maximum     =  1.0;
    badVar.enabled     = true;

    auto badResult =
        rws::StructureDesignMutator::apply(spec, {badVar}, {0.5});

    // Missing target warns but doesn't set result.ok = false
    bool foundMissing = false;
    for (const auto& w : badResult.warnings) {
        if (w.code == "StructureOptimization.Variable.MissingTarget") {
            foundMissing = true;
            break;
        }
    }
    REQUIRE(foundMissing);

    // ── Test value out of bounds ─────────────────────────────────────────
    auto outResult =
        rws::StructureDesignMutator::apply(spec, vars, {5.0});
    REQUIRE(!outResult.ok);

    // ── Test mismatched count ────────────────────────────────────────────
    auto cntResult =
        rws::StructureDesignMutator::apply(spec, vars, {});
    REQUIRE(!cntResult.ok);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 目标评分器
// =============================================================================

static void testScorer()
{
    std::printf("testScorer ... ");

    rws::StructureOptimizationProblem problem;
    rws::StructureObjectiveScorer scorer;

    // ── Infeasible candidate (required reachable < required) ─────────────
    rws::StructureCandidateResult infeasible;
    infeasible.index = 0;
    infeasible.raw.modelValid             = true;
    infeasible.raw.requiredReachableCount = 3;
    infeasible.raw.requiredTaskCount      = 5;
    infeasible.raw.collisionFreeRate      = 1.0;

    // ── Feasible candidate ───────────────────────────────────────────────
    rws::StructureCandidateResult feasible;
    feasible.index = 1;
    feasible.raw.modelValid             = true;
    feasible.raw.requiredReachableCount = 5;
    feasible.raw.requiredTaskCount      = 5;
    feasible.raw.collisionFreeRate      = 1.0;
    feasible.raw.jointMarginP10         = 0.15;
    feasible.raw.manipulabilityP10      = 0.05;
    feasible.raw.totalKinematicLength   = 0.5;
    feasible.raw.engineeringPreference  = 0.8;

    scorer.score(problem, infeasible);
    scorer.score(problem, feasible);

    // With no constraints defined, no hard constraints are checked.
    // The scorer always sets feasible = true initially, then loops over
    // constraints. With an empty list, both remain feasible.
    REQUIRE(infeasible.feasible);
    REQUIRE(feasible.feasible);
    REQUIRE(feasible.totalScore >= 0.0);
    REQUIRE(feasible.totalScore <= 100.0);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 带约束的评分:在问题里加一条硬约束 RequiredTaskReachable 后,
// 可达数不足的候选被判 infeasible,可达数足够的候选保持 feasible,
// 且 totalScore 保持在 [0,100] 区间;排序 sortForDecision 保证可行候选排前。
static void testScorerWithConstraints()
{
    std::printf("testScorerWithConstraints ... ");

    rws::StructureOptimizationProblem problem;
    rws::StructureObjectiveScorer scorer;

    // Add RequiredTaskReachable constraint
    rws::StructureConstraint reachCon;
    reachCon.id       = "ReachableReq";
    reachCon.kind     = rws::StructureConstraintKind::RequiredTaskReachable;
    reachCon.hard     = true;
    reachCon.enabled  = true;
    problem.constraints.push_back(reachCon);

    // ── Infeasible candidate ─────────────────────────────────────────────
    rws::StructureCandidateResult infeasible;
    infeasible.index = 0;
    infeasible.raw.modelValid             = true;
    infeasible.raw.requiredReachableCount = 3;
    infeasible.raw.requiredTaskCount      = 5;
    infeasible.raw.collisionFreeRate      = 1.0;

    // ── Feasible candidate ───────────────────────────────────────────────
    rws::StructureCandidateResult feasible;
    feasible.index = 1;
    feasible.raw.modelValid             = true;
    feasible.raw.requiredReachableCount = 5;
    feasible.raw.requiredTaskCount      = 5;
    feasible.raw.collisionFreeRate      = 1.0;
    feasible.raw.jointMarginP10         = 0.15;
    feasible.raw.manipulabilityP10      = 0.05;
    feasible.raw.totalKinematicLength   = 0.5;
    feasible.raw.engineeringPreference  = 0.8;

    scorer.score(problem, infeasible);
    scorer.score(problem, feasible);

    REQUIRE(!infeasible.feasible);
    REQUIRE(feasible.feasible);
    REQUIRE(feasible.totalScore >= 0.0);
    REQUIRE(feasible.totalScore <= 100.0);

    // ── Sort and verify feasible ranks first ─────────────────────────────
    std::vector< rws::StructureCandidateResult > candidates = {infeasible, feasible};
    rws::StructureObjectiveScorer::sortForDecision(candidates);

    REQUIRE(candidates[0].feasible == true);
    REQUIRE(candidates[0].index == 1); // feasible had index 1

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 硬约束检查
// =============================================================================

static void testGenericObjectivesAndConstraints()
{
    std::printf("testGenericObjectivesAndConstraints ... ");

    rws::StructureOptimizationProblem problem;
    problem.objectives = {
        {"structure.preference", rws::OptimizationDirection::Maximize,
         {1.0, 0.0, true}, 1.0, true}
    };
    problem.metricConstraints = {
        {"collision.free_rate", rws::ComparisonOperator::GreaterThanOrEqual,
         0.9, true, true}
    };

    rws::StructureCandidateResult candidate;
    candidate.raw.engineeringPreference = 0.8;
    candidate.raw.collisionFreeRate = 0.8;

    rws::StructureObjectiveScorer scorer;
    scorer.score(problem, candidate);

    REQUIRE(std::abs(candidate.totalScore - 80.0) < 1e-12);
    REQUIRE(!candidate.feasible);
    REQUIRE(candidate.violatedConstraints.size() == 1);
    REQUIRE(candidate.violatedConstraints[0] == "collision.free_rate");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 硬约束:注册八类硬约束(ModelValid / RequiredTaskReachable /
// MinimumJointMargin / MaximumTotalLength / MaximumBaseHeight /
// MinimumWorkspaceCoverage / MaximumCrossSection / MaximumLinkSlenderness),
// 用一个逐项违反的候选验证:被判 infeasible,且 violatedConstraints 完整记录
// 全部八个约束 id,不吞掉任何一项(覆盖的完备性检查)。
static void testHardConstraints()
{
    std::printf("testHardConstraints ... ");

    rws::StructureOptimizationProblem problem;
    rws::StructureObjectiveScorer scorer;

    // Add one constraint of each kind that we can violate
    {
        rws::StructureConstraint c;
        c.id = "ModelValid"; c.kind = rws::StructureConstraintKind::ModelValid;
        c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "RequiredReachable"; c.kind = rws::StructureConstraintKind::RequiredTaskReachable;
        c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MinMargin"; c.kind = rws::StructureConstraintKind::MinimumJointMargin;
        c.threshold = 0.05; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MaxLength"; c.kind = rws::StructureConstraintKind::MaximumTotalLength;
        c.threshold = 2.0; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MaxHeight"; c.kind = rws::StructureConstraintKind::MaximumBaseHeight;
        c.threshold = 1.0; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MinCoverage"; c.kind = rws::StructureConstraintKind::MinimumWorkspaceCoverage;
        c.threshold = 0.5; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MaxCross"; c.kind = rws::StructureConstraintKind::MaximumCrossSection;
        c.threshold = 0.1; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }
    {
        rws::StructureConstraint c;
        c.id = "MaxSlender"; c.kind = rws::StructureConstraintKind::MaximumLinkSlenderness;
        c.threshold = 20.0; c.hard = true; c.enabled = true;
        problem.constraints.push_back(c);
    }

    // Candidate that violates all constraints
    rws::StructureCandidateResult candidate;
    candidate.raw.modelValid             = false;  // violates ModelValid
    candidate.raw.requiredReachableCount = 0;      // violates RequiredReachable
    candidate.raw.requiredTaskCount      = 5;
    candidate.raw.minimumJointMargin     = 0.01;   // violates MinMargin (< 0.05)
    candidate.raw.totalKinematicLength   = 3.0;    // violates MaxLength (> 2.0)
    candidate.raw.baseHeight             = 2.0;    // violates MaxHeight (> 1.0)
    candidate.raw.workspaceCoverage      = 0.1;    // violates MinCoverage (< 0.5)
    candidate.raw.maxCrossSection        = 0.5;    // violates MaxCross (> 0.1)
    candidate.raw.maxLinkSlenderness     = 50.0;   // violates MaxSlender (> 20.0)
    candidate.raw.collisionFreeRate      = 0.0;

    scorer.score(problem, candidate);

    REQUIRE(!candidate.feasible);
    REQUIRE(candidate.violatedConstraints.size() == 8);

    // Verify specific constraint IDs are present
    bool foundModelValid = false;
    bool foundReachable  = false;
    for (const auto& id : candidate.violatedConstraints) {
        if (id == "ModelValid")        foundModelValid = true;
        if (id == "RequiredReachable") foundReachable  = true;
    }
    REQUIRE(foundModelValid);
    REQUIRE(foundReachable);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 候选解生成器
// =============================================================================

static void testGenerator()
{
    std::printf("testGenerator ... ");

    // ── 两个设计变量 ─────────────────────────────────────────────────
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"var1", "Var 1", "joint1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.0, -10.0, 10.0, 0.5},
        {"var2", "Var 2", "joint2", "mm",
         rws::StructureVariableKind::JointPositionY,
         0.0,  -5.0,  5.0, 0.25}
    };

    // ── randomUniform: 10 个候选, seed=42 ───────────────────────────
    auto candidates1 = rws::StructureCandidateGenerator::randomUniform(
        problem.variables, 10, 42);
    REQUIRE(candidates1.size() == 10);
    REQUIRE(candidates1[0].size() == 2);

    // ── 相同 seed 应得到相同结果 ─────────────────────────────────────
    auto candidates2 = rws::StructureCandidateGenerator::randomUniform(
        problem.variables, 10, 42);
    REQUIRE(candidates2.size() == 10);
    bool same = true;
    for (int i = 0; i < 10 && same; ++i)
    {
        for (std::size_t j = 0; j < 2 && same; ++j)
        {
            if (std::abs(candidates1[i][j] - candidates2[i][j]) > 1e-12)
                same = false;
        }
    }
    REQUIRE(same);

    // ── latinHypercube: 10 个候选 ───────────────────────────────────
    auto lhs = rws::StructureCandidateGenerator::latinHypercube(
        problem.variables, 10, 42);
    REQUIRE(lhs.size() == 10);
    REQUIRE(lhs[0].size() == 2);

    // ── quantize ─────────────────────────────────────────────────────
    {
        rws::StructureDesignVariable v;
        v.minimum = 0.0;
        v.maximum = 10.0;
        v.step    = 3.0;

        // round(3.2/3.0)*3 = 1*3 = 3.0
        double q = rws::StructureCandidateGenerator::quantize(3.2, v);
        REQUIRE(std::abs(q - 3.0) < 1e-12);

        // round(5.5/3.0)*3 = 2*3 = 6.0
        q = rws::StructureCandidateGenerator::quantize(5.5, v);
        REQUIRE(std::abs(q - 6.0) < 1e-12);

        // round(11.0/3.0)*3 = 12, clamped to 10.0
        q = rws::StructureCandidateGenerator::quantize(11.0, v);
        REQUIRE(std::abs(q - 10.0) < 1e-12);
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 候选解缓存
// =============================================================================

static void testCache()
{
    std::printf("testCache ... ");

    rws::StructureCandidateCache cache;

    // ── 一个变量的问题 ───────────────────────────────────────────────
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"var1", "Var 1", "joint1", "mm",
         rws::StructureVariableKind::JointPositionX,
         5.0, 0.0, 10.0, 1.0}
    };

    // ── 存入一个结果 ──────────────────────────────────────────────────
    rws::StructureCandidateResult result;
    result.index      = 0;
    result.status     = rws::StructureCandidateStatus::Feasible;
    result.feasible   = true;
    result.totalScore = 0.85;
    problem.evaluation.checkCollision = true;

    std::vector<double> values = {5.0};
    cache.put(problem, values, rws::StructureEvaluationStage::Quick, result);
    REQUIRE(cache.size() == 1);

    // ── 查找应命中 ───────────────────────────────────────────────────
    rws::StructureCandidateResult found;
    bool foundResult = cache.find(problem, values,
                                  rws::StructureEvaluationStage::Quick, found);
    REQUIRE(foundResult);
    REQUIRE(found.feasible);
    REQUIRE(std::abs(found.totalScore - 0.85) < 1e-12);
    REQUIRE(cache.hitCount() == 1);

    // ── 改变 checkCollision → 应未命中 ──────────────────────────────
    problem.evaluation.checkCollision = false;
    foundResult = cache.find(problem, values,
                             rws::StructureEvaluationStage::Quick, found);
    REQUIRE(!foundResult);
    REQUIRE(cache.hitCount() == 1);

    problem.evaluation.checkCollision = true;
    problem.context.modelSpec.robotName = "ChangedModel";
    foundResult = cache.find(problem, values,
                             rws::StructureEvaluationStage::Quick, found);
    REQUIRE(!foundResult);
    REQUIRE(cache.hitCount() == 1);

    // ── clear ────────────────────────────────────────────────────────
    // Workspace sampling and the coverage box change evaluation results.
    cache.clear();
    problem.context.modelSpec.robotName.clear();
    problem.evaluation.coverageBox.enabled = true;
    problem.evaluation.coverageBox.cells = {{2, 2, 2}};
    problem.evaluation.quickWorkspace.sampleCount = 8;
    cache.put(problem, values, rws::StructureEvaluationStage::Quick, result);
    REQUIRE(cache.find(problem, values, rws::StructureEvaluationStage::Quick, found));
    problem.evaluation.coverageBox.cells[0] = 3;
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Quick, found));
    problem.evaluation.coverageBox.cells[0] = 2;
    problem.evaluation.quickWorkspace.randomSeed = 2u;
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Quick, found));

    // 多区域覆盖率与冻结场景都会改变候选的实际评价环境，缓存绝不能把它们当作同一个
    // 问题复用。该断言保护 problemToJson() 参与缓存键的契约，防止后续精简序列化时
    // 意外遗漏新增区域或工装快照。
    cache.clear();
    problem.evaluation.quickWorkspace.randomSeed = 1u;
    problem.evaluation.coverageBoxes.clear();
    rws::WorkspaceCoverageBox regionalBox;
    regionalBox.id = "fixture_area";
    regionalBox.referenceFrame = "Fixture_A";
    regionalBox.enabled = true;
    regionalBox.cells = {{2, 2, 2}};
    problem.evaluation.coverageBoxes.push_back(regionalBox);
    problem.scenarioSnapshot.schemaVersion = 1;
    problem.scenarioSnapshot.snapshotFingerprint = "scenario-fingerprint-a";
    cache.put(problem, values, rws::StructureEvaluationStage::Quick, result);
    REQUIRE(cache.find(problem, values, rws::StructureEvaluationStage::Quick, found));
    problem.evaluation.coverageBoxes[0].cells[2] = 3;
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Quick, found));
    problem.evaluation.coverageBoxes[0].cells[2] = 2;
    problem.scenarioSnapshot.snapshotFingerprint = "scenario-fingerprint-b";
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Quick, found));

    cache.clear();
    problem.scenarioSnapshot.snapshotFingerprint = "scenario-fingerprint-a";
    rws::RequirementExecutionRegion cachedRegion;
    cachedRegion.id = "cached-verified-region";
    cachedRegion.refFrame = "WORLD";
    cachedRegion.minimumVerificationStage = rws::RequirementExecutionStage::Verified;
    problem.requirementExecution.provenance.requirementFingerprint =
        "cache-requirement-fingerprint";
    problem.requirementExecution.workspaceRegions.push_back(cachedRegion);
    cache.put(problem, values, rws::StructureEvaluationStage::Verified, result);
    REQUIRE(cache.find(problem, values, rws::StructureEvaluationStage::Verified, found));
    problem.requirementExecution.workspaceRegions.front().directionSamples = 2;
    REQUIRE(!cache.find(problem, values,
                        rws::StructureEvaluationStage::Verified, found));

    cache.clear();
    REQUIRE(cache.size() == 0);
    REQUIRE(cache.hitCount() == 0);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 候选模型工厂
// =============================================================================

static void testModelFactory()
{
    std::printf("testModelFactory ... ");

    // ── 使用默认六轴模型(确保 saveFiles 能正常工作) ─────────────────────
    rws::RobotModelSpec spec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    spec.robotName = "TestRobot";

    // ── 构建请求 ─────────────────────────────────────────────────────────
    rws::CandidateModelBuildRequest req;
    req.spec         = spec;
    req.deviceName   = "TestRobot";   // 与 robotName 一致
    req.checkCollision = false;       // 简化测试,不加载碰撞检测

    // ── 执行工厂 ─────────────────────────────────────────────────────────
    rws::CandidateModelFactory factory;
    rws::CandidateModelBuildResult result = factory.build(req);

    REQUIRE(result.ok);
    REQUIRE(!result.artifact.workcell.isNull());
    REQUIRE(!result.artifact.device.isNull());

    // TCP 帧应回退到 device->getEnd()
    REQUIRE(!result.artifact.tcpFrame.isNull());

    // 无碰撞检测时应为 null
    REQUIRE(result.artifact.collisionDetector.isNull());

    // 临时目录应有效
    REQUIRE(result.artifact.temporaryDirectory.get() != nullptr);
    REQUIRE(result.artifact.temporaryDirectory->isValid());

    // 冻结场景必须与变异后的机器人共同出现在候选 WorkCell。这里不通过提前换算
    // WORLD 位姿来规避工装，而是断言 Fixture_A 被真实写入候选场景，供后续 IK/
    // 碰撞评价按同一个 Frame 名称解析。
    rws::StructureOptimizationScenarioSnapshot scenario;
    scenario.schemaVersion = 1;
    scenario.snapshotFingerprint = "scenario-test-fingerprint";
    scenario.sceneSpec.robotName = "FrozenCell";
    rws::FrameSpec fixture;
    fixture.name = "Fixture_A";
    fixture.refFrame = "WORLD";
    fixture.pos = {{0.4, 0.0, 0.2}};
    scenario.sceneSpec.sceneFrames.push_back(fixture);
    QTemporaryDir clonedScenarioRoot;
    REQUIRE(clonedScenarioRoot.isValid());
    REQUIRE(QDir().mkpath(clonedScenarioRoot.filePath("assets")));
    const QString sourceMesh = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/geometry/base.stl");
    const QString clonedMesh = clonedScenarioRoot.filePath("assets/base.stl");
    REQUIRE(QFile::copy(sourceMesh, clonedMesh));
    scenario.sceneSpec.saveDirectory = "scenes";
    rws::SceneGeometrySpec fixtureMesh;
    fixtureMesh.name = "FixtureMesh";
    fixtureMesh.refFrame = "Fixture_A";
    fixtureMesh.kind = rws::GeometryKind::Polytope;
    fixtureMesh.file = "assets/base.stl";
    fixtureMesh.collisionModel = false;
    scenario.sceneSpec.sceneGeometries.push_back(fixtureMesh);
    req.scenarioSnapshot = &scenario;
    req.scenarioBaseDirectory = clonedScenarioRoot.path().toStdString();
    result = factory.build(req);
    REQUIRE(result.ok);
    REQUIRE(result.artifact.workcell->findFrame("Fixture_A") != nullptr);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  Fake evaluator for optimizer testing
// =============================================================================

// 二次型假评价器:得分 = 100 - 10 * Σ(变量值-偏好值)²,全部可行。
// 用于优化器/灵敏度测试——"越接近偏好值越好"的解析行为让断言可精确计算。
class QuadraticFakeEvaluator : public rws::IStructureCandidateEvaluator {
  public:
    void evaluate(
        const rws::StructureOptimizationProblem& problem,
        rws::StructureCandidateResult& candidate,
        rws::StructureEvaluationStage stage,
        const rws::StructureOptimizationCallbacks& callbacks,
        rws::StructureCandidateCache* cache) override
    {
        candidate.stage = stage;

        // Quadratic penalty: closer to preferred values is better
        double error = 0.0;
        for (std::size_t i = 0; i < candidate.values.size() && i < problem.variables.size(); ++i)
        {
            double diff = candidate.values[i] - problem.variables[i].preferredValue;
            error += diff * diff;
        }

        candidate.totalScore = std::max(0.0, 100.0 - error * 10.0);
        candidate.feasible   = true;
        candidate.status     = rws::StructureCandidateStatus::Feasible;

        // Populate minimal raw metrics
        candidate.raw.modelValid             = true;
        candidate.raw.requiredReachableCount = 5;
        candidate.raw.requiredTaskCount      = 5;
        candidate.raw.weightedReachability   = 1.0;
        candidate.raw.manipulabilityP10      = 0.01;
        candidate.raw.jointMarginP10         = 0.1;
        candidate.raw.collisionFreeRate      = 1.0;
        candidate.raw.totalKinematicLength   = 1.0;

        rws::StructureObjectiveScorer scorer;
        scorer.score(problem, candidate);
    }
};

// 全不可行假评价器:任何候选都判 Infeasible、得分 0。
// 专用于验证"无可行解"场景下优化器与灵敏度的兜底行为。
class AlwaysInfeasibleEvaluator : public rws::IStructureCandidateEvaluator {
  public:
    void evaluate(
        const rws::StructureOptimizationProblem&,
        rws::StructureCandidateResult& candidate,
        rws::StructureEvaluationStage stage,
        const rws::StructureOptimizationCallbacks&,
        rws::StructureCandidateCache*) override
    {
        candidate.stage = stage;
        candidate.feasible = false;
        candidate.totalScore = 0.0;
        candidate.status = rws::StructureCandidateStatus::Infeasible;
    }
};

// 流水线工件生产者:记录执行顺序,产出 ik.solutions 工件与可达性指标。
// 用于验证 EngineeringEvaluatorPipeline 能按工件依赖自动拓扑排序。
class PipelineArtifactProducer : public rws::IEngineeringEvaluator {
  public:
    explicit PipelineArtifactProducer(std::vector<std::string>* executionOrder)
        : _executionOrder(executionOrder)
    {}

    std::string id() const override { return "test.producer"; }
    std::string version() const override { return "1"; }
    std::vector<std::string> providedArtifactIds() const override
    {
        return {"ik.solutions"};
    }

    rws::EngineeringEvaluationResult evaluate(
        const rws::CandidateEvaluationContext& candidate,
        const rws::EvaluationRequest&,
        const rws::EvaluationCallbacks&) override
    {
        _executionOrder->push_back(id());
        rws::EngineeringEvaluationResult result;
        result.providerId = id();
        result.providerVersion = version();
        result.status = rws::EngineeringEvaluationStatus::Success;
        result.inputSnapshot = candidate.inputSnapshot;
        result.artifacts.push_back({"ik.solutions", "application/json", "[]"});
        result.metrics.push_back({"kinematics.reachability.weighted", 1.0, "ratio",
                                  rws::EngineeringMetricStatus::Valid, id()});
        return result;
    }

  private:
    std::vector<std::string>* _executionOrder;
};

// 流水线工件消费者:记录执行顺序,要求 ik.solutions 工件——请求中缺失该工件时
// 返回 DataInsufficient;具备时产出轨迹可行性约束。
class PipelineArtifactConsumer : public rws::IEngineeringEvaluator {
  public:
    explicit PipelineArtifactConsumer(std::vector<std::string>* executionOrder)
        : _executionOrder(executionOrder)
    {}

    std::string id() const override { return "test.consumer"; }
    std::string version() const override { return "1"; }
    std::vector<std::string> requiredArtifactIds() const override
    {
        return {"ik.solutions"};
    }

    rws::EngineeringEvaluationResult evaluate(
        const rws::CandidateEvaluationContext& candidate,
        const rws::EvaluationRequest& request,
        const rws::EvaluationCallbacks&) override
    {
        _executionOrder->push_back(id());
        rws::EngineeringEvaluationResult result;
        result.providerId = id();
        result.providerVersion = version();
        result.inputSnapshot = candidate.inputSnapshot;
        result.status = rws::EngineeringEvaluationStatus::Success;
        const bool hasIkArtifact = std::any_of(
            request.inputArtifacts.begin(), request.inputArtifacts.end(),
            [](const rws::EngineeringArtifact& artifact) {
                return artifact.artifactId == "ik.solutions";
            });
        if (!hasIkArtifact) {
            result.status = rws::EngineeringEvaluationStatus::DataInsufficient;
            return result;
        }
        result.constraints.push_back({"trajectory.feasible", "==", 1.0, 1.0,
                                      true, true, ""});
        return result;
    }

  private:
    std::vector<std::string>* _executionOrder;
};

// 子套件 工程评价流水线:验证 EngineeringEvaluatorPipeline 按工件依赖自动
// 拓扑排序——即使先 addEvaluator(consumer) 再 addEvaluator(producer),执行时
// producer 必须先运行产出 ik.solutions,consumer 才能消费(顺序为 producer→consumer);
// 只挂 consumer 而缺少提供者时返回 DataInsufficient 且不执行任何环节。
static void testEngineeringEvaluatorPipeline()
{
    std::printf("testEngineeringEvaluatorPipeline ... ");

    std::vector<std::string> order;
    PipelineArtifactProducer producer(&order);
    PipelineArtifactConsumer consumer(&order);
    rws::EngineeringEvaluatorPipeline pipeline;
    pipeline.addEvaluator(consumer);
    pipeline.addEvaluator(producer);

    rws::CandidateEvaluationContext candidate;
    candidate.inputSnapshot.modelHash = "model";
    rws::EngineeringEvaluationResult result = pipeline.evaluate(
        candidate, rws::EvaluationRequest(), rws::EvaluationCallbacks());

    REQUIRE(result.status == rws::EngineeringEvaluationStatus::Success);
    REQUIRE(order.size() == 2);
    REQUIRE(order[0] == "test.producer");
    REQUIRE(order[1] == "test.consumer");
    REQUIRE(result.artifacts.size() == 1);
    REQUIRE(result.constraints.size() == 1);

    rws::EngineeringEvaluatorPipeline incompletePipeline;
    incompletePipeline.addEvaluator(consumer);
    order.clear();
    result = incompletePipeline.evaluate(
        candidate, rws::EvaluationRequest(), rws::EvaluationCallbacks());
    REQUIRE(result.status == rws::EngineeringEvaluationStatus::DataInsufficient);
    REQUIRE(order.empty());

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 系统指标假评价器:完整输出运动学/碰撞/几何/偏好十二项工程指标,
// 并记录收到的 modelHash 以便断言优化器喂入的模型快照正确。
class SystemMetricsEvaluator : public rws::IEngineeringEvaluator {
  public:
    std::string lastModelHash;

    std::string id() const override { return "test.system-metrics"; }
    std::string version() const override { return "1"; }

    rws::EngineeringEvaluationResult evaluate(
        const rws::CandidateEvaluationContext& candidate,
        const rws::EvaluationRequest&,
        const rws::EvaluationCallbacks&) override
    {
        lastModelHash = candidate.inputSnapshot.modelHash;
        rws::EngineeringEvaluationResult result;
        result.providerId = id();
        result.providerVersion = version();
        result.status = rws::EngineeringEvaluationStatus::Success;
        result.inputSnapshot = candidate.inputSnapshot;
        const std::vector<rws::EngineeringMetric> metrics = {
            {"kinematics.reachability.weighted", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"kinematics.manipulability.p10", 0.01, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"kinematics.joint_margin.p10", 0.2, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"kinematics.joint_margin.minimum", 0.2, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"kinematics.workspace.coverage", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"collision.free_rate", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.compactness", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.kinematic_length", 0.8, "m", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.base_height", 0.2, "m", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.cross_section.maximum", 0.01, "m2", rws::EngineeringMetricStatus::Valid, id()},
            {"geometry.link_slenderness.maximum", 5.0, "ratio", rws::EngineeringMetricStatus::Valid, id()},
            {"structure.preference", 1.0, "ratio", rws::EngineeringMetricStatus::Valid, id()}};
        result.metrics = metrics;
        return result;
    }
};

// 子套件 系统级优化器:用真实指标评价器(SystemMetricsEvaluator)驱动
// SystemEngineeringOptimizer,验证候选数(基线 + 3)、未被取消、存在最佳候选,
// 且评价器拿到的 modelHash 与 RobotModelFingerprint 计算的模型指纹一致——
// 证明优化器把正确的模型快照喂给了工程评价环节。
static void testSystemEngineeringOptimizer()
{
    std::printf("testSystemEngineeringOptimizer ... ");

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.modelSpec.robotName = "SystemHashRobot";
    problem.variables = {{"x", "X", "joint1", "m",
                          rws::StructureVariableKind::JointPositionX,
                          0.0, -1.0, 1.0, 0.1, 0.0, 0.0}};
    problem.run.strategy = rws::StructureStrategyKind::Random;
    problem.run.candidateCount = 3;
    problem.run.randomSeed = 8;

    SystemMetricsEvaluator evaluator;
    rws::EngineeringEvaluatorPipeline pipeline;
    pipeline.addEvaluator(evaluator);
    rws::SystemEngineeringOptimizer optimizer;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };
    const rws::StructureOptimizationResult result =
        optimizer.optimize(problem, pipeline, callbacks);

    REQUIRE(!result.canceled);
    REQUIRE(result.candidates.size() == 4);
    REQUIRE(result.bestCandidateIndex >= 0);
    REQUIRE(evaluator.lastModelHash ==
            rws::RobotModelFingerprint::canonicalSha256(problem.context.modelSpec));
    for (const rws::StructureCandidateResult& candidate : result.candidates) {
        REQUIRE(candidate.feasible);
        REQUIRE(candidate.status == rws::StructureCandidateStatus::Feasible);
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 候选解评估器 (接口验证)
// =============================================================================

static void testEvaluator()
{
    std::printf("testEvaluator ... ");

    // Minimal problem — no valid context, so the real evaluator's mutator
    // should fail quickly and return a Failed candidate.
    rws::StructureOptimizationProblem problem;
    rws::StructureCandidateEvaluator  evaluator;
    rws::StructureCandidateResult     candidate;

    candidate.index  = 0;
    candidate.values = {};

    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };

    evaluator.evaluate(problem, candidate, rws::StructureEvaluationStage::Quick,
                       callbacks, nullptr);

    // Without a valid model spec, the mutator returns ok=false → Failed
    REQUIRE(candidate.status == rws::StructureCandidateStatus::Failed);

    rws::KinematicEngineeringEvaluator engineeringEvaluator(problem);
    rws::CandidateEvaluationContext context;
    context.variableValues = candidate.values;
    const rws::EngineeringEvaluationResult engineeringResult =
        engineeringEvaluator.evaluate(context, rws::EvaluationRequest(),
                                      rws::EvaluationCallbacks());
    REQUIRE(engineeringResult.status == rws::EngineeringEvaluationStatus::Failed);
    REQUIRE(engineeringResult.providerId == "structure.kinematics");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 混合优化器 (使用 Fake 评估器)
// =============================================================================

static void testOptimizer()
{
    std::printf("testOptimizer ... ");

    // ── Problem: 2 design variables, Hybrid strategy ───────────────────
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "joint1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.1,
         0.3, 0.5},   // preferred = 0.3, weight = 0.5
        {"y", "Y", "joint2", "mm",
         rws::StructureVariableKind::JointPositionY,
         0.0, -1.0, 1.0, 0.1,
         -0.2, 0.5}   // preferred = -0.2, weight = 0.5
    };

    problem.run.strategy         = rws::StructureStrategyKind::Hybrid;
    problem.run.candidateCount   = 30;
    problem.run.eliteCount       = 5;
    problem.run.localEliteCount  = 3;
    problem.run.maxLocalSweeps   = 6;
    problem.run.randomSeed       = 42;

    // Add a RequiredTaskReachable constraint for feasibility testing
    rws::StructureConstraint reachCon;
    reachCon.id      = "Reachable";
    reachCon.kind    = rws::StructureConstraintKind::RequiredTaskReachable;
    reachCon.hard    = true;
    reachCon.enabled = true;
    problem.constraints.push_back(reachCon);

    // ── Run optimization ──────────────────────────────────────────────
    QuadraticFakeEvaluator                    fakeEval;
    rws::HybridStructureOptimizer             optimizer;
    rws::StructureOptimizationCallbacks  callbacks;
    callbacks.isCancellationRequested = []() { return false; };

    rws::StructureOptimizationResult result = optimizer.optimize(
        problem, fakeEval, callbacks);
    rws::StructureOptimizationResult repeated = optimizer.optimize(
        problem, fakeEval, callbacks);

    // ── Assertions ────────────────────────────────────────────────────
    REQUIRE(!result.canceled);
    REQUIRE(result.baselineCandidateIndex == 0);
    REQUIRE(!result.candidates.empty());
    REQUIRE(result.diagnostics.generatedCandidates > 0);
    REQUIRE(result.diagnostics.evaluatedCandidates > 0);
    REQUIRE(result.diagnostics.totalSeconds >= 0.0);

    // At least one feasible candidate should exist
    bool foundFeasible = false;
    for (const auto& c : result.candidates)
        if (c.feasible) { foundFeasible = true; break; }
    REQUIRE(foundFeasible);
    REQUIRE(result.candidates.size() == repeated.candidates.size());
    for (std::size_t i = 0; i < result.candidates.size() &&
                            i < repeated.candidates.size(); ++i) {
        REQUIRE(result.candidates[i].values == repeated.candidates[i].values);
        REQUIRE(result.candidates[i].feasible == repeated.candidates[i].feasible);
        REQUIRE(std::abs(result.candidates[i].totalScore -
                         repeated.candidates[i].totalScore) < 1e-12);
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 灵敏度分析器
// =============================================================================

//! 简化的模拟评估器: 越偏离 0 则得分越低, >0.8 则不可行。
// 子套件 混合优化工作流:驱动 HybridStructureOptimizer 完成 Quick→Local→
// FinalVerified 的完整过程,验证进度回调的 Local/FinalVerified 计划数、诊断计数
// (quickEvaluated=6, verifiedElite=4, finalVerified=1)以及灵敏度条目数,并保证
// 两次相同输入的优化结果(最佳候选、灵敏度条目与降分)完全确定、可复现。
static void testHybridVerificationAndSensitivityWorkflow()
{
    std::printf("testHybridVerificationAndSensitivityWorkflow ... ");

    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "joint1", "m", rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.1, 0.2, 0.5},
        {"y", "Y", "joint2", "m", rws::StructureVariableKind::JointPositionY,
         0.0, -1.0, 1.0, 0.1, -0.2, 0.5}
    };
    problem.run.strategy = rws::StructureStrategyKind::Hybrid;
    problem.run.candidateCount = 4;
    problem.run.eliteCount = 4;
    problem.run.localEliteCount = 2;
    problem.run.finalVerificationCount = 1;
    problem.run.maxLocalSweeps = 2;
    problem.run.randomSeed = 123u;

    std::vector<rws::StructureProgress> progress;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };
    callbacks.onProgress = [&progress](const rws::StructureProgress& value) {
        progress.push_back(value);
    };

    QuadraticFakeEvaluator evaluator;
    rws::HybridStructureOptimizer optimizer;
    const rws::StructureOptimizationResult result =
        optimizer.optimize(problem, evaluator, callbacks);
    QuadraticFakeEvaluator repeatedEvaluator;
    const rws::StructureOptimizationResult repeated =
        optimizer.optimize(problem, repeatedEvaluator, callbacks);

    int localPlanned = -1;
    int finalVerifiedCompleted = 0;
    int finalVerifiedPlanned = -1;
    for (const rws::StructureProgress& value : progress) {
        if (value.stage == "Local")
            localPlanned = value.planned;
        if (value.stage == "FinalVerified") {
            finalVerifiedCompleted = value.completed;
            finalVerifiedPlanned = value.planned;
        }
    }

    REQUIRE(!result.canceled);
    REQUIRE(localPlanned == 2);
    REQUIRE(finalVerifiedPlanned == 1);
    REQUIRE(finalVerifiedCompleted == 1);
    REQUIRE(result.diagnostics.quickEvaluatedCandidates == 6);
    REQUIRE(result.diagnostics.verifiedEliteCandidates == 4);
    REQUIRE(result.diagnostics.finalVerifiedCandidates == 1);
    REQUIRE(result.diagnostics.sensitivityEvaluations ==
            static_cast<int>(result.sensitivity.entries.size()));
    REQUIRE(result.bestCandidateIndex >= 0);
    REQUIRE(!result.sensitivity.entries.empty());
    REQUIRE(result.bestCandidateIndex == repeated.bestCandidateIndex);
    REQUIRE(result.sensitivity.entries.size() == repeated.sensitivity.entries.size());
    for (std::size_t i = 0; i < result.sensitivity.entries.size() &&
                            i < repeated.sensitivity.entries.size(); ++i) {
        REQUIRE(result.sensitivity.entries[i].variableId ==
                repeated.sensitivity.entries[i].variableId);
        REQUIRE(std::abs(result.sensitivity.entries[i].scoreDrop -
                         repeated.sensitivity.entries[i].scoreDrop) < 1e-12);
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 无可行解的灵敏度兜底:当评价器使所有候选都不可行时,优化结果不得
// 谎报最佳候选(bestCandidateIndex == -1),灵敏度等级必须为 Unknown,
// 并给出 StructureOptimization.NoFeasibleCandidate 告警,供 UI 展示原因。
static void testNoFeasibleCandidateLeavesSensitivityUnknown()
{
    std::printf("testNoFeasibleCandidateLeavesSensitivityUnknown ... ");

    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "joint1", "m", rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.1}
    };
    problem.run.strategy = rws::StructureStrategyKind::Hybrid;
    problem.run.candidateCount = 2;
    problem.run.eliteCount = 1;
    problem.run.localEliteCount = 1;
    problem.run.finalVerificationCount = 1;

    AlwaysInfeasibleEvaluator evaluator;
    rws::HybridStructureOptimizer optimizer;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };
    const rws::StructureOptimizationResult result =
        optimizer.optimize(problem, evaluator, callbacks);

    const bool hasNoFeasibleWarning = std::any_of(
        result.warnings.begin(), result.warnings.end(),
        [](const rws::AnalysisWarning& warning) {
            return warning.code == "StructureOptimization.NoFeasibleCandidate";
        });
    REQUIRE(result.bestCandidateIndex == -1);
    REQUIRE(result.sensitivity.robustnessGrade == "Unknown");
    REQUIRE(hasNoFeasibleWarning);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 灵敏度假评价器:值越偏离 0 得分越低,任一变量 |v|>0.8 即不可行。
// 与 QuadraticFakeEvaluator 类似但解析更简单,便于手工推算扰动后的降分。
struct SensitivityMockEvaluator : public rws::IStructureCandidateEvaluator {
    void evaluate(
        const rws::StructureOptimizationProblem& problem,
        rws::StructureCandidateResult& candidate,
        rws::StructureEvaluationStage stage,
        const rws::StructureOptimizationCallbacks& callbacks,
        rws::StructureCandidateCache* cache) override
    {
        (void)problem; (void)callbacks; (void)cache;
        candidate.stage   = stage;
        candidate.feasible = true;
        candidate.totalScore = 100.0;
        for (double v : candidate.values) {
            if (v > 0.8 || v < -0.8) {
                candidate.feasible = false;
                candidate.violatedConstraints.push_back("OutOfRange");
                candidate.totalScore = 0.0;
                candidate.status = rws::StructureCandidateStatus::Infeasible;
                return;
            }
            candidate.totalScore -= std::abs(v) * 10.0;
        }
        candidate.status = rws::StructureCandidateStatus::Feasible;
    }
};

// 子套件 灵敏度分析:用 SensitivityMockEvaluator 在最佳候选 x=0,y=0 上做扰动,
// 验证每个变量生成 +step/-step 两个扰动条目(共 4 个),最大降分 = 2 → 等级 "A",
// 且没有关键变量——关键变量列表只在降分超过阈值时才填充。
static void testSensitivity()
{
    std::printf("testSensitivity ... ");

    // 问题: 两个设计变量
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "j1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.2, 0.0, 0.0, true, false},
        {"y", "Y", "j2", "mm",
         rws::StructureVariableKind::JointPositionY,
         0.0, -1.0, 1.0, 0.2, 0.0, 0.0, true, false}
    };

    // 最佳候选: x=0.0, y=0.0 (全零, score=100)
    rws::StructureCandidateResult best;
    best.index  = 0;
    best.values = {0.0, 0.0};
    best.feasible  = true;
    best.totalScore = 100.0;

    // 执行分析
    SensitivityMockEvaluator mockEval;
    rws::StructureSensitivityAnalyzer analyzer;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = []() { return false; };

    rws::StructureSensitivityResult result =
        analyzer.analyze(problem, best, mockEval, callbacks, nullptr);

    // 断言:
    // 每个变量有 2 个扰动方向, 共 4 个 entry
    REQUIRE(result.entries.size() == 4);

    // x = 0, perturb +0.2 -> score = 100 - 0.2*10 = 98, drop = 2
    // y = 0, perturb +0.2 -> score = 100 - 0.2*10 = 98, drop = 2
    // 所以 maxDrop = 2, grade = "A"
    REQUIRE(result.maximumScoreDrop > 0.0);
    REQUIRE(result.maximumScoreDrop <= 2.0 + 1e-12);
    REQUIRE(result.robustnessGrade == "A");

    // 无关键变量 (所有 drop <= 2)
    REQUIRE(result.criticalVariableIds.empty());

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: JSON 序列化往返
// =============================================================================

// 子套件 灵敏度取消:验证取消回调在第三次检查时触发后,灵敏度分析立即停止
// (只剩 1 个扰动条目),整体等级保持 "Unknown"——取消必须贯穿扰动循环,
// 而不是等到分析完成后再检查一次。
static void testSensitivityStopsAfterCancellation()
{
    std::printf("testSensitivityStopsAfterCancellation ... ");

    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"x", "X", "j1", "m", rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.2},
        {"y", "Y", "j2", "m", rws::StructureVariableKind::JointPositionY,
         0.0, -1.0, 1.0, 0.2}
    };
    rws::StructureCandidateResult best;
    best.values = {0.0, 0.0};
    best.feasible = true;
    best.totalScore = 100.0;

    int cancellationChecks = 0;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = [&cancellationChecks]() {
        ++cancellationChecks;
        return cancellationChecks >= 3;
    };

    SensitivityMockEvaluator evaluator;
    rws::StructureSensitivityAnalyzer analyzer;
    const rws::StructureSensitivityResult result = analyzer.analyze(
        problem, best, evaluator, callbacks, nullptr);

    REQUIRE(result.entries.size() == 1);
    REQUIRE(cancellationChecks >= 2);
    REQUIRE(result.robustnessGrade == "Unknown");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 问题 JSON 往返:构造带变量(离散域)、约束、运行参数、快速/验证工作区
// 采样、覆盖盒、目标、指标约束与需求溯源(requirementProvenance)的完整问题,
// 序列化后反序列化并逐字段比对,保证 v2 schema 不丢字段;报告保留冻结姿态的
// 解析来源;旧 v1 权重 JSON 能迁移到 v2 objectives 且不启用覆盖盒、快速采样回退
// 默认 1000。
static void testJsonRoundTrip()
{
    std::printf("testJsonRoundTrip ... ");

    // 创建填充的问题
    rws::StructureOptimizationProblem problem;
    problem.context.projectName = "TestProj";
    problem.context.robotName   = "TestRobot";

    problem.variables = {
        {"a", "Var A", "j1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.5, -1.0, 1.0, 0.1, 0.3, 0.5, true, false}
    };
    problem.variables[0].domainDefinition.domain = rws::DesignVariableDomain::Discrete;
    problem.variables[0].domainDefinition.discreteOptions = {"0.4", "0.5", "0.6"};

    problem.constraints = {
        {"c1", "Constraint 1", "", rws::StructureConstraintKind::ModelValid,
         0.0, 0.0, true, true}
    };

    problem.run.candidateCount = 100;
    problem.run.randomSeed     = 42;
    problem.evaluation.evaluatorId = "test.kinematics";
    problem.evaluation.evaluatorVersion = "2.0";
    problem.evaluation.quickWorkspace.mode = rws::WorkspaceSamplingMode::Grid;
    problem.evaluation.quickWorkspace.sampleCount = 123;
    problem.evaluation.quickWorkspace.gridStepsPerJoint = 4;
    problem.evaluation.quickWorkspace.checkCollision = false;
    problem.evaluation.quickWorkspace.randomSeed = 9u;
    problem.evaluation.verifiedWorkspace.sampleCount = 456;
    problem.evaluation.verifiedWorkspace.randomSeed = 11u;
    problem.evaluation.coverageBox.enabled = true;
    problem.evaluation.coverageBox.minimum = {{-0.5, -0.4, 0.1}};
    problem.evaluation.coverageBox.maximum = {{0.5, 0.6, 1.1}};
    problem.evaluation.coverageBox.cells = {{2, 3, 4}};
    problem.objectives = {
        {"kinematics.reachability.weighted", rws::OptimizationDirection::Maximize,
         {1.0, 0.0, true}, 0.7, true}
    };
    problem.metricConstraints = {
        {"collision.free_rate", rws::ComparisonOperator::GreaterThanOrEqual,
         0.95, true, true}
    };
    // 冻结需求的审计身份必须随优化项目往返，确保导出的项目不会丢失其输入依据。
    // 显式填写每个字段，避免审计结构新增成员后，聚合初始化的成员顺序在测试中被悄然误用。
    problem.requirementProvenance.requirementFingerprint = "requirement-sha256";
    problem.requirementProvenance.workcellFingerprint = "workcell-state-sha256";
    problem.requirementProvenance.compilerVersion = "EngineeringRequirements.Freezer.1";
    problem.requirementProvenance.frozenAt = "2026-07-30T09:15:00.123Z";

    // 序列化
    const std::string json = rws::StructureOptimizationJson::problemToJson(problem);
    REQUIRE(!json.empty());
    REQUIRE(json.find("\"schemaVersion\": 2") != std::string::npos);

    // 反序列化
    rws::StructureOptimizationProblem parsed;
    std::string error;
    bool ok = rws::StructureOptimizationJson::problemFromJson(json, parsed, &error);
    if (!ok)
        std::printf("\n  fromJson error: %s\n", error.c_str());
    REQUIRE(ok);

    // 验证字段
    REQUIRE(parsed.context.projectName == "TestProj");
    REQUIRE(parsed.context.robotName   == "TestRobot");
    REQUIRE(parsed.variables.size() == 1);
    REQUIRE(parsed.variables[0].id == "a");
    REQUIRE(std::abs(parsed.variables[0].currentValue - 0.5) < 1e-12);
    REQUIRE(parsed.variables[0].domainDefinition.domain == rws::DesignVariableDomain::Discrete);
    REQUIRE(parsed.variables[0].domainDefinition.discreteOptions.size() == 3);
    REQUIRE(parsed.constraints.size() == 1);
    REQUIRE(parsed.constraints[0].id == "c1");
    REQUIRE(parsed.run.candidateCount == 100);
    REQUIRE(parsed.run.randomSeed == 42u);
    REQUIRE(parsed.evaluation.evaluatorId == "test.kinematics");
    REQUIRE(parsed.evaluation.evaluatorVersion == "2.0");
    REQUIRE(parsed.evaluation.quickWorkspace.mode == rws::WorkspaceSamplingMode::Grid);
    REQUIRE(parsed.evaluation.quickWorkspace.sampleCount == 123);
    REQUIRE(parsed.evaluation.quickWorkspace.gridStepsPerJoint == 4);
    REQUIRE(!parsed.evaluation.quickWorkspace.checkCollision);
    REQUIRE(parsed.evaluation.quickWorkspace.randomSeed == 9u);
    REQUIRE(parsed.evaluation.verifiedWorkspace.sampleCount == 456);
    REQUIRE(parsed.evaluation.coverageBox.enabled);
    REQUIRE(parsed.evaluation.coverageBox.cells[0] == 2);
    REQUIRE(parsed.evaluation.coverageBox.cells[1] == 3);
    REQUIRE(parsed.evaluation.coverageBox.cells[2] == 4);
    REQUIRE(parsed.objectives.size() == 1);
    REQUIRE(parsed.objectives[0].metricId == "kinematics.reachability.weighted");
    REQUIRE(std::abs(parsed.objectives[0].weight - 0.7) < 1e-12);
    REQUIRE(parsed.metricConstraints.size() == 1);
    REQUIRE(parsed.metricConstraints[0].metricId == "collision.free_rate");
    REQUIRE(parsed.requirementProvenance.requirementFingerprint == "requirement-sha256");
    REQUIRE(parsed.requirementProvenance.workcellFingerprint == "workcell-state-sha256");
    REQUIRE(parsed.requirementProvenance.compilerVersion == "EngineeringRequirements.Freezer.1");
    REQUIRE(parsed.requirementProvenance.frozenAt == "2026-07-30T09:15:00.123Z");

    // 报告必须保留冻结姿态的解析来源，而不是仅展示已经失去业务语义的 RPY 数值。
    rws::OptimizationTaskPoint auditedStation;
    auditedStation.required = true;
    auditedStation.point.id = "fixture_pick";
    auditedStation.point.name = "Fixture pick";
    auditedStation.point.refFrame = "Fixture_A";
    auditedStation.point.tcpFrame = "TCP";
    auditedStation.point.position = {{0.12, 0.03, 0.08}};
    auditedStation.point.rpyDeg = {{0.0, 90.0, 0.0}};
    auditedStation.point.note = "Orientation resolution: resolver=OrientationRuleResolver.1;mode=AlignFrame;target=Fixture_A";
    parsed.tasks.push_back(auditedStation);

    const std::string report = rws::StructureOptimizationReportWriter::write(
        parsed, rws::StructureOptimizationResult{});
    REQUIRE(report.find("test.kinematics@2.0") != std::string::npos);
    REQUIRE(report.find("system evaluators not enabled") != std::string::npos);
    REQUIRE(report.find("Engineering Requirement Provenance") != std::string::npos);
    REQUIRE(report.find("2026-07-30T09:15:00.123Z") != std::string::npos);
    REQUIRE(report.find("Frozen Key Stations") != std::string::npos);
    REQUIRE(report.find("fixture_pick") != std::string::npos);
    REQUIRE(report.find("OrientationRuleResolver.1") != std::string::npos);

    const std::string legacyJson = R"json({
        "schemaVersion": 1,
        "type": "StructureOptimizationProblem",
        "weights": {
            "reachability": 0.6,
            "manipulability": 0.0,
            "jointMargin": 0.0,
            "collision": 0.0,
            "compactness": 0.0,
            "preference": 0.4
        }
    })json";
    rws::StructureOptimizationProblem migrated;
    ok = rws::StructureOptimizationJson::problemFromJson(legacyJson, migrated, &error);
    REQUIRE(ok);
    REQUIRE(migrated.objectives.size() == 6);
    REQUIRE(std::abs(migrated.objectives[0].weight - 0.6) < 1e-12);
    REQUIRE(!migrated.evaluation.coverageBox.enabled);
    REQUIRE(migrated.evaluation.quickWorkspace.sampleCount == 1000);
    const std::string upgradedJson = rws::StructureOptimizationJson::problemToJson(migrated);
    REQUIRE(upgradedJson.find("\"schemaVersion\": 2") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 审计证据输出:构造带模型溯源(源路径/源指纹/快照指纹)与区域级覆盖率
// 指标的候选结果,验证报告、结果 JSON、审计 CSV 三种导出形式都保留证据阶段统计、
// 灵敏度来源与关键变量、各区域覆盖率及本地参考系,供工程师复核是哪个工装区域
// 限制了候选结构。
static void testAuditableEvidenceOutput()
{
    std::printf("testAuditableEvidenceOutput ... ");

    rws::StructureOptimizationProblem problem;
    problem.context.robotName = "AuditRobot";
    problem.context.sourceModelPath = "models/audit.rmb.json";
    problem.context.modelProvenance = {"models/audit.rmb.json", "source-sha",
                                       "snapshot-sha"};
    problem.evaluation.evaluatorId = "test.kinematics";
    problem.evaluation.evaluatorVersion = "2.0";
    problem.evaluation.coverageBox.enabled = true;
    problem.evaluation.coverageBox.minimum = {{-0.5, -0.4, 0.1}};
    problem.evaluation.coverageBox.maximum = {{0.5, 0.6, 1.1}};
    problem.evaluation.coverageBox.cells = {{2, 3, 4}};
    problem.evaluation.quickWorkspace.sampleCount = 120;
    problem.evaluation.quickWorkspace.randomSeed = 17u;
    problem.evaluation.verifiedWorkspace.sampleCount = 480;
    problem.evaluation.verifiedWorkspace.randomSeed = 23u;
    problem.variables = {
        {"link2", "Link 2", "Joint2", "m",
         rws::StructureVariableKind::JointPositionX,
         0.4, 0.2, 0.8, 0.01, 0.4, 0.0, true, false}
    };

    rws::StructureOptimizationResult result;
    result.baselineCandidateIndex = 0;
    result.bestCandidateIndex = 7;
    result.diagnostics.generatedCandidates = 31;
    result.diagnostics.evaluatedCandidates = 29;
    result.diagnostics.cacheHits = 6;
    result.diagnostics.quickEvaluatedCandidates = 20;
    result.diagnostics.verifiedEliteCandidates = 4;
    result.diagnostics.finalVerifiedCandidates = 2;
    result.diagnostics.sensitivityEvaluations = 2;
    result.sensitivity.robustnessGrade = "B";
    result.sensitivity.criticalVariableIds = {"link2"};

    rws::StructureCandidateResult best;
    best.index = 7;
    best.feasible = true;
    best.status = rws::StructureCandidateStatus::Feasible;
    best.stage = rws::StructureEvaluationStage::Verified;
    best.totalScore = 91.0;
    best.values = {0.45};
    best.raw.workspaceCoverage = 0.75;
    best.raw.workspaceOccupiedCellCount = 18;
    best.raw.workspaceTotalCellCount = 24;
    // 多区域冻结需求不能只在评价器内部可见；报告必须保留每个区域的局部参考系、
    // 覆盖率和网格统计，才能让工程师复核哪个工装区域限制了候选结构。
    best.raw.workspaceRegionMetrics = {
        {"area_a", "Fixture_A", 0.90, 9, 10},
        {"area_b", "Fixture_B", 0.50, 5, 10}
    };
    result.candidates.push_back(best);

    const std::string report = rws::StructureOptimizationReportWriter::write(problem, result);
    REQUIRE(report.find("Final verified candidates: 2") != std::string::npos);
    REQUIRE(report.find("Best candidate: 7") != std::string::npos);
    REQUIRE(report.find("Workspace coverage: 0.750") != std::string::npos);
    REQUIRE(report.find("Grid cells: 2 x 3 x 4") != std::string::npos);
    REQUIRE(report.find("Quick sampling: mode=RandomUniform, samples=120") !=
            std::string::npos);
    REQUIRE(report.find("Sensitivity source: verified evaluator") != std::string::npos);
    REQUIRE(report.find("Critical variables: link2") != std::string::npos);
    REQUIRE(report.find("Trajectory evaluator: not enabled") != std::string::npos);
    REQUIRE(report.find("Dynamics evaluator: not enabled") != std::string::npos);
    REQUIRE(report.find("Drive selection evaluator: not enabled") != std::string::npos);
    REQUIRE(report.find("Source status: Tracked") != std::string::npos);
    REQUIRE(report.find("Source model path: models/audit.rmb.json") != std::string::npos);
    REQUIRE(report.find("Source fingerprint: source-sha") != std::string::npos);
    REQUIRE(report.find("Snapshot fingerprint: snapshot-sha") != std::string::npos);
    REQUIRE(report.find("Workspace Coverage Results") != std::string::npos);
    REQUIRE(report.find("area_a") != std::string::npos);
    REQUIRE(report.find("Fixture_B") != std::string::npos);
    REQUIRE(report.find("0.500") != std::string::npos);

    const std::string json = rws::StructureOptimizationJson::resultToJson(problem, result);
    REQUIRE(json.find("\"quickEvaluatedCandidates\": 20") != std::string::npos);
    REQUIRE(json.find("\"finalVerifiedCandidates\": 2") != std::string::npos);
    REQUIRE(json.find("\"sensitivityEvaluations\": 2") != std::string::npos);

    const std::string audit = rws::StructureOptimizationCsv::auditCsv(problem, result);
    REQUIRE(audit.find("QuickEvaluatedCandidates") != std::string::npos);
    REQUIRE(audit.find("FinalVerifiedCandidates") != std::string::npos);
    REQUIRE(audit.find("SensitivityEvaluations") != std::string::npos);
    REQUIRE(audit.find("test.kinematics@2.0") != std::string::npos);
    REQUIRE(audit.find("ModelProvenanceStatus,Tracked") != std::string::npos);
    REQUIRE(audit.find("SourceModelPath,models/audit.rmb.json") != std::string::npos);
    REQUIRE(audit.find("SourceFingerprint,source-sha") != std::string::npos);
    REQUIRE(audit.find("SnapshotFingerprint,snapshot-sha") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: CSV 导出
// =============================================================================

static void testCsvExport()
{
    std::printf("testCsvExport ... ");

    // 准备结果
    rws::StructureOptimizationProblem problem;
    problem.variables = {
        {"v1", "Var 1", "j1", "mm",
         rws::StructureVariableKind::JointPositionX,
         0.0, -1.0, 1.0, 0.1, 0.0, 0.0, true, false}
    };

    rws::StructureOptimizationResult result;

    rws::StructureCandidateResult c0;
    c0.index      = 0;
    c0.feasible   = true;
    c0.totalScore = 85.5;
    c0.status     = rws::StructureCandidateStatus::Feasible;
    c0.values     = {0.1};
    c0.raw.requiredReachableCount = 5;
    c0.raw.requiredTaskCount      = 5;
    c0.raw.manipulabilityP10      = 0.05;
    c0.raw.jointMarginP10         = 0.12;
    c0.raw.collisionFreeRate      = 1.0;
    c0.raw.totalKinematicLength   = 0.8;

    rws::StructureTaskMetric tm;
    tm.taskId = "t1";
    tm.taskName = "Task 1";
    tm.required   = true;
    tm.reachable  = true;
    tm.inCollision = false;
    tm.manipulability = 0.3;
    tm.jointMargin    = 0.15;
    tm.usableSolutionCount = 5;
    c0.raw.taskMetrics.push_back(tm);

    result.candidates.push_back(c0);

    // candidatesCsv
    const std::string csv = rws::StructureOptimizationCsv::candidatesCsv(problem, result);
    REQUIRE(!csv.empty());

    // 检查表头包含关键列
    REQUIRE(csv.find("Index,Status,Feasible,TotalScore") != std::string::npos);
    REQUIRE(csv.find("v1") != std::string::npos);
    // 检查数据行
    REQUIRE(csv.find("\n0,Feasible,") != std::string::npos);
    REQUIRE(csv.find(",true,") != std::string::npos);

    // taskDetailCsv
    const std::string detail = rws::StructureOptimizationCsv::taskDetailCsv(problem, result);
    REQUIRE(!detail.empty());

    REQUIRE(detail.find("CandidateIndex,TaskId,TaskName") != std::string::npos);
    REQUIRE(detail.find("t1") != std::string::npos);
    REQUIRE(detail.find("Task 1") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 工作区覆盖率统计:2x2x2 覆盖盒内放入 Pass/Warning/碰撞三个样本与一个
// 盒外样本,验证只有"落在盒内且未碰撞"的样本才占据单元(2/8=0.25)——
// 碰撞样本与盒外样本都不计入,Warning 仍算可达,覆盖率不可超过 1。
static void testWorkspaceCoverage()
{
    std::printf("testWorkspaceCoverage ... ");

    rws::WorkspaceCoverageBox box;
    box.enabled = true;
    box.minimum = {{0.0, 0.0, 0.0}};
    box.maximum = {{2.0, 2.0, 2.0}};
    box.cells = {{2, 2, 2}};

    rws::WorkspaceSample first;
    first.tcpPosition = {{0.1, 0.1, 0.1}};
    first.status = rws::AnalysisStatus::Pass;

    rws::WorkspaceSample second;
    second.tcpPosition = {{1.1, 1.1, 1.1}};
    second.status = rws::AnalysisStatus::Warning;

    rws::WorkspaceSample collided;
    collided.tcpPosition = {{1.1, 0.1, 0.1}};
    collided.status = rws::AnalysisStatus::Pass;
    collided.inCollision = true;

    rws::WorkspaceSample outside;
    outside.tcpPosition = {{2.1, 0.1, 0.1}};
    outside.status = rws::AnalysisStatus::Pass;

    const std::vector<rws::WorkspaceSample> samples = {
        first, second, collided, outside};
    const rws::StructureWorkspaceCoverageResult result =
        rws::StructureWorkspaceCoverage::analyze(samples, box);
    REQUIRE(result.totalCellCount == 8u);
    REQUIRE(result.occupiedCellCount == 2u);
    REQUIRE(std::abs(result.coverage - 0.25) < 1e-12);
    REQUIRE(std::abs(rws::StructureWorkspaceCoverage::calculate(samples, box) - 0.25) <
            1e-12);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 结构优化器接入真实工作空间覆盖率
// =============================================================================

// 构造一个专用于工作区覆盖率测试的问题:关闭碰撞、启用 1x1x1 大覆盖盒、
// 快速采样 16 个点固定种子。供多个覆盖率用例复用,保证求值环境一致。
static rws::StructureOptimizationProblem makeWorkspaceCoverageProblem()
{
    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.modelSpec.robotName = "WorkspaceCoverageRobot";
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;
    problem.context.tcpFrame.clear();
    problem.evaluation.checkCollision = false;
    problem.evaluation.coverageBox.enabled = true;
    problem.evaluation.coverageBox.minimum = {{-10.0, -10.0, -10.0}};
    problem.evaluation.coverageBox.maximum = {{10.0, 10.0, 10.0}};
    problem.evaluation.coverageBox.cells = {{1, 1, 1}};
    problem.evaluation.quickWorkspace.sampleCount = 16;
    problem.evaluation.quickWorkspace.checkCollision = false;
    problem.evaluation.quickWorkspace.randomSeed = 7u;
    return problem;
}

// 在工程评价结果里按 metricId 查找指标,找不到返回 nullptr。测试辅助函数。
static const rws::EngineeringMetric* findMetric(
    const rws::EngineeringEvaluationResult& result, const std::string& metricId)
{
    for (const rws::EngineeringMetric& metric : result.metrics) {
        if (metric.metricId == metricId)
            return &metric;
    }
    return nullptr;
}

// 判断工程评价结果是否产出了指定工件。测试辅助函数。
static bool hasArtifact(const rws::EngineeringEvaluationResult& result,
                        const std::string& artifactId)
{
    for (const rws::EngineeringArtifact& artifact : result.artifacts) {
        if (artifact.artifactId == artifactId)
            return true;
    }
    return false;
}

// 子套件 工作区覆盖率评价器:验证 KinematicEngineeringEvaluator 在真实六轴模型上
// 产出正值 coverage 指标与 coverage-summary 工件;再挂一条超过可达上限的
// MinimumWorkspaceCoverage 硬约束(阈值 1.1),评价结果必须为 Infeasible。
static void testWorkspaceCoverageEvaluator()
{
    std::printf("testWorkspaceCoverageEvaluator ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    rws::KinematicEngineeringEvaluator evaluator(problem);
    rws::CandidateEvaluationContext context;
    rws::EvaluationRequest request;
    const rws::EngineeringEvaluationResult result = evaluator.evaluate(
        context, request, rws::EvaluationCallbacks());
    const rws::EngineeringMetric* coverage = findMetric(
        result, "kinematics.workspace.coverage");

    REQUIRE(result.status == rws::EngineeringEvaluationStatus::Success);
    REQUIRE(coverage != nullptr);
    REQUIRE(coverage != nullptr && coverage->value > 0.0);
    REQUIRE(hasArtifact(result, "kinematics.workspace.coverage-summary"));

    rws::StructureConstraint minimumCoverage;
    minimumCoverage.id = "coverage-too-high";
    minimumCoverage.kind = rws::StructureConstraintKind::MinimumWorkspaceCoverage;
    minimumCoverage.threshold = 1.1;
    minimumCoverage.hard = true;
    minimumCoverage.enabled = true;
    problem.constraints.push_back(minimumCoverage);
    rws::KinematicEngineeringEvaluator constrainedEvaluator(problem);
    const rws::EngineeringEvaluationResult constrained =
        constrainedEvaluator.evaluate(context, request, rws::EvaluationCallbacks());
    REQUIRE(constrained.status == rws::EngineeringEvaluationStatus::Infeasible);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 评价器一致性:在候选场景里放置一个覆盖整个可达空间的大碰撞盒子,验证
// KinematicEngineeringEvaluator 对"当前位姿"任务(Must 碰撞必需 / Should 无碰撞)
// 的结果与直接调用 TargetEvaluator + ConfigurationEvaluator 完全一致——Must 任务
// 因碰撞不可达、Should 任务可达;任务指标的 jointMargin/manipulability 与配置
// 求值逐位吻合,防止两条评价路径漂移。
static void testSharedTargetEvaluatorConsistency()
{
    std::printf("testSharedTargetEvaluatorConsistency ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.evaluation.checkCollision = true;
    problem.evaluation.coverageBox.enabled = false;
    problem.evaluation.coverageBoxes.clear();

    rws::SceneGeometrySpec collisionFixture;
    collisionFixture.name = "EvaluatorConsistencyCollisionFixture";
    collisionFixture.refFrame = "WORLD";
    collisionFixture.kind = rws::GeometryKind::Box;
    collisionFixture.size = {{10.0, 10.0, 10.0}};
    collisionFixture.collisionModel = true;
    problem.context.modelSpec.sceneGeometries.push_back(collisionFixture);

    rws::CandidateModelBuildRequest request;
    request.spec = problem.context.modelSpec;
    request.deviceName = problem.context.deviceName;
    request.tcpFrame = problem.context.tcpFrame;
    request.checkCollision = true;
    const rws::CandidateModelBuildResult built = rws::CandidateModelFactory().build(request);
    REQUIRE(built.ok);
    REQUIRE(!built.artifact.collisionDetector.isNull());
    if (!built.ok) return;

    const rw::math::Transform3D<> worldTtcp = rw::kinematics::Kinematics::frameTframe(
        built.artifact.workcell->getWorldFrame(), built.artifact.tcpFrame.get(),
        built.artifact.state);
    const rw::math::RPY<> targetRpy(worldTtcp.R());

    rws::RequirementExecutionTask requirementTask;
    requirementTask.id = "shared-evaluator-current-pose";
    requirementTask.name = "Shared evaluator current pose";
    requirementTask.level = rws::RequirementExecutionLevel::Must;
    requirementTask.compileState = rws::RequirementExecutionCompileState::Included;
    requirementTask.refFrame = "WORLD";
    requirementTask.tcpFrame = built.artifact.tcpFrame->getName();
    requirementTask.collisionFreeRequired = true;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        requirementTask.position[axis] = worldTtcp.P()[axis];
        requirementTask.rpyDeg[axis] = targetRpy(axis) * rw::math::Rad2Deg;
    }
    requirementTask.positionToleranceMeters = 1e-6;
    requirementTask.orientationToleranceDeg = 1e-4;
    problem.requirementExecution.tasks.push_back(requirementTask);

    rws::RequirementExecutionTask shouldTask = requirementTask;
    shouldTask.id = "shared-evaluator-should-task";
    shouldTask.name = "Shared evaluator Should task";
    shouldTask.level = rws::RequirementExecutionLevel::Should;
    shouldTask.collisionFreeRequired = false;
    problem.requirementExecution.tasks.push_back(shouldTask);

    rws::StructureCandidateResult structureResult;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = [] () { return false; };
    rws::KinematicEngineeringEvaluator(problem).evaluateLegacy(
        structureResult, rws::StructureEvaluationStage::Verified, callbacks, nullptr);
    REQUIRE(structureResult.raw.taskMetrics.size() == 2);
    if (structureResult.raw.taskMetrics.size() != 2) return;
    REQUIRE(structureResult.raw.taskMetrics[0].required);
    REQUIRE(!structureResult.raw.taskMetrics[1].required);
    REQUIRE(!structureResult.raw.taskMetrics[0].reachable);
    REQUIRE(structureResult.raw.taskMetrics[0].inCollision);
    REQUIRE(structureResult.raw.taskMetrics[1].reachable);
    REQUIRE(!structureResult.raw.taskMetrics[1].inCollision);

    rws::AnalysisContextInput contextInput;
    contextInput.workcell = built.artifact.workcell;
    contextInput.device = built.artifact.device;
    contextInput.tcpFrame = built.artifact.tcpFrame;
    contextInput.baseState = built.artifact.state;
    contextInput.modelFingerprint = "structure-evaluator-consistency-model";
    contextInput.environmentFingerprint = "structure-evaluator-consistency-environment";
    contextInput.thresholds = problem.evaluation.thresholds;
    contextInput.collisionDetector = built.artifact.collisionDetector;
    contextInput.collisionRequired = true;
    rws::AnalysisContext context;
    std::string contextError;
    REQUIRE(rws::makeAnalysisContext(contextInput, context, &contextError));

    rws::TargetEvaluationOptions options;
    rws::TaskPoint target;
    target.id = requirementTask.id;
    target.name = requirementTask.name;
    target.refFrame = requirementTask.refFrame;
    target.tcpFrame = requirementTask.tcpFrame;
    target.position = requirementTask.position;
    target.rpyDeg = requirementTask.rpyDeg;
    target.tolerance.positionMeters = requirementTask.positionToleranceMeters;
    target.tolerance.orientationDeg = requirementTask.orientationToleranceDeg;

    options.evidenceStage = rws::AnalysisEvidenceStage::Verified;
    options.checkCollision = true;
    options.requireCollisionFree = true;
    options.positionToleranceMeters = target.tolerance.positionMeters;
    options.orientationToleranceDeg = target.tolerance.orientationDeg;
    const rws::TargetEvaluation direct =
        rws::TargetEvaluator().evaluate(context, target, options);
    REQUIRE(!direct.candidates.empty());
    if (direct.candidates.empty()) return;

    const rws::TargetCandidate& selected = direct.candidates.front();
    rws::ConfigurationEvaluationOptions configurationOptions;
    configurationOptions.evidenceStage = rws::AnalysisEvidenceStage::Verified;
    configurationOptions.checkCollision = true;
    configurationOptions.requireCollisionFree = true;
    const rws::ConfigurationEvaluation sameQ =
        rws::ConfigurationEvaluator().evaluate(
            context, selected.configuration.q, configurationOptions);
    REQUIRE(sameQ.collisionChecked);
    REQUIRE(sameQ.inCollision);
    REQUIRE(selected.configuration.feasibility == sameQ.feasibility);
    REQUIRE(selected.configuration.inCollision == sameQ.inCollision);
    REQUIRE(std::abs(selected.configuration.minimumJointMargin -
                     sameQ.minimumJointMargin) < 1e-12);
    REQUIRE(std::abs(selected.configuration.manipulability -
                     sameQ.manipulability) < 1e-12);

    const rws::StructureTaskMetric& metric = structureResult.raw.taskMetrics.front();
    REQUIRE(metric.reachable == (direct.feasibility == rws::Feasibility::Feasible));
    REQUIRE(metric.inCollision == sameQ.inCollision);
    REQUIRE(std::abs(metric.jointMargin - sameQ.minimumJointMargin) < 1e-12);
    REQUIRE(std::abs(metric.manipulability - sameQ.manipulability) < 1e-12);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 Verified 区域走共享评价器:在当前位姿处放置一个极小(1e-6)的 Verified
// 区域,验证 kinematic 求值器在 Quick 与 Verified 两个阶段都成功,且 Verified 阶段
// 产出 coverage == 1.0 的指标——证明区域覆盖率确实经由同一套 RegionCoverage
// 机制计算,而非只对任务点生效。
static void testVerifiedRegionUsesSharedEvaluator()
{
    std::printf("testVerifiedRegionUsesSharedEvaluator ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.evaluation.checkCollision = true;
    problem.evaluation.verifiedWorkspace.sampleCount = 8;
    problem.evaluation.verifiedWorkspace.checkCollision = false;

    rws::CandidateModelBuildRequest buildRequest;
    buildRequest.spec = problem.context.modelSpec;
    buildRequest.deviceName = problem.context.deviceName;
    buildRequest.tcpFrame = problem.context.tcpFrame;
    buildRequest.checkCollision = true;
    const rws::CandidateModelBuildResult built =
        rws::CandidateModelFactory().build(buildRequest);
    REQUIRE(built.ok);
    if (!built.ok) return;
    const rw::math::Transform3D<> worldTtcp = rw::kinematics::Kinematics::frameTframe(
        built.artifact.workcell->getWorldFrame(), built.artifact.tcpFrame.get(),
        built.artifact.state);
    const rw::math::RPY<> currentRpy(worldTtcp.R());

    rws::RequirementExecutionRegion region;
    region.id = "verified-far-region";
    region.name = "Verified far region";
    region.level = rws::RequirementExecutionLevel::Must;
    region.compileState = rws::RequirementExecutionCompileState::Included;
    region.refFrame = "WORLD";
    region.center = {{worldTtcp.P()[0], worldTtcp.P()[1], worldTtcp.P()[2]}};
    region.size = {{1e-6, 1e-6, 1e-6}};
    region.fixedRpyDeg = {{currentRpy(0) * rw::math::Rad2Deg,
                           currentRpy(1) * rw::math::Rad2Deg,
                           currentRpy(2) * rw::math::Rad2Deg}};
    region.minimumCoverage = 1.0;
    region.samplesPerAxis = 2;
    region.orientationMode = rws::RequirementExecutionOrientationMode::Fixed;
    region.directionSamples = 1;
    region.rollSamples = 1;
    region.minimumOrientationCoverage = 0.5;
    region.minimumVerificationStage = rws::RequirementExecutionStage::Quick;
    region.collisionFreeRequired = false;
    problem.requirementExecution.provenance.requirementFingerprint =
        "verified-region-requirements";
    problem.requirementExecution.workspaceRegions.push_back(region);

    rws::CandidateEvaluationContext context;
    rws::EvaluationRequest quickRequest;
    quickRequest.stage = rws::EngineeringEvaluationStage::Quick;
    const rws::EngineeringEvaluationResult quick =
        rws::KinematicEngineeringEvaluator(problem).evaluate(
            context, quickRequest, rws::EvaluationCallbacks());
    REQUIRE(quick.status == rws::EngineeringEvaluationStatus::Success);

    rws::EvaluationRequest verifiedRequest;
    verifiedRequest.stage = rws::EngineeringEvaluationStage::Verified;
    const rws::EngineeringEvaluationResult verified =
        rws::KinematicEngineeringEvaluator(problem).evaluate(
            context, verifiedRequest, rws::EvaluationCallbacks());
    REQUIRE(verified.status == rws::EngineeringEvaluationStatus::Success);
    const rws::EngineeringMetric* coverage =
        findMetric(verified, "kinematics.workspace.coverage");
    REQUIRE(coverage != nullptr);
    REQUIRE(coverage != nullptr && coverage->value == 1.0);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 区域位置覆盖率保真:验证 PointAtTarget 区域下 orientationCoverage 小于
// positionCoverage(位置可达但方向受限),并把最小方向覆盖率收紧到恰好通过;
// 结构评价器中的区域指标取的是 positionCoverage 并与直接求值一致——防止方向
// 覆盖率把位置可达的区域误判为不可达(只把位置覆盖作为硬约束)。
static void testVerifiedRegionPreservesPositionCoverage()
{
    std::printf("testVerifiedRegionPreservesPositionCoverage ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.evaluation.coverageBox.enabled = false;
    problem.evaluation.coverageBoxes.clear();
    REQUIRE(!problem.context.modelSpec.limits.empty());
    if (problem.context.modelSpec.limits.empty()) return;
    problem.context.modelSpec.limits.back().posMin = -20.0;
    problem.context.modelSpec.limits.back().posMax = 20.0;

    rws::CandidateModelBuildRequest buildRequest;
    buildRequest.spec = problem.context.modelSpec;
    buildRequest.deviceName = problem.context.deviceName;
    buildRequest.tcpFrame = problem.context.tcpFrame;
    buildRequest.checkCollision = false;
    const rws::CandidateModelBuildResult built =
        rws::CandidateModelFactory().build(buildRequest);
    REQUIRE(built.ok);
    if (!built.ok) return;

    const rw::math::Transform3D<> worldTtcp = rw::kinematics::Kinematics::frameTframe(
        built.artifact.workcell->getWorldFrame(), built.artifact.tcpFrame.get(),
        built.artifact.state);
    const rw::math::Vector3D<> orientationTarget =
        worldTtcp.P() + worldTtcp.R().getCol(2);

    rws::RequirementExecutionRegion region;
    region.id = "position-versus-orientation-region";
    region.name = "Position versus orientation region";
    region.level = rws::RequirementExecutionLevel::Must;
    region.compileState = rws::RequirementExecutionCompileState::Included;
    region.refFrame = "WORLD";
    region.tcpFrame = built.artifact.tcpFrame->getName();
    region.center = {{worldTtcp.P()[0], worldTtcp.P()[1], worldTtcp.P()[2]}};
    region.size = {{1e-6, 1e-6, 1e-6}};
    region.samplesPerAxis = 2;
    region.orientationMode = rws::RequirementExecutionOrientationMode::PointAtTarget;
    region.orientationTargetPoint = QString("%1,%2,%3")
        .arg(orientationTarget[0], 0, 'g', 17)
        .arg(orientationTarget[1], 0, 'g', 17)
        .arg(orientationTarget[2], 0, 'g', 17).toStdString();
    region.rollSamples = 24;
    region.minimumCoverage = 1.0;
    region.minimumOrientationCoverage = 0.0;
    region.collisionFreeRequired = false;

    rws::AnalysisContextInput contextInput;
    contextInput.workcell = built.artifact.workcell;
    contextInput.device = built.artifact.device;
    contextInput.tcpFrame = built.artifact.tcpFrame;
    contextInput.baseState = built.artifact.state;
    contextInput.modelFingerprint = "position-versus-orientation-model";
    contextInput.environmentFingerprint = "position-versus-orientation-environment";
    contextInput.thresholds = problem.evaluation.thresholds;
    rws::AnalysisContext analysisContext;
    std::string contextError;
    REQUIRE(rws::makeAnalysisContext(contextInput, analysisContext, &contextError));
    const rws::RegionCoverageResult direct =
        rws::RegionCoverageEvaluator().evaluate(analysisContext, region);
    REQUIRE(direct.feasibility == rws::Feasibility::Feasible);
    REQUIRE(direct.positionCoverage == 1.0);
    REQUIRE(direct.orientationCoverage > 0.0);
    REQUIRE(direct.orientationCoverage < direct.positionCoverage);

    region.minimumOrientationCoverage = direct.orientationCoverage;
    problem.requirementExecution.workspaceRegions.push_back(region);
    rws::StructureConstraint positionConstraint;
    positionConstraint.id = "position-coverage-only";
    positionConstraint.targetName = region.id;
    positionConstraint.kind = rws::StructureConstraintKind::MinimumWorkspaceCoverage;
    positionConstraint.threshold = region.minimumCoverage;
    positionConstraint.enabled = true;
    positionConstraint.hard = true;
    problem.constraints.push_back(positionConstraint);

    rws::StructureCandidateResult structureResult;
    rws::StructureOptimizationCallbacks callbacks;
    callbacks.isCancellationRequested = [] () { return false; };
    rws::KinematicEngineeringEvaluator(problem).evaluateLegacy(
        structureResult, rws::StructureEvaluationStage::Verified, callbacks, nullptr);
    REQUIRE(structureResult.status == rws::StructureCandidateStatus::Feasible);
    REQUIRE(structureResult.raw.workspaceRegionMetrics.size() == 1);
    REQUIRE(structureResult.raw.workspaceRegionMetrics.size() == 1 &&
            std::abs(structureResult.raw.workspaceRegionMetrics.front().coverage -
                     direct.positionCoverage) < 1e-12);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 覆盖率数据不足:当快速工作区采样数被设为 0 时,工程评价结果必须是
// DataInsufficient 且不产出 coverage 指标——采样缺失不能被伪装成低覆盖率。
static void testWorkspaceCoverageDataInsufficient()
{
    std::printf("testWorkspaceCoverageDataInsufficient ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    problem.evaluation.quickWorkspace.sampleCount = 0;
    rws::KinematicEngineeringEvaluator evaluator(problem);
    rws::CandidateEvaluationContext context;
    const rws::EngineeringEvaluationResult result = evaluator.evaluate(
        context, rws::EvaluationRequest(), rws::EvaluationCallbacks());

    REQUIRE(result.status == rws::EngineeringEvaluationStatus::DataInsufficient);
    REQUIRE(findMetric(result, "kinematics.workspace.coverage") == nullptr);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 覆盖率取消:取消回调在第二次检查时触发后,评价结果必须是 Cancelled,
// 而不是被误判为 Success 或 Infeasible。
static void testWorkspaceCoverageCancellation()
{
    std::printf("testWorkspaceCoverageCancellation ... ");

    rws::StructureOptimizationProblem problem = makeWorkspaceCoverageProblem();
    rws::KinematicEngineeringEvaluator evaluator(problem);
    rws::CandidateEvaluationContext context;
    int cancellationChecks = 0;
    rws::EvaluationCallbacks callbacks;
    callbacks.isCancellationRequested = [&cancellationChecks]() {
        ++cancellationChecks;
        return cancellationChecks >= 2;
    };
    const rws::EngineeringEvaluationResult result = evaluator.evaluate(
        context, rws::EvaluationRequest(), callbacks);

    REQUIRE(result.status == rws::EngineeringEvaluationStatus::Cancelled);
    REQUIRE(cancellationChecks >= 2);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  UI table models and default variable suggestions
// =============================================================================

static void testUiTableModelsAndSuggestions()
{
    std::printf("testUiTableModelsAndSuggestions ... ");

    rws::StructureDesignVariable variable;
    variable.id = "joint1_z";
    variable.label = "Joint1 Z";
    variable.targetName = "Joint1";
    variable.kind = rws::StructureVariableKind::JointPositionZ;
    variable.currentValue = 0.3;
    variable.minimum = 0.21;
    variable.maximum = 0.39;
    variable.step = 0.001;
    variable.enabled = true;

    rws::StructureVariableTableModel variableModel;
    variableModel.setVariables({variable});
    REQUIRE(variableModel.rowCount() == 1);
    REQUIRE(variableModel.columnCount() >= 8);
    REQUIRE(variableModel.variables().size() == 1);
    REQUIRE(variableModel.variables()[0].id == "joint1_z");
    REQUIRE(variableModel.data(variableModel.index(0, 0)).toString().toStdString() == "joint1_z");

    rws::OptimizationTaskPoint task;
    task.point.id = "pick";
    task.point.name = "Pick";
    task.required = false;

    rws::OptimizationTaskTableModel taskModel;
    taskModel.setTasks({task});
    REQUIRE(taskModel.rowCount() == 1);
    REQUIRE(taskModel.columnCount() >= 7);
    REQUIRE(taskModel.tasks().size() == 1);
    REQUIRE(taskModel.tasks()[0].point.id == "pick");

    rws::StructureCandidateResult candidate;
    candidate.index = 7;
    candidate.feasible = true;
    candidate.totalScore = 88.5;
    candidate.status = rws::StructureCandidateStatus::Feasible;

    rws::StructureCandidateTableModel candidateModel;
    candidateModel.setCandidates({candidate});
    REQUIRE(candidateModel.rowCount() == 1);
    REQUIRE(candidateModel.columnCount() >= 9);
    REQUIRE(candidateModel.candidates().size() == 1);
    REQUIRE(candidateModel.data(candidateModel.index(0, 0)).toInt() == 7);

    rws::StructureOptimizationResult candidateResult;
    rws::StructureCandidateResult baseline = candidate;
    baseline.index = 2;
    baseline.totalScore = 80.0;
    candidateResult.baselineCandidateIndex = 2;
    candidateResult.candidates = {candidate, baseline};
    candidateModel.setResult(candidateResult);
    REQUIRE(candidateModel.candidateByIndex(2) != nullptr);
    REQUIRE(candidateModel.candidateByIndex(2)->totalScore == 80.0);
    REQUIRE(candidateModel.data(candidateModel.index(0,
        rws::StructureCandidateTableModel::ImprovementColumn)).toDouble() == 8.5);

    rws::RobotDesignContext context;
    context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    context.modelSpec.robotName = "SuggestionRobot";
    context.modelSpec.transformJoints[0].pos[2] = 0.3;
    context.modelSpec.robotBaseFrame.pos[2] = 0.2;
    bool setTcpOffset = false;
    for (auto& joint : context.modelSpec.transformJoints) {
        if (rws::typeToKind(joint.type) == rws::JointKind::ToolFrame) {
            joint.pos[0] = 0.05;
            setTcpOffset = true;
            break;
        }
    }
    if (!setTcpOffset) {
        rws::JointTransformSpec tcp;
        tcp.name = "TCP";
        tcp.type = "ToolFrame";
        tcp.pos[0] = 0.05;
        context.modelSpec.transformJoints.push_back(tcp);
    }

    const std::vector< rws::StructureDesignVariable > suggested =
        rws::StructureOptimizationUiLogic::suggestVariables(context);

    bool foundJointZ = false;
    bool foundTcp = false;
    bool foundBaseHeight = false;
    bool foundLinkGeometry = false;
    for (const auto& suggestedVariable : suggested) {
        if (suggestedVariable.kind == rws::StructureVariableKind::JointPositionZ)
            foundJointZ = true;
        if (suggestedVariable.kind == rws::StructureVariableKind::TcpOffsetX ||
            suggestedVariable.kind == rws::StructureVariableKind::TcpOffsetY ||
            suggestedVariable.kind == rws::StructureVariableKind::TcpOffsetZ)
            foundTcp = true;
        if (suggestedVariable.kind == rws::StructureVariableKind::BaseHeight)
            foundBaseHeight = true;
        if (suggestedVariable.kind == rws::StructureVariableKind::LinkRadius ||
            suggestedVariable.kind == rws::StructureVariableKind::LinkWidth ||
            suggestedVariable.kind == rws::StructureVariableKind::LinkHeight)
            foundLinkGeometry = true;
    }
    REQUIRE(foundJointZ);
    REQUIRE(foundTcp);
    REQUIRE(foundBaseHeight);
    REQUIRE(foundLinkGeometry);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 约束表模型 + 项目适配器:验证约束表模型能编辑阈值;项目 saveProject/
// loadProject 往返保留约束、策略、局部精英数、权重等设置,且保存的模型输出目录是
// 相对路径(项目可整体搬迁),加载后仍被解析回可直接使用的绝对目录。
static void testConstraintModelAndProjectAdapter()
{
    std::printf("testConstraintModelAndProjectAdapter ... ");

    rws::StructureConstraint constraint;
    constraint.id = "workspace";
    constraint.label = "Workspace coverage";
    constraint.targetName = "workcell";
    constraint.kind = rws::StructureConstraintKind::MinimumWorkspaceCoverage;
    constraint.threshold = 0.85;
    constraint.secondaryThreshold = 0.05;
    constraint.enabled = true;
    constraint.hard = true;

    rws::StructureConstraintTableModel constraintModel;
    constraintModel.setConstraints({constraint});
    REQUIRE(constraintModel.rowCount() == 1);
    REQUIRE(constraintModel.constraints().at(0).id == "workspace");
    REQUIRE(constraintModel.setData(
        constraintModel.index(0, rws::StructureConstraintTableModel::ThresholdColumn),
        0.9));
    REQUIRE(std::abs(constraintModel.constraints().at(0).threshold - 0.9) < 1e-12);

    rws::StructureOptimizationProblem original;
    original.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    original.context.robotName = original.context.modelSpec.robotName;
    original.context.deviceName = original.context.modelSpec.robotName;
    original.constraints.push_back(constraint);
    original.run.strategy = rws::StructureStrategyKind::Grid;
    original.run.gridSteps = 7;
    original.run.localEliteCount = 4;
    original.weights.preference = 0.12;

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString modelOutputDirectory = dir.filePath("model-output");
    REQUIRE(QDir().mkpath(modelOutputDirectory));
    original.context.modelSpec.saveDirectory = modelOutputDirectory.toStdString();
    const QString path = dir.filePath("example.structure-optimization.json");
    QString error;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        path, original, 12, &error));

    // 项目资源可随目录整体搬迁。验证写入的模型输出目录是相对路径，而加载后仍被
    // Adapter 解析回可直接供编辑器和导出服务使用的绝对目录。
    QFile serializedFile(path);
    REQUIRE(serializedFile.open(QIODevice::ReadOnly));
    QJsonParseError jsonError;
    const QJsonDocument serializedDocument =
        QJsonDocument::fromJson(serializedFile.readAll(), &jsonError);
    serializedFile.close();
    REQUIRE(jsonError.error == QJsonParseError::NoError && serializedDocument.isObject());
    const QString savedDirectory = serializedDocument.object().value("problem").toObject()
        .value("context").toObject().value("data").toObject().value("modelSpec").toObject()
        .value("saveDirectory").toString();
    REQUIRE(!savedDirectory.isEmpty() && QFileInfo(savedDirectory).isRelative());

    rws::StructureOptimizationProblem loaded;
    int selectedCandidateIndex = -1;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        path, loaded, &selectedCandidateIndex, &error));
    REQUIRE(selectedCandidateIndex == 12);
    REQUIRE(loaded.context.modelSpec.robotName == original.context.modelSpec.robotName);
    REQUIRE(loaded.constraints.size() == 1);
    REQUIRE(loaded.constraints[0].id == "workspace");
    REQUIRE(loaded.run.strategy == rws::StructureStrategyKind::Grid);
    REQUIRE(loaded.run.gridSteps == 7);
    REQUIRE(loaded.run.localEliteCount == 4);
    REQUIRE(std::abs(loaded.weights.preference - 0.12) < 1e-12);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 冻结工程需求工件适配器:手工构造与 compiled 快照一致的 v4 工件
// (含执行契约与执行指纹),验证适配层只接受已冻结且模型指纹一致的需求——
// Must 工位/覆盖盒转为硬约束与独立评价区域,Should 区域不阻断;v3 工件被拒绝
// 并要求重冻结;多个 Must 区域各自成为独立覆盖盒,不能合并成单个 WORLD 盒。
static void testFrozenEngineeringRequirementArtifactAdapter()
{
    std::printf("testFrozenEngineeringRequirementArtifactAdapter ... ");

    // 使用与结构优化项目相同的 RobotModelSpec 构造冻结工件，验证适配层只
    // 接收已冻结且模型身份一致的工程需求，而不是直接读取编辑态表单数据。
    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;

    rws::FrozenRequirementArtifact artifact;
    artifact.schemaVersion = 4;
    artifact.requirementFingerprint = "requirement-fingerprint";
    artifact.environmentFingerprint = "environment-fingerprint";
    artifact.workcellFingerprint = "workcell-fingerprint";
    artifact.frozenAt = "2026-07-30T09:15:00.123Z";
    artifact.modelBinding.robotName = problem.context.modelSpec.robotName;
    artifact.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(problem.context.modelSpec);
    artifact.compiled.frozen = true;
    artifact.compiled.modelBinding = artifact.modelBinding;
    artifact.compiled.requirementFingerprint = artifact.requirementFingerprint;
    artifact.scenario.environmentFingerprint = artifact.environmentFingerprint;
    artifact.frozenRobotState.deviceName = artifact.modelBinding.robotName;
    artifact.frozenRobotState.tcpFrameName = "TCP";
    artifact.frozenRobotState.kinematicFingerprint = "robot-kinematic-fingerprint";
    artifact.frozenRobotState.tcpWorldPose[15] = 1.0;
    artifact.frozenRobotState.capturedAt = artifact.frozenAt;

    rws::CompiledPoseTask mustStation;
    mustStation.id = "pick";
    mustStation.name = "Pick station";
    mustStation.level = rws::RequirementLevel::Must;
    mustStation.refFrame = "WORLD";
    mustStation.tcpFrame = "TCP";
    mustStation.position = {{0.4, 0.1, 0.3}};
    mustStation.tolerance.positionMeters = 0.002;
    mustStation.validation.collisionFreeRequired = true;
    artifact.compiled.poseTasks.push_back(mustStation);

    rws::CompiledPoseTask shouldStation = mustStation;
    shouldStation.id = "inspect";
    shouldStation.name = "Inspection station";
    shouldStation.level = rws::RequirementLevel::Should;
    shouldStation.validation.collisionFreeRequired = false;
    artifact.compiled.poseTasks.push_back(shouldStation);

    rws::WorkspaceDemandRegion region;
    region.id = "work_area";
    region.name = "Work area";
    region.level = rws::RequirementLevel::Must;
    region.refFrame = "WORLD";
    region.tcpFrame = "TCP";
    region.center = {{0.5, 0.0, 0.4}};
    region.size = {{0.4, 0.2, 0.3}};
    region.minimumCoverage = 0.85;
    region.samplesPerAxis = 6;
    region.orientationMode = rws::OrientationMode::PointAtTarget;
    region.orientationTargetPoint = "0.8,0.0,0.4";
    region.directionSamples = 2;
    region.rollSamples = 3;
    region.minimumOrientationCoverage = 0.5;
    region.minimumVerificationStage = rws::RequirementVerificationStage::Quick;
    region.collisionFreeRequired = false;
    artifact.compiled.workspaceRegions.push_back(region);

    // 结构优化适配器只接受 v4 工件，因此测试须手工构造与 compiled 快照一致的执行契约
    // (execution)：provenance 逐项对齐工件顶层指纹，工位与覆盖盒从 compiled 投影而来，
    // 最后计算执行指纹。这样适配器的一致性审计(validateExecutionConsistency)才会通过。
    artifact.execution.schemaVersion = 1;
    artifact.execution.provenance.requirementFingerprint = artifact.requirementFingerprint;
    artifact.execution.provenance.robotModelFingerprint = artifact.modelBinding.robotModelFingerprint;
    artifact.execution.provenance.workcellFingerprint = artifact.workcellFingerprint;
    artifact.execution.provenance.environmentFingerprint = artifact.environmentFingerprint;
    artifact.execution.provenance.compilerVersion = artifact.compilerVersion;
    artifact.execution.provenance.frozenAt = artifact.frozenAt;
    artifact.execution.provenance.sourcePath = artifact.modelBinding.sourcePath;
    // 工位任务点从 compiled 投影到执行契约类型(枚举用 static_cast 同序映射)。
    for (const rws::CompiledPoseTask& source : artifact.compiled.poseTasks) {
        rws::RequirementExecutionTask task;
        task.id = source.id;
        task.name = source.name;
        task.level = static_cast<rws::RequirementExecutionLevel>(source.level);
        task.compileState = static_cast<rws::RequirementExecutionCompileState>(source.compileState);
        task.processType = static_cast<rws::RequirementExecutionProcessType>(source.processType);
        task.excludedReason = source.excludedReason;
        task.refFrame = source.refFrame;
        task.tcpFrame = source.tcpFrame;
        task.position = source.position;
        task.rpyDeg = source.rpyDeg;
        task.positionToleranceMeters = source.tolerance.positionMeters;
        task.orientationToleranceDeg = source.tolerance.orientationDeg;
        task.allowToolRollFree = source.tolerance.allowToolRollFree;
        task.collisionFreeRequired = source.validation.collisionFreeRequired;
        artifact.execution.tasks.push_back(task);
    }
    // 覆盖盒同样投影到执行契约类型。
    for (const rws::WorkspaceDemandRegion& source : artifact.compiled.workspaceRegions) {
        rws::RequirementExecutionRegion executionRegion;
        executionRegion.id = source.id;
        executionRegion.name = source.name;
        executionRegion.level = static_cast<rws::RequirementExecutionLevel>(source.level);
        executionRegion.compileState = static_cast<rws::RequirementExecutionCompileState>(source.compileState);
        executionRegion.excludedReason = source.excludedReason;
        executionRegion.refFrame = source.refFrame;
        executionRegion.tcpFrame = source.tcpFrame;
        executionRegion.center = source.center;
        executionRegion.size = source.size;
        executionRegion.minimumCoverage = source.minimumCoverage;
        executionRegion.samplesPerAxis = source.samplesPerAxis;
        executionRegion.minimumVerificationStage =
            static_cast<rws::RequirementExecutionStage>(source.minimumVerificationStage);
        executionRegion.orientationMode =
            static_cast<rws::RequirementExecutionOrientationMode>(source.orientationMode);
        executionRegion.orientationTargetPoint = source.orientationTargetPoint;
        executionRegion.directionSamples = source.directionSamples;
        executionRegion.rollSamples = source.rollSamples;
        executionRegion.minimumOrientationCoverage = source.minimumOrientationCoverage;
        executionRegion.collisionFreeRequired = source.collisionFreeRequired;
        artifact.execution.workspaceRegions.push_back(executionRegion);
    }
    // 依据执行契约计算执行指纹，供适配器的执行契约一致性校验使用。
    artifact.executionFingerprint = rws::RequirementExecutionJson::fingerprint(artifact.execution);

    std::string error;
    REQUIRE(rws::EngineeringRequirementArtifactAdapter::apply(artifact, problem, &error));
    REQUIRE(problem.tasks.size() == 2);
    REQUIRE(problem.tasks[0].required);
    REQUIRE(!problem.tasks[1].required);
    REQUIRE(problem.context.taskPoints.size() == 2);
    REQUIRE(problem.evaluation.coverageBox.enabled);
    REQUIRE(problem.evaluation.coverageBox.cells[0] == 5);
    REQUIRE(problem.requirementProvenance.requirementFingerprint == "requirement-fingerprint");
    REQUIRE(problem.requirementProvenance.workcellFingerprint == "workcell-fingerprint");
    REQUIRE(problem.requirementProvenance.environmentFingerprint == "environment-fingerprint");
    REQUIRE(problem.requirementProvenance.frozenAt == "2026-07-30T09:15:00.123Z");
    REQUIRE(problem.requirementExecution.provenance.requirementFingerprint ==
            artifact.execution.provenance.requirementFingerprint);
    REQUIRE(problem.requirementExecution.tasks.size() == artifact.execution.tasks.size());
    REQUIRE(problem.requirementExecution.tasks[0].collisionFreeRequired);
    REQUIRE(!problem.requirementExecution.tasks[1].collisionFreeRequired);
    REQUIRE(problem.requirementExecution.workspaceRegions.size() ==
            artifact.execution.workspaceRegions.size());
    REQUIRE(problem.requirementExecution.workspaceRegions.front().minimumVerificationStage ==
            rws::RequirementExecutionStage::Quick);
    REQUIRE(problem.requirementExecution.workspaceRegions.front().orientationMode ==
            artifact.execution.workspaceRegions.front().orientationMode);
    REQUIRE(problem.requirementExecution.workspaceRegions.front().orientationTargetPoint ==
            artifact.execution.workspaceRegions.front().orientationTargetPoint);
    REQUIRE(problem.requirementExecution.workspaceRegions.front().directionSamples == 2);
    REQUIRE(problem.requirementExecution.workspaceRegions.front().rollSamples == 3);
    REQUIRE(std::abs(problem.requirementExecution.workspaceRegions.front().minimumOrientationCoverage - 0.5) < 1e-12);
    REQUIRE(!problem.requirementExecution.workspaceRegions.front().collisionFreeRequired);
    rws::StructureOptimizationProblem restoredProblem;
    const std::string problemJson = rws::StructureOptimizationJson::problemToJson(problem);
    REQUIRE(rws::StructureOptimizationJson::problemFromJson(
        problemJson, restoredProblem, &error));
    REQUIRE(restoredProblem.requirementExecution.provenance.requirementFingerprint ==
            artifact.execution.provenance.requirementFingerprint);
    REQUIRE(restoredProblem.requirementExecution.tasks[0].collisionFreeRequired);
    REQUIRE(!restoredProblem.requirementExecution.tasks[1].collisionFreeRequired);
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.size() == 1);
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().id == "work_area");
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().orientationMode ==
            rws::RequirementExecutionOrientationMode::PointAtTarget);
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().orientationTargetPoint ==
            "0.8,0.0,0.4");
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().directionSamples == 2);
    REQUIRE(restoredProblem.requirementExecution.workspaceRegions.front().rollSamples == 3);
    REQUIRE(std::abs(restoredProblem.requirementExecution.workspaceRegions.front().minimumOrientationCoverage - 0.5) < 1e-12);
    REQUIRE(!restoredProblem.requirementExecution.workspaceRegions.front().collisionFreeRequired);

    // 旧版 v3 工件必须被结构优化适配器拒绝，错误信息应明确要求 v4(带 Verified 证据)。
    rws::FrozenRequirementArtifact legacyArtifact = artifact;
    legacyArtifact.schemaVersion = 3;
    REQUIRE(!rws::EngineeringRequirementArtifactAdapter::apply(legacyArtifact, problem, &error));
    REQUIRE(error.find("v4") != std::string::npos);

    // 追加第二个 Must 覆盖盒：compiled 与 execution 都必须同步加入，并重算执行指纹，
    // 保持两契约一致，否则适配器的一致性校验会失败。
    rws::WorkspaceDemandRegion secondRegion = region;
    secondRegion.id = "second_area";
    artifact.compiled.workspaceRegions.push_back(secondRegion);
    rws::RequirementExecutionRegion secondExecutionRegion = artifact.execution.workspaceRegions.front();
    secondExecutionRegion.id = secondRegion.id;
    artifact.execution.workspaceRegions.push_back(secondExecutionRegion);
    artifact.executionFingerprint = rws::RequirementExecutionJson::fingerprint(artifact.execution);
    // 多个必须覆盖区域必须分别转换为独立的评价区域和硬约束，不能再以“只支持一个”
    // 的方式拒绝工程需求，更不能把两者静默并成一个更大的 WORLD 覆盖盒。
    REQUIRE(rws::EngineeringRequirementArtifactAdapter::apply(artifact, problem, &error));
    REQUIRE(problem.evaluation.coverageBoxes.size() == 2);
    REQUIRE(problem.evaluation.coverageBoxes[0].id == "work_area");
    REQUIRE(problem.evaluation.coverageBoxes[1].id == "second_area");
    REQUIRE(problem.constraints.size() >= 2);

    // A supported artifact may contain an advisory pose-policy workspace.
    // Structure optimization currently evaluates position coverage only, so a
    // Should region must not become a hard blocker solely for this limitation.
    rws::WorkspaceDemandRegion advisoryPoseRegion = region;
    advisoryPoseRegion.id = "advisory_pose_area";
    advisoryPoseRegion.level = rws::RequirementLevel::Should;
    advisoryPoseRegion.directionSamples = 2;
    artifact.compiled.workspaceRegions.push_back(advisoryPoseRegion);
    rws::RequirementExecutionRegion advisoryExecutionRegion =
        artifact.execution.workspaceRegions.front();
    advisoryExecutionRegion.id = advisoryPoseRegion.id;
    advisoryExecutionRegion.level = rws::RequirementExecutionLevel::Should;
    advisoryExecutionRegion.directionSamples = advisoryPoseRegion.directionSamples;
    artifact.execution.workspaceRegions.push_back(advisoryExecutionRegion);
    artifact.executionFingerprint = rws::RequirementExecutionJson::fingerprint(artifact.execution);
    REQUIRE(rws::EngineeringRequirementArtifactAdapter::apply(artifact, problem, &error));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 项目适配器恢复托管场景根:把含托管冻结场景(带 Fixture 与几何)的项目
// 整体复制到新目录、删除原资产后,loadProject 必须解析出显式传入的 cloneRoot
// 作为场景根,使 CandidateModelFactory 仍能构建出带 ManagedFixture 的 WorkCell。
static void testProjectAdapterRestoresManagedScenarioRoot()
{
    std::printf("testProjectAdapterRestoresManagedScenarioRoot ... ");

    QTemporaryDir workspace;
    REQUIRE(workspace.isValid());
    const QString sourceRoot = workspace.filePath("source-project");
    const QString cloneRoot = workspace.filePath("clone-project");
    REQUIRE(QDir().mkpath(QDir(sourceRoot).filePath("optimizations")));
    REQUIRE(QDir().mkpath(QDir(sourceRoot).filePath("assets")));
    REQUIRE(QDir().mkpath(QDir(cloneRoot).filePath("optimizations")));
    REQUIRE(QDir().mkpath(QDir(cloneRoot).filePath("assets")));

    const QString sourceMesh = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/geometry/base.stl");
    const QString sourceProjectMesh = QDir(sourceRoot).filePath("assets/base.stl");
    const QString cloneProjectMesh = QDir(cloneRoot).filePath("assets/base.stl");
    REQUIRE(QFile::copy(sourceMesh, sourceProjectMesh));
    REQUIRE(QFile::copy(sourceMesh, cloneProjectMesh));

    rws::StructureOptimizationProblem original;
    original.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(sourceRoot);
    original.context.robotName = original.context.modelSpec.robotName;
    original.context.deviceName = original.context.modelSpec.robotName;
    original.scenarioSnapshot.schemaVersion = 1;
    original.scenarioSnapshot.snapshotFingerprint = "managed-scenario";
    rws::FrameSpec fixture;
    fixture.name = "ManagedFixture";
    fixture.refFrame = "WORLD";
    original.scenarioSnapshot.sceneSpec.sceneFrames.push_back(fixture);
    rws::SceneGeometrySpec geometry;
    geometry.name = "ManagedFixtureMesh";
    geometry.refFrame = fixture.name;
    geometry.kind = rws::GeometryKind::Polytope;
    geometry.file = "assets/base.stl";
    original.scenarioSnapshot.sceneSpec.sceneGeometries.push_back(geometry);

    const QString sourceDocument =
        QDir(sourceRoot).filePath("optimizations/main.structure-optimization.json");
    const QString cloneDocument =
        QDir(cloneRoot).filePath("optimizations/main.structure-optimization.json");
    QString error;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        sourceDocument, original, -1, &error));
    REQUIRE(QFile::copy(sourceDocument, cloneDocument));
    REQUIRE(QFile::remove(sourceProjectMesh));

    const QString previousCwd = QDir::currentPath();
    REQUIRE(QDir::setCurrent(workspace.filePath("unrelated-cwd")) ||
            (QDir().mkpath(workspace.filePath("unrelated-cwd")) &&
             QDir::setCurrent(workspace.filePath("unrelated-cwd"))));
    rws::StructureOptimizationProblem loaded;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        cloneDocument, loaded, nullptr, &error, cloneRoot));
    REQUIRE(loaded.scenarioSnapshot.baseDirectory == cloneRoot.toStdString());

    rws::CandidateModelBuildRequest request;
    request.spec = loaded.context.modelSpec;
    request.deviceName = loaded.context.deviceName;
    request.checkCollision = false;
    request.scenarioSnapshot = &loaded.scenarioSnapshot;
    request.scenarioBaseDirectory = loaded.scenarioSnapshot.baseDirectory;
    const rws::CandidateModelBuildResult result = rws::CandidateModelFactory().build(request);
    REQUIRE(result.ok);
    REQUIRE(!result.artifact.workcell.isNull());
    if (!result.artifact.workcell.isNull())
        REQUIRE(result.artifact.workcell->findFrame("ManagedFixture") != nullptr);
    REQUIRE(QDir::setCurrent(previousCwd));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 冻结需求项目导入:验证导入服务从真实文件边界读取冻结工件——需求文件中
// 必须有 frozenArtifact;模型用相对路径(随目录搬迁);机器人 jog 只报告
// robotStateChanged 而不拒绝,工装移动则明确拒绝(场景指纹变化);缺少
// frozenArtifact 的文件绝不能成为优化输入。
static void testFrozenRequirementProjectImportCreatesAuditableProblem()
{
    std::printf("testFrozenRequirementProjectImportCreatesAuditableProblem ... ");

    // 导入服务必须从真实文件边界读取冻结工件：需求 JSON 中只有编辑态 RequirementSet 或者
    // 工件缺失时都不能绕过冻结门禁。模型使用相对路径，覆盖工程项目随目录整体移动的场景。
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString modelPath = directory.filePath("robot.rmb.json");
    const QString requirementPath = directory.filePath("cell.requirements.json");
    const QString workcellPath = directory.filePath("source.wc.xml");
    QFile workcellFile(workcellPath);
    REQUIRE(workcellFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(workcellFile.write("<WorkCell name=\"ImportWorkCell\" />\n") > 0);
    workcellFile.close();
    const rws::RobotModelSpec model =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(directory.path());

    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(modelFile.write(QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(model))) > 0);
    modelFile.close();

    rws::RequirementSet requirements;
    requirements.name = "Frozen cell requirement";
    requirements.modelBinding.sourcePath = "robot.rmb.json";
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);
    requirements.frozen = true;

    rws::PoseTask requestedStation;
    requestedStation.id = "load";
    requestedStation.name = "Machine load";
    requestedStation.level = rws::RequirementLevel::Must;
    requestedStation.refFrame = "WORLD";
    requestedStation.tcpFrame = "ImportTcp";
    requestedStation.position = {{0.35, 0.0, 0.45}};
    requirements.poseTasks.push_back(requestedStation);

    const rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ImportBase", rw::math::Transform3D<>()));
    const rw::models::RevoluteJoint::Ptr joint = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("ImportJoint", rw::math::Transform3D<>()));
    const rw::kinematics::FixedFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ImportTcp", rw::math::Transform3D<>()));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(joint, base);
    structure->addFrame(tcp, joint);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "ImportWorkCell", workcellPath.toStdString()));
    const rw::models::SerialDevice::Ptr device = rw::core::ownedPtr(
        new rw::models::SerialDevice(base.get(), tcp.get(), model.robotName,
                                     structure->getDefaultState()));
    workcell->addDevice(device);
    const rw::kinematics::MovableFrame::Ptr fixture = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("ImportFixture"));
    workcell->addFrame(fixture, workcell->getWorldFrame());
    const rw::kinematics::State frozenState = workcell->getDefaultState();

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(rws::RequirementFreezer::freeze(requirements, *workcell, frozenState,
                                             model, artifact, &error,
                                             directory.path().toStdString()));

    QJsonObject requirementProject = rws::RequirementSetJson::toObject(requirements);
    requirementProject["frozenArtifact"] = rws::FrozenRequirementArtifactJson::toObject(artifact);
    QFile requirementFile(requirementPath);
    REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(requirementFile.write(QJsonDocument(requirementProject).toJson()) > 0);
    requirementFile.close();

    rws::StructureOptimizationProblem imported;
    rws::FrozenRequirementValidationResult validation;
    REQUIRE(rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, frozenState, imported, &validation, &error));
    REQUIRE(imported.tasks.size() == 1);
    REQUIRE(imported.tasks.front().point.id == "load");
    REQUIRE(imported.requirementProvenance.requirementFingerprint ==
            artifact.requirementFingerprint);
    REQUIRE(imported.requirementProvenance.frozenAt == artifact.frozenAt);
    REQUIRE(imported.context.sourceModelPath == modelPath.toStdString());
    REQUIRE(validation.warnings.empty());
    REQUIRE(imported.scenarioSnapshot.baseDirectory == directory.path().toStdString());

    rw::kinematics::State joggedState = frozenState;
    device->setQ(rw::math::Q(1, 0.25), joggedState);
    REQUIRE(rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, joggedState, imported, &validation, &error));
    REQUIRE(validation.robotStateChanged);

    rw::kinematics::State fixtureMovedState = frozenState;
    fixture->setTransform(
        rw::math::Transform3D<>(rw::math::Vector3D<>(0.1, 0.0, 0.0)), fixtureMovedState);
    REQUIRE(!rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, fixtureMovedState, imported, &validation, &error));
    REQUIRE(error.find("Fixture or external environment") != std::string::npos);

    // 没有冻结工件的需求文件只能继续编辑，绝不能被误用为下游优化输入。
    requirementProject.remove("frozenArtifact");
    REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    REQUIRE(requirementFile.write(QJsonDocument(requirementProject).toJson()) > 0);
    requirementFile.close();
    REQUIRE(!rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, frozenState, imported, &validation, &error));
    REQUIRE(error.find("frozenArtifact") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 托管冻结需求导入使用显式项目根:构造嵌套目录(项目根/data/frozen)下的
// 冻结需求与从 UR 设备转换的托管模型,验证不传 projectRoot 的启发式导入构造出的
// 场景无法构建候选;传入显式 projectRoot 后场景根正确、模型可移植,项目整体移动
// 后模型溯源仍为 Current。
static void testManagedFrozenRequirementImportUsesExplicitProjectRoot()
{
    std::printf("testManagedFrozenRequirementImportUsesExplicitProjectRoot ... ");

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectRoot = directory.filePath("ManagedProject");
    const QString nestedRoot = QDir(projectRoot).filePath("data/frozen");
    const QString deviceRoot = QDir(projectRoot).filePath("devices");
    const QString assetRoot = QDir(projectRoot).filePath("assets");
    REQUIRE(QDir().mkpath(nestedRoot));
    REQUIRE(QDir().mkpath(QDir(deviceRoot).filePath("geometry")));
    REQUIRE(QDir().mkpath(assetRoot));

    const QString urSourceRoot = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A");
    REQUIRE(QFile::copy(QDir(urSourceRoot).filePath("UR.wc.xml"),
                        QDir(deviceRoot).filePath("UR.wc.xml")));
    const QDir sourceGeometry(QDir(urSourceRoot).filePath("geometry"));
    for (const QString& fileName : sourceGeometry.entryList(QDir::Files)) {
        REQUIRE(QFile::copy(sourceGeometry.filePath(fileName),
                            QDir(deviceRoot).filePath("geometry/" + fileName)));
    }
    REQUIRE(QFile::copy(sourceGeometry.filePath("base.stl"),
                        QDir(assetRoot).filePath("fixture.stl")));

    const QString workcellPath = QDir(projectRoot).filePath("scenes/main.wc.xml");
    REQUIRE(QDir().mkpath(QFileInfo(workcellPath).absolutePath()));
    QFile workcellFile(workcellPath);
    REQUIRE(workcellFile.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray workcellXml =
        "<WorkCell name=\"ManagedImportScene\">\n"
        "  <Frame name=\"Fixture\" refframe=\"WORLD\" />\n"
        "  <Drawable name=\"FixtureMesh\" refframe=\"Fixture\">\n"
        "    <Polytope file=\"../assets/fixture.stl\" />\n"
        "  </Drawable>\n"
        "  <Include file=\"../devices/UR.wc.xml\" />\n"
        "</WorkCell>\n";
    REQUIRE(workcellFile.write(workcellXml) == workcellXml.size());
    workcellFile.close();

    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load(workcellPath.toStdString());
    REQUIRE(!workcell.isNull());
    if (workcell.isNull()) {
        std::printf("FAILED (%d)\n", g_testFailures);
        return;
    }
    QStringList conversionWarnings;
    rws::RobotModelSpec model = rws::WorkCellConverter::convert(
        *workcell, workcell->getDefaultState(), nestedRoot.toStdString(), conversionWarnings);
    REQUIRE(rws::WorkCellConverter::hasConvertibleRobotModel(model));
    REQUIRE(!workcell->getDevices().empty());
    if (!workcell->getDevices().empty()) {
        model.robotName = workcell->getDevices().front()->getName();
        model.includes.clear();
        model.imported = rws::ImportedDocumentSpec();
    }

    const QString modelPath = QDir(nestedRoot).filePath("robot.rmb.json");
    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray modelJson = QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(model));
    REQUIRE(modelFile.write(modelJson) == modelJson.size());
    modelFile.close();

    rws::RequirementSet requirements;
    requirements.name = "Managed nested requirement";
    requirements.frozen = true;
    requirements.modelBinding.sourcePath = "robot.rmb.json";
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);
    rws::PoseTask station;
    station.id = "managed-load";
    station.name = "Managed load";
    station.level = rws::RequirementLevel::Must;
    station.refFrame = "WORLD";
    station.tcpFrame = workcell->getDevices().front()->getEnd()->getName();
    requirements.poseTasks.push_back(station);

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    const bool frozen = rws::RequirementFreezer::freeze(
        requirements, *workcell, workcell->getDefaultState(), model, artifact, &error,
        projectRoot.toStdString());
    if (!frozen)
        std::fprintf(stderr, "Managed import freeze error: %s\n", error.c_str());
    REQUIRE(frozen);
    const QString requirementPath = QDir(nestedRoot).filePath("cell.requirements.json");
    QJsonObject requirementProject = rws::RequirementSetJson::toObject(requirements);
    requirementProject["frozenArtifact"] = rws::FrozenRequirementArtifactJson::toObject(artifact);
    QFile requirementFile(requirementPath);
    REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(requirementFile.write(QJsonDocument(requirementProject).toJson()) > 0);
    requirementFile.close();

    rws::StructureOptimizationProblem heuristicProblem;
    rws::FrozenRequirementValidationResult validation;
    const bool heuristicImported = rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, workcell->getDefaultState(), heuristicProblem,
        &validation, &error);
    if (!heuristicImported)
        std::fprintf(stderr, "Heuristic import error: %s\n", error.c_str());
    REQUIRE(heuristicImported);
    rws::CandidateModelBuildRequest heuristicRequest;
    heuristicRequest.spec = heuristicProblem.context.modelSpec;
    heuristicRequest.deviceName = heuristicProblem.context.deviceName;
    heuristicRequest.checkCollision = false;
    heuristicRequest.scenarioSnapshot = &heuristicProblem.scenarioSnapshot;
    heuristicRequest.scenarioBaseDirectory = heuristicProblem.scenarioSnapshot.baseDirectory;
    REQUIRE(!rws::CandidateModelFactory().build(heuristicRequest).ok);

    rws::StructureOptimizationProblem managedProblem;
    const bool managedImported = rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, workcell->getDefaultState(), managedProblem,
        &validation, &error, projectRoot);
    if (!managedImported)
        std::fprintf(stderr, "Managed import error: %s\n", error.c_str());
    REQUIRE(managedImported);
    REQUIRE(QDir::cleanPath(QString::fromStdString(managedProblem.scenarioSnapshot.baseDirectory)) ==
            QDir::cleanPath(projectRoot));
    rws::CandidateModelBuildRequest managedRequest;
    managedRequest.spec = managedProblem.context.modelSpec;
    managedRequest.deviceName = managedProblem.context.deviceName;
    managedRequest.checkCollision = false;
    managedRequest.scenarioSnapshot = &managedProblem.scenarioSnapshot;
    managedRequest.scenarioBaseDirectory = managedProblem.scenarioSnapshot.baseDirectory;
    const rws::CandidateModelBuildResult managedBuild =
        rws::CandidateModelFactory().build(managedRequest);
    if (!managedBuild.ok) {
        for (const rws::AnalysisWarning& warning : managedBuild.warnings)
            std::fprintf(stderr, "Managed candidate build error: %s\n", warning.message.c_str());
    }
    REQUIRE(managedBuild.ok);

    rws::RobotModelSpec portableModel;
    QString portableError;
    REQUIRE(rws::RobotModelProjectPaths::makePortable(
        model, projectRoot, portableModel, &portableError));
    portableModel.saveDirectory.clear();
    rws::RobotModelSpec portableRuntime;
    REQUIRE(rws::RobotModelProjectPaths::resolveManaged(
        portableModel, projectRoot, portableRuntime, &portableError));
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray portableJson =
        QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(portableModel));
    REQUIRE(modelFile.write(portableJson) == portableJson.size());
    modelFile.close();

    rws::RequirementSet portableRequirements = requirements;
    portableRequirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(portableRuntime);
    rws::FrozenRequirementArtifact portableArtifact;
    REQUIRE(rws::RequirementFreezer::freeze(
        portableRequirements, *workcell, workcell->getDefaultState(), portableRuntime,
        portableArtifact, &error, projectRoot.toStdString()));
    QJsonObject portableRequirementProject =
        rws::RequirementSetJson::toObject(portableRequirements);
    portableRequirementProject["frozenArtifact"] =
        rws::FrozenRequirementArtifactJson::toObject(portableArtifact);
    REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(requirementFile.write(QJsonDocument(portableRequirementProject).toJson()) > 0);
    requirementFile.close();

    rws::StructureOptimizationProblem portableProblem;
    const bool portableImported = rws::FrozenRequirementProjectImportService::createProblem(
        requirementPath, *workcell, workcell->getDefaultState(), portableProblem,
        &validation, &error, projectRoot);
    if (!portableImported)
        std::fprintf(stderr, "Portable managed import error: %s\n", error.c_str());
    REQUIRE(portableImported);
    REQUIRE(rws::RobotModelFingerprint::canonicalSha256(portableProblem.context.modelSpec) ==
            portableRequirements.modelBinding.robotModelFingerprint);

    const QString optimizationPath =
        QDir(projectRoot).filePath("optimizations/main.structure-optimization.json");
    const rws::RobotModelStalenessResult currentStatus =
        rws::RobotModelStalenessChecker::checkManaged(
            portableProblem.context, optimizationPath, projectRoot);
    REQUIRE(currentStatus.status == rws::RobotModelSourceStatus::Current);
    REQUIRE(QDir::cleanPath(currentStatus.resolvedSourcePath) == QDir::cleanPath(modelPath));

    REQUIRE(QDir().mkpath(QFileInfo(optimizationPath).absolutePath()));
    rws::StructureOptimizationProblem persistedProblem = portableProblem;
    const QString relativeModelPath = QDir(QFileInfo(optimizationPath).absolutePath())
                                          .relativeFilePath(modelPath);
    persistedProblem.context.sourceModelPath = relativeModelPath.toStdString();
    persistedProblem.context.modelProvenance.sourceModelPath =
        relativeModelPath.toStdString();
    persistedProblem.context.modelSpec.saveDirectory =
        QStringLiteral("../generated/robot-models").toStdString();
    QString projectError;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        optimizationPath, persistedProblem, -1, &projectError));

    const QString movedRoot = directory.filePath("ManagedProjectMoved");
    REQUIRE(QDir(directory.path()).rename("ManagedProject", "ManagedProjectMoved"));
    QTemporaryDir hostileDirectory;
    REQUIRE(hostileDirectory.isValid());
    const QString previousCwd = QDir::currentPath();
    REQUIRE(QDir::setCurrent(hostileDirectory.path()));

    const QString movedModelPath = QDir(movedRoot).filePath("data/frozen/robot.rmb.json");
    const QString movedOptimizationPath =
        QDir(movedRoot).filePath("optimizations/main.structure-optimization.json");
    rws::StructureOptimizationProblem movedProblem;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        movedOptimizationPath, movedProblem, nullptr, &projectError, movedRoot));
    const rws::RobotModelStalenessResult movedStatus =
        rws::RobotModelStalenessChecker::checkManaged(
            movedProblem.context, movedOptimizationPath, movedRoot);
    REQUIRE(movedStatus.status == rws::RobotModelSourceStatus::Current);
    REQUIRE(QDir::cleanPath(movedStatus.resolvedSourcePath) ==
            QDir::cleanPath(movedModelPath));
    REQUIRE(QDir::setCurrent(previousCwd));

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 项目工厂:验证从 RobotModelSpec 创建结构优化问题(设备名/变量/空任务),
// 保存/加载往返后 JSON 完全一致;空模型规格必须被拒绝并给出稳定的
// StructureOptimization.Context.Invalid 错误。
static void testProjectFactory()
{
    std::printf("testProjectFactory ... ");

    rws::RobotModelSpec spec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    spec.robotName = "FactoryRobot";

    rws::StructureOptimizationProblem problem;
    std::string error;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(spec, problem, &error));
    REQUIRE(problem.context.modelSpec.robotName == "FactoryRobot");
    REQUIRE(problem.context.robotName == "FactoryRobot");
    REQUIRE(problem.context.deviceName == "FactoryRobot");
    REQUIRE(!problem.variables.empty());
    REQUIRE(problem.tasks.empty());

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectPath = directory.filePath("factory.structure-optimization.json");
    QString projectError;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        projectPath, problem, -1, &projectError));

    rws::StructureOptimizationProblem loaded;
    REQUIRE(rws::StructureOptimizationProjectAdapter::loadProject(
        projectPath, loaded, nullptr, &projectError));
    REQUIRE(loaded.context.modelSpec.robotName == problem.context.modelSpec.robotName);
    REQUIRE(loaded.context.modelSpec.transformJoints.size() ==
            problem.context.modelSpec.transformJoints.size());
    REQUIRE(loaded.variables.size() == problem.variables.size());
    REQUIRE(rws::StructureOptimizationJson::problemToJson(loaded) ==
            rws::StructureOptimizationJson::problemToJson(problem));

    rws::StructureOptimizationProblem invalidProblem;
    error.clear();
    REQUIRE(!rws::StructureOptimizationProjectFactory::create(
        rws::RobotModelSpec(), invalidProblem, &error));
    REQUIRE(error.find("StructureOptimization.Context.Invalid") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 项目工厂溯源:验证 factory 记录的模型溯源与 RobotModelStalenessChecker
// 状态机联动——源文件未变→Current,修改→Stale,删除→SourceMissing,写坏→
// SourceInvalid;并覆盖相对几何后缀碰撞与平台根目录边界下的托管解析。
static void testProjectFactoryProvenance()
{
    std::printf("testProjectFactoryProvenance ... ");

    QTemporaryDir directory;
    REQUIRE(directory.isValid());

    rws::RobotModelSpec spec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(directory.path());
    spec.robotName = "ProvenanceRobot";
    QStringList saveErrors;
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(spec, saveErrors));
    const QString sourceFilePath = rws::RobotModelXmlWriter::specSidecarFilePath(spec);
    const QString relativeSourcePath = QFileInfo(sourceFilePath).fileName();
    const QString projectPath = directory.filePath("provenance.structure-optimization.json");

    rws::StructureOptimizationProblem problem;
    std::string error;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        spec, relativeSourcePath, problem, &error));
    REQUIRE(problem.context.modelProvenance.sourceModelPath ==
            relativeSourcePath.toStdString());
    REQUIRE(!problem.context.modelProvenance.sourceFingerprint.empty());
    REQUIRE(problem.context.modelProvenance.sourceFingerprint ==
            problem.context.modelProvenance.snapshotFingerprint);
    REQUIRE(rws::RobotModelStalenessChecker::check(problem.context, projectPath).status ==
            rws::RobotModelSourceStatus::Current);

    rws::RobotModelSpec changedSpec = spec;
    changedSpec.transformJoints.at(1).pos[0] += 0.001;
    saveErrors.clear();
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(changedSpec, saveErrors));
    REQUIRE(rws::RobotModelStalenessChecker::check(problem.context, projectPath).status ==
            rws::RobotModelSourceStatus::Stale);

    REQUIRE(QFile::remove(sourceFilePath));
    REQUIRE(rws::RobotModelStalenessChecker::check(problem.context, projectPath).status ==
            rws::RobotModelSourceStatus::SourceMissing);

    QFile invalidSource(sourceFilePath);
    REQUIRE(invalidSource.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(invalidSource.write("{not-json") > 0);
    invalidSource.close();
    REQUIRE(rws::RobotModelStalenessChecker::check(problem.context, projectPath).status ==
            rws::RobotModelSourceStatus::SourceInvalid);

    QTemporaryDir suffixCollisionDirectory;
    REQUIRE(suffixCollisionDirectory.isValid());
    rws::RobotModelSpec suffixCollisionSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(
            suffixCollisionDirectory.path());
    suffixCollisionSpec.robotName = "StandaloneSuffixCollision";
    suffixCollisionSpec.saveDirectory =
        suffixCollisionDirectory.filePath("generated/robot-models").toStdString();
    suffixCollisionSpec.drawables.clear();
    suffixCollisionSpec.collisionModels.clear();
    suffixCollisionSpec.sceneGeometries.clear();
    rws::DrawableSpec relativeDrawable;
    relativeDrawable.name = "standalone-relative-geometry";
    relativeDrawable.refFrame = "WORLD";
    relativeDrawable.shape = "Mesh";
    relativeDrawable.filePath = "assets/base.stl";
    suffixCollisionSpec.drawables.push_back(relativeDrawable);
    const QString suffixCollisionSource =
        suffixCollisionDirectory.filePath("standalone.rmb.json");
    QFile suffixCollisionFile(suffixCollisionSource);
    REQUIRE(suffixCollisionFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray suffixCollisionJson = QByteArray::fromStdString(
        rws::RobotModelSpecJson::toJson(suffixCollisionSpec));
    REQUIRE(suffixCollisionFile.write(suffixCollisionJson) == suffixCollisionJson.size());
    suffixCollisionFile.close();
    rws::StructureOptimizationProblem suffixCollisionProblem;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        suffixCollisionSpec, suffixCollisionSource, suffixCollisionProblem, &error));
    REQUIRE(rws::RobotModelStalenessChecker::check(
                suffixCollisionProblem.context,
                suffixCollisionDirectory.filePath(
                    "standalone.structure-optimization.json")).status ==
            rws::RobotModelSourceStatus::Current);

    QTemporaryDir rootBoundaryDirectory;
    REQUIRE(rootBoundaryDirectory.isValid());
    const QString rootBoundaryAsset =
        rootBoundaryDirectory.filePath("assets/root-boundary.stl");
    REQUIRE(QDir().mkpath(QFileInfo(rootBoundaryAsset).absolutePath()));
    QFile rootBoundaryAssetFile(rootBoundaryAsset);
    REQUIRE(rootBoundaryAssetFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(rootBoundaryAssetFile.write("solid root-boundary\nendsolid root-boundary\n") > 0);
    rootBoundaryAssetFile.close();
    const QString normalizedBoundaryAsset = QDir::fromNativeSeparators(
        QFileInfo(rootBoundaryAsset).absoluteFilePath());
#ifdef Q_OS_WIN
    const QString rootBoundary = normalizedBoundaryAsset.left(3);
#else
    const QString rootBoundary = QStringLiteral("/");
#endif
    REQUIRE(QFileInfo(rootBoundary).isAbsolute());
    const QString rootRelativeAsset =
        QDir(rootBoundary).relativeFilePath(normalizedBoundaryAsset);
    REQUIRE(QFileInfo(rootRelativeAsset).isRelative());
    rws::RobotModelSpec rootPortable =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(rootBoundaryDirectory.path());
    rootPortable.robotName = "ManagedRootBoundary";
    rootPortable.saveDirectory.clear();
    rootPortable.drawables.clear();
    rootPortable.collisionModels.clear();
    rootPortable.sceneGeometries.clear();
    rws::DrawableSpec rootDrawable;
    rootDrawable.name = "managed-root-boundary";
    rootDrawable.refFrame = "WORLD";
    rootDrawable.shape = "Mesh";
    rootDrawable.filePath = rootRelativeAsset.toStdString();
    rootPortable.drawables.push_back(rootDrawable);
    const QString rootSource = rootBoundaryDirectory.filePath("root-boundary.rmb.json");
    QFile rootSourceFile(rootSource);
    REQUIRE(rootSourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray rootSourceJson =
        QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(rootPortable));
    REQUIRE(rootSourceFile.write(rootSourceJson) == rootSourceJson.size());
    rootSourceFile.close();
    rws::RobotModelSpec rootRuntime;
    QString rootPathError;
    REQUIRE(rws::RobotModelProjectPaths::resolveManaged(
        rootPortable, rootBoundary, rootRuntime, &rootPathError));
    rws::StructureOptimizationProblem rootProblem;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        rootRuntime, rootSource, rootProblem, &error));
    REQUIRE(rws::RobotModelStalenessChecker::checkManaged(
                rootProblem.context,
                rootBoundaryDirectory.filePath("root-boundary.structure-optimization.json"),
                rootBoundary).status == rws::RobotModelSourceStatus::Current);

    rws::RobotDesignContext legacyContext;
    REQUIRE(rws::RobotModelStalenessChecker::check(legacyContext, projectPath).status ==
            rws::RobotModelSourceStatus::Untracked);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 导出服务:验证 exportAll 一次写出报告/审计 CSV/项目 JSON 等 6 个文件,
// 目标已存在时重复导出失败;候选模型导出必须把冻结工装 Fixture_A 写入同一份场景
// XML,保证预览/评价与交付给工程师的模型对工装 Frame 的解释一致。
static void testExportService()
{
    std::printf("testExportService ... ");

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;

    rws::StructureOptimizationResult result;
    rws::StructureCandidateResult candidate;
    candidate.index = 5;
    candidate.feasible = true;
    candidate.status = rws::StructureCandidateStatus::Feasible;
    candidate.totalScore = 91.5;
    candidate.raw.weightedReachability = 1.0;
    result.baselineCandidateIndex = 5;
    result.bestCandidateIndex = 5;
    result.candidates.push_back(candidate);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    rws::StructureOptimizationExportRequest request;
    request.directory = dir.filePath("export");
    request.selectedCandidateIndex = 5;
    request.exportCandidateModel = false;
    const rws::StructureOptimizationExportResult exported =
        rws::StructureOptimizationExportService::exportAll(problem, result, request);
    REQUIRE(exported.ok);
    REQUIRE(exported.writtenFiles.size() == 6);
    REQUIRE(QFileInfo::exists(dir.filePath("export/report.md")));
    REQUIRE(QFileInfo::exists(dir.filePath("export/audit.csv")));
    REQUIRE(QFileInfo::exists(dir.filePath("export/project.structure-optimization.json")));

    const rws::StructureOptimizationExportResult conflict =
        rws::StructureOptimizationExportService::exportAll(problem, result, request);
    REQUIRE(!conflict.ok);
    REQUIRE(!conflict.errors.isEmpty());

    // 候选评价已将冻结工装合入临时 WorkCell，因此最终导出的模型也必须写入同一份
    // 场景。否则预览/评价与交付给研发工程师的 XML 对工装 Frame 的解释会不一致。
    rws::StructureOptimizationScenarioSnapshot scenario;
    scenario.schemaVersion = 1;
    scenario.snapshotFingerprint = "fixture-export-snapshot";
    rws::FrameSpec fixture;
    fixture.name = "Fixture_A";
    fixture.refFrame = "WORLD";
    fixture.pos = {{0.4, 0.0, 0.2}};
    scenario.sceneSpec.sceneFrames.push_back(fixture);
    problem.scenarioSnapshot = scenario;

    const QString modelDirectory = dir.filePath("candidate-with-fixture");
    QStringList modelErrors;
    REQUIRE(rws::StructureCandidateExporter::exportModel(
        problem, candidate, modelDirectory, modelErrors));
    REQUIRE(modelErrors.isEmpty());

    QDir exportedModel(modelDirectory);
    const QStringList sceneFiles = exportedModel.entryList(
        QStringList() << "*Scene.wc.xml", QDir::Files);
    REQUIRE(sceneFiles.size() == 1);
    if (sceneFiles.size() == 1) {
        QFile sceneFile(exportedModel.filePath(sceneFiles.front()));
        REQUIRE(sceneFile.open(QIODevice::ReadOnly));
        if (sceneFile.isOpen()) {
            const QByteArray sceneXml = sceneFile.readAll();
            REQUIRE(sceneXml.contains("Fixture_A"));
        }
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 端到端验收(UR 六轴):从 UR.wc.xml 创建托管项目、转换模型、构造含 3 个
// 任务与 3 个设计变量的问题并保存;重新加载后校验模型溯源为 Current,并实际跑
// 两次系统级优化(验证确定性)与最佳候选模型导出,检查候选 WC 落盘。
static void testAcceptedUr6585AProject()
{
    std::printf("testAcceptedUr6585AProject ... ");

    QTemporaryDir inputDirectory;
    REQUIRE(inputDirectory.isValid());
    const QString sourceWorkCell = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml");
    const QString manifestPath =
        inputDirectory.filePath("UrProject/UrProject.rwproj");
    const QString projectDirectory = QFileInfo(manifestPath).absolutePath();

    QString projectError;
    rws::ProjectManager manager;
    REQUIRE(manager.createProjectFromWorkCell(
        manifestPath, sourceWorkCell, &projectError));
    QString managedWorkCellPath;
    REQUIRE(manager.resolveResource(
        "scene.main", managedWorkCellPath, &projectError));
    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load(
            managedWorkCellPath.toStdString());
    REQUIRE(!workcell.isNull());

    const QString modelDirectory =
        QDir(projectDirectory).filePath("generated/robot-models");
    REQUIRE(QDir().mkpath(modelDirectory));
    QStringList conversionWarnings;
    rws::RobotModelSpec modelSpec;
    if (!workcell.isNull()) {
        modelSpec = rws::WorkCellConverter::convert(
            *workcell, workcell->getDefaultState(),
            modelDirectory.toStdString(), conversionWarnings);
    }
    REQUIRE(rws::WorkCellConverter::hasConvertibleRobotModel(modelSpec));

    const QString modelPath =
        QDir(modelDirectory).filePath("UR-6-85-5-A.rmb.json");
    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray modelJson = QByteArray::fromStdString(
        rws::RobotModelSpecJson::toJson(modelSpec));
    if (modelFile.isOpen()) {
        REQUIRE(modelFile.write(modelJson) == modelJson.size());
        modelFile.close();
    }

    rws::StructureOptimizationProblem generatedProject;
    std::string error;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        modelSpec, modelPath, generatedProject, &error));
    generatedProject.context.sourceScenePath = managedWorkCellPath.toStdString();
    generatedProject.context.tcpFrame.clear();

    const auto addTask = [&generatedProject](
                             const char* id, const char* name,
                             const std::array<double, 3>& position) {
        rws::OptimizationTaskPoint task;
        task.point.id = id;
        task.point.name = name;
        task.point.refFrame = "WORLD";
        task.point.tcpFrame.clear();
        task.point.position = position;
        task.point.rpyDeg = {{180.0, 0.0, 0.0}};
        task.point.enabled = true;
        task.required = true;
        generatedProject.tasks.push_back(task);
        generatedProject.context.taskPoints.push_back(task.point);
    };
    addTask("acceptance-task-1", "UR acceptance task 1", {{0.3, -0.2, 0.4}});
    addTask("acceptance-task-2", "UR acceptance task 2", {{0.35, 0.0, 0.45}});
    addTask("acceptance-task-3", "UR acceptance task 3", {{0.3, 0.2, 0.4}});

    const auto makeVariable = [](
                                  const char* id, const char* label,
                                  const char* target,
                                  rws::StructureVariableKind kind,
                                  double currentValue, double minimum,
                                  double maximum, double step) {
        rws::StructureDesignVariable variable;
        variable.id = id;
        variable.label = label;
        variable.targetName = target;
        variable.unit = "m";
        variable.kind = kind;
        variable.currentValue = currentValue;
        variable.minimum = minimum;
        variable.maximum = maximum;
        variable.step = step;
        variable.enabled = true;
        return variable;
    };
    generatedProject.variables = {
        makeVariable("Joint2_pos_x", "Joint2 X", "Joint2",
                     rws::StructureVariableKind::JointPositionX,
                     -0.425, -0.48, -0.37, 0.005),
        makeVariable("Joint3_pos_x", "Joint3 X", "Joint3",
                     rws::StructureVariableKind::JointPositionX,
                     -0.39243, -0.44, -0.34, 0.005),
        makeVariable("Joint1_pos_z", "Joint1 Z", "Joint1",
                     rws::StructureVariableKind::JointPositionZ,
                     0.0892, 0.07, 0.11, 0.002)};
    generatedProject.evaluation.coverageBox.enabled = true;
    generatedProject.evaluation.coverageBox.minimum = {{-2.0, -2.0, -2.0}};
    generatedProject.evaluation.coverageBox.maximum = {{2.0, 2.0, 2.0}};
    generatedProject.evaluation.coverageBox.cells = {{2, 2, 2}};
    generatedProject.evaluation.quickWorkspace.sampleCount = 8;
    generatedProject.evaluation.verifiedWorkspace.sampleCount = 8;
    generatedProject.run.strategy = rws::StructureStrategyKind::Hybrid;
    generatedProject.run.candidateCount = 8;
    generatedProject.run.eliteCount = 3;
    generatedProject.run.localEliteCount = 2;
    generatedProject.run.finalVerificationCount = 2;
    generatedProject.run.maxLocalSweeps = 2;
    generatedProject.run.gridSteps = 3;
    generatedProject.run.randomSeed = 20260727u;
    generatedProject.objectives =
        rws::StructureOptimizationObjectiveProfile::legacyObjectives(
            generatedProject.weights);

    const QString projectPath = QDir(projectDirectory).filePath(
        "optimizations/UR-6-85-5-A.structure-optimization.json");
    REQUIRE(QDir().mkpath(QFileInfo(projectPath).absolutePath()));
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        projectPath, generatedProject, -1, &projectError));

    rws::RobotModelSpec persistedModelSpec;
    QFile persistedModelFile(modelPath);
    REQUIRE(persistedModelFile.open(QIODevice::ReadOnly));
    if (persistedModelFile.isOpen()) {
        REQUIRE(rws::RobotModelSpecJson::fromJson(
            persistedModelFile.readAll().toStdString(),
            persistedModelSpec, &error));
        persistedModelFile.close();
    }

    rws::StructureOptimizationProblem project;
    int selectedCandidateIndex = -1;
    const bool loaded = rws::StructureOptimizationProjectAdapter::loadProject(
        projectPath, project, &selectedCandidateIndex, &projectError);
    REQUIRE(loaded);
    if (loaded) {
        const std::string sourceFingerprint =
            rws::RobotModelFingerprint::canonicalSha256(persistedModelSpec);
        REQUIRE(project.context.robotName == "UR-6-85-5-A");
        REQUIRE(project.context.modelSpec.robotName == persistedModelSpec.robotName);
        REQUIRE(QDir::cleanPath(QString::fromStdString(project.context.modelSpec.saveDirectory)) ==
                QDir::cleanPath(modelDirectory));
        REQUIRE(project.context.modelSpec.transformJoints.size() ==
                persistedModelSpec.transformJoints.size());
        REQUIRE(!project.context.modelProvenance.sourceFingerprint.empty());
        REQUIRE(project.context.modelProvenance.sourceFingerprint == sourceFingerprint);
        REQUIRE(project.context.modelProvenance.snapshotFingerprint == sourceFingerprint);
        REQUIRE(rws::RobotModelStalenessChecker::check(project.context, projectPath).status ==
                rws::RobotModelSourceStatus::Current);
        REQUIRE(project.tasks.size() == 3);
        REQUIRE(project.tasks[0].point.tcpFrame.empty());
        REQUIRE(project.evaluation.coverageBox.enabled);
        REQUIRE(project.run.randomSeed == 20260727u);
        REQUIRE(project.variables.size() == 3);
        if (project.variables.size() == 3) {
            REQUIRE(project.variables[0].id == "Joint2_pos_x");
            REQUIRE(project.variables[1].id == "Joint3_pos_x");
            REQUIRE(project.variables[2].id == "Joint1_pos_z");
        }

        rws::KinematicEngineeringEvaluator evaluator(project);
        rws::EngineeringEvaluatorPipeline pipeline;
        pipeline.addEvaluator(evaluator);
        rws::StructureOptimizationCallbacks callbacks;
        callbacks.isCancellationRequested = []() { return false; };
        rws::SystemEngineeringOptimizer optimizer;
        const rws::StructureOptimizationResult first = optimizer.optimize(
            project, pipeline, callbacks);
        const rws::StructureOptimizationResult second = optimizer.optimize(
            project, pipeline, callbacks);

        REQUIRE(!first.canceled);
        REQUIRE(first.bestCandidateIndex >= 0);
        REQUIRE(first.bestCandidateIndex == second.bestCandidateIndex);
        REQUIRE(first.sensitivity.robustnessGrade != "Unknown");

        const rws::StructureCandidateResult* best = nullptr;
        for (const rws::StructureCandidateResult& candidate : first.candidates) {
            if (candidate.index == first.bestCandidateIndex) {
                best = &candidate;
                break;
            }
        }
        REQUIRE(best != nullptr);
        if (best != nullptr) {
            REQUIRE(best->feasible);
            REQUIRE(best->raw.requiredReachableCount == best->raw.requiredTaskCount);
            REQUIRE(best->raw.collisionFreeRate == 1.0);
            REQUIRE(best->raw.workspaceCoverage >= 0.01);
        }

        QTemporaryDir exportDirectory;
        REQUIRE(exportDirectory.isValid());
        rws::StructureOptimizationExportRequest request;
        request.directory = exportDirectory.filePath("ur-example-export");
        request.selectedCandidateIndex = first.bestCandidateIndex;
        request.exportCandidateModel = true;
        const rws::StructureOptimizationExportResult exported =
            rws::StructureOptimizationExportService::exportAll(project, first, request);
        REQUIRE(exported.ok);
        const QDir candidateDirectory(request.directory + "/candidate-" +
                                      QString::number(first.bestCandidateIndex));
        REQUIRE(!candidateDirectory.entryList(QStringList() << "*.wc.xml",
                                              QDir::Files).isEmpty());
    }

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 端到端验收(300kg 机器人文件):用 RobotModelBuilderPlugin 从 URDF 创建
// 项目、发布托管 WorkCell、冻结需求,验证项目整体移动后资源仍可解析、冻结工件
// 可重新导入为运动学任务与优化问题;全程校验源目录未被写回(只读源)。
static void testPortable300kgRobotFileProjectAcceptance()
{
    std::printf("testPortable300kgRobotFileProjectAcceptance ... ");

    const QString sourceRoot = QDir(QStringLiteral(STRUCTUREOPTIMIZER_TEST_SOURCE_DIR))
                                   .filePath(QStringLiteral(
                                       "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/"
                                       "300kg_urdf"));
    const QString sourceUrdf = QDir(sourceRoot).filePath(QStringLiteral("output/300kg.urdf"));
    const auto treeSnapshot = [](const QString& root) {
        QMap<QString, QByteArray> result;
        QDirIterator iterator(root, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                result.insert(QStringLiteral("!unreadable!") + path, QByteArray());
                continue;
            }
            result.insert(QDir(root).relativeFilePath(path),
                          QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
        }
        return result;
    };
    const QMap<QString, QByteArray> sourceBefore = treeSnapshot(sourceRoot);
    REQUIRE(QFileInfo(sourceUrdf).isFile());
    REQUIRE(!sourceBefore.isEmpty());

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString originalRoot = directory.filePath(QStringLiteral("RobotProject"));
    const QString originalProject = QDir(originalRoot).filePath(QStringLiteral("300kg.rwproj"));
    REQUIRE(QDir().mkpath(originalRoot));
    QString modelPath;
    QString requirementPath;
    QString publishedWorkCellPath;
    QString error;

    {
        QString requirementDocument;
        rws::CallbackProjectDocumentProvider requirementProvider(
            QStringLiteral("acceptance.requirements"),
            QStringLiteral("rws.engineering-requirements"),
            [](const QString&, const rws::ProjectDocumentContext&, QString*) { return true; },
            [&requirementDocument](const QString& targetPath,
                                   const rws::ProjectDocumentContext&, QString* saveError) {
                QFile source(requirementDocument);
                QFile target(targetPath);
                if (!source.open(QIODevice::ReadOnly) ||
                    !target.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                    target.write(source.readAll()) < 0) {
                    if (saveError != nullptr)
                        *saveError = QStringLiteral("Could not stage the frozen requirement.");
                    return false;
                }
                return true;
            });
        rws::RobotModelBuilderPlugin builderPlugin;
        rw::core::PropertyMap properties;
        rws::RobWorkStudio studio(properties);
        REQUIRE(studio.registerProjectDocumentProvider(&requirementProvider, &error));
        builderPlugin.setRobWorkStudio(&studio);
        builderPlugin.initialize();
        rws::RobotModelBuilderWidget* builder =
            qobject_cast<rws::RobotModelBuilderWidget*>(builderPlugin.widget());
        REQUIRE(builder != nullptr);
        if (builder == nullptr)
            return;

        rws::RobotProjectImportCallbacks callbacks;
        callbacks.preflight = [&builderPlugin](const QString& path, const QString& root,
                                                QString* callbackError) {
            const QString result = builderPlugin.preflightRobotProjectSource(path, root);
            if (callbackError != nullptr)
                *callbackError = result;
            return result.isEmpty();
        };
        callbacks.commit = [&builderPlugin](const QString& path, const QString& root,
                                             QString* callbackError) {
            const QString result = builderPlugin.commitRobotProjectSource(path, root);
            if (callbackError != nullptr)
                *callbackError = result;
            return result.isEmpty();
        };
        const bool created = studio.createProjectFromRobotFilePaths(
            sourceUrdf, originalProject, callbacks, &error);
        if (!created)
            std::fprintf(stderr, "300kg project creation error: %s\n",
                         error.toStdString().c_str());
        REQUIRE(created);
        if (!created)
            return;

        rws::RobotModelSpec runtimeSpec = builder->currentModelSpec();
        REQUIRE(runtimeSpec.robotName == "300kg");
        const std::size_t movableJointCount = static_cast<std::size_t>(std::count_if(
            runtimeSpec.transformJoints.begin(), runtimeSpec.transformJoints.end(),
            [](const rws::JointTransformSpec& joint) {
                return rws::isMovable(rws::typeToKind(joint.type));
            }));
        REQUIRE(movableJointCount == 6);
        REQUIRE(runtimeSpec.drawables.size() == 7);
        REQUIRE(runtimeSpec.collisionModels.size() == 7);

        const auto pathInsideProject = [&originalRoot](const std::string& value) {
            const QString path = QFileInfo(QString::fromStdString(value)).absoluteFilePath();
            const QString relative = QDir::fromNativeSeparators(
                QDir(originalRoot).relativeFilePath(path));
            return QFileInfo(path).isFile() && relative != QStringLiteral("..") &&
                   !relative.startsWith(QStringLiteral("../")) &&
                   !QDir::isAbsolutePath(relative);
        };
        bool geometryContained = true;
        for (const rws::DrawableSpec& drawable : runtimeSpec.drawables)
            geometryContained = geometryContained && pathInsideProject(drawable.filePath);
        for (const rws::CollisionModelSpec& collision : runtimeSpec.collisionModels)
            geometryContained = geometryContained && pathInsideProject(collision.filePath);
        REQUIRE(geometryContained);

        REQUIRE(studio.saveCurrentProject(&error));
        REQUIRE(studio.resolveProjectResource(QStringLiteral("robot-model.main"), modelPath,
                                              &error));
        REQUIRE(QFileInfo(modelPath).isFile());

        rws::RobotModelPublishRequest publishRequest;
        publishRequest.spec = runtimeSpec;
        publishRequest.projectRoot = originalRoot;
        publishRequest.promote = [&studio](const QString& scene,
                                           const QStringList& dependencies,
                                           QString* promoteError) {
            return studio.promoteGeneratedWorkCell(scene, dependencies, promoteError);
        };
        const bool published = rws::RobotModelPublishService::publishAndLoad(
            publishRequest, &error);
        if (!published)
            std::fprintf(stderr, "300kg publication error: %s\n", error.toStdString().c_str());
        REQUIRE(published);
        if (!published) {
            REQUIRE(treeSnapshot(sourceRoot) == sourceBefore);
            std::printf("FAILED (%d)\n", g_testFailures);
            return;
        }

        REQUIRE(!studio.mainWorkCellResourceId().isEmpty());
        REQUIRE(studio.resolveProjectResource(studio.mainWorkCellResourceId(),
                                              publishedWorkCellPath, &error));
        REQUIRE(QFileInfo(publishedWorkCellPath).isFile());
        REQUIRE(studio.getWorkcell() != nullptr);
        REQUIRE(studio.getWorkcell()->getDevices().size() == 1);

        rws::RobotModelBuilderWidget persistedBuilder;
        persistedBuilder.setProjectOutputDirectory(originalRoot);
        REQUIRE(persistedBuilder.loadProjectDocument(modelPath, &error));
        runtimeSpec = persistedBuilder.currentModelSpec();

        const rw::models::WorkCell::Ptr workcell = studio.getWorkcell();
        const rw::kinematics::State state = workcell->getDefaultState();
        const rw::models::Device::Ptr device = workcell->getDevices().front();
        rws::RequirementSet requirements;
        requirements.name = "300kg acceptance requirement";
        requirements.frozen = true;
        requirementPath = QDir(originalRoot).filePath(
            QStringLiteral("requirements/main.requirements.json"));
        REQUIRE(QDir().mkpath(QFileInfo(requirementPath).absolutePath()));
        requirements.modelBinding.sourcePath =
            QDir(QFileInfo(requirementPath).absolutePath())
                .relativeFilePath(modelPath).toStdString();
        requirements.modelBinding.robotName = runtimeSpec.robotName;
        requirements.modelBinding.robotModelFingerprint =
            rws::RobotModelFingerprint::canonicalSha256(runtimeSpec);
        rws::PoseTask task;
        task.id = "home";
        task.name = "300kg home pose";
        task.level = rws::RequirementLevel::Must;
        task.refFrame = "WORLD";
        task.tcpFrame = device->getEnd()->getName();
        const rw::math::Transform3D<> worldTtcp =
            rw::kinematics::Kinematics::worldTframe(device->getEnd(), state);
        task.position = {{worldTtcp.P()[0], worldTtcp.P()[1], worldTtcp.P()[2]}};
        requirements.poseTasks.push_back(task);

        rws::FrozenRequirementArtifact artifact;
        std::string domainError;
        const bool frozen = rws::RequirementFreezer::freeze(
            requirements, *workcell, state, runtimeSpec, artifact, &domainError,
            originalRoot.toStdString());
        if (!frozen)
            std::fprintf(stderr, "300kg freeze error: %s\n", domainError.c_str());
        REQUIRE(frozen);

        QJsonObject requirementObject = rws::RequirementSetJson::toObject(requirements);
        requirementObject.insert(QStringLiteral("frozenArtifact"),
                                 rws::FrozenRequirementArtifactJson::toObject(artifact));
        QFile requirementFile(requirementPath);
        REQUIRE(requirementFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray requirementJson = QJsonDocument(requirementObject).toJson();
        REQUIRE(requirementFile.write(requirementJson) == requirementJson.size());
        requirementFile.close();
        requirementDocument = requirementPath;

        std::vector<rws::TaskPoint> kinematicTasks;
        REQUIRE(rws::FrozenRequirementKinematicAdapter::apply(
            artifact, *workcell, state, kinematicTasks, &domainError,
            originalRoot.toStdString()));
        REQUIRE(kinematicTasks.size() == 1);

        rws::StructureOptimizationProblem optimizationProblem;
        rws::FrozenRequirementValidationResult validation;
        const bool optimizationImported =
            rws::FrozenRequirementProjectImportService::createProblem(
            requirementPath, *workcell, state, optimizationProblem, &validation,
            &domainError, originalRoot);
        if (!optimizationImported)
            std::fprintf(stderr, "300kg optimization import error: %s\n", domainError.c_str());
        REQUIRE(optimizationImported);
        REQUIRE(optimizationProblem.tasks.size() == 1);

        rws::ProjectResource requirementResource;
        requirementResource.id = QStringLiteral("engineering-requirements.main");
        requirementResource.kind = QStringLiteral("rws.engineering-requirements");
        requirementResource.path = QDir(originalRoot).relativeFilePath(requirementPath);
        requirementResource.ownership = QStringLiteral("generated");
        requirementResource.required = true;
        requirementResource.dependencies = {QStringLiteral("robot-model.main"),
                                            studio.mainWorkCellResourceId()};
        REQUIRE(studio.ensureGeneratedProjectResource(requirementResource, nullptr, &error));
        REQUIRE(studio.saveCurrentProject(&error));
    }

    const QString movedRoot = directory.filePath(QStringLiteral("RobotProjectMoved"));
    REQUIRE(QDir().rename(originalRoot, movedRoot));
    const QString movedProject = QDir(movedRoot).filePath(QStringLiteral("300kg.rwproj"));
    QTemporaryDir hostileDirectory;
    REQUIRE(hostileDirectory.isValid());
    const QString previousCwd = QDir::currentPath();
    REQUIRE(QDir::setCurrent(hostileDirectory.path()));

    rws::ProjectManager reopened;
    REQUIRE(reopened.openProject(movedProject, &error));
    QString movedSource;
    QString movedModel;
    QString movedWorkCell;
    QString movedRequirements;
    REQUIRE(reopened.resolveResource(QStringLiteral("robot-source.main"), movedSource, &error));
    REQUIRE(reopened.resolveResource(QStringLiteral("robot-model.main"), movedModel, &error));
    REQUIRE(reopened.resolveResource(
        reopened.manifest().entryPoints.value(QStringLiteral("mainWorkCell")),
        movedWorkCell, &error));
    REQUIRE(reopened.resolveResource(QStringLiteral("engineering-requirements.main"),
                                     movedRequirements, &error));
    REQUIRE(movedSource.startsWith(QDir::cleanPath(movedRoot)));
    REQUIRE(movedModel.startsWith(QDir::cleanPath(movedRoot)));
    REQUIRE(movedWorkCell.startsWith(QDir::cleanPath(movedRoot)));
    REQUIRE(movedRequirements.startsWith(QDir::cleanPath(movedRoot)));

    rws::RobotModelBuilderWidget movedBuilder;
    movedBuilder.setProjectOutputDirectory(movedRoot);
    REQUIRE(movedBuilder.loadProjectDocument(movedModel, &error));
    const rws::RobotModelSpec movedSpec = movedBuilder.currentModelSpec();
    REQUIRE(movedSpec.drawables.size() == 7);
    const rw::models::WorkCell::Ptr movedCell =
        rw::loaders::WorkCellLoader::Factory::load(movedWorkCell.toStdString());
    REQUIRE(!movedCell.isNull());
    if (!movedCell.isNull()) {
        QFile requirementFile(movedRequirements);
        REQUIRE(requirementFile.open(QIODevice::ReadOnly));
        const QJsonDocument requirementJson = QJsonDocument::fromJson(requirementFile.readAll());
        requirementFile.close();
        rws::FrozenRequirementArtifact movedArtifact;
        std::string domainError;
        REQUIRE(rws::FrozenRequirementKinematicAdapter::parseArtifactJson(
            requirementJson.object(), movedArtifact, &domainError));
        std::vector<rws::TaskPoint> movedTasks;
        REQUIRE(rws::FrozenRequirementKinematicAdapter::apply(
            movedArtifact, *movedCell, movedCell->getDefaultState(), movedTasks, &domainError,
            movedRoot.toStdString()));
        REQUIRE(movedTasks.size() == 1);
        rws::StructureOptimizationProblem movedProblem;
        rws::FrozenRequirementValidationResult validation;
        const bool movedOptimizationImported =
            rws::FrozenRequirementProjectImportService::createProblem(
            movedRequirements, *movedCell, movedCell->getDefaultState(), movedProblem,
            &validation, &domainError, movedRoot);
        if (!movedOptimizationImported)
            std::fprintf(stderr, "Moved 300kg optimization import error: %s\n",
                         domainError.c_str());
        REQUIRE(movedOptimizationImported);
        REQUIRE(movedProblem.tasks.size() == 1);
    }
    REQUIRE(QDir::setCurrent(previousCwd));
    REQUIRE(treeSnapshot(sourceRoot) == sourceBefore);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 优化器项目资源依赖:首次创建时插件生成 structure-optimization.main 资源
// 并挂接当前可解析的上游(scene.main/robot-model.main/engineering-requirements.main);
// 对已存在的旧资源,保存时去重并保留非空依赖,不能重复添加自身或残留脏依赖。
static void testStructureOptimizerResourceDependencies()
{
    std::printf("testStructureOptimizerResourceDependencies ... ");

    // First creation remains an independent path: no legacy resource exists, so the plugin must
    // create its default resource and attach exactly the currently resolvable upstream IDs.
    {
        QTemporaryDir creationDirectory;
        REQUIRE(creationDirectory.isValid());
        const QString creationProjectPath = creationDirectory.filePath(
            "CreationProject/CreationProject.rwproj");
        const QString creationProjectDirectory =
            QFileInfo(creationProjectPath).absolutePath();
        const QString creationModelPath = QDir(creationProjectDirectory).filePath(
            "generated/robot-models/main.rmb.json");
        const QString creationRequirementsPath = QDir(creationProjectDirectory).filePath(
            "requirements/main.json");
        REQUIRE(QDir().mkpath(QFileInfo(creationModelPath).absolutePath()));
        REQUIRE(QDir().mkpath(QFileInfo(creationRequirementsPath).absolutePath()));
        const auto writeCreationFile = [](const QString& path, const QByteArray& data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                   file.write(data) == data.size();
        };

        QString creationError;
        rws::ProjectManager creationManager;
        REQUIRE(creationManager.createProjectFromWorkCell(
            creationProjectPath,
            sourcePath("RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml"),
            &creationError));
        REQUIRE(writeCreationFile(creationModelPath, "{}\n"));
        REQUIRE(writeCreationFile(creationRequirementsPath, "{}\n"));
        const auto addCreationResource = [&creationManager, &creationError](
                                             const QString& id, const QString& kind,
                                             const QString& path) {
            rws::ProjectResource resource;
            resource.id = id;
            resource.kind = kind;
            resource.path = path;
            resource.ownership = "generated";
            resource.required = false;
            return creationManager.addGeneratedResource(resource, &creationError);
        };
        REQUIRE(addCreationResource("robot-model.main", "rws.robot-model",
                                    "generated/robot-models/main.rmb.json"));
        REQUIRE(addCreationResource("engineering-requirements.main",
                                    "rws.engineering-requirements",
                                    "requirements/main.json"));
        REQUIRE(creationManager.saveProject(&creationError));
        creationManager.closeProject();

        const auto loadDocument = [](const QString&, const rws::ProjectDocumentContext&,
                                     QString*) { return true; };
        const auto saveDocument = [](const QString&, const rws::ProjectDocumentContext&,
                                     QString*) { return true; };
        rws::CallbackProjectDocumentProvider robotModelProvider(
            "creation.robot-model-provider", "rws.robot-model", loadDocument, saveDocument);
        rws::CallbackProjectDocumentProvider requirementProvider(
            "creation.requirement-provider", "rws.engineering-requirements",
            loadDocument, saveDocument);
        rw::core::PropertyMap creationProperties;
        rws::RobWorkStudio creationStudio(creationProperties);
        REQUIRE(creationStudio.registerProjectDocumentProvider(
            &robotModelProvider, &creationError));
        REQUIRE(creationStudio.registerProjectDocumentProvider(
            &requirementProvider, &creationError));
        rws::StructureOptimizerPlugin creationPlugin;
        creationPlugin.setRobWorkStudio(&creationStudio);
        creationPlugin.initialize();
        creationStudio.openFile(creationProjectPath.toStdString());

        rws::StructureOptimizationProblem creationProblem;
        std::string creationFactoryError;
        REQUIRE(rws::StructureOptimizationProjectFactory::create(
            rws::RobotModelXmlWriter::makeDefaultSixAxisModel(creationDirectory.path()),
            creationProblem, &creationFactoryError));
        rws::StructureOptimizerWidget* creationWidget =
            qobject_cast<rws::StructureOptimizerWidget*>(creationPlugin.widget());
        REQUIRE(creationWidget != nullptr);
        if (creationWidget != nullptr) {
            creationWidget->setProblem(creationProblem);
            REQUIRE(QMetaObject::invokeMethod(
                creationWidget, "projectDocumentChanged", Qt::DirectConnection));
        }
        REQUIRE(creationStudio.saveCurrentProject(&creationError));

        rws::ProjectManager creationVerification;
        REQUIRE(creationVerification.openProject(creationProjectPath, &creationError));
        rws::ProjectResource createdOptimization;
        REQUIRE(creationVerification.manifest().findResource(
            "structure-optimization.main", createdOptimization));
        REQUIRE(createdOptimization.kind == "rws.structure-optimization");
        REQUIRE(createdOptimization.path ==
                "optimizations/main.structure-optimization.json");
        REQUIRE(createdOptimization.ownership == "generated");
        REQUIRE(createdOptimization.required);
        REQUIRE(createdOptimization.dependencies == QStringList({
            "scene.main", "robot-model.main", "engineering-requirements.main"}));
        creationStudio.close();
        creationPlugin.setRobWorkStudio(nullptr);
    }

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectPath =
        directory.filePath("DependencyProject/DependencyProject.rwproj");
    const QString sourceWorkCell = sourcePath(
        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml");
    const QString projectDirectory = QFileInfo(projectPath).absolutePath();
    const QString modelPath =
        QDir(projectDirectory).filePath("generated/robot-models/main.rmb.json");
    const QString requirementsPath =
        QDir(projectDirectory).filePath("requirements/main.json");
    REQUIRE(QDir().mkpath(QFileInfo(modelPath).absolutePath()));
    REQUIRE(QDir().mkpath(QFileInfo(requirementsPath).absolutePath()));

    const auto writeFile = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               file.write(data) == data.size();
    };
    QString error;
    rws::ProjectManager manager;
    REQUIRE(manager.createProjectFromWorkCell(projectPath, sourceWorkCell, &error));
    REQUIRE(writeFile(modelPath, "{}\n"));
    REQUIRE(writeFile(requirementsPath, "{}\n"));

    const auto addResource = [&manager, &error](const QString& id, const QString& kind,
                                                const QString& path) {
        rws::ProjectResource resource;
        resource.id = id;
        resource.kind = kind;
        resource.path = path;
        resource.ownership = "generated";
        resource.required = false;
        return manager.addGeneratedResource(resource, &error);
    };
    REQUIRE(addResource("robot-model.main", "rws.robot-model",
                        "generated/robot-models/main.rmb.json"));
    REQUIRE(addResource("engineering-requirements.main", "rws.engineering-requirements",
                        "requirements/main.json"));
    REQUIRE(addResource("legacy.upstream", "legacy.upstream",
                        "requirements/legacy-upstream.json"));
    REQUIRE(writeFile(QDir(projectDirectory).filePath("requirements/legacy-upstream.json"),
                      "{}\n"));

    rws::StructureOptimizationProblem legacyProblem;
    std::string legacyFactoryError;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(projectDirectory),
        legacyProblem, &legacyFactoryError));
    const QString optimizationPath = QDir(projectDirectory).filePath(
        "legacy/legacy.structure-optimization.json");
    REQUIRE(QDir().mkpath(QFileInfo(optimizationPath).absolutePath()));
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        optimizationPath, legacyProblem, -1, &error));
    rws::ProjectResource legacyOptimization;
    legacyOptimization.id = "structure-optimization.main";
    legacyOptimization.kind = "rws.structure-optimization";
    legacyOptimization.path = "legacy/legacy.structure-optimization.json";
    legacyOptimization.ownership = "project";
    legacyOptimization.required = false;
    legacyOptimization.dependencies = {"scene.main", "legacy.upstream", "scene.main"};
    REQUIRE(manager.addGeneratedResource(legacyOptimization, &error));
    REQUIRE(manager.saveProject(&error));
    manager.closeProject();

    rws::StructureOptimizerPlugin plugin;
    const auto loadDocument = [](const QString&, const rws::ProjectDocumentContext&,
                                 QString*) { return true; };
    const auto saveDocument = [](const QString&, const rws::ProjectDocumentContext&,
                                 QString*) { return true; };
    rws::CallbackProjectDocumentProvider robotModelProvider(
        "test.robot-model-provider", "rws.robot-model", loadDocument, saveDocument);
    rws::CallbackProjectDocumentProvider requirementProvider(
        "test.requirement-provider", "rws.engineering-requirements",
        loadDocument, saveDocument);
    rws::CallbackProjectDocumentProvider legacyProvider(
        "test.legacy-provider", "legacy.upstream", loadDocument, saveDocument);
    rw::core::PropertyMap properties;
    rws::RobWorkStudio studio(properties);
    REQUIRE(studio.registerProjectDocumentProvider(&robotModelProvider, &error));
    REQUIRE(studio.registerProjectDocumentProvider(&requirementProvider, &error));
    REQUIRE(studio.registerProjectDocumentProvider(&legacyProvider, &error));
    plugin.setRobWorkStudio(&studio);
    plugin.initialize();
    studio.openFile(projectPath.toStdString());
    REQUIRE(!studio.projectDirectory().isEmpty());

    REQUIRE(qobject_cast<rws::StructureOptimizerWidget*>(plugin.widget()) != nullptr);
    REQUIRE(studio.saveCurrentProject(&error));

    rws::ProjectManager verificationManager;
    REQUIRE(verificationManager.openProject(projectPath, &error));
    rws::ProjectResource optimizationResource;
    REQUIRE(verificationManager.manifest().findResource(
        "structure-optimization.main", optimizationResource));
    const QSet<QString> uniqueDependencies(optimizationResource.dependencies.begin(),
                                           optimizationResource.dependencies.end());
    REQUIRE(optimizationResource.dependencies.size() == 3);
    REQUIRE(uniqueDependencies.size() == optimizationResource.dependencies.size());
    REQUIRE(uniqueDependencies.contains("scene.main"));
    REQUIRE(uniqueDependencies.contains("robot-model.main"));
    REQUIRE(uniqueDependencies.contains("engineering-requirements.main"));
    REQUIRE(optimizationResource.kind == legacyOptimization.kind);
    REQUIRE(optimizationResource.path == legacyOptimization.path);
    REQUIRE(optimizationResource.ownership == legacyOptimization.ownership);
    REQUIRE(optimizationResource.required == legacyOptimization.required);

    studio.close();
    plugin.setRobWorkStudio(nullptr);
    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 候选预览控制器:验证 preview 把候选模型导出并交给宿主打开
// (host.current 变为新 WC),clearPreview 恢复原 WC,且记录被预览的候选索引。
static void testCandidatePreviewController()
{
    std::printf("testCandidatePreviewController ... ");

    struct FakeHost : rws::IWorkCellPreviewHost {
        QString current = "original.wc.xml";
        QString lastOpened;
        bool openWorkCell(const QString& path, QString*) override {
            lastOpened = path;
            current = path;
            return true;
        }
        QString currentWorkCellPath() override { return current; }
    } host;

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;
    rws::StructureCandidateResult candidate;
    candidate.index = 3;
    candidate.feasible = true;
    candidate.status = rws::StructureCandidateStatus::Feasible;

    rws::CandidatePreviewController preview(&host);
    QString error;
    const bool previewed = preview.preview(problem, candidate, &error);
    if (!previewed)
        std::fprintf(stderr, "  Preview error: %s\n", error.toStdString().c_str());
    REQUIRE(previewed);
    REQUIRE(preview.previewedCandidateIndex() == 3);
    REQUIRE(host.current != "original.wc.xml");
    preview.clearPreview();
    REQUIRE(host.current == "original.wc.xml");

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 优化器 Widget 状态:验证五个页签与关键按钮存在、任务/约束表的增删复制、
// collectProblem 的字段保真;项目文档脏状态与 markClean 事务;模型快照过期/托管
// 场景的状态文案;非法模型规格禁用启动按钮。
static void testStructureOptimizerWidgetState()
{
    std::printf("testStructureOptimizerWidgetState ... ");

    rws::StructureOptimizerWidget widget;

    QTabWidget* tabs = widget.findChild<QTabWidget*>("structureOptimizerTabs");
    REQUIRE(tabs != nullptr);
    if (tabs != nullptr) {
        REQUIRE(tabs->count() == 5);
        REQUIRE(tabs->tabText(0).toStdString() == "设计变量");
        REQUIRE(tabs->tabText(1).toStdString() == "任务与约束");
        REQUIRE(tabs->tabText(2).toStdString() == "优化设置");
        REQUIRE(tabs->tabText(3).toStdString() == "候选方案");
        REQUIRE(tabs->tabText(4).toStdString() == "报告导出");
    }

    QPushButton* startButton =
        widget.findChild<QPushButton*>("startOptimizationButton");
    REQUIRE(startButton != nullptr);
    if (startButton != nullptr)
        REQUIRE(!startButton->isEnabled());
    REQUIRE(widget.findChild<QPushButton*>(
                "newStructureOptimizationProjectFromModelButton") != nullptr);
    // 需求定义插件冻结后的工件必须有明确入口，避免工程师重新手工录入已经校验的任务点。
    REQUIRE(widget.findChild<QPushButton*>(
                "newStructureOptimizationProjectFromFrozenRequirementButton") != nullptr);

    rws::StructureOptimizationProblem emptyTaskProject;
    std::string factoryError;
    const rws::RobotModelSpec factorySpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        factorySpec, emptyTaskProject, &factoryError));
    REQUIRE(emptyTaskProject.tasks.empty());
    widget.setProblem(emptyTaskProject);
    if (startButton != nullptr)
        REQUIRE(!startButton->isEnabled());

    QTableView* taskView = widget.findChild<QTableView*>("optimizationTaskTable");
    QPushButton* addTaskButton =
        widget.findChild<QPushButton*>("addOptimizationTaskButton");
    REQUIRE(taskView != nullptr);
    REQUIRE(addTaskButton != nullptr);
    if (taskView != nullptr && addTaskButton != nullptr) {
        REQUIRE(taskView->model()->rowCount() == 0);
        addTaskButton->click();
        REQUIRE(taskView->model()->rowCount() == 1);
        REQUIRE(taskView->model()->columnCount() == 13);
        REQUIRE(taskView->model()->data(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::IdColumn))
                    .toString().startsWith("task_"));
        REQUIRE(taskView->model()->data(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::RefFrameColumn))
                    .toString() == "WORLD");
        REQUIRE(taskView->model()->data(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::TcpFrameColumn))
                    .toString().isEmpty());
        REQUIRE(taskView->model()->setData(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::RollColumn),
                    15.0));
        REQUIRE(taskView->model()->setData(
                    taskView->model()->index(0, rws::OptimizationTaskTableModel::TcpFrameColumn),
                    "Joint5"));
        const rws::StructureOptimizationProblem edited = widget.collectProblem();
        REQUIRE(edited.tasks[0].point.rpyDeg[0] == 15.0);
        REQUIRE(edited.tasks[0].point.tcpFrame == "Joint5");

        QPushButton* duplicateTaskButton =
            widget.findChild<QPushButton*>("duplicateOptimizationTaskButton");
        QPushButton* removeTaskButton =
            widget.findChild<QPushButton*>("removeOptimizationTaskButton");
        REQUIRE(duplicateTaskButton != nullptr);
        REQUIRE(removeTaskButton != nullptr);
        if (duplicateTaskButton != nullptr && removeTaskButton != nullptr) {
            taskView->selectRow(0);
            duplicateTaskButton->click();
            REQUIRE(taskView->model()->rowCount() == 2);
            REQUIRE(taskView->model()->data(
                        taskView->model()->index(1, rws::OptimizationTaskTableModel::IdColumn))
                        .toString() != taskView->model()->data(
                        taskView->model()->index(0, rws::OptimizationTaskTableModel::IdColumn))
                        .toString());
            removeTaskButton->click();
            REQUIRE(taskView->model()->rowCount() == 1);
        }
    }

    QTableView* constraintView =
        widget.findChild<QTableView*>("structureConstraintTable");
    QPushButton* addConstraintButton =
        widget.findChild<QPushButton*>("addStructureConstraintButton");
    REQUIRE(constraintView != nullptr);
    REQUIRE(addConstraintButton != nullptr);
    if (constraintView != nullptr && addConstraintButton != nullptr) {
        REQUIRE(constraintView->model()->rowCount() == 0);
        addConstraintButton->click();
        REQUIRE(constraintView->model()->rowCount() == 1);
        QComboBox* constraintKindCombo =
            widget.findChild<QComboBox*>("newStructureConstraintKindCombo");
        REQUIRE(constraintKindCombo != nullptr);
        if (constraintKindCombo != nullptr) {
            constraintKindCombo->setCurrentIndex(constraintKindCombo->findData(
                static_cast<int>(rws::StructureConstraintKind::MinimumJointMargin)));
            addConstraintButton->click();
            REQUIRE(constraintView->model()->rowCount() == 2);
            REQUIRE(constraintView->model()->data(
                        constraintView->model()->index(
                            1, rws::StructureConstraintTableModel::KindColumn))
                        .toString() == "最小关节裕度");
            REQUIRE(constraintView->model()->data(
                        constraintView->model()->index(
                            1, rws::StructureConstraintTableModel::ThresholdColumn))
                        .toDouble() == 0.01);
        }

        QPushButton* duplicateConstraintButton =
            widget.findChild<QPushButton*>("duplicateStructureConstraintButton");
        QPushButton* removeConstraintButton =
            widget.findChild<QPushButton*>("removeStructureConstraintButton");
        REQUIRE(duplicateConstraintButton != nullptr);
        REQUIRE(removeConstraintButton != nullptr);
        if (duplicateConstraintButton != nullptr && removeConstraintButton != nullptr) {
            constraintView->selectRow(0);
            duplicateConstraintButton->click();
            REQUIRE(constraintView->model()->rowCount() == 3);
            removeConstraintButton->click();
            REQUIRE(constraintView->model()->rowCount() == 2);
        }
    }

    rws::StructureOptimizationProblem problem;
    problem.context.modelSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    problem.context.robotName = problem.context.modelSpec.robotName;
    problem.context.deviceName = problem.context.modelSpec.robotName;

    rws::StructureDesignVariable variable;
    variable.id = "joint1_z";
    variable.label = "Joint1 Z";
    variable.targetName = problem.context.modelSpec.transformJoints[0].name;
    variable.kind = rws::StructureVariableKind::JointPositionZ;
    variable.currentValue = problem.context.modelSpec.transformJoints[0].pos[2];
    variable.minimum = 0.1;
    variable.maximum = 0.5;
    variable.step = 0.001;
    variable.enabled = true;
    problem.variables.push_back(variable);

    rws::OptimizationTaskPoint task;
    task.point.id = "target";
    task.point.name = "Target";
    task.point.enabled = true;
    task.required = true;
    problem.tasks.push_back(task);
    problem.constraints.push_back({"coverage", "Coverage", "workcell",
                                   rws::StructureConstraintKind::MinimumWorkspaceCoverage,
                                   0.8, 0.0, true, true});
    problem.run.strategy = rws::StructureStrategyKind::Grid;
    problem.run.localEliteCount = 4;
    problem.run.finalVerificationCount = 2;
    problem.run.maxLocalSweeps = 17;
    problem.run.gridSteps = 6;
    problem.weights.reachability = 0.30;
    problem.weights.preference = 0.10;

    widget.setProblem(problem);
    if (startButton != nullptr)
        REQUIRE(startButton->isEnabled());

    // Provider 使用 Widget 的规范 JSON 快照判断脏状态。加载后必须干净，修改一个
    // 已持久化任务字段后变脏；只有模拟保存事务完整提交后的 markClean 才恢复干净。
    QTemporaryDir projectDocumentDirectory;
    REQUIRE(projectDocumentDirectory.isValid());
    const QString projectDocument = projectDocumentDirectory.filePath("optimization.json");
    QString projectDocumentError;
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        projectDocument, problem, -1, &projectDocumentError));
    REQUIRE(widget.loadProjectDocument(projectDocument, &projectDocumentError));
    REQUIRE(!widget.isProjectDocumentDirty());
    if (taskView != nullptr) {
        REQUIRE(taskView->model()->setData(
            taskView->model()->index(0, rws::OptimizationTaskTableModel::RollColumn), 22.0));
        REQUIRE(widget.isProjectDocumentDirty());
        REQUIRE(widget.saveProjectDocument(projectDocument, &projectDocumentError));
        widget.markProjectDocumentClean();
        REQUIRE(!widget.isProjectDocumentDirty());
    }
    REQUIRE(widget.canCloseProjectDocument(&projectDocumentError));

    QTemporaryDir provenanceDirectory;
    REQUIRE(provenanceDirectory.isValid());
    rws::RobotModelSpec sourceSpec =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(provenanceDirectory.path());
    sourceSpec.robotName = "StaleWidgetRobot";
    QStringList sourceSaveErrors;
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(sourceSpec, sourceSaveErrors));
    const QString sourceModelPath =
        rws::RobotModelXmlWriter::specSidecarFilePath(sourceSpec);
    rws::StructureOptimizationProblem staleProblem;
    std::string staleFactoryError;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        sourceSpec, sourceModelPath, staleProblem, &staleFactoryError));
    staleProblem.variables = problem.variables;
    staleProblem.tasks = problem.tasks;
    staleProblem.constraints = problem.constraints;
    staleProblem.run = problem.run;
    staleProblem.weights = problem.weights;
    staleProblem.objectives = problem.objectives;

    rws::RobotModelSpec changedSourceSpec = sourceSpec;
    changedSourceSpec.transformJoints.at(1).pos[0] += 0.001;
    sourceSaveErrors.clear();
    REQUIRE(rws::RobotModelXmlWriter::saveSpecSidecar(changedSourceSpec, sourceSaveErrors));
    widget.setProblem(staleProblem);
    if (startButton != nullptr)
        REQUIRE(startButton->isEnabled());
    REQUIRE(widget.statusText().contains(QString::fromUtf8("模型快照已过期")));

    QTemporaryDir managedStatusDirectory;
    REQUIRE(managedStatusDirectory.isValid());
    const QString managedStatusRoot = managedStatusDirectory.filePath("ManagedProject");
    const QString managedStatusAsset =
        QDir(managedStatusRoot).filePath("assets/base.stl");
    REQUIRE(QDir().mkpath(QFileInfo(managedStatusAsset).absolutePath()));
    QFile managedStatusAssetFile(managedStatusAsset);
    REQUIRE(managedStatusAssetFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(managedStatusAssetFile.write("solid managed\nendsolid managed\n") > 0);
    managedStatusAssetFile.close();
    rws::RobotModelSpec managedPortable =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(managedStatusRoot);
    managedPortable.robotName = "ManagedWidgetStatus";
    managedPortable.saveDirectory.clear();
    managedPortable.drawables.clear();
    managedPortable.collisionModels.clear();
    managedPortable.sceneGeometries.clear();
    rws::DrawableSpec managedDrawable;
    managedDrawable.name = "managed-widget-geometry";
    managedDrawable.refFrame = "WORLD";
    managedDrawable.shape = "Mesh";
    managedDrawable.filePath = "assets/base.stl";
    managedPortable.drawables.push_back(managedDrawable);
    rws::RobotModelSpec managedRuntime;
    QString managedPathError;
    REQUIRE(rws::RobotModelProjectPaths::resolveManaged(
        managedPortable, managedStatusRoot, managedRuntime, &managedPathError));
    const QString managedStatusSource =
        QDir(managedStatusRoot).filePath("models/main.rmb.json");
    REQUIRE(QDir().mkpath(QFileInfo(managedStatusSource).absolutePath()));
    QFile managedStatusSourceFile(managedStatusSource);
    REQUIRE(managedStatusSourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray managedStatusJson =
        QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(managedPortable));
    REQUIRE(managedStatusSourceFile.write(managedStatusJson) == managedStatusJson.size());
    managedStatusSourceFile.close();
    rws::StructureOptimizationProblem managedStatusProblem;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        managedRuntime, managedStatusSource, managedStatusProblem, &staleFactoryError));
    managedStatusProblem.variables = problem.variables;
    managedStatusProblem.tasks = problem.tasks;
    managedStatusProblem.constraints = problem.constraints;
    managedStatusProblem.run = problem.run;
    managedStatusProblem.weights = problem.weights;
    managedStatusProblem.objectives = problem.objectives;
    const QString managedStatusDocument = QDir(managedStatusRoot).filePath(
        "optimizations/main.structure-optimization.json");
    REQUIRE(QDir().mkpath(QFileInfo(managedStatusDocument).absolutePath()));
    REQUIRE(rws::StructureOptimizationProjectAdapter::saveProject(
        managedStatusDocument, managedStatusProblem, -1, &projectDocumentError));
    REQUIRE(widget.loadProjectDocument(
        managedStatusDocument, &projectDocumentError, managedStatusRoot));
    REQUIRE(!widget.statusText().contains(QString::fromUtf8("模型快照已过期")));
    rws::StructureOptimizationProblem standaloneStatusProblem;
    REQUIRE(rws::StructureOptimizationProjectFactory::create(
        managedPortable, managedStatusSource, standaloneStatusProblem, &staleFactoryError));
    standaloneStatusProblem.variables = problem.variables;
    standaloneStatusProblem.tasks = problem.tasks;
    standaloneStatusProblem.constraints = problem.constraints;
    standaloneStatusProblem.run = problem.run;
    standaloneStatusProblem.weights = problem.weights;
    standaloneStatusProblem.objectives = problem.objectives;
    widget.setProblem(standaloneStatusProblem);
    REQUIRE(!widget.statusText().contains(QString::fromUtf8("模型快照已过期")));
    REQUIRE(widget.loadProjectDocument(
        managedStatusDocument, &projectDocumentError, managedStatusRoot));
    REQUIRE(!widget.statusText().contains(QString::fromUtf8("模型快照已过期")));
    REQUIRE(widget.loadProjectDocument(managedStatusDocument, &projectDocumentError));
    REQUIRE(widget.statusText().contains(QString::fromUtf8("模型快照已过期")));

    const rws::StructureOptimizationProblem collected = widget.collectProblem();
    REQUIRE(collected.variables.size() == 1);
    REQUIRE(collected.tasks.size() == 1);
    REQUIRE(collected.variables[0].id == "joint1_z");
    REQUIRE(collected.tasks[0].point.id == "target");
    REQUIRE(collected.constraints.size() == 1);
    REQUIRE(collected.run.strategy == rws::StructureStrategyKind::Grid);
    REQUIRE(collected.run.localEliteCount == 4);
    REQUIRE(collected.run.finalVerificationCount == 2);
    REQUIRE(collected.run.maxLocalSweeps == 17);
    REQUIRE(collected.run.gridSteps == 6);
    REQUIRE(std::abs(collected.weights.reachability - 0.30) < 1e-12);
    REQUIRE(std::abs(collected.weights.preference - 0.10) < 1e-12);
    REQUIRE(rws::StructureOptimizationObjectiveProfile::isLegacyProfile(
        collected.objectives));
    REQUIRE(std::abs(collected.objectives[0].weight - 0.30) < 1e-12);

    REQUIRE(widget.findChild<QComboBox*>("structureOptimizationStrategyCombo") != nullptr);
    REQUIRE(widget.findChild<QSpinBox*>("structureOptimizationLocalEliteCount") != nullptr);
    REQUIRE(widget.findChild<QSpinBox*>("structureOptimizationFinalVerificationCount") != nullptr);
    REQUIRE(widget.findChild<QSpinBox*>("structureOptimizationMaxLocalSweeps") != nullptr);
    REQUIRE(widget.findChild<QSpinBox*>("structureOptimizationGridSteps") != nullptr);
    REQUIRE(widget.findChild<QDoubleSpinBox*>("structureOptimizationWeightReachability") != nullptr);
    REQUIRE(widget.findChild<QDoubleSpinBox*>("structureOptimizationWeightPreference") != nullptr);

    rws::StructureOptimizationProblem invalidProblem = problem;
    invalidProblem.context.modelSpec = rws::RobotModelSpec();
    widget.setProblem(invalidProblem);
    if (startButton != nullptr)
        REQUIRE(!startButton->isEnabled());
    REQUIRE(widget.statusText().toStdString().find(
                "RobotDesignContext.ModelSpec.Incomplete") != std::string::npos);

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// 子套件 托管项目门禁:构造只有源文件与模型文件、尚未发布托管 WorkCell 的机器人
// 项目,点击"从冻结需求新建"时必须显示明确的 WorkCell 就绪门禁文案,禁止在模型
// 发布前创建优化项目。需独立运行(依赖完整 RobWorkStudio 栈)。
static void testManagedRobotProjectRequiresPublishedWorkCell()
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectPath = directory.filePath("RobotDraft/RobotDraft.rwproj");
    rws::ProjectManifest manifest;
    manifest.project.id = "robot-draft";
    manifest.project.name = "RobotDraft";
    rws::ProjectResource source;
    source.id = "robot-source.main";
    source.kind = "robwork.passive-asset";
    source.path = "sources/robot.urdf";
    source.ownership = "project";
    source.required = false;
    rws::ProjectResource model;
    model.id = "robot-model.main";
    model.kind = "robwork.robot-model";
    model.path = "generated/robot-models/robot.rmb.json";
    model.ownership = "generated";
    model.required = false;
    model.dependencies = {source.id};
    manifest.resources = {source, model};
    manifest.entryPoints.insert("robotSource", source.id);
    QString error;
    rws::ProjectManager manager;
    REQUIRE(manager.createProject(projectPath, manifest, &error));
    manager.closeProject();

    const QString projectRoot = QFileInfo(projectPath).absolutePath();
    REQUIRE(QDir().mkpath(QFileInfo(QDir(projectRoot).filePath(source.path)).absolutePath()));
    QFile sourceFile(QDir(projectRoot).filePath(source.path));
    REQUIRE(sourceFile.open(QIODevice::WriteOnly));
    REQUIRE(sourceFile.write("<robot name=\"RobotDraft\"/>") > 0);
    sourceFile.close();
    REQUIRE(QDir().mkpath(QFileInfo(QDir(projectRoot).filePath(model.path)).absolutePath()));
    QFile modelFile(QDir(projectRoot).filePath(model.path));
    REQUIRE(modelFile.open(QIODevice::WriteOnly));
    REQUIRE(modelFile.write("{}") == 2);
    modelFile.close();

    rw::core::PropertyMap properties;
    rws::RobWorkStudio studio(properties);
    rws::CallbackProjectDocumentProvider modelProvider(
        "test.robot-model", "robwork.robot-model",
        [](const QString&, const rws::ProjectDocumentContext&, QString*) { return true; },
        [](const QString&, const rws::ProjectDocumentContext&, QString*) { return true; });
    REQUIRE(studio.registerProjectDocumentProvider(&modelProvider, &error));
    studio.openFile(projectPath.toStdString());
    REQUIRE(!studio.projectDirectory().isEmpty());
    REQUIRE(studio.mainWorkCellResourceId().isEmpty());
    rw::models::WorkCell::Ptr placeholder =
        rw::core::ownedPtr(new rw::models::WorkCell("RobotDraft"));
    rws::StructureOptimizerWidget widget;
    widget.setRobWorkStudio(&studio);
    widget.setScenarioContext(placeholder.get(), placeholder->getDefaultState());
    QPushButton* create = widget.findChild<QPushButton*>(
        "newStructureOptimizationProjectFromFrozenRequirementButton");
    REQUIRE(create != nullptr);
    if (create != nullptr)
        create->click();
    REQUIRE(widget.statusText() == QStringLiteral(
        "The robot project has not generated its managed WorkCell. Review the model in "
        "RobotModelBuilder and run Save and Load first."));
    studio.close();
}

// 子套件 异步控制器状态:用假任务循环驱动 StructureOptimizationController,验证
// running/paused/completed 信号时序、暂停期间进度不再推进、恢复后继续推进、
// 取消后 completed 携带 canceled 且控制器退出运行态。
// 项目关闭/切换回归:先构造带优化变量、任务与约束的旧问题并开启项目文档,
// 再调用 clearProjectDocumentContext(),校验任务与约束全部清空且脏标志复位,
// 确保新项目不会继承旧项目的优化会话。
static void testWidgetProjectCloseClearsOptimizationSession()
{
    rws::StructureOptimizerWidget widget;
    rws::StructureOptimizationProblem problem;
    problem.context.projectName = "old-project";
    problem.variables.push_back({"joint1_x", "Joint 1 X", "joint1", "m",
                                 rws::StructureVariableKind::JointPositionX,
                                 0.0, -1.0, 1.0, 0.1});
    rws::OptimizationTaskPoint task;
    task.point.id = "old-task";
    task.point.name = "Old task";
    problem.tasks.push_back(task);
    rws::StructureConstraint constraint;
    constraint.id = "old-constraint";
    problem.constraints.push_back(constraint);
    widget.setProblem(problem);
    widget.beginGeneratedProjectDocument(QStringLiteral("old/optimizations/main.json"));
    REQUIRE(widget.collectProblem().tasks.size() == 1);
    REQUIRE(widget.collectProblem().constraints.size() == 1);

    widget.clearProjectDocumentContext();

    const rws::StructureOptimizationProblem cleared = widget.collectProblem();
    REQUIRE(cleared.tasks.empty());
    REQUIRE(cleared.constraints.empty());
    REQUIRE(!widget.isProjectDocumentDirty());
}

static void testStructureOptimizationControllerAsyncState()
{
    std::printf("testStructureOptimizationControllerAsyncState ... ");

    struct SharedState {
        int progressCount = 0;
        bool canceled = false;
    };
    std::shared_ptr<SharedState> shared(new SharedState());

    rws::StructureOptimizationController controller(
        [shared](const rws::StructureOptimizationProblem&,
                 const rws::StructureOptimizationCallbacks& callbacks) {
            rws::StructureOptimizationResult result;
            for (int i = 0; i < 200; ++i) {
                if (callbacks.isCancellationRequested &&
                    callbacks.isCancellationRequested()) {
                    result.canceled = true;
                    shared->canceled = true;
                    return result;
                }
                if (callbacks.waitIfPaused)
                    callbacks.waitIfPaused();
                rws::StructureProgress progress;
                progress.stage = "Fake";
                progress.completed = i + 1;
                progress.planned = 200;
                progress.bestScore = static_cast<double>(i);
                if (callbacks.onProgress)
                    callbacks.onProgress(progress);
                ++shared->progressCount;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return result;
        });

    bool sawRunning = false;
    bool sawPaused = false;
    bool sawCompleted = false;
    bool completedCanceled = false;

    QObject::connect(&controller, &rws::StructureOptimizationController::runningChanged,
                     [&](bool running) { if (running) sawRunning = true; });
    QObject::connect(&controller, &rws::StructureOptimizationController::pausedChanged,
                     [&](bool paused) { if (paused) sawPaused = true; });
    QObject::connect(&controller, &rws::StructureOptimizationController::completed,
                     [&](const rws::StructureOptimizationResult& result) {
                         sawCompleted = true;
                         completedCanceled = result.canceled;
                     });

    rws::StructureOptimizationProblem problem;
    problem.run.candidateCount = 200;
    REQUIRE(controller.start(problem));
    REQUIRE(sawRunning);

    QEventLoop waitForProgress;
    QTimer::singleShot(80, &waitForProgress, SLOT(quit()));
    waitForProgress.exec();
    controller.pause();
    REQUIRE(sawPaused);
    const int pausedCount = shared->progressCount;

    QEventLoop pausedLoop;
    QTimer::singleShot(80, &pausedLoop, SLOT(quit()));
    pausedLoop.exec();
    REQUIRE(shared->progressCount <= pausedCount + 1);

    controller.resume();
    QEventLoop resumedLoop;
    QTimer::singleShot(80, &resumedLoop, SLOT(quit()));
    resumedLoop.exec();
    REQUIRE(shared->progressCount > pausedCount);

    controller.cancel();
    QEventLoop finishedLoop;
    QObject::connect(&controller, &rws::StructureOptimizationController::completed,
                     &finishedLoop, [&finishedLoop](const rws::StructureOptimizationResult&) {
                         finishedLoop.quit();
                     });
    QTimer::singleShot(5000, &finishedLoop, SLOT(quit()));
    if (!sawCompleted)
        finishedLoop.exec();

    REQUIRE(sawCompleted);
    REQUIRE(completedCanceled);
    REQUIRE(!controller.isRunning());

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  main
// =============================================================================

int main(int argc, char** argv)
{
    // 测试程序可能在底层 XML/WorkCell 加载器触发运行库终止；关闭 stdout 缓冲，
    // 使 CI 和本地调试日志始终保留最后一个已进入的测试用例名称，避免 abort() 将
    // 缓冲区中的诊断信息一并丢失。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::printf("=== StructureOptimizer Test Suite ===\n\n");
    std::fflush(stdout);

    const std::string suite = argc > 1 ? argv[1] : std::string();

    if (suite == "abi") {
        QCoreApplication app(argc, argv);
        testHistoricalStructureOptimizerAbiRemainsLinkable();
        return g_testFailures == 0 ? 0 : 1;
    }

    if (suite == "accepted_ur") {
        QCoreApplication app(argc, argv);
        testAcceptedUr6585AProject();
        if (g_testFailures == 0) {
            std::printf("Accepted UR project test passed.\n");
            return 0;
        }
        std::printf("Accepted UR project test FAILED.\n");
        return 1;
    }

    if (suite == "robot_file_acceptance") {
        QApplication app(argc, argv);
        testPortable300kgRobotFileProjectAcceptance();
        if (g_testFailures == 0) {
            std::printf("Robot file acceptance test passed.\n");
            return 0;
        }
        std::printf("Robot file acceptance test FAILED.\n");
        return 1;
    }

    if (suite == "resource_dependencies") {
        QApplication app(argc, argv);
        testStructureOptimizerResourceDependencies();
        if (g_testFailures == 0) {
            std::printf("Resource dependency test passed.\n");
            return 0;
        }
        std::printf("Resource dependency test FAILED.\n");
        return 1;
    }

    if (suite == "project_context") {
        QCoreApplication app(argc, argv);
        testProjectAdapterRestoresManagedScenarioRoot();
        if (g_testFailures == 0) {
            std::printf("Project context test passed.\n");
            return 0;
        }
        std::printf("Project context test FAILED.\n");
        return 1;
    }

    // 独立运行开关:仅执行"项目关闭清空优化会话"回归,便于快速验证而无需跑整个 widget 套件。
    if (suite == "project_close") {
        QApplication app(argc, argv);
        testWidgetProjectCloseClearsOptimizationSession();
        if (g_testFailures == 0) {
            std::printf("Project close test passed.\n");
            return 0;
        }
        std::printf("Project close test FAILED.\n");
        return 1;
    }

    if (suite == "widget") {
        QApplication app(argc, argv);
        testStructureOptimizerWidgetState();
        std::fflush(stdout);
        if (g_testFailures == 0)
        {
            std::printf("All tests passed.\n");
            return 0;
        }
        std::printf("%d test(s) FAILED.\n", g_testFailures);
        return 1;
    }

    if (suite == "managed_project_gate") {
        QApplication app(argc, argv);
        testManagedRobotProjectRequiresPublishedWorkCell();
        if (g_testFailures == 0) {
            std::printf("Managed project gate test passed.\n");
            return 0;
        }
        std::printf("Managed project gate test FAILED.\n");
        return 1;
    }

    QCoreApplication app(argc, argv);

    testHistoricalStructureOptimizerAbiRemainsLinkable();

    if (suite == "model_staleness") {
        testProjectFactoryProvenance();
        if (g_testFailures == 0) {
            std::printf("Model staleness tests passed.\n");
            return 0;
        }
        std::printf("Model staleness tests FAILED.\n");
        return 1;
    }

    if (suite == "model_factory") {
        testModelFactory();
        if (g_testFailures == 0) {
            std::printf("Model factory test passed.\n");
            return 0;
        }
        std::printf("Model factory test FAILED.\n");
        return 1;
    }

    if (suite == "frozen_requirements") {
        testFrozenEngineeringRequirementArtifactAdapter();
        testFrozenRequirementProjectImportCreatesAuditableProblem();
        testManagedFrozenRequirementImportUsesExplicitProjectRoot();
        if (g_testFailures == 0) {
            std::printf("All frozen requirement tests passed.\n");
            return 0;
        }
        std::printf("%d frozen requirement test(s) FAILED.\n", g_testFailures);
        return 1;
    }

    if (suite == "evaluator_consistency") {
        testSharedTargetEvaluatorConsistency();
        testVerifiedRegionUsesSharedEvaluator();
        testVerifiedRegionPreservesPositionCoverage();
        if (g_testFailures == 0) {
            std::printf("Evaluator consistency test passed.\n");
            return 0;
        }
        std::printf("Evaluator consistency test FAILED.\n");
        return 1;
    }

    if (suite == "cache") {
        testCache();
        if (g_testFailures == 0) {
            std::printf("Cache test passed.\n");
            return 0;
        }
        std::printf("Cache test FAILED.\n");
        return 1;
    }

    if (suite == "ui") {
        testUiTableModelsAndSuggestions();
        testConstraintModelAndProjectAdapter();
        testProjectFactory();
        testProjectFactoryProvenance();
        testExportService();
        testAcceptedUr6585AProject();
        testCandidatePreviewController();
        std::fflush(stdout);
        testStructureOptimizationControllerAsyncState();
        std::fflush(stdout);

        if (g_testFailures == 0)
        {
            std::printf("All tests passed.\n");
            return 0;
        }
        std::printf("%d test(s) FAILED.\n", g_testFailures);
        return 1;
    }

    testProblemDefaultsAndValidation();

    printf("\n");

    testGenerator();

    printf("\n");

    testCache();
    testMutator();
    testScorer();
    testScorerWithConstraints();
    testGenericObjectivesAndConstraints();
    testHardConstraints();

    printf("\n");

    testModelFactory();

    printf("\n");

    testEvaluator();
    testWorkspaceCoverage();
    testWorkspaceCoverageEvaluator();
    testSharedTargetEvaluatorConsistency();
    testVerifiedRegionUsesSharedEvaluator();
    testVerifiedRegionPreservesPositionCoverage();
    testWorkspaceCoverageDataInsufficient();
    testWorkspaceCoverageCancellation();
    testEngineeringEvaluatorPipeline();
    testSystemEngineeringOptimizer();
    testOptimizer();
    testHybridVerificationAndSensitivityWorkflow();
    testNoFeasibleCandidateLeavesSensitivityUnknown();

    printf("\n");

    testSensitivity();
    testSensitivityStopsAfterCancellation();
    testJsonRoundTrip();
    testAuditableEvidenceOutput();
    testCsvExport();
    testUiTableModelsAndSuggestions();
    testConstraintModelAndProjectAdapter();
    testFrozenEngineeringRequirementArtifactAdapter();
    testFrozenRequirementProjectImportCreatesAuditableProblem();
    testManagedFrozenRequirementImportUsesExplicitProjectRoot();
    testProjectFactory();
    testProjectFactoryProvenance();
    testExportService();
    testAcceptedUr6585AProject();
    testCandidatePreviewController();
    testStructureOptimizationControllerAsyncState();

    std::printf("\n");

    if (g_testFailures == 0)
    {
        std::printf("All tests passed.\n");
        return 0;
    }
    else
    {
        std::printf("%d test(s) FAILED.\n", g_testFailures);
        return 1;
    }
}
