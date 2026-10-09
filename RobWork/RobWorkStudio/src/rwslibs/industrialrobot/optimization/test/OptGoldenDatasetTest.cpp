/**
 * @file   OptGoldenDatasetTest.cpp
 * @brief  optimization 黄金数据集消费套件（OptGolden*）——WP-20-T11 主交付
 *         面：testdata/golden/opt-* 三数据集经 testkit 黄金设施（GoldenFixture
 *         六步装载＋DatasetManifest 完整性＋ToleranceProfile 档案容差）消费。
 *         期望值来自数据文件（各 generate/*.mjs 独立参考实现产物——按单元卡
 *         冻结语义封闭推导，与产品实现零共享代码），覆盖任务卡 acceptance
 *         三条的黄金观测：
 *   - OptGoldenLhs：同种子确定性候选生成黄金（AT-09——OPT-VER-130 黄金面：
 *     §8.2 冻结算法独立复算批逐值对照＋补丁身份链 SHA-256 对照＋同配置
 *     双跑等价集合与稳定排序＋红实验证非恒真）；
 *   - OptGoldenPareto：不可行不入可行集＋Pareto 非支配黄金（AT-09——
 *     OPT-VER-122 黄金面：两级编排终态逐候选状态对照＋可行集/非支配集
 *     经 testkit SetMatchTraits 域侧特化的集合等价断言〔testkit 零 Pareto
 *     逻辑——§5.4 业务规则归域红线〕＋三项静态指标档案容差对照）；
 *   - OptGoldenAdopt：采用守卫黄金（AT-12——OPT-VER-143/144/145 黄金面：
 *     两步命令组合语义＋身份链＋物化缝字节摘要＋基线字节不变）；
 *   - 显式容差支配（acceptance 3）：显式配置容差场景的前沿翻转黄金
 *     （P-OPT-5 裁决前产品默认恒零容差——非零容差只存在于数据集场景声明）。
 *
 * 设计依据：
 *   - units/optimization.md §7.4/§7.5（Pareto 非支配/候选分类判定流——
 *     不可行不入可行集）、§8.2（seeded-lhs 确定性生成）、§4.2/§5.5（身份
 *     链）、§10.1~§10.4（采用守卫组装面）、§13.0（黄金数据集约定——
 *     DatasetManifest/SetMatchTraits 域侧特化/容差档案）、§13.1（OPT-VER-
 *     122/130/143~145）、§13.3（AT-09/AT-12 观测点）、§16.3 P-OPT-5
 *     （支配容差默认零——显式配置执行）
 *   - units/testkit.md §5.4（SetMatchTraits——匹配 traits 消费方特化、
 *     testkit 零业务规则）、§4.2/§4.5（DatasetManifest/完整性）、§4.3
 *     （容差档案）、§6.1（GoldenFixture 六步生命周期）
 *   - 需求 OPT-01/02/03/04/06/07/08（任务契约 requirements）、NFR-COR-01/
 *     02/03、CON-05、AT-09/AT-12
 *   - 任务契约 tasks/foundation/WP-20-T11.json acceptance 1/2/3
 *
 * 测试策略（黄金数据独立性——analytic-case 的 lint 义务）：期望值全部由
 * 数据集 generate/ 脚本按单元卡冻结语义独立推导（splitmix64/NSGA/身份链
 * 均为独立复算，与产品 C++ 实现零共享代码），本文件只做"装载→驱动产品
 * 公共接口→黄金对照"三件事；驱动入口全部为虚接口/公共类消费（
 * generateSeededLhsCandidates/TwoStageEvaluationOrchestrator/
 * IParetoFrontBuilder/IOptimizationCandidateApplier/patchIdentity/
 * candidateIdOf——接口路径钉扎，WP-20-T03 接口路径零覆盖教训）。
 *
 * 线程约束：gtest 用例天然串行；被测入口纯函数/编排器串行纪律。
 */

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/optimization/Applier.hpp>
#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/Constraint.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Pareto.hpp>
#include <sdurws/ird/optimization/Run.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/SetCheck.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace opt = sdurws::ird::optimization;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace tk = sdurws::ird::testkit;
using namespace sdurws::ird::testkit;  // IRD_EXPECT_* 宏展开所需（AssertMacros 契约）

// =====================================================================
// SetMatchTraits 域侧特化（§5.4——业务匹配规则归域，testkit 零 Pareto/优化
// 逻辑；本特化即"域侧特化"的落地面：Pareto 集合等价断言的身份键＝候选
// 身份规范文本、数值键＝激活目标值〔声明序，经容差档案逐项 C4 对照〕）
// =====================================================================

namespace optgolden {

/// 候选生成批的补丁项成员（SetMatchTraits 元素——身份＝候选标签＋绑定 token）。
struct LhsItemMember {
    std::string identity;  ///< "<label>|<bindingId>"（批内唯一——expected 侧
                           ///  重复即 DatasetInvalid，参考集自相矛盾防线）
    double valueSi;        ///< 补丁标量值（SI 单位——m/无量纲）
};

/// Pareto 前沿成员（SetMatchTraits 元素——身份＝CandidateId 规范文本；
/// 数值＝激活目标值，声明序固定 [包络, 结构质量, 最小关节裕量]）。
struct ParetoMember {
    std::string candidateId;      ///< "cnd-<64hex>"（去重与配对锚）
    std::array<double, 3> metrics = {};  ///< 激活目标值（声明序；SI/无量纲）
    std::size_t nondominationRank = 0;   ///< 非支配 rank（集合断言外逐项核对）
    bool paretoNondominated = false;     ///< 非支配集成员标记（同上）
    bool isBaseline = false;             ///< 基线标记（同上）
};

}  // namespace optgolden

namespace sdurws::ird::testkit {

/// LHS 补丁项 traits：身份精确配对＋单数值字段容差匹配（档案 batch.items.value）。
template <>
struct SetMatchTraits<optgolden::LhsItemMember> {
    static std::optional<std::string> identity(const optgolden::LhsItemMember& m)
    {
        return m.identity;
    }
    static std::vector<NumericFieldView> numerics(const optgolden::LhsItemMember& m)
    {
        return {NumericFieldView{"batch.items.value", m.valueSi}};
    }
};

/// Pareto 前沿成员 traits：身份精确配对＋三激活目标值容差匹配（档案
/// metrics.envelope / metrics.structuralMass / metrics.minJointMargin）。
template <>
struct SetMatchTraits<optgolden::ParetoMember> {
    static std::optional<std::string> identity(const optgolden::ParetoMember& m)
    {
        return m.candidateId;
    }
    static std::vector<NumericFieldView> numerics(const optgolden::ParetoMember& m)
    {
        return {NumericFieldView{"metrics.envelope", m.metrics[0]},
                NumericFieldView{"metrics.structuralMass", m.metrics[1]},
                NumericFieldView{"metrics.minJointMargin", m.metrics[2]}};
    }
};

}  // namespace sdurws::ird::testkit

// =====================================================================
// 黄金 JSON 读取脚手架（装载/完整性已由 GoldenFixture 校验；此处只做字段
// 提取——字段缺失记失败并回退安全值，防越界崩溃掩盖首因）
// =====================================================================

namespace {

/// 读数据集文本文件（inputs/expected 侧相对路径经 dataset 解析）。
std::string readDatasetText(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    const auto path = inputSide ? ds.resolveInput(rel) : ds.resolveExpected(rel);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

tk::JsonValue loadJson(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    return tk::parseJson(readDatasetText(ds, rel, inputSide));
}

/// 数值字段（缺失记失败并回退 0）。
double num(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isNumber()) {
        ADD_FAILURE() << "黄金数据缺数值字段: " << key;
        return 0.0;
    }
    return v->number;
}

/// 整数字段（u64 种子/预算——double 承载安全域内直取）。
std::uint64_t uintOf(const tk::JsonValue& o, const char* key)
{
    return static_cast<std::uint64_t>(num(o, key));
}

/// 字符串字段（缺失记失败并回退空串）。
std::string str(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isString()) {
        ADD_FAILURE() << "黄金数据缺字符串字段: " << key;
        return {};
    }
    return v->text;
}

/// 可缺数值字段（null/缺失 → nullopt——黄金 null 语义＝"—"）。
std::optional<double> optNum(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || v->isNull()) {
        return std::nullopt;
    }
    if (!v->isNumber()) {
        ADD_FAILURE() << "黄金数据数值字段类型不符: " << key;
        return std::nullopt;
    }
    return v->number;
}

// ---- 研究定义/目标集的黄金→域模型装配 -----------------------------------

/// 绑定集（inputs.variables——字段直取；词表匹配由产品校验面执行）。
std::vector<opt::VariableBinding> bindingsFromJson(const tk::JsonValue& inputs)
{
    std::vector<opt::VariableBinding> out;
    const tk::JsonValue* vars = inputs.find("variables");
    if (vars == nullptr || !vars->isArray()) {
        ADD_FAILURE() << "黄金 inputs 缺 variables 数组";
        return out;
    }
    for (const tk::JsonValue& v : vars->items) {
        opt::VariableBinding b;
        b.bindingId = str(v, "bindingId");
        const std::string kind = str(v, "kind");
        // 类别 token → 词表（continuous/quantized/enumeration——§5.2 四类中
        // R1 采样面三类；discrete-device 属 R2 不在黄金数据集）。
        if (kind == "continuous") {
            b.kind = opt::VariableKind::Continuous;
        } else if (kind == "quantized") {
            b.kind = opt::VariableKind::Quantized;
        } else if (kind == "enumeration") {
            b.kind = opt::VariableKind::Enumeration;
        } else {
            ADD_FAILURE() << "黄金绑定类别未知: " << kind;
        }
        if (const auto u = core::UnitToken::find(str(v, "unit")); u.has_value()) {
            b.unit = *u;
        } else if (!str(v, "unit").empty()) {
            ADD_FAILURE() << "黄金绑定单位未注册: " << str(v, "unit");
        }
        b.lowerBound = num(v, "lowerBound");   // SI 单位（m/无量纲）
        b.upperBound = num(v, "upperBound");   // SI 单位
        b.step = num(v, "step");               // 量化步长（SI；连续恒 0）
        if (const tk::JsonValue* ev2 = v.find("enumValues");
            ev2 != nullptr && ev2->isArray()) {
            for (const tk::JsonValue& e : ev2->items) {
                b.enumValues.push_back(e.text);
            }
        }
        b.defaultValue = num(v, "defaultValue");
        // 默认下标为可缺字段（连续/量化绑定无下标语义——黄金 adopt 数据
        // 不携带；枚举绑定的界内校验由产品校验面执行）。
        if (const tk::JsonValue* di = v.find("defaultValueIndex");
            di != nullptr && di->isNumber()) {
            b.defaultValueIndex = static_cast<std::uint32_t>(di->number);
        }
        b.locked = v.find("locked") != nullptr && v.find("locked")->isBool()
                   && v.find("locked")->boolean;
        b.authorized = v.find("authorized") == nullptr
                       || (v.find("authorized")->isBool() && v.find("authorized")->boolean);
        b.authorityFieldPath = str(v, "authorityFieldPath");
        b.diagSubject = str(v, "diagSubject");
        out.push_back(std::move(b));
    }
    return out;
}

