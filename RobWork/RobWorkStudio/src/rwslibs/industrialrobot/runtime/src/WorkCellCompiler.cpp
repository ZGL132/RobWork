/**
 * @file   WorkCellCompiler.cpp
 * @brief  S6 WorkCell 编译器实现——CanonicalModel → rw::models::WorkCell
 *         的确定性变换（Frame 树/SerialDevice/Joint/限位/BaseMount 唯一
 *         写入）＋WorkCellConstView 实现＋RobWork 异常转译。
 *
 * 设计依据：
 *   - units/runtime.md §5.2 S6 行、§5.3（失败回滚——RAII 瞬态对象）、§6.3
 *     （BaseMount 唯一写入点）、§7.2（写入 WC 的名字与映射逐一相等）、
 *     §8.1（P-RT-3 已登记 L1 依赖：rw kinematics/models）、§8.2（所有权与
 *     只读包装——可变句柄仅本文件可达）、§8.3（WorkCellConstView）、§8.4
 *     （异常→稳定诊断转译——不吞异常）
 *   - 需求 ARC-03、MDL-06（原子性：任一失败不发布半成品）、MDL-14、
 *     NFR-COR-01/02（确定性可复现）、NFR-REL-05（用户诊断不含调用栈）
 *   - 任务契约 tasks/foundation/RT-T07.json（acceptance 1～3）
 *
 * ★ 编译模式说明：本文件调用大量基线库非模板符号（WorkCell/StateStructure/
 * SerialDevice/RevoluteJoint 的构造与查询——实现于 sdurw_kinematics/
 * sdurw_models 的 .cpp），**只在集成模式编译**（CMakeLists 按 TARGET
 * sdurw_kinematics 条件增列；冒烟模式无框架库可链，本文件不进构建——
 * §5.1 冒烟口径"仅验证目标注册与 include 路径"，理由已在 CMake 增列块与
 * units/runtime.md §15.4 登记）。
 *
 * 线程安全：compileWorkCell 无共享可变状态（每次调用全新对象树——§5.5
 * 可重入）；WorkCellConstView 发布后只读；translateRobWorkError 纯函数。
 * 单位纪律：全部长度单位 m、角度单位 rad、时间 s、质量 kg（SI 真值——
 * §4.4；CanonicalModel 已保证，本文件不做换算，只做结构映射）。
 * 坐标系读法：T_ab＝"b 系相对 a 系"（core §4.6）——与基线 Frame 语义一致
 * （Frame::getTransform 返回相对父系的位姿）。
 */

#include "WorkCellCompiler.hpp"

#include <sdurws/ird/runtime/Adapter.hpp>
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // checkWorldBaseTransform/
                                                      // checkBaseMountConsistency/
                                                      // rotationFromCustomEaa（S6 复用）
#include <sdurws/ird/runtime/Errors.hpp>              // token/registryCode/RuntimeError
#include <sdurws/ird/runtime/NameMap.hpp>             // buildRuntimeNameMap（名称唯一源）

#include <rw/core/Exception.hpp>          // rw::core::Exception（§8.4 转译行 1）
#include <rw/kinematics/FixedFrame.hpp>   // 基座/连杆/TCP/场景帧
#include <rw/models/Joint.hpp>            // bounds/限速/限加速度显式设值
#include <rw/models/PrismaticJoint.hpp>   // 移动关节
#include <rw/models/RevoluteJoint.hpp>    // 旋转/连续关节（连续＝无限位旋转）
#include <rw/models/SerialDevice.hpp>     // 设备构造（基座→法兰链）
#include <rw/models/WorkCell.hpp>         // 编译产物

