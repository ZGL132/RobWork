/**
 * @file   Variable.hpp
 * @brief  设计变量模型——变量类别、绑定、R1 内置词表（9 类）、新机型/改型
 *         两种初始化、绑定集校验与补丁差异（所有权 O1；任务 WP-20-T03）。
 *
 * 设计依据：
 *   - units/optimization.md §5.2（变量定义与绑定——VariableKind 四类与
 *     VariableBinding 结构设计基线）、§5.3（R1 变量表——OPT-B 9 类权威清单，
 *     REQUIREMENTS §15.0 同源；绑定 token 形态/类型/单位/边界约束/权威来源
 *     五列逐行承载）、§5.4（改型授权与锁定——未授权参数默认锁定）、
 *     §5.7（阶段锁——StageB 引用 R2 变量即拒 OPT-STAGE-LOCKED）、
 *     §8.2（新机型/改型两种初始化——共用同一管线，仅初始化参数与锁定集
 *     不同）、§12.2（IOptimizationVariableProvider 接口签名）、§12.3
 *     （错误/线程/所有权约定）
 *   - 需求 OPT-01（新机型/改型两种初始化）、OPT-02（连续/量化/枚举/离散
 *     器件四类变量；drivetrain.ratio StageB 绑定可编辑并编译进候选〔V12-02，
 *     c＝Δq_joint/Δθ_motor 口径〕；改型默认锁定非授权参数）
 *   - 诊断码：DiagCodes.hpp 常量 kOptInputInvalid/kOptVarLocked/
 *     kOptStageLocked/kOptVarUnbindable（卡 §6.6 登记表——码值唯一书写点）
 *   - 待裁决联动（knownPitfalls）：P-OPT-4（连杆截面类型枚举切换 R1 不启用
 *     ——词表条目 enabledInStageB=false 承载）；P-OPT-2（候选物化通道裁决前
 *     ——研究定义面先行；本头不依赖任何基线对象字节，权威字段"存在性/权威
 *     模式"等基线前提留给 WP-20-T08 Preflight 消费快照②端口核验）
 *
 * 背景说明（变量模型三概念——卡 §5.1 流程图）：
 *   词表（VariableDefinition）＝"哪些权威字段类可被优化"的域内唯一定义点
 *     （绑定 token 形态、值类别、单位、绑定前提）——按阶段给全量模板；
 *   绑定（VariableBinding）＝研究定义中"激活了哪个具体字段＋值域＋锁定/
 *     授权状态"——用户实例化并进 config.opt（进 sliceId）；
 *   补丁（CandidatePatch，见 CandidatePatch.hpp）＝一次候选对若干绑定的
 *     取值——候选描述而非项目修订（N1：RobotDesign 真值归 modeling）。
 *
 * ★ 多分量变量的实例化口径（实现口径，单元卡增量修订登记）：卡面 §5.3
 *   的 token 形态按"权威字段"书写（如 mdl.joint[i].bounds、
 *   mdl.base.orientation）；补丁取值粒度＝单个 SI 标量（PatchItem.scalarValue，
 *   §5.5），故多分量权威字段在实例化时按**值分量后缀**拆为多个绑定
 *   （如 …bounds.qmin／…bounds.qmax；…orientation.eaa.x/.y/.z）。词表
 *   tokenPattern 相应按分量细分登记（每条目 specRef 标注卡面行号）——
 *   语义与卡面字段表一致，仅实例化粒度的显式化。
 *
 * 线程约束：全部类型为纯值；builtinVariableDefinitions 返回内部静态表引用
 * （只读共享、可并发）；服务对象 const 方法均可并发（§12.3 线程行）。
 * 确定性：词表条目序＝卡面 §5.3 行序（确定性序）；diff 输出按 bindingId
 * 字典序（I-OPT-10 供差异预览与导出的稳定顺序）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_VARIABLE_HPP
#define SDURWS_IRD_OPTIMIZATION_VARIABLE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Units.hpp>          // core::UnitToken——绑定单位句柄
                                              // （SI 因子换算唯一实现，NFR-MNT-03）
#include <sdurws/ird/evidence/Snapshot.hpp>   // evidence::AnalysisSnapshot——
                                              // validateBindings 的快照闭包核对输入
#include <sdurws/ird/optimization/Types.hpp>  // OptimizationStage——词表阶段作用域

namespace sdurws::ird::optimization {

// 前向声明：CandidatePatch（定义见 CandidatePatch.hpp）——本头 diff 面
// 仅以 const 引用消费它（C++17 成员/自由函数声明无需完整类型）。
// include 方向纪律：CandidatePatch.hpp → Variable.hpp 单向（补丁校验消费
// 绑定语义）；反向只允许前向声明——双向 include 会因 include guard 打断
// 造成"别名未定义"编译错（本头初版实测）。
struct CandidatePatch;

// =====================================================================
// 变量类别（卡 §5.2 原文契约；OPT-02 四类）
// =====================================================================

/**
 * @brief 变量值类别（OPT-02：连续/量化/枚举/离散器件）。
 *
 * Continuous＝SI 真值标量（值域 [lower,upper] 内任意有限值）；
 * Quantized＝步长网格上的量化标量（step>0；值按确定性 round-half-even
 *   对齐后进补丁 canonical——卡 §8.2 变量编码；量化是"变量类型系统能力"，
 *   全阶段支持，R1 内置词表暂无强制量化变量——卡 §5.3 量化变量说明）；
 * Enumeration＝封闭值域中的下标选择（如材料键、基座预设）；
 * DiscreteDevice＝离散器件引用（R2/OPT-D——目录 modelId/组合规范序列，
 *   StageB 引用即 OPT-STAGE-LOCKED 拒绝，§5.6/§5.7）。
 */