/// 运行配置（inputs.config——seed/预算/策略；目标集由用例按场景装配）。
opt::OptimizationConfiguration configFromJson(const tk::JsonValue& inputs)
{
    opt::OptimizationConfiguration c;
    const tk::JsonValue* cfg = inputs.find("config");
    if (cfg == nullptr || !cfg->isObject()) {
        ADD_FAILURE() << "黄金 inputs 缺 config 对象";
        return c;
    }
    c.schemaVersion = static_cast<std::uint32_t>(uintOf(*cfg, "schemaVersion"));
    const std::string stage = str(*cfg, "stage");
    c.stage = (stage == "stage-d") ? opt::OptimizationStage::StageD
                                   : opt::OptimizationStage::StageB;
    c.seed = uintOf(*cfg, "seed");  // I-OPT-2：≥1（0 非法——黄金输入违约即被校验拦截）
    if (const tk::JsonValue* budget = cfg->find("budget");
        budget != nullptr && budget->isObject()) {
        c.budget.maxCandidates = static_cast<std::uint32_t>(uintOf(*budget, "maxCandidates"));
        c.budget.maxVerifiedCandidates =
            static_cast<std::uint32_t>(uintOf(*budget, "maxVerifiedCandidates"));
        c.budget.maxGenerations = static_cast<std::uint32_t>(uintOf(*budget, "maxGenerations"));
    }
    c.strategyId = str(*cfg, "strategyId");
    c.variables = bindingsFromJson(inputs);
    return c;
}

/// MetricId token → 枚举（词表反查——toToken 全表匹配，无第二解析面）。
std::optional<opt::MetricId> metricIdFromToken(const std::string& token)
{
    for (int i = 0; i < static_cast<int>(opt::kMetricCount); ++i) {
        const auto id = static_cast<opt::MetricId>(i);
        if (std::string(opt::toToken(id)) == token) {
            return id;
        }
    }
    return std::nullopt;
}

/// 激活目标集（inputs.objectives*——声明序保留；容差为数据集场景显式声明）。
opt::ObjectiveSet objectivesFromJson(const tk::JsonValue& inputs, const char* key)
{
    std::vector<opt::ObjectiveEntry> entries;
    const tk::JsonValue* arr = inputs.find(key);
    if (arr == nullptr || !arr->isArray()) {
        ADD_FAILURE() << "黄金 inputs 缺目标集数组: " << key;
        return opt::ObjectiveSet{entries};
    }
    for (const tk::JsonValue& o : arr->items) {
        const auto id = metricIdFromToken(str(o, "metricId"));
        if (!id.has_value()) {
            ADD_FAILURE() << "黄金目标指标 token 未知: " << str(o, "metricId");
            continue;
        }
        opt::ObjectiveEntry entry;
        entry.metricId = *id;
        if (const tk::JsonValue* tol = o.find("tolerance");
            tol != nullptr && tol->isObject()) {
            entry.tolerance.relative = num(*tol, "relative");
            entry.tolerance.absolute = num(*tol, "absolute");
        }
        entries.push_back(entry);
    }
    return opt::makeObjectiveSet(entries);
}

/// 摘要十六进制（SHA-256 小写 hex——身份链对照用）。
std::string toHex(const core::Digest256& digest)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (std::uint8_t b : digest) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

/// 字节流 SHA-256（core::ContentDigester 单点——CR-02 身份计算唯一入口）。
std::string sha256Hex(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return toHex(digester.finalize());
}

}  // namespace

// =====================================================================
// 套件一：同种子确定性候选生成黄金（opt-lhs-golden——AT-09/OPT-VER-130）
// =====================================================================

namespace {

/// LHS 套件夹具（GoldenFixture 六步——数据集 opt-lhs-golden@1.0.0）。
class OptGoldenLhs : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override
    {
        return {"opt-lhs-golden", "1.0.0"};
    }
};

/// 期望批成员展开（expected.batch[*].items——枚举项不入数值集合，另行断言）。
std::vector<optgolden::LhsItemMember> lhsMembersFromGolden(const tk::JsonValue& expected)
{
    std::vector<optgolden::LhsItemMember> members;
    const tk::JsonValue* batch = expected.find("batch");
    if (batch == nullptr || !batch->isArray()) {
        ADD_FAILURE() << "黄金 expected 缺 batch 数组";
        return members;
    }
    for (const tk::JsonValue& c : batch->items) {
        const std::string label = str(c, "label");
        const tk::JsonValue* items = c.find("items");
        if (items == nullptr || !items->isArray()) {
            ADD_FAILURE() << "黄金候选缺 items: " << label;
            continue;
        }
        for (const tk::JsonValue& it : items->items) {
            const std::string kind = str(it, "kind");
            if (kind == "enumeration") {
                continue;  // 枚举下标为离散面——批内逐项精确断言（见用例体）
            }
            members.push_back(optgolden::LhsItemMember{
                label + "|" + str(it, "bindingId"), num(it, "scalarValue")});
        }
    }
    return members;
}

/// 实际批成员展开（产品生成批——与黄金同构展开；枚举项不入数值集合，
/// 由绑定类别判定——PatchItem 表达层无类别字段，kind 唯一来源＝绑定）。
std::vector<optgolden::LhsItemMember> lhsMembersFromBatch(
    const std::vector<opt::CandidatePatch>& batch,
    const std::vector<opt::VariableBinding>& bindings)
{
    std::vector<optgolden::LhsItemMember> members;
    for (const opt::CandidatePatch& patch : batch) {
        for (const opt::PatchItem& item : patch.items) {
            const auto binding = std::find_if(
                bindings.begin(), bindings.end(),
                [&](const opt::VariableBinding& b) {
                    return b.bindingId == item.bindingId;
                });
            if (binding != bindings.end()
                && binding->kind == opt::VariableKind::Enumeration) {
                continue;  // 枚举项——批内逐项精确断言（见用例体）
            }
            members.push_back(optgolden::LhsItemMember{
                patch.label + "|" + item.bindingId, item.scalarValue});
        }
    }
    return members;
}

}  // namespace

/**
 * 黄金主用例（acceptance 1——AT-09 同种子确定性/OPT-VER-130 生成半区）：
 * 黄金配置复算批与独立参考实现期望批逐值对照（SetMatchTraits 集合等价＋
 * 稳定序）＋基线恒批首＋量化对齐面＋枚举下标＋补丁身份链（SHA-256 独立
 * 复算对照）＋双跑逐字节可复现＋红实验两则（异种子异批/期望值变异检出
 * ——证黄金对照非恒真）。
 */
