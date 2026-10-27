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
#include <rwsim/util/RecursiveNewtonEuler.hpp>  // REUSE-T30：上游 RNE 可消费性
                                                // 探针（migration §12.5 许可——
                                                // 严格限原公共 API、仅测试不接
                                                // 生产，生产接线归 WP-07）

#include <rw/kinematics/FixedFrame.hpp>  // 探针注入：承载帧重名（真实 RW_THROW）
#include <rw/kinematics/Frame.hpp>       // 关节帧回溯（设备一致校验的测试复算）
#include <rw/kinematics/Kinematics.hpp>  // REUSE-T30：frameTframe/worldTframe
                                         // （位姿/速度 oracle 读数面）
#include <rw/math/Q.hpp>                 // 摩擦参数读回
#include <rw/math/Rotation3D.hpp>        // REUSE-T30：独立 Rodrigues 复算
#include <rw/math/Transform3D.hpp>       // REUSE-T30：canonical FK 位姿复合
#include <rw/math/VelocityScrew6D.hpp>   // REUSE-T30：体速度读数（角速度 oracle）
#include <rw/models/Joint.hpp>           // WC 侧关节名集合（设备一致）
#include <rw/models/JointDevice.hpp>     // WC 设备关节集
#include <rw/models/WorkCell.hpp>        // WC 查询面

#include "CanonicalModelFixture.hpp"     // minimal/rich 夹具（确定性 id）

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>  // REUSE-T30：Rodrigues 三角函数（确定性 libm——无随机）
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
using sdurws::ird::runtime::testfixture::Fixture;
using sdurws::ird::runtime::testfixture::cvFrom;
using sdurws::ird::runtime::testfixture::diagInertia;
using sdurws::ird::runtime::testfixture::digestOf;
using sdurws::ird::runtime::testfixture::idFrom;
using sdurws::ird::runtime::testfixture::minimalFixture;
using sdurws::ird::runtime::testfixture::richFixture;
using sdurws::ird::runtime::testfixture::userProv;
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
// REUSE-T30（WP-06/T02 动力学映射子任务）：Device/State/固定段复合刚体/
// offset 参考点/RNE 可消费性 oracle——S7 映射定向核断言面。
// 设计依据：migration §13.3 WP-06 行、任务契约 tasks/REUSE-T30.json
// acceptance 2～3；上游语义对照 rwsim RigidDevice.cpp（RigidLink 回溯与
// getSubPart 速度积）、RecursiveNewtonEuler.cpp（solve DOF+1/payload 于
// end frame/重力独立供给）。全部复算不调产品/基线映射实现——oracle 独立。
// =====================================================================

/// REUSE-T30 位姿/数值容差（m·rad——与 RT-EQ-1 同量级，附录 D 第 4 项口径）。
constexpr double kT30Tolerance = 1e-9;

/**
 * @brief 轴角 Rodrigues 旋转（REUSE-T30 独立复算——测试内自含，不与产品
 *        canonicalRodrigues 共享实现：oracle 独立性优先于代码复用）。
 *
 * @param axis  [in] 单位旋转轴（无量纲——canonical 关节权威轴语义）
 * @param theta [in] 转角，rad（右手法则，绕轴正转）
 * @return 3×3 旋转矩阵（行主序构造）
 */
rw::math::Rotation3D<double> t30Rodrigues(const rw::math::Vector3D<double>& axis, double theta)
{
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    const double t = 1.0 - c;
    const double x = axis[0], y = axis[1], z = axis[2];
    return rw::math::Rotation3D<double>(
        t * x * x + c, t * x * y - s * z, t * x * z + s * y,
        t * x * y + s * z, t * y * y + c, t * y * z - s * x,
        t * x * z - s * y, t * y * z + s * x, t * z * z + c);
}

/// REUSE-T30 体速度读数（角速度＝干净的世界系矢量；linear＝上游 getVelocity
/// 读数经旋转回世界系的原始分量——参考点语义未文档化，见测试 2 登记）。
struct T30BodyVelocityWorld
{
    rw::math::Vector3D<double> vAtBodyOrigin{};  ///< getVelocity linear 读数（世界系旋转）
    rw::math::Vector3D<double> w{};              ///< 角速度矢量（世界系表达，rad/s）
};

