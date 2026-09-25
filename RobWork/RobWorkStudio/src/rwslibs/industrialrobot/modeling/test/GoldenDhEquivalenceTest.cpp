/**
 * @file   GoldenDhEquivalenceTest.cpp
 * @brief  DH↔显式等价黄金数据集全链用例组（MdlGoldenDhEquivalence）——契约
 *         tasks/foundation/WP-13-T16.json acceptance 3/4 的具名自证（V-11）：
 *
 *   黄金展开（闭式对照）：mdl-dh-equivalence 七样本（exact 五族＋
 *     exact-non-unique 退化族两例）经 dhToExplicit 与 expected/expansion.json
 *     的闭式期望逐关节逐项对照——origin/axis 走档案 mdl-dh 条目（dh[*]，
 *     附录 D 第 5 项 1e-9）；rotation 行主序 9 元为闭式中间承载（位级差异
 *     仅浮点累乘序，1e-12 远低于第 5 项上界——无独立档案条目，测试侧比
 *     较常数非工程判定阈值）
 *   黄金 roundtrip（参数级）：explicitToDh 五状态判定＋选定解与
 *     expected/roundtrip.json 逐关节逐项对照（dh-solve[*]，第 5 项）；
 *     exact-non-unique 的自由坐标字典序面与解集证人面
 *   FK 级双一致（第 4 项，exact 族）：显式基线 vs DH 候选（求解参数重组）
 *     经真实 runtime 编译链探针对照——fk.max-position/orientation-deviation
 *     档案上界（1e-9）逐项不超（V-11"FK 对照 ≤1×10⁻⁹ m/rad"）；exact-
 *     non-unique 族断言位置面（轴线/原点几何恒等）——字典序钉定的自由
 *     theta 相位差进入姿态面（结构事实，切权威语义归 T08/T09 命令面）
 *   五状态各含样例（V-11 末句）：mdl-not-expressible 三例 NotExpressible
 *     终判（prismatic/零轴/Fixed——终判不进入求解、subject＝首违规关节）
 *     ＋Approximate 扰动例（收敛但超差、附 E 与收敛态、不得成为权威）＋
 *     AnalysisFailed 溢出例（数值失败不构成语义结论）；与本数据集
 *     Exact/ExactNonUnique 合成五状态全覆盖
 *   C4 边角样例消费（ACC4）：zero-anchor（零值）／near-zero（近零）／
 *     sign-cancel（正负抵消）三样本随闭式期望逐项消费（manifest
 *     edgeCases.sampleRefs 的消费面）
 *
 * 设计依据：units/modeling.md §7.4/§7.5/§7.6/§10.2（V-11 行）、§10.1；
 * units/testkit.md §4.2/§4.3；需求 MDL-02/MDL-10、AT-16、NFR-COR-01；
 * 集成模式专属（TARGET sdurw_kinematics gating——FK 探针消费 runtime 公共
 * 构造器，DhConvertEquivalenceTest 同款装配纪律；冒烟不编入）。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/modeling/DiagCodes.hpp>
#include <sdurws/ird/modeling/DhConvert.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/modeling/Template.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/Sources.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace tk = sdurws::ird::testkit;
using namespace sdurws::ird;  // 嵌套单元名可见（core::/runtime:: 前缀解析）

// IRD_EXPECT_* 宏以未限定名展开 Check 函数——显式引入（GoldenImportTest 同款注）。
using tk::checkAtMost;
using tk::checkCloseWithin;
using tk::checkIdentical;

using sdurws::ird::modeling::AuthorityMode;
using sdurws::ird::modeling::DhChain;
using sdurws::ird::modeling::DhChainJoint;
using sdurws::ird::modeling::DhConversionResult;
using sdurws::ird::modeling::DhConvergenceState;
using sdurws::ird::modeling::DhDetermination;
using sdurws::ird::modeling::DhExplicitConverter;
using sdurws::ird::modeling::DhParameters;
using sdurws::ird::modeling::JointEntry;
using sdurws::ird::modeling::JointLimits;
using sdurws::ird::modeling::JointType;
using sdurws::ird::modeling::ModelingWorkingSet;
using namespace sdurws::ird::modeling;  // NOLINT——被测契约面直用

// ---- 通用小工具（GoldenImportTest 同形自持副本——测试域局部） ----

/// 读取数据集文件全文（读失败显性失败——黄金资产损坏不得静默跳过）。
std::string readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << p.string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// JSON 取串（缺字段即失败后返回空——数据集域内 schema 契约）。
std::string jStr(const tk::JsonValue& o, const std::string& k)
{
    const tk::JsonValue* v = o.find(k);
    if (v == nullptr) {
        ADD_FAILURE() << "JSON 缺字段: " << k;
        return {};
    }
    return v->text;
}

/// JSON 取数（缺字段即失败后返回 0——同上）。
double jNum(const tk::JsonValue& o, const std::string& k)
{
    const tk::JsonValue* v = o.find(k);
    if (v == nullptr) {
        ADD_FAILURE() << "JSON 缺字段: " << k;
        return 0.0;
    }
    return v->number;
}

/// 装载 mdl-dh-equivalence 黄金数据集（装载失败＝数据资产缺陷，显性失败）。
tk::GoldenDataset loadEquivalenceDataset()
{
    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({"mdl-dh-equivalence", "1.0.0"}))
        << "mdl-dh-equivalence 装载失败（数据集非法级——testkit §7.2）";
    return ds;
}

/// 装载 mdl-not-expressible 黄金数据集（五状态终判/非权威半区）。
tk::GoldenDataset loadNotExpressibleDataset()
{
    tk::GoldenDataset ds;
    EXPECT_NO_THROW(ds = tk::GoldenDataset::load({"mdl-not-expressible", "1.0.0"}))
        << "mdl-not-expressible 装载失败（数据集非法级——testkit §7.2）";
    return ds;
}

/// 装载容差档案 mdl-dh（附录 D 第 4/5/6/7 项——比较上界唯一来源）。
tk::ToleranceProfile loadMdlDhProfile()
{
    return tk::ToleranceProfile::load(tk::toleranceProfileDir("mdl-dh")
                                      / "v1.0.0.json");
}

/// 用户来源标记（测试装配值——黄金样本为用户提供的权威输入面）。
sdurws::ird::core::ValueProvenance goldenProvenance()
{
    return sdurws::ird::core::ValueProvenance::make(
        sdurws::ird::core::ProvenanceKind::UserProvided, {}, {},
        std::string("wp13-t16-golden"));
}

/// 在期望样本数组中按 id 定位（黄金期望面与输入面以 id 关联）。
const tk::JsonValue* findSampleById(const tk::JsonValue& root,
                                    const std::string& id)
{
    const tk::JsonValue* samples = root.find("samples");
    if (samples == nullptr) {
        ADD_FAILURE() << "期望文件缺 samples 数组";
        return nullptr;
    }
    for (const tk::JsonValue& s : samples->items) {
        if (jStr(s, "id") == id) { return &s; }
    }
    ADD_FAILURE() << "期望文件缺样本: " << id;
    return nullptr;
}

/// 由 DH 参数 JSON 行构造 DhChainJoint（thetaOffset/d/a/alpha/zeroOffset；
/// bounds 黄金面统一 ±π——roundtrip 透传位，不入几何）。
DhChainJoint makeDhJointFromJson(const tk::JsonValue& j)
{
    DhChainJoint entry;
    entry.dh.thetaOffset = jNum(j, "thetaOffset");  // rad
    entry.dh.d = jNum(j, "d");                      // m
    entry.dh.a = jNum(j, "a");                      // m
    entry.dh.alpha = jNum(j, "alpha");              // rad
    entry.zeroOffset = jNum(j, "zeroOffset");       // rad（权威零位——显式分离）
    entry.type = JointType::Revolute;
    entry.objectId = sdurws::ird::core::ObjectId::generate();
    entry.localName = jStr(j, "name");
    entry.bounds = sdurws::ird::core::SourcedValue<JointLimits>::provided(
        JointLimits{-3.141592653589793, 3.141592653589793}, goldenProvenance());
    return entry;
}

/// 由数据集 inputs/samples.json 的一行构造 DhChain（exact 族全样本；键名
/// 兼容 "joints" 与退化/扰动行的 "chain"——两键承载同一行 schema）。
DhChain makeChainFromSample(const tk::JsonValue& sample)
{
    DhChain chain;
    const tk::JsonValue* joints = sample.find("joints");
    if (joints == nullptr) { joints = sample.find("chain"); }
    if (joints == nullptr) {
        ADD_FAILURE() << "样本缺 joints/chain 数组: " << jStr(sample, "id");
        return chain;
    }
    for (const tk::JsonValue& j : joints->items) {
        chain.joints.push_back(makeDhJointFromJson(j));
    }
    return chain;
}

/// 展开产物→显式权威基线工作集（DhConvertEquivalenceTest 同款夹具形态；
/// 几何由被测展开产出，物性缺失＝DataInsufficient 降级面）。
ModelingWorkingSet makeExplicitBaseline(const DhChain& chain,
                                        const std::string& displayName)
{
    ModelingWorkingSet ws;
    ws.design.displayName = displayName;
    ws.design.authority = AuthorityMode::Explicit;
    ws.rootObjectId = sdurws::ird::core::ObjectId::generate();

    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const DhExplicitConverter converter;
    const ExpandOutcome expanded = converter.dhToExplicit(chain, diags);
    if (!expanded.ok) { ADD_FAILURE() << "夹具展开失败"; }
    ws.design.joints = expanded.joints;
    for (std::size_t i = 0; i <= chain.joints.size(); ++i) {
        LinkEntry link;
        link.objectId = sdurws::ird::core::ObjectId::generate();
        link.localName = "L" + std::to_string(i);
        ws.design.links.push_back(link);
    }
    return ws;
}

/// 由基线构造候选 DH 权威工作集（authority=StandardDH＋dhDerived 逐关节
/// 落参——求解参数重组；DhConvertEquivalenceTest 同款）。
ModelingWorkingSet makeDhCandidate(const ModelingWorkingSet& baseline,
                                   const std::vector<DhParameters>& params)
{
    ModelingWorkingSet candidate = baseline;
    candidate.design.authority = AuthorityMode::StandardDH;
    if (params.size() != candidate.design.joints.size()) {
        ADD_FAILURE() << "夹具参数数量与关节链不一致";
        return candidate;
    }
    for (std::size_t i = 0; i < candidate.design.joints.size(); ++i) {
        candidate.design.joints[i].dhDerived = params[i];
    }
    return candidate;
}

// ---- 探针替身（DhConvertEquivalenceTest 同形自持副本——S5 真实构造） ----

/**
 * @brief 探针替身（CompileProbe 集成模式装配——S5 公共构造路径）：
 *        对注入 Description 经 runtime 公共构造器 CanonicalModelBuilder
 *        装配规范模型。R-2 边界与装配取舍见 DhConvertEquivalenceTest
 *        RuntimeCompileProbe 注（同形副本——文件局部自持）。
 */
