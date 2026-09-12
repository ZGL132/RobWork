/**
 * @file   CacheKey.hpp
 * @brief  编译缓存纯判定——CompileCacheKey 分层键（WC/DWC 两层）＋
 *         IRuntimeCompileCacheKey（键生成）＋judgeCompileCacheCompatibility
 *         （兼容判定；§9.4 判定表全行）。
 *
 * 设计依据：
 *   - units/runtime.md §9.4（编译缓存纯判定：分量表／分层键结构／判定表
 *     逐行——FullReuse／WorkCellOnlyReuse／Incompatible／语义等价不参与／
 *     部分与失败产物永不命中／"命中≠当前性"）、§3.1（模块清单 CacheKey 行：
 *     "CompileCacheKey 及其分量、CompileCacheCompatibility、
 *     judgeCompileCacheCompatibility、IRuntimeCompileCacheKey"）
 *   - 需求 CON-04（缓存按契约判定；部分/失败产物不得作为完整命中——
 *     不做存储/淘汰）、CON-05（缓存键纯内容——"当前 HEAD 前进本身不使
 *     编译缓存失效"，只有被消费对象内容变化→modelIdentity 变→新键）
 *   - 任务契约 tasks/foundation/RT-T10.json（产物：CacheKey.hpp/.cpp；
 *     acceptance：RT-CACHE-1～4 分层键与判定表全用例、存储归 execution
 *     只做纯判定、部分/失败产物不作完整命中）
 *   - CR-05（跨单元红线处置约束）：本模块与 evidence 的分工是"键链一致"
 *     ——同切片→同 modelIdentity→同 WC/DWC 键（SA-07）；策略/种子/线程
 *     **不在本键**（它们经 evidence 切片身份进入评估缓存，CON-06——
 *     §9.4 FullReuse 行原文），本头以分量指纹表把该"不在"钉成可断言面
 *     （见 RT-CACHE-1 用例的指纹集合钉住）
 *
 * 与 RT-T09 交付的单点关系（§15.4 v0.10②）：WC 层键复用
 * snapshotidentity::computeWorkCellCompileIdentity——同一公式单点，避免
 * 第二套键编码；本头不得再写 WC 层键的第二实现。
 *
 * 存储边界（acceptance 2）：本模块只定义键身份与兼容判定（纯函数）；
 * **存储、命中执行、淘汰归 execution**（CON-04/N-6，§9.4 首句原文）——
 * 本头与实现文件不含任何缓存容器/命中查询/淘汰 API，键的"登记前生成"
 * 由 execution 在缓存写入前调用（§10.0 buildKey 行"execution 缓存登记前
 * 调用"）。
 *
 * 实现层增补（§15.4 v0.11 登记，DTB §5.4）：CompileCacheKey 在 §9.4 草图
 * 两字段（workCellKey/dynamicWorkCellKey）之上增补**分量指纹表**
 * （components，8 条 WC 层分量逐项摘要）——judge 的"分量级差异清单
 * （model-changed / compiler-changed / …）"需要逐分量可比数据，而两个
 * 复合摘要本身无法还原差异分量；指纹表只增不改草图的字段语义，且
 * judge 对无指纹键仍可按复合键给出保守结论（reasons 退化为
 * workcell-key-changed）。
 *
 * 两模式编译口径（§15.4 v0.11，同 RT-T09 分工）：本头消费
 * snapshotidentity::computeWorkCellCompileIdentity（实现于 src/Snapshot.cpp，
 * 集成模式编译单元）——CacheKey.cpp 与 CacheKeyTest.cpp 按 TARGET
 * sdurw_kinematics 条件增列；冒烟模式下本头不被任何 TU include，
 * "目标注册＋include 路径"的冒烟口径不受影响。
 *
 * 线程安全：全部实体为纯值/无状态（键生成器构造后只读、judge 纯函数），
 * 可重入、可并发。
 */