#include <algorithm>
#include <exception>
#include <new>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::runtime {
namespace {

// =====================================================================
// 局部常量与工具（匿名命名空间——翻译单元内可见，不外泄符号）。
// =====================================================================

/// 极轴判定阈值（无量纲）：axis 与 ±Z 夹角余弦接近 ±1 时走精确分支。
/// 取 1×10⁻¹²（与附录 D 第 6 项对称性容差同尺度）——该阈值下一般分支的
/// sinθ ≥ √(2×10⁻¹²) ≈ 1.4×10⁻⁶，Rodrigues 归一化数值稳定，无奇异风险。
constexpr double kPoleCosTolerance = 1e-12;

/// 转译消息摘要的展示长度上限（字符）：RW 异常消息可能极长（含内部状态
/// 转储），诊断载体按 NFR-REL-05 只保留摘要——超限截断并显式标注（截断
/// 只影响展示长度，不影响失败判定；完整原文属开发诊断面，走日志不走
/// 诊断记录）。
constexpr std::size_t kCauseSummaryLimit = 512;

/**
 * @brief 构造把局部 Z 轴 (0,0,1) 旋转到权威关节轴 axis 的对齐旋转矩阵
 *        （确定性纯函数——同 axis 同矩阵，无环境依赖）。
 *
 * 数学：这是"任意轴关节映射到基线 Z 轴关节"的关键部件——基线
 * RevoluteJoint/PrismaticJoint 固定绕局部 Z 旋转，权威轴 axis（关节系内
 * 单位向量，MDL-09/MDL-11）经 R_align·Z＝axis 对齐后，关节旋转的物理轴
 * 与权威一致。
 *
 * 分支（按 dot(Z,axis)＝cosθ 确定性选择，避免 Rodrigues 在极轴附近的
 * 归一化奇异）：
 *   - cosθ ＞ 1−1e-12（axis≈+Z）：单位阵——已对齐，零开销；
 *   - cosθ ＜ −(1−1e-12)（axis≈−Z）：绕 X 转 π（diag(1,−1,−1)——与 P-RT-4
 *     倒挂冻结矩阵同值；任一把 −Z 映到 +Z 的正交阵皆可，取确定性固定
 *     形态，不引入三角函数舍入）；
 *   - 一般情形：EAA（轴角矢量） Rodrigues——轴＝Z×axis（右手最小旋转
 *     轴），角＝atan2(sin, cos)。复用 BaseWorldTransform::rotationFromCustomEaa
 *     （RT-T06 已验证的换算单点——不重复实现同一数学）。
 *
 * @param axis [in] 权威关节轴（关节系内；前置：单位向量——builder 规格化
 *             保证，非单位输入属调用方违约，不做复核）
 * @return 正交旋转矩阵 R_align（R_align·(0,0,1)＝axis；det＝+1）
 */
rw::math::Rotation3D<double> axisAlignmentRotation(const rw::math::Vector3D<double>& axis)
{
    const rw::math::Vector3D<double> zAxis(0.0, 0.0, 1.0);  // 基线关节的固定旋转轴（无量纲）
    const double cosAngle = dot(zAxis, axis);               // ＝cosθ（axis 单位化后的内积）

    if (cosAngle > 1.0 - kPoleCosTolerance) {
        // axis 与 +Z 同向：恒等对齐（精确阵，无舍入）。
        return rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                            0.0, 1.0, 0.0,
                                            0.0, 0.0, 1.0);
    }
    if (cosAngle < -(1.0 - kPoleCosTolerance)) {
        // axis 与 −Z 同向：绕 X 转 π（元素全为 {0,±1}——确定性固定形态）。
        return rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                            0.0, -1.0, 0.0,
                                            0.0, 0.0, -1.0);
    }
    // 一般情形：右手最小旋转（轴＝Z×axis，角＝θ）。cross 为 rw 模板函数
    // （header-only 实例化）；norm2 为 Vector3D 成员（长度）；atan2 保证
    // θ∈(0,π]，与极轴分支互斥。
    const rw::math::Vector3D<double> rotAxis = cross(zAxis, axis);
    const double sinAngle = rotAxis.norm2();                // ＝sinθ（axis 单位向量）
    const double angle = std::atan2(sinAngle, cosAngle);    // 单位 rad
    return rotationFromCustomEaa(rotAxis * (angle / sinAngle));
}

/**
 * @brief 计算基线关节的静态变换 T_static（§5.2 S6 任意轴映射的枢纽）。
 *
 * 定义：T_static ＝ T_parent_joint · R_axis(zeroOffset) · R_align。
 * 基线关节瞬时变换为 getTransform(q_rw)＝T_static·Rz(q_rw)（RevoluteJoint
 * Basic 实现实测：parent·(_transform·Rz(q))——SerialDevice FK 复合后）；
 * 配合子连杆帧上的补偿旋转 R_alignᵀ（见 compileWorkCell 主流程），链上
 * 连杆系满足 F(link_i)＝F(link_{i-1})·T_parent_joint·R_axis(offset+q_rw)
 * ＝权威 FK（q_authoritative＝zeroOffset＋q_rw，CanonicalJoint::zeroOffset
 * 注释的口径）。
 *
 * @param tParentJoint [in] 权威 T_parent_joint（父连杆系→关节系；m/rad）
 * @param axis         [in] 权威关节轴（关节系内单位向量）
 * @param zeroOffset   [in] 零位偏置（单位 rad〔移动关节 m〕；RobWork 侧
 *                     q_rw＝q_authoritative−offset）
 * @return 基线 Joint 构造用静态变换（平移与 T_parent_joint 相同——纯旋转
 *         不动原点）
 */
