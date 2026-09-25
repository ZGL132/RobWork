/**
 * @file   ModelDiffTest.cpp
 * @brief  Model Diff 差异增量表用例组（MdlModelDiff）——WP-13-T14 四条
 *         acceptance 的具名自证：数据实体落位与三组分组（ACC1）、两
 *         RobotDesign 逐字段三态输出与派生只读字段豁免＋确定性稳定排序
 *         （ACC2）、纯函数不覆盖输入＋方向镜像＋空差集空报告（ACC3）、
 *         AT-12 数据侧逐项可观察（ACC4——呈现面零依赖归 WP-22-T11/UX-13，
 *         零 Qt 由 BuildRedLineTest 产品面扫描与 ird_gates 双重复核）。
 *
 * 设计依据：
 *   - units/modeling.md §9.4.9（IModelDiffService 签名与三组分组词表）、
 *     §4.3/§4.3-A/§4.3-B（字段表行序——组内"字段序"的权威）、§4.8（引用
 *     表/资源清单 canonical 集合语义）、§14.2 D-MDL-5（派生字段不入差异
 *     面——本用例组的 (a)(b)(c) 三分支按同一口径反证）
 *   - REQUIREMENTS MDL-08（数据层差异比较、不覆盖基线）、AT-12（数据侧：
 *     比较型差异逐项可观察）、NFR-COR-02（同输入→同报告；稳定排序）、
 *     ARC-04（ObjectId 对象定位锚）
 *   - 任务契约 tasks/foundation/WP-13-T14.json acceptance 1～4
 *
 * 测试自证基线声明（NFR-COR-01）：期望条目由用例内**独立构造**的输入
 * 差异逐项推出（哪一字段改了什么→应得哪一条），不消费产品实现的任何
 * 内部比较结果；对象 id 为固定规范文本（确定性跨运行稳定——排序断言的
 * 前提），不使用随机生成身份。
 */

#include <sdurws/ird/modeling/ModelDiff.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/modeling/Codec.hpp>  // RobotDesignCodec——只读证据的字节编码
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cstddef>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using namespace sdurws::ird::modeling;  // NOLINT——用例直接面对被测契约面
namespace core = ::sdurws::ird::core;

namespace {

/// 测试内独立 π（不引实现常量——夹具自持纪律）。
constexpr double kPiTest = 3.14159265358979323846;

// ---- 确定性测试身份（固定规范文本——排序断言跨运行稳定的前提） ----

/// 按槽位号构造固定 ObjectId（"obj-"+32 位小写十六进制——ARC-04 词表；
/// 同槽位恒同 id，异槽位恒异 id）。
core::ObjectId oidOf(unsigned slot)
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << "obj-";
    os.width(32);
    os.fill('0');
    os << std::hex << slot;
    const std::string text = os.str();
    const auto parsed = core::ObjectId::tryFromCanonical(text);
    // 固定槽位串必合法（tag+32hex）——失败即测试自身破损，直接登记失败。
    if (!parsed.has_value()) {
        ADD_FAILURE() << "测试身份构造失败: " << text;
        return core::ObjectId{};
    }
    return *parsed;
}

/// 用户输入来源标记（methodTag 语法 [a-z0-9./_-]——既有夹具同款词）。
core::ValueProvenance userProv()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       std::string("test-fixture"));
}

/// 导入映射来源标记（与 userProv 异 kind——来源标记变化用例的对照面）。
core::ValueProvenance importProv()
{
    return core::ValueProvenance::make(core::ProvenanceKind::ImportMapped,
                                       std::nullopt, std::nullopt,
                                       std::string("test-import"));
}

/// 显式权威旋转关节（axis/origin/bounds 全 Provided；zeroOffset 单位 rad）。
JointEntry makeJoint(const core::ObjectId& oid, const std::string& name,
                     const rw::math::Vector3D<double>& axis, double zeroOffsetRad)
{
    JointEntry joint;
    joint.objectId = oid;
    joint.localName = name;
    joint.type = JointType::Revolute;
    joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        axis, userProv());
    joint.origin = core::SourcedValue<JointPose>::provided(
        JointPose{}, userProv());  // 恒位姿（m/rad——T_parent_joint）
    joint.zeroOffset = zeroOffsetRad;
    joint.bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{-kPiTest, kPiTest}, userProv());
    return joint;
}

/// 连杆（质量 Provided——kg；其余物性缺省 NotProvided——MDL-06 降级语义）。
LinkEntry makeLink(const core::ObjectId& oid, const std::string& name, double massKg)
{
    LinkEntry link;
    link.objectId = oid;
    link.localName = name;
    link.body.mass = core::SourcedValue<double>::provided(massKg, userProv());
    return link;
}