TEST_F(OptGoldenLhs, SeededBatchMatchesGoldenAndReplays_WP20T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"OPT-01", "OPT-02", "OPT-06",
                                            "NFR-COR-01", "NFR-COR-02"}),
                  (std::vector<std::string>{"AT-09"}),
                  tk::DatasetRef{"opt-lhs-golden", "1.0.0"}, "opt-golden@1.0.0");
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/lhs-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/lhs-expected.json", false);
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    // ---- 黄金配置装配（config 校验面同步执行——种子/预算违约即抛）。
    opt::OptimizationConfiguration config = configFromJson(inputs);
    config.objectives = opt::defaultObjectives(opt::OptimizationStage::StageB);
    ASSERT_NO_THROW(opt::validateConfiguration(config));

    // ---- 采样维度序核对（黄金 dimensionOrder＝bindingId 字典序——§8.2 第 2 步）。
    const tk::JsonValue* dimOrder = inputs.find("dimensionOrder");
    ASSERT_TRUE(dimOrder != nullptr && dimOrder->isArray());
    std::vector<std::string> goldenDims;
    for (const tk::JsonValue& d : dimOrder->items) {
        goldenDims.push_back(d.text);
    }
    std::vector<std::string> sortedDims = goldenDims;
    std::sort(sortedDims.begin(), sortedDims.end());
    EXPECT_EQ(goldenDims, sortedDims) << "黄金维度序非 bindingId 字典序（§8.2 采样序）";

    // ---- 复算批（产品公共接口——同种子同配置）。
    std::vector<opt::CandidatePatch> batch =
        opt::generateSeededLhsCandidates(config);

    // 批规模与基线恒批首（§8.2——基线总是生成并参与评估）。
    const tk::JsonValue* goldenBatch = expected.find("batch");
    ASSERT_TRUE(goldenBatch != nullptr && goldenBatch->isArray());
    ASSERT_EQ(batch.size(), goldenBatch->items.size());
    ASSERT_FALSE(batch.empty());
    EXPECT_TRUE(batch.front().items.empty()) << "基线候选恒为批首空补丁（§8.2）";
    EXPECT_EQ(batch.front().label, "baseline");
    EXPECT_TRUE(opt::patchIdentity(batch.front()).isValid())
        << "空补丁有确定身份（基线候选——§5.5③）";

    // ---- 数值集合等价（SetMatchTraits 域侧特化——档案容差逐项 C4）＋稳定序。
    const auto goldenMembers = lhsMembersFromGolden(expected);
    const auto actualMembers = lhsMembersFromBatch(batch, config.variables);
    ASSERT_FALSE(goldenMembers.empty());
    const tk::SetCheckResult setResult =
        tk::checkSetEquivalent(goldenMembers, actualMembers, *profile,
                               tk::SetMatchTraits<optgolden::LhsItemMember>{});
    EXPECT_TRUE(setResult.equivalent)
        << "复算批与黄金期望批数值集合不等价（缺失 "
        << setResult.missingExpected.size() << "/多余 "
        << setResult.extraActual.size() << "）";
    EXPECT_TRUE(setResult.duplicates.empty()) << "复算批身份键重复";
    // 稳定序：批内项顺序（基线→cand-<i>、项按维度字典序）逐位一致——
    // checkStableOrder 身份按位精确相等（附录 D 第 12 项无容差）。
    const tk::OrderCheckResult orderResult = tk::checkStableOrder(
        goldenMembers, actualMembers, tk::SetMatchTraits<optgolden::LhsItemMember>{},
        *profile);
    EXPECT_TRUE(orderResult.sameOrder)
        << "复算批项序与黄金不一致（首个错位 " << orderResult.firstDivergence
        << "——生成序为确定性契约）";

    // ---- 枚举下标与枚举键（离散面逐项精确断言）。
    for (std::size_t ci = 0; ci < goldenBatch->items.size(); ++ci) {
        const tk::JsonValue& gc = goldenBatch->items[ci];
        const opt::CandidatePatch& patch = batch[ci];
        const tk::JsonValue* items = gc.find("items");
        ASSERT_TRUE(items != nullptr && items->isArray());
        // 标量项数一致（枚举项单独走本循环核对）。
        std::size_t goldenEnumCount = 0;
        for (const tk::JsonValue& it : items->items) {
            if (str(it, "kind") != "enumeration") {
                continue;
            }
            ++goldenEnumCount;
            const std::string bindingId = str(it, "bindingId");
            bool found = false;
            for (const opt::PatchItem& pi : patch.items) {
                if (pi.bindingId != bindingId) {
                    continue;
                }
                found = true;
                EXPECT_EQ(pi.enumIndex,
                          static_cast<std::uint32_t>(num(it, "enumIndex")))
                    << "枚举下标失配（候选 " << ci << " 绑定 " << bindingId << "）";
                break;
            }
            EXPECT_TRUE(found) << "复算批缺枚举项: " << bindingId;
        }
        // 补丁身份链（§5.5 canonical → SHA-256——node crypto 独立复算对照；
        // 位级等价：同一 canonical 字节必得同一摘要）。
        const std::string goldenPatchId = str(gc, "patchId");
        EXPECT_EQ(opt::patchIdentity(patch).toCanonical(), goldenPatchId)
            << "补丁身份失配（候选 " << ci << "——canonical 编码或字节面漂移）";
        (void)goldenEnumCount;
    }

    // ---- 双跑逐字节可复现（I-OPT-3 最强形态——canonical 字节相等）。
    const auto batch2 = opt::generateSeededLhsCandidates(config);
    ASSERT_EQ(batch2.size(), batch.size());
    for (std::size_t i = 0; i < batch.size(); ++i) {
        EXPECT_EQ(opt::canonicalize(batch2[i]), opt::canonicalize(batch[i]))
            << "同配置双跑第 " << i << " 候选 canonical 字节不等（确定性承诺）";
    }

    // ---- 生成面审计口径对照（黄金 audit.candidatesGenerated）。
    if (const tk::JsonValue* audit = expected.find("audit");
        audit != nullptr && audit->isObject()) {
        EXPECT_EQ(batch.size(),
                  static_cast<std::size_t>(uintOf(*audit, "candidatesGenerated")));
    }

    // ---- 红实验一（非恒真证明——异种子必异批）：seed+1 复算批与黄金
    //      集合不等价（否则黄金对照对种子失敏——I-OPT-3 检验力失效）。
    opt::OptimizationConfiguration other = config;
    other.seed = config.seed + 1;  // ≥1 合法域内——数据集场景声明，非产品默认
    const auto otherBatch = opt::generateSeededLhsCandidates(other);
    const auto otherMembers = lhsMembersFromBatch(otherBatch, other.variables);
    const tk::SetCheckResult redResult =
        tk::checkSetEquivalent(goldenMembers, otherMembers, *profile,
                               tk::SetMatchTraits<optgolden::LhsItemMember>{});
    EXPECT_FALSE(redResult.equivalent)
        << "异种子复算批与黄金等价——黄金对照对种子失敏（红实验失败）";

    // ---- 红实验二（非恒真证明——期望值变异检出）：黄金成员值 +0.01 后
    //      集合等价必须翻转失败（否则对照对数值偏差失敏）。
    auto mutated = goldenMembers;
    ASSERT_FALSE(mutated.empty());
    mutated.front().valueSi += 0.01;
    const tk::SetCheckResult redResult2 =
        tk::checkSetEquivalent(mutated, actualMembers, *profile,
                               tk::SetMatchTraits<optgolden::LhsItemMember>{});
    EXPECT_FALSE(redResult2.equivalent)
        << "黄金期望值变异未被检出——黄金对照失敏（红实验失败）";
}

// =====================================================================
// 套件二：Pareto 非支配与不可行不入可行集黄金（opt-pareto-golden——
// AT-09/OPT-VER-122/124 黄金面＋acceptance 3 显式容差支配）
// =====================================================================

namespace {

/// 两级编排夹具（真注册表＋黄金事实驱动的投影/探针替身——T06 模型测试
/// 同款形态；事实按补丁身份查表注入——P-OPT-2 裁决前接缝纪律）。
class OrchestratorRig
{
public:
    ev::EvidenceProfileRegistry profiles;
    ev::EvaluatorRegistry evaluators{profiles};
    core::ContentIdentity profileIdentity{};

    explicit OrchestratorRig(const tk::JsonValue& inputs)
    {
        // ---- kin Profile（空必需项集——完备性核对平凡通过）＋两评估键注册。
        ev::RequiredEvidenceProfile profile;
        profile.profileId = "kin";
        profile.version = "1.0";
        profiles.registerProfile(profile);
        // 评估键以 std::string 承载（避免临时串 c_str 悬垂——两键均为
        // optimization 词表常量，kebab 词形经证据注册表契约校验）。
        const std::string kinKeys[2] = {std::string(opt::kKinTaskPointsBatchKey),
                                        std::string(opt::kKinRegionCoverageKey)};
        for (const std::string& key : kinKeys) {
            ev::EvaluatorDescriptor d;
            d.key = key;
            d.contractVersion = 1U;
            d.profile.profileId = "kin";
            d.profile.version = "1.0";
            d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
            d.stateless = true;
            d.threadSafety = ev::ThreadSafety::FullyThreadSafe;
            evaluators.registerEvaluator(
                std::make_unique<ScriptedKinFactory>(std::move(d)), {});
        }
        if (const auto found = profiles.findProfile("kin", "1.0")) {
            profileIdentity = found->contentIdentity;
        }
        // ---- 事实表装载（inputs.candidateFacts——批序铺位）。
        const tk::JsonValue* facts = inputs.find("candidateFacts");
        if (facts == nullptr || !facts->isArray()) {
            ADD_FAILURE() << "黄金 inputs 缺 candidateFacts 数组";
            return;
        }
        for (const tk::JsonValue& f : facts->items) {
            FactRow row;
            row.key = str(f, "key");
            row.kind = str(f, "kind");
            row.jointSampleRad = num(f, "jointSampleRad");
            if (const tk::JsonValue* env = f.find("envelope"); env != nullptr) {
                row.envelope = opt::EnvelopeFacts{num(*env, "xMin"), num(*env, "yMin"),
                                                  num(*env, "zMin"), num(*env, "xMax"),
                                                  num(*env, "yMax"), num(*env, "zMax")};
            }
            if (const tk::JsonValue* masses = f.find("linkMassesKg");
                masses != nullptr && masses->isArray()) {
                for (std::size_t i = 0; i < masses->items.size(); ++i) {
                    opt::LinkMassFact lm;
                    lm.linkSubject =
                        "obj-000000000000000000000000000000d" + std::to_string(i + 1);
                    lm.massKg = masses->items[i].number;  // kg（黄金事实值）
                    lm.provenanceToken = "provided";
                    row.linkMasses.push_back(std::move(lm));
                }
            }
            if (const tk::JsonValue* margins = f.find("margins");
                margins != nullptr && margins->isArray()) {
                for (std::size_t i = 0; i < margins->items.size(); ++i) {
                    opt::PointMarginFact pm;
                    pm.caseIdText = "case-" + std::to_string(i + 1);
                    pm.minMargin = margins->items[i].number;  // D-KIN-2 归一化承载值
                    pm.unitSymbol = "1";
                    row.pointMargins.push_back(std::move(pm));
                }
            }
            facts_.push_back(std::move(row));
        }
        if (const tk::JsonValue* limits = inputs.find("jointLimits");
            limits != nullptr && limits->isObject()) {
            qmin_ = num(*limits, "qminRad");  // rad（黄金限位）
            qmax_ = num(*limits, "qmaxRad");  // rad
        }
    }

    /// 事实行（按批序 index 铺位——与黄金 candidates[*].index 对齐）。
    struct FactRow
    {
        std::string key;
        std::string kind;  // feasible / limit-violation / margin-missing
        double jointSampleRad = 0.0;  // rad（限位判定样本）
        opt::EnvelopeFacts envelope{};       // 基座系 AABB（m）
        std::vector<opt::LinkMassFact> linkMasses;    // kg
        std::vector<opt::PointMarginFact> pointMargins;  // 无量纲
    };

    const FactRow& factAt(std::size_t index) const
    {
        if (index >= facts_.size()) {
            ADD_FAILURE() << "事实表越界（批序 " << index << "）";
            static const FactRow empty{};
            return empty;
        }
        return facts_[index];
    }

    /// 事实键查表（黄金 candidates[*].factKey → 事实行——投影查表的源头；
    /// 未命中返回 nullptr，由调用方记失败）。
    const FactRow* factByKey(const std::string& key) const
    {
        for (const FactRow& row : facts_) {
            if (row.key == key) {
                return &row;
            }
        }
        return nullptr;
    }

    double qmin() const noexcept { return qmin_; }  // rad
    double qmax() const noexcept { return qmax_; }  // rad

private:
    /// ③端口脚本化评估器（空输出——无违例素材，Feasible 由覆盖完备＋限位
    /// 合法得到；与 T06 模型测试同款）。
    class ScriptedKinEvaluator final : public ev::IEngineeringEvaluator
    {
    public:
        explicit ScriptedKinEvaluator(ev::EvaluatorDescriptor d)
            : m_descriptor(std::move(d))
        {
        }
        const ev::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }
        ev::EvaluationOutput evaluate(const ev::EvaluationRequest&,
                                      ev::IEvaluationContext&) override
        {
            return {};
        }

    private:
        ev::EvaluatorDescriptor m_descriptor;
    };
    class ScriptedKinFactory final : public ev::IEvaluatorFactory
    {
    public:
        explicit ScriptedKinFactory(ev::EvaluatorDescriptor d)
            : m_descriptor(std::move(d))
        {
        }
        const ev::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }
        std::unique_ptr<ev::IEngineeringEvaluator> create() const override
        {
            return std::make_unique<ScriptedKinEvaluator>(m_descriptor);
        }

