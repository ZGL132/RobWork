/**
 * @file   PolicyInput.hpp
 * @brief  策略原始输入（RawPolicyInput）与策略编码器（PolicyCodec）——
 *         canonical 编码、往返编解码与语义内容身份（contentIdentity）。
 *
 * 设计依据：
 *   - units/policy.md §3.1（本头产物行：RawPolicyInput、PolicySchema 常量、
 *     PolicyCodec（encode/decode/contentIdentity））、§5.1（解析管线——
 *     RawPolicyInput 为管线入口类型）、§5.3（canonical 编码与内容身份——
 *     本头编码规则的唯一权威章节）、§12 POL-T03 行（本任务产物）
 *   - 需求 CON-05（内容身份＝缓存/切片失效判据）、CON-06（快照存已解析策略
 *     身份；跨入口一致）、NFR-COR-02（确定性：同输入同字节同身份）、
 *     NFR-COR-03（非有限/非法不静默）、SA-12（单位换算唯一权威在 core）、
 *     UX-08/KIN-12（显示单位不入身份——POL-ID-3）、附录 D 第 12 项
 *     （身份精确等值——浮点近似相等禁入身份，POL-ID-5）
 *   - 任务契约 tasks/foundation/POL-T03.json（≙WP-07-T03）acceptance 1～3：
 *     ①POL-ID-1~5（canonical 往返＋位模式＋排序无关性）；②近似相等禁入
 *     身份；③CR-02 处置约束——contentIdentity 对策略语义内容经 core
 *     ContentDigester 摘要（core.md §4.2 责任边界的 policy 侧承接），
 *     不复制摘要算法、不私设第二哈希路径
 *
 * 背景说明（本头在策略管线中的位置——第一读者须知）：
 *   EngineeringPolicySet（PolicySet.hpp，POL-T02）是"已解析、已校验、已发布"
 *   的策略对象；本头提供它的"上游两端"：
 *   1. RawPolicyInput——待解析输入（§4.1"策略配置"概念）。解析管线
 *      （resolvePolicy，POL-T04）第①步消费它；它可携带显示单位与未设置
 *      字段（§5.1 管线图原文），由 PolicyCodec 从项目对象字节解码而来
 *      （modeling/io 导入装配的 RawPolicyInput 同样进入本管线，MDL-04）。
 *   2. PolicyCodec——对象字节的产出/消费方（§5.3"与 project 分工"行：
 *      磁盘编址、事务、升级器归 project，字节对 project 不透明）＋语义
 *      内容身份的计算原语（解析管线第⑥步"PolicyCodec 语义闭包 canonical
 *      编码 → SHA-256 → core::ContentIdentity"的落点）。
 *
 *   身份的两层区分（§4.1"六个策略/身份概念"——防混用）：
 *   - 对象内容版本（core::ContentVersion）：project 按**对象字节**编址
 *     （磁盘侧身份，policy 不计算）；
 *   - 策略内容身份（core::ContentIdentity）：policy 对**工程语义**计算
 *     （本头 contentIdentity——§5.3）。"同语义不同编码→同身份；显示单位/
 *     字段排序/来源标注不影响"（§4.1 原文）。
 *
 * CR-02 处置（契约 acceptance 3——本头的摘要边界声明）：
 *   摘要算法唯一实现归 core（ContentDigester，SHA-256，core.md §4.2/D-05）。
 *   本单元**不复制、不私设**任何第二哈希路径：全单元唯一的摘要调用点是
 *   PolicyCodec::contentIdentity（PolicyInput.cpp 实现），其摘要输入恒为
 *   encodeSemanticProjection 产出的语义闭包投影编码。编码器排除字段登记
 *   （foundation-api-diff.md §CR-02）：PolicyCodec 排除
 *   policyObject/origin/校验状态/诊断/兼容注记——仅语义闭包字段
 *   {schemaVersion, collision, jointThresholds, applicability,
 *   numericContractAnchor}（§4.2 字段表语义闭包集合）入身份。
 *
 * 浮点纪律（§5.3/evidence D-06 同源——POL-ID-5 的机制层）：
 *   全部 double 经 IEEE754 双精度 8 字节位模式承载（round-trip 精确——
 *   同值同字节）；NaN/±Inf 在编码入口拒绝（canonicalF64）且接收端对非
 *   有限位型拒绝（parseCanonicalF64——双端纪律，runtime RT-Codec 同款）。
 *   **严禁浮点近似相等作为身份键**（近似关系无传递性——1e-15 级差异必须
 *   产生不同身份；容差比较归 core closeWithin，只用于数值校验面，永不
 *   进入身份计算）。
 *
 * 确定性来源（NFR-COR-02）：字段按规范序编码（集合升序去重、无序对排序、
 * 规则列表排序——详见 PolicyCodec 类注释"编码规范序"）；大端字节序；长度
 * 前缀；presence 字节显式编码可选值（缺失≠空值≠零）；无 locale 依赖、无
 * I/O、无时间/随机源——同输入同字节（worker/main 跨进程同身份）。
 *
 * 线程安全：本头全部实体为纯值类型/无状态静态函数（无可变共享状态），
 * 可重入，并发只读安全（§5.3"线程安全（可重入）"行）。
 */