#ifndef SDURWS_IRD_RUNTIME_CACHEKEY_HPP
#define SDURWS_IRD_RUNTIME_CACHEKEY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>             // ContentIdentity（键值类型）
#include <sdurws/ird/runtime/Adapter.hpp>         // RobWorkBaselineVersion（键分量默认来源）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // CanonicalModel（buildKey 输入）
#include <sdurws/ird/runtime/Compiler.hpp>        // CompileOptions（buildKey 输入；D-04 分层）
#include <sdurws/ird/runtime/NameMap.hpp>         // kNameMapRuleVersion（键分量单点）
#include <sdurws/ird/runtime/Snapshot.hpp>        // snapshotidentity::computeWorkCellCompileIdentity
                                                  // （WC 键公式单点，复用不重写）＋
                                                  // SnapshotCodecVersions（键分量类型）

namespace sdurws::ird::runtime {

// =====================================================================
// CompileCacheComponent——分量指纹（实现层增补，§15.4 v0.11①）。
// =====================================================================

/**
 * @brief 缓存键分量指纹（分量稳定名→该分量的确定性摘要）。
 *
 * 背景（为什么需要它）：§9.4 判定表的 Incompatible 行要求附"分量级
 * reasons"（RT-CACHE-1"reasons 精确"、RT-CACHE-3"contract-changed
 * reason"）——两个 SHA-256 复合键无法还原差异出自哪个分量，故键在两个
 * 复合键之外随带逐分量指纹，judge 逐项比对后产出精确原因。
 *
 * @note component 为稳定分量名（reasons 词汇的词干——judge 输出
 *       "<component>-changed"）；一经交付不得改名（判定结论的观测面，
 *       与稳定诊断码同级的对外契约）。digest 为该分量值的确定性摘要
 *       （编码：大端定长整数／长度前缀字符串；model 分量直接取
 *       modelIdentity 本身——模型身份已是摘要，无需再摘要）。
 * 值语义纯结构；线程安全。
 */
struct CompileCacheComponentFingerprint {
    /// 分量稳定名（WC 层 8 分量的词干集见 buildKey 实现注释——
    /// model/contract/compiler/baseline/namemap-rule/base-world-rule/codec/wc-options）。
    std::string component;
    /// 该分量的确定性摘要（同分量同摘要——NFR-COR-02）。
    core::ContentIdentity digest;
};

// =====================================================================
// CompileCacheKey——分层键（§9.4 原文结构＋实现层增补指纹表）。
// =====================================================================

/**
 * @brief 编译缓存键（§9.4 原文契约——"分层键：单模型派生两层"）。
 *
 * 两层语义（D-04 分层键决策，§9.1 workCellCompileIdentity 行同源）：
 *   - workCellKey＝f(modelIdentity, compilerContractVersion, compilerVersion,
 *     robworkBaselineVersion, nameMapRuleVersion, baseWorldRuleVersion,
 *     codecVersions, compileOptions〔DWC 无关子集〕)——由
 *     snapshotidentity::computeWorkCellCompileIdentity 单点计算（本模块
 *     不写第二套公式）；
 *   - dynamicWorkCellKey＝f(workCellKey 全部分量, compileOptions 全集,
 *     capabilityLevel)——实现为 SHA-256(workCellKey‖requestDynamicWorkCell)
 *     的链式派生（§15.4 v0.11②）：requestDynamicWorkCell（＝capabilityLevel，
 *     "是否要求 DWC"）是 WC 子集之外唯一的增量分量，链式派生同时保证
 *     "任一 WC 分量变→DWC 键必变"（§9.4 RT-CACHE-1 判据的结构性前提），
 *     且避免全量分量第二遍编码。
 *
 * nullopt 语义（§9.4 草图原文"SkippedNoPhysics 时为 nullopt"）：
 *   - 请求侧：CompileOptions.requestDynamicWorkCell=false（本次编译不构造
 *     DWC）→ nullopt——键生成不预判物性（那是编译事实，§5.2 S7）；
 *   - 缓存侧：execution 登记快照键时按快照事实落账——SkippedNoPhysics
 *     （无论"物性缺失跳过"还是"未请求"）一律存 nullopt（Compiled 才存
 *     buildKey 的 DWC 键）。该落账规则属 execution 的存储职责（acceptance 2
 *     边界），本头仅在注释声明，不提供存储面。
 *
 * 值语义聚合体；线程安全（纯值）。合法键：workCellKey 非零（§9.1 合法列
 * "非零"）；"未 finalize/身份为空"的键（workCellKey 全零——buildKey 对
 * 无效模型的产出）是合法承载值但**永不构成命中**（judge 恒 Incompatible，
 * §9.4 判定表"部分模型/失败产物"行）。
 */
struct CompileCacheKey {
    /// WC 层缓存键（§9.1 公式单点计算；非零——§9.1 合法列）。
    core::ContentIdentity workCellKey;
    /// DWC 层缓存键（链式派生；SkippedNoPhysics 落账为 nullopt——见类注释）。
    std::optional<core::ContentIdentity> dynamicWorkCellKey;
    /// WC 层分量指纹表（实现层增补——8 条，声明序＝键编码域分量序；
    /// reasons 精确判定的依据。空表＝调用方仅持有复合键（judge 退化为
    /// 复合键比对——保守 reasons）。见 CompileCacheComponentFingerprint 注释）。
    std::vector<CompileCacheComponentFingerprint> components;
};

// =====================================================================
// codecVersionsFromCodecHeaders——编码器版本三元组的单一权威组装。
// =====================================================================

/**
 * @brief 从各编码头常量组装 SnapshotCodecVersions（键分量的单一权威取值）。
 *
 * §9.1 codecVersions 行："三元分别取自 rtcodec 编码头常量与
 * MaterializedSnapshotCodec 编码头常量——消费方不得另写字面量（编码升版
 * ＝全体身份变化，§4.5；单一权威＝各 Codec 头常量）"。本函数是该纪律在
 * 缓存键侧的落点（快照侧同值——RT-T09 键公式用例互证）。
 *
 * @return 三元组（canonical-model/name-map/snapshot 各 major+minor）
 *
 * 线程/确定性：纯函数、可重入（读编译期常量）。
 */
SnapshotCodecVersions codecVersionsFromCodecHeaders();

// =====================================================================
// IRuntimeCompileCacheKey——键生成抽象（§9.4 原文接口）。
// =====================================================================

/**
 * @brief 编译缓存键生成器抽象（§9.4 原文契约——"纯函数"）。
 *
 * 接口属性（§10.0 buildKey 行逐项）：前置＝模型有效；后置＝分层键
 * （WC/DWC）；错误＝无（不抛——noexcept 由接口钉住）；可重入；确定性；
 * 只读；无副作用；返回值归调用方；调用时机＝execution 缓存登记前；
 * 非法调用＝把键当"当前性"判据（§9.4"缓存命中 vs 结果当前性——两回事"：
 * 当前性归 evidence computeCurrentness，CON-05）。
 *
 * "模型有效"前置的违约路径（noexcept 约束下的设计）：模型身份为全零
 * （未 finalize——Failed/Cancelled 不产生模型，防御性输入）时**不抛**，
 * 返回零键（workCellKey 全零＋DWC nullopt＋空指纹）——该键进入判定必被
 * Incompatible 拒绝（§9.4"缓存判定对未 finalize 输入恒 Incompatible"），
 * 半成品由此在判定面被结构性拦截（不产出占位合法键伪装命中面，
 * NFR-COR-03 不静默）。
 */
class IRuntimeCompileCacheKey {
public:
    virtual ~IRuntimeCompileCacheKey() = default;