rw::math::Transform3D<double> jointStaticTransform(
    const rw::math::Transform3D<double>& tParentJoint,
    const rw::math::Vector3D<double>& axis,
    double zeroOffset)
{
    // R_axis(offset)：绕权威轴转零位偏置（EAA＝轴角矢量——复用 RT-T06
    // 换算单点）。offset＝0 时 Rodrigues 精确返回单位阵（sin 0＝cos 1）。
    const rw::math::Rotation3D<double> rZeroOffset = rotationFromCustomEaa(axis * zeroOffset);
    // R_align：Z 轴对齐权威轴（物理旋转轴由此确立）。
    const rw::math::Rotation3D<double> rAlign = axisAlignmentRotation(axis);
    // 平移取 T_parent_joint 原值（两旋转均为纯旋转，不动关节原点）。
    return rw::math::Transform3D<double>(tParentJoint.P(), tParentJoint.R() * rZeroOffset * rAlign);
}

/**
 * @brief 从映射取"某对象某范围"条目的消歧后全名（编译器命名唯一来源）。
 *
 * 背景（§7.2/PA-1）：编译器写入 WC 的每个名字都必须来自映射（不允许
 * 现场拼装——R-4 前缀拼装唯一合法位置在 NameMap 模块）。身份条目经
 * resolveObjectId 直取；BaseMount/BaseFrame/Flange/Tcp/SceneObject 等
 * 派生条目共享所属对象的 ObjectId，须经 entries() 按 (objectId, scope)
 * 定位——本函数即该定位的唯一封装（编译器内部三处调用的公共点）。
 *
 * @param map    [in] buildRuntimeNameMap 产物（只读）
 * @param id     [in] 所属对象身份（robot/joint/link/tool/scene）
 * @param scope  [in] 目标范围（派生条目的语义轴）
 * @return 该 (对象, 范围) 组合的消歧后全名（映射内 fullName 全局唯一）
 *
 * @throws RuntimeError 码＝StructureInvalid：条目不存在（映射生成规则保证
 *         每组合恰一条——不可达，属编译器内部不变量破坏的防御性 fail-fast，
 *         不得以空名继续——NFR-COR-03 不静默）
 */
std::string scopedFullName(const RuntimeNameMap& map, const core::ObjectId& id, NameScope scope)
{
    // 线性扫描条目集（条目量＝链长×2＋工具＋场景级别，O(n) 足够；映射的
    // 有序索引按 fullName 键组织，不支持 (objectId, scope) 复合键直查）。
    for (const RuntimeNameMap::Entry& entry : map.entries()) {
        if (entry.objectId == id && entry.scope == scope) {
            return entry.fullName;  // 消歧后全名（WC 帧名逐字节使用）
        }
    }
    throw RuntimeError(RuntimeErrorCode::StructureInvalid,
                       "runtime/workcell-compiler: 名称映射缺少必需要素（scope="
                           + std::to_string(static_cast<int>(scope)) + "）——内部不变量破坏");
}

/**
 * @brief 有限性复核（防御面）：把 optional<RuntimeError> 中的校验错误转为
 *        fail-fast 抛出（checkWorldBaseTransform 等非抛出规则函数的统一
 *        消费点——错误信息原样透传，不吞不改）。
 */
void throwIfError(const std::optional<RuntimeError>& error)
{
    if (error.has_value()) {
        throw *error;  // RuntimeError 按值重抛（稳定码＋detail 完整保留）
    }
}

}  // namespace

// =====================================================================
// compileWorkCell——S6 唯一入口（签名契约见私有头 WorkCellCompiler.hpp）。
// =====================================================================

