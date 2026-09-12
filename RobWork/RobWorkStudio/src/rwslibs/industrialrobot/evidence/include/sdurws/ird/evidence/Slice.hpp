/**
 * @file   Slice.hpp
 * @brief  输入切片（InputSlice）——评估实际消费依赖的冻结清单与双层内容
 *         身份（sliceId＝缓存键与失效判据；inputBaselineId＝比较基准与
 *         "同一冻结输入复评"凭据），及其组装（SliceBuilder，§4.2.3②冻结
 *         协议）与规范编码（SliceCodec，IRDSLCE1 双形态＋canonical 浮点
 *         位模式原语）。
 *
 * 设计依据：
 *   - units/evidence.md §4.2.2（InputSlice 字段表：evaluationKey/契约版本
 *     进身份、entries (kind,key) 字典序 ≥1、sliceId/inputBaselineId/
 *     snapshotId）、§4.2.3（声明—解析—冻结协议：②冻结期＝SliceBuilder
 *     解析对账＋Object 条目快照子集校验）、§4.2.4（改求解预算复评——
 *     新 sliceId 旧 inputBaseline 的直接消费场景）、§5.1（双层身份表：
 *     sliceId＝SliceCodec 全部冻结条目＋Environment 版本要素的 SHA-256；
 *     inputBaselineId＝SliceCodec 的子集——基准类条目＋快照身份块；D-04
 *     决策）、§5.2（canonical 编码规则表：magic IRDSLCE1/大端/长度前缀/
 *     presence 字节/浮点 IEEE754 位模式＋NaN±Inf 编码入口拒绝/版本化/
 *     纯函数可重入）、§3.1 组成表（Slice.hpp｜InputSlice/SliceBuilder/
 *     SliceCodec/双层身份）
 *   - 跨单元红线 CR-02（traceability/foundation-api-diff.md：摘要算法唯一
 *     ＝core ContentDigester；编码器排除字段以 codec 单测钉住）、CR-05
 *     （切片 Environment 新增 runtime.model-identity/runtime.robwork-baseline
 *     两保留 token——消费 CanonicalModel 的评估必填、值由组装方值传递录入、
 *     evidence 不重算 CanonicalModel；二条目属基准类要素进入 inputBaselineId；
 *     联合契约测试与 RT-T09 共享同一样例断言身份一致）
 *   - 需求 CON-04（算法/契约版本兼容——Environment/契约版本进 sliceId）、
 *     CON-05（切片内容身份＝缓存键与失效判据）、CON-06（策略/名称映射身份
 *     进切片）、KIN-13（求解配置进运行与缓存身份）、NFR-COR-02/03
 *   - 任务契约 tasks/foundation/EV-T04.json（≙WP-05-T04）acceptance 1～3：
 *     ①canonical 往返＋浮点位模式＋NaN 拒绝＋EV-ID-1/2；②CR-02 排除字段
 *     纪律（摘要只经 core ContentDigester）；③CR-05 必填校验与编码＋进入
 *     inputBaselineId＋与 RT-T09 共享样例断言身份一致
 *
 * 背景说明（为什么"切片"与"双层身份"是缓存正确性的根基）：
 *   评估结果可否复用（缓存键）、历史结果是否过期（失效判据）、两次结果
 *   可否直接比较（比较基准）是三个语义不同的问题：改求解配置（扩大初值）
 *   后必须重算（sliceId 变→缓存不命中）但输入基准未变（inputBaselineId
 *   不变→新旧结果同属一次"冻结输入研究"）；改模型对象则两者都变。单一
 *   身份无法同时表达（D-04 原文），故 InputSlice 携带双层身份。切片只含
 *   被声明且解析成功的依赖条目——未声明输入的变化不可能影响 sliceId，
 *   这既是 AT-05 失效矩阵精准性的实现基础，也使"漏声明"成为唯一的失效
 *   风险（防线＝注册期闭包校验〔Dependency.hpp〕＋冻结期子集校验〔本头〕）。
 *
 * 实现形态说明：InputSlice 为聚合值类型（公共成员＋成对 ==/!=，无 setter
 * ——冻结后不可变由 SliceBuilder::build() 唯一生产者保证，EV-T02
 * DependencyEntry/EV-T03 AnalysisSnapshot 同款纪律）；builder/codec 的
 * 非平凡逻辑在 src/Slice.cpp（本头只放契约与纯声明＋轻量 inline 校验器）。
 *
 * 消费的 core 契约（core.md v0.1 基线——P-EV-1 状态锚点）：
 *   ContentIdentity/ContentVersion/ContentDigester（core Digest.hpp——
 *   SHA-256 摘要唯一实现点，CR-02；本单元"对什么字节做摘要"的切片侧
 *   承接＝SliceCodec 双形态）。
 *
 * 线程安全：SliceBuilder 非线程安全（仅组装线程持有——§4.2.3 冻结协议
 * 在评估派发前的组装段单线程执行，SnapshotBuilder 同款约束）；InputSlice
 * 冻结后只读可跨线程共享；SliceCodec 全部静态纯函数——可重入（§5.2
 * "线程安全（可重入）"原文）。
 */