/// 基准模型：3 关节（J1/J2/J3）＋4 连杆（base/l1/l2/l3）——I-MDL-1
/// （links==joints+1）；Explicit 权威；displayName/notes 固定种子值。
RobotDesign baseDesign()
{
    RobotDesign d;
    d.displayName = "demo";
    d.authority = AuthorityMode::Explicit;
    d.joints.push_back(makeJoint(oidOf(1), "J1",
                                 rw::math::Vector3D<double>(0.0, 0.0, 1.0), 0.0));
    d.joints.push_back(makeJoint(oidOf(2), "J2",
                                 rw::math::Vector3D<double>(0.0, 1.0, 0.0), 0.25));
    d.joints.push_back(makeJoint(oidOf(3), "J3",
                                 rw::math::Vector3D<double>(1.0, 0.0, 0.0), 0.5));
    d.links.push_back(makeLink(oidOf(11), "base", 5.0));
    d.links.push_back(makeLink(oidOf(12), "l1", 2.0));
    d.links.push_back(makeLink(oidOf(13), "l2", 2.0));
    d.links.push_back(makeLink(oidOf(14), "l3", 1.5));
    d.notes = "n0";
    return d;
}

/// 工作集包装（design＋根身份——部件对象/变更日志保持缺省：MDL-08 比较
/// 面为两 RobotDesign，见 ModelDiff.hpp 文件头范围决策）。
ModelingWorkingSet wsOf(const RobotDesign& d, unsigned rootSlot)
{
    ModelingWorkingSet ws;
    ws.design = d;
    ws.rootObjectId = oidOf(rootSlot);
    return ws;
}

/// 在组内按（字段,subjectPath）查找条目；未找到返回 nullptr。
const ModelDiffEntry* findEntry(const std::vector<ModelDiffEntry>& group,
                                const std::string& field, const std::string& path)
{
    for (const ModelDiffEntry& e : group) {
        if (e.field == field && e.subjectPath == path) { return &e; }
    }
    return nullptr;
}

}  // namespace

// =====================================================================
// ACC1：落位（接口/实体）＋空差集→空报告非 null（兼 ACC3 空差集分支）
// =====================================================================

TEST(MdlModelDiff, PlacementEntityAndEmptyDiff_WP13T14_ACC1_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08"},
                  std::vector<std::string>{"AT-12"});

    // 接口落位（§9.4.9 签名——编译期证明＋经接口引用调用）：实现可赋
    // 予接口引用，diff 以两个 const ModelingWorkingSet& 入参、直返报告
    // （无错误轨——卡面签名）。
    static_assert(std::is_base_of<IModelDiffService, ModelDiffService>::value,
                  "ModelDiffService 须实现 IModelDiffService（§9.4.9）");
    ModelDiffService service;
    IModelDiffService& iface = service;

    // 空差集：两工作集逐字段全等 → 三组全空、报告仍是合法值对象
    // （"空报告非 null"——无条目、无错误、无特殊判别态）。
    const RobotDesign design = baseDesign();
    const ModelingWorkingSet a = wsOf(design, 41);
    const ModelingWorkingSet b = wsOf(design, 42);
    const ModelDiffReport report = iface.diff(a, b);

    EXPECT_TRUE(report.structure.empty());
    EXPECT_TRUE(report.parameters.empty());
    EXPECT_TRUE(report.properties.empty());
    // 方向字段：两侧根身份原样带入（本用例两根身份不同——报告逐侧对号）。
    ASSERT_TRUE(report.baselineObjectId.has_value());
    ASSERT_TRUE(report.candidateObjectId.has_value());
    EXPECT_EQ(report.baselineObjectId->toCanonical(), oidOf(41).toCanonical());
    EXPECT_EQ(report.candidateObjectId->toCanonical(), oidOf(42).toCanonical());
}

// =====================================================================
// ACC2：两 RobotDesign 差异逐项输出——增/删/改三态、逐字段、权威字段
// 变更与来源标记变更均入表
// =====================================================================