WorkCellCompileOutcome compileWorkCell(const CanonicalModel& model)
{
    // ---------------------------------------------------------------
    // 异常边界（§8.4）：编译体全程在 try 内——RobWork 基线异常、内存不足、
    // 其他标准异常统一经 translateRobWorkError 转译后 fail-fast。顺序约束：
    //   1) RuntimeError 最先——它已是本单元稳定失败语义载体（十段链各段的
    //      错误码面），透传即可，绝不能被误转译成 wc-compile-failed（否则
    //      稳定码被覆盖，错误归属失真）；
    //   2) std::bad_alloc→ResourceBudget（§5.5"内存不足"转译行）；
    //   3) rw::core::Exception→WorkCellCompileFailed（§8.4 表行 1；
    //      RT-AD-1 断言的稳定码通道）；
    //   4) 其余 std::exception→WorkCellCompileFailed（编译硬失败兜底——
    //      S6 内部不应有别的异常源，出现即编译失败，不吞不续）。
    // 转译不吞异常（§8.4"适配层不吞异常"）：catch 后一律 throw RuntimeError
    // 终止编译，WC 及全部瞬态帧经局部对象析构自动释放（MDL-06 原子性——
    // WorkCell 析构联动 StateStructure 释放全部 Frame/Joint/Device）。
    // ---------------------------------------------------------------
    try {
        // ===== 第 1 步：名称源唯一化（§7.2——写入 WC 的名字与映射逐一相等）。
        // 映射是纯函数（同 model 同映射），S8 交叉校验也以它为基准——编译器
        // 不得自行拼名（R-4），消歧后的名字才能保证 WC 单一命名空间无冲突。
        const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
        const RobotChain& chain = model.chain();

        // 设备名：身份条目（Device 作用域）＝机器人对象的全名本身（无前缀
        // ——§7.3"Device 作用域例外"；resolveObjectId 返回身份条目）。
        const RuntimeName deviceName = nameMap.resolveObjectId(chain.robotObjectId).get();

        // ===== 第 2 步：入口防御复核（§5.2 S6 失败条件"T_world_base 非法
        // 旋转→InputInvalid"）。S5 构造不变量已拒绝非法旋转，本检查是对
        // S5 产物的防御性复核（不信任上游的编译器自防御——六段之后任何
        // 阶段都不应把非法变换写进 WC）。
        const rw::math::Transform3D<double> tWorldBase = model.world().T_world_base;
        throwIfError(checkWorldBaseTransform(tWorldBase));

        // ===== 第 3 步：WorkCell 本体与 BaseMount 唯一写入（§6.3）。
        // WC 名＝设备名（纯标签——WC 名不入任何身份/缓存键，§8.2）。
        // WorkCell(name) 自建 StateStructure；全部帧经 wc->addFrame 挂接
        // （基线要求 frame 为共享 Ptr——StateStructure 接管所有权）。
        rw::core::Ptr<rw::models::WorkCell> workCell =
            rw::core::ownedPtr(new rw::models::WorkCell(deviceName.fullName));
        rw::kinematics::Frame* const worldFrame = workCell->getWorldFrame();

        // ★ BaseMount 唯一写入点（§6.3 原文）：世界帧与设备基座之间的单个
        // FixedFrame，transform 一次性置为 T_world_base，此后编译器任何阶段
        // 不再改写；其余一切帧变换均不含安装分量（S9 自检兜底）。
        const std::string baseMountName =
            scopedFullName(nameMap, chain.robotObjectId, NameScope::BaseMount);
        rw::core::Ptr<rw::kinematics::FixedFrame> baseMount =
            rw::core::ownedPtr(new rw::kinematics::FixedFrame(baseMountName, tWorldBase));
        workCell->addFrame(baseMount, rw::core::Ptr<rw::kinematics::Frame>(worldFrame));

        // ===== 第 4 步：基座连杆帧（link[0]→BaseFrame 作用域）。
        // 变换＝恒等：BaseMount 已携带全部安装分量，基座连杆系与安装面重合
        // ——"其他一切 Frame 变换均来自关节链/工具偏置/场景位姿，不含安装
        // 分量"（§6.3；RT-BW-4 的结构断言面）。
        const std::string baseFrameName =
            scopedFullName(nameMap, chain.links.front().objectId, NameScope::BaseFrame);
        rw::core::Ptr<rw::kinematics::FixedFrame> previousFrame =
            rw::core::ownedPtr(new rw::kinematics::FixedFrame(
                baseFrameName, detail::identityTransform3D()));
        workCell->addFrame(previousFrame, baseMount);

        // 设备链（SerialDevice 构造输入——基座到法兰的连通 Frame 序列；
        // 裸指针视图，所有权仍在 StateStructure）。
        std::vector<rw::kinematics::Frame*> serialChain;
        serialChain.push_back(previousFrame.get());

        // ===== 第 5 步：逐关节编译（显式设值——§8.2"不依赖 RobWork 构造
        // 默认"；RobWork Joint 构造默认 bounds＝±DBL_MAX、限速/限加速度＝1，
        // 全部被下方显式覆写，RT-AD-2 逐字段对照锁定）。
        const std::size_t jointCount = chain.joints.size();
        for (std::size_t i = 0; i < jointCount; ++i) {
            const CanonicalJoint& joint = chain.joints.at(i);
            // 关节 Frame 名：Joint 作用域身份条目（消歧后全名）。
            const std::string jointFrameName =
                nameMap.resolveObjectId(joint.objectId).get().fullName;

            rw::core::Ptr<rw::kinematics::Frame> jointFrame;
            if (joint.type == JointType::Fixed) {
                // 固定关节：刚性连接，无旋转自由度——基线侧用 FixedFrame
                // 表达（dof＝0，不出现在设备活动关节集）。T_parent_joint
                // 原样挂接；zeroOffset 对无轴关节无意义，不消费（其合法性
                // 边界归 S3 校验器口径，本层不扩大拒绝面）。
                jointFrame = rw::core::ownedPtr(
                    new rw::kinematics::FixedFrame(jointFrameName, joint.origin));
            } else {
                // 旋转/连续/移动关节：静态变换＝T_parent_joint·R_axis(offset)
                // ·R_align（任意轴映射推导见 jointStaticTransform 注释）。
                const rw::math::Transform3D<double> tStatic =
                    jointStaticTransform(joint.origin, joint.axis, joint.zeroOffset);
                if (joint.type == JointType::Prismatic) {
                    jointFrame = rw::core::ownedPtr(
                        new rw::models::PrismaticJoint(jointFrameName, tStatic));
                } else {
                    // Revolute 与 Continuous 同用基线 RevoluteJoint（绕对齐
                    // 后的 Z 旋转）：连续关节＝无限位旋转（基线无独立
                    // continuous 类型；bounds 下方显式置无限——是显式语义
                    // 表达，非构造默认残留）。
                    jointFrame = rw::core::ownedPtr(
                        new rw::models::RevoluteJoint(jointFrameName, tStatic));
                }

                // ---- 逐字段显式设值（RT-AD-2 的编译器侧落点）----
                // 基线 Joint 的运动字段载体是 1 维 Q 向量。
                rw::models::Joint* const jointHandle =
                    dynamic_cast<rw::models::Joint*>(jointFrame.get());
                // 限位：权威 [qmin,qmax] 对应 q_rw＝q_auth−offset（RobWork
                // q 从零起算——zeroOffset 语义），故 WC 侧 bounds 平移 −offset；
                // Continuous（规范侧必无 bounds——S3 保证）或限位未提供时
                // 显式置 ±inf（与基线构造默认 ±DBL_MAX 可区分——证明设值
                // 路径执行，而非默认残留）。
                const double inf = std::numeric_limits<double>::infinity();
                double qLower = -inf;
                double qUpper = inf;
                if (joint.bounds.has_value()) {
                    qLower = joint.bounds->lower - joint.zeroOffset;  // 单位随关节量纲（rad/m）
                    qUpper = joint.bounds->upper - joint.zeroOffset;
                }
                jointHandle->setBounds(rw::math::Q(1, qLower), rw::math::Q(1, qUpper));

                // 最大速度：Provided→权威值（rad/s 或 m/s）；NotProvided→
                // 显式 +inf（基线默认 1 被覆写——能力缺失语义在 capability
                // 声明，WC 侧不伪装成有限速度，§5.6 正交表）。
                const double maxVel = joint.maxVelocity.tryValue().value_or(inf);
                jointHandle->setMaxVelocity(rw::math::Q(1, maxVel));
                // 最大加速度：同上（rad/s² 或 m/s²）。
                const double maxAcc = joint.maxAcceleration.tryValue().value_or(inf);
                jointHandle->setMaxAcceleration(rw::math::Q(1, maxAcc));
            }

            // 关节帧挂到上一级连杆帧（i＝0 时为 BaseFrame——安装分量已在
            // BaseMount，链内不再出现安装旋转）。
            workCell->addFrame(jointFrame, previousFrame);
            serialChain.push_back(jointFrame.get());

            // ---- 关节旋转后的连杆帧（link[i+1]）----
            // ★ 尾巴补偿（任意轴映射的关键半边）：非 Fixed 关节的子连杆帧
            // 放置补偿旋转 R_alignᵀ——由共轭恒等式
            //   R_align·Rz(q_rw)·R_alignᵀ ＝ R_axis(q_rw)
            // 关节帧上的固定尾巴与旋转被一并消去，使连杆系精确等于权威
            // FK（含法兰系无残差——RT-EQ-1 的等价断言面）。
            // 末连杆判定：关节 i 的子连杆下标 i+1，等于末下标 links.size()−1
            // 即 i+2==links.size()（off-by-one 直查——Flange 作用域只挂末连杆）。
            const bool isLastLink = (i + 2 == chain.links.size());
            const std::string linkFrameName = isLastLink
                ? scopedFullName(nameMap, chain.links.at(i + 1).objectId, NameScope::Flange)
                : scopedFullName(nameMap, chain.links.at(i + 1).objectId, NameScope::LinkFrame);

            rw::math::Transform3D<double> linkStatic = detail::identityTransform3D();
            if (joint.type != JointType::Fixed) {
                // 补偿旋转＝R_alignᵀ（R_align 与本关节 static 中的一致）；
                // Fixed 关节无尾巴，恒等即可。基线 inverse() 为原地转置
                // （非 const 成员——RT-T06 已登记），先拷贝再转置。
                const rw::math::Rotation3D<double> rAlign =
                    axisAlignmentRotation(joint.axis);
                rw::math::Rotation3D<double> rAlignT = rAlign;
                rAlignT.inverse();
                linkStatic = rw::math::Transform3D<double>(
                    rw::math::Vector3D<double>(0.0, 0.0, 0.0), rAlignT);
            }
            rw::core::Ptr<rw::kinematics::FixedFrame> linkFrame =
                rw::core::ownedPtr(new rw::kinematics::FixedFrame(linkFrameName, linkStatic));
            workCell->addFrame(linkFrame, jointFrame);
            serialChain.push_back(linkFrame.get());
            previousFrame = linkFrame;  // 下一级关节的父＝本连杆帧
        }

        // ===== 第 6 步：工具 TCP 帧（§4.3.4/MDL-13——挂法兰，tcpOffset 为
        // 法兰系内权威偏置 T_flange_tcp；多工具各自成帧，默认 TCP 的选择
        // 是 kinematics 侧消费语义〔KIN-14〕，编译层不排序不裁剪）。
        // 几何资源（visual/collision mesh）的 WC 挂接随资源消费任务落位
        // （IRuntimeResourceProvider 阶段 B 接入——§8.6；§12 RT-T07 交付
        // 范围为 Frame 树/Device/Joint/限位，几何挂接已登记 §15.4）。
        for (const CanonicalTool& tool : model.tools()) {
            const std::string tcpName =
                scopedFullName(nameMap, tool.objectId, NameScope::Tcp);
            rw::core::Ptr<rw::kinematics::FixedFrame> tcpFrame =
                rw::core::ownedPtr(new rw::kinematics::FixedFrame(tcpName, tool.tcpOffset));
            workCell->addFrame(tcpFrame, previousFrame);  // previousFrame＝法兰帧
        }

        // ===== 第 7 步：场景对象帧（§4.3.4/MDL-15——挂 WORLD 根，世界系
        // 固连位姿原样写入；★ 禁止清单 3〔§6.4〕：环境几何不随安装姿态
        // 旋转——不预乘 T_world_base，RT-BW-4 结构断言面之一）。
        for (const CanonicalSceneObject& sceneObject : model.scene()) {
            const std::string sceneName =
                scopedFullName(nameMap, sceneObject.objectId, NameScope::SceneObject);
            rw::core::Ptr<rw::kinematics::FixedFrame> sceneFrame =
                rw::core::ownedPtr(
                    new rw::kinematics::FixedFrame(sceneName, sceneObject.worldPose));
            workCell->addFrame(sceneFrame, rw::core::Ptr<rw::kinematics::Frame>(worldFrame));
        }

        // ===== 第 8 步：SerialDevice 构造与登记（§5.2 S6 输出"Device 名"
        // ——映射消歧名；链＝BaseFrame→[Joint_i→LinkFrame_i]→Flange，活动
        // 关节集＝链上的基线 Joint，顺序＝链序＝权威关节序）。
        // 默认状态在全部帧入树后取（关节 q 全 0——WC 不设初值，权威静止
        // 位姿语义经 zeroOffset 映射在链变换内）。
        const rw::kinematics::State defaultState = workCell->getDefaultState();
        rw::core::Ptr<rw::models::SerialDevice> device = rw::core::ownedPtr(
            new rw::models::SerialDevice(serialChain, deviceName.fullName, defaultState));
        workCell->addDevice(device.template cast<rw::models::Device>());

        // ===== 第 9 步：S9 防御性自检（§12 RT-T07 测试列"RT-BW-4〔S9 部分〕
        // "的编译器侧落点）：BaseMount 读回值 vs 权威 T_world_base 逐元素
        // 比较（附录 D 第 4 项容差 1×10⁻⁹），含 ≈T·T 二次叠加形态探测
        // （AT-37 反例——实现缺陷类失败，不得放行，§6.6）。
        const std::optional<BaseMountDeviation> deviation =
            checkBaseMountConsistency(baseMount->getFixedTransform(), tWorldBase);
        if (deviation.has_value()) {
            // 比较型偏差数据进 detail（实际值/期望值/单位——诊断可定位；
            // 该失败理论不可达＝编译器自身缺陷信号，fail-fast 不发布）。
            std::string detail =
                "runtime/workcell-compiler: BaseMount 一致性自检失败（S9）——"
                "maxPositionDeviation=" + std::to_string(deviation->maxPositionDeviation)
                + " m, maxRotationDeviation=" + std::to_string(deviation->maxRotationDeviation)
                + ", doubleAppliedPattern=" + (deviation->doubleAppliedPattern ? "true" : "false");
            throw RuntimeError(RuntimeErrorCode::BaseWorldInconsistent, std::move(detail));
        }

        // ===== 第 10 步：产物装配（名字收集口径：编译器写入的帧名＋设备
        // 名——S8 交叉校验 actualNames 输入；不含基线 WORLD 根帧，其是树
        // 锚点而非编译产物，§7.2 保留字口径）。写入序＝确定性序。
        WorkCellCompileOutcome outcome;
        outcome.workCell = workCell;
        outcome.deviceName = deviceName.fullName;
        for (rw::kinematics::Frame* frame : serialChain) {
            outcome.runtimeNames.push_back(frame->getName());
        }
        outcome.runtimeNames.push_back(baseMount->getName());
        for (const CanonicalTool& tool : model.tools()) {
            outcome.runtimeNames.push_back(
                scopedFullName(nameMap, tool.objectId, NameScope::Tcp));
        }
        for (const CanonicalSceneObject& sceneObject : model.scene()) {
            outcome.runtimeNames.push_back(
                scopedFullName(nameMap, sceneObject.objectId, NameScope::SceneObject));
        }
        return outcome;
    }
    catch (const RuntimeError&) {
        throw;  // 本单元稳定失败语义（各段码面）——透传，不转译（见异常边界注释）
    }
    catch (const std::bad_alloc& ex) {
        // 内存不足→ResourceBudget（§5.5"超时/内存不足"行——对象级清理由
        // RAII 完成：此处 throw 时局部 WC/帧已随栈回退析构）。
        const core::DiagnosticRecord record =
            translateRobWorkError(ex, "S6", model.chain().robotObjectId);
        throw RuntimeError(RuntimeErrorCode::ResourceBudget,
                           std::string("stage=S6; ") + record.cause);
    }
    catch (const rw::core::Exception& ex) {
        // RobWork 基线异常→wc-compile-failed（§8.4 表行 1——RT-AD-1 的
        // 稳定码通道）。detail 携带 stage 与消息摘要（诊断字段面）；诊断
        // 记录本身由 RT-T11 编译链经 translateRobWorkError 全量登记。
        const core::DiagnosticRecord record =
            translateRobWorkError(ex, "S6", model.chain().robotObjectId);
        throw RuntimeError(RuntimeErrorCode::WorkCellCompileFailed,
                           std::string("stage=S6; ") + record.cause);
    }
    catch (const std::exception& ex) {
        // 其余标准异常＝编译硬失败兜底（S6 内部不应出现其他异常源；不吞
        // 不续——MDL-06）。转译行：非 rw 类型→RT-ROBWORK-ERROR 事件码面。
        const core::DiagnosticRecord record =
            translateRobWorkError(ex, "S6", model.chain().robotObjectId);
        throw RuntimeError(RuntimeErrorCode::WorkCellCompileFailed,
                           std::string("stage=S6; ") + record.cause);
    }
}