#ifndef SDURWS_IRD_POLICY_POLICYINPUT_HPP
#define SDURWS_IRD_POLICY_POLICYINPUT_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>

#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

namespace sdurws::ird::policy {

// =====================================================================
// PolicySchema——策略编码格式常量（§3.1 产物行"PolicySchema 常量"；
// §4.1"策略 schema 版本"概念与 §5.3"版本化"规则的常量载体）。
// =====================================================================

/**
 * @brief 策略编码格式的冻结常量集（magic/形态字节/schema 代）。
 *
 * 单一事实源：schema 代复用 PolicySet.hpp 的 kPolicySchemaVersionCurrent
 * （v0.3 登记的先行落位点——本头落地后按其注释归位为"复用而非重复定义"）。
 * magic "IRDPOL1" 与排除字段登记同源（foundation-api-diff.md §CR-02：
 * 四家编码器 magic 互异——IRDSNAP1/IRDSLCE1/IRDCANO/IRDPOL1）。
 *
 * 版本化纪律（§5.3）：schemaVersion 写入编码且参与身份——编码升版＝全体
 * 策略身份变化（破坏性，走设计变更评审；次版本追加可选字段除外——旧编码
 * 可解析、新字段按未设置处理，§4.6）。
 */
struct PolicySchema {
    /// 当前 schema 代（＝kPolicySchemaVersionCurrent＝1；复用 PolicySet.hpp）。
    static constexpr std::uint32_t currentVersion = kPolicySchemaVersionCurrent;
    /// 编码 magic（7 字节 ASCII；CR-02 登记原文 "IRDPOL1"）。
    static constexpr std::string_view magic = "IRDPOL1";
    /// 形态字节：full＝对象字节（磁盘持久化形态，往返载体——decode 唯一接受）。
    static constexpr std::uint8_t formFull = 0x00;
    /// 形态字节：语义闭包投影（contentIdentity 的摘要输入；非往返载体——
    /// decode 拒绝，同 evidence SliceCodec baseline-projection 先例）。
    static constexpr std::uint8_t formSemanticProjection = 0x01;
};

// =====================================================================
// RawPolicyInput——待解析输入（§5.1 管线入口；可携带显示单位与未设置字段）。
// =====================================================================

/**
 * @brief 原始阈值输入（§4.4 PolicyThreshold 的"未解析"对偶形态）。
 *
 * 为什么不直接用 PolicyThreshold：PolicyThreshold 构造时校验有限性与域窗
 * （make() 唯一入口——NFR-COR-03 的类型层强制），而 RawPolicyInput 必须
 * **保留原文**——阈值 NaN/±Inf/越域的原始值在此驻留，供解析期生成比较型
 * 诊断（§5.2 行 3/4："原文保留于 RawPolicyInput"——NFR-COR-03 不静默转
 * 0/默认）。若此处复用 PolicyThreshold，非法输入在装配期即被类型层拒绝，
 * 用户将看不到自己输错了什么（违背 §5.2 诊断定位语义）。
 *
 * 字段语义：
 *   - value：原始数值（显示单位制下的字面值——尚未经单位换算；NaN/±Inf
 *     允许驻留，encode 阶段拒绝，见 PolicyCodec::encode）；
 *   - unitToken：显示单位 token（core UnitToken 词表形态的字符串，如
 *     "m"/"mm"/"rad"）。词表核对与 SI 归一归解析管线②（经 core convert
 *     ——SA-12 换算唯一权威）；本类型只搬运字节，不校验词表（未注册
 *     token 的 POLICY-UNIT-MISMATCH 诊断是解析期职责——PA-1 不越权）。
 *     空 token 合法承载（＝输入未提供单位；解析期按字段域默认单位核对）。
 *
 * 显示单位不入身份（POL-ID-3/UX-08）：value/unitToken 只进入 full 形态
 * 编码（往返保真）；语义闭包投影编码只承载归一后的 SI 真值（无单位
 * 字段）——同一语义在不同显示单位下的输入产生同一身份。
 *
 * 线程安全：纯值。
 */
struct RawThresholdInput {
    /// 原始数值（显示单位制字面值；单位由 unitToken 承载；可 NaN/±Inf——
    /// 供解析期诊断保留原文；encode 拒绝非有限值）。
    double value = 0.0;
    /// 显示单位 token（core UnitToken 词表形态字符串；词表核对归解析②）。
    std::string unitToken;