TEST(MdlModelDiff, PerFieldThreeStatesAndProvenance_WP13T14_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08"},
                  std::vector<std::string>{"AT-12"});

    RobotDesign base = baseDesign();
    RobotDesign cand = baseDesign();

    // —— 构造九处已知差异（期望条目由输入差异独立推出）——
    // ① 改：J1 轴线值变化（权威一等字段——Parameters 组）。
    cand.joints[0].axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(1.0, 0.0, 0.0), userProv());
    // ② 改：J2 零位偏置（两态均权威——Parameters 组）。
    cand.joints[1].zeroOffset = 0.75;
    // ③ 改：J2 限位（Parameters 组——与②同对象，钉字段序）。
    cand.joints[1].bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{-1.0, 1.0}, userProv());
    // ④ 删：J3 移除（基线下标 2——Removed 条目路径按基线侧计数）。
    cand.joints.pop_back();
    cand.links.pop_back();  // links==joints+1（I-MDL-1——保持候选侧合法）
    // ⑤ 增：新关节 J4（候选下标 2——Added 条目路径按候选侧计数）。
    cand.joints.push_back(makeJoint(oidOf(4), "J4",
                                    rw::math::Vector3D<double>(0.0, 0.0, 1.0), 0.0));
    cand.links.push_back(makeLink(oidOf(18), "l4", 1.0));
    // ⑥ 改：l1 质量值变化（物性组——valueChanged）。
    cand.links[1].body.mass = core::SourcedValue<double>::provided(3.0, userProv());
    // ⑦ 改：l2 质量**仅来源标记**变化（值不变——provenanceChanged 单独
    //     为真；acceptance 2"来源标记变更均入表"的判定面）。
    cand.links[2].body.mass = core::SourcedValue<double>::provided(2.0, importProv());
    // ⑧ 改：displayName（根元字段——Structure 组）。
    cand.displayName = "demo2";
    // ⑨ 增：toolRefs 追加 T2（引用集合——条目锚＝被引用工具 id）。
    cand.toolRefs.push_back(oidOf(21));

    const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));

    // 逐项核对（无聚合、无遗漏——计数与内容双对账）。
    // 结构组 6 条：displayName / J4 增 / J3 删 / l4 增 / l3 删 / toolRef T2 增
    // （④⑤的关节增删按 I-MDL-1 同步增删了首尾连杆——其成员条目同为三态
    // 证据面）。
    ASSERT_EQ(report.structure.size(), 6U);
    {
        const ModelDiffEntry* dn = findEntry(report.structure, "displayName", "displayName");
        ASSERT_NE(dn, nullptr);
        EXPECT_EQ(dn->kind, ModelDiffChangeKind::Modified);
        EXPECT_EQ(dn->group, ModelDiffGroup::Structure);
        EXPECT_FALSE(dn->objectId.isValid());  // 根对象字段——全零定位锚
        EXPECT_EQ(dn->baselineText, "demo");
        EXPECT_EQ(dn->candidateText, "demo2");
    }
    {
        // J4 的 Added 与 J3 的 Removed 同 path（两侧下标口径一致地落位）——
        // 以 kind 消歧核对。
        const ModelDiffEntry* add = nullptr;
        const ModelDiffEntry* rem = nullptr;
        for (const ModelDiffEntry& e : report.structure) {
            if (e.field != "joint") { continue; }
            if (e.kind == ModelDiffChangeKind::Added && add == nullptr) { add = &e; }
            if (e.kind == ModelDiffChangeKind::Removed && rem == nullptr) { rem = &e; }
        }
        ASSERT_NE(add, nullptr);
        EXPECT_EQ(add->kind, ModelDiffChangeKind::Added);  // 仅候选侧存在
        EXPECT_TRUE(add->objectId.isValid());
        EXPECT_EQ(add->objectId.toCanonical(), oidOf(4).toCanonical());
        EXPECT_EQ(add->subjectPath, "joints[2]");  // Added 路径按候选侧计数
        EXPECT_TRUE(add->baselineText.empty());  // 空串＝该侧不存在
        EXPECT_NE(add->candidateText.find("J4"), std::string::npos);
        ASSERT_NE(rem, nullptr);
        // Removed 条目锚＝被删关节 id（J3——基线下标 2）。
        EXPECT_EQ(rem->objectId.toCanonical(), oidOf(3).toCanonical());
        EXPECT_EQ(rem->subjectPath, "joints[2]");  // Removed 路径按基线侧计数
        EXPECT_NE(rem->baselineText.find("J3"), std::string::npos);
        EXPECT_TRUE(rem->candidateText.empty());
    }
    {
        const ModelDiffEntry* tr = findEntry(report.structure, "toolRefs",
                                             "toolRefs[" + oidOf(21).toCanonical() + "]");
        ASSERT_NE(tr, nullptr);
        EXPECT_EQ(tr->kind, ModelDiffChangeKind::Added);
        EXPECT_EQ(tr->objectId.toCanonical(), oidOf(21).toCanonical());  // 锚＝被引用对象
    }
    // 参数组 3 条：J1.axis / J2.zeroOffset / J2.bounds（字段序钉住——
    // §4.3-A 行序 zeroOffset(表行 7) 在 bounds(表行 8) 之前）。
    ASSERT_EQ(report.parameters.size(), 3U);
    EXPECT_EQ(report.parameters[0].field, "axis");
    EXPECT_EQ(report.parameters[0].subjectPath, "joints[0].axis");
    EXPECT_EQ(report.parameters[0].objectId.toCanonical(), oidOf(1).toCanonical());
    EXPECT_TRUE(report.parameters[0].valueChanged);
    EXPECT_FALSE(report.parameters[0].provenanceChanged);
    EXPECT_EQ(report.parameters[1].field, "zeroOffset");
    EXPECT_EQ(report.parameters[2].field, "bounds");
    EXPECT_EQ(report.parameters[1].objectId.toCanonical(), oidOf(2).toCanonical());
    EXPECT_EQ(report.parameters[2].objectId.toCanonical(), oidOf(2).toCanonical());
    // 物性组 2 条：l1 质量（值变）/ l2 质量（仅来源变）。
    ASSERT_EQ(report.properties.size(), 2U);
    {
        const ModelDiffEntry* m1 = findEntry(report.properties, "mass", "links[1].body.mass");
        ASSERT_NE(m1, nullptr);
        EXPECT_TRUE(m1->valueChanged);
        EXPECT_FALSE(m1->provenanceChanged);
        EXPECT_NE(m1->baselineText.find("2"), std::string::npos);
        EXPECT_NE(m1->candidateText.find("3"), std::string::npos);
    }
    {
        const ModelDiffEntry* m2 = findEntry(report.properties, "mass", "links[2].body.mass");
        ASSERT_NE(m2, nullptr);
        EXPECT_FALSE(m2->valueChanged);   // 值不变
        EXPECT_TRUE(m2->provenanceChanged);  // 仅来源标记变化——仍入表
        EXPECT_NE(m2->baselineText.find("user-provided"), std::string::npos);
        EXPECT_NE(m2->candidateText.find("import-mapped"), std::string::npos);
    }
}