/**
 * @brief 按 canonical 连杆下标读其编译产物体承载帧的速度（仅旋转回世界系，
 *        不用基线螺旋变换——保持 oracle 独立性；linear 的参考点语义见
 *        DwcFixedSegmentCompositeRigidBody_REUSE_T30 内登记）。
 */
T30BodyVelocityWorld t30WorldVelocityOf(const rwsim::dynamics::DynamicWorkCell& dwc,
                                        const RuntimeNameMap& nameMap,
                                        const CanonicalModel& model, std::size_t linkIndex,
                                        const rw::kinematics::State& state)
{
    T30BodyVelocityWorld out;
    const CanonicalLink& link = model.chain().links.at(linkIndex);
    const rwsim::dynamics::Body::Ptr body =
        dwc.findBody(scopedFullName(nameMap, link.objectId, NameScope::Body));
    if (body.isNull()) {
        ADD_FAILURE() << "REUSE-T30 速度读数：连杆体未命中（index " << linkIndex << "）";
        return out;
    }
    const rw::math::Transform3D<double> wTb =
        rw::kinematics::Kinematics::worldTframe(body->getBodyFrame(), state);
    // RigidLink::getVelocity（回溯关节前缀速度积——angular 纯旋转量参考点
    // 无关；linear 含上游 AD 变换附加项，不作物理体原点速度解释）。
    const rw::math::VelocityScrew6D<> vBody = body->getVelocity(state);
    out.vAtBodyOrigin = wTb.R() * vBody.linear();
    out.w = wTb.R() * (vBody.angular().axis() * vBody.angular().angle());
    return out;
}

/**
 * REUSE-T30 非零 offset 参考点 oracle：j1＝0.4 rad、j2＝−0.25 rad（缺省
 * 夹具 zeroOffset=0 会使期望取值点平凡）。断言面：
 * ①DWC 设备运动学模型与 WC 设备同一对象（无双补偿第二 owner）；
 * ②默认 State（q_rw=0⟺q_auth=zeroOffset，WorkCellCompiler
 * setFixedTransform(getTransform(zeroOffset)) 烘焙——REUSE-T13 语义）下
 * getQ=0、getJointVelocities=0；
 * ③bounds＝权威限位−offset（T13 语义读回）；
 * ④关节锚点位置＋轴线、连杆承载帧全位姿对 canonical 独立 FK 复算逐元素
 * 一致（期望取值点＝q_auth=zeroOffset+q_rw 与 RT-EQ-1 同口径）。
 */