#ifndef SDURWS_IRD_EVIDENCE_SLICE_HPP
#define SDURWS_IRD_EVIDENCE_SLICE_HPP

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// CR-05 保留 Environment token（foundation-api-diff.md CR-05 裁决第 1 点）
// =====================================================================

/// Environment 条目 token：runtime 模型身份供给（CR-05——值＝
/// RuntimeSnapshot.modelIdentity 规范文本 "cid-<64hex>"；由组装方值传递
/// 录入，evidence 不重算/重编码 CanonicalModel）。token 词形满足
/// isValidDependencyKey（[a-z][a-z0-9.-]{2,63}）——与依赖键同词表域。
inline constexpr std::string_view kEnvRuntimeModelIdentity{"runtime.model-identity"};

/// Environment 条目 token：RobWork 基线版本供给（CR-05——值＝
/// robworkBaselineVersion：commit/tag＋选项摘要；非空、无空白——
/// RobWorkBaselineVersion 契约，RT-T09 断言 4 同款）。
inline constexpr std::string_view kEnvRuntimeRobworkBaseline{"runtime.robwork-baseline"};

/**
 * @brief 判断 Environment 条目 token 是否为基准类要素（进入 inputBaselineId
 *        的保留 token——CR-05 裁决第 2 点）。
 *
 * @param token [in] Environment 条目的要素名 token
 * @return 属基准类保留 token true（当前仅上列二值）
 *
 * 说明：基准类 Environment 的白名单是"封闭的"——除 CR-05 二条目外，
 * 其余 Environment 要素（产品版本/评估契约版本/编码器版本/编译器契约
 * 版本/碰撞后端版本）均为"非基准版本"（§5.1 inputBaselineId 行排除语），
 * 进 sliceId 不进 inputBaselineId。新增基准类 token 属设计变更（走评审
 * 并同步本函数与本注释），不允许调用方旁路。
 *
 * 线程安全：可重入纯函数。
 */
inline bool isBaselineEnvironmentToken(std::string_view token) noexcept
{
    return token == kEnvRuntimeModelIdentity || token == kEnvRuntimeRobworkBaseline;
}

// =====================================================================
// 评估键语法（§8.2 CheckpointSummary 行原文语法——EvaluationKey 的词形闸门）
// =====================================================================

/**
 * @brief 评估键语法校验（§8.2 原文："语法 [a-z][a-z0-9-]{1,63}"——如
 *        "kin.batch-ik" 所在的键族注意：**评估键不含点**，与依赖键
 *        （允许点）词形不同；示例 "kin-batch-ik" 形态合法）。
 *
 * @param key [in] 待检评估键（切片绑定的评估器名——进入 sliceId）
 * @return 语法合法 true；空串/超长/首字符非小写字母/出现词表外字符
 *         （含点）false
 *
 * 说明：EvaluationKey 的类型化定义归 Evaluator.hpp（§3.1 组成表 EV-T10
 * 行），本任务阶段该头未落地——本函数先行承载其词形契约（实现口径：
 * InputSlice::evaluationKey 暂以 std::string 承载，EV-T10 落地后按需
 * 类型化，登记单元卡变更记录）。词形约束保证评估键在编码/诊断/缓存键
 * 中的无歧义承载（§5.2 字符串规则的构造入口前置）。
 *
 * 确定性：纯函数、无 locale 依赖（NFR-COR-02）。
 */