enum class VariableKind { Continuous, Quantized, Enumeration, DiscreteDevice };

/**
 * @brief 变量类别稳定 token（诊断定位与导出书写用）。
 * @return "continuous"/"quantized"/"enumeration"/"discrete-device"。
 */
std::string_view toToken(VariableKind kind) noexcept;

/// 变量绑定的权威字段定位 token（卡 §5.2 原文别名——值语义以 modeling/
/// runtime 冻结字段为准；如 "mdl.joint[3].dh.a"、"mdl.link[2].material"）。
using BindingToken = std::string;

// =====================================================================
// 词表条目与绑定（卡 §5.2/§5.3）
// =====================================================================

/**
 * @brief 变量定义词表条目——"哪类权威字段可被优化"的域内唯一定义点。
 *
 * 生命周期：builtinVariableDefinitions() 返回内部静态表 const 引用——
 * 调用方只读、不可释放；本结构自身按值拷贝传播。
 * 线程安全：纯值＋只读共享。
 */
struct VariableDefinition {
    BindingToken tokenPattern = {};  ///< 绑定 token 形态；[i]/[j]/[key] 为实例化
                                     ///  占位（分别对应关节序/传动关节序/TCP 键）。
                                     ///  多分量字段按分量后缀细分（见文件头注）。
    VariableKind kind = VariableKind::Continuous;  ///< 词表定义值类别
    std::string unitSymbol = {};     ///< SI 单位符号（core::UnitToken 注册符号：
                                     ///  "m" 长度 / "rad" 角度 / "1" 无量纲）；
                                     ///  枚举/离散为空串（值非物理量）
    std::string authorityFieldPath = {};  ///< 权威字段定位形态（占位同 tokenPattern；
                                          ///  物化目标——modeling/runtime 冻结字段，
                                          ///  如 "robot-design/joints[i]/dh/a"）
    std::string specRef = {};        ///< 卡面出处（如 "units/optimization.md §5.3 #2"）
                                     ///  ——词表登记的追溯字段
    std::string bindingPrerequisite = {};  ///< 绑定前提的**文字承载**（§5.3"边界与
                                           ///  约束"列，如"基线 authority==StandardDH
                                           ///  才可绑定"）——基线事实核验归 Preflight
                                           ///  （WP-20-T08 #14，经快照②端口），
                                           ///  本词表不读对象字节（P-OPT-2 裁决前
                                           ///  研究定义面先行）
    std::string mutexGroup = {};     ///< 权威互斥组键（I-OPT-7；空＝不参与互斥）。
                                     ///  同组键的两个激活绑定互斥（如同关节的
                                     ///  DH 组与安装位置组——StandardDH/Explicit
                                     ///  权威互斥，I-MDL-8；基座预设与 custom）。
                                     ///  占位 [i] 随 bindingId 实例化替换。
    std::vector<std::string> enumValues = {};  ///< 枚举封闭值域（枚举类条目；
                                               ///  值域对齐权威侧冻结清单——
                                               ///  材料键集 modeling
                                               ///  PropertyEstimation 表、基座
                                               ///  预设 ground/inverted/wall）
    bool enabledInStageB = true;     ///< StageB（R1）是否可绑定；false 而被引用
                                     ///  ⇒ 校验拒绝 OPT-STAGE-LOCKED（§5.7——
                                     ///  不降级、不丢弃）。1b 截面类型枚举
                                     ///  P-OPT-4 裁决前两阶段均 false。
    bool enabledInStageD = false;    ///< StageD（R2）是否可绑定（离散器件类 true）
    bool valueMustBePositive = false;  ///< 值域硬约束：补丁标量值必须 >0 且有限
                                       ///  （I-MDL-11 传动比——c＝Δq_joint/
                                       ///  Δθ_motor，无量纲；非发明阈值，
                                       ///  需求原文值域）
};