    /// 逐字段精确等值（承载序；byte 级比较无容差——附录 D 第 12 项）。
    bool operator==(const RawThresholdInput& o) const
    {
        return value == o.value && unitToken == o.unitToken;
    }
    bool operator!=(const RawThresholdInput& o) const { return !(*this == o); }
};

/**
 * @brief 碰撞规则子模型的原始输入形态（§4.3 CollisionRules 的对偶）。
 *
 * 与发布形态（CollisionRules）的差异：
 *   - safetyClearance 为 RawThresholdInput（原始值＋显示单位；可携带
 *     NaN 等待解析诊断）而非 PolicyThreshold（构造时已校验 SI 真值）；
 *   - 其余字段（enabled/enabledDomains/excludeAdjacentLinksByDefault/
 *     mandatoryPairs/excludedPairs）类型与语义同 §4.3 原文——布尔/
 *     枚举/规则结构无单位语义，直接复用 PolicySet.hpp 类型（单一词表，
 *     不设第二套结构）。
 *
 * 默认值说明：与 CollisionRules 相同（enabled/excludeAdjacent 默认 true；
 * enabledDomains 空集——"默认解析 {Self,Environment,Tool}"是解析管线③
 * 职责，本类型不预填——避免第二默认源，§4.3 注释同款口径）。
 *
 * 线程安全：纯值聚合。
 */
struct RawCollisionInput {
    /// 碰撞域总开关（§4.3：Quick 预览缺碰撞证据 ≠ enabled=false——KIN-13）。
    bool enabled = true;
    /// 启用域（空集＝解析③默认解析前/显式清空；§5.1④ 空域启用校验归解析）。
    std::vector<CollisionDomain> enabledDomains;
    /// 安全间距原始输入（SI 归一前；nullopt＝未设置——§5.2 行 5 必填
    /// 校验归解析/发布门，本类型不强制）。
    std::optional<RawThresholdInput> safetyClearance;
    /// 相邻连杆默认过滤（§4.3：模型事实非策略发明）。
    bool excludeAdjacentLinksByDefault = true;
    /// 必须检测对（§4.3；重复/冲突检查归解析④——本类型按原样承载）。
    std::vector<PairRule> mandatoryPairs;
    /// 允许忽略对（理由非空等结构约束由 PairRule::make/发布门保证）。
    std::vector<PairRule> excludedPairs;