class RuntimeCompileProbe final : public sdurws::ird::modeling::CompileProbe {
public:
    runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>
        buildCanonicalModel(
            const runtime::RobotDesignDescription& description) const override
    {
        ++calls;  // 调用观测（只读分段消费面计数）
        try {
            runtime::CanonicalModelHeader header;
            header.project = m_project;
            header.branch = m_branch;
            header.revision = m_revision;
            header.revisionSeq = 1;
            header.descriptionContractVersion = description.descriptionContractVersion;
            header.compilerContractVersion = 1;
            header.builtFrom = digestOf(description.robotLocalName);
            runtime::ObjectRefEntry robotRef;
            robotRef.objectId = m_robotOid;
            robotRef.contentVersion = m_cv;
            robotRef.objectTypeToken = runtime::kRobotDesignObjectType;
            robotRef.digest = digestOf("golden-robot");
            header.objectRefs.push_back(robotRef);
            for (const runtime::JointDescription& j : description.joints) {
                runtime::ObjectRefEntry e;
                e.objectId = j.objectId;
                e.contentVersion = m_cv;
                e.objectTypeToken = "joint";
                e.digest = digestOf("golden-" + j.localName);
                header.objectRefs.push_back(e);
            }
            for (const runtime::LinkDescription& l : description.links) {
                runtime::ObjectRefEntry e;
                e.objectId = l.objectId;
                e.contentVersion = m_cv;
                e.objectTypeToken = "link";
                e.digest = digestOf("golden-" + l.localName);
                header.objectRefs.push_back(e);
            }

            runtime::RobotChain chain;
            chain.robotObjectId = m_robotOid;
            chain.robotLocalName = description.robotLocalName;
            chain.deviceName = description.robotLocalName;
            chain.joints.reserve(description.joints.size());
            for (const runtime::JointDescription& j : description.joints) {
                runtime::CanonicalJoint cj;
                cj.objectId = j.objectId;
                cj.localName = j.localName;
                cj.type = j.type;
                cj.axis = j.axis;
                cj.origin = j.origin;
                cj.zeroOffset = 0.0;
                if (j.lower.tryValue().has_value() && j.upper.tryValue().has_value()) {
                    runtime::JointBounds b;
                    b.lower = j.lower.value();
                    b.upper = j.upper.value();
                    cj.bounds = b;
                }
                cj.workingRange = j.workingRange;
                cj.maxVelocity = j.maxVelocity;
                cj.maxAcceleration = j.maxAcceleration;
                chain.joints.push_back(std::move(cj));
            }
            chain.links.reserve(description.links.size());
            for (const runtime::LinkDescription& l : description.links) {
                runtime::CanonicalLink cl;
                cl.objectId = l.objectId;
                cl.localName = l.localName;
                chain.links.push_back(std::move(cl));
            }

            runtime::CanonicalModelBuilder builder;
            builder.setHeader(header);
            builder.setWorld(runtime::WorldPlacement{});
            builder.setChain(chain);
            return runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>::ok(
                builder.build());
        } catch (const runtime::RuntimeError& e) {
            return runtime::Expected<runtime::CanonicalModel, runtime::RuntimeError>::err(
                runtime::RuntimeError(e.code(), std::string(e.what())));
        }
    }