TEST(DynamicWorkCellCompilerTest, DwcDefaultStatePosesAtZeroOffsetReference_REUSE_T30_RT_EQ_1)
{
    Fixture f = richFixture();
    f.chain.joints.at(0).zeroOffset = 0.4;    // rad（Revolute——T13 量纲语义）
    f.chain.joints.at(1).zeroOffset = -0.25;  // rad
    const CanonicalModel model = f.build();
    ASSERT_TRUE(model.capabilities().hasDynamicWorkCell);

    const TwoStageResult r = compileBoth(model);
    ASSERT_EQ(r.s7.status, DwcCompileStatus::Compiled);
    const rwsim::dynamics::DynamicWorkCell& dwc = *r.s7.dynamicWorkCell;
    const rw::kinematics::State state = r.s6.workCell->getDefaultState();  // 任务私有 State

    // ---- ①Device 一致：RigidDevice 运动学模型＝WC 设备（指针同一）。
    ASSERT_EQ(dwc.getDynamicDevices().size(), 1u);
    const rwsim::dynamics::RigidDevice::Ptr rigidDevice =
        dwc.getDynamicDevices().front().cast<rwsim::dynamics::RigidDevice>();
    const rw::core::Ptr<rw::models::JointDevice> wcDevice =
        r.s6.workCell->findDevice(model.chain().deviceName).cast<rw::models::JointDevice>();
    ASSERT_TRUE(!rigidDevice.isNull());
    ASSERT_TRUE(!wcDevice.isNull());
    EXPECT_EQ(rigidDevice->getKinematicModel().get(), wcDevice.get())
        << "DWC 设备应复用 WC 运动学模型（无第二 offset 施加点）";

    // ---- ②State：RobWork 侧从零起算——默认 State q_rw=0；速度接口默认零。
    const rw::math::Q qDefault = wcDevice->getQ(state);
    ASSERT_EQ(qDefault.size(), 2u);
    EXPECT_DOUBLE_EQ(qDefault[0], 0.0) << "默认 State 应为 q_rw=0（q_auth=zeroOffset）";
    EXPECT_DOUBLE_EQ(qDefault[1], 0.0);
    const rw::math::Q velDefault = rigidDevice->getJointVelocities(state);
    ASSERT_EQ(velDefault.size(), 2u);
    EXPECT_DOUBLE_EQ(velDefault[0], 0.0) << "速度接口默认零（无虚假初速）";
    EXPECT_DOUBLE_EQ(velDefault[1], 0.0);

    // ---- ③bounds＝权威限位−zeroOffset（rich 继承 minimal：j1[-2.97,2.97]、
    // j2[-π,π] rad；T13 q_auth 权威限位对应 q_rw 平移语义）。
    const std::vector<rw::models::Joint*>& joints = wcDevice->getJoints();
    ASSERT_EQ(joints.size(), 2u);
    constexpr double kPi = 3.14159265358979323846;
    EXPECT_NEAR(joints.at(0)->getBounds().first[0], -2.97 - 0.4, kT30Tolerance);
    EXPECT_NEAR(joints.at(0)->getBounds().second[0], 2.97 - 0.4, kT30Tolerance);
    EXPECT_NEAR(joints.at(1)->getBounds().first[0], -kPi + 0.25, kT30Tolerance);
    EXPECT_NEAR(joints.at(1)->getBounds().second[0], kPi + 0.25, kT30Tolerance);

    // ---- ④位姿 oracle：默认 State（q_auth=zeroOffset）下 canonical 独立
    // FK 复算（从设备基座逐级复合——安装分量不入面；关节锚点对照位置＋Z
    // 列——Rz(q_auth) 绕 Z 转不动二者；连杆承载帧对照全位姿）。
    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    rw::math::Transform3D<double> canonical = detail::identityTransform3D();
    for (std::size_t i = 0; i < model.chain().joints.size(); ++i) {
        const CanonicalJoint& cj = model.chain().joints.at(i);
        // 权威锚点＝父连杆系·T_parent_joint；权威旋转＝绕权威轴转
        // q_auth＝zeroOffset＋q_rw（q_rw=0——期望取值点）。
        const rw::math::Transform3D<double> anchor = canonical * cj.origin;
        const rw::math::Rotation3D<double> rotAfter =
            t30Rodrigues(cj.axis, cj.zeroOffset + 0.0);

        const rw::kinematics::Frame* const jointFrame = r.s6.workCell->findFrame(
            nameMap.resolveObjectId(cj.objectId).get().fullName);
        ASSERT_NE(jointFrame, nullptr) << "关节帧未命中: index " << i;
        const rw::math::Transform3D<double> rwJointT =
            rw::kinematics::Kinematics::frameTframe(wcDevice->getBase(), jointFrame, state);
        for (int a = 0; a < 3; ++a) {
            EXPECT_NEAR(rwJointT.P()[a], anchor.P()[a], kT30Tolerance)
                << "锚点位置分量 " << a << "（q_auth=zeroOffset 处）";
        }
        // 轴线：WC 关节系 Z 列 vs 权威轴的父链投影（绕轴旋转不动轴自身）。
        const rw::math::Vector3D<double> canonicalAxis = anchor.R() * cj.axis;
        const rw::math::Vector3D<double> rwAxis{
            rwJointT.R()(0, 2), rwJointT.R()(1, 2), rwJointT.R()(2, 2)};
        for (int a = 0; a < 3; ++a) {
            EXPECT_NEAR(rwAxis[a], canonicalAxis[a], kT30Tolerance) << "轴线分量 " << a;
        }
        // 推进 canonical 链：连杆 i+1＝锚点·绕权威轴 q_auth。
        canonical = anchor
            * rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0), rotAfter);
    }

    // 末端连杆体承载帧（MovableFrame）全位姿＝权威法兰系——DWC 挂接不破坏
    // S6 几何/offset 语义（错挂子树或双补偿在旋转矩阵上不可通过）。
    const CanonicalLink& flangeLink = model.chain().links.back();
    const rwsim::dynamics::Body::Ptr flangeBody = dwc.findBody(
        scopedFullName(nameMap, flangeLink.objectId, NameScope::Body));
    ASSERT_TRUE(!flangeBody.isNull());
    const rw::math::Transform3D<double> carrierT = rw::kinematics::Kinematics::frameTframe(
        wcDevice->getBase(), flangeBody->getBodyFrame(), state);
    for (int row = 0; row < 3; ++row) {
        EXPECT_NEAR(carrierT.P()[row], canonical.P()[row], kT30Tolerance)
            << "法兰承载帧位置分量 " << row << "（q_auth=zeroOffset 处）";
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(carrierT.R()(row, col), canonical.R()(row, col), kT30Tolerance)
                << "法兰承载帧旋转 (" << row << "," << col << ")";
        }
    }
}