    /// 逐字段精确等值（列表按承载序——规范化语义见 PairRule 注释同款）。
    bool operator==(const RawCollisionInput& o) const
    {
        return enabled == o.enabled && enabledDomains == o.enabledDomains
            && safetyClearance == o.safetyClearance
            && excludeAdjacentLinksByDefault == o.excludeAdjacentLinksByDefault
            && mandatoryPairs == o.mandatoryPairs && excludedPairs == o.excludedPairs;
    }
    bool operator!=(const RawCollisionInput& o) const { return !(*this == o); }
};

/**
 * @brief 关节限位/行程阈值子模型的原始输入形态（§4.4 JointThresholds 对偶）。
 *
 * 全部阈值槽位为 std::optional<RawThresholdInput>——nullopt＝输入未提供
 * （§4.5 四态承载第 1 态"未设置"）：有冻结默认的槽位（行程上限 4π——
 * 附录 D 第 11 项唯一冻结默认）由解析③填入（origin=DefaultAppendixD）；
 * 无冻结默认的槽位（nearLimitRatio/conditionNumberWarning——P-POL-2）
 * 保持 nullopt＝该检查显式不适用，不发明数值。
 *
 * 发布形态中 finiteRotationTravelLimit 为值字段（解析③填入后必有值）；
 * Raw 形态统一 optional 表达"未提供"——presence 编码由 codec 显式承载
 * （缺失≠空值≠零，CR-02 纪律）。
 *
 * 线程安全：纯值聚合。
 */
struct RawJointThresholdsInput {
    /// 近限位比原始输入（无量纲，(0,1]；域窗校验归解析/PolicyThreshold::make）。
    std::optional<RawThresholdInput> nearLimitRatio;
    /// 条件数警告阈值原始输入（无量纲，[1,+∞)）。
    std::optional<RawThresholdInput> conditionNumberWarning;
    /// 有限限位旋转关节行程上限原始输入（SI rad 语义，(0,+∞)；nullopt＝
    /// 未设置——解析③填 4π DefaultAppendixD）。
    std::optional<RawThresholdInput> finiteRotationTravelLimit;
    /// 行程校验开关（true=默认执行 MDL-06④ 策略校验）。
    bool travelLimitCheckEnabled = true;

    /// 逐字段精确等值（承载序）。
    bool operator==(const RawJointThresholdsInput& o) const
    {
        return nearLimitRatio == o.nearLimitRatio
            && conditionNumberWarning == o.conditionNumberWarning
            && finiteRotationTravelLimit == o.finiteRotationTravelLimit
            && travelLimitCheckEnabled == o.travelLimitCheckEnabled;
    }
    bool operator!=(const RawJointThresholdsInput& o) const { return !(*this == o); }
};

/**
 * @brief 待解析策略输入（§5.1 管线入口类型——"策略配置"概念的载体）。
 *
 * 生命周期与来源：由 PolicyCodec::decode 从项目对象字节产出（§5.1 管线
 * 图"RawPolicyInput：PolicyCodec 解码自项目对象字节"），或由 modeling/io
 * 导入装配（MDL-04 自碰撞配置等）、ui 编辑表单装配。值语义纯聚合（无
 * 不变量强制——合法性校验是解析管线①~⑤的职责，本类型按原样承载）。
 *
 * 字段分组与身份归属（§4.2 字段表的"解析前"对偶——语义闭包集合不变）：
 *   - 语义闭包（解析归一后经 contentIdentity 入身份）：schemaVersion、
 *     collision、jointThresholds、applicability、numericContractAnchor；
 *   - 管理与审计（入对象字节、**不入身份**——CR-02 排除字段登记）：
 *     policyObject、origin、compatibilityNotes；
 *   - 校验状态与诊断不在本类型（它们是解析的**输出**——PolicyParseResult/
 *     EngineeringPolicySet 的字段，POL-T04；对象字节亦不编诊断——发布
 *     对象的告知性诊断由重解析确定性再生）。
 *
 * "以 RawPolicyInput 直接充当快照策略身份"在类型层不可表达（§4.6 非法
 * 用法行）：本类型无 contentIdentity 字段——身份只经解析管线对发布形态
 * 计算（未解析对象无内容身份）。
 *
 * 等值语义：operator== 为承载序逐字段精确比较（与 EngineeringPolicySet
 * 同款——"对象逐字段相等"，POL-ID-1 观测点）。编码层对集合/规则列表的
 * 规范化排序（POL-ID-2）意味着 decode(encode(x))==x 在 x 已规范化
 * （解析②产物形态）时逐字段成立；未规范化输入的往返以"规范化后相等"
 * 成立（测试两侧均钉）。承载序比较不承担无序化语义（PairRule 注释同款
 * 先例——避免第二套排序权威）。
 *
 * 线程安全：纯值聚合。
 */
struct RawPolicyInput {
    /// 策略编码格式代号（§4.1；写入编码且参与身份——§5.3 版本化）。
    std::uint32_t schemaVersion = PolicySchema::currentVersion;
    /// 目标策略对象身份（project 分配；**不入身份**——CR-02 排除字段；
    /// full 形态编码承载之，解码产物携带——解析期对象存在性核对（§5.2
    /// 行 10）与记忆化键（POL-T05）需要它）。
    core::ObjectId policyObject;