/**
 * @brief 单个变量绑定（卡 §5.2 结构设计基线逐字承载）——研究定义中
 *        "激活哪个具体字段＋值域＋锁定/授权状态"。
 *
 * 与词表的关系：bindingId 必须匹配某条词表 tokenPattern（未知绑定在
 * validateBindings/makeCandidatePatch 拒绝）；kind/unit/enumValues 等
 * 元数据与词表定义一致性由 validateBindings 核对（防私造值域）。
 * 值域语义：lowerBound/upperBound 为含端点闭区间，SI 单位（unit.symbol
 * 的量纲）；必须 lowerBound < upperBound（严格——MDL-06④ 同式口径）。
 *
 * 锁定/授权（§5.4 改型初始化）：authorized=false ⇒ locked=true（未授权
 * 参数默认锁定，OPT-02）——联动由 initializeBindings 施加；锁定绑定出现在
 * 补丁中在**生成阶段**拒绝（OPT-VAR-LOCKED，比较型定位 bindingId＋对象）。
 *
 * 线程安全：纯值。
 */
struct VariableBinding {
    BindingToken bindingId = {};     ///< 稳定 token（研究内唯一；字典序进
                                     ///  canonical 与 diff——确定性键）
    VariableKind kind = VariableKind::Continuous;  ///< 值类别（与词表一致性由
                                                   ///  校验核对——见实现口径）
    core::UnitToken unit = {};       ///< SI 单位句柄（连续/量化须 isValid——
                                     ///  core::UnitToken::find 取得；枚举/
                                     ///  离散允许默认无效句柄＝值非物理量）
    double lowerBound = 0.0;         ///< 下界（含），SI 单位；连续/量化有效
    double upperBound = 0.0;         ///< 上界（含），SI 单位；必须 lower<upper
    double step = 0.0;               ///< 量化步长 >0（与单位同量纲）；Continuous
                                     ///  恒 0；枚举/离散无意义置 0（卡 §5.2 原文）
    std::vector<std::string> enumValues = {};  ///< 枚举封闭值域（须等于词表值域
                                               ///  ——防私造枚举项）
    double defaultValue = 0.0;       ///< 连续/量化默认（基线值，SI 单位）；枚举
                                     ///  用 defaultValueIndex
    std::uint32_t defaultValueIndex = 0;  ///< 枚举默认下标（enumValues 内）
    bool locked = false;             ///< 锁定：生成阶段拒绝出现在补丁中（§5.4）
    bool authorized = true;          ///< 改型授权（§5.4：未授权默认 locked；
                                     ///  authorized=false 必须伴随 locked=true）
    std::string authorityFieldPath = {};  ///< 权威字段定位（实例化后缀，如
                                          ///  "robot-design/joints[3]/dh/a"）
    std::string diagSubject = {};    ///< 关联对象 ObjectId 规范文本（诊断定位；
                                     ///  "obj-<32hex>"——validateBindings 对
                                     ///  非空者核对快照 objectClosure 存在性）
};

// =====================================================================
// 内置词表（卡 §5.3 R1 变量表——域内唯一定义点）
// =====================================================================