// =====================================================================
// WorkCellConstView——只读视图实现（契约注释见公共头 Adapter.hpp）。
// =====================================================================

WorkCellConstView::WorkCellConstView(rw::core::Ptr<const rw::models::WorkCell> workCell)
    : m_workCell(std::move(workCell))
{
    // 空句柄＝调用方契约违约（§8.3"hasWorkCell 恒 true"——无 WC 的场景由
    // capability 表达而非空视图）；按 §8.4"空 Ptr→robwork-error＋操作名"
    // fail-fast，不产出空视图（NFR-COR-03：不静默吞错）。
    if (m_workCell.isNull()) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           "runtime/robwork-null-handle: WorkCellConstView 构造收到空 WorkCell 句柄");
    }
}

const rw::models::WorkCell& WorkCellConstView::workCell() const noexcept
{
    // 构造已保证非空——解引用安全（前置在唯一构造入口集中执行）。
    return *m_workCell;
}

const rw::kinematics::Frame* WorkCellConstView::findFrame(std::string_view name) const noexcept
{
    // 基线 findFrame 入参为 const std::string&（string_view 无隐式转换——
    // 显式构造；未命中返回 nullptr，与基线语义一致）。
    return m_workCell->findFrame(std::string(name));
}

rw::core::Ptr<const rw::models::SerialDevice>
    WorkCellConstView::findDevice(std::string_view name) const noexcept
{
    // 基线 findDevice 返回可变 Ptr——视图面收敛为 const（§8.2 只读包装：
    // 调用方可查询设备结构，任何写路径在类型层不可达）。
    return m_workCell->findDevice(std::string(name)).template cast<const rw::models::SerialDevice>();
}