    // ---- 语义闭包块（参与内容身份——经解析归一后） ----
    /// 碰撞规则原始输入（§4.3 对偶）。
    RawCollisionInput collision;
    /// 关节限位/行程阈值原始输入（§4.4 对偶）。
    RawJointThresholdsInput jointThresholds;
    /// 适用范围（§4.2.1——直接复用发布形态类型；modes 枚举强类型即词表）。
    PolicyApplicability applicability;
    /// 数值契约基线锚（§4.2：本策略默认值所依据的附录 D 版本；参与内容
    /// 身份与兼容判定；默认"appendixD@v1.16"）。
    std::string numericContractAnchor = std::string{kNumericContractAnchorDefault};

    // ---- 管理与审计块（入字节、不入身份——CR-02 排除字段） ----
    /// 创建来源（§4.2 origin 行：Template/Imported/UserEdited/SystemDefault）。
    PolicyOrigin origin;
    /// 向后兼容与迁移说明（§4.2：自由文本；迁移执行归 project 升级器）。
    std::optional<std::string> compatibilityNotes;

    /// 逐字段精确等值（承载序——见类注释等值语义）。
    bool operator==(const RawPolicyInput& o) const
    {
        return schemaVersion == o.schemaVersion && policyObject == o.policyObject
            && collision == o.collision && jointThresholds == o.jointThresholds
            && applicability == o.applicability
            && numericContractAnchor == o.numericContractAnchor && origin == o.origin
            && compatibilityNotes == o.compatibilityNotes;
    }
    bool operator!=(const RawPolicyInput& o) const { return !(*this == o); }
};

// =====================================================================
// PolicyCodec——策略编码器（§5.3 canonical 编码与内容身份的唯一实现点）。
// =====================================================================

/**
 * @brief 策略编码器：对象字节编解码（encode/decode）、语义闭包投影编码
 *        （encodeSemanticProjection）与内容身份计算（contentIdentity）。
 *
 * 全静态无状态纯函数（§5.3"实现"行：纯函数；同输入同字节；线程安全
 * （可重入）；无 I/O）。本类是 §5.3 编码规则的**唯一**实现点——任何其他
 * 代码不得复制其布局逻辑（NFR-MNT-03 单一权威）。
 *
 * 编码总纲（§5.3 逐行＋CR-02 四家编码器一致纪律）：
 *   - 确定性二进制：magic "IRDPOL1"＋字段按规范序（非输入序）；长度前缀；
 *     大端；无填充；
 *   - 可选值：presence 字节显式编码（0＝缺失/1＝存在——缺失≠空值≠零，
 *     NFR-COR-03）；
 *   - 浮点：IEEE754 双精度 8 字节位模式（canonicalF64）；NaN/±Inf 编码
 *     入口拒绝＋接收端非有限位型拒绝（双端纪律）；
 *   - 单位：语义闭包投影内为 SI 真值；RawPolicyInput 的显示单位只在
 *     full 形态承载（解析期归一——显示单位不入身份，POL-ID-3/SA-12）；
 *   - schemaVersion 写入编码且参与身份（§5.3 版本化）；
 *   - 摘要唯一经 core ContentDigester（CR-02——contentIdentity 是全单元
 *     唯一摘要调用点，不私设第二哈希路径）。
 *
 * 双形态设计（CR-02"身份排除项"登记的机械落点）：
 *   - formFull（0x00）＝对象字节：完整编码 RawPolicyInput（语义闭包原始
 *     形态＋显示单位＋policyObject/origin/兼容注记）。往返载体——磁盘
 *     持久化经 project（字节对 project 不透明）；decode 唯一接受的形态。
 *   - formSemanticProjection（0x01）＝语义闭包投影：仅语义闭包字段的
 *     归一形态（SI 真值；无显示单位/管理/审计字段）。**contentIdentity
 *     的唯一摘要输入**；非往返载体（decode 拒绝——它不携带往返所需的
 *     管理字段，同 evidence SliceCodec baseline-projection 先例）。
 *
 * 编码规范序（POL-ID-2"排序无关性"的机制定义；解析管线②的归一口径
 * 与此一致——codec 自足排序，不依赖调用方先排序）：
 *   - 集合字段（enabledDomains/modes/modelObjects/taskObjects/
 *     caseObjects）：升序排序＋去重（枚举按枚举值序、Id128 按 bytes
 *     字节字典序）——集合语义（成员相同即同语义）；
 *   - PairRule 无序对：{first,second} 按 ScopeTarget 规范键升序摆放
 *     （{a,b} 与 {b,a} 同编码——无序对语义，§4.3）；
 *   - PairRule 列表（mandatoryPairs/excludedPairs）：按规范化后的
 *     (first, second, level, reason) 字典序升序——列表承载序不进入身份；
 *   - ScopeTarget 规范键：(kind, 载荷)——kind 枚举序（Object<Role<Group）；
 *     kind 内 Object 按 object.bytes 字节序、Role 按 roleToken 字节
 *     字典序、Group 按 groupName 字节字典序（无 locale 比较）。
 *
 * 错误语义（AGENTS.md 错误总纲在本类的落点）：
 *   - encode/contentIdentity 的失败＝调用方契约违约——fail-fast 抛
 *     PolicyError（无效对象身份/未来或未知 schema 代/非有限阈值/结构
 *     非法等）；
 *   - decode 的失败＝字节损坏（§9.2 validate 行"字节损坏在解码期报错"）
 *     ——抛 PolicyError(EncodingInvalid)（POL-T03 表尾追加码）；版本
 *     代违约例外走 SchemaVersionFuture/Unknown（PM-06 只读拒绝语义）。
 *   - 本类不抛其他异常类型；不吞错、不静默降级。
 */
class PolicyCodec {
public:
    // ---- full 形态：对象字节编解码（往返载体） ----

