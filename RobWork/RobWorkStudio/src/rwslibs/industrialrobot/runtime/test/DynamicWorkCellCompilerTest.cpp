/**
 * @file   DynamicWorkCellCompilerTest.cpp
 * @brief  RT-T08 测试——S7 DynamicWorkCell 编译器（能力门控/原子性/关联
 *         校验/销毁顺序）＋DWC 只读视图。
 *
 * 设计依据（用例↔需求/验收标准追溯）：
 *   - RT-CPX-2（DYN-06/MDL-06 断言分域）：全连杆物性 NotProvided→降级——
 *     SkippedNoPhysics＋hasDynamicWorkCell=false＋警告清单（非失败、非输入
 *     非法）；混合缺失→缺失对象清单精确定位。
 *   - §5.6 正交关系（RT-CAP-2 同源语义）：能力缺失＝正常返回＋警告；provided
 *     而非法（mass=−1）＝S5 构造拒绝（InputInvalid）——两者严格区分，S7 不
 *     收到非法输入；runtime 输出无任何工程判定字段（降级路径不抛、无裁决）。
 *   - RT-CPX-1（MDL-06/AT-01）：WC 编译成功、DWC 构造异常（注入重名承载帧
 *     ——真实基线 RW_THROW 路径）→DwcCompileFailed（stage=S7）、WC 结构
 *     零残留（原子性——承载帧未入树）。
 *   - RT-AD-3（§8.5 销毁顺序）：快照侧持有结构析构（含 DWC）——WC 实际
 *     释放不早于 DWC（引用计数观测：outcome 析构后 WC 仍因 DWC 自持引用
 *     存活）、全部对象最终释放（无泄漏）、持 DWC 期间 WC 可查（无
 *     use-after-free）。ASAN 在本工程工具链不可用（MSVC），以 shared_ptr
 *     控制块计数做等价观测——证据登记 §15.4 v0.9。
 *   - §8.5 同源/Body 覆盖/设备一致：编译器内置校验的正向断言（Body 数＝
 *     有物性连杆＋有物性工具、逐体质量/质心/惯量/重力显式设值读回——
 *     §8.2"DWC 重力、每 Body 质量惯量"）＋R-2 摩擦承载读回。
 *   - RT-STUB-0（§11）：RobWork/rwsim 本体不做替身——全部断言针对真实基线
 *     库构造的 WorkCell/DynamicWorkCell。
 *
 * 工程事实（两模式差异，CMake 同步登记）：本文件链接真实框架库（rwsim
 * DynamicWorkCell/RigidDevice 为非模板外联符号），**只在集成模式编译**——
 * 冒烟模式不进测试目标（units/runtime.md §15.4 v0.9 RT-T08 登记条目）。
 *
 * 确定性：夹具 id/摘要由固定种子派生（CanonicalModelFixture 同款）；无
 * 随机/时钟/locale 依赖——全部断言跨进程可复现（NFR-COR-02）。
 */

#include "DynamicWorkCellCompiler.hpp"  // 被测：S7 编译器（单元私有头——测试与 src 同权）

#include <sdurws/ird/runtime/Adapter.hpp>   // DynamicWorkCellConstView（视图面）
#include <sdurws/ird/runtime/Errors.hpp>    // token（稳定码断言）
#include <sdurws/ird/runtime/NameMap.hpp>   // buildRuntimeNameMap（期望名来源）

#include <rwsim/dynamics/Body.hpp>             // Body::getInfo（物性读回）
#include <rwsim/dynamics/DynamicWorkCell.hpp>  // DWC 查询面（getBodies/getGravity）
#include <rwsim/dynamics/MaterialDataMap.hpp>  // 摩擦承载读回（§8.8 无补丁方案）
#include <rwsim/dynamics/RigidDevice.hpp>      // 设备一致校验读数（getLinks）

#include <rw/kinematics/FixedFrame.hpp>  // 探针注入：承载帧重名（真实 RW_THROW）
#include <rw/kinematics/Frame.hpp>       // 关节帧回溯（设备一致校验的测试复算）
#include <rw/math/Q.hpp>                 // 摩擦参数读回
#include <rw/models/Joint.hpp>           // WC 侧关节名集合（设备一致）
#include <rw/models/JointDevice.hpp>     // WC 设备关节集
#include <rw/models/WorkCell.hpp>        // WC 查询面

