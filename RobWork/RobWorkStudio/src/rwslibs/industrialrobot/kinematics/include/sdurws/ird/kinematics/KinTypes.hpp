/**
 * @file   KinTypes.hpp
 * @brief  kinematics 值模型公共头（T03 批次）——TcpRef（TCP 引用值）与
 *         IKinRuntimeView（runtime 只读模型视图的本单元最小消费接口）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 KinTypes.hpp 行——"值模型、
 *     结局枚举"，任务列 T03/T04：本头随 T03 落位首批类型，T04 落位时在
 *     表尾追加解集/结局枚举等值模型——不重排既有声明）、§5.2 输入行
 *     （"TCP 引用（tool-definition oid＋tcpKey→快照解析）"——TcpRef
 *     字段出处）、§4.2（RuntimeSnapshot 消费面——IKinRuntimeView 的
 *     消费语义出处）、§14.4 条 6（"IKinRuntimeView 最小消费接口——
 *     runtime 只读视图的本单元侧封装，不新增 runtime 语义"）
 *   - 治理裁决 O-37（DTB §4.2 已裁决 closed，2026-09-22）：运行时模型
 *     视图＝**宿主注入**形态——评估宿主构建绑定请求 RuntimeSnapshot 的
 *     只读模型视图、经评估器工厂闭包注入。本头即该裁决在 kinematics 侧
 *     的类型落点：本单元定义自有最小接口，宿主（L5 装配）以适配器实现
 *     之；对端 evidence.md §9 的注入点文字增补归 evidence 卡所有者
 *     （P-KIN-2 在途——本单元不依赖、不冻结该侧契约）。
 *   - 任务契约 tasks/foundation/WP-15-T03.json（acceptance 2/4 的类型面）
 *
 * 背景说明（为什么不是直接消费 runtime::IRuntimeModelView）：卡面 §9.2
 * IFkEvaluator.evaluate 的视图参数类型为 IKinRuntimeView——自有最小接口
 * 使 (a) 单元测试可在无完整 RuntimeSnapshot 的条件下以测试替身实现视图
 * （R-KIN-1"T03 前以测试替身先行"），(b) 本单元对 runtime 的依赖收敛到
 * §4.2 消费面真正用到的成员，runtime 契约后续演进时的对账面最小。
 * 宿主适配器（runtime::IRuntimeModelView → IKinRuntimeView）归 L5 装配
 * （evidence §3.3 D-07 注入先例同款分工——适配器十行级，非本单元交付物）。
 *
 * 线程安全：本头全部实体为纯值/纯接口（无共享可变状态）；实现类的线程
 * 约束随实现注释声明。
 */

#ifndef IRD_KINEMATICS_KINTYPES_HPP
#define IRD_KINEMATICS_KINTYPES_HPP

#include <string>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/Identity.hpp>      // ObjectId（工具对象稳定身份）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // runtime::CanonicalModel（只读模型真值）

namespace sdurws::ird::kinematics {

// =====================================================================
// TcpRef——TCP 引用（§5.2 输入行"tool-definition oid＋tcpKey→快照解析"）
// =====================================================================

/**
 * @brief TCP 引用值：指名快照内的一个工具定义对象及其 TCP。
 *
 * 解析规则（§5.2/§9.6 消费语义的 T03 实现口径，随单元卡 §14.6 登记）：
 *   - toolObject 经快照模型的对象索引（CanonicalModel::findObject）解析到
 *     CanonicalTool——解析失败（无工具/引用悬空/指名对象非工具）＝
 *     KinematicsError{NoTcp}（§9.6 KIN-NO-TCP"TCP 未配置/悬空"两分语义）；
 *   - tcpKey 为 TCP 选择键：**空串＝canonical TCP**（规范模型每工具恰一个
 *     tcpOffset——runtime §4.3.4 单 TCP 面，与 KIN-14 默认 TCP 单值同源）；
 *     非空时必须与该工具的 canonical TCP 身份 token（＝工具 localName——
 *     canonical TCP 以其所属工具命名）精确相等，否则＝
 *     KinematicsError{FrameUnresolved}（§9.2 @错误 行"tcpRef 指名的 TCP 帧
 *     不存在"——多 TCP 工具是 modeling 侧语义扩展，canonical 面就绪前
 *     键不命中即帧未解析，不做前缀/模糊匹配，R-4 禁拼串定位）。
 *
 * 值语义纯结构；线程安全（并发只读安全）。确定性：同引用同解析结果
 * （解析只依赖快照内容——NFR-COR-01/02）。
 */
struct TcpRef {
    /// 工具定义对象稳定身份（tool-definition oid——§5.2 输入行；快照内
    /// 引用，跨 snapshot 使用属调用方契约违约——§9.2"非法调用"行）。
    core::ObjectId toolObject;
    /// TCP 选择键（空串＝canonical TCP；非空须与工具 localName 精确相等
    /// ——解析规则见结构体注）。单位/量纲：无（身份 token）。
    std::string tcpKey;