// =====================================================================
// ACC2：派生只读字段不作独立差异项（D-MDL-5 差异面=编码身份面三分支）
// =====================================================================

TEST(MdlModelDiff, DerivedReadOnlyFieldsNotReported_WP13T14_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08", "MDL-02"},
                  std::vector<std::string>{"AT-12"});

    // (a) 两侧均 StandardDH：axis/origin 为派生只读（D-MDL-5）——两侧
    //     派生缓存字节不同也不产生条目；dhDerived 为权威——其变化入表。
    {
        RobotDesign base = baseDesign();
        RobotDesign cand = baseDesign();
        base.authority = AuthorityMode::StandardDH;
        cand.authority = AuthorityMode::StandardDH;
        for (JointEntry& j : base.joints) {
            j.dhDerived = DhParameters{0.1, 0.2, 0.3, 0.4};  // θ rad/d·a m/α rad
        }
        for (JointEntry& j : cand.joints) {
            j.dhDerived = DhParameters{0.1, 0.2, 0.3, 0.4};
        }
        // 模拟"派生缓存不一致"（内存派生值可不同——不入身份，D-MDL-5）。
        cand.joints[0].axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(1.0, 0.0, 0.0), userProv());
        // 权威面差异：J2 的 DH 参数（d 变化——单位 m）。
        cand.joints[1].dhDerived = DhParameters{0.1, 0.25, 0.3, 0.4};

        const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
        // 派生侧零条目（axis/origin 差异不入表）。
        for (const ModelDiffEntry& e : report.parameters) {
            EXPECT_NE(e.field, "axis");
            EXPECT_NE(e.field, "origin");
        }
        // 权威面恰一条：J2.dhDerived。
        ASSERT_EQ(report.parameters.size(), 1U);
        EXPECT_EQ(report.parameters[0].field, "dhDerived");
        EXPECT_EQ(report.parameters[0].objectId.toCanonical(), oidOf(2).toCanonical());
        // 权威模式相同——无 authority 条目。
        for (const ModelDiffEntry& e : report.structure) {
            EXPECT_NE(e.field, "authority");
        }
    }

    // (b) 两侧均 Explicit：dhDerived 为派生展示值（不入编码身份）——其
    //     变化不产生条目；axis 权威——变化照常入表（正向对照）。
    {
        RobotDesign base = baseDesign();
        RobotDesign cand = baseDesign();
        base.joints[0].dhDerived = DhParameters{0.1, 0.2, 0.3, 0.4};
        cand.joints[0].dhDerived = DhParameters{0.9, 0.2, 0.3, 0.4};  // 展示缓存差异
        cand.joints[1].axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, 1.0), userProv());  // 权威差异

        const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
        ASSERT_EQ(report.parameters.size(), 1U);
        EXPECT_EQ(report.parameters[0].field, "axis");
        EXPECT_EQ(report.parameters[0].objectId.toCanonical(), oidOf(2).toCanonical());
    }

    // (c) 权威模式不同：受管三字段在至少一侧为派生值——全部跳过，
    //     authority 开关差异是唯一语义事实（恰一条结构组条目）。
    {
        RobotDesign base = baseDesign();
        RobotDesign cand = baseDesign();
        base.authority = AuthorityMode::Explicit;
        cand.authority = AuthorityMode::StandardDH;
        // 两侧受管字段字节均不同（若无规则将产生三条目）。
        cand.joints[0].axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(1.0, 0.0, 0.0), userProv());
        base.joints[0].dhDerived = DhParameters{0.1, 0.2, 0.3, 0.4};
        cand.joints[0].dhDerived = DhParameters{0.5, 0.2, 0.3, 0.4};

        const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
        ASSERT_EQ(report.structure.size(), 1U);
        EXPECT_EQ(report.structure[0].field, "authority");
        EXPECT_EQ(report.structure[0].kind, ModelDiffChangeKind::Modified);
        EXPECT_TRUE(report.parameters.empty());
        EXPECT_TRUE(report.properties.empty());
    }
}