/**
 * REUSE-T30 固定段复合刚体裁决：夹具链 j1(rev,z)→jF(Fixed)→j2(rev,y)。
 * 断言面：
 * ①设备 DOF＝2（Fixed 关节编译为 FixedFrame 不入设备关节集——S6 语义）；
 * ②逐体回溯关节名多重集＝{j1,j1,j2}（lF 体越过 FixedFrame 命中最近上游
 *   活动关节 j1——std::set 同构盲区的实证：3 项去重后 2 项）；
 * ③同刚体段（l1/lF）：角速度逐元素相等且＝权威轴角速度独立期望 (0,0,1.5)
 *   ——与基线 RigidDevice.cpp getSubPart(0,_jointIdx+1) 速度积语义一致
 *   （固定关节零速度贡献；若固定段下游体错误获得 j2 速度即失败）；线性
 *   分量的参考点语义未文档化（上游 getVelocity 经 AD 变换含体位置相关
 *   附加项——探针定标），不作物理断言、登记归 WP-07 定向核；
 * ④跨段（l2）：角速度＝j1 贡献＋j2 贡献（j2 轴世界方向随 q_auth(0.1) 旋转
 *   ——独立期望，即复合段识别判别面）。
 */
TEST(DynamicWorkCellCompilerTest, DwcFixedSegmentCompositeRigidBody_REUSE_T30)
{
    Fixture f = richFixture();
    // 插入 Fixed 关节 jF（index 1）与对应连杆 lF（index 2）——链
    // base→j1→l1→jF→lF→j2→l2(法兰)。Fixed 无 bounds/maxVel/摩擦（不动关节
    // ——S3 未约束面）；物性齐备（连杆门控）。
    const core::ObjectId jFId = idFrom<core::ObjectId>("jF");
    const core::ObjectId lFId = idFrom<core::ObjectId>("lF");
    {
        ObjectRefEntry e;
        e.objectId = jFId;
        e.contentVersion = cvFrom("jF");
        e.objectTypeToken = "joint";
        e.digest = digestOf("jF-bytes");
        f.header.objectRefs.push_back(e);
        e.objectId = lFId;
        e.contentVersion = cvFrom("lF");
        e.objectTypeToken = "link";
        e.digest = digestOf("lF-bytes");
        f.header.objectRefs.push_back(e);
    }
    CanonicalJoint fixedJoint;
    fixedJoint.objectId = jFId;
    fixedJoint.localName = "joint_fixed";
    fixedJoint.type = JointType::Fixed;
    // S3 对轴统一要求有限非零（MDL-11）——Fixed 不消费轴语义，占位 z 轴。
    fixedJoint.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    // T_parent_joint：右移 0.05 m＋抬升 0.1 m（非退化几何——速度场叉积非零）。
    fixedJoint.origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.05, 0.0, 0.1), detail::identityTransform3D().R());
    f.chain.joints.insert(f.chain.joints.begin() + 1, fixedJoint);
    CanonicalLink fixedLink;
    fixedLink.objectId = lFId;
    fixedLink.localName = "link_fixed";
    fixedLink.mass = val(2.5);  // kg
    fixedLink.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 0.02), userProv());
    fixedLink.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        diagInertia(0.005, 0.006, 0.007), userProv());
    f.chain.links.insert(f.chain.links.begin() + 2, fixedLink);
    // 传动比长度须随关节扩展（§4.3.4——0 或 joints.size()）。
    f.drivetrain.ratioPerJoint.insert(f.drivetrain.ratioPerJoint.begin() + 1, val(1.0));

    const CanonicalModel model = f.build();
    ASSERT_TRUE(model.capabilities().hasDynamicWorkCell);
    ASSERT_EQ(model.chain().joints.size(), 3u);
    ASSERT_EQ(model.chain().links.size(), 4u);

    const TwoStageResult r = compileBoth(model);
    ASSERT_EQ(r.s7.status, DwcCompileStatus::Compiled);
    const rwsim::dynamics::DynamicWorkCell& dwc = *r.s7.dynamicWorkCell;

    ASSERT_EQ(dwc.getDynamicDevices().size(), 1u);
    const rwsim::dynamics::RigidDevice::Ptr rigidDevice =
        dwc.getDynamicDevices().front().cast<rwsim::dynamics::RigidDevice>();
    const rw::core::Ptr<rw::models::JointDevice> wcDevice =
        r.s6.workCell->findDevice(model.chain().deviceName).cast<rw::models::JointDevice>();
    ASSERT_TRUE(!rigidDevice.isNull());
    ASSERT_TRUE(!wcDevice.isNull());

    // ---- ①设备 DOF＝2（Fixed 不入关节集）；移动连杆体 3 个（l1/lF/l2）。
    ASSERT_EQ(wcDevice->getJoints().size(), 2u);
    ASSERT_EQ(rigidDevice->getLinks().size(), 3u);

    // ---- ②逐体回溯多重集：按体名定位（不依赖 getLinks 顺序），lF 体应
    // 越过 FixedFrame 回溯最近上游活动关节 j1；同名去重恰为设备关节集
    // （存在性面——与编译器 §8.5 4c set 校验互证）。
    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    const std::string j1Name =
        nameMap.resolveObjectId(model.chain().joints.at(0).objectId).get().fullName;
    const std::string j2Name =
        nameMap.resolveObjectId(model.chain().joints.at(2).objectId).get().fullName;
    std::vector<std::string> jointsViaBodies;  // 多重集承载（可重复）
    for (std::size_t linkIndex = 1; linkIndex < model.chain().links.size(); ++linkIndex) {
        const CanonicalLink& link = model.chain().links.at(linkIndex);
        const rwsim::dynamics::Body::Ptr body =
            dwc.findBody(scopedFullName(nameMap, link.objectId, NameScope::Body));
        ASSERT_TRUE(!body.isNull()) << "连杆体未命中: index " << linkIndex;
        rw::kinematics::Frame* frame = body->getBodyFrame();
        rw::models::Joint* joint = nullptr;
        while (frame != nullptr && joint == nullptr) {
            joint = dynamic_cast<rw::models::Joint*>(frame);
            frame = frame->getParent();
        }
        ASSERT_NE(joint, nullptr) << "连杆体应回溯到活动关节: index " << linkIndex;
        jointsViaBodies.push_back(joint->getName());
    }
    ASSERT_EQ(jointsViaBodies.size(), 3u);
    EXPECT_EQ(jointsViaBodies.at(0), j1Name) << "l1 体→j1";
    EXPECT_EQ(jointsViaBodies.at(1), j1Name)
        << "lF 体（固定段下游）应回溯最近上游活动关节 j1（复合刚体裁决）";
    EXPECT_EQ(jointsViaBodies.at(2), j2Name) << "l2 体→j2";
    const std::set<std::string> uniqueViaBodies(jointsViaBodies.begin(),
                                                jointsViaBodies.end());
    EXPECT_EQ(uniqueViaBodies.size(), 2u)
        << "多重集 3 项去重 2 项——set 同构盲区的实证（§8.5 4c 为存在性校验）";

    // ---- ③④角速度 oracle：写任务私有 State（q 与 q̇ 任意确定值）。
    rw::kinematics::State state = r.s6.workCell->getDefaultState();
    wcDevice->setQ(rw::math::Q(2, 0.1, -0.2), state);
    rigidDevice->setJointVelocities(rw::math::Q(2, 1.5, -0.8), state);
    const rw::math::Q velRead = rigidDevice->getJointVelocities(state);
    ASSERT_EQ(velRead.size(), 2u);
    EXPECT_DOUBLE_EQ(velRead[0], 1.5) << "速度接口读回（rad/s）";
    EXPECT_DOUBLE_EQ(velRead[1], -0.8);

    const T30BodyVelocityWorld v1 = t30WorldVelocityOf(dwc, nameMap, model, 1, state);
    const T30BodyVelocityWorld vF = t30WorldVelocityOf(dwc, nameMap, model, 2, state);
    const T30BodyVelocityWorld v2 = t30WorldVelocityOf(dwc, nameMap, model, 3, state);

    // 独立期望：j1 权威轴 z、安装/T_parent 恒等 → 角速度 (0,0,q̇1)。
    // 线速度面说明（REUSE-T30 定向核登记）：上游 RigidLink::getVelocity 的
    // linear 分量经 inverse(bTf)·(bJf·vel) 的 AD 变换，参考点语义未文档化
    // （探针定标：linear 读数含与体位置相关的 AD 附加项，非纯体原点速度）
    // ——速度积语义（getSubPart(0,_jointIdx+1) 前缀截断＝固定关节零贡献）
    // 由角速度面完全判别（若固定段下游体错误获得 j2 速度，ω 断言即失败）；
    // 线速度的参考点契约归 WP-07 消费 RNE/速度接口时定向核，本包不把上游
    // 未文档化实现细节钉进断言。
    const rw::math::Vector3D<double> w1Expected(0.0, 0.0, 1.5);
    for (int a = 0; a < 3; ++a) {
        EXPECT_NEAR(v1.w[a], w1Expected[a], kT30Tolerance)
            << "l1 角速度分量 " << a << "（独立期望＝权威轴×q̇1）";
    }
    // 同刚体段：lF 与 l1 角速度逐元素相等（固定关节零速度贡献）。
    for (int a = 0; a < 3; ++a) {
        EXPECT_NEAR(vF.w[a], v1.w[a], kT30Tolerance)
            << "lF 角速度分量 " << a << "（同段应与 l1 相等）";
    }
    // 跨段：l2 角速度＝j1 贡献＋j2 贡献；j2 权威轴 y 的世界方向随
    // q_auth(q1)=0.1 旋转（R_z(0.1)·(0,−0.8,0)——独立期望）。该期望本身即
    // 判别面：若 l2 被错误归入 j1 段（_jointIdx=0）则得 (0,0,1.5)≠期望。
    const rw::math::Vector3D<double> w2Expected =
        w1Expected + t30Rodrigues(rw::math::Vector3D<double>(0, 0, 1), 0.1)
            * rw::math::Vector3D<double>(0.0, -0.8, 0.0);
    for (int a = 0; a < 3; ++a) {
        EXPECT_NEAR(v2.w[a], w2Expected[a], kT30Tolerance)
            << "l2 角速度分量 " << a << "（两关节贡献独立期望——跨段判别面）";
    }
}