/**
 * @brief 指定阶段的内置变量定义词表（卡 §5.2 原文签名；§5.3 表的物化）。
 *
 * StageB 返回 R1 全量（§5.3 表 9 类，多分量按文件头注口径细分条目；其中
 * 1b 截面类型枚举 P-OPT-4 裁决前 enabledInStageB=false——保留登记不删除）；
 * StageD 追加离散器件类（§5.6——motor-key/reducer-key，REQUIREMENTS §15.0
 * OPT-D 行权威命名）。
 *
 * @param stage [in] 目标阶段
 * @return 词表只读引用（内部静态表——进程生存期；调用方不得修改/释放）。
 *         条目序＝卡面 §5.3 行序＋StageD 追加序（确定性，NFR-COR-02）。
 */
const std::vector<VariableDefinition>& builtinVariableDefinitions(OptimizationStage stage);

/**
 * @brief bindingId → 词表条目匹配（本头实现口径的辅助查询面）。
 *
 * 匹配规则：bindingId 与 tokenPattern 逐段匹配——[...] 占位段匹配任意
 * 非空段（不含 ']'），其余段逐字相等；与词表条目序无关。
 *
 * ★ 匹配域＝**全量词表**（含未启用条目——如 1b 截面类型、离散器件类）：
 *   "token 是否为登记变量"与"该阶段是否启用它"是两个判定——后者由校验
 *   层按条目 enabledInStageB/enabledInStageD 执行（§5.7 阶段锁，命中即
 *   OPT-STAGE-LOCKED，不降级不丢弃）。若匹配只查阶段启用集，StageB 引用
 *   电机型号会被误报为"未知绑定"而非阶段锁——这正是 AT-09/V12-02 要防的
 *   语义混同（"未登记"≠"登记了但阶段不支持"）。
 *
 * @param bindingId [in] 待匹配的绑定 token
 * @param stage     [in] 阶段上下文（词表查询本身阶段无关——全量匹配；
 *                  参数保留供未来阶段特化条目的扩展点）
 * @return 命中条目的只读指针（内部静态表生存期）；无命中返回 nullptr
 *         （调用方按"未知绑定"处置——OPT-PATCH-ILLEGAL/OPT-INPUT-INVALID）。
 */
const VariableDefinition* matchDefinition(const BindingToken& bindingId,
                                          OptimizationStage stage);

// =====================================================================
// 两种初始化（OPT-01；卡 §8.2/§5.4——共用同一管线，仅初始化参数与锁定集不同）
// =====================================================================

/**
 * @brief 研究初始化模式（OPT-01 的两种方式）。
 */
enum class StudyInitialization {
    NewModel,  ///< 新机型：基线＝模板创建的当前修订 RobotDesign；变量授权
               ///< 默认全开（authorized=true、locked=false）；边界由用户按
               ///< 机型工程范围填写——无需求侧默认边界值（P-03：不发明
               ///< 默认工程数值）；策略＝全局空间填充（探索优先）
    Refit,     ///< 既有机型改型：基线＝当前修订；未授权参数默认锁定
               ///< （authorized=false ⇒ locked=true，OPT-02/§5.4）；策略＝
               ///< 基线邻域扰动；授权由用户逐变量显式开启（进 config.opt）
};

/**
 * @brief 对一组已实例化的绑定施加初始化策略（卡 §8.2 两种初始化的落地面）。
 *
 * "同一管线，仅初始化参数与锁定集不同"的实现形态：本函数不发明变量、
 * 不改值域，只按模式统一施加 authorized/locked 默认值并回填词表元数据
 * （kind/unit/enumValues/authorityFieldPath——以词表定义为准；绑定已显式
 * 设置的边界/默认值/诊断定位保持不变）。
 *
 * 授权语义（Refit 模式）：初始化产出**全部** authorized=false、locked=true
 * （未授权参数默认锁定，OPT-02）；"授权"是用户在研究定义中逐变量显式
 * 开启的**后续编辑动作**（授权状态进 config.opt canonical——授权集合变化
 * ＝新输入，§5.4），不属于初始化的推断职责——入参 authorized 位是
 * VariableBinding 的通用字段默认（卡 §5.2，服务新机型场景），Refit 不
 * 信任它、统一重置，防止"字段默认值被误读为显式授权"而漏锁。
 * NewModel 模式一律 authorized=true/locked=false（全部默认可绑定）。
 *
 * @param stage    [in] 目标阶段（元数据回填的词表来源）
 * @param mode     [in] 初始化模式（NewModel 全开／Refit 默认锁定）
 * @param bindings [in] 用户实例化的绑定（至少含 bindingId；建议已填边界/
 *                 诊断定位——未填者可后续编辑，校验在 validateBindings）
 * @return 施加初始化策略后的绑定集（保持入参顺序；逐条拷贝修改）
 *
 * @throws OptimizationError(kOptInputInvalid) 入参 bindingId 为空或在本
 *         阶段词表无匹配（初始化只对词表内变量生效——未知绑定属调用方
 *         契约违约，fail-fast）
 */
