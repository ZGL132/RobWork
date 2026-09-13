/**
 * @file   PolicyParsing.hpp
 * @brief  策略解析器与校验器——resolvePolicy 七段管线、IPolicyValidator
 *         （只校验不发布）与 IPolicyValidationContext（注入的修订闭包查询）。
 *
 * 设计依据：
 *   - units/policy.md §5.1（解析与发布管线——七段流程与 IPolicyValidationContext
 *     签名的唯一权威章节）、§5.2（校验规则与错误分类表——诊断码/严重级/处置/
 *     可确认性的全量反例来源）、§9.2（IPolicyValidator 接口契约——前置/后置/
 *     错误/线程/生命周期行）、§12 POL-T04 行（本任务产物）
 *   - 需求 ARC-05（策略单一权威——解析是"原始配置→已发布策略对象"的唯一
 *     通道）、ERR-01（诊断三轴正交、稳定诊断绑定对象、比较型三要素）、
 *     NFR-COR-02/03（确定性；不静默、不短路、不发明数值）
 *   - 任务契约 tasks/foundation/POL-T04.json（≙WP-07-T04）acceptance 1～2：
 *     ①POL-PARSE-1~6 错误分类表全量反例通过（不短路）；②O-10/P-POL-2 保守
 *     口径——默认解析仅附录 D 第 11 项（行程上限 4π，origin=DefaultAppendixD），
 *     无冻结默认项保持 nullopt＝显式不适用，不发明数值、不设第二默认
 *
 * 背景说明（本头在策略管线中的位置——第一读者须知）：
 *   RawPolicyInput（PolicyInput.hpp，POL-T03）是"可携带显示单位与未设置字段"
 *   的待解析输入；EngineeringPolicySet（PolicySet.hpp，POL-T02）是"已解析、
 *   已校验、已发布"的不可变策略对象。本头提供两者之间的**唯一**通道：
 *   resolvePolicy 七段管线（§5.1：①语法与 schema 校验 → ②规范化与单位校验 →
 *   ③默认值解析 → ④规则集检查 → ⑤适用范围验证 → ⑥内容身份生成 → ⑦发布）。
 *   谁在何时调用：请求方（域插件/execution 准备段/project 处理器）经④策略
 *   端口 IPolicyProvider::resolvePolicy（POL-T05 落地，含 (policyObject,
 *   ContentVersion) 记忆化）；策略编辑命令在 prepare 阶段经 IPolicyValidator
 *   预校验（SA-15 边界——无效输入就地阻止、不产生修订）。
 *
 * 纯函数契约（§5.1"解析器为纯函数"）：同 (RawPolicyInput 字节内容, 校验上下文
 * 应答) → 同结果（POL-ID-1 管线级）。无 I/O、无时间/随机源、无共享可变状态；
 * 诊断文本经固定格式化（"C" locale 数字格式），确定性 NFR-COR-02。
 *
 * 输入非法 vs 调用方违约（AGENTS.md 错误总纲在本头的落点——双轨分工）：
 *   - **输入非法**（策略内容不合法：阈值越域/规则冲突/对象不存在/版本未来…）
 *     的主轨是 PolicyParseResult.diagnostics（§5.2 错误分类表逐行——全量收集
 *     不短路，可恢复：调用方修正后重新解析）；
 *   - **调用方契约违约**（不经 PolicyCodec::decode / PairRule::make 装配即不可
 *     产生的非法实例：全零 policyObject、空 numericContractAnchor、结构非法的
 *     PairRule/ScopeTarget）＝ fail-fast——抛 PolicyError（同码面），**不**进
 *     诊断收集（这类实例不可能来自合法字节解码，静默转诊断会掩盖装配侧 bug）。
 *
 * 线程安全：本头全部实体为无状态纯函数/无状态对象（PolicyValidator 无数据
 * 成员），可重入，并发只读安全（§9.2"线程：同 §5.1 纯函数契约"行）。
 */

#ifndef SDURWS_IRD_POLICY_POLICYPARSING_HPP
#define SDURWS_IRD_POLICY_POLICYPARSING_HPP

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