    private:
        ev::EvaluatorDescriptor m_descriptor;
    };

    std::vector<FactRow> facts_;
    double qmin_ = -1.0;  // rad（黄金限位默认——inputs.jointLimits 覆写）
    double qmax_ = 1.0;   // rad
};

/// 黄金事实驱动的候选投影（ICandidateProjector 接缝——**按补丁身份查表**：
/// patchId（canonical）→ 事实行。补丁身份是投影的稳定键，与评估阶段
/// （Quick/Verified 复评同补丁再投影）和批序无耦合；查表失配＝黄金
/// candidates[*].patchId 与编排复算批的补丁不一致（身份链断），记失败后
/// 回安全空投影，由后续状态断言揭示根因）。
class FactProjector final : public opt::ICandidateProjector
{
public:
    FactProjector(std::map<std::string, OrchestratorRig::FactRow> factsByPatchId,
                  double qmin, double qmax)
        : m_facts(std::move(factsByPatchId))
        , m_qmin(qmin)  // rad（黄金限位——限位表的判定面）
        , m_qmax(qmax)  // rad
    {
    }

    opt::CandidateProjection project(const opt::CandidatePatch& patch,
                                     const std::vector<opt::VariableBinding>&) const override
    {
        const std::string patchId = opt::patchIdentity(patch).toCanonical();
        const auto it = m_facts.find(patchId);
        if (it == m_facts.end()) {
            ADD_FAILURE() << "投影收到黄金事实表之外的补丁（身份查表失配）: "
                          << patchId;
            return opt::CandidateProjection{};
        }
        const OrchestratorRig::FactRow& f = it->second;
        opt::CandidateProjection p;
        p.baselineChainInEnabledScope = true;
        // 限位表：单转动关节，有限限位 [qmin,qmax]（rad——黄金限位）。
        opt::JointLimitSpecRecord joint;
        joint.jointSubject = "obj-000000000000000000000000000000d0";
        joint.localName = "J2";
        joint.qmin = m_qmin;
        joint.qmax = m_qmax;
        joint.isContinuous = false;
        p.jointLimits.joints = {joint};
        p.jointLimits.configurations = {{f.jointSampleRad}};  // 黄金构型样本
        p.metricFacts.envelope = f.envelope;
        p.metricFacts.linkMasses = f.linkMasses;
        p.metricFacts.pointMargins = f.pointMargins;  // margin-missing 行为空表
        return p;
    }

private:
    std::map<std::string, OrchestratorRig::FactRow> m_facts;
    double m_qmin;  // rad
    double m_qmax;  // rad
};

/// 限位探针替身（policy 探针接缝的可控替身——出界构型产出 must 级发现；
/// 阈值唯一来自黄金限位——接缝签名零阈值参数纪律的测试面镜像）。
class GoldenJointLimitProbe final : public opt::IJointLimitProbe
{
public:
    GoldenJointLimitProbe(const OrchestratorRig& rig)
        : m_rig(rig)
    {
    }

    opt::JointLimitProbeResult probeJointLimits(
        const opt::JointLimitProbeRequest& request) const override
    {
        opt::JointLimitProbeResult r;
        r.completed = true;
        for (const auto& joint : request.joints) {
            if (!joint.qmin.has_value() || !joint.qmax.has_value()) {
                continue;  // 无限位关节（continuous）——无出界面
            }
            for (const auto& configuration : request.configurations) {
                if (configuration.size() != request.joints.size()) {
                    continue;  // 形状违约（测试夹具自保证——防御跳过）
                }
                const double q = configuration[&joint - request.joints.data()];
                if (q > *joint.qmax || q < *joint.qmin) {
                    opt::JointLimitFindingRecord finding;
                    finding.jointSubject = joint.jointSubject;
                    finding.kindToken = "limit-violated";  // policy 五值词表
                    finding.levelToken = "must";           // must 级——硬约束违例
                    finding.actualValue = q;               // rad
                    finding.thresholdValue =
                        (q > *joint.qmax) ? *joint.qmax : *joint.qmin;  // rad
                    finding.unitSymbol = "rad";
                    r.findings.push_back(std::move(finding));
                }
            }
        }
        return r;
    }

private:
    const OrchestratorRig& m_rig;
};

/// 碰撞探针替身（Completed 零发现——静态碰撞子集无违例素材）。
class NopCollisionProbe final : public opt::IStaticCollisionProbe
{
public:
    opt::StaticCollisionProbeResult probeSampleSet(
        const std::vector<std::vector<double>>&) const override
    {
        opt::StaticCollisionProbeResult r;
        r.completed = true;
        return r;
    }
};

/// 编译替身（编译成功——物化链故障面归 T04 用例）。
class OkCompiler final : public opt::ICandidateCompiler
{
public:
    opt::CandidateCompileResult
    compile(const opt::CandidatePatch&, const std::vector<opt::VariableBinding>&) const override
    {
        opt::CandidateCompileResult r;
        r.ok = true;
        return r;
    }
};

/// 评估调用上下文替身（零取消/零进度）。
class NopEvaluationContext final : public ev::IEvaluationContext
{
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }
};

/// 修订闭包来源替身（全部 (oid,cv) 在册——快照组装协议）。
class AllPresentClosure final : public ev::IRevisionClosureSource
{
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 非零内容身份（测试构造——避免保留值）。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// Pareto 前沿成员展开（黄金侧——metrics 对象按激活目标声明序取值）。
std::vector<optgolden::ParetoMember> paretoMembersFromGolden(
    const tk::JsonValue& expected, const tk::JsonValue& entries,
    const std::vector<opt::ObjectiveEntry>& objectiveOrder)
{
    std::vector<optgolden::ParetoMember> members;
    // 候选 id → 指标对象（golden.candidates[*]）。
    std::map<std::string, const tk::JsonValue*> metricsById;
    if (const tk::JsonValue* cands = expected.find("candidates");
        cands != nullptr && cands->isArray()) {
        for (const tk::JsonValue& c : cands->items) {
            metricsById[str(c, "candidateId")] = c.find("metrics");
        }
    }
    for (const tk::JsonValue& e : entries.items) {
        optgolden::ParetoMember m;
        m.candidateId = str(e, "candidateId");
        m.nondominationRank = static_cast<std::size_t>(num(e, "nondominationRank"));
        m.paretoNondominated =
            e.find("paretoNondominated") != nullptr && e.find("paretoNondominated")->boolean;
        m.isBaseline = e.find("isBaseline") != nullptr && e.find("isBaseline")->boolean;
        const auto it = metricsById.find(m.candidateId);
        if (it == metricsById.end() || it->second == nullptr) {
            ADD_FAILURE() << "黄金前沿成员缺指标对象: " << m.candidateId;
            continue;
        }
        // 激活目标静态名序（包络/结构质量/最小关节裕量——SetMatchTraits
        // numerics 固定序；缺指标成员不入前沿——黄金侧 null 即缺陷）。
        static const char* kFields[3] = {"envelope", "structuralMass", "minJointMargin"};
        (void)objectiveOrder;
        for (std::size_t k = 0; k < 3; ++k) {
            const auto v = optNum(*it->second, kFields[k]);
            if (!v.has_value()) {
                ADD_FAILURE() << "黄金前沿成员指标缺失（应不入前沿）: " << m.candidateId;
                m.metrics[k] = 0.0;
                continue;
            }
            m.metrics[k] = *v;
        }
        members.push_back(std::move(m));
    }
    return members;
}

}  // namespace

/// Pareto 套件夹具（GoldenFixture 六步——数据集 opt-pareto-golden@1.0.0）。
class OptGoldenPareto : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override
    {
        return {"opt-pareto-golden", "1.0.0"};
    }
};

/**
 * 编排终态黄金用例（acceptance 1——AT-09/OPT-VER-122/124/128）：
 * 黄金事实驱动的两级编排复算（Quick 批 9 候选→零筛选设计幸存 6→Verified
 * 复核→Pareto）——逐候选状态对照（限位 Must 违例→Infeasible／激活指标
 * 缺失→DataInsufficient／其余 Feasible）＋不可行/数据不足候选绝不出现于
 * 可行集与非支配集＋审计计数对照＋Pareto 集合等价（SetMatchTraits 域侧
 * 特化）＋稳定序三键逐位对照＋三项静态指标档案容差对照。
 */