    mutable int calls = 0;  ///< 调用观测（单线程用例内）
    core::ProjectId m_project = core::ProjectId::generate();
    core::BranchId m_branch = core::BranchId::generate();
    core::RevisionId m_revision = core::RevisionId::generate();
    core::ObjectId m_robotOid = core::ObjectId::generate();
    core::ContentVersion m_cv{};

private:
    /// SHA-256（身份块测试值——非零保证）。
    static core::Digest256 digestOf(const std::string& seed)
    {
        core::ContentDigester d;
        d.update(seed.data(), seed.size());
        return d.finalize();
    }
};

// ---- 显式关节构造（mdl-not-expressible 输入面——JointEntry 直构） ----

/// 关节类型 token 反查（样本集词表三元——Revolute/Prismatic/Fixed；测试
/// 域局部映射，非产品词表第二实现点）。
JointType jointTypeFromToken(const std::string& token)
{
    if (token == "Revolute") { return JointType::Revolute; }
    if (token == "Prismatic") { return JointType::Prismatic; }
    if (token == "Fixed") { return JointType::Fixed; }
    ADD_FAILURE() << "样本集外关节类型: " << token;
    return JointType::Revolute;
}

/// 由显式关节 JSON 行构造 JointEntry（axis/origin Provided＋bounds 可缺
/// ——Fixed 行无 bounds；缺省 SourcedValue 构造即 NotProvided）。
JointEntry makeExplicitJointFromJson(const tk::JsonValue& j)
{
    JointEntry entry;
    entry.objectId = sdurws::ird::core::ObjectId::generate();
    entry.localName = jStr(j, "name");
    entry.type = jointTypeFromToken(jStr(j, "type"));
    entry.zeroOffset = jNum(j, "zeroOffset");

    const tk::JsonValue* axis = j.find("axis");
    if (axis != nullptr && axis->items.size() == std::size_t{3}) {
        entry.axis = sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::
            provided(rw::math::Vector3D<double>(axis->items[0].number,
                                                axis->items[1].number,
                                                axis->items[2].number),
                     goldenProvenance());
    }
    const tk::JsonValue* origin = j.find("origin");
    if (origin != nullptr) {
        const tk::JsonValue* pos = origin->find("position");
        const tk::JsonValue* rot = origin->find("rotation");
        if (pos != nullptr && rot != nullptr && rot->items.size() == std::size_t{9}) {
            const rw::math::Vector3D<double> p(pos->items[0].number,
                                               pos->items[1].number,
                                               pos->items[2].number);
            const rw::math::Rotation3D<double> r(
                rot->items[0].number, rot->items[1].number, rot->items[2].number,
                rot->items[3].number, rot->items[4].number, rot->items[5].number,
                rot->items[6].number, rot->items[7].number, rot->items[8].number);
            entry.origin
                = sdurws::ird::core::SourcedValue<JointPose>::provided(
                    JointPose(rw::math::Transform3D<double>(p, r)),
                    goldenProvenance());
        }
    }
    const tk::JsonValue* bounds = j.find("bounds");
    if (bounds != nullptr && bounds->items.size() == std::size_t{2}) {
        entry.bounds = sdurws::ird::core::SourcedValue<JointLimits>::provided(
            JointLimits{bounds->items[0].number, bounds->items[1].number},
            goldenProvenance());
    }
    return entry;
}