#include "CanonicalModelFixture.hpp"     // minimal/rich 夹具（确定性 id）

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
using sdurws::ird::runtime::testfixture::Fixture;
using sdurws::ird::runtime::testfixture::idFrom;
using sdurws::ird::runtime::testfixture::minimalFixture;
using sdurws::ird::runtime::testfixture::richFixture;
using sdurws::ird::runtime::testfixture::val;
using sdurws::ird::runtime::testfixture::expectBuildThrows;

namespace core = sdurws::ird::core;

// =====================================================================
// 测试工具（确定性；与产品代码无共享实现——独立复算作对照）。
// =====================================================================

/**
 * @brief S6→S7 两段串联的便捷入口（先编译 WC 再编译 DWC——契约输入序；
 *        返回双产物供分项断言）。
 */
struct TwoStageResult
{
    WorkCellCompileOutcome s6;                 ///< S6 产物（WC/设备名/运行时名）
    DynamicWorkCellCompileOutcome s7;          ///< S7 产物（DWC 或降级标记）
};

TwoStageResult compileBoth(const CanonicalModel& model)
{
    TwoStageResult r;
    r.s6 = compileWorkCell(model);
    r.s7 = compileDynamicWorkCell(model, r.s6);
    return r;
}

/// 断言调用抛出指定稳定码的 RuntimeError 且 detail 含 stage=S7 定位。
void expectS7Throws(const CanonicalModel& model, const WorkCellCompileOutcome& s6,
                    RuntimeErrorCode code, const char* what)
{
    try {
        compileDynamicWorkCell(model, s6);
        ADD_FAILURE() << what << "：未抛出（期望稳定码 " << token(code) << "）";
    } catch (const RuntimeError& e) {
        EXPECT_EQ(e.code(), code) << what << "：稳定码不符（实得 " << token(e.code())
                                  << "，detail=" << e.what() << "）";
        if (code == RuntimeErrorCode::DwcCompileFailed) {
            // §8.4 转译表"＋stage"——detail 必含段号定位（RT-CPX-1 断言面）。
            EXPECT_NE(std::string(e.what()).find("stage=S7"), std::string::npos)
                << what << "：detail 缺 stage=S7 段号定位（" << e.what() << "）";
        }
    } catch (const std::exception& e) {
        ADD_FAILURE() << what << "：抛出非 RuntimeError 异常（" << e.what() << "）";
    }
}

/**
 * @brief 夹具变形：把某连杆物性三元组全部抹为 NotProvided（能力缺失注入
 *        ——NotProvided 是"未提供"，与"provided 而非法"严格分域，§5.6）。
 */
void stripLinkPhysics(Fixture& f, std::size_t linkIndex)
{
    CanonicalLink& l = f.chain.links.at(linkIndex);
    l.mass = core::SourcedValue<double>{};
    l.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>{};
    l.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>{};
}

// =====================================================================
// RT-CPX-2：物性缺失降级（DYN-06/MDL-06 断言分域）。
// =====================================================================

/**
 * RT-CPX-2 主断言：全连杆物性 NotProvided（minimal 夹具）→不抛、整体
 * 降级——status=SkippedNoPhysics、DWC 空、缺失清单＝全连杆（链序）、
 * 警告恰一条 RT-CAPABILITY-MISSING（subject＝首个缺失连杆）。WC 不被
 * 触碰（帧数不变——零副作用）。
 */