    /**
     * @brief 从模型与编译选项生成分层键（§9.4 原文签名；纯函数）。
     *
     * @param model   [in] 规范模型（只读；contentIdentity 即键的 model 分量）
     * @param options [in] 编译选项（全集——WC 子集入 workCellKey，
     *                requestDynamicWorkCell 决定 DWC 层是否在场，D-04）
     * @return 分层键（模型无效时为零键——见类注释；不抛）
     *
     * @throws 无（§10.0"无（不抛）"；编译器侧分量非法在构造期 fail-fast）
     *
     * 线程/确定性：纯函数、可重入；同输入同键（NFR-COR-02）。
     */
    virtual CompileCacheKey buildKey(const CanonicalModel& model,
                                     const CompileOptions& options) const noexcept = 0;
};

// =====================================================================
// RuntimeCompileCacheKeyBuilder——键生成器唯一实现（§15.4 v0.11③）。
// =====================================================================

/**
 * @brief 缓存键生成器（IRuntimeCompileCacheKey 唯一实现——无状态以外的
 *        全部键分量为构造期常量）。
 *
 * 分量来源（§9.4 分量表逐行——"编译器/环境侧"分量在构造期固定，"内容
 * 侧"分量逐请求取自模型）：
 *   - modelIdentity ← model.contentIdentity()（每请求）；
 *   - compileOptions ← 参数（每请求；D-04 分层）；
 *   - compilerContractVersion/compilerVersion ← 构造入参（值注入——与
 *     worker 物化路径"编译器版本面随载荷值传递"同构，§15.4 v0.10③；
 *     execution 从编译器实例取值后传入，本类不持有编译器引用）；
 *   - robworkBaselineVersion ← 构造入参，默认 RobWorkBaselineVersion::
 *     capture()（§8.4 采集单点）；
 *   - nameMapRuleVersion ← 构造入参，默认 kNameMapRuleVersion（§7.1 单点）；
 *   - baseWorldRuleVersion ← 恒取 kBaseWorldRuleVersion（§6 单点——在
 *     computeWorkCellCompileIdentity 内部生效，本类不经手）；
 *   - codecVersions ← 构造入参，默认 codecVersionsFromCodecHeaders()
 *     （各编码头常量组装）。
 *
 * 可重复使用（同配置多模型多请求）；线程安全（构造后只读）。
 */
class RuntimeCompileCacheKeyBuilder final : public IRuntimeCompileCacheKey {
public:
    /**
     * @brief 构造键生成器（编译器/环境侧分量在此固定）。
     *
     * @param compilerContractVersion [in] 编译器契约版本（≥1——§4.3.1 合法列）
     * @param compilerVersion         [in] 编译器实现版本（非空——§9.1 合法列）
     * @param robworkBaselineVersion  [in] 基线版本（非空；默认＝采集单点）
     * @param nameMapRuleVersion      [in] 名称规则版本（默认＝单点常量）
     * @param codecVersions           [in] 编码器版本三元组（默认＝编码头组装）
     *
     * @throws RuntimeError 码＝InputInvalid：契约版本为 0／任一版本串为空
     *         （调用方错误 fail-fast——若延后到 buildKey 将违反"无（不抛）"，
     *         故全部校验前移到构造期）
     */
    RuntimeCompileCacheKeyBuilder(std::uint32_t compilerContractVersion,
                                  std::string compilerVersion,
                                  std::string robworkBaselineVersion
                                  = RobWorkBaselineVersion::capture().text,
                                  std::uint32_t nameMapRuleVersion = kNameMapRuleVersion,
                                  SnapshotCodecVersions codecVersions
                                  = codecVersionsFromCodecHeaders());

