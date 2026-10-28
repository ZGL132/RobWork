/**
 * @file   RneConsumptionDevice.cpp
 * @brief  RNE 可消费一次性设备工厂实现（公共契约面见同名公共头文件头——
 *         裁决脉络/GAP 引用/所有权语义均在头，本文件只补构造细节）。
 *
 * 设计依据（头面引用汇总）：migration §13.3 WP-07（REUSE-T52）；GAP-RNE-
 * FRAME 薄适配（A2 裁决＋2026-10-26 所有者构造面上收裁决——模型构造权归
 * runtime，dynamics 经 dynamics->runtime 登记边消费）；§8.2 canonical 逐字
 * 断言／modeling DWC 导出／S7 产品映射三处零触碰（A2 原约束）。
 *
 * 实现要点：
 *   - 轴对齐旋转与本单元 WorkCellCompiler.cpp 的 axisAlignmentRotation 同源
 *     （REUSE-T13/MDL-11 公式：R·z＝axis、makeLookAt 复用、±z 精确分支）。
 *     本单元内两 TU 各持一份同源私有实现——2026-10-26 裁决的最小授权面
 *     （不触碰既有 S6 翻译单元）；合并为单一私有头归后续卫生批登记。
 *   - 关节变换/尾巴/烘焙与 S6 逐字段同源（tAligned、setFixedTransform(
 *     getTransform(offset))、连杆帧 R_alignᵀ）——权威 FK 无残差（RT-EQ-1
 *     的等价语义面），由消费方 oracle（任意构型解析/能量平衡/黄金数据集）
 *     数值钉住。
 *   - BodyInfo 关节帧承载＝GAP-RNE-FRAME 薄适配本体（头面引用）；缺失
 *     物性 0 填充（InertiaMatrix 默认构造＝零张量——对角构造器缺省值）。
 */

#include <sdurws/ird/runtime/RneConsumptionDevice.hpp>

#include <sdurws/ird/runtime/Errors.hpp> // RuntimeError/InputInvalid（公共边界防御）

#include <rw/kinematics/FixedFrame.hpp>
#include <rw/models/Joint.hpp>
#include <rw/models/PrismaticJoint.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rwsim/dynamics/Body.hpp>
#include <rwsim/dynamics/FixedBody.hpp>
#include <rwsim/dynamics/RigidDevice.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace sdurws { namespace ird { namespace runtime {

namespace {

/**
 * @brief 将局部 Z 轴对齐权威轴的确定性正交旋转（R·z＝axis）。
 *
 * 与本单元 src/WorkCellCompiler.cpp axisAlignmentRotation 同源公式（本单元
 * 内镜像——最小授权面裁决不触碰既有 S6 翻译单元，见文件头实现要点）；
 * REUSE-T13/MDL-11：up 选择与 axis 充分不平行仅固定滚转自由度，精确 ±z
 * 保留冻结形态；前置（axis 单位向量）由公共边界防御复检保证。
 */
rw::math::Rotation3D<double> axisAlignmentRotation(const rw::math::Vector3D<double>& axis)
{
    if (axis[0] == 0.0 && axis[1] == 0.0) {
        return axis[2] > 0.0 ? rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1)
                             : rw::math::Rotation3D<double>(1, 0, 0, 0, -1, 0, 0, 0, -1);
    }
    // 0.9 为内部坐标图选择阈值（无量纲），不是数值验收容差；避开平行 up。
    const rw::math::Vector3D<double> up = std::abs(axis[1]) < 0.9
        ? rw::math::Vector3D<double>(0, 1, 0)
        : rw::math::Vector3D<double>(1, 0, 0);
    return rw::math::Transform3D<double>::makeLookAt(
        rw::math::Vector3D<double>(0, 0, 0), -axis, up).R();
}

}  // namespace