namespace sdurws::ird::policy {

// =====================================================================
// IPolicyValidationContext——解析期注入的修订闭包查询（§5.1 原文签名；
// 适配器归 L5/请求方——§9.7 表"IPolicyValidationContext"行）。
// =====================================================================

/**
 * @brief 解析/校验期对"修订闭包事实"的只读查询接口（零编译依赖的注入面）。
 *
 * 为什么需要注入（§5.1）：对象存在性、对象角色、组定义都是**修订闭包内的
 * 事实**，权威归 project（存在性）与建模语义（角色）——policy 是纯函数，
 * 不持有任何模型/项目状态（N-3/N-4 边界），三类事实经本接口在解析⑤④步
 * 查询。适配器由请求方装配（§9.7：每**一次解析**一个实例——闭包在解析期间
 * 不可变，保证"同输入同应答→同结果"的确定性前提）。
 *
 * 实现纪律（§9.7 合法/非法调用行）：适配器只做只读转发——存在性/角色查询
 * 不得修改映射；组定义应答"组名是否已在本策略定义"（组定义数据的持有方是
 * 装配侧——RawPolicyInput v1 的 schema 不携带组定义字段，见 §7.2"扁平组"
 * 与本头 resolvePolicy 注释的落点登记）。
 *
 * 线程安全：实现须并发只读安全（§9.7 表"并发只读"行）；本头对它的使用
 * 仅为解析期只读查询。
 */
class IPolicyValidationContext {
public:
    virtual ~IPolicyValidationContext() = default;

    /**
     * @brief 对象在修订闭包内是否存在（§5.2 行 10"闭包内查无"的应答源）。
     *
     * @param object [in] 待核对对象身份（全零保留值由调用侧语义处理——本接口
     *               不预判，实现按闭包事实应答）
     * @return true＝闭包内存在；false＝不存在（对象已删除/跨修订混入）
     */
    virtual bool objectExists(core::ObjectId object) const = 0;

    /**
     * @brief 对象的建模角色（场景对象角色词表 token——RobotLink|Tool|Payload|
     *        EnvironmentObject|Workpiece，§6.1 SceneObjectRole 同源词表）。
     *
     * 用途：解析期 Role 展开等价判定——PairRule 的 Role 端与 Object 端是否
     * 覆盖同一对象类（Object 的角色 == Role 的 token ⇒ 两者展开集相交，
     * §5.2 行 8"经 Role/Group 展开的等价对"的可检子集）。
     *
     * @param object [in] 待查询对象身份
     * @return 角色 token（词表内的五个串之一）；对象不存在/无角色 → nullopt
     *        （不猜测——ARC-04 精神）
     */
    virtual std::optional<std::string> objectRole(core::ObjectId object) const = 0;