inline bool isValidEvaluationKey(std::string_view key) noexcept
{
    // 长度边界：首字符 1 个＋后续 1~63 个 ⇒ 总长 2~64（{1,63} 语义）。
    if (key.size() < 2 || key.size() > 64) {
        return false;
    }
    // 首字符：必须小写字母。
    if (key[0] < 'a' || key[0] > 'z') {
        return false;
    }
    // 后续字符：[a-z0-9-]（无点——评估键与依赖键词形的差异点）。
    for (const char c : key.substr(1)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// =====================================================================
// InputSlice——切片本体（§4.2.2 字段表，冻结后不可变）
// =====================================================================

/// 切片内容身份（§3.1 组成表概念名 SliceId 的语义别名——本体＝
/// core::ContentIdentity，§4.2.2 sliceId 行原文类型；CaseId 同款先例，
/// 强类型纪律不变：与快照身份/基准身份互不替代、互不转换——§5.1）。
using SliceId = core::ContentIdentity;

/// 输入基准身份（§3.1 组成表概念名 InputBaselineId 的语义别名——本体＝
/// core::ContentIdentity，§4.2.2 inputBaselineId 行原文类型；D-04 双层
 /// 分离的语义标注，与 SliceId 同形不同义、不提供互转）。
using InputBaselineId = core::ContentIdentity;

/**
 * @brief 输入切片（§4.2.3："评估派发前冻结；SliceBuilder.build() 计算
 *        sliceId 后不可变"）。
 *
 * 生命周期与冻结纪律：由 SliceBuilder::build() 一次性校验、计算双层身份
 * 并返回；此后无任何修改途径——本结构不提供 setter，调用方按只读值对待
 * （EV-T03 AnalysisSnapshot 同款不可变承诺）。同输入不同契约版本＝不同
 * 切片（CON-04）；同一切片可多次运行（重试）；同一 inputBaseline 可有
 * 多个切片（改求解配置复评——§4.2.4）。
 *
 * 值语义（拷贝即深拷贝）；冻结后只读，可跨线程共享。确定性：同内容组装
 * （任意条目添加序）必得同 sliceId/inputBaselineId（builder 冻结时对
 * entries 规范化排序——NFR-COR-02；实现口径登记单元卡 v0.5）。
 */
struct InputSlice {
    /// 切片绑定的评估器键（isValidEvaluationKey 词形；进 sliceId——CON-04）。
    /// 类型说明见 isValidEvaluationKey 注（EvaluationKey 类型化归 EV-T10）。
    std::string evaluationKey;
    /// 评估器契约版本（无量纲版本号；进 sliceId——同输入不同契约版本＝
    /// 不同切片，CON-04/EV-CPA-2 的身份面）。
    std::uint32_t evaluatorContractVersion = 0;
    /// 冻结的依赖清单（§4.2.2：≥1、(kind,key) 字典序稳定存储——builder
    /// 冻结时规范化；每条 Object 条目已在冻结期通过快照子集校验）。
    std::vector<DependencyEntry> entries;
    /// 来源快照身份（切片 ⊆ 快照内容——§4.2.2；取自 build() 入参快照，
    /// 非调用方申报）。同时进入 inputBaselineId 编码（§5.1"快照身份块"）。
    core::ContentIdentity snapshotId;
    /// 切片内容身份＝SHA-256 over SliceCodec full 形态编码（§5.1——缓存键
    /// 与失效判据，CON-05；builder 计算，非申报值）。
    core::ContentIdentity sliceId;
    /// 输入基准身份＝SHA-256 over SliceCodec baseline-projection 形态编码
    /// （§5.1 D-04：仅基准类条目＋快照身份块参与——比较基准（EVI-02/
    /// RPT-04）与"同一冻结输入复评"（C5）的凭据；builder 计算，非申报值）。
    core::ContentIdentity inputBaselineId;

    /// 全字段成员精确等值（测试/codec 往返核对用；身份判定以 sliceId 为准
    /// ——§5.1 字节等值）。
    bool operator==(const InputSlice& o) const
    {
        return evaluationKey == o.evaluationKey
            && evaluatorContractVersion == o.evaluatorContractVersion
            && entries == o.entries && snapshotId == o.snapshotId
            && sliceId == o.sliceId && inputBaselineId == o.inputBaselineId;
    }
    bool operator!=(const InputSlice& o) const { return !(*this == o); }
};

// =====================================================================
// SliceBuilder——冻结期协议（§4.2.3②：解析对账＋子集校验＋身份计算）
// =====================================================================

/**
 * @brief 切片组装器（§4.2.3② 冻结协议承载——评估派发前一次性冻结）。
 *
 * 使用协议（与 §10.1 步骤④对应）：
 *   ①绑定评估面：setEvaluation() 一次给定（评估键＋契约版本）；
 *   ②录入解析结果条目：addEntry() 逐条累加——条目由请求方按评估器
 *     descriptor.inputs 声明解析而来（§4.2.3②"请求方提供解析结果"；
 *     条件真假〔applied/notAppliedReason〕的解析归请求方域语义，
 *     evidence 不解释 conditionToken——N-5）；CR-05 场景置位
 *     setConsumesCanonicalModel()；
 *   ③冻结：build(snapshot) 执行全部校验→规范化排序→计算双层身份→
 *     返回不可变 InputSlice。冻结时刻＝build() 返回时。
 *
 * 校验集（全部拒绝抛 EvidenceError(SliceIncomplete)——调用方组装契约
 * 违约 fail-fast；校验顺序固定，同一坏组装必报同一首错，NFR-COR-02；
 * 顺序见 src/Slice.cpp 实现头注释）：
 *   - 评估键语法（isValidEvaluationKey）；
 *   - 来源快照必须为 builder 产出的冻结快照（snapshotId 非零）；
 *   - 条目集语法/结构（validateDependencyEntries：载荷-kind 匹配、
 *     (kind,key) 唯一、applied/notAppliedReason 配对——builder 先规范化
 *     排序再校验，"未排序"不再是冻结拒绝面）；
 *   - Object 条目快照子集校验（§4.2.2："每条 Object 条目必须能在快照
 *     objectClosure 中找到（子集校验，防漏声明错配）"——(oid,cv) 精确
 *     匹配；快照外的对象进切片＝消费了闭包外输入，CON-01 破坏）；
 *   - CR-05 必填校验：setConsumesCanonicalModel(true) 时，Environment
 *     条目必须含 runtime.model-identity 与 runtime.robwork-baseline 各
 *     恰一条（消费 CanonicalModel 的评估必填——与 compilerContractVersion
 *     必填口径并列；值由组装方经 RuntimeSnapshot 值传递录入）；
 *   - CR-05 值形态校验（凡以保留 token 命名的 Environment 条目恒久生效，
 *     不论必填与否）：runtime.model-identity 值必须为 "cid-<64hex>" 规范
 *     文本（core::ContentIdentity 可解析——跨单元身份可比前提）；
 *     runtime.robwork-baseline 值必须非空且无空白字符（版本串契约）。
 *
 * 线程约束：非线程安全——仅组装线程持有（§4.2.3 组装段单线程，与
 * SnapshotBuilder 同源约束）。
 */
class SliceBuilder {
public:
    SliceBuilder() = default;

    /// ①绑定评估面（覆盖式——一次组装一个评估面；语法在 build() 校验）。
    SliceBuilder& setEvaluation(std::string evaluationKey,
                                std::uint32_t evaluatorContractVersion);

    /// ②录入解析结果条目（累加；(kind,key) 重复与载荷非法在 build() 拒绝
    /// ——录入序任意，冻结时规范化排序）。
    SliceBuilder& addEntry(DependencyEntry entry);

    /**
     * @brief 声明本切片的评估消费 CanonicalModel（CR-05 必填开关）。
     *
     * @param required [in] true＝消费 CanonicalModel——build() 强制校验
     *                 runtime.model-identity/runtime.robwork-baseline 双
     *                 Environment 条目必填；false（默认）＝不强制。
     *
     * 说明：谁在消费 CanonicalModel 由评估器依赖声明表达（域知识），
     * evidence 不域判——本开关由组装方按 descriptor 的声明置位，evidence
     * 只承载开关并机械执行必填校验（"evidence 不重算 CanonicalModel"
     * 的同源边界：校验的是条目在在与值形态，从不解释模型内容）。
     */
    SliceBuilder& setConsumesCanonicalModel(bool required);

    /**
     * @brief ③冻结：全部校验＋规范化排序＋计算双层身份＋返回不可变切片。
     *
     * @param snapshot [in] 来源快照（SnapshotBuilder 产出的冻结快照；
     *                 切片的 snapshotId 字段取自它——非申报值；全部
     *                 Object 条目对它的 objectClosure 做子集校验）。调用方
     *                 持有，本函数仅在调用期使用，不保存引用。
     *
     * @return 冻结切片（sliceId/inputBaselineId 均为计算值；entries 已
     *         按 (kind,key) 字典序规范化——同内容任意添加序同身份）
     *
     * @throws EvidenceError（EvidenceErrorCode::SliceIncomplete）任一上述
     *         校验失败（detail 携带就地定位信息——涉事依赖键与原因）
     *
     * 复杂度：O(E log E)（排序）＋O(E·C)（Object 子集校验，C＝闭包条数
     * ——冻结期一次性，非热点）。
     */
    InputSlice build(const AnalysisSnapshot& snapshot) const;

private:
    std::string m_evaluationKey;              ///< 评估键累加缓冲（①绑定）
    std::uint32_t m_contractVersion = 0;      ///< 契约版本（①绑定）
    std::vector<DependencyEntry> m_entries;   ///< 条目累加缓冲（②录入，插入序）
    bool m_consumesCanonicalModel = false;    ///< CR-05 必填开关（默认不强制）
};

// =====================================================================
// SliceCodec——规范编码（§5.2 canonical 规则在切片面的落点＋CR-02 纪律）
// =====================================================================

/**
 * @brief 切片规范编码器（§5.2 编码规则表＋CR-02 一致纪律在 SliceCodec 的
 *        实现；四家编码器同款：确定性二进制＋magic＋长度前缀＋大端＋
 *        presence 字节＋浮点位模式＋NaN±Inf 拒绝＋摘要唯一经
 *        core::ContentDigester）。
 *
 * 双形态与双层身份（§5.1）：
 *   - full 形态（身份形态——sliceId 对它计算）：评估面＋快照身份块＋
 *     全部冻结条目（含 Environment 版本要素与未适用的条件条目——§4.2.1
 *     "其条件输入仍在身份里"）；
 *   - baseline-projection 形态（身份形态——inputBaselineId 对它计算）：
 *     快照身份块＋基准类条目子集（实现口径：全部 Object＋SampleSet＋
 *     NameMap＋Environment 的基准类保留 token 条目；排除求解类
 *     Configuration、Policy、UpstreamResult 与 Environment 非基准版本
 *     ——§5.1/§4.2.2"仅模型/需求/工况集/冻结样本集条目参与"＋CR-05
 *     二条目属基准类。口径推导见 src/Slice.cpp 实现头注释 D-1）。
 *   - 两形态的条目编码字节级同构（"SliceCodec 的子集"字面成立）；
 *     baseline-projection 非往返载体（parse 仅接受 full——身份投影编码
 *     不是数据交换格式，runtime RT-Codec 身份域同款纪律）。
 *
 * 浮点承载原语（§5.2 数值行："配置中的浮点以 IEEE754 双精度 8 字节大端
 * 位模式承载（round-trip 精确）；NaN/±Inf 在编码入口拒绝"）：canonicalF64/
 * parseCanonicalF64 为该规则在 evidence 侧的唯一实现点——供各域生产
 * Configuration 载荷 canonical 字节时使用（SA-12：单位归一化归域，本原语
 * 只做无语义的字节承载；EV-ID-2 的"浮点近似相等禁止作身份键"由位模式
 * 承载＋字节等值身份共同保证）。
 *
 * 线程安全：全部静态纯函数——可重入（§5.2 原文）。
 */
class SliceCodec {
public:
    /// 编码 magic（§5.2 原文 "IRDSLCE1"——8 字节 ASCII）。
    static constexpr std::string_view kMagic{"IRDSLCE1"};
    /// 本实现 codec 版本（写入编码第 9 字节；升版＝全体切片身份变化——
    /// 破坏性变更走设计变更评审并同步缓存治理登记，§5.2 版本化行）。
    static constexpr std::uint8_t kCodecVersion = 1;

    /**
     * @brief full 形态编码（sliceId 的摘要对象；EV-ID-1 的往返载体）。
     *
     * @param slice [in] 冻结切片（builder 产出；未冻结手改值编码出的字节
     *              与其 sliceId 不再对应——契约：只编码冻结切片）
     * @return 规范字节（同内容恒同字节——NFR-COR-02）
     *
     * @throws EvidenceError（SliceIncomplete）切片内部字段与编码不变量
     *         冲突（字符串含 NUL/超长等——builder 已挡的再入防线，属
     *         调用方契约违约）
     */
    static std::vector<std::uint8_t> encodeFull(const InputSlice& slice);

    /**
     * @brief baseline-projection 形态编码（inputBaselineId 的摘要对象；
     *        非往返载体——仅作身份计算与测试钉住排除字段用）。
     *
     * @param slice [in] 冻结切片（同 encodeFull 契约）
     * @return 规范字节（快照身份块＋基准类条目子集——排除面见类注释）
     *
     * @throws EvidenceError（SliceIncomplete）同 encodeFull
     */
    static std::vector<std::uint8_t> encodeBaselineProjection(const InputSlice& slice);

    /**
     * @brief full 形态解析（恒重算双层身份——身份是编码的函数，不是编码
     *        的载荷：解析结果不可能携带与内容不符的申报身份）。
     *
     * @param encoding [in] encodeFull 的输出（或同规范的字节序列；
     *                 baseline-projection 字节不接受——非往返载体）
     * @return 解析出的切片（sliceId/inputBaselineId 为重算值）
     *
     * @throws EvidenceError（SliceIncomplete）magic/版本/形态不符、截断、
     *         条目载荷与 kind 不符、字符串含 NUL、非有限浮点位型等结构性
     *         非法（detail 带就地偏移）
     */
    static InputSlice parse(const std::vector<std::uint8_t>& encoding);

    /**
     * @brief IEEE754 双精度位模式大端 8 字节编码（§5.2 数值行唯一实现点）。
     *
     * @param value [in] 待编码浮点（必须有限——NaN/±Inf 拒绝）
     * @return 8 字节位模式（大端；round-trip 精确——parseCanonicalF64
     *         还原后位级相等，含 -0.0 与非规格化数的位型）
     *
     * @throws EvidenceError（SliceIncomplete）value 非有限（NaN/±Inf 编码
     *         入口拒绝——§5.2 原文；非有限值无稳定位模式语义，进身份会
     *         破坏 NFR-COR-02 确定性）
     *
     * 线程安全：可重入纯函数。
     */
    static std::array<std::uint8_t, 8> canonicalF64(double value);

    /**
     * @brief 位模式还原（canonicalF64 的逆——round-trip 精确的接收端）。
     *
     * @param bytes [in] 8 字节位模式（大端；调用方保证可读——指针须指向
     *              至少 8 字节有效内存）
     * @return 还原的 double（位级相等；-0.0 保留位型）
     *
     * @throws EvidenceError（SliceIncomplete）位型为非有限（NaN/±Inf——
     *         接收端复核：手工构造/传输损坏的位型在此暴露，runtime
     *         RT-Codec f64 读取同款双端纪律）
     *
     * 线程安全：可重入纯函数。
     */
    static double parseCanonicalF64(const std::uint8_t* bytes);
};

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_SLICE_HPP