    /**
     * @brief 将原始策略输入编码为对象字节（full 形态——磁盘持久化形态）。
     *
     * 编码前对集合/规则列表执行规范化排序（见类注释"编码规范序"）——
     * 输入字段排序变化不改变输出字节（POL-ID-2）；可选值以 presence
     * 字节显式编码（缺失≠空值≠零）。
     *
     * @param input [in] 原始策略输入（调用方持有；本函数不修改——只读引用；
     *              显示单位/未设置字段按原样承载）
     * @return full 形态对象字节（确定性——同输入同字节；NFR-COR-02）
     *
     * @throws PolicyError(SchemaVersionFuture)      input.schemaVersion 大于
     *         当前代（未来代实例在当前程序中不可产生——发布门同款核对）
     * @throws PolicyError(SchemaVersionUnknown)     input.schemaVersion 小于
     *         当前代（未知旧代）
     * @throws PolicyError(PolicyObjectInvalid)      policyObject 无效（全零
     *         保留值——对象字节必须挂在已分配的策略对象上）；或
     *         numericContractAnchor 为空（§4.2 必填列）；或任一 PairRule/
     *         ScopeTarget 结构非法（kind↔字段不一致/reason 空——复用
     *         PolicySet.hpp detail 谓词，单一事实源）
     * @throws PolicyError(ThresholdNonFinite)       任一阈值槽位的原始数值为
     *         NaN/±Inf（§5.2"编码入口拒绝"——CR-02 浮点纪律。注意：非有限
     *         值允许**驻留** RawPolicyInput 供解析诊断保留原文，但无效策略
     *         不产生修订（SA-15 prepare 阻断），故要求持久化即契约违约）
     */
    static std::vector<std::uint8_t> encode(const RawPolicyInput& input);