TEST(DynamicWorkCellCompilerTest, AllLinksNoPhysicsSkipsDwc_RT_CPX_2)
{
    const CanonicalModel model = minimalFixture().build();
    ASSERT_FALSE(model.capabilities().hasDynamicWorkCell)
        << "前置：minimal 夹具能力位应为 false（S5 派生口径）";

    const TwoStageResult r = compileBoth(model);

    // 降级而非失败：两段都正常返回（无异常——能力缺失不妨碍发布，§5.6）。
    EXPECT_EQ(r.s7.status, DwcCompileStatus::SkippedNoPhysics);
    EXPECT_TRUE(r.s7.dynamicWorkCell.isNull()) << "降级路径不得产出 DWC";
    ASSERT_NE(r.s7.workCell.get(), nullptr) << "WC 同源透传（§8.5 顺序载体）";

    // 缺失对象清单＝全连杆（3 条，链序）——§9.1"Skipped 时的缺失对象清单"。
    const RobotChain& chain = model.chain();
    ASSERT_EQ(r.s7.skippedObjects.size(), chain.links.size());
    for (std::size_t i = 0; i < chain.links.size(); ++i) {
        EXPECT_EQ(r.s7.skippedObjects.at(i), chain.links.at(i).objectId)
            << "缺失清单第 " << i << " 项与链序不符（§9.1）";
    }

    // 警告清单：恰一条、稳定码/subject/级别语义（§9.6"每缺失一条警告、
    // subject 定位首个缺失对象"；RT-CAPABILITY-MISSING＝警告级事件码——
    // 非 error 级编译失败码面）。
    ASSERT_EQ(r.s7.warnings.size(), 1u);
    const core::DiagnosticRecord& warn = r.s7.warnings.front();
    EXPECT_EQ(warn.code, "RT-CAPABILITY-MISSING");
    ASSERT_TRUE(warn.subject.has_value());
    EXPECT_EQ(*warn.subject, chain.links.front().objectId)
        << "subject 应定位首个缺失对象（链序第一）";
    EXPECT_FALSE(warn.cause.empty());

    // 零副作用：WC 帧数与 S6 产物一致（降级路径不触碰 WC）。
    EXPECT_EQ(r.s7.workCell->getFrames().size(), r.s6.workCell->getFrames().size());
}

/**
 * RT-CPX-2 混合缺失：仅中间连杆缺物性→缺失清单恰一条、subject 定位该
 * 连杆；能力位 false 与缺失清单互核一致（§9.6 派生口径的 S7 侧复核）。
 */
TEST(DynamicWorkCellCompilerTest, PartialMissingPhysicsSkipsWithPreciseList_RT_CPX_2)
{
    Fixture f = minimalFixture();
    // link[1] 抹缺（minimal 本无物性，先补齐再抹中间——精确单点缺失）。
    for (std::size_t i = 0; i < f.chain.links.size(); ++i) {
        CanonicalLink& l = f.chain.links.at(i);
        l.mass = val(4.0);  // 单位 kg
        l.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, 0.05), testfixture::userProv());
        l.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
            testfixture::diagInertia(0.01, 0.02, 0.03), testfixture::userProv());
    }
    stripLinkPhysics(f, 1);  // 仅 link_1 缺失

    const CanonicalModel model = f.build();
    ASSERT_FALSE(model.capabilities().hasDynamicWorkCell);

    const TwoStageResult r = compileBoth(model);
    EXPECT_EQ(r.s7.status, DwcCompileStatus::SkippedNoPhysics);
    ASSERT_EQ(r.s7.skippedObjects.size(), 1u);
    EXPECT_EQ(r.s7.skippedObjects.front(), f.chain.links.at(1).objectId)
        << "缺失清单应精确指向被抹缺的连杆";

    ASSERT_EQ(r.s7.warnings.size(), 1u);
    ASSERT_TRUE(r.s7.warnings.front().subject.has_value());
    EXPECT_EQ(*r.s7.warnings.front().subject, f.chain.links.at(1).objectId)
        << "subject 应定位唯一缺失连杆";
}

// =====================================================================
// §5.6 正交关系：能力缺失 ≠ 输入非法 ≠ 编译失败（RT-CAP-2 同源语义）。
// =====================================================================

/**
 * §5.6 正交主断言（RT-CAP-2 的 runtime 侧承载）：provided 而非法
 * （mass=−1）在 S5 构造即被拒绝（InputInvalid）——非法输入到不了 S7；
 * 对照 RT-CPX-2：NotProvided 走降级返回。两条路径的码面严格区分
 * （InputInvalid vs 无异常＋警告），无任何工程判定字段产生。
 */
TEST(DynamicWorkCellCompilerTest, ProvidedIllegalIsNotCapabilityMissing_S5_6)
{
    Fixture f = minimalFixture();
    // 全连杆补物性后把首连杆质量改为 provided 非法值（m=−1 kg）。
    for (std::size_t i = 0; i < f.chain.links.size(); ++i) {
        CanonicalLink& l = f.chain.links.at(i);
        l.mass = val(4.0);
        l.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, 0.05), testfixture::userProv());
        l.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
            testfixture::diagInertia(0.01, 0.02, 0.03), testfixture::userProv());
    }
    f.chain.links.at(0).mass = val(-1.0);  // provided 而非法（§5.6 典型反例）

    // 非法输入＝S5 构造拒绝（InputInvalid，就地定位）——绝不降级为能力
    // 缺失、也不进 S7（MDL-06 断言分域：两者严格区分，RT-CAP-2 口径）。
    expectBuildThrows(f.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "provided 非法（m=−1）应在 S5 构造拒绝");
}