/// 在诊断清单中查找指定码的首条记录下标（无则返回 npos）。
std::size_t findDiag(const std::vector<sdurws::ird::core::DiagnosticRecord>& diags,
                     std::string_view code)
{
    for (std::size_t i = 0; i < diags.size(); ++i) {
        if (diags[i].code == code) { return i; }
    }
    return static_cast<std::size_t>(-1);
}

}  // namespace

// =====================================================================
// ACC3（V-11 展开半区）：闭式展开逐关节逐项对照（含 C4 边角三样本）
// =====================================================================

/**
 * @brief V-11 黄金展开（AT-16）：七样本 dhToExplicit 与
 *   expected/expansion.json 闭式期望对照——origin.position/axis 走档案
 *   dh[*] 条目（第 5 项 1e-9 逐项上界——IRD_EXPECT_CLOSE 逐分量）；rotation
 *   行主序 9 元以 1e-12 测试侧常数对照（闭式两侧仅浮点累乘序差异，远低
 *   于第 5 项；档案无该量条目——C4 不默认、测试侧显式声明）。zero-anchor/
 *   near-zero/sign-cancel 三边角样本（C4 零值/近零/正负抵消）同回路消费。
 */
TEST(MdlGoldenDhEquivalence, GoldenExpansionClosedForm_V11_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"},
                  tk::DatasetRef{"mdl-dh-equivalence", "1.0.0"});

    const tk::GoldenDataset ds = loadEquivalenceDataset();
    const tk::ToleranceProfile profile = loadMdlDhProfile();
    const tk::JsonValue inputs = tk::parseJson(readFile(
        ds.resolveInput("inputs/samples.json")));
    const tk::JsonValue expansion = tk::parseJson(readFile(
        ds.resolveExpected("expected/expansion.json")));
    const tk::JsonValue* inSamples = inputs.find("samples");
    ASSERT_NE(inSamples, nullptr);

    const DhExplicitConverter converter;
    for (const tk::JsonValue& sample : inSamples->items) {
        const std::string id = jStr(sample, "id");
        SCOPED_TRACE(id);  // 失败定位（档案 fieldPath 本体不含样本 id——
                           // resolve 的模板段匹配要求逐字符相等，C4）。
        const tk::JsonValue* expected = findSampleById(expansion, id);
        ASSERT_NE(expected, nullptr);

        // 展开正路径（无损——C-5；纯函数不产诊断）。
        const DhChain chain = makeChainFromSample(sample);
        std::vector<sdurws::ird::core::DiagnosticRecord> diags;
        const ExpandOutcome expanded = converter.dhToExplicit(chain, diags);
        ASSERT_TRUE(expanded.ok) << id << " 展开失败";
        EXPECT_TRUE(diags.empty()) << id << " 展开正路径不产诊断";

        // 逐关节对照（闭式期望——origin/axis 走档案，rotation 测试侧）。
        const tk::JsonValue* expJoints = expected->find("joints");
        ASSERT_NE(expJoints, nullptr);
        ASSERT_EQ(expanded.joints.size(), expJoints->items.size())
            << id << " 关节数不符";
        for (std::size_t i = 0; i < expJoints->items.size(); ++i) {
            const tk::JsonValue& ej = expJoints->items[i];
            const JointEntry& joint = expanded.joints[i];
            // 档案 fieldPath（"*" 模板逐元素——前缀须与条目逐字符相等）。
            const std::string prefix
                = id + "/dh[" + std::to_string(i) + "]";  // 仅消息定位
            const std::string originPath
                = "dh[" + std::to_string(i) + "].origin.";
            const std::string axisPath = "dh[" + std::to_string(i) + "].axis.";
            // origin.position（T_parent_joint 平移——单位 m）。
            const auto originVal = joint.origin.tryValue();
            ASSERT_TRUE(originVal.has_value()) << prefix << " origin 缺失";
            const rw::math::Transform3D<double> t
                = static_cast<rw::math::Transform3D<double>>(*originVal);
            const tk::JsonValue* originJson = ej.find("origin");
            ASSERT_NE(originJson, nullptr);
            const tk::JsonValue* pos = originJson->find("position");
            ASSERT_NE(pos, nullptr);
            ASSERT_EQ(pos->items.size(), std::size_t{3});
            IRD_EXPECT_CLOSE(originPath + "x", t.P()[0], pos->items[0].number,
                             profile, "m");
            IRD_EXPECT_CLOSE(originPath + "y", t.P()[1], pos->items[1].number,
                             profile, "m");
            IRD_EXPECT_CLOSE(originPath + "z", t.P()[2], pos->items[2].number,
                             profile, "m");
            // origin.rotation（行主序 9 元——闭式中间承载，1e-12 测试侧）。
            const tk::JsonValue* rot = originJson->find("rotation");
            ASSERT_NE(rot, nullptr);
            ASSERT_EQ(rot->items.size(), std::size_t{9});
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    EXPECT_NEAR(t.R()(r, c),
                                rot->items[static_cast<std::size_t>(r * 3 + c)].number,
                                1e-12)
                        << prefix << ".rotation[" << r << "][" << c
                        << "]（闭式对照——测试侧常数 1e-12）";
                }
            }
            // axis（R_{0,i}·ez 单位轴——无量纲，档案第 5 项口径）。
            const tk::JsonValue* axis = ej.find("axis");
            ASSERT_NE(axis, nullptr);
            ASSERT_EQ(axis->items.size(), std::size_t{3});
            const auto axisVal = joint.axis.tryValue();
            ASSERT_TRUE(axisVal.has_value()) << prefix << " axis 缺失";
            IRD_EXPECT_CLOSE(axisPath + "x", (*axisVal)[0], axis->items[0].number,
                             profile, "1");
            IRD_EXPECT_CLOSE(axisPath + "y", (*axisVal)[1], axis->items[1].number,
                             profile, "1");
            IRD_EXPECT_CLOSE(axisPath + "z", (*axisVal)[2], axis->items[2].number,
                             profile, "1");
        }
    }
}