// =====================================================================
// ACC2：确定性——同输入→同报告；组内稳定排序（对象 id→字段序）
// =====================================================================

TEST(MdlModelDiff, DeterminismAndStableFieldOrder_WP13T14_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08", "NFR-COR-02"},
                  std::vector<std::string>{"AT-12"});

    RobotDesign base = baseDesign();
    RobotDesign cand = baseDesign();
    // J2 四个参数字段全改（§4.3-A 行序：axis(5)→zeroOffset(7)→bounds(8)
    // →workingRange(9)——字段序断言的数据面）。
    cand.joints[1].axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(1.0, 0.0, 0.0), userProv());
    cand.joints[1].zeroOffset = 0.9;
    cand.joints[1].bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{-0.5, 0.5}, userProv());
    cand.joints[1].workingRange = core::SourcedValue<JointLimits>::provided(
        JointLimits{-2.0, 2.0}, userProv());
    cand.displayName = "demo2";

    const ModelingWorkingSet a = wsOf(base, 41);
    const ModelingWorkingSet b = wsOf(cand, 42);

    // 同输入重复 diff → 报告逐字段相等（NFR-COR-02——值面确定性）。
    const ModelDiffReport r1 = ModelDiffService{}.diff(a, b);
    const ModelDiffReport r2 = ModelDiffService{}.diff(a, b);
    EXPECT_TRUE(r1 == r2);

    // 组内排序：J2 的四条参数条目按字段表行序排列（同对象 id——字段序
    // 决定次序；机械钉住防表序漂移——与 Codec 字段序纪律同源）。
    std::vector<std::string> j2Fields;
    for (const ModelDiffEntry& e : r1.parameters) {
        if (e.objectId.toCanonical() == oidOf(2).toCanonical()) {
            j2Fields.push_back(e.field);
        }
    }
    ASSERT_EQ(j2Fields.size(), 4U);
    EXPECT_EQ(j2Fields[0], "axis");
    EXPECT_EQ(j2Fields[1], "zeroOffset");
    EXPECT_EQ(j2Fields[2], "bounds");
    EXPECT_EQ(j2Fields[3], "workingRange");

    // 结构组排序：根字段（全零锚——字节序最小）居对象/引用条目之前。
    ASSERT_FALSE(r1.structure.empty());
    EXPECT_EQ(r1.structure.front().field, "displayName");

    // 分组/三态 token 全词表（机器判读面——switch 全枚举的运行期对账）。
    EXPECT_EQ(std::string(modelDiffGroupToken(ModelDiffGroup::Structure)), "structure");
    EXPECT_EQ(std::string(modelDiffGroupToken(ModelDiffGroup::Parameters)), "parameters");
    EXPECT_EQ(std::string(modelDiffGroupToken(ModelDiffGroup::Properties)), "properties");
    EXPECT_EQ(std::string(modelDiffChangeKindToken(ModelDiffChangeKind::Added)), "added");
    EXPECT_EQ(std::string(modelDiffChangeKindToken(ModelDiffChangeKind::Removed)), "removed");
    EXPECT_EQ(std::string(modelDiffChangeKindToken(ModelDiffChangeKind::Modified)), "modified");
}