/**
 * REUSE-T30 RNE 可消费性（必核项——migration §13.3 WP-06"映射产物可被
 * 消费"边界含载荷＋§12.5 探测许可：严格限原公共 API、仅测试不接生产，
 * 生产接线归 WP-07/WP-09）：以编译产物 RigidDevice 直接构造上游
 * RecursiveNewtonEuler。
 *
 * 夹具＝单 z 轴旋转关节（R_align=I——关节系与权威连杆系重合，上游 RNE
 * 把 masscenter 乘关节帧旋转的消费假设在此面成立）＋物性非退化（质心离
 * 轴：link_1 体 m=4.0、com=(0.1,0.05,0.02)——q=0 时体系＝基座系）。
 * 断言面（全部独立手算 oracle，不调产品/基线实现）：
 * ①validate() 接受编译产物设备（SerialDevice＋Revolute）；
 * ②solve 返回 DOF+1 个 wrench（上游 getJointForces res(DOF+1)——含
 *   payload 槽；参考系/索引/payload 力矩传递语义归 WP-07 深核）；
 * ③静止（dq=ddq=0、payload=0）：RNE 重力显式供给（ctor 默认零——重力
 *   不入映射产物，WP-07 接线须 setGravity 传递）下
 *   wrench[0]＝(F=(0,0,4·9.81), T=(0.05,−0.1,0)·4·9.81)——全分量精确、
 *   沿轴力矩零（重力∥轴）；wrench[1]（payload 槽）＝全零；
 * ④ddq=2.0 rad/s²：惯量项 I·(αẑ)=(0,0,0.03α)＋切向力 m·(αẑ×r) 合成
 *   wrench[0].T=(1.962−0.016, −3.924−0.008, (0.05+0.03)α)——沿轴力矩＝
 *   (I_zz＋m·(rx²+ry²))·α＝0.16 N·m（独立手算全分量精确）；
 * ⑤setPayload 通道可用：读回＋不抛＋尺寸不变；payload 力矩的上游传递
 *   语义（getJointForces 的 cross(dr−com_link,F_p) 项）与直接物理手算的
 *   偏差登记为 WP-07 核验点，本包不断言其数值。
 * 边界登记（GAP——任务契约/README 同步）：权威轴≠z 时上游 RNE 把
 * BodyInfo.masscenter（权威连杆系表达——Body.hpp"body frame"契约与
 * §8.2 canonical 逐字语义）乘以关节帧旋转（差 R_align 对齐旋转）→ 质心
 * 世界位置错位（本包探针实证：j2 轴 y、com 沿 z 0.2 时 τ_x＝±5.886 恰
 * 为 R_align 混入量级，且 Rz(q) 不动沿 z 质心——构型相关项缺失）。修复
 * 须改 S7 BodyInfo 表达系（R_alignᵀ 变换）并连带 §8.2 逐字断言/建模导出
 * 消费者语义变化——属所有者裁决项，本包不自批；受影响消费者＝WP-07
 * （RNE）/WP-09（FD）。工具体（Tcp 帧 FixedBody）不进设备连杆序列——
 * 上游 RNE 只经 RigidDevice ctor＋setPayload 消费，工具物性不自动进入
 * 求解（WP-07 消费边界）。
 */