TEST_F(OptGoldenPareto, OrchestratorRunMatchesGolden_WP20T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"OPT-03", "OPT-04", "OPT-06",
                                            "OPT-07", "NFR-COR-02", "NFR-COR-03"}),
                  (std::vector<std::string>{"AT-09"}),
                  tk::DatasetRef{"opt-pareto-golden", "1.0.0"}, "opt-golden@1.0.0");
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/pareto-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/pareto-expected.json", false);
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    // ---- 装配（黄金配置＋零容差默认目标集——acceptance 3 的默认面）。
    opt::OptimizationConfiguration config = configFromJson(inputs);
    config.objectives = objectivesFromJson(inputs, "objectivesZeroTolerance");
    ASSERT_NO_THROW(opt::validateConfiguration(config));

    OrchestratorRig rig(inputs);
    // 投影查表：patchId（黄金 candidates[*].patchId——期望侧身份链）→
    // factKey → 事实行。查表在测试体装配（需要黄金期望侧——rig 只见输入侧）。
    const tk::JsonValue* goldenCandidatesForMap = expected.find("candidates");
    ASSERT_TRUE(goldenCandidatesForMap != nullptr && goldenCandidatesForMap->isArray());
    std::map<std::string, OrchestratorRig::FactRow> factsByPatchId;
    for (const tk::JsonValue& gc : goldenCandidatesForMap->items) {
        const OrchestratorRig::FactRow* row = rig.factByKey(str(gc, "factKey"));
        if (row == nullptr) {
            ADD_FAILURE() << "黄金事实键未命中: " << str(gc, "factKey");
            continue;
        }
        factsByPatchId[str(gc, "patchId")] = *row;
    }
    FactProjector projector(std::move(factsByPatchId), rig.qmin(), rig.qmax());
    GoldenJointLimitProbe jointProbe(rig);
    NopCollisionProbe collisionProbe;
    OkCompiler compiler;
    NopEvaluationContext ctx;
    opt::ParetoFrontBuilder paretoBuilder;

    opt::StaticHardConstraintDeps pipelineDeps;
    pipelineDeps.evaluators = &rig.evaluators;
    pipelineDeps.jointLimitProbe = &jointProbe;
    pipelineDeps.collisionProbe = &collisionProbe;
    pipelineDeps.compiler = &compiler;
    pipelineDeps.producers = &rig.evaluators;  // EvaluatorRegistry 实现 IProducerRegistryView
    pipelineDeps.profiles = &rig.profiles;     // EvidenceProfileRegistry 实现 IProfileRegistryView
    opt::StaticHardConstraintPipeline pipeline(opt::OptimizationStage::StageB, pipelineDeps);

    opt::TwoStageOrchestratorDeps deps;
    deps.pipeline = &pipeline;
    deps.projector = &projector;
    deps.paretoBuilder = &paretoBuilder;
    deps.cache = nullptr;  // 无缓存会话——缓存判定链归契约测试（T06 面）
    opt::TwoStageEvaluationOrchestrator orchestrator(opt::OptimizationStage::StageB, deps);

    // ---- 冻结请求（黄金基线身份——候选身份链的 SHA-256 输入面）。
    const tk::JsonValue* baselineJson = inputs.find("baseline");
    ASSERT_TRUE(baselineJson != nullptr && baselineJson->isObject());
    const auto baselineRoot =
        core::ObjectId::fromCanonical(str(*baselineJson, "objectOid"));
    const auto baselineCv =
        core::ContentVersion::fromCanonical(str(*baselineJson, "contentVersion"));

    std::vector<ev::CaseEntry> cases;
    cases.push_back({core::ObjectId::generate(), "额定工况", true, true});
    cases.push_back({core::ObjectId::generate(), "极限工况", true, true});
    ev::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    ev::ObjectRefEntry design;
    design.objectId = core::ObjectId::generate();
    design.contentVersion = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000a1");
    design.objectTypeToken = "robot-design";
    design.digest = design.contentVersion.bytes;
    builder.addObjectRef(design);
    ev::PolicyRef policy;
    policy.policyContentIdentity = makeContentIdentity(4);
    builder.setPolicyRef(policy);
    ev::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = makeContentIdentity(5);
    builder.setNameMapRef(nameMap);
    for (const auto& c : cases) {
        builder.addCase(c);
    }
    ev::ReproductionBlock repro;
    repro.productVersion = "wp20-t11-golden";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    const ev::AnalysisSnapshot snapshot = builder.build(AllPresentClosure{});

    opt::TwoStageRunRequest request;
    request.config = config;
    request.baselineRoot = baselineRoot;
    request.baselineCv = baselineCv;
    request.snapshot = snapshot;
    request.task = {core::ProjectId::generate(), core::BranchId::generate(),
                    core::RevisionId::generate(), core::RunId::generate(),
                    core::AttemptId{1U}};
    request.profile.profileId = "kin";
    request.profile.version = "1.0";
    request.profile.contentIdentity = rig.profileIdentity;
    // 覆盖矩阵（Quick/Verified 均全必验执行——零筛选设计的覆盖完备面）。
    auto makeCoverage = [&](bool executedAll) {
        ev::CaseCoverageMatrix matrix;
        matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
        for (const auto& entry : snapshot.caseSet.entries) {
            ev::CaseCoverageEntry row;
            row.caseId = entry.caseId;
            row.status = executedAll ? ev::CaseExecutionStatus::Executed
                                     : ev::CaseExecutionStatus::NotExecuted;
            if (executedAll) {
                row.runId = core::RunId::generate();
                row.resultSliceId = makeContentIdentity(6);
            }
            matrix.entries.push_back(std::move(row));
        }
        return matrix;
    };
    request.coverage = makeCoverage(true);
    request.quickCoverage = makeCoverage(true);

    // ---- 两级编排复算（产品公共接口）。
    opt::TwoStageRunResult run;
    ASSERT_NO_THROW(run = orchestrator.run(request, ctx));
    EXPECT_EQ(run.runPhase, opt::RunPhase::Completed);
    EXPECT_TRUE(run.runCompleted);
    EXPECT_FALSE(run.searchEmpty) << "可行集非空（6 个 Feasible）——非搜索空";

    // ---- 逐候选状态对照（黄金 expected.candidates——批序 index 对齐）。
    const tk::JsonValue* goldenCandidates = expected.find("candidates");
    ASSERT_TRUE(goldenCandidates != nullptr && goldenCandidates->isArray());
    ASSERT_EQ(run.quickRecords.size(), goldenCandidates->items.size())
        << "Quick 批候选数与黄金不符";
    // 批序＝黄金序（FactProjector 按调用序铺位——身份链再核对一次）。
    for (std::size_t i = 0; i < goldenCandidates->items.size(); ++i) {
        const tk::JsonValue& gc = goldenCandidates->items[i];
        const opt::TwoStageRunRecord& record = run.quickRecords[i];
        SCOPED_TRACE(std::string("候选[") + std::to_string(i) + "] "
                     + str(gc, "label"));
        // 身份链对照（§4.2 SHA-256——黄金独立复算）。
        EXPECT_EQ(record.candidateId.toCanonical(), str(gc, "candidateId"));
        EXPECT_EQ(record.isBaseline, gc.find("isBaseline") != nullptr
                                         && gc.find("isBaseline")->boolean);
        // 状态对照（§7.5 判定流封闭推导面）。
        const std::string goldenStatus = str(gc, "expectedStatus");
        std::optional<opt::CandidateStatus> status;
        for (int s = 0; s <= static_cast<int>(opt::CandidateStatus::ParetoNondominated);
             ++s) {
            const auto candidate = static_cast<opt::CandidateStatus>(s);
            if (std::string(opt::toToken(candidate)) == goldenStatus) {
                status = candidate;
                break;
            }
        }
        ASSERT_TRUE(status.has_value()) << "黄金状态 token 未知: " << goldenStatus;
        EXPECT_EQ(record.status, *status)
            << "候选状态失配（黄金封闭推导 vs 管线判定）";
        // Quick 记录效力面（screening-only——EVI-01 表 1）。
        EXPECT_TRUE(record.screeningOnly);
        EXPECT_FALSE(record.formalPassEligible);
    }

    // ---- 审计计数对照（黄金 expected.audit）。
    if (const tk::JsonValue* audit = expected.find("audit");
        audit != nullptr && audit->isObject()) {
        EXPECT_EQ(run.audit.candidatesGenerated,
                  static_cast<std::uint32_t>(uintOf(*audit, "candidatesGenerated")));
        EXPECT_EQ(run.audit.quickEvaluated,
                  static_cast<std::uint32_t>(uintOf(*audit, "quickEvaluated")));
        EXPECT_EQ(run.audit.quickScreenedOut,
                  static_cast<std::uint32_t>(uintOf(*audit, "quickScreenedOut")))
            << "零筛选设计下无保守淘汰（Quick-Feasible ≤ 复核预算）";
        EXPECT_EQ(run.audit.verifiedEvaluated,
                  static_cast<std::uint32_t>(uintOf(*audit, "verifiedEvaluated")));
    }

    // ---- 不可行/数据不足候选绝不入可行集与非支配集（OPT-VER-122 核心）。
    std::map<std::string, std::string> statusById;  // candidateId → 黄金状态
    for (const auto& gc : goldenCandidates->items) {
        statusById[str(gc, "candidateId")] = str(gc, "expectedStatus");
    }
    const auto idText = [](const opt::CandidateId& id) { return id.toCanonical(); };
    for (const auto& entry : run.pareto.feasibleIds) {
        const auto it = statusById.find(idText(entry));
        ASSERT_NE(it, statusById.end());
        EXPECT_EQ(it->second, "feasible")
            << "非 Feasible 候选进入可行集（AT-09 红线——" << it->first << "）";
    }
    for (const auto& entry : run.pareto.nondominatedIds) {
        const auto it = statusById.find(idText(entry));
        ASSERT_NE(it, statusById.end());
        EXPECT_EQ(it->second, "feasible")
            << "非 Feasible 候选进入非支配集（AT-09 红线——" << it->first << "）";
    }

    // ---- Verified 记录效力面＋三项静态指标档案容差对照（OPT-VER-124）。
    for (const auto& record : run.verifiedRecords) {
        EXPECT_FALSE(record.screeningOnly) << "Verified 记录非 screening-only";
        const auto it = statusById.find(idText(record.candidateId));
        ASSERT_NE(it, statusById.end());
        EXPECT_EQ(it->second, "feasible") << "Verified 批混入非可行候选";
        // 黄金指标（该候选 metrics 对象）逐项档案容差对照。
        const tk::JsonValue* gc = nullptr;
        for (const auto& cand : goldenCandidates->items) {
            if (str(cand, "candidateId") == idText(record.candidateId)) {
                gc = &cand;
                break;
            }
        }
        ASSERT_NE(gc, nullptr);
        const tk::JsonValue* metrics = gc->find("metrics");
        ASSERT_TRUE(metrics != nullptr && metrics->isObject());
        struct MetricProbe
        {
            const char* fieldPath;  // 档案 resolve 键＝断言标签（模板条目名）
            const char* jsonField;  // 黄金 metrics 对象字段名
            opt::MetricId id;       // 指标枚举
            const char* unitToken;  // SI 单位符号（档案 unit 通道）
        };
        static const MetricProbe probes[] = {
            {"metrics.envelope", "envelope", opt::MetricId::Envelope, "m"},
            {"metrics.structuralMass", "structuralMass", opt::MetricId::StructuralMass,
             "kg"},
            {"metrics.minJointMargin", "minJointMargin",
             opt::MetricId::MinJointMargin, "1"},
        };
        for (const auto& probe : probes) {
            const auto goldenValue = optNum(*metrics, probe.jsonField);
            const auto actualValue = record.metrics.valueOf(probe.id);
            EXPECT_EQ(actualValue.has_value(), goldenValue.has_value())
                << "指标存在性失配（黄金 null＝\"—\"不按 0 合成——NFR-COR-03）: "
                << probe.jsonField;
            if (goldenValue.has_value() && actualValue.has_value()) {
                IRD_EXPECT_CLOSE(probe.fieldPath, *actualValue, *goldenValue, *profile,
                                 probe.unitToken);
            } else if (!goldenValue.has_value() && actualValue.has_value()) {
                ADD_FAILURE() << "黄金缺指标而产品产出值（缺失语义失配）: "
                              << probe.jsonField;
            }
        }
        // margin-missing 候选的缺失原因 token（kMetricGap* 记录面——§7.1 行 3）。
        if (it->second == "data-insufficient") {
            EXPECT_FALSE(
                record.metrics.valueOf(opt::MetricId::MinJointMargin).has_value());
            EXPECT_EQ(record.metrics
                          .metrics[static_cast<std::size_t>(
                              opt::MetricId::MinJointMargin)]
                          .gapToken,
                      opt::kMetricGapSourceMissing)
                << "裕量缺失原因 token 失配（黄金 margin-missing 场景）";
        }
    }

    // ---- Pareto 集合等价（SetMatchTraits 域侧特化）＋稳定序三键逐位对照。
    const tk::JsonValue* goldenRun = expected.find("run");
    ASSERT_TRUE(goldenRun != nullptr && goldenRun->isObject());
    const tk::JsonValue* goldenEntries = goldenRun->find("entries");
    ASSERT_TRUE(goldenEntries != nullptr && goldenEntries->isArray());
    const auto goldenMembers =
        paretoMembersFromGolden(expected, *goldenEntries, config.objectives.entries);

    // 实际侧成员（编排 Pareto 输出＋Verified 记录指标——按激活目标声明序）。
    std::map<std::string, const opt::TwoStageRunRecord*> recordsById;
    for (const auto& record : run.verifiedRecords) {
        recordsById[idText(record.candidateId)] = &record;
    }
    std::vector<optgolden::ParetoMember> actualMembers;
    for (const auto& entry : run.pareto.entries) {
        const auto it = recordsById.find(idText(entry.candidateId));
        ASSERT_NE(it, recordsById.end())
            << "前沿成员无 Verified 记录（Quick 记录混入 Pareto 的类型学破口）";
        optgolden::ParetoMember m;
        m.candidateId = idText(entry.candidateId);
        m.nondominationRank = entry.nondominationRank;
        m.paretoNondominated = entry.paretoNondominated;
        m.isBaseline = entry.isBaseline;
        for (std::size_t k = 0; k < config.objectives.entries.size() && k < 3; ++k) {
            const auto v = it->second->metrics.valueOf(
                config.objectives.entries[k].metricId);
            ASSERT_TRUE(v.has_value()) << "前沿成员激活指标缺失（应被管线排除）";
            m.metrics[k] = *v;
        }
        actualMembers.push_back(std::move(m));
    }

    const tk::SetCheckResult setResult =
        tk::checkSetEquivalent(goldenMembers, actualMembers, *profile,
                               tk::SetMatchTraits<optgolden::ParetoMember>{});
    EXPECT_TRUE(setResult.equivalent)
        << "Pareto 集合与黄金不等价（缺失 " << setResult.missingExpected.size()
        << "/多余 " << setResult.extraActual.size() << "）";
    EXPECT_TRUE(setResult.duplicates.empty());

    // 稳定序三键逐位对照（rank→声明序目标→CandidateId——黄金独立复算序）。
    ASSERT_EQ(run.pareto.entries.size(), goldenEntries->items.size());
    for (std::size_t i = 0; i < goldenEntries->items.size(); ++i) {
        const tk::JsonValue& ge = goldenEntries->items[i];
        const opt::ParetoFrontEntry& ae = run.pareto.entries[i];
        EXPECT_EQ(idText(ae.candidateId), str(ge, "candidateId"))
            << "稳定序第 " << i << " 位失配（三键序黄金面）";
        EXPECT_EQ(ae.nondominationRank,
                  static_cast<std::size_t>(num(ge, "nondominationRank")));
        EXPECT_EQ(ae.paretoNondominated,
                  ge.find("paretoNondominated") != nullptr
                      && ge.find("paretoNondominated")->boolean);
        EXPECT_EQ(ae.isBaseline, ge.find("isBaseline") != nullptr
                                     && ge.find("isBaseline")->boolean);
    }
    // 双集合显式面（§7.4——可行集/非支配集保持 entries 序）。
    if (const tk::JsonValue* ids = goldenRun->find("nondominatedIds");
        ids != nullptr && ids->isArray()) {
        ASSERT_EQ(run.pareto.nondominatedIds.size(), ids->items.size());
        for (std::size_t i = 0; i < ids->items.size(); ++i) {
            EXPECT_EQ(idText(run.pareto.nondominatedIds[i]), ids->items[i].text);
        }
    }
    if (const tk::JsonValue* ids = goldenRun->find("feasibleIds");
        ids != nullptr && ids->isArray()) {
        ASSERT_EQ(run.pareto.feasibleIds.size(), ids->items.size());
        for (std::size_t i = 0; i < ids->items.size(); ++i) {
            EXPECT_EQ(idText(run.pareto.feasibleIds[i]), ids->items[i].text);
        }
    }
    if (const tk::JsonValue* dropped = goldenRun->find("duplicatesDropped");
        dropped != nullptr && dropped->isNumber()) {
        EXPECT_EQ(run.pareto.duplicatesDropped,
                  static_cast<std::uint32_t>(dropped->number));
    }
}