// =====================================================================
// Compiled 主路径：构造正确性＋§8.2 显式设值＋§8.5 关联校验正向断言。
// =====================================================================

/**
 * Compiled 路径主断言（rich 夹具：3 连杆＋1 有物性工具全齐备）：
 *   status=Compiled、DWC/WC 非空、同源（§8.5 行 1）、Body 数＝3+1（行 2）、
 *   逐体质量/质心/惯量与 canonical 逐字一致（§8.2"每 Body 质量惯量"）、
 *   重力读回＝权威值（§8.2"DWC 重力"——基线默认 −9.82≠−9.81，读回即
 *   设值路径证据）。
 */
TEST(DynamicWorkCellCompilerTest, CompiledPathBodiesAndExplicitValues_RT_T08)
{
    const CanonicalModel model = richFixture().build();
    ASSERT_TRUE(model.capabilities().hasDynamicWorkCell);

    const TwoStageResult r = compileBoth(model);
    EXPECT_EQ(r.s7.status, DwcCompileStatus::Compiled);
    ASSERT_TRUE(!r.s7.dynamicWorkCell.isNull());
    ASSERT_TRUE(!r.s7.workCell.isNull());
    EXPECT_EQ(r.s7.skippedObjects.size(), 0u);
    EXPECT_EQ(r.s7.warnings.size(), 0u) << "全齐备路径无警告（§5.6 组合表第 1 行）";

    const rwsim::dynamics::DynamicWorkCell& dwc = *r.s7.dynamicWorkCell;

    // §8.5 行 1 同源：DWC.getWorkCell() 指向 S6 产物 WC（防御性断言的正向面）。
    EXPECT_EQ(dwc.getWorkCell().get(), r.s6.workCell.get());

    // §8.5 行 2 Body 覆盖：3 连杆＋1 有物性工具＝4。
    ASSERT_EQ(dwc.getBodies().size(), 4u);

    // §8.2 逐体物性读回：每连杆经映射 Body 名命中，质量/质心/惯量逐字。
    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    for (std::size_t i = 0; i < model.chain().links.size(); ++i) {
        const CanonicalLink& link = model.chain().links.at(i);
        const rwsim::dynamics::Body::Ptr body =
            dwc.findBody(scopedFullName(nameMap, link.objectId, NameScope::Body));
        ASSERT_TRUE(!body.isNull()) << "连杆体未命中（Body 覆盖）: index " << i;
        EXPECT_DOUBLE_EQ(body->getInfo().mass, *link.mass.tryValue())
            << "连杆体质量与权威不符（§8.2 显式设值）";
        // 值拷贝（tryValue 按值返回 optional——绑定其子对象引用会悬垂）。
        const rw::math::Vector3D<double> com = *link.centerOfMass.tryValue();
        for (int a = 0; a < 3; ++a) {
            EXPECT_DOUBLE_EQ(body->getInfo().masscenter[a], com[a])
                << "连杆体质心分量 " << a << " 与权威不符（体系下表示，单位 m）";
        }
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                EXPECT_DOUBLE_EQ(body->getInfo().inertia(row, col),
                                 (*link.inertia.tryValue())(row, col))
                    << "连杆体惯量元素 (" << row << "," << col
                    << ") 与权威不符（质心系，kg·m²）";
            }
        }
    }

    // 工具体：名＝映射 Tcp 条目（§7.1 Body 范围仅 link——工具复用 Tcp 名）。
    const CanonicalTool& tool = model.tools().front();
    const rwsim::dynamics::Body::Ptr toolBody =
        dwc.findBody(scopedFullName(nameMap, tool.objectId, NameScope::Tcp));
    ASSERT_TRUE(!toolBody.isNull()) << "有物性工具体未命中";
    EXPECT_DOUBLE_EQ(toolBody->getInfo().mass, *tool.mass.tryValue());

    // §8.2"DWC 重力"：读回＝权威 world.gravityWorld（0,0,−9.81）——基线
    // 构造默认 (0,0,−9.82) 被显式覆写（数值差即设值证据，DYN-01）。
    const rw::math::Vector3D<>& g = dwc.getGravity();
    const rw::math::Vector3D<double>& gExpected = model.world().gravityWorld;
    for (int a = 0; a < 3; ++a) {
        EXPECT_DOUBLE_EQ(g[a], gExpected[a]) << "重力分量 " << a << "（m/s²，世界系）";
    }
    EXPECT_DOUBLE_EQ(g[2], -9.81) << "重力应取 canonical 默认 −9.81（非基线 −9.82）";
}