// =====================================================================
// ACC3：不覆盖基线——diff 纯函数、输入只读（post 不修改输入）
// =====================================================================

TEST(MdlModelDiff, PureFunctionInputsReadOnly_WP13T14_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08"},
                  std::vector<std::string>{"AT-12"});

    const RobotDesign baseD = baseDesign();
    RobotDesign candD = baseDesign();
    candD.joints[0].axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(1.0, 0.0, 0.0), userProv());
    candD.links[1].body.mass = core::SourcedValue<double>::provided(3.0, userProv());
    candD.displayName = "demo2";

    const ModelingWorkingSet baseline = wsOf(baseD, 41);
    const ModelingWorkingSet candidate = wsOf(candD, 42);

    // 前置证据：diff 前 deep 拷贝两工作集＋canonical 字节快照
    // （RobotDesignCodec——确定性编码，字节相等⇔逐字段相等）。
    const ModelingWorkingSet baselineBefore = baseline;
    const ModelingWorkingSet candidateBefore = candidate;
    const RobotDesignCodec codec;
    const auto baseBytesBefore = codec.encode(
        ObjectVariant{baseline.design}, kCurrentFormatVersion);
    const auto candBytesBefore = codec.encode(
        ObjectVariant{candidate.design}, kCurrentFormatVersion);
    ASSERT_TRUE(baseBytesBefore.ok());
    ASSERT_TRUE(candBytesBefore.ok());

    // 执行 diff（§9.4.9 const 纯函数——@post 不修改输入）。
    const ModelDiffReport report = ModelDiffService{}.diff(baseline, candidate);
    EXPECT_FALSE(report.structure.empty());

    // post 证据①：deep 相等（operator== 全字段——design/changes/根身份/
    // 部件视图七成员全等）。
    EXPECT_TRUE(baseline == baselineBefore);
    EXPECT_TRUE(candidate == candidateBefore);
    // post 证据②：canonical 字节逐位相等（独立于值相等的字节面证据）。
    const auto baseBytesAfter = codec.encode(
        ObjectVariant{baseline.design}, kCurrentFormatVersion);
    const auto candBytesAfter = codec.encode(
        ObjectVariant{candidate.design}, kCurrentFormatVersion);
    ASSERT_TRUE(baseBytesAfter.ok());
    ASSERT_TRUE(candBytesAfter.ok());
    EXPECT_EQ(baseBytesAfter.get(), baseBytesBefore.get());
    EXPECT_EQ(candBytesAfter.get(), candBytesBefore.get());
}

// =====================================================================
// ACC3：方向语义——交换 baseline/candidate 呈镜像（报告方向字段＋三态
// 增删互换、改态文本互换）
// =====================================================================

TEST(MdlModelDiff, DirectionMirrorOnSwap_WP13T14_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08"},
                  std::vector<std::string>{"AT-12"});

    RobotDesign base = baseDesign();
    RobotDesign cand = baseDesign();
    cand.joints[1].zeroOffset = 0.75;                     // 改
    cand.joints.pop_back();                               // 删 J3
    cand.links.pop_back();
    cand.joints.push_back(makeJoint(oidOf(4), "J4",      // 增 J4
                                    rw::math::Vector3D<double>(0.0, 0.0, 1.0), 0.0));
    cand.links.push_back(makeLink(oidOf(18), "l4", 1.0));

    const ModelDiffReport forward = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
    const ModelDiffReport backward = ModelDiffService{}.diff(wsOf(cand, 42), wsOf(base, 41));

    // 方向字段互换（报告级方向声明——acceptance 3"报告方向字段"）。
    ASSERT_TRUE(forward.baselineObjectId.has_value());
    ASSERT_TRUE(backward.candidateObjectId.has_value());
    EXPECT_EQ(forward.baselineObjectId->toCanonical(),
              backward.candidateObjectId->toCanonical());
    EXPECT_EQ(forward.candidateObjectId->toCanonical(),
              backward.baselineObjectId->toCanonical());

    // 镜像性质：forward 每条在 backward 找到同（组,锚,路径,字段）条目——
    // Added↔Removed 互换、Modified 不变、两侧文本互换、两面标记不变。
    auto mirrorOf = [&backward](const ModelDiffEntry& e) -> const ModelDiffEntry* {
        const std::vector<ModelDiffEntry>* group = &backward.structure;
        if (e.group == ModelDiffGroup::Parameters) { group = &backward.parameters; }
        if (e.group == ModelDiffGroup::Properties) { group = &backward.properties; }
        for (const ModelDiffEntry& m : *group) {
            if (m.objectId == e.objectId && m.subjectPath == e.subjectPath
                && m.field == e.field) {
                return &m;
            }
        }
        return nullptr;
    };
    std::size_t checked = 0;
    for (const std::vector<ModelDiffEntry>* group :
         {&forward.structure, &forward.parameters, &forward.properties}) {
        for (const ModelDiffEntry& e : *group) {
            const ModelDiffEntry* m = mirrorOf(e);
            ASSERT_NE(m, nullptr) << "镜像缺失: " << e.subjectPath;
            EXPECT_EQ(m->group, e.group);
            if (e.kind == ModelDiffChangeKind::Added) {
                EXPECT_EQ(m->kind, ModelDiffChangeKind::Removed);
            } else if (e.kind == ModelDiffChangeKind::Removed) {
                EXPECT_EQ(m->kind, ModelDiffChangeKind::Added);
            } else {
                EXPECT_EQ(m->kind, ModelDiffChangeKind::Modified);
            }
            EXPECT_EQ(m->baselineText, e.candidateText);  // 两侧文本互换
            EXPECT_EQ(m->candidateText, e.baselineText);
            EXPECT_EQ(m->valueChanged, e.valueChanged);
            EXPECT_EQ(m->provenanceChanged, e.provenanceChanged);
            ++checked;
        }
    }
    // 三态在 forward 侧均已出现（镜像断言的覆盖前提）。
    EXPECT_GE(checked, 3U);
}