    /**
     * @brief 组名是否已在本策略定义（§5.1 接口注释"内部自洽性二次校验"）。
     *
     * 用途：解析④的组引用存在性核对——引用未定义组 → POLICY-RULE-CYCLE
     * （§5.2 行 9：组引用循环/引用未定义组共用本码）。组定义数据由装配侧
     * 持有（模板/项目对象侧的扁平对象组）；本应答是其存在性的单点事实源。
     *
     * @param groupName [in] 待核对组名（ScopeTarget::groupName 承载的串）
     * @return true＝已定义；false＝未定义
     */
    virtual bool groupDefined(std::string_view groupName) const = 0;
};

// =====================================================================
// PolicyParseResult——解析结果（§5.1 原文三字段）。
// =====================================================================

/**
 * @brief 策略解析结果（§5.1 管线的返回载体——值语义纯聚合）。
 *
 * 三字段不变式：
 *   - policy 非空 ⇔ diagnostics 中无 Error 级条目（§5.1⑦"仅当全部校验通过
 *     （Valid）非空"；Info 级条目随发布对象附带——validationDiagnostics）；
 *   - diagnostics 全量（不因首个错误短路——§5.2 表 2④ 精神）；诊断发射顺序
 *     确定性（管线段序 ①→②→③→④→⑤，段内按字段/规则承载序——NFR-COR-02）；
 *   - sourceVersion：来源对象内容版本（诊断定位用）。**resolvePolicy 恒返回
 *     nullopt**——对象字节的内容版本由 project 按**对象字节**编址（§4.1"两层
 *     身份区分"），RawPolicyInput 不携带、policy 不计算对象字节摘要（CR-02
 *     摘要单点在 PolicyCodec::contentIdentity 且输入是语义闭包投影，非对象
 *     字节）；该字段由④端口 Provider（POL-T05）在记忆化键 (policyObject,
 *     ContentVersion) 处填充——类型先行落位，填充点归其所有者（PA-1）。
 *
 * 线程安全：纯值。
 */
struct PolicyParseResult {
    /// 已发布策略对象（仅当全部校验通过非空；不可变值——调用方按值持有）。
    std::optional<EngineeringPolicySet> policy;
    /// 全量诊断（不短路；合法时仅含告知性条目）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// 来源对象内容版本（诊断定位用；resolvePolicy 恒 nullopt——见结构注释）。
    std::optional<core::ContentVersion> sourceVersion;
};

// =====================================================================
// resolvePolicy——七段解析管线（§5.1 唯一权威；POL-T04 主产物）。
// =====================================================================

/**
 * @brief 解析并（在全部校验通过时）发布策略对象（§5.1 管线的唯一实现入口）。
 *
 * 管线段序与发射顺序（诊断按此序全量收集，段内按字段/规则承载序——确定性）：
 *   - **契约违约复检（fail-fast，不入诊断）**：policyObject 全零、
 *     numericContractAnchor 空、任一 PairRule/ScopeTarget 结构非法
 *     （detail 谓词复检——同 PolicyCodec::encode 口径，见 Errors.hpp 总纲）；
 *   - **版本门（§5.2 行 2）**：schemaVersion 高于当前代 →
 *     POLICY-SCHEMA-VERSION-FUTURE（比较型：实际/期望代＋升级指引）；低于当前
 *     代 → POLICY-SCHEMA-VERSION-UNKNOWN。**命中即返回**：未来/未知代的字段
 *     布局不被当前代码认识，继续解释字段＝"前向猜测解析"（行 2 明文禁止，
 *     PM-06 同源只读拒绝）——此为行 2 处置语义，非"首错短路"（行 2④ 的
 *     不短路约束针对可共存的输入异常，不要求对未知布局做无意义解释）；
 *   - **① 语法与 schema 校验**：角色 token 词表核对（Role 端 token 不在
 *     RobotLink|Tool|Payload|EnvironmentObject|Workpiece 五值词表 →
 *     POLICY-SCHEMA-UNKNOWN-FIELD——词表外的 token 是"schema 未定义的键"的
 *     类型层活性检测面：拼写错误的角色会静默收窄必检集，必须拒绝而不忽略；
 *     其余字段为 C++ 强类型固定 schema，未知字段在类型层不可表达，字节层
 *     等价拒绝由 PolicyCodec::decode 精确耗尽契约承担——落点登记见单元卡
 *     v0.5 增量）；schemaVersion 已由版本门处理；
 *   - **② 规范化与单位校验**（逐阈值槽位，struct 序：safetyClearance →
 *     nearLimitRatio → conditionNumberWarning → finiteRotationTravelLimit）：
 *     非有限原文先判（→ POLICY-THRESHOLD-NON-FINITE，原文保留于诊断 cause 与
 *     RawPolicyInput——行 3）；显示单位经 core 唯一换算入口归一 SI（SA-12）：
 *     未注册 token/量纲不符/换算溢出 → POLICY-UNIT-MISMATCH（core convert
 *     抛错原文转发——行 6）；空 token＝按字段域默认 SI 单位解释（factor 1）；
 *     归一值经 PolicyThreshold::make 域窗校验，非正 →
 *     POLICY-THRESHOLD-NON-POSITIVE、越窗 → POLICY-THRESHOLD-OUT-OF-RANGE
 *     （行 4——比较型诊断：实际 SI 值/期望域界/字段 SI 单位，ERR-01）；
 *   - **③ 默认值解析（O-10/P-POL-2 保守口径——契约 acceptance 2）**：唯一
 *     冻结默认＝附录 D 第 11 项行程上限 4π rad——输入未提供时填入
 *     （origin=DefaultAppendixD）并发告知性诊断 POLICY-INFO-DEFAULT-APPLIED
 *     （§5.2 行 12/§9.6 唯一已登记 INFO 码）；nearLimitRatio/
 *     conditionNumberWarning 无冻结默认——保持 nullopt＝该警告检查显式不适用
 *     （§4.4/P-POL-2：不发明数值、不设第二默认；如需空缺语义走 P-POL-2 裁决，
 *     解析器不越权）；域集合 {Self,Environment,Tool} 的"默认解析"归模板装配
 *     侧（RawPolicyInput 的空集无法与"显式清空"区分，§5.1④"空域启用→非法"
 *     为解析期权威语义——落点登记见单元卡 v0.5 增量）；
 *   - **④ 规则集检查**（行 7/8/9）：重复规则——同一规则清单内"级别相同且
 *     对端覆盖等价"的条目 → POLICY-RULE-DUPLICATE（列出重复条目）；排除∩必检
 *     冲突——excludedPairs 与 mandatoryPairs 存在对端覆盖等价的规则对 →
 *     POLICY-RULE-CONFLICT（定位冲突对——结构上保证过滤不得隐藏必检，
 *     §7.2/D-06）；组引用——Group 端组名经 context.groupDefined 核对，未定义
 *     → POLICY-RULE-CYCLE（行 9；"组引用组/循环"在扁平组 schema 层不可表达，
 *     本检查为其纵深防御的活性面）；**对端覆盖等价**＝同 kind 同载荷（Object
 *     同 ID/Role 同 token/Group 同名）或 Object×Role 经 context.objectRole
 *     命中同角色类——经 Role 展开的等价对在解析期可检；Object×Group/
 *     Role×Group 的成员级交集需场景清单，归会话构建完整执行（§7.2 会话期
 *     同码复核——两层防御，落点登记见单元卡 v0.5 增量）；
 *   - **⑤ 适用范围验证**（行 10/11）：规则 Object 端与 applicability 三对象
 *     集逐一经 context.objectExists 核对，查无 → POLICY-SCOPE-OBJECT-MISSING
 *     （subject 绑定缺失对象 ID——POL-PARSE-6）；碰撞启用而 enabledDomains
 *     为空 → POLICY-APPLICABILITY-INVALID（§5.1④"无域可检"）；modes 为
 *     core::EvaluationMode 强类型枚举——"合法子集"由类型层保证（未知模式
 *     token 不可表达——落点登记同上）；
 *   - **⑥ 内容身份生成**：PolicyCodec::contentIdentity（语义闭包 canonical
 *     编码 → SHA-256——CR-02 摘要单点；发布路径唯一调用）；
 *   - **⑦ 发布**：EngineeringPolicySet::make（发布门——validationState=Valid；
 *     validationDiagnostics=全量诊断〔此时仅 Info〕）；碰撞规则对端按字典序
 *     规范化承载（§5.1②"消除顺序歧义"——与 codec 编码规范序同键定义，
 *     decode(encode(发布产物对应 Raw 输入)) 承载序稳定）。
 *
 * enabled=false 的策略（§4.3"可整体停用后恢复"）：阈值/规则/对象存在性检查
 * **照常执行**（"其余碰撞字段仍须完整合法"），仅"必填安全间距缺省"与"空域
 * 启用"两项以 enabled==true 为前提（间距检查/域启用语义随总开关停用）。
 *
 * @param input   [in] 待解析原始策略输入（调用方持有；本函数不修改——只读；
 *                通常来自 PolicyCodec::decode，或 modeling/io/ui 装配）
 * @param context [in] 修订闭包查询（调用方保证存活至本调用返回；每解析一个
 *                实例——§9.7；应答确定性是 POL-ID-1 的前提）
 * @return 解析结果（policy 非空 ⇔ 无 Error 级诊断；诊断全量；sourceVersion
 *         恒 nullopt——填充归 Provider/POL-T05，见 PolicyParseResult 注释）
 *
 * @throws PolicyError(PolicyErrorCode::PolicyObjectInvalid) policyObject 为
 *         全零保留值，或 numericContractAnchor 为空（调用方契约违约——fail-
 *         fast，不经诊断轨；合法解码产物不可能触发）
 * @throws PolicyError(PolicyErrorCode::PolicyObjectInvalid) 任一 PairRule/
 *         ScopeTarget 结构非法（reason 空/kind↔字段不一致——detail 谓词复检，
 *         同 PolicyCodec::encode 口径）
 *
 * 确定性：同 (input 字节内容, context 应答) → 同结果（POL-ID-1 管线级；无
 * I/O、无环境依赖）。线程安全：纯函数（可重入）。
 */
PolicyParseResult resolvePolicy(const RawPolicyInput& input,
                                const IPolicyValidationContext& context);

// =====================================================================
// 场景对象角色词表（§4.3/§6.1 SceneObjectRole 同源五值——解析①的核对表）。
// =====================================================================

/**
 * @brief 场景对象角色 token 冻结词表（建模语义所有、policy 消费——§4.3
 *        ScopeTarget 注释"RobotLink|Tool|Payload|EnvironmentObject|Workpiece"
 *        与 §6.1 SceneObjectRole 枚举同源；顺序＝枚举声明序）。
 *
 * @return 五个 token（静态存储期；调用方无需释放；顺序稳定——测试与诊断
 *         文案依赖）
 */
std::vector<std::string_view> sceneObjectRoleTokens();

// =====================================================================
// IPolicyValidator——只校验不发布（§9.2 原文契约）＋唯一实现。
// =====================================================================

/**
 * @brief 策略校验器接口（§9.2 原文签名——编辑命令 prepare 预校验与导入装配
 *        校验的共用入口）。
 *
 * 与 resolvePolicy 的分工（§9.2"非法调用"行）：validate 返回全量诊断、**无
 * 发布副作用**（不产生策略对象——编辑命令 prepare 阶段用，SA-15 边界）；
 * 需要策略对象必须走 resolvePolicy（其返回类型携带 policy——类型层防误用）。
 * 两者共享同一校验管线（诊断逐字一致——测试钉住）。
 *
 * 线程安全：实现无状态（§9.2"生命周期：无状态，随 provider 共享"）——可
 * 全进程共享一个实例并发调用。
 */
class IPolicyValidator {
public:
    virtual ~IPolicyValidator() = default;