    /// 精确等值（逐成员；身份按 core 强类型等值、键按字节串等值——
    /// 无容差，身份面比较禁用数值容差）。
    bool operator==(const TcpRef& o) const
    {
        return toolObject == o.toolObject && tcpKey == o.tcpKey;
    }
    bool operator!=(const TcpRef& o) const { return !(*this == o); }
};

// =====================================================================
// IKinRuntimeView——runtime 只读模型视图的最小消费接口（§4.2/§14.4-6；
// O-37 宿主注入形态的 kinematics 侧类型落点）
// =====================================================================

/**
 * @brief 运行时模型视图的本单元最小消费接口（§4.2 消费面在 T03 的投影）。
 *
 * 语义边界（§14.4 条 6 原文口径）：**不新增 runtime 语义**——本接口只把
 * §4.2 消费面表中本单元实际消费的成员以自有虚接口收拢，每个成员的语义
 * 与 runtime 侧对应访问器逐字一致：
 *   - model()：规范模型只读真值（关节类型/轴/origin/zeroOffset/限位/
 *     工作范围、工具/TCP、T_world_base 唯一来源、gravityWorld）——约束：
 *     只读；不缓存跨请求（§4.2"每请求重建视图"）。
 *   - worldToBase()：T_world_base（世界系→基座系读法 core §4.6——
 *     p_world＝T_world_base·p_base）；基座—世界唯一读取点（M-11/AT-37：
 *     本单元禁止第二套基座变换代数，tcpInWorld 只经该值组合）。
 *
 * 生命周期与线程（§9.4 表 IKinRuntimeView 行）：每请求构建（宿主持有）、
 * 请求结束失效；构建于宿主、使用于工作线程（**只读**）——实现方须保证
 * evaluate() 调用期间视图存活且并发只读安全；评估器不接管所有权
 * （§9.2 @所有权 行"view 由调用方持有"）。
 *
 * 扩展纪律：后续任务（T04 IK 等）需要更多消费面成员时在本接口**表尾
 * 追加**纯虚成员并同步登记单元卡（不重排既有成员——虚表序进入二进制
 * 契约）；T03 的最小面＝上列两成员（位姿指标无重力相关量，gravityBase()
 * 随首个重力消费任务追加——§4.5"重力相关量仅经 gravityBase"的约束在
 * 消费出现时生效）。
 */
class IKinRuntimeView {
public:
    virtual ~IKinRuntimeView() = default;

    /// 规范模型只读真值（§4.2 消费面 model() 行——语义同
    /// runtime::IRuntimeModelView::model()；返回引用所指对象须在视图
    /// 存活期内稳定）。
    virtual const runtime::CanonicalModel& model() const = 0;

    /// T_world_base（§4.2 消费面 worldToBase() 行——世界系→基座系读法
    /// core §4.6；平移单位 m、旋转无量纲正交阵；前置：快照编译产物已过
    /// 合法域校验）。
    virtual rw::math::Transform3D<double> worldToBase() const = 0;
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_KINTYPES_HPP