RneConsumptionDevice buildRneConsumptionDevice(const CanonicalModel& model)
{
    const RobotChain& chain = model.chain();
    RneConsumptionDevice out;

    // ---- 公共边界防御（NFR-COR-03——产品路径由消费方前置校验先行，此处
    //      为不可达防御；错误轨 RuntimeError/InputInvalid 就地定位）----
    if (chain.joints.empty()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "stage=RneDevice; 模型链无可动关节（空链）——拒绝构造");
    }

    // ---- 帧树基座：基帧＝基座连杆系（恒等；消费方上游 RNE 以设备基帧为
    //      递推原点；BaseMount 安装分量不入链——重力已由消费方基座系投影）。
    rw::core::Ptr<rw::kinematics::FixedFrame> baseFrame = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ird-rne/base", rw::math::Transform3D<>()));
    out.stateStructure = rw::core::ownedPtr(new rw::kinematics::StateStructure());
    out.stateStructure->addFrame(baseFrame);

    rw::core::Ptr<rw::kinematics::Frame> prevLinkFrame = baseFrame;
    const std::size_t n = chain.joints.size();
    out.joints.reserve(n);
    out.prismatic.reserve(n);
    out.zeroOffsets.reserve(n);
    std::vector<rw::core::Ptr<rw::kinematics::Frame>> linkFrames;  // 设备链视图（tree 持所有权）
    linkFrames.reserve(n);
    std::vector<std::pair<rwsim::dynamics::BodyInfo, rw::models::Object::Ptr>>
        linkBodies;  // RigidDevice 构造输入（链序＝确定序；S7 同款 pair 形态）
    linkBodies.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        const CanonicalJoint& cj = chain.joints.at(i);

        // 词表与几何防御（头面 @throws 契约；Fixed＝RNE 面不可构造类型）。
        if (cj.type == JointType::Fixed) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "stage=RneDevice; 模型链携带 Fixed 关节（关节序 "
                                   + std::to_string(i) + "）——RNE 载体仅支持三个可动类型");
        }
        const double axisNorm = cj.axis.norm2();
        if (!std::isfinite(axisNorm) || std::abs(axisNorm - 1.0) > 1e-9) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "stage=RneDevice; 关节轴向非单位向量（‖axis‖="
                                   + std::to_string(axisNorm) + "，关节序 "
                                   + std::to_string(i) + "）——编译产物应已规格化");
        }
        if (!std::isfinite(cj.zeroOffset)) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "stage=RneDevice; 关节零位偏置非有限（关节序 "
                                   + std::to_string(i) + "）");
        }

        // ---- 关节帧：tAligned＝origin·T(0, R_align)；zeroOffset 烘焙
        //      （S6 WorkCellCompiler.cpp:650-666 同源——q_rw=0 的静态结果进
        //      fixed transform，权威角经 q_auth＝offset+q_rw 保持）。
        const rw::math::Rotation3D<double> rAlign = axisAlignmentRotation(cj.axis);
        const rw::math::Transform3D<double> tAligned =
            cj.origin
            * rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0), rAlign);
        const std::string jointFrameName = "ird-rne/joint" + std::to_string(i);
        rw::core::Ptr<rw::models::Joint> joint;
        const bool prismatic = (cj.type == JointType::Prismatic);
        if (prismatic) {
            rw::core::Ptr<rw::models::PrismaticJoint> moving = rw::core::ownedPtr(
                new rw::models::PrismaticJoint(jointFrameName, tAligned));
            moving->setFixedTransform(moving->getTransform(cj.zeroOffset));  // offset 烘焙
            joint = moving;
        } else {
            // Revolute/Continuous 共用上游旋转关节（Continuous 的无限位由
            // canonical 无 bounds 语义承载，不引入第二模型 owner）。
            rw::core::Ptr<rw::models::RevoluteJoint> rotating = rw::core::ownedPtr(
                new rw::models::RevoluteJoint(jointFrameName, tAligned));
            rotating->setFixedTransform(rotating->getTransform(cj.zeroOffset));  // offset 烘焙
            joint = rotating;
        }
        out.stateStructure->addFrame(joint, prevLinkFrame);

        // ---- 连杆帧：尾巴补偿 R_alignᵀ（S6 WorkCellCompiler.cpp:715-726
        //      同源——连杆系精确等于权威 FK）。
        const rw::math::Rotation3D<double> rAlignT = rw::math::inverse(rAlign);
        const auto linkOffset = rw::math::Transform3D<>(rw::math::Vector3D<double>(0, 0, 0), rAlignT)
            * (i + 1 == chain.joints.size() ? chain.flangeOffset : rw::math::Transform3D<>::identity());
        rw::core::Ptr<rw::kinematics::FixedFrame> linkFrame = rw::core::ownedPtr(
            new rw::kinematics::FixedFrame("ird-rne/link" + std::to_string(i + 1),
                                           linkOffset));
        out.stateStructure->addFrame(linkFrame, joint);
        prevLinkFrame = linkFrame;
        linkFrames.push_back(linkFrame);

        // ---- 驱动体 BodyInfo：**关节帧**承载（GAP-RNE-FRAME 薄适配本体）。
        //      物性缺失按 0/保守值填充（InertiaMatrix 默认构造＝零张量——
        //      对角构造器缺省值；WP-17-T05"缺失＝零贡献"语义保留）。
        const CanonicalLink& link = chain.links.at(i + 1);
        const double mass = link.mass.tryValue().value_or(0.0);  // 单位 kg
        const rw::math::Vector3D<double> com = link.centerOfMass.tryValue().value_or(
            rw::math::Vector3D<double>(0, 0, 0));                // 连杆系下，单位 m
        const rw::math::InertiaMatrix<double> inertia = link.inertia.tryValue().value_or(
            rw::math::InertiaMatrix<double>());                  // 质心系，kg·m²
        rwsim::dynamics::BodyInfo info;
        info.mass = mass;
        info.masscenter = linkOffset * com;            // 权威连杆系→关节帧（薄适配）
        info.inertia = linkOffset.R() * inertia * rw::math::inverse(linkOffset.R());  // 张量相似变换（R·I·Rᵀ 形态）
        rw::core::Ptr<rw::models::RigidObject> object = rw::core::ownedPtr(
            new rw::models::RigidObject(linkFrame));  // 持有副本——引用计数共享
        linkBodies.emplace_back(info, object);

        out.joints.push_back(joint.get());
        out.prismatic.push_back(prismatic);
        out.zeroOffsets.push_back(cj.zeroOffset);
    }

    // ---- 法兰基线（末关节驱动体＝links[n] 原始物性——§5.4 变体合成的
    //      只读基项，法兰系表达；换算基＝末关节 R_alignᵀ，与连杆帧尾巴同源）。
    {
        const CanonicalLink& flange = chain.links.back();
        out.flangeBaseMass = flange.mass.tryValue().value_or(0.0);
        out.flangeBaseCom = flange.centerOfMass.tryValue().value_or(
            rw::math::Vector3D<double>(0, 0, 0));
        out.flangeBaseInertia = flange.inertia.tryValue().value_or(
            rw::math::InertiaMatrix<double>());
        out.flangeAlignInverse = axisAlignmentRotation(chain.joints.back().axis);
        out.flangeAlignInverse.inverse();  // 正交阵逆＝转置（基线 inverse() 原地语义）
        out.flangeTranslation = out.flangeAlignInverse * chain.flangeOffset.P();
        out.flangeAlignInverse = out.flangeAlignInverse * chain.flangeOffset.R();
    }

    // ---- 设备整体构造：基座体 FixedBody 零物性占位（links[0] 固连基座、
    //      物性不产生任何关节力矩故不入递推——消费方体号约定）；RigidLink
    //      经承载帧回溯定位驱动关节（findParentFrom<Joint>——连杆帧直挂关
    //      节帧下即回溯成立的结构前提）；上游 RNE 的 validate（SerialDevice
    //      ＋关节词表）由本构造面保证。
    rw::core::Ptr<rw::models::RigidObject> baseObject = rw::core::ownedPtr(
        new rw::models::RigidObject(baseFrame));  // 持有副本——引用计数共享
    rwsim::dynamics::Body::Ptr baseBody = rw::core::ownedPtr(
        new rwsim::dynamics::FixedBody(rwsim::dynamics::BodyInfo(), baseObject));

    std::vector<rw::kinematics::Frame*> serialChain;
    serialChain.push_back(baseFrame.get());
    for (std::size_t i = 0; i < n; ++i) {
        serialChain.push_back(out.joints[i]);
        serialChain.push_back(linkFrames[i].get());
    }

    out.state = out.stateStructure->getDefaultState();
    out.device = rw::core::ownedPtr(
        new rw::models::SerialDevice(serialChain, "ird-rne/device", out.state));
    out.rigidDevice = rw::core::ownedPtr(
        new rwsim::dynamics::RigidDevice(baseBody, linkBodies, out.device));
    return out;
}

}}}  // namespace sdurws::ird::runtime