std::size_t WorkCellConstView::frameCount() const noexcept
{
    // 基线 getFrames 返回全部帧（含 WORLD 根）——视图如实转发。
    return m_workCell->getFrames().size();
}

rw::kinematics::State WorkCellConstView::defaultState() const
{
    // 值拷贝——调用方线程私有（State 是唯一可变工作区且线程私有，§8.7）。
    return m_workCell->getDefaultState();
}

// =====================================================================
// translateRobWorkError——异常转译纯函数实现（§8.4 转译表逐行）。
// =====================================================================

core::DiagnosticRecord translateRobWorkError(const std::exception& ex,
                                             std::string_view stage,
                                             std::optional<core::ObjectId> subject)
{
    // ---- 转译行分派（按异常动态类型；确定性——同类型同码）----
    std::string diagCode;
    std::string cause;
    std::string action;

    if (dynamic_cast<const std::bad_alloc*>(&ex) != nullptr) {
        // §8.4 表行 5：std::bad_alloc→resource-budget（内存不足；§5.5）。
        diagCode = std::string(registryCode(RuntimeErrorCode::ResourceBudget));
        cause = "编译过程内存不足（std::bad_alloc）";
        action = "缩小模型规模或释放内存后重试（同输入重试结果一致，PM-09）";
    } else if (const rw::core::Exception* rwEx = dynamic_cast<const rw::core::Exception*>(&ex)) {
        // §8.4 表行 1：rw::core::Exception（基线统一异常）→wc/dwc-compile-failed
        // ＋stage＋消息摘要。码按 stage 选择（S7 面 RT-T08 使用同一入口——
        // 稳定码经 registryCode 冻结映射取串，PA-1 不私裁码值）。
        diagCode = std::string(stage == "S7"
                                   ? registryCode(RuntimeErrorCode::DwcCompileFailed)
                                   : registryCode(RuntimeErrorCode::WorkCellCompileFailed));

        // ---- 消息摘要（NFR-REL-05：用户诊断不含调用栈）----
        // 基线 what() 形态＝"Id[-1]<file>:<line> : <message>"（Exception.cpp
        // 构造实测）："<file>:<line>" 是抛出位置（调用栈信息），必须在
        // 用户诊断面剥离；开发诊断如需完整原文另行走日志。
        std::string raw = rwEx->what();
        // 第一层：剥 "Id[...]" 前缀（找首个 ']'——id 段无嵌套）。
        if (raw.rfind("Id[", 0) == 0) {
            const std::size_t idEnd = raw.find(']');
            if (idEnd != std::string::npos) {
                raw.erase(0, idEnd + 1);
            }
        }
        // 第二层：剥 "<file>:<line> : " 位置前缀（首个 " : " 分隔——路径
        // 不含空格冒号空格序列，消息正文取分隔之后）。
        const std::size_t sep = raw.find(" : ");
        const std::string message = (sep != std::string::npos) ? raw.substr(sep + 3) : raw;
        // 摘要截断（展示长度限制——kCauseSummaryLimit；显式标注不静默）。
        if (message.size() > kCauseSummaryLimit) {
            cause = message.substr(0, kCauseSummaryLimit) + "…[消息摘要截断]";
        } else {
            cause = message;
        }
        action = "检查模型输入与 RobWork 基线兼容性；同输入重试结果一致（确定性编译）";
    } else {
        // 兜底行：非 rw 类型异常→RT-ROBWORK-ERROR 事件码（Errors.hpp
        // registryCode 注释：事件码不经 StableCodeRegistry，产出归转译点
        // ——本函数即 RT-T07 转译点；字面串与 Errors.hpp 登记一致）。
        diagCode = "RT-ROBWORK-ERROR";
        cause = ex.what();  // 标准异常无位置前缀，原文即摘要
        action = "记录开发诊断并上报（基线适配层未知异常形态）";
    }

    // 防御：空消息占位（DiagnosticRecord::make 要求 cause 非空——C-3）。
    if (cause.empty()) {
        cause = "（异常未携带消息）";
    }

    // context 携带 stage（§8.4 转译表"＋stage"——定位到编译段）。
    return core::DiagnosticRecord::make(
        diagCode, std::move(subject), std::nullopt, std::nullopt,
        "runtime 编译段 " + std::string(stage) + "（RobWork 异常转译）",
        cause, action);
}

}  // namespace sdurws::ird::runtime