// =====================================================================
// ACC2 补充面：引用集合/链序位/资源清单/defaultTcp/位姿集引用语义
// =====================================================================

TEST(MdlModelDiff, RefSetChainIndexAndResourceSemantics_WP13T14_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08"},
                  std::vector<std::string>{"AT-12"});

    // ① 引用表集合语义（§4.8——canonical 字节按字典序规范化：纯重排≠差异）。
    {
        RobotDesign base = baseDesign();
        RobotDesign cand = baseDesign();
        base.toolRefs.push_back(oidOf(21));
        base.toolRefs.push_back(oidOf(22));
        cand.toolRefs.push_back(oidOf(22));  // 同集合、不同顺序
        cand.toolRefs.push_back(oidOf(21));
        const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
        EXPECT_TRUE(report.structure.empty());  // 顺序差异不是身份差异——零条目
    }
    // ② 引用增删：锚＝被引用工具 id（点击定位语义——UX-13 跳到工具对象）。
    {
        RobotDesign base = baseDesign();
        RobotDesign cand = baseDesign();
        base.toolRefs.push_back(oidOf(21));
        cand.toolRefs.push_back(oidOf(22));
        const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
        ASSERT_EQ(report.structure.size(), 2U);
        EXPECT_EQ(report.structure[0].kind, ModelDiffChangeKind::Removed);
        EXPECT_EQ(report.structure[0].objectId.toCanonical(), oidOf(21).toCanonical());
        EXPECT_EQ(report.structure[1].kind, ModelDiffChangeKind::Added);
        EXPECT_EQ(report.structure[1].objectId.toCanonical(), oidOf(22).toCanonical());
    }
    // ③ 关节链重排（同 id 集合、不同链序）——chainIndex 条目（数组下标是
    //    权威语义：串联有序，§4.3）。
    {
        RobotDesign base = baseDesign();
        RobotDesign cand = baseDesign();
        std::swap(cand.joints[1], cand.joints[2]);  // [J1,J3,J2]
        const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
        ASSERT_EQ(report.structure.size(), 2U);  // J2、J3 各一条链序位条目
        for (const ModelDiffEntry& e : report.structure) {
            EXPECT_EQ(e.field, "chainIndex");
            EXPECT_EQ(e.kind, ModelDiffChangeKind::Modified);
        }
        // 排序按对象 id：oidOf(2)（J2，候选下标 2）在 oidOf(3)（J3，候选
        // 下标 1）之前——规范文本字节序。
        EXPECT_EQ(report.structure[0].objectId.toCanonical(), oidOf(2).toCanonical());
        EXPECT_EQ(report.structure[0].subjectPath, "joints[2]");
        EXPECT_EQ(report.structure[0].baselineText, "joints[1]");
        EXPECT_EQ(report.structure[0].candidateText, "joints[2]");
        EXPECT_EQ(report.structure[1].objectId.toCanonical(), oidOf(3).toCanonical());
    }
    // ④ 资源清单与 defaultTcp/poseSetRef。
    {
        RobotDesign base = baseDesign();
        RobotDesign cand = baseDesign();
        ResourceRef r1;
        r1.resourceId = "res-mesh";
        r1.state = ResourceState::Recorded;
        base.resourceManifest.push_back(r1);
        ResourceRef r2 = r1;
        r2.state = ResourceState::Solidified;  // 状态机推进（Recorded→Solidified）
        cand.resourceManifest.push_back(r2);
        ResourceRef r3;
        r3.resourceId = "res-extra";
        cand.resourceManifest.push_back(r3);
        cand.defaultTcp = TcpRef{oidOf(21), "tcp1"};
        cand.poseSetRef = oidOf(31);

        const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
        // 3 条：res-mesh 改 / res-extra 增 / defaultTcp 改 / poseSetRef 改
        // ——实为 4 条（资源两条＋引用两条）。
        ASSERT_EQ(report.structure.size(), 4U);
        const ModelDiffEntry* resMod =
            findEntry(report.structure, "resource", "resourceManifest[res-mesh]");
        ASSERT_NE(resMod, nullptr);
        EXPECT_EQ(resMod->kind, ModelDiffChangeKind::Modified);
        EXPECT_NE(resMod->candidateText.find("Solidified"), std::string::npos);  // 状态机推进
        EXPECT_NE(resMod->baselineText.find("Recorded"), std::string::npos);
        const ModelDiffEntry* resAdd =
            findEntry(report.structure, "resource", "resourceManifest[res-extra]");
        ASSERT_NE(resAdd, nullptr);
        EXPECT_EQ(resAdd->kind, ModelDiffChangeKind::Added);
        const ModelDiffEntry* tcp =
            findEntry(report.structure, "defaultTcp", "defaultTcp");
        ASSERT_NE(tcp, nullptr);
        EXPECT_EQ(tcp->objectId.toCanonical(), oidOf(21).toCanonical());  // 锚＝工具
        const ModelDiffEntry* pose =
            findEntry(report.structure, "poseSetRef", "poseSetRef");
        ASSERT_NE(pose, nullptr);
        EXPECT_EQ(pose->objectId.toCanonical(), oidOf(31).toCanonical());
    }
}