    /**
     * @brief 从对象字节解码出原始策略输入（encode 的严格逆——往返入口）。
     *
     * 仅接受 full 形态（语义闭包投影编码非往返载体——传入即拒绝）。解码
     * 逐字段校验字节契约（magic/形态/版本/长度前缀/枚举词表/位模式有限性/
     * 精确耗尽）——任何违约即拒绝（不静默截断/跳过——NFR-COR-03）。
     *
     * @param encoding [in] encode 的输出（或同规范的历史字节——schema 代
     *                 由字节内 schemaVersion 决定校验口径）
     * @return 原始策略输入（规范化形态——集合/规则列表为编码规范序；
     *         已规范化输入满足 decode(encode(x))==x，POL-ID-1）
     *
     * @throws PolicyError(EncodingInvalid)          字节契约违约：长度不足/
     *         magic 不符/形态非 full/载荷长度与实际不符/枚举值越表/
     *         非有限位模式/字符串载荷越界/载荷未精确耗尽
     * @throws PolicyError(SchemaVersionFuture)      字节内 schemaVersion 大于
     *         当前代（未来版本不前向猜测解析——PM-06 同源）
     * @throws PolicyError(SchemaVersionUnknown)     字节内 schemaVersion 小于
     *         已知最低代
     * @throws PolicyError(PolicyObjectInvalid)      解码出的 policyObject 为
     *         全零保留值（无效实例——保留值纪律）
     */
    static RawPolicyInput decode(const std::vector<std::uint8_t>& encoding);

    // ---- 语义闭包投影：身份摘要输入（CR-02 边界的可见化原语） ----

    /**
     * @brief 编码语义闭包投影（formSemanticProjection——contentIdentity
     *        的唯一摘要输入；公开以供测试手工重算摘要钉住 CR-02 边界，
     *        与 POL-T04 解析管线⑥复用同一字节形态）。
     *
     * 投影只承载语义闭包字段 {schemaVersion, collision, jointThresholds,
     * applicability, numericContractAnchor}（§4.2 语义闭包集合——CR-02
     * 排除字段登记的机械落实）：policyObject/origin/校验状态/诊断/兼容
     * 注记**结构性地不存在**于本编码（无字段槽位）；阈值只编码 SI 真值
     * （无显示单位槽位——POL-ID-3）；阈值只编码数值（无来源/域槽位——
     * 来源标注不影响身份（§4.1"来源标注不影响"），评估行为仅消费数值，
     * 数值域由字段位置隐含（PolicySet.hpp v0.3 ②同款口径））。
     *
     * @param schemaVersion         [in] schema 代（须为当前代——身份只对
     *                              当前代码认识的代计算）
     * @param collision             [in] 已归一碰撞规则（SI 真值——解析②
     *                              产物形态；结构须合法）
     * @param jointThresholds       [in] 已归一阈值子模型（PolicyThreshold
     *                              均经 make() 域校验——siValue 有限）
     * @param applicability         [in] 适用范围
     * @param numericContractAnchor [in] 数值契约锚（非空）
     * @return 语义闭包投影编码（确定性；规范化排序同 full——见类注释）
     *
     * @throws PolicyError(SchemaVersionFuture/Unknown)  schemaVersion 非
     *         当前代（调用方契约违约——fail-fast）
     * @throws PolicyError(PolicyObjectInvalid)          numericContractAnchor
     *         为空；或任一 PairRule/ScopeTarget 结构非法（detail 谓词复用）
     * @throws PolicyError(ThresholdNonFinite)           任一 siValue 非有限
     *         （canonicalF64 编码入口拒绝——纵深防御；正常经 make() 构造
     *         的阈值不可能触发）
     */
    static std::vector<std::uint8_t>
    encodeSemanticProjection(std::uint32_t schemaVersion,
                             const CollisionRules& collision,
                             const JointThresholds& jointThresholds,
                             const PolicyApplicability& applicability,
                             std::string_view numericContractAnchor);