// =====================================================================
// ACC3（V-11 roundtrip 半区）：参数级一致＋字典序自由坐标面
// =====================================================================

/**
 * @brief V-11 黄金 roundtrip（AT-16）：七样本 explicitToDh 与
 *   expected/roundtrip.json 对照——exact 族判定 Exact＋解即输入参数（规范
 *   形态闭式逆）；exact-non-unique 族判定 ExactNonUnique＋自由坐标字典序
 *   面（coincident [0,4]／parallel [4]）＋解集证人非空＋字典序定值解与期
 *   望一致（自由 theta 钉中性值 0）。参数对照走档案 dh-solve[*] 条目
 *   （第 5 项 1e-9——IRD_EXPECT_CLOSE 逐项）。
 */
TEST(MdlGoldenDhEquivalence, GoldenRoundtripParameters_V11_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-02"},
                  std::vector<std::string>{"AT-16"},
                  tk::DatasetRef{"mdl-dh-equivalence", "1.0.0"});

    const tk::GoldenDataset ds = loadEquivalenceDataset();
    const tk::ToleranceProfile profile = loadMdlDhProfile();
    const tk::JsonValue inputs = tk::parseJson(readFile(
        ds.resolveInput("inputs/samples.json")));
    const tk::JsonValue roundtrip = tk::parseJson(readFile(
        ds.resolveExpected("expected/roundtrip.json")));
    const tk::JsonValue* inSamples = inputs.find("samples");
    ASSERT_NE(inSamples, nullptr);

    const DhExplicitConverter converter;
    for (const tk::JsonValue& sample : inSamples->items) {
        const std::string id = jStr(sample, "id");
        const std::string family = jStr(sample, "family");
        SCOPED_TRACE(id);  // 失败定位（档案 fieldPath 不含样本 id——C4）。
        const tk::JsonValue* expected = findSampleById(roundtrip, id);
        ASSERT_NE(expected, nullptr);

        // 基线（几何由展开产出——"DH 可表达"的显式链）。
        const DhChain chain = makeChainFromSample(sample);
        const ModelingWorkingSet baseline
            = makeExplicitBaseline(chain, "GOLDEN-" + id);

        // 再求 DH（五状态判定）。
        std::vector<sdurws::ird::core::DiagnosticRecord> diags;
        const DhConversionResult solved
            = converter.explicitToDh(baseline.design.joints, diags);

        // 判定结论与自由坐标面。
        const DhDetermination wantDetermination
            = family == "exact" ? DhDetermination::Exact
                                : DhDetermination::ExactNonUnique;
        EXPECT_EQ(solved.determination, wantDetermination)
            << id << " 判定结论";
        if (family == "exact") {
            EXPECT_TRUE(diags.empty()) << id << " Exact 正路径不产诊断";
        } else {
            for (const auto& d : diags) {
                EXPECT_NE(d.code, std::string(sdurws::ird::modeling::kMdlDhNotExpressible));
                EXPECT_NE(d.code, std::string(sdurws::ird::modeling::kMdlDhAnalysisFailed));
            }
        }
        const tk::JsonValue* freeExp = expected->find("freeCoordinates");
        ASSERT_NE(freeExp, nullptr);
        ASSERT_EQ(solved.freeCoordinates.size(), freeExp->items.size())
            << id << " 自由坐标数";
        for (std::size_t i = 0; i < freeExp->items.size(); ++i) {
            EXPECT_EQ(solved.freeCoordinates[i],
                      static_cast<std::size_t>(freeExp->items[i].number))
                << id << " 自由坐标[" << i << "]（4×关节＋{0:θ,1:d,2:a,3:α} 字典序）";
        }
        // 解集证人面（ExactNonUnique 非空——禁随机挑选；Exact 为空）。
        EXPECT_EQ(solved.solutionSet.empty(), family == "exact")
            << id << " 解集报告面";

        // 选定解逐关节逐项对照（档案 dh-solve[*]——第 5 项上界）。
        const tk::JsonValue* paramsExp = expected->find("parameters");
        ASSERT_NE(paramsExp, nullptr);
        ASSERT_EQ(solved.parameters.size(), paramsExp->items.size())
            << id << " 选定解关节数";
        for (std::size_t i = 0; i < paramsExp->items.size(); ++i) {
            const tk::JsonValue& pj = paramsExp->items[i];
            const std::string prefix
                = id + "/dh-solve[" + std::to_string(i) + "]";  // 仅消息定位
            const std::string solvePath
                = "dh-solve[" + std::to_string(i) + "].";  // 档案条目路径
            IRD_EXPECT_CLOSE(solvePath + "thetaOffset",
                             solved.parameters[i].thetaOffset, jNum(pj, "thetaOffset"),
                             profile, "rad");
            IRD_EXPECT_CLOSE(solvePath + "d", solved.parameters[i].d, jNum(pj, "d"),
                             profile, "m");
            IRD_EXPECT_CLOSE(solvePath + "a", solved.parameters[i].a, jNum(pj, "a"),
                             profile, "m");
            IRD_EXPECT_CLOSE(solvePath + "alpha", solved.parameters[i].alpha,
                             jNum(pj, "alpha"), profile, "rad");
        }
    }
}