TEST(DynamicWorkCellCompilerTest, DwcProductConsumableByUpstreamRne_REUSE_T30)
{
    // 单 z 轴关节夹具：minimal 裁掉 j2/l2（含闭包引用——CM-0 对象逐一登
    // 记）后补全物性。base 物性齐备但 RNE 链只含移动连杆（getLinks）。
    Fixture f = minimalFixture();
    f.chain.joints.pop_back();
    f.chain.links.pop_back();
    const core::ObjectId j2Id = idFrom<core::ObjectId>("j2");
    const core::ObjectId l2Id = idFrom<core::ObjectId>("l2");
    std::vector<ObjectRefEntry>& refs = f.header.objectRefs;
    refs.erase(std::remove_if(refs.begin(), refs.end(),
                              [&](const ObjectRefEntry& e) {
                                  return e.objectId == j2Id || e.objectId == l2Id;
                              }),
               refs.end());
    f.chain.links.at(0).mass = val(5.0);  // kg（base 体——承载面，不入 RNE 链）
    f.chain.links.at(0).centerOfMass =
        core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.02, 0.01, 0.05), userProv());
    f.chain.links.at(0).inertia =
        core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
            diagInertia(0.01, 0.02, 0.03), userProv());
    f.chain.links.at(1).mass = val(4.0);  // kg（link_1 体——RNE 唯一移动连杆）
    f.chain.links.at(1).centerOfMass =
        core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.1, 0.05, 0.02), userProv());
    f.chain.links.at(1).inertia =
        core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
            diagInertia(0.01, 0.02, 0.03), userProv());

    const CanonicalModel model = f.build();
    ASSERT_TRUE(model.capabilities().hasDynamicWorkCell);
    ASSERT_EQ(model.chain().joints.size(), 1u);

    const TwoStageResult r = compileBoth(model);
    ASSERT_EQ(r.s7.status, DwcCompileStatus::Compiled);
    ASSERT_EQ(r.s7.dynamicWorkCell->getDynamicDevices().size(), 1u);
    const rwsim::dynamics::RigidDevice::Ptr rigidDevice =
        r.s7.dynamicWorkCell->getDynamicDevices().front().cast<rwsim::dynamics::RigidDevice>();
    ASSERT_TRUE(!rigidDevice.isNull());
    ASSERT_EQ(rigidDevice->getLinks().size(), 1u) << "RNE 链＝移动连杆体（base 不入列）";

    // ---- ①②上游 RNE 直接以编译产物构造——零转换/零包装；重力显式供给。
    rwsim::util::RecursiveNewtonEuler rne(rigidDevice);
    EXPECT_TRUE(rne.validate()) << "上游求解器应接受编译产物设备";
    rne.setGravity(rw::math::Vector3D<>(0.0, 0.0, -9.81));
    rne.setPayload(rw::math::Vector3D<>(0.0, 0.0, 0.0), 0.0, diagInertia(0.0, 0.0, 0.0));

    rw::kinematics::State state = r.s6.workCell->getDefaultState();
    const double kG = 9.81;  // m/s²（与 setGravity 同值——oracle 手算基准）

    // ---- ③静止（dq=ddq=0、payload=0）：全分量精确 oracle。
    const std::vector<rw::math::Wrench6D<>> rest =
        rne.solve(state, rw::math::Q(1, 0.0), rw::math::Q(1, 0.0));
    ASSERT_EQ(rest.size(), 2u) << "DOF+1（含 payload 槽——上游 getJointForces）";
    EXPECT_NEAR(rest.at(0).force()[0], 0.0, kT30Tolerance);
    EXPECT_NEAR(rest.at(0).force()[1], 0.0, kT30Tolerance);
    EXPECT_NEAR(rest.at(0).force()[2], 4.0 * kG, kT30Tolerance) << "支撑力＝m·g";
    EXPECT_NEAR(rest.at(0).torque()[0], 0.05 * 4.0 * kG, kT30Tolerance)
        << "τ_x＝com_y·F_z（cross(com,F) 独立手算）";
    EXPECT_NEAR(rest.at(0).torque()[1], -0.1 * 4.0 * kG, kT30Tolerance)
        << "τ_y＝−com_x·F_z";
    EXPECT_NEAR(rest.at(0).torque()[2], 0.0, kT30Tolerance)
        << "沿轴（z）力矩零——重力∥轴（虚假项判别）";
    for (int a = 0; a < 3; ++a) {
        EXPECT_NEAR(rest.at(1).force()[a], 0.0, kT30Tolerance);
        EXPECT_NEAR(rest.at(1).torque()[a], 0.0, kT30Tolerance);
    }

    // ---- ④ddq=2.0 rad/s²：惯量项＋切向力合成（全分量独立手算）。
    const std::vector<rw::math::Wrench6D<>> accel =
        rne.solve(state, rw::math::Q(1, 0.0), rw::math::Q(1, 2.0));
    ASSERT_EQ(accel.size(), 2u);
    EXPECT_NEAR(accel.at(0).force()[0], -0.4, kT30Tolerance)
        << "切向力 m·(αẑ×r)_x＝−m·α·r_y";
    EXPECT_NEAR(accel.at(0).force()[1], 0.8, kT30Tolerance) << "切向力 y 分量";
    EXPECT_NEAR(accel.at(0).force()[2], 4.0 * kG, kT30Tolerance);
    EXPECT_NEAR(accel.at(0).torque()[0], 0.05 * 4.0 * kG - 0.02 * 0.8, kT30Tolerance);
    EXPECT_NEAR(accel.at(0).torque()[1], -0.1 * 4.0 * kG - 0.02 * 0.4, kT30Tolerance);
    EXPECT_NEAR(accel.at(0).torque()[2], (0.05 + 0.03) * 2.0, kT30Tolerance)
        << "沿轴力矩＝(I_zz＋m·(rx²+ry²))·α＝0.16 N·m（惯量项 I·(αẑ) 的 z 分量"
           "与 cross(com,F)_z 合成——平行轴定理）";

    // ---- ⑤payload 通道：读回＋不抛＋尺寸不变（数值语义归 WP-07 深核）。
    rne.setPayload(rw::math::Vector3D<>(0.02, 0.01, 0.01), 0.5,
                   diagInertia(0.001, 0.001, 0.001));
    EXPECT_DOUBLE_EQ(rne.getPayloadMass(), 0.5) << "payload 读回（通道可用）";
    const std::vector<rw::math::Wrench6D<>> loaded =
        rne.solve(state, rw::math::Q(1, 0.0), rw::math::Q(1, 0.0));
    ASSERT_EQ(loaded.size(), 2u) << "payload 接入后 solve 尺寸不变";
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
