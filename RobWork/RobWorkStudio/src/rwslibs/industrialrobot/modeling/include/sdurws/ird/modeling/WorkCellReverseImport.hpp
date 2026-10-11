/**
 * @file   WorkCellReverseImport.hpp
 * @brief  WorkCell 反向导入（MDL-18/AT-33，R2/阶段 D）——.wc.xml 有损提取
 *         引擎的公共入口与 WorkCell 通道的 ImportUnsupportedItem kind 词表
 *         追加值。提取产出＝RobotDesign **草稿**＋导入报告（ImportOutcome
 *         复用面）：设备/关节/限位/位姿/Q 构型/碰撞设置逐项提取，不可
 *         表达/未保留内容逐项列出（提取报告是验收核心——AT-33 字面）。
 *
 * 设计依据：
 *   - units/modeling.md §6.6（WorkCell 反向导入——通道/提取语义/草稿红线/
 *     "从 WorkCell XML 新建项目来源"明确不做）、§6.4（分支报告＋显式选链
 *     ——目标设备选择同机制）、§6.7（导入资源与对象身份——临时句柄/
 *     Recorded 外部引用）、§7.4（DH→显式权威展开——无损）、§9.4.3
 *     （IModelImportMapper 三方法——mapWorkCellXml 委托本引擎）、§9.5
 *     （MDL-IMPORT-* 稳定码面）
 *   - units/io.md §9.6 v0.10（GenericXml 通用 XML 通道——冻结读取面；
 *     本单元消费 io 防护产物 bytes/快照/依赖树，语义解释归 modeling）
 *   - 需求 MDL-18（任意外部 WorkCell 反向导入——有损提取＋提取报告，结果
 *     为草稿不直接形成正式模型）、AT-33（从 .wc.xml 创建草稿模型＋提取
 *     报告——不可表达/未保留内容逐项列出）、MDL-10/12（不可表达内容按
 *     同一判定规则处置——多自由度/从动关节不得绕过）、MDL-11（零位与限位
 *     语义）、MDL-22（未配置安装默认地面）、MDL-03（不静默——一切默认
 *     补全/忽略/不支持逐项可观察）、NFR-COR-01/02/03（确定性/不静默）、
 *     CON-03（Recorded 外部引用可携带）、PA-1（导入产物为草稿——不经
 *     确认不产生修订，不经 project/**）
 *   - 任务契约 tasks/foundation/WP-13-T17.json（阶段 D 启用＋授权扩面
 *     fc75ccfa——allowedFiles 含 io/** 实现面）
 *
 * 背景说明（本通道在产品里的位置——为什么是"有损"提取）：
 * RobWork WorkCell（.wc.xml）是"帧树＋设备＋场景"的通用场景描述，而本
 * 产品的 RobotDesign 是"单机器人显式/DH 参数化模型"——两者天然不同构。
 * 反向导入只提取与 RobotDesign 同构的部分（单串联设备的关节链：类型/
 * 限位/位姿/几何/碰撞引用/Q 构型），其余内容（场景帧/相机/外部碰撞设置
 * 文件/备选构型/多自由度关节/从动关节/非单串联设备）一律**逐项列出**在
 * 提取报告中（不可表达＝判定不映射；未保留＝提取了但无落点）——绝不
 * 静默丢弃（NFR-COR-03/MDL-03）。提取结果永远是草稿：对象身份全部为
 * 确定性临时句柄（§6.7），不经确认不产生修订、不落盘、不经 project/**
 * （PA-1 权威唯一——应用才经命令 prepare 分配真实 ObjectId）。
 *
 * WorkCell XML 格式语义来源：RobWork 框架 XMLRWLoader/XMLRWParser 文法
 * （只读参考——本单元不引入框架 sdurw_loaders，DTB §4.6"原则不使用、
 * 启用须登记"表行；解析自实现经 pugixml，与 Import.cpp URDF 通道同款
 * O-40 消费面）。关键格式约定（提取语义逐条对照）：
 *   - <WorkCell name>：根元素；直下可出现 SerialDevice/ParallelDevice/
 *     TreeDevice/MobileDevice/Frame/Drawable/CollisionModel/Camera/
 *     CollisionSetup/ProximitySetup/Property/Calibration（框架文法全集）。
 *   - <Joint name type="Revolute|Prismatic|Spherical|PrismaticSpherical|
 *     Universal|PrismaticUniversal" state?="Active|Passive" refframe?>：
 *     设备内关节元素；子元素 <Pos x y z>（m）＋<RPY r p y>（**deg**——
 *     框架加载按 Deg2Rad 换算，本引擎同口径转 rad）、<PosLimit min max>
 *     （Revolute 单位 **deg**、Prismatic 单位 m——框架 addLimits 的
 *     convFactor 语义）、<VelLimit max>/<AccLimit max>、<Depend on gain
 *     offset>（从动关节——mimic 同义）、Drawable/CollisionModel（几何）。
 *   - <DHJoint name alpha a (d|theta|beta|b) offset ...>：DH 参数化关节
 *     （alpha/theta 单位 deg；a/d/offset 单位 m）——按 §7.4 DH→显式展开
 *     为 axis/origin（无损，MDL-10"DH→通用关节必须无损"）。
 *   - <Frame name type?="Fixed|Movable|..." refframe? daf?>：固定帧/自由
 *     帧（设备内＝链上 Fixed 连接或不可表达自由关节；WorkCell 级＝场景
 *     项——不映射）。
 *   - <Q name> q0 q1 ...</Q>：设备命名构型（rad；对位设备 Active 可动
 *     关节序）——首配置提取为逐关节 zeroOffset（权威零位＝源初始姿态），
 *     其余构型未保留逐项列出。
 *   - <CollisionSetup file>/<ProximitySetup file>：碰撞/邻近设置**外部
 *     文件引用**（框架文法无内联 Exclude——引用文件不在本通道解析范围，
 *     未保留逐项列出）。
 *   - Joint 的旋转/移动轴：RobWork Joint 无轴元素——轴向固定为关节局部
 *     系 z 轴 (0,0,1)（框架 RevoluteJoint/PrismaticJoint 语义），提取时
 *     以 Provided axis=(0,0,1) 落位并在映射清单注明来源。
 *
 * 线程安全：提取引擎无共享可变状态、可重入、多线程并发调用安全（卡
 * §3.4 总约定 1）。确定性（NFR-COR-01/02）：全部数值解析 locale 无关
 * （std::from_chars）；遍历＝文档序；诊断按（相对键，行，列，产出序）
 * 稳定排序；同输入字节＋同 options→逐字段相等输出。全 SI（m/rad/kg/
 * N·m）；源文件角度制式 deg 的换算点在注释显式标注。错误语义：调用方
 * 契约违约（bytes 与依赖树不匹配/显式选链不存在）→ 值面
 * SourceInconsistent（无草稿），不抛异常；源内容不可表达 → Unsupported
 * JointType/MimicBlocked 值面（语义与 URDF 通道同一判定）。
 */