/**
 * §8.5 行 3 设备一致＋RigidDevice 关节定位：RigidDevice 恰一个、运动学
 * 模型＝WC 设备；各连杆体承载帧回溯的首个关节名集合＝WC 设备关节名集合
 * （测试内独立复算——与编译器内置校验互证）。
 */
TEST(DynamicWorkCellCompilerTest, DeviceConsistencyJointsCrossCheck_RT_T08)
{
    const CanonicalModel model = richFixture().build();
    const TwoStageResult r = compileBoth(model);
    const rwsim::dynamics::DynamicWorkCell& dwc = *r.s7.dynamicWorkCell;

    // DWC 设备清单恰一个 RigidDevice，运动学模型＝WC 的同名设备（同源）。
    ASSERT_EQ(dwc.getDynamicDevices().size(), 1u);
    const rwsim::dynamics::RigidDevice::Ptr rigidDevice =
        dwc.getDynamicDevices().front().cast<rwsim::dynamics::RigidDevice>();
    ASSERT_TRUE(!rigidDevice.isNull());
    EXPECT_EQ(rigidDevice->getKinematicModel().get(),
              r.s6.workCell->findDevice(model.chain().deviceName).get());

    // 测试内独立复算：连杆体承载帧回溯关节名集合 vs WC 设备关节名集合。
    std::set<std::string> jointsViaBodies;
    for (const rwsim::dynamics::Body::Ptr& body : rigidDevice->getLinks()) {
        rw::kinematics::Frame* frame = body->getBodyFrame();  // 裸指针（基线形态）
        rw::models::Joint* joint = nullptr;
        while (frame != nullptr && joint == nullptr) {
            joint = dynamic_cast<rw::models::Joint*>(frame);
            frame = frame->getParent();
        }
        ASSERT_NE(joint, nullptr) << "连杆体应回溯到关节（链结构前提）";
        jointsViaBodies.insert(joint->getName());
    }
    std::set<std::string> jointsViaDevice;
    const rw::core::Ptr<rw::models::JointDevice> device =
        r.s6.workCell->findDevice(model.chain().deviceName).cast<rw::models::JointDevice>();
    ASSERT_TRUE(!device.isNull());
    for (const rw::models::Joint* j : device->getJoints()) {
        jointsViaDevice.insert(j->getName());
    }
    EXPECT_EQ(jointsViaBodies, jointsViaDevice)
        << "RigidDevice 关节集应与 WC 设备关节集逐名一致（§8.5 行 3）";
    EXPECT_EQ(jointsViaBodies.size(), model.chain().joints.size())
        << "移动连杆体应逐关节覆盖（N 体 ↔ N 关节）";
}

/**
 * R-2 摩擦承载读回（§8.8 无补丁方案）：摩擦全 Provided 关节→材质注册
 * （id＝关节映射全名）＋(mat, mat) 对上的 Custom 数据，参数名 fv/fc/bias
 * 与权威值逐字一致；无摩擦关节不注册（数据不伪造）。
 */
TEST(DynamicWorkCellCompilerTest, FrictionCarriedViaMaterialDataMap_R2)
{
    const CanonicalModel model = richFixture().build();  // j1/j2 摩擦全 Provided
    const TwoStageResult r = compileBoth(model);
    const rwsim::dynamics::DynamicWorkCell& dwc = *r.s7.dynamicWorkCell;
    const rwsim::dynamics::MaterialDataMap& materials = dwc.getMaterialData();

    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    for (const CanonicalJoint& joint : model.chain().joints) {
        const std::string matId =
            nameMap.resolveObjectId(joint.objectId).get().fullName;
        ASSERT_TRUE(materials.hasFrictionData(matId, matId,
                                              static_cast<int>(
                                                  rwsim::dynamics::Custom)))
            << "关节摩擦承载缺失（材质对自定义数据）: " << matId;
        const rwsim::dynamics::FrictionData& data =
            materials.getFrictionData(matId, matId,
                                      static_cast<int>(
                                          rwsim::dynamics::Custom));
        EXPECT_EQ(data.typeName, "ird-joint-friction") << "承载约定名（§15.4 v0.9）";
        ASSERT_EQ(data.parameters.size(), 3u);
        EXPECT_EQ(data.parameters.at(0).first, "fv");
        EXPECT_DOUBLE_EQ(data.parameters.at(0).second[0],
                         *joint.friction.viscous.tryValue())
            << "粘性摩擦 fv（N·m·s/rad）与权威不符";
        EXPECT_EQ(data.parameters.at(1).first, "fc");
        EXPECT_DOUBLE_EQ(data.parameters.at(1).second[0],
                         *joint.friction.coulomb.tryValue())
            << "库仑摩擦 fc（N·m）与权威不符";
        EXPECT_EQ(data.parameters.at(2).first, "bias");
        EXPECT_DOUBLE_EQ(data.parameters.at(2).second[0],
                         *joint.friction.bias.tryValue())
            << "摩擦偏置 bias（N·m）与权威不符";
    }
}