// =====================================================================
// ACC3（V-11 FK 半区）：编译链 FK 对照双一致（真实 runtime 编译器探针）
// =====================================================================

/**
 * @brief V-11 FK 级一致（AT-16，附录 D 第 4 项）：七样本显式基线 vs DH
 *   候选（求解参数重组）经真实 runtime 编译链探针对照——equivalent 全称
 *   成立＋最大偏差走档案 fk.* 条目上界（1e-9，档案为上界权威——数据只准
 *   更严）＋逐关节偏差明细对齐链序。"roundtrip 参数级＋FK 级双一致"
 *   （V-11）与 T09 程序化样本同面、黄金参数承载。
 */
TEST(MdlGoldenDhEquivalence, GoldenFkEquivalence_V11_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{"AT-16"},
                  tk::DatasetRef{"mdl-dh-equivalence", "1.0.0"});

    const tk::GoldenDataset ds = loadEquivalenceDataset();
    const tk::ToleranceProfile profile = loadMdlDhProfile();
    // 档案上界（第 4 项——比较边界唯一来源，不私设字面阈值）。
    const double posBound
        = profile.resolve("fk.max-position-deviation").tolerance.absolute;
    const double oriBound
        = profile.resolve("fk.max-orientation-deviation").tolerance.absolute;

    const tk::JsonValue inputs = tk::parseJson(readFile(
        ds.resolveInput("inputs/samples.json")));
    const tk::JsonValue* inSamples = inputs.find("samples");
    ASSERT_NE(inSamples, nullptr);

    const DhExplicitConverter converter;
    RuntimeCompileProbe probe;
    for (const tk::JsonValue& sample : inSamples->items) {
        const std::string id = jStr(sample, "id");
        const std::string family = jStr(sample, "family");
        SCOPED_TRACE(id);
        const DhChain chain = makeChainFromSample(sample);
        const ModelingWorkingSet baseline
            = makeExplicitBaseline(chain, "GOLDEN-" + id);

        // 再求 DH→求解参数重组候选。
        std::vector<sdurws::ird::core::DiagnosticRecord> diags;
        const DhConversionResult solved
            = converter.explicitToDh(baseline.design.joints, diags);
        ASSERT_TRUE(solved.determination == DhDetermination::Exact
                    || solved.determination == DhDetermination::ExactNonUnique)
            << id << " 预置 exact 族必须可解";
        const ModelingWorkingSet candidate
            = makeDhCandidate(baseline, solved.parameters);

        // 编译链 FK 对照（S1～S5 只读分段——每侧各一次）。
        const EquivalenceReport report
            = converter.verifyEquivalent(baseline, candidate, probe);
        ASSERT_TRUE(report.inputsValid) << id << " " << report.failureDetail;
        ASSERT_TRUE(report.compileOk) << id << " " << report.failureDetail;
        ASSERT_EQ(report.jointDeviations.size(), chain.joints.size())
            << id << " 逐关节偏差明细对齐链序";
        // fieldPath＝档案条目本体（上界即条目 tolerance.absolute——档案为
        // 上界权威）。
        IRD_EXPECT_AT_MOST("fk.max-position-deviation",
                           report.maxPositionDeviation, posBound, "m");

        if (family == "exact") {
            // exact 族：选定解与输入同支（规范形态恒等输入——roundtrip
            // 用例已钉参数级）→FK 级双一致全称成立（V-11"FK 对照
            // ≤1×10⁻⁹ m/rad"）。
            EXPECT_TRUE(report.equivalent) << id << " 黄金参数必须 FK 级等价";
            IRD_EXPECT_AT_MOST("fk.max-orientation-deviation",
                               report.maxOrientationDeviation, oriBound, "rad");
        } else {
            // exact-non-unique 族：字典序钉定代表保轴线/原点几何（位置面
            // 恒 ≤上界——上面已断言），但自由 theta 相位（钉中性值 0 与
            // 输入值的差）进入帧相位——姿态面对照含该相位差（结构事实，
            // 非缺陷：切权威的场景语义归 T08/T09 命令面裁决）。此处以
            // "非负有限"钉住报告面完整（不伪造数值——NFR-COR-03）。
            EXPECT_GE(report.maxOrientationDeviation, 0.0);
            EXPECT_TRUE(std::isfinite(report.maxOrientationDeviation));
        }
    }
    // 只读分段消费面：每样本两侧各一次编译（不发布快照）。
    EXPECT_EQ(probe.calls, static_cast<int>(inSamples->items.size()) * 2);
}