// =====================================================================
// ACC4：AT-12 数据侧——比较型差异逐项可观察（呈现面归 WP-22-T11/UX-13；
// 接口零 UI 类型依赖由 ModelDiff.hpp 数据面构成＋BuildRedLineTest 产品面
// 零 Qt 扫描＋ird_gates 复核三重承载）
// =====================================================================

TEST(MdlModelDiff, At12DataSidePerItemObservability_WP13T14_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08"},
                  std::vector<std::string>{"AT-12"});

    // 构造恰 3 处已知差异（每组一处）——期望条目总数＝3（一处差异↔一条
    // 条目：无聚合、无遗漏、无副生条目）。
    RobotDesign base = baseDesign();
    RobotDesign cand = baseDesign();
    cand.displayName = "demo2";                                        // 结构组 1 条
    cand.joints[1].zeroOffset = 0.75;                                  // 参数组 1 条
    cand.links[2].body.mass = core::SourcedValue<double>::provided(
        2.0, importProv());                                            // 物性组 1 条（仅来源）

    const ModelDiffReport report = ModelDiffService{}.diff(wsOf(base, 41), wsOf(cand, 42));
    const std::size_t total = report.structure.size() + report.parameters.size()
        + report.properties.size();
    EXPECT_EQ(total, 3U);
    EXPECT_EQ(report.structure.size(), 1U);
    EXPECT_EQ(report.parameters.size(), 1U);
    EXPECT_EQ(report.properties.size(), 1U);

    // 逐项可观察：每条目定位四元组完整（组/三态/锚/路径/字段列），两面
    // 标记至少一面为真——呈现面消费所需的全部数据要素在条目上自足。
    for (const std::vector<ModelDiffEntry>* group :
         {&report.structure, &report.parameters, &report.properties}) {
        for (const ModelDiffEntry& e : *group) {
            EXPECT_FALSE(e.subjectPath.empty());
            EXPECT_FALSE(e.field.empty());
            EXPECT_TRUE(e.valueChanged || e.provenanceChanged);
            EXPECT_FALSE(e.baselineText.empty() && e.candidateText.empty());
        }
    }
    // 数据实体零行为/零 UI 依赖的结构面：报告为纯值聚合（operator== 可
    // 拷贝可比对——M-5 分工红线的类型面证据；零 Qt 由产品面扫描承载）。
    const ModelDiffReport copy = report;
    EXPECT_TRUE(copy == report);
}