/**
 * 门控细化（RT-T04 登记口径的 S7 侧）：全连杆物性齐备但工具无物性→
 * 仍 Compiled（工具物性缺项不阻止 DWC 构造），Body 数＝3（无工具体）；
 * 摩擦缺项→Compiled 且不注册承载材质（hasFrictionModel=false 的数据面）。
 */
TEST(DynamicWorkCellCompilerTest, ToolAndFrictionMissingStillCompiles_RT_T08)
{
    Fixture f = richFixture();
    // 抹工具物性（工具仍在模型中——仅无物性）＋抹关节摩擦。
    f.tools.front().mass = core::SourcedValue<double>{};
    f.tools.front().centerOfMass =
        core::SourcedValue<rw::math::Vector3D<double>>{};
    f.tools.front().inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>{};
    for (CanonicalJoint& j : f.chain.joints) {
        j.friction = CanonicalJointFriction{};
    }
    const CanonicalModel model = f.build();
    ASSERT_TRUE(model.capabilities().hasDynamicWorkCell)
        << "hasDynamicWorkCell 只看连杆（RT-T04 登记口径）";
    ASSERT_FALSE(model.capabilities().hasFullMassInertia);
    ASSERT_FALSE(model.capabilities().hasFrictionModel);

    const TwoStageResult r = compileBoth(model);
    EXPECT_EQ(r.s7.status, DwcCompileStatus::Compiled);
    ASSERT_TRUE(!r.s7.dynamicWorkCell.isNull());
    // Body 数＝3 连杆（工具体不计——§8.5 覆盖口径）。
    EXPECT_EQ(r.s7.dynamicWorkCell->getBodies().size(), 3u);
    // 摩擦承载不注册（材质缺位）。注意不能用 hasFrictionData 探未注册名
    // ——基线 getDataID 对未知名直接 RW_THROW（MaterialDataMap.cpp 实测），
    // 故改查材质清单（首项为基线构造预置的空串占位）。
    const std::vector<std::string>& mats =
        r.s7.dynamicWorkCell->getMaterialData().getMaterials();
    EXPECT_EQ(std::find(mats.begin(), mats.end(), "IRB_T.joint_1"), mats.end())
        << "无摩擦关节不应注册承载材质（数据不伪造）";
}

// =====================================================================
// RT-CPX-1：DWC 构造异常→整体失败（原子性）。
// =====================================================================

/**
 * RT-CPX-1：WC 编译成功、DWC 构造异常（注入：向 S6 产物 WC 预置与首个体
 * 承载帧同名的帧——真实基线 StateStructure"Frame name is not unique"
 * RW_THROW 路径）→DwcCompileFailed（stage=S7 定位）、S6 WC 结构零残留
 * （承载帧未入树——帧数与注入后基线一致，MDL-06 原子性在 S7 层的形态：
 * 失败不产出半成品 DWC，调用方按事务纪律弃置 WC）。
 */