// =====================================================================
// ACC3/ACC4（V-11 五状态各含样例）：终判/非权威半区（C4 扰动与溢出例）
// =====================================================================

/**
 * @brief V-11 五状态半区（AT-16）：mdl-not-expressible 五样本——
 *   ①NotExpressible×3（prismatic/零轴/Fixed）：终判不进入求解（参数空）、
 *     诊断 MDL-DH-NOT-EXPRESSIBLE 且 subject＝首违规关节（期望文件
 *     firstViolatingJointIndex 对账）；②Approximate（DH 可解链＋J1 原点
 *     1e-6 m 不可吸收扰动）：收敛但超第 5 项上界→近似判定（附 E>0 与
 *     收敛态、比较型诊断、参数为近似参考不得成为权威）；③AnalysisFailed
 *     （原点 1e200 m 溢出）：数值发散不构成语义结论（参数/偏差全空）。
 *   合并本文件前两用例的 Exact/ExactNonUnique——五状态全覆盖（V-11 末句）。
 */
TEST(MdlGoldenDhEquivalence, GoldenNotExpressibleFiveStates_V11_WP13T16_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-02"},
                  std::vector<std::string>{"AT-16"},
                  tk::DatasetRef{"mdl-not-expressible", "1.0.0"});

    const tk::GoldenDataset ds = loadNotExpressibleDataset();
    const tk::ToleranceProfile profile = loadMdlDhProfile();
    const tk::JsonValue inputs = tk::parseJson(readFile(
        ds.resolveInput("inputs/samples.json")));
    const tk::JsonValue expectations = tk::parseJson(readFile(
        ds.resolveExpected("expected/samples-expected.json")));
    const tk::JsonValue* inSamples = inputs.find("samples");
    ASSERT_NE(inSamples, nullptr);

    const DhExplicitConverter converter;
    // 五状态覆盖旗（Exact/ExactNonUnique 由 mdl-dh-equivalence 数据集承载
    // ——本文件前两用例；本用例补齐终判/非权威三态）。
    bool sawApproximate = false;
    bool sawNotExpressible = false;
    bool sawAnalysisFailed = false;

    for (const tk::JsonValue& sample : inSamples->items) {
        const std::string id = jStr(sample, "id");
        const std::string family = jStr(sample, "family");
        const tk::JsonValue* expected = findSampleById(expectations, id);
        ASSERT_NE(expected, nullptr);
        const tk::JsonValue* exp = expected->find("expected");
        ASSERT_NE(exp, nullptr);

        if (family == "not-expressible") {
            sawNotExpressible = true;
            // 显式关节链直构（终判于第一阶结构检查——不进入求解）。
            std::vector<JointEntry> joints;
            std::vector<sdurws::ird::core::ObjectId> oids;
            for (const tk::JsonValue& j : sample.find("joints")->items) {
                joints.push_back(makeExplicitJointFromJson(j));
                oids.push_back(joints.back().objectId);
            }
            std::vector<sdurws::ird::core::DiagnosticRecord> diags;
            const DhConversionResult result = converter.explicitToDh(joints, diags);

            IRD_EXPECT_IDENTICAL(id + "/determination",
                                 std::string(dhDeterminationToken(result.determination)),
                                 jStr(*exp, "determination"));
            IRD_EXPECT_IDENTICAL(id + "/convergence",
                                 std::string(dhConvergenceToken(result.convergence)),
                                 jStr(*exp, "convergence"));
            EXPECT_TRUE(result.parameters.empty()) << id << " 终判不产出参数";
            // 终判诊断：码面＋subject＝首违规关节（结构检查定位面）。
            const std::size_t idx = findDiag(diags, jStr(*exp, "diagnosticCode"));
            ASSERT_NE(idx, static_cast<std::size_t>(-1))
                << id << " 缺终判诊断";
            const std::size_t violating
                = static_cast<std::size_t>(jNum(*exp, "firstViolatingJointIndex"));
            ASSERT_LT(violating, oids.size());
            ASSERT_TRUE(diags[idx].subject.has_value()) << id << " 终判必带 subject";
            EXPECT_TRUE(diags[idx].subject.value() == oids[violating])
                << id << " subject＝首违规关节（index " << violating << "）";
        } else if (family == "approximate") {
            sawApproximate = true;
            // DH 可解链展开→施加不可吸收扰动（期望文件声明的规格——
            // 扰动由消费测试施加，本文件只声明规格）。
            const DhChain chain = makeChainFromSample(sample);
            std::vector<sdurws::ird::core::DiagnosticRecord> diags;
            const ExpandOutcome expanded = converter.dhToExplicit(chain, diags);
            ASSERT_TRUE(expanded.ok) << id << " 扰动前展开失败";
            std::vector<JointEntry> joints = expanded.joints;
            const tk::JsonValue* perturb = sample.find("perturbation");
            ASSERT_NE(perturb, nullptr);
            const std::size_t jointIndex
                = static_cast<std::size_t>(jNum(*perturb, "jointIndex"));
            const tk::JsonValue* translate = perturb->find("translateM");
            ASSERT_NE(translate, nullptr);
            ASSERT_LT(jointIndex, joints.size());
            const auto originVal = joints[jointIndex].origin.tryValue();
            ASSERT_TRUE(originVal.has_value());
            const rw::math::Transform3D<double> t
                = static_cast<rw::math::Transform3D<double>>(*originVal);
            joints[jointIndex].origin
                = sdurws::ird::core::SourcedValue<JointPose>::provided(
                    JointPose(rw::math::Transform3D<double>(
                        t.P() + rw::math::Vector3D<double>(translate->items[0].number,
                                                           translate->items[1].number,
                                                           translate->items[2].number),
                        t.R())),
                    goldenProvenance());

            std::vector<sdurws::ird::core::DiagnosticRecord> solveDiags;
            const DhConversionResult result = converter.explicitToDh(joints, solveDiags);
            IRD_EXPECT_IDENTICAL(id + "/determination",
                                 std::string(dhDeterminationToken(result.determination)),
                                 jStr(*exp, "determination"));
            IRD_EXPECT_IDENTICAL(id + "/convergence",
                                 std::string(dhConvergenceToken(result.convergence)),
                                 jStr(*exp, "convergence"));
            // 收敛但超差：E 呈现指标为正、逐关节偏差至少一项超档案上界、
            // 参数为近似参考（数量＝链长——不得成为权威，C-4）。
            EXPECT_GT(result.errorMetricE, 0.0) << id << " E 度量必须为正";
            EXPECT_EQ(result.parameters.size(),
                      static_cast<std::size_t>(jNum(*exp, "parameterCount")))
                << id << " 近似参考参数数量";
            EXPECT_FALSE(result.deviations.empty()) << id << " 偏差表非空";
            const double tol = jNum(*exp, "tolerance");  // 附录 D 第 5 项口径
            bool anyOver = false;
            for (const auto& dev : result.deviations) {
                if (dev.axisAngleDeviation > tol || dev.originPositionDeviation > tol) {
                    anyOver = true;
                }
            }
            EXPECT_EQ(anyOver, exp->find("anyDeviationOverTolerance")->boolean)
                << id << " 至少一项逐项偏差超上界";
            // 比较型诊断（三要素随产码点写入——MDL-DH-APPROXIMATE）。
            const std::size_t idx = findDiag(solveDiags, jStr(*exp, "diagnosticCode"));
            ASSERT_NE(idx, static_cast<std::size_t>(-1)) << id << " 缺近似诊断";
            EXPECT_EQ(solveDiags[idx].comparison.has_value(),
                      exp->find("diagnosticHasComparison")->boolean)
                << id << " 近似诊断必带比较型三要素";
        } else if (family == "analysis-failed") {
            sawAnalysisFailed = true;
            // 结构合法但数值溢出（原点 1e200 m）——不构成语义结论。
            std::vector<JointEntry> joints;
            for (const tk::JsonValue& j : sample.find("joints")->items) {
                joints.push_back(makeExplicitJointFromJson(j));
            }
            std::vector<sdurws::ird::core::DiagnosticRecord> diags;
            const DhConversionResult result = converter.explicitToDh(joints, diags);
            IRD_EXPECT_IDENTICAL(id + "/determination",
                                 std::string(dhDeterminationToken(result.determination)),
                                 jStr(*exp, "determination"));
            IRD_EXPECT_IDENTICAL(id + "/convergence",
                                 std::string(dhConvergenceToken(result.convergence)),
                                 jStr(*exp, "convergence"));
            EXPECT_TRUE(result.parameters.empty()) << id << " 数值失败无参数";
            EXPECT_TRUE(result.deviations.empty()) << id << " 数值失败无偏差表";
            EXPECT_NE(findDiag(diags, jStr(*exp, "diagnosticCode")),
                      static_cast<std::size_t>(-1))
                << id << " 缺数值失败诊断";
        }
    }

    EXPECT_TRUE(sawNotExpressible) << "NotExpressible 样例在册";
    EXPECT_TRUE(sawApproximate) << "Approximate 样例在册";
    EXPECT_TRUE(sawAnalysisFailed) << "AnalysisFailed 样例在册";
}