    // ---- 内容身份（解析管线⑥的落点；CON-05/06 的 policy 侧承接） ----

    /**
     * @brief 计算策略语义内容身份（§5.3：语义闭包 canonical 编码 →
     *        SHA-256 → core::ContentIdentity）。
     *
     * CR-02 处置（契约 acceptance 3）：本函数是**全单元唯一的摘要调用
     * 点**——摘要算法唯一经 core::ContentDigester（SHA-256，D-05），本
     * 单元不复制摘要算法、不私设第二哈希路径；摘要输入恒为
     * encodeSemanticProjection 的输出（同函数内直接产出——两者不可独立
     * 漂移）。
     *
     * 调用时机（§5.1⑥）：解析管线对**通过校验并发布**的策略计算（§4.1：
     * "内容身份只对通过校验并发布的策略计算"——本函数是纯原语，"只在
     * 发布态调用"的纪律由解析管线/发布门执行；对未校验输入调用本函数
     * 属调用方契约违约，产出的身份不得对外使用）。同语义闭包输入恒得
     * 同身份（确定性——NFR-COR-02）；进入依赖切片与缓存键（CON-06）。
     *
     * @param schemaVersion         [in] schema 代（须为当前代）
     * @param collision             [in] 已归一碰撞规则（同
     *                              encodeSemanticProjection 契约）
     * @param jointThresholds       [in] 已归一阈值子模型
     * @param applicability         [in] 适用范围
     * @param numericContractAnchor [in] 数值契约锚（非空）
     * @return 内容身份（非全零——SHA-256 对固定 magic 前缀编码不可能为
     *         全零；isValid() 恒 true）
     *
     * @throws 同 encodeSemanticProjection（本函数先产出投影字节再摘要——
     *         校验序一致）
     */
    static core::ContentIdentity
    contentIdentity(std::uint32_t schemaVersion,
                    const CollisionRules& collision,
                    const JointThresholds& jointThresholds,
                    const PolicyApplicability& applicability,
                    std::string_view numericContractAnchor);

    // ---- canonical 浮点原语（§5.3 浮点行在 policy 侧的唯一实现点） ----

    /**
     * @brief 有限 double → IEEE754 双精度 8 字节位模式（大端）。
     *
     * §5.3 浮点行在 policy 侧的唯一实现点（evidence canonicalF64/runtime
     * RT-Codec 同款双端纪律——CR-02 只约束摘要算法唯一归 core，浮点编码
     * 原语按单元本地实现，四家纪律一致）。位模式承载保证 round-trip
     * 精确（同值同字节——POL-ID-5 的机制层）。
     *
     * @param value [in] 待编码值；须有限（NaN/±Inf 拒绝——非有限值无稳定
     *              位模式语义，进身份会破坏确定性）
     * @return 8 字节大端位模式
     *
     * @throws PolicyError(ThresholdNonFinite) value 为 NaN/±Inf
     */
    static std::array<std::uint8_t, 8> canonicalF64(double value);

    /**
     * @brief 8 字节大端位模式 → double（canonicalF64 的严格逆——接收端）。
     *
     * 接收端复核（双端纪律另一半）：非有限位型（NaN/±Inf）拒绝——手工
     * 构造/传输损坏的位型在此暴露为异常，而非静默进入身份计算。
     *
     * @param bytes [in] 8 字节大端位模式（调用方保证可解引用 8 字节——
     *              指针指向的缓冲区须≥8 字节；本函数不接管所有权）
     * @return 还原的有限 double（round-trip 精确——
     *         canonicalF64(parseCanonicalF64(b)) 逐位还原）
     *
     * @throws PolicyError(EncodingInvalid) 位型非有限（NaN/±Inf 接收端拒绝）
     */
    static double parseCanonicalF64(const std::uint8_t* bytes);
};

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_POLICYINPUT_HPP