TEST(DynamicWorkCellCompilerTest, DwcConstructionFailureIsAtomic_RT_CPX_1)
{
    const CanonicalModel model = richFixture().build();
    WorkCellCompileOutcome s6 = compileWorkCell(model);

    // 注入坏数据路径：预置基座连杆 Body 承载帧同名帧（首个 S7 挂帧操作即
    // 触发基线重名异常——RT-AD-1 同款真实异常通道，非替身）。
    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    const std::string poisonedName =
        scopedFullName(nameMap, model.chain().links.front().objectId, NameScope::Body);
    const std::size_t framesBefore = s6.workCell->getFrames().size();
    rw::core::Ptr<rw::kinematics::FixedFrame> poison =
        rw::core::ownedPtr(new rw::kinematics::FixedFrame(
            poisonedName, detail::identityTransform3D()));
    s6.workCell->addFrame(poison, s6.workCell->getWorldFrame());

    expectS7Throws(model, s6, RuntimeErrorCode::DwcCompileFailed,
                   "DWC 构造异常应转译为 DwcCompileFailed");

    // 原子性：失败调用后 WC 帧集合＝注入后基线（S7 承载帧未入树——首操作
    // 即败、零残留；异常时局部瞬态对象经 RAII 析构）。
    EXPECT_EQ(s6.workCell->getFrames().size(), framesBefore + 1)
        << "WC 帧数应仅含注入帧（S7 零新增残留）";
    EXPECT_EQ(s6.workCell->findFrame(poisonedName), poison.get())
        << "注入帧仍在（对照锚）——S7 的同名承载帧未顶替/未入树";
}

/**
 * RT-CPX-1 防御面：S6 产物 WC 为空（调用方契约违约）→RobWorkError
 * fail-fast（§8.4 空句柄行——不产出半成品，不静默）。
 */
TEST(DynamicWorkCellCompilerTest, NullWorkCellRejected_RT_T08)
{
    const CanonicalModel model = richFixture().build();
    WorkCellCompileOutcome empty;
    empty.deviceName = model.chain().deviceName;  // 有名无 WC——违约形态

    try {
        compileDynamicWorkCell(model, empty);
        ADD_FAILURE() << "空 WC 句柄应 fail-fast";
    } catch (const RuntimeError& e) {
        EXPECT_EQ(e.code(), RuntimeErrorCode::RobWorkError)
            << "空句柄应归 robwork-error（§8.4），实得 " << token(e.code());
    }
}

// =====================================================================
// RT-AD-3：销毁顺序（§8.5——DWC 先于 WC 实际释放；无泄漏；无 UAF）。
// =====================================================================

/**
 * RT-AD-3：持有结构析构顺序的引用计数观测（§8.5 销毁顺序行的行为面
 * 钉法——成员声明序 dynamicWorkCell→workCell，析构逆序使 WC 成员先出
 * 作用域；WC 对象因 rwsim DWC 自持 WC 引用而存活至 DWC 释放）：
 *   1) outcome 析构后（工作副本 dwcKeep 仍持有 DWC）：WC weak 未过期
 *      ——WC 未在 DWC 之前被实际释放（若 WC 先死则 weak 过期，断言失败）；
 *   2) 持 DWC 期间 WC 结构可查（无 use-after-free）；
 *   3) 释放最后持有者后全部控制块过期（无泄漏）。
 * 注：ASAN 在 MSVC 工具链不可用，以 shared_ptr 控制块做等价泄漏/UAF
 * 观测——证据登记 §15.4 v0.9（RT-AD-3 行）。
 */