    /**
     * @brief 生成分层键（接口契约见 IRuntimeCompileCacheKey；实现要点）：
     *
     *   1. 模型有效性检查——contentIdentity 全零→返回零键（不抛，
     *      "未 finalize 恒不命中"的判定面表示，见接口注释）；
     *   2. WC 键＝computeWorkCellCompileIdentity（§9.1 公式单点——复用，
     *      不写第二套编码）；
     *   3. DWC 键＝SHA-256(workCellKey‖requestDynamicWorkCell)（链式派生；
     *      requestDynamicWorkCell=false→nullopt）；
     *   4. 分量指纹表＝8 条逐项摘要（声明序＝WC 键编码域分量序：
     *      model→contract→compiler→baseline→namemap-rule→base-world-rule→
     *      codec→wc-options）。
     *
     * @param model   [in] 规范模型（只读）
     * @param options [in] 编译选项（全集）
     * @return 分层键（不抛——全部可抛路径已在构造期拦截或于此提前返回）
     */
    CompileCacheKey buildKey(const CanonicalModel& model,
                             const CompileOptions& options) const noexcept override;

private:
    // ---- 编译器/环境侧分量（构造期常量；声明序＝键分量表行序）----
    std::uint32_t m_compilerContractVersion;  ///< 编译器契约版本（≥1）
    std::string m_compilerVersion;            ///< 编译器实现版本（非空）
    std::string m_robworkBaselineVersion;     ///< RobWork 基线版本（非空）
    std::uint32_t m_nameMapRuleVersion;       ///< 名称生成规则版本
    SnapshotCodecVersions m_codecVersions;    ///< 编码器版本三元组
};

// =====================================================================
// CompileCacheCompatibility／judgeCompileCacheCompatibility——判定表。
// =====================================================================

/**
 * @brief 兼容判定结论（§9.4 原文结构——verdict＋分量级 reasons）。
 *
 * verdict 三值语义（§9.4 判定表逐行）：
 *   - FullReuse：完整命中——"workCellKey 等 ∧ DWC 键等（或同为 nullopt 且
 *     请求不要求 DWC）"；策略/种子/线程不在键内（经 evidence 切片身份进
 *     评估缓存，CON-06）；
 *   - WorkCellOnlyReuse："WorkCell 可复用而 DynamicWorkCell 不可"的显式
 *     表达——调用方可复用 WC 层产物、须重编 DWC；**不得作为完整命中上报**
 *     （CON-04）；
 *   - Incompatible：任一基础分量不等——旧编译器版本/旧基线/旧编码器一律
 *     在此拒绝（RT-CACHE-3），不存在"版本接近可凑用"的语义等价复用
 *     （§4.3.6：字节等值是唯一等价关系，保守方向宁可重算不错复用）。
 *
 * reasons＝分量级差异清单（稳定词汇，一经交付不得改语义）：
 *   - 复合键不等时的逐分量差异："<分量名>-changed"——model-changed／
 *     contract-changed／compiler-changed／baseline-changed／
 *     namemap-rule-changed／base-world-rule-changed／codec-changed／
 *     wc-options-changed（按 WC 键编码域分量序排列）；
 *   - WC 相等而 DWC 不可复用：dwc-missing-in-cached（缓存侧 SkippedNoPhysics
 *     而请求要求 DWC——RT-CACHE-2）／dwc-not-requested（请求不要求 DWC 而
 *     缓存侧有——保守不报完整命中）／dwc-key-mismatch；
 *   - 未 finalize/身份为空键：key-invalid（判定表"部分模型/失败产物"行
 *     ——永不构成命中）；
 *   - 复合键不等而指纹不可比/无差异：workcell-key-changed（保守兜底）。
 *   FullReuse 时 reasons 为空。请求侧指纹序为比对基准（键由同一 builder
 *   产出时两侧同序——指纹表声明序即编码域分量序）。
 * 值语义纯结构；线程安全。
 */
struct CompileCacheCompatibility {
    /// 判定结论（语义见类注释——三值互斥）。
    enum class Verdict { FullReuse, WorkCellOnlyReuse, Incompatible } verdict;