/**
 * 显式容差支配黄金用例（acceptance 3——AT-09 容差支配/OPT-VER-132 黄金面）：
 * 同一可行候选集在两套目标集下经 IParetoFrontBuilder 公共接口复算——默认
 * 零容差前沿与黄金 run 面一致；显式配置容差（数据集场景声明——包络
 * ε_abs 0.25/裕量 ε_abs 0.05）前沿与黄金 explicitToleranceScenario 一致
 * ＋支配关系翻转记录核对＋产品默认零容差钉扎（P-OPT-5 裁决前不预设）。
 */
TEST_F(OptGoldenPareto, ExplicitToleranceFlipsFront_WP20T11_ACC3)
{
    IRD_TEST_INFO((std::vector<std::string>{"OPT-04", "NFR-COR-02"}),
                  (std::vector<std::string>{"AT-09"}),
                  tk::DatasetRef{"opt-pareto-golden", "1.0.0"}, "opt-golden@1.0.0");
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/pareto-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/pareto-expected.json", false);
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    // ---- 产品默认零容差钉扎（P-OPT-5/DOPT-5——defaultObjectives 全零容差，
    //      非零容差只存在于黄金场景显式声明）。
    for (const auto& entry : opt::defaultObjectives(opt::OptimizationStage::StageB).entries) {
        EXPECT_EQ(entry.tolerance.relative, 0.0);
        EXPECT_EQ(entry.tolerance.absolute, 0.0);
    }

    // ---- 可行成员集（黄金 candidates——Feasible 六成员，含指标值）。
    const tk::JsonValue* goldenCandidates = expected.find("candidates");
    ASSERT_TRUE(goldenCandidates != nullptr && goldenCandidates->isArray());
    const tk::JsonValue* objZero = inputs.find("objectivesZeroTolerance");
    ASSERT_TRUE(objZero != nullptr && objZero->isArray());
    const opt::ObjectiveSet zeroSet = objectivesFromJson(inputs, "objectivesZeroTolerance");
    const opt::ObjectiveSet explicitSet =
        objectivesFromJson(inputs, "objectivesExplicitTolerance");
    // 显式容差确非零（数据集场景声明——否则本用例失去观测面）。
    bool anyNonZero = false;
    for (const auto& entry : explicitSet.entries) {
        anyNonZero = anyNonZero || entry.tolerance.absolute > 0.0
                     || entry.tolerance.relative > 0.0;
    }
    ASSERT_TRUE(anyNonZero) << "显式容差场景全零——黄金场景声明缺陷";

    const auto buildCandidate = [&](const tk::JsonValue& c) {
        opt::FeasibleCandidate fc;
        // "cnd-<hex>" → Digest256 位模式（同 hex 必同字节——内容寻址键的
        // 测试侧装载；CandidateId 只承载字节，无第二解析面）。
        fc.candidateId.bytes = core::ContentIdentity::fromCanonical(
                                   "cid-" + str(c, "candidateId").substr(4))
                                   .bytes;
        fc.isBaseline = c.find("isBaseline") != nullptr && c.find("isBaseline")->boolean;
        fc.metricValues.assign(opt::kMetricCount, std::nullopt);
        const tk::JsonValue* metrics = c.find("metrics");
        if (metrics != nullptr && metrics->isObject()) {
            if (const auto v = optNum(*metrics, "envelope")) {
                fc.metricValues[static_cast<std::size_t>(opt::MetricId::Envelope)] = *v;
            }
            if (const auto v = optNum(*metrics, "structuralMass")) {
                fc.metricValues[static_cast<std::size_t>(opt::MetricId::StructuralMass)] = *v;
            }
            if (const auto v = optNum(*metrics, "minJointMargin")) {
                fc.metricValues[static_cast<std::size_t>(opt::MetricId::MinJointMargin)] = *v;
            }
        }
        return fc;
    };
    std::vector<opt::FeasibleCandidate> feasible;
    std::map<std::string, opt::FeasibleCandidate> byId;
    for (const auto& c : goldenCandidates->items) {
        if (str(c, "expectedStatus") != "feasible") {
            continue;  // 不可行/数据不足候选不入前沿输入（管线侧排除的黄金面）
        }
        feasible.push_back(buildCandidate(c));
        byId[str(c, "candidateId")] = feasible.back();
    }
    ASSERT_EQ(feasible.size(), 6U) << "可行成员数与黄金设计不符";

    const opt::ParetoFrontBuilder builder;  // 产品唯一实现（IParetoFrontBuilder 面）

    // ---- 零容差前沿（默认面——与黄金 run 面一致）。
    const opt::ParetoFrontResult zeroFront = builder.buildFront(feasible, zeroSet);
    const tk::JsonValue* goldenRun = expected.find("run");
    ASSERT_TRUE(goldenRun != nullptr);
    const tk::JsonValue* zeroEntries = goldenRun->find("entries");
    ASSERT_TRUE(zeroEntries != nullptr && zeroEntries->isArray());
    ASSERT_EQ(zeroFront.entries.size(), zeroEntries->items.size());
    for (std::size_t i = 0; i < zeroEntries->items.size(); ++i) {
        const tk::JsonValue& ge = zeroEntries->items[i];
        const opt::ParetoFrontEntry& ae = zeroFront.entries[i];
        EXPECT_EQ(ae.candidateId.toCanonical(), str(ge, "candidateId"))
            << "零容差稳定序第 " << i << " 位失配";
        EXPECT_EQ(ae.nondominationRank,
                  static_cast<std::size_t>(num(ge, "nondominationRank")));
        EXPECT_EQ(ae.paretoNondominated,
                  ge.find("paretoNondominated") != nullptr
                      && ge.find("paretoNondominated")->boolean);
    }

    // ---- 显式容差前沿（黄金 explicitToleranceScenario——翻转面）。
    const opt::ParetoFrontResult explicitFront = builder.buildFront(feasible, explicitSet);
    const tk::JsonValue* scenario = expected.find("explicitToleranceScenario");
    ASSERT_TRUE(scenario != nullptr && scenario->isObject());
    const tk::JsonValue* explicitEntries = scenario->find("entries");
    ASSERT_TRUE(explicitEntries != nullptr && explicitEntries->isArray());
    ASSERT_EQ(explicitFront.entries.size(), explicitEntries->items.size());
    for (std::size_t i = 0; i < explicitEntries->items.size(); ++i) {
        const tk::JsonValue& ge = explicitEntries->items[i];
        const opt::ParetoFrontEntry& ae = explicitFront.entries[i];
        EXPECT_EQ(ae.candidateId.toCanonical(), str(ge, "candidateId"))
            << "显式容差稳定序第 " << i << " 位失配（容差只进支配判定不进排序键）";
        EXPECT_EQ(ae.nondominationRank,
                  static_cast<std::size_t>(num(ge, "nondominationRank")));
        EXPECT_EQ(ae.paretoNondominated,
                  ge.find("paretoNondominated") != nullptr
                      && ge.find("paretoNondominated")->boolean);
    }
    if (const tk::JsonValue* ids = scenario->find("nondominatedIds");
        ids != nullptr && ids->isArray()) {
        ASSERT_EQ(explicitFront.nondominatedIds.size(), ids->items.size());
        for (std::size_t i = 0; i < ids->items.size(); ++i) {
            EXPECT_EQ(explicitFront.nondominatedIds[i].toCanonical(), ids->items[i].text);
        }
    }

    // ---- 支配翻转记录核对（黄金 gained 非空；逐对经 dominates 公共纯函数
    //      复核：零容差不支配→显式容差支配。对格式＝"a->b" 箭头分隔串——
    //      黄金脚本 dominanceMatrix 的书写面）。
    const tk::JsonValue* gained = scenario->find("dominancePairsGainedUnderTolerance");
    ASSERT_TRUE(gained != nullptr && gained->isArray());
    ASSERT_FALSE(gained->items.empty()) << "黄金翻转记录为空（数据集设计缺陷）";
    for (const tk::JsonValue& pair : gained->items) {
        ASSERT_TRUE(pair.isString());
        const std::string pairText = pair.text;
        const auto sep = pairText.find("->");
        ASSERT_NE(sep, std::string::npos)
            << "翻转对格式非法（应为 \"a->b\"）: " << pairText;
        const std::string aText = pairText.substr(0, sep);
        const std::string bText = pairText.substr(sep + 2);
        const auto ia = byId.find(aText);
        const auto ib = byId.find(bText);
        ASSERT_NE(ia, byId.end());
        ASSERT_NE(ib, byId.end());
        EXPECT_FALSE(opt::dominates(ia->second, ib->second, zeroSet))
            << "零容差下已支配（翻转记录失真）: " << aText << "->" << bText;
        EXPECT_TRUE(opt::dominates(ia->second, ib->second, explicitSet))
            << "显式容差下未支配（翻转未发生）: " << aText << "->" << bText;
    }
}