TEST(DynamicWorkCellCompilerTest, DestructionOrderDwcBeforeWorkCell_RT_AD_3)
{
    std::weak_ptr<const rw::models::WorkCell> observeWorkCell;
    std::weak_ptr<const rwsim::dynamics::DynamicWorkCell> observeDwc;

    {
        const CanonicalModel model = richFixture().build();
        const TwoStageResult r = compileBoth(model);
        ASSERT_EQ(r.s7.status, DwcCompileStatus::Compiled);

        // 观测器：借 std::shared_ptr 控制块计数（rw::core::Ptr 的底层承载
        // ——Ptr::getCppSharedPtr 实测）。
        observeWorkCell = r.s7.workCell.getCppSharedPtr();
        observeDwc = r.s7.dynamicWorkCell.getCppSharedPtr();
        ASSERT_FALSE(observeWorkCell.expired());
        ASSERT_FALSE(observeDwc.expired());

        // DWC 工作副本（模拟快照外借——RT-T09 视图借持形态）。
        rwsim::dynamics::DynamicWorkCell::Ptr dwcKeep = r.s7.dynamicWorkCell;

        {   // ★ 构造 §8.5 声明序的持有结构并析构：成员序〔dynamicWorkCell,
            // workCell〕→析构逆序＝workCell 成员先出作用域。若"WC 对象随
            // 之释放"（即 DWC 未保活 WC），下方 weak 过期断言即失败。
            struct SnapshotOrderHolder
            {
                rwsim::dynamics::DynamicWorkCell::Ptr dynamicWorkCell;  // 先声明
                rw::core::Ptr<rw::models::WorkCell> workCell;           // 后声明
            } holder;
            holder.dynamicWorkCell = r.s7.dynamicWorkCell;
            holder.workCell = r.s7.workCell;
        }  // holder 析构——WC 成员先释放（引用计数−1），DWC 成员随释（−1，
           // 但 dwcKeep 与 DWC 内部 _workcell 引用仍在）。

        // 断言 1：WC 实际释放不早于 DWC——holder 的 WC 成员已析构而对象
        // 仍存活（DWC 自持引用保活）＝"DWC 先于 WC 释放"（§8.5）。
        EXPECT_FALSE(observeWorkCell.expired())
            << "WC 不应在 DWC 之前被实际释放（§8.5 销毁顺序）";
        EXPECT_FALSE(observeDwc.expired());

        // 断言 2：持 DWC 期间 WC 可查——无 use-after-free（经 DWC 读回
        // Body 与经 WC 查帧双面；findFrame 返回裸指针，未命中为 nullptr）。
        EXPECT_EQ(dwcKeep->getBodies().size(), 4u);
        EXPECT_TRUE(dwcKeep->getWorkCell()->findFrame("IRB_T.BaseMount") != nullptr)
            << "经 DWC 自持引用查询 WC 结构应有效（无 UAF）";
    }
    // 作用域末：dwcKeep 已析构（最后 DWC 持有者）→DWC 释放→其内部 WC
    // 引用随之释放→WC 对象此刻才释放。

    // 断言 3：全部控制块过期——无泄漏（RT-AD-3"无泄漏"面）。
    EXPECT_TRUE(observeDwc.expired()) << "DWC 应随最后持有者释放（无泄漏）";
    EXPECT_TRUE(observeWorkCell.expired())
        << "WC 应在 DWC 释放后随之释放（引用链闭合，无泄漏）";
}

// =====================================================================
// DynamicWorkCellConstView（§8.2/§8.3 只读包装——RT-T08 交付面）。
// =====================================================================

/**
 * 视图查询面：findBody 命中/未命中空 Ptr（不默认命中）、bodyCount 与
 * getBodies 一致、dynamicWorkCell() const 引用可用；空 Ptr 构造→
 * RobWorkError（§8.4 空句柄行，与 WorkCellConstView 同款）。
 */
TEST(DynamicWorkCellConstView, ViewQueriesAndNullRejection_RT_T08)
{
    const CanonicalModel model = richFixture().build();
    const TwoStageResult r = compileBoth(model);

    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    const DynamicWorkCellConstView view(r.s7.dynamicWorkCell.cast<
        const rwsim::dynamics::DynamicWorkCell>());

    // bodyCount 与基线全体清单一致。
    EXPECT_EQ(view.bodyCount(), r.s7.dynamicWorkCell->getBodies().size());

    // findBody 命中（名＝映射 Body 条目）与未命中（空 Ptr，不默认命中）。
    const std::string bodyName =
        scopedFullName(nameMap, model.chain().links.at(1).objectId, NameScope::Body);
    const rw::core::Ptr<const rwsim::dynamics::Body> hit = view.findBody(bodyName);
    ASSERT_TRUE(!hit.isNull());
    EXPECT_EQ(hit.get(), r.s7.dynamicWorkCell->findBody(bodyName).get());
    EXPECT_TRUE(view.findBody("no/such/body").isNull());

    // const 引用只读消费（物性读数——写路径在类型层不可达）。
    EXPECT_EQ(view.dynamicWorkCell().getBodies().size(), view.bodyCount());

    // 空 Ptr 构造→fail-fast（不产空视图）。以具名变量传入——避免"最令人
    // 困惑解析"把构造表达式解析成函数声明（C++ 句法陷阱）。
    rw::core::Ptr<const rwsim::dynamics::DynamicWorkCell> nullHandle;
    try {
        DynamicWorkCellConstView nullView(nullHandle);
        ADD_FAILURE() << "空句柄应 fail-fast";
    } catch (const RuntimeError& e) {
        EXPECT_EQ(e.code(), RuntimeErrorCode::RobWorkError);
        EXPECT_NE(std::string(e.what()).find("runtime/robwork-null-handle"),
                  std::string::npos)
            << "空句柄诊断应含稳定 token 前缀";
    }
}

}  // namespace