    /// 分量级差异清单（稳定词汇——见类注释表；FullReuse 时为空）。
    std::vector<std::string> reasons;
};

/**
 * @brief 编译缓存兼容判定（§9.4 原文签名——纯函数；判定表全行的唯一执行点）。
 *
 * 判定顺序（与判定表行序对应）：
 *   1. 键有效性门（"部分模型/失败产物永不构成命中"行）——任一侧
 *      workCellKey 全零或"在场 DWC 键"全零→Incompatible(key-invalid)；
 *   2. WC 层分量比对（"任一基础分量不等"行）——指纹逐项比对得精确
 *      reasons；复合键不等而指纹不可比（空表/序失配）→保守兜底
 *      workcell-key-changed；
 *   3. DWC 层判定（前两行）——WC 相等后按 optional 组合给出 FullReuse 或
 *      WorkCellOnlyReuse（含"缓存 nullopt 而请求要求 DWC"的 RT-CACHE-2 行，
 *      结论**非** FullReuse——不得作为完整命中上报）。
 *
 * "缓存命中 vs 结果当前性"（判定表末行）：本函数只回答"产物对该键可否
 * 复用"；该键相对当前输入是否仍适用（HEAD 前进、非消费对象变化）归
 * evidence computeCurrentness（CON-05）——本判定不消费任何 HEAD/时钟/
 * 会话状态（纯函数，键即全部输入）。
 *
 * @param requested [in] 当前请求的键（buildKey 产出；只读）
 * @param cached    [in] 缓存侧的键（execution 登记值；只读）
 * @return 判定结论（verdict＋reasons——见结构注释）
 *
 * 线程/确定性：纯函数、可重入；同键对同结论（NFR-COR-02）。判定**不
 * 对称**：requested/cached 互换可改变 DWC 层结论方向（"谁要求 DWC"是
 * 有向语义——WorkCellOnlyReuse 的处置路径归请求侧）。
 */
CompileCacheCompatibility judgeCompileCacheCompatibility(const CompileCacheKey& requested,
                                                         const CompileCacheKey& cached);

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_CACHEKEY_HPP