// =====================================================================
// 套件三：采用守卫黄金（opt-adopt-golden——AT-12/OPT-VER-143/144/145）
// =====================================================================

namespace {

/// 采用守卫套件夹具（GoldenFixture 六步——数据集 opt-adopt-golden@1.0.0）。
class OptGoldenAdopt : public tk::GoldenFixture
{
protected:
    tk::DatasetRef datasetRef() const override
    {
        return {"opt-adopt-golden", "1.0.0"};
    }
};

/// 迷你规范格式 "IRDDSGN1" 设计表字节（物化缝黄金——与 generate 脚本同式：
/// u32 版本 1＋u16 字段数＋每字段〔u16 名称长＋名称 UTF-8＋f64 小端〕按
/// 名称字典序；std::map 天然有序——与 JS Array#sort 字节序一致）。
std::vector<std::uint8_t> designTableBytes(const std::map<std::string, double>& table)
{
    std::vector<std::uint8_t> out;
    const auto appendU16 = [&out](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFU));
    };
    const auto appendU32 = [&out](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU));
        }
    };
    for (const char c : "IRDDSGN1") {
        if (c != '\0') {
            out.push_back(static_cast<std::uint8_t>(c));
        }
    }
    appendU32(1U);
    appendU16(static_cast<std::uint16_t>(table.size()));
    for (const auto& [name, value] : table) {
        appendU16(static_cast<std::uint16_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        // -0.0 规范化为 +0.0（位模式确定性——与黄金脚本一致）。
        const double v = (value == 0.0) ? 0.0 : value;
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "double 须为 64 位 IEEE 754");
        std::memcpy(&bits, &v, sizeof(bits));
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFU));
        }
    }
    return out;
}

/// 迷你规范格式解析（差异预览缝输入——两侧同构字节 → 字段表）。
std::map<std::string, double> parseDesignTable(const std::vector<std::uint8_t>& bytes)
{
    std::map<std::string, double> table;
    std::size_t pos = 0;
    const auto readU16 = [&]() {
        const std::uint16_t v = static_cast<std::uint16_t>(
            bytes[pos] | (static_cast<std::uint16_t>(bytes[pos + 1]) << 8));
        pos += 2;
        return v;
    };
    const auto readU32 = [&]() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(bytes[pos + static_cast<std::size_t>(i)])
                 << (8 * i);
        }
        pos += 4;
        return v;
    };
    const std::string magic(bytes.begin(), bytes.begin() + 8);
    if (magic != "IRDDSGN1") {
        ADD_FAILURE() << "设计字节 magic 非法: " << magic;
        return table;
    }
    pos = 8;
    (void)readU32();  // 格式版本（黄金恒 1）
    const std::uint16_t count = readU16();
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint16_t len = readU16();
        const std::string name(bytes.begin() + static_cast<long>(pos),
                               bytes.begin() + static_cast<long>(pos + len));
        pos += len;
        std::uint64_t bits = 0;
        for (int k = 0; k < 8; ++k) {
            bits |= static_cast<std::uint64_t>(bytes[pos + static_cast<std::size_t>(k)])
                    << (8 * k);
        }
        pos += 8;
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof(v));
        table[name] = v;
    }
    return table;
}

/// 六位小数固定格式（差异预览文本——与黄金脚本 toFixed(6) 同形；黄金表值
/// 均为 6 位小数内精确表示，无舍入歧义）。
std::string fmt6(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return buf;
}

/// 黄金输入的基线设计表（inputs.baselineDesignTable → 有序表）。
std::map<std::string, double> baselineTableFromGolden(const tk::JsonValue& inputs)
{
    std::map<std::string, double> table;
    if (const tk::JsonValue* t = inputs.find("baselineDesignTable");
        t != nullptr && t->isObject()) {
        // JsonLite 对象成员序＝插入序；std::map 承载后按名称字典序（与
        // 黄金脚本 Object.keys().sort() 的字节序一致——序列化序单点）。
        for (const auto& [key, value] : t->members) {
            if (value.isNumber()) {
                table[key] = value.number;
            }
        }
    }
    return table;
}

/// 补丁值 → 设计字段映射（inputs.variables[*].designField——黄金显式声明）。
std::map<std::string, std::string> designFieldMapFromGolden(const tk::JsonValue& inputs)
{
    std::map<std::string, std::string> map;
    if (const tk::JsonValue* vars = inputs.find("variables");
        vars != nullptr && vars->isArray()) {
        for (const tk::JsonValue& v : vars->items) {
            map[str(v, "bindingId")] = str(v, "designField");
        }
    }
    return map;
}

/// 物化缝替身（P-OPT-3 裁决前测试供给——补丁覆盖基线表后按迷你规范格式
/// 编码；同补丁同绑定 ⇒ 同字节——NFR-COR-02 缝实现侧纪律）。
class GoldenMaterializer final : public opt::ICandidateDesignMaterializer
{
public:
    GoldenMaterializer(std::map<std::string, double> baselineTable,
                       std::map<std::string, std::string> designFields)
        : m_baseline(std::move(baselineTable))
        , m_fields(std::move(designFields))
    {
    }

    std::vector<std::uint8_t> materialize(
        const opt::CandidatePatch& patch,
        const std::vector<opt::VariableBinding>&) const override
    {
        std::map<std::string, double> table = m_baseline;  // 基线全量＋补丁覆盖
        for (const opt::PatchItem& item : patch.items) {
            const auto it = m_fields.find(item.bindingId);
            if (it == m_fields.end()) {
                ADD_FAILURE() << "补丁绑定缺设计字段映射: " << item.bindingId;
                continue;
            }
            table[it->second] = item.scalarValue;
        }
        ++m_calls;
        return designTableBytes(table);
    }

    int calls() const noexcept { return m_calls; }

private:
    std::map<std::string, double> m_baseline;
    std::map<std::string, std::string> m_fields;
    mutable int m_calls = 0;
};

/// 差异预览缝替身（P-OPT-8——两侧迷你格式闭式 diff；modified 条目按字段名
/// 字典序——modeling diff 投影的黄金形态）。
class GoldenDiffSource final : public opt::ICandidateDiffSource
{
public:
    opt::CandidateDiffPreview diff(
        const std::vector<std::uint8_t>& baselineDesignCanonical,
        const std::vector<std::uint8_t>& candidateDesignCanonical) const override
    {
        ++m_calls;
        const auto baselineTable = parseDesignTable(baselineDesignCanonical);
        const auto candidateTable = parseDesignTable(candidateDesignCanonical);
        opt::CandidateDiffPreview preview;
        for (const auto& [name, baselineValue] : baselineTable) {
            const auto it = candidateTable.find(name);
            if (it == candidateTable.end() || it->second == baselineValue) {
                continue;  // 缺失/同值——非 modified 条目（黄金场景无增删）
            }
            opt::CandidateDiffEntry entry;
            entry.group = "parameters";     // MDL-08 参数组
            entry.kind = "modified";        // 变化三态
            entry.objectId = m_rootOid;     // 根对象字段条目锚（obj- 文本）
            entry.subjectPath = name;       // 值模型内字段定位
            entry.field = name;
            entry.valueChanged = true;
            entry.provenanceChanged = false;
            entry.baselineText = fmt6(baselineValue);
            entry.candidateText = fmt6(it->second);
            preview.entries.push_back(std::move(entry));
        }
        return preview;
    }

    void setRootOid(std::string oid) { m_rootOid = std::move(oid); }
    int calls() const noexcept { return m_calls; }

private:
    std::string m_rootOid;
    mutable int m_calls = 0;
};

}  // namespace