    /**
     * @brief 只校验不发布：返回全量诊断（不短路）；合法 ⇔ 无 Error 级条目。
     *
     * @param input   [in] 待校验原始策略输入（同 resolvePolicy 契约）
     * @param context [in] 修订闭包查询（同 resolvePolicy 契约）
     * @return 全量诊断（发射顺序与 resolvePolicy 一致——确定性）
     *
     * @throws 同 resolvePolicy 的 fail-fast 面（调用方契约违约——结构非法
     *         实例/全零对象身份/空锚）
     */
    virtual std::vector<core::DiagnosticRecord>
    validate(const RawPolicyInput& input, const IPolicyValidationContext& context) const = 0;
};

/**
 * @brief IPolicyValidator 唯一实现（无状态——validate 转调与 resolvePolicy
 *        共享的管线校验段，保证两入口诊断逐字一致）。
 *
 * 直接构造即可（无配置面）；随 provider 共享（§9.2 生命周期行）。
 * 线程安全：无数据成员，可重入。
 */
class PolicyValidator final : public IPolicyValidator {
public:
    PolicyValidator() = default;

    /// @copydoc IPolicyValidator::validate
    std::vector<core::DiagnosticRecord>
    validate(const RawPolicyInput& input,
             const IPolicyValidationContext& context) const override;
};

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_POLICYPARSING_HPP