std::vector<VariableBinding> initializeBindings(OptimizationStage stage,
                                                StudyInitialization mode,
                                                const std::vector<VariableBinding>& bindings);

// =====================================================================
// 绑定集校验（I-OPT-7～9；§5.4/§5.7——研究定义校验面）
// =====================================================================

/**
 * @brief 单条绑定校验问题（比较型定位：码＋绑定 token＋中文细节）。
 *
 * code 取值域＝DiagCodes.hpp OPT-* 常量（本面出现四种：kOptInputInvalid
 * 结构/互斥非法、kOptStageLocked 阶段不支持、kOptVarUnbindable 词表条目
 * 在当前阶段未启用〔警告语义的登记承载——见报表面注〕、kOptVarLocked
 * 锁定/授权状态不一致）。detail 为开发诊断文本（中文；非用户可见文案）。
 */
struct BindingValidationIssue {
    std::string code = {};       ///< OPT-* 稳定码 token（DiagCodes.hpp 常量）
    std::string bindingId = {};  ///< 定位：涉事绑定 token（未定位到具体绑定的
                                 ///  整体性问题为空串）
    std::string detail = {};     ///< 中文开发诊断（违规语义＋期望形态）
};

/**
 * @brief 绑定集校验报告（逐项定位；issues 非空即存在拒绝项）。
 *
 * 语义分级：kOptVarUnbindable 为 warning 语义（词表条目未启用但绑定存在
 * ——Preflight #14 的"绑定存在但未激活"警告同源），其余 code 均为阻塞
 * （研究定义校验拒绝，运行启动阻断——卡 §6.4 #3/#10）。ok() 仅反映
 * "无阻塞项"（允许警告随行）。
 */
struct BindingValidationReport {
    std::vector<BindingValidationIssue> issues = {};  ///< 全部问题（按发现序）

    /// @brief 无阻塞项（警告不计入）——allowQuick 前提的绑定面部分。
    bool ok() const noexcept;
    /// @brief 报告中是否存在指定稳定码的问题（测试与编排面定位用）。
    bool hasCode(std::string_view code) const noexcept;
};

// =====================================================================
// 变量差异（I-OPT-10——供 §10.4 候选差异预览与导出复用）
// =====================================================================

/**
 * @brief 两个补丁在同一绑定上的差异条目（I-OPT-10 原文：变更对象定位经
 *        authorityFieldPath＋diagSubject）。
 *
 * 值三元组按差异绑定的类别取用：Scalar→oldScalar/newScalar（SI 单位＝
 * 绑定单位）；Enum→oldEnumIndex/newEnumIndex；Discrete→oldDiscreteRef/
 * newDiscreteRef。与变更无关的成员保持默认 0/空串（不构成第二语义）。
 */
struct VariableDiffEntry {
    BindingToken bindingId = {};      ///< 差异绑定 token
    std::string authorityFieldPath = {};  ///< 权威字段定位（来自绑定；绑定未知
                                          ///  时为空——未知绑定差异仍报告，
                                          ///  定位退化）
    std::string diagSubject = {};     ///< 关联对象定位（来自绑定；同上）
    char kind = 'M';                  ///< 差异类别：'A'＝added（b 有 a 无）、
                                      ///  'D'＝removed（a 有 b 无）、'M'＝
                                      ///  modified（两侧都有但值不同）
    double oldScalar = 0.0;           ///< Scalar 类：a 侧值（SI 单位）
    double newScalar = 0.0;           ///< Scalar 类：b 侧值（SI 单位）
    std::uint32_t oldEnumIndex = 0;   ///< Enum 类：a 侧下标
    std::uint32_t newEnumIndex = 0;   ///< Enum 类：b 侧下标
    std::string oldDiscreteRef = {};  ///< Discrete 类：a 侧引用
    std::string newDiscreteRef = {};  ///< Discrete 类：b 侧引用
};