/**
 * 采用守卫黄金用例（acceptance 1——AT-12/OPT-VER-143/144/145 黄金面）：
 * 黄金身份面下的双变量复合候选组装——身份链对照（patchId/candidateId/默认
 * 分支名）＋两步命令语义对照（step1 建支〔baseRevisionId＝运行输入修订、
 * 不复制对象〕＋step2 应用〔token/payload 摘要/expectedRevision/双编译〕）
 * ＋完整复算提示恒位＋差异预览逐条目对照（传动比条目在场→零警告——
 * P-OPT-8）＋基线字节不变（组装前后摘要相等——AT-12 纯函数面）＋过期基线
 * 仅标记不阻塞（staleScenario）。
 */
TEST_F(OptGoldenAdopt, AdoptGuardPlanMatchesGolden_WP20T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"OPT-08", "PM-12", "CON-05", "NFR-COR-02"}),
                  (std::vector<std::string>{"AT-12"}),
                  tk::DatasetRef{"opt-adopt-golden", "1.0.0"}, "opt-golden@1.0.0");
    ASSERT_TRUE(dataset.has_value());
    ASSERT_TRUE(profile.has_value());

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/adopt-inputs.json", true);
    const tk::JsonValue expected = loadJson(*dataset, "expected/adopt-expected.json", false);
    ASSERT_TRUE(inputs.isObject());
    ASSERT_TRUE(expected.isObject());

    // ---- 身份面（黄金固定 canonical 文本——fromCanonical 语法违约即抛）。
    const tk::JsonValue* identity = inputs.find("identity");
    ASSERT_TRUE(identity != nullptr && identity->isObject());
    const auto project = core::ProjectId::fromCanonical(str(*identity, "project"));
    const auto branch = core::BranchId::fromCanonical(str(*identity, "branch"));
    const auto revision = core::RevisionId::fromCanonical(str(*identity, "revision"));
    const auto snapshot =
        core::ContentIdentity::fromCanonical(str(*identity, "snapshot"));
    const auto baselineRoot =
        core::ObjectId::fromCanonical(str(*identity, "baselineRoot"));
    const auto baselineCv =
        core::ContentVersion::fromCanonical(str(*identity, "baselineCv"));

    // ---- 研究绑定＋候选补丁（产品构造面——词表匹配/边界校验全执行）。
    const std::vector<opt::VariableBinding> bindings = bindingsFromJson(inputs);
    ASSERT_EQ(bindings.size(), 2U);
    std::vector<opt::PatchItem> items;
    if (const tk::JsonValue* patchItems = inputs.find("patchItems");
        patchItems != nullptr && patchItems->isArray()) {
        for (const tk::JsonValue& p : patchItems->items) {
            opt::PatchItem item;
            item.bindingId = str(p, "bindingId");
            item.scalarValue = num(p, "scalarValue");  // SI 真值（1 无量纲/m）
            items.push_back(item);
        }
    }
    const opt::CandidatePatch patch =
        opt::makeCandidatePatch(bindings, opt::OptimizationStage::StageB, items);

    // ---- 身份链黄金对照（§4.2/§5.5——node crypto 独立复算）。
    EXPECT_EQ(opt::patchIdentity(patch).toCanonical(), str(expected, "patchId"));
    const opt::CandidateId candidateId = opt::candidateIdOf(baselineRoot, baselineCv, patch);
    EXPECT_EQ(candidateId.toCanonical(), str(expected, "candidateId"));

    // ---- 运行聚合组装（Completed＋Verified-Feasible 单候选——OPT-08 归属面）。
    opt::TwoStageRunResult done;
    done.runPhase = opt::RunPhase::Completed;
    done.runCompleted = true;
    opt::TwoStageRunRecord record;
    record.candidateId = candidateId;
    record.patch = patch;
    record.isBaseline = false;
    record.mode = core::EvaluationMode::Verified;
    record.screeningOnly = false;
    record.status = opt::CandidateStatus::Feasible;
    record.formalPassEligible = true;
    done.verifiedRecords = {record};
    opt::OptimizationRunResult runResult = opt::assembleRunResult(
        opt::OptimizationRunId::generate(), project, branch, revision, snapshot,
        baselineRoot, baselineCv, opt::OptimizationConfiguration{}, done);
    runResult.config.variables = bindings;  // 运行冻结绑定（物化/警告面消费）

    // ---- 注入缝（物化＝迷你规范格式；预览＝闭式 diff——黄金 inputs 声明）。
    const std::map<std::string, double> baselineTable = baselineTableFromGolden(inputs);
    ASSERT_FALSE(baselineTable.empty());
    const auto designFields = designFieldMapFromGolden(inputs);
    GoldenMaterializer materializer(baselineTable, designFields);
    GoldenDiffSource diffSource;
    diffSource.setRootOid(str(*identity, "baselineRoot"));
    const opt::OptimizationCandidateApplier applier({&materializer, &diffSource});

    // ---- 请求（基线字节＝迷你规范格式基线表——组装前后摘要对照的锚）。
    const std::vector<std::uint8_t> baselineBytes = designTableBytes(baselineTable);
    const std::string baselineDigestBefore = sha256Hex(baselineBytes);
    opt::ApplyCandidateRequest request;
    request.run = runResult;
    request.candidateId = candidateId;
    request.baseline.branch = branch;
    request.baseline.tip = revision;   // 正面形态：tip＝运行输入修订
    request.baseline.writable = true;
    request.baselineDesignCanonical = baselineBytes;

    opt::CandidateApplyPlan plan;
    ASSERT_NO_THROW(plan = applier.buildApplyPlan(request));

    // ---- 通道化状态（黄金 plan 面）。
    EXPECT_TRUE(plan.allowApply);
    EXPECT_TRUE(plan.blockedReasons.empty());
    EXPECT_FALSE(plan.expectedStaleBaseline);
    EXPECT_TRUE(plan.recalcRequired) << "完整复算提示恒位（§10.1 RECALC）";

    // ---- step1 建支语义（黄金 plan.createBranch——OPT-VER-145 组装面）。
    EXPECT_EQ(plan.createBranch.baseRevisionId, revision);
    EXPECT_EQ(plan.createBranch.label, str(expected, "defaultBranchLabel"))
        << "默认分支名＝\"方案 \"＋候选 id 前 12 位（黄金锚）";

    // ---- step2 应用语义（黄金 plan.step2——OPT-VER-143 组装面）。
    const tk::JsonValue* step2 = expected.find("plan");
    ASSERT_TRUE(step2 != nullptr && step2->isObject());
    const tk::JsonValue* step2Golden = step2->find("step2");
    ASSERT_TRUE(step2Golden != nullptr && step2Golden->isObject());
    ASSERT_TRUE(plan.applyDesign.has_value());
    EXPECT_EQ(plan.applyDesign->commandType, str(*step2Golden, "commandToken"));
    EXPECT_EQ(plan.applyDesign->expectedRevision, revision);
    EXPECT_TRUE(plan.applyDesign->requiresDualCompile);
    // payload 摘要（SHA-256——物化缝字节面黄金对照）。
    EXPECT_EQ(sha256Hex(plan.applyDesign->payloadCanonical),
              str(*step2Golden, "payloadSha256"))
        << "候选设计字节摘要失配（物化缝与黄金迷你规范格式漂移）";

    // ---- 差异预览逐条目对照（P-OPT-8 零警告锚——传动比条目在场）。
    const tk::JsonValue* goldenPreview = step2->find("diffPreview");
    ASSERT_TRUE(goldenPreview != nullptr && goldenPreview->isObject());
    const tk::JsonValue* goldenEntryList = goldenPreview->find("entries");
    ASSERT_TRUE(goldenEntryList != nullptr && goldenEntryList->isArray());
    ASSERT_EQ(plan.diffPreview.entries.size(), goldenEntryList->items.size());
    for (std::size_t i = 0; i < goldenEntryList->items.size(); ++i) {
        const tk::JsonValue& ge = goldenEntryList->items[i];
        const opt::CandidateDiffEntry& ae = plan.diffPreview.entries[i];
        SCOPED_TRACE(std::string("diff[") + std::to_string(i) + "]");
        EXPECT_EQ(ae.group, str(ge, "group"));
        EXPECT_EQ(ae.kind, str(ge, "kind"));
        EXPECT_EQ(ae.objectId, str(ge, "objectId"));
        EXPECT_EQ(ae.subjectPath, str(ge, "subjectPath"));
        EXPECT_EQ(ae.field, str(ge, "field"));
        EXPECT_EQ(ae.valueChanged,
                  ge.find("valueChanged") != nullptr && ge.find("valueChanged")->boolean);
        EXPECT_EQ(ae.baselineText, str(ge, "baselineText"));
        EXPECT_EQ(ae.candidateText, str(ge, "candidateText"));
    }
    EXPECT_TRUE(plan.diffPreview.warnings.empty())
        << "传动比条目在 diff 中在场——零警告（P-OPT-8 不虚构面）";

    // ---- 基线字节不变（AT-12/OPT-VER-144 纯函数面——组装前后摘要相等，
    //      且与黄金基线字节摘要一致——迷你规范格式基线面无漂移）。
    const tk::JsonValue* invariance = expected.find("baselineInvariance");
    ASSERT_TRUE(invariance != nullptr && invariance->isObject());
    EXPECT_EQ(sha256Hex(request.baselineDesignCanonical), baselineDigestBefore)
        << "组装后基线字节被修改（预览候选不得修改基线——AT-12 红线）";
    EXPECT_EQ(baselineDigestBefore, str(*invariance, "baselineBytesSha256"))
        << "基线字节摘要与黄金不一致（迷你规范格式基线面漂移）";

    // ---- 过期基线场景（黄金 staleScenario——仅标记不阻塞，§10.3 第 4 项）。
    const tk::JsonValue* stale = expected.find("staleScenario");
    ASSERT_TRUE(stale != nullptr && stale->isObject());
    const tk::JsonValue* staleState = inputs.find("baselineStates");
    ASSERT_TRUE(staleState != nullptr && staleState->isObject());
    const tk::JsonValue* staleInput = staleState->find("stale");
    ASSERT_TRUE(staleInput != nullptr && staleInput->isObject());
    opt::ApplyCandidateRequest staleRequest = request;
    staleRequest.baseline.tip =
        core::RevisionId::fromCanonical(str(*staleInput, "tip"));
    const opt::CandidateApplyPlan stalePlan = applier.buildApplyPlan(staleRequest);
    EXPECT_TRUE(stalePlan.expectedStaleBaseline);
    const bool goldenApplyUnchanged =
        stale->find("allowApplyUnchanged") != nullptr
        && stale->find("allowApplyUnchanged")->boolean;
    EXPECT_EQ(stalePlan.allowApply, goldenApplyUnchanged)
        << "通道化状态不受过期标记影响（组装语义与基线时点分立）";
}