#ifndef IRD_MODELING_WORKCELLREVERSEIMPORT_HPP
#define IRD_MODELING_WORKCELLREVERSEIMPORT_HPP

#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // core::DiagnosticRecord（diags 输出参数元素类型）
#include <sdurws/ird/modeling/Import.hpp>   // ValidatedSource/ImportOptions/ImportOutcome/ImportUnsupportedItem（复用面）

namespace sdurws::ird::modeling {

// =====================================================================
// WorkCell 通道 ImportUnsupportedItem.kind 词表追加值（表尾追加纪律——
// Import.hpp 主词表的通道增量；一经随报告对外呈现不改义不改拼，
// diagnostics StableCodeRegistry 同族稳定性约定）
// =====================================================================

/// "device-type"——非单串联设备（ParallelDevice/TreeDevice/MobileDevice）：
/// RobotDesign 只承载单串联关节链，多链/移动设备不可提取（MDL-12 同一
/// 判定——分支/非串联结构不可表达）。作为多设备候选时经分支报告呈现。
inline constexpr std::string_view kWcUnsupportedDeviceType = "device-type";

/// "scene-object"——WorkCell 级场景内容（静态 Frame/Drawable/
/// CollisionModel）：RobotDesign 草稿为单设备模型，场景对象不在提取范围
/// （用户可在应用后经场景编辑流另行添加）——未保留逐项列出。
inline constexpr std::string_view kWcUnsupportedSceneObject = "scene-object";

/// "collision-setup-external"——<CollisionSetup file>/<ProximitySetup
/// file>：碰撞/邻近设置为外部文件引用，引用文件不在本通道解析范围
/// （提取报告如实登记"引用未展开"）——未保留逐项列出。
inline constexpr std::string_view kWcUnsupportedCollisionSetup = "collision-setup-external";

/// "q-config-residual"——设备内第二及以后的 <Q> 命名构型：仅首配置提取
/// 为权威零位（zeroOffset），备选构型无落点——未保留逐项列出。
inline constexpr std::string_view kWcUnsupportedQConfigResidual = "q-config-residual";

/// "joint-acceleration-limit"——<AccLimit max>：§4.3-A 无 maxAcceleration
/// 字段（不发明 schema）——未保留逐项列出（与 URDF 通道
/// "joint-velocity-limit" 同族语义）。
inline constexpr std::string_view kWcUnsupportedAccLimit = "joint-acceleration-limit";

/// "base-placement"——设备基帧的安装位姿/锚点声明（refframe 非世界或
/// 基帧姿态非恒等）：BasePlacement 安装预设语义归 runtime 词表
/// （P-RT-4），源安装语义不映射——按 MDL-22 默认地面＋未保留逐项列出。
inline constexpr std::string_view kWcUnsupportedBasePlacement = "base-placement";

// =====================================================================
// 提取引擎（IModelImportMapper::mapWorkCellXml 的唯一委托实现点——§9.4.3
// 接口契约在本函数内逐条兑现）
// =====================================================================

/**
 * @brief WorkCell 反向导入提取（MDL-18/AT-33——有损提取＋提取报告；
 *        §6.6 通道语义的单一实现，mapWorkCellXml 委托本函数）。
 *
 * 契约（§9.4.3 mapWorkCellXml 行＋§6.6，逐条）：
 *   - @pre source 为 io 产物（bytes/entrySnapshot/dependencyTree；.wc.xml
 *     的良构性由 io 通道保证——本函数的 DOM 解析失败为防御面，走值面
 *     SourceInconsistent）。
 *   - @post 产出 ImportOutcome{draft, report, error}：
 *       · draft 有值＝单串联设备提取成功（草稿——临时句柄/Recorded 资源/
 *         authority=Explicit；**不产生修订、不落盘、不经 project/**
 *         ——PA-1，应用才经命令 prepare 分配真实身份）；
 *       · draft 无值＝不可提取（非 WorkCell 根/无设备/不可表达关节类型/
 *         多设备未选链/选链不存在/源不一致）——error 必有值，报告仍完整
 *         产出（"识别＋报告"语义：提取报告是无条件产出面，AT-33 逐项
 *         可观察不因失败丢失）；
 *       · error 有值且 draft 有值＝阻断型软失败（从动关节 Depend——
 *         mimic 同款：类型保留、草稿用于呈现与编辑、不可提交修订）。
 *   - @错误 ImportErrorCode 值面（UnsupportedJointType/MimicBlocked/
 *     MultiBranchNeedsSelection/SourceInconsistent——语义与 URDF 通道
 *     同一判定规则，MDL-10/12）。
 *   - report 四清单（mapped/defaults/ignored/unsupported）＋待确认
 *     （pendingConfirms：限位缺失等）＋关节状态＋资源状态表＋分支报告
 *     （多设备目标选择）＋主链能力结论（judgeChainCapability 复用——
 *     MDL-12 同一判定的单一实现）逐项可观察。
 *
 * 提取语义摘要（逐条细则见实现注）：
 *   - 目标设备选择＝§6.4 同机制：多 SerialDevice 文件产分支报告
 *     （branchRoot＝设备名），未显式选链拒绝（ImportOptions.
 *     selectedMainBranch 回填）；单设备直通。
 *   - 关节类型：Revolute/Prismatic/Fixed（Frame type=Fixed）可表达；
 *     Spherical/PrismaticSpherical/Universal/PrismaticUniversal/Movable
 *     等多自由度/自由关节不可表达（kind="joint-type"，无草稿——不得转
 *     FixedFrame）；<Depend> 从动关节＝mimic 同款（识别＋报告＋阻断，
 *     类型保留）。
 *   - 位姿：<Pos>（m）＋<RPY>（deg→rad——源文件角度制式 deg，框架
 *     Deg2Rad 同口径）→ origin（T_parent_joint）；缺省→零位（默认清单）。
 *   - 限位：<PosLimit min max>（Revolute 单位 deg→rad；Prismatic 单位
 *     m）→ bounds；缺失→待确认（limit-missing）。
 *   - Q 构型：首配置→逐关节 zeroOffset（权威零位＝源初始姿态；
 *     q_authoritative = q_zeroOffset + q_rw）；备选构型未保留。
 *   - 几何：Drawable→visual、CollisionModel→collision；<Polytope
 *     file>→Mesh 资源（Recorded；依赖树缺失叶→IO-RES-MISSING 事实，
 *     "缺失≠不可行"）；图元→geometry-primitive（复用 URDF 词表）。
 *   - 碰撞设置：外部文件引用→collision-setup-external（未保留）。
 *
 * @param source  [in] io 已验证产物（ValidatedSource 契约同 mapUrdf）
 * @param options [in] 导入选项（selectedMainBranch＝目标设备名——§6.4
 *                选链回填语义；单设备文件留空）
 * @param diags   [out] 诊断记录输出（追加不清空；按源文件行序稳定排序；
 *                码面＝已注册 MDL-IMPORT-族与 IO-族稳定码，唯一构造点见
 *                DiagCodes.hpp 常量——禁字符串拼码）
 * @return ImportOutcome（值语义归调用方——§9.4.3 所有权行）
 *
 * 纯函数；线程安全；确定性（同输入字节＋同 options→同输出）。
 */
ImportOutcome extractWorkCellDevice(const ValidatedSource& source,
                                    const ImportOptions& options,
                                    std::vector<core::DiagnosticRecord>& diags);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_WORKCELLREVERSEIMPORT_HPP
