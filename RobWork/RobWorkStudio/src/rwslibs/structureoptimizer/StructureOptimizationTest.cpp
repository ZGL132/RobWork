#include "StructureOptimizationTypes.hpp"
#include "StructureOptimizationValidation.hpp"
#include "StructureDesignMutator.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureCandidateGenerator.hpp"
#include "StructureCandidateCache.hpp"
#include "CandidateModelFactory.hpp"
#include "StructureCandidateEvaluator.hpp"
#include "HybridStructureOptimizer.hpp"
#include "StructureOptimizationStrategy.hpp"
#include "StructureSensitivityAnalyzer.hpp"
#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationCsv.hpp"
#include "StructureVariableTableModel.hpp"
#include "OptimizationTaskTableModel.hpp"
#include "StructureCandidateTableModel.hpp"
#include "StructureOptimizationUiLogic.hpp"
#include "StructureOptimizerWidget.hpp"
#include "StructureOptimizationController.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QMetaObject>
#include <QPushButton>
#include <QTimer>
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

    // ── clear ────────────────────────────────────────────────────────
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

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  Fake evaluator for optimizer testing
// =============================================================================

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

    if (g_testFailures == 0)
        std::printf("PASSED\n");
    else
        std::printf("FAILED (%d)\n", g_testFailures);
}

// =============================================================================
//  测试用例: 灵敏度分析器
// =============================================================================

//! 简化的模拟评估器: 越偏离 0 则得分越低, >0.8 则不可行。
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

    problem.constraints = {
        {"c1", "Constraint 1", "", rws::StructureConstraintKind::ModelValid,
         0.0, 0.0, true, true}
    };

    problem.run.candidateCount = 100;
    problem.run.randomSeed     = 42;

    // 序列化
    const std::string json = rws::StructureOptimizationJson::problemToJson(problem);
    REQUIRE(!json.empty());

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
    REQUIRE(parsed.constraints.size() == 1);
    REQUIRE(parsed.constraints[0].id == "c1");
    REQUIRE(parsed.run.candidateCount == 100);
    REQUIRE(parsed.run.randomSeed == 42u);

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

    widget.setProblem(problem);
    if (startButton != nullptr)
        REQUIRE(startButton->isEnabled());

    const rws::StructureOptimizationProblem collected = widget.collectProblem();
    REQUIRE(collected.variables.size() == 1);
    REQUIRE(collected.tasks.size() == 1);
    REQUIRE(collected.variables[0].id == "joint1_z");
    REQUIRE(collected.tasks[0].point.id == "target");

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
    std::printf("=== StructureOptimizer Test Suite ===\n\n");
    std::fflush(stdout);

    const std::string suite = argc > 1 ? argv[1] : std::string();

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

    QCoreApplication app(argc, argv);

    if (suite == "ui") {
        testUiTableModelsAndSuggestions();
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
    testHardConstraints();

    printf("\n");

    testModelFactory();

    printf("\n");

    testEvaluator();
    testOptimizer();

    printf("\n");

    testSensitivity();
    testJsonRoundTrip();
    testCsvExport();
    testUiTableModelsAndSuggestions();
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