/**
 * @brief 两个补丁的变量差异（I-OPT-10；卡 §12.2 原文签名）。
 *
 * 确定性：输出按 bindingId 字典序（稳定顺序——差异预览与导出直接消费）。
 * 仅报告"值不同"的绑定；两侧相同值的绑定不出现在输出中。
 *
 * @param a [in] 基准补丁（old 侧）
 * @param b [in] 比较补丁（new 侧）
 * @return 差异条目（bindingId 升序；无差异返回空表）
 */
std::vector<VariableDiffEntry> diffCandidatePatch(const CandidatePatch& a,
                                                  const CandidatePatch& b);

// =====================================================================
// 变量词表服务（卡 §12.2 接口原文——O1 变量词表服务面）
// =====================================================================

/**
 * @brief 变量词表与绑定校验服务接口（OPT-02；卡 §12.2 设计基线签名）。
 *
 * 实现不持状态（全部 const 只读、可并发——§12.3 线程行）；validateBindings
 * 消费快照仅做 objectClosure 只读核对（不写项目、不产生诊断目录条目——
 * 诊断实例化归编排面）。
 */
class IOptimizationVariableProvider {
public:
    virtual ~IOptimizationVariableProvider() = default;

    /// @brief 返回指定阶段的内置变量定义词表（§5.3；R2 追加离散器件类）。
    virtual std::vector<VariableDefinition> definitionsFor(OptimizationStage stage) const = 0;

    /// @brief 校验绑定集：互斥/权威字段存在性/边界合法性；锁定与授权状态
    ///        核对（§5.4）。拒绝项阻断运行启动（Preflight #3/#10 消费面）。
    /// @threadSafe 是（只读消费快照闭包）。
    /// @determinism 同输入同报告（纯函数面）。
    virtual BindingValidationReport validateBindings(
        const std::vector<VariableBinding>& bindings,
        const evidence::AnalysisSnapshot& snapshot) const = 0;

    /// @brief 两个补丁的变量差异（§5.2 I-OPT-10；供差异预览与导出）。
    virtual std::vector<VariableDiffEntry> diffCandidatePatch(
        const CandidatePatch& a, const CandidatePatch& b) const = 0;
};

/**
 * @brief 默认实现（内置词表驱动；WP-20-T03 落位）。
 *
 * 装配期以 OptimizationStage 定型（§12.3"服务实例由 L5 装配注入，运行期
 * 只读"——校验的阶段面在装配期确定，运行期同一实例只服务同一阶段的
 * 研究；跨阶段校验各建实例）。validateBindings 的快照消费＝diagSubject
 * 非空者必须出现在 snapshot.objectClosure（悬空引用即研究定义非法——
 * Preflight #4"引用悬空"的绑定级核对；不读对象字节，P-OPT-2 裁决前
 * 研究定义面先行）。实例无其他可变状态——const 方法可并发（§12.3）。
 */
class OptimizationVariableProvider final : public IOptimizationVariableProvider {
public:
    /// @param stage [in] 本实例服务的优化阶段（装配期定型；默认 StageB——
    ///        R1 主口径）
    explicit OptimizationVariableProvider(OptimizationStage stage = OptimizationStage::StageB);

    std::vector<VariableDefinition> definitionsFor(OptimizationStage stage) const override;
    BindingValidationReport validateBindings(
        const std::vector<VariableBinding>& bindings,
        const evidence::AnalysisSnapshot& snapshot) const override;
    std::vector<VariableDiffEntry> diffCandidatePatch(
        const CandidatePatch& a, const CandidatePatch& b) const override;

private:
    OptimizationStage m_stage = OptimizationStage::StageB;  ///< 装配期定型的阶段面
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_VARIABLE_HPP
