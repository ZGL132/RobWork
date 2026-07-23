#include "StructureOptimizationTypes.hpp"
#include "StructureOptimizationValidation.hpp"
#include "StructureDesignMutator.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureCandidateGenerator.hpp"
#include "StructureCandidateCache.hpp"
#include "CandidateModelFactory.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>

#include <QDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
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
//  main
// =============================================================================

int main()
{
    std::printf("=== StructureOptimizer Test Suite ===\n\n");

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
