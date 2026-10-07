/**
 * @file   CandidatePatch.hpp
 * @brief  候选补丁（CandidatePatch）与确定性身份——补丁项、canonical 序列化、
 *         CandidatePatchId/CandidateId、构造校验（锁定/边界/值域）与补丁
 *         覆盖视图数据面（所有权 O2；任务 WP-20-T03）。
 *
 * 设计依据：
 *   - units/optimization.md §5.5（CandidatePatch 数据模型与确定性序列化——
 *     magic "IRDOPTP1"＋u32 codecVersion＋u32 itemCount＋每项定长/定宽编码；
 *     相同语义补丁 ⇒ 相同字节 ⇒ 相同 CandidatePatchId）、§4.2（候选身份
 *     CandidateId＝SHA-256(baselineRootOid‖baselineRootCv‖CandidatePatchId)，
 *     规范文本 cnd-<64hex>，内容寻址、跨运行稳定、天然去重；候选身份不含
 *     显示单位、UI 排序状态、运行配置）、§5.4（生成阶段锁定拒绝 OPT-VAR-LOCKED）、
 *     I-OPT-8/9（显示单位不入身份；越界/非有限/枚举外值构造时拒绝不截断）、
 *     §5.1/§16.3 P-OPT-2（补丁覆盖视图 ICandidateDesignOverlay——候选物化
 *     通道：接口与数据面就绪，物化实现待裁决，裁决前研究定义/导出/应用
 *     组装面先行）、§8.2 变量编码（量化＝步长网格确定性对齐 round-half-even）
 *   - 需求 OPT-02（drivetrain.ratio StageB 绑定可编辑并编译进候选——V12-02；
 *     c＝Δq_joint/Δθ_motor 口径与 drivetrain/runtime 一致）、AT-09（ratio
 *     回归反例：StageB 激活传动比不被阶段锁错误拒绝）
 *   - 诊断码：kOptPatchIllegal/kOptVarLocked/kOptStageLocked/kOptInputInvalid
 *     （DiagCodes.hpp 常量——§6.6 登记表）
 *   - 任务契约 tasks/foundation/WP-20-T03.json acceptance 3（确定性序列化/
 *     身份不含 UI 状态/锁定拒绝）
 *
 * 背景说明（身份链，卡 §4.2 六层身份中的候选/补丁两层）：
 *   CandidatePatchId＝对补丁 canonical 字节的 SHA-256（core::ContentIdentity
 *   承载——"cid-<64hex>"）；CandidateId＝SHA-256(基线根对象 16 字节‖基线根
 *   对象内容版本 32 字节‖补丁身份 32 字节)——同一补丁作用于同一基线必得同
 *   CandidateId（跨运行稳定）。CandidateId 是**本域自有**格式（cnd- 前缀
 *   64 hex；不进入 core Id128 tag 冻结集，DOPT-2/DOPT-3 同源口径）。
 *
 * 确定性承诺（NFR-COR-02）逐条（卡 §5.5）：①补丁项排序＝bindingId 字典序
 *   （canonicalize 内部再排序兜底——构造边界强制＋序列化双保险）；②值编码
 *   定宽小端、枚举存下标、离散器件存规范引用文本；③空补丁合法（基线候选，
 *   有确定身份）；④相同补丁不得生成多个不同候选身份；⑤候选身份不含显示
 *   单位/UI 状态（label 不参与身份——I-OPT-8）；⑥补丁 canonical 进缓存键
 *   （经 config.opt/评估请求切片——T06 消费面）与导出（T09 消费面）。
 *
 * 线程约束：全部纯值/纯函数（canonicalize/patchIdentity/candidateIdOf 可
 * 并发；ContentDigester 实例不共享——每线程各建，core §4.2）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_CANDIDATE_PATCH_HPP
#define SDURWS_IRD_OPTIMIZATION_CANDIDATE_PATCH_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>         // core::ContentIdentity/Digest256——
                                              // 补丁身份与摘要承载（CON-05）
#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId/ContentVersion——
                                              // 候选身份公式的基线输入
#include <sdurws/ird/optimization/Types.hpp>  // OptimizationStage/OptimizationError
#include <sdurws/ird/optimization/Variable.hpp>  // VariableKind/VariableBinding——
                                                 // 补丁校验的绑定语义来源

namespace sdurws::ird::optimization {

// =====================================================================
// 补丁项与补丁（卡 §5.5 结构设计基线逐字承载）
// =====================================================================

/**
 * @brief 补丁值形态标记（canonical 编码的 tag 字节——值三选一的显式化）。
 *
 * 取值为 ASCII 字母（'S'/'E'/'D'）——canonical 字节流内可读性诊断友好；
 * 枚举值即编码值（无第二映射，防漂移）。
 */
enum class PatchValueTag : std::uint8_t {
    Scalar = 0x53,      ///< 'S'——连续/量化标量（SI 真值，f64 小端）
    EnumIndex = 0x45,   ///< 'E'——枚举下标（u32 小端）
    DiscreteRef = 0x44, ///< 'D'——离散器件引用（u16 长度＋UTF-8 字节，R2）
};

/**
 * @brief 单条补丁项：一个绑定的新值（SI 真值或枚举下标；卡 §5.5 原文）。
 *
 * 值的有效性由绑定类别决定（三选一；构造规范化见 makeCandidatePatch）：
 *   Continuous/Quantized → scalarValue（SI 单位＝绑定单位；量化项为对齐后
 *     网格值）；Enumeration → enumIndex（enumValues 下标）；DiscreteDevice
 *     → discreteRef（R2：modelId/DeviceCombinationId＋目录身份的规范文本）。
 * 非当前形态的成员在规范化构造中清零/清空——保证"相同语义补丁 ⇒ 相同
 * 字节"不受未用字段残渣影响（确定性前提，卡 §5.5 ②）。
 *
 * 线程安全：纯值。
 */
struct PatchItem {
    BindingToken bindingId = {};     ///< 与 VariableBinding.bindingId 对应
                                     ///  （补丁项→绑定→权威字段的定位链）
    double scalarValue = 0.0;        ///< 连续/量化值（SI 单位；-0.0 规范化为
                                     ///  +0.0——位模式确定性）
    std::uint32_t enumIndex = 0;     ///< 枚举下标（enumValues 内）
    std::string discreteRef = {};    ///< 离散器件规范引用（R2——离散项唯一
                                     ///  有效载荷；其余形态为空串）
};

/**
 * @brief 候选补丁：基线之上的变量覆盖集（候选描述，非项目修订——N1）。
 *
 * items 按 bindingId 字典序存储（构造时排序——稳定序列化前提，卡 §5.5
 * 原文）；canonicalize 对乱序输入仍先排序兜底（双保险，见文件头确定性①）。
 * label 为人类可读标签，**不参与身份**（I-OPT-8：候选身份不含 UI 状态——
 * 改标签不改 CandidatePatchId/CandidateId）。
 *
 * 生命周期：值语义；身份计算（patchIdentity/candidateIdOf）不缓存——每次
 * 重算（O(补丁字节)），调用方无失效管理负担。
 */
struct CandidatePatch {
    std::vector<PatchItem> items = {};  ///< 补丁项（bindingId 字典序；可空＝
                                        ///  基线候选，卡 §5.5 ③）
    std::string label = {};             ///< 人类可读标签（不参与身份）
};

// =====================================================================
// 确定性序列化与身份（卡 §5.5/§4.2 原文签名）
// =====================================================================

/**
 * @brief 补丁 canonical 字节（卡 §5.5 原文签名）。
 *
 * 编码格式（codecVersion 1；全部多字节量为小端）：
 *   [0..7]   magic "IRDOPTP1"（8 字节 ASCII——卡面指定魔数）
 *   [8..11]  u32 codecVersion＝1（编码版本——升版即新身份，防跨版本误同）
 *   [12..15] u32 itemCount（补丁项数）
 *   每项（items 按 bindingId 字典序；canonicalize 内部重排序兜底）：
 *     u16 bindingId 字节长度＋bindingId UTF-8 字节（不含终止符）
 *     u8   值形态 tag（PatchValueTag 枚举值）
 *     'S' → f64 位模式 8 字节小端（IEEE 754；SI 真值；量化项为对齐后网格值）
 *     'E' → u32 枚举下标
 *     'D' → u16 离散引用字节长度＋UTF-8 字节
 *
 * @param patch [in] 补丁（重复 bindingId 抛——重复语义无法定义确定性序）。
 * @return canonical 字节（空补丁输出定长 16 字节头——空补丁有确定身份）。
 *
 * @throws OptimizationError(kOptPatchIllegal) items 内存在重复 bindingId
 *         （序列化前确定性校验——调用方构造违规，fail-fast）
 */
std::vector<std::uint8_t> canonicalize(const CandidatePatch& patch);

/**
 * @brief 补丁身份 CandidatePatchId（卡 §5.5 原文签名）＝SHA-256(canonical
 *        字节)，以 core::ContentIdentity 承载（"cid-<64hex>"）。
 *
 * 空补丁有确定身份（基线候选）；相同语义补丁必得相同身份（确定性 ①②④）。
 */
core::ContentIdentity patchIdentity(const CandidatePatch& patch);

/**
 * @brief 候选身份 CandidateId（本域自有格式——"cnd-<64hex>"）。
 *
 * 承载 core::Digest256 原始字节（32 字节）；toCanonical() 输出 "cnd-" 前缀
 * ＋64 位小写十六进制。不进入 core Id128 tag 冻结集（DOPT-2 口径——core
 * 冻结面不动）；.isValid()＝非全零（全零保留值纪律与 core 同源）。
 */
struct CandidateId {
    core::Digest256 bytes = {};  ///< 摘要原始字节；全零＝空（保留值）

    /// 规范文本 "cnd-<64 小写 hex>"（导出/日志书写形态）。
    std::string toCanonical() const;
    /// 非全零。
    bool isValid() const noexcept;
    bool operator==(const CandidateId& o) const noexcept { return bytes == o.bytes; }
    bool operator!=(const CandidateId& o) const noexcept { return !(*this == o); }
    /// 字节字典序（结果排序的终键——卡 §4.2/DOPT-8 稳定排序纪律的预留面）。
    bool operator<(const CandidateId& o) const noexcept { return bytes < o.bytes; }
};

/**
 * @brief 候选身份计算（卡 §4.2 公式原文）：
 *        CandidateId ＝ SHA-256( baselineRootOid ‖ baselineRootCv ‖
 *        CandidatePatchId )。
 *
 * 输入编码（裸字节直拼，80 字节定长——无分隔符歧义）：
 *   baselineRootOid 16 字节（core::ObjectId.bytes）‖
 *   baselineRootCv  32 字节（core::ContentVersion.bytes）‖
 *   CandidatePatchId 32 字节（core::ContentIdentity.bytes）。
 * 同一补丁作用于同一基线必得同 CandidateId（跨运行稳定、天然去重）；
 * **不含**显示单位、UI 排序状态、运行配置（I-OPT-8——label/排序不入身份）。
 *
 * @param baselineRoot [in] 基线根对象身份（RobotDesign 根对象——objectClosure
 *                     锚；须 isValid，违约抛）
 * @param baselineCv   [in] 基线根对象内容版本（修订内解析锚；须 isValid）
 * @param patch        [in] 候选补丁（重复 bindingId 抛——经 canonicalize）
 * @return 候选身份（内容寻址）
 *
 * @throws OptimizationError(kOptPatchIllegal) 补丁含重复 bindingId；
 *         OptimizationError(kOptInputInvalid) 基线身份/内容版本为全零
 *         （保留值不可作基线——调用方契约违约）
 */
CandidateId candidateIdOf(const core::ObjectId& baselineRoot,
                          const core::ContentVersion& baselineCv,
                          const CandidatePatch& patch);

// =====================================================================
// 补丁构造与校验（卡 §5.5 拒绝形态＋§5.4 锁定＋§5.7 阶段锁）
// =====================================================================

/**
 * @brief 单条补丁校验问题（比较型定位：码＋绑定 token＋对象定位＋细节）。
 *
 * 卡 §5.5 原文"携带 OPT-PATCH-ILLEGAL 的诊断返回（编排面）"的承载形态；
 * code 取值域＝kOptPatchIllegal/kOptVarLocked/kOptStageLocked。subject
 * 取自绑定 diagSubject（对象定位——ERR-01 逐项定位纪律）。
 */
struct PatchValidationIssue {
    std::string code = {};       ///< OPT-* 稳定码 token（DiagCodes.hpp 常量）
    std::string bindingId = {};  ///< 涉事绑定 token（整体性问题为空串）
    std::string subject = {};    ///< 关联对象 ObjectId 文本（绑定 diagSubject；
                                 ///  绑定未知时为空）
    std::string detail = {};     ///< 中文开发诊断（违规语义＋期望形态）
};

/**
 * @brief 补丁校验报告（编排面诊断轨——issues 非空即拒绝该候选）。
 */
struct PatchValidationReport {
    std::vector<PatchValidationIssue> issues = {};  ///< 全部问题（按发现序）
    /// @brief 无问题（补丁合法——可构造候选）。
    bool ok() const noexcept { return issues.empty(); }
    /// @brief 是否存在指定稳定码的问题（测试与编排定位用）。
    bool hasCode(std::string_view code) const noexcept;
};

/**
 * @brief 补丁项校验（诊断轨；不构造补丁——编排面在淘汰原因中携带问题）。
 *
 * 校验规则（逐条；全部不静默修正——NFR-COR-03）：
 *   ① bindingId 非空；② 词表匹配（matchDefinition）——无匹配＝未知绑定
 *   OPT-PATCH-ILLEGAL；③ 词表命中但条目在当前阶段未启用（enabledInStage*
 *   false）＝OPT-STAGE-LOCKED（阶段锁不降级——§5.7；如 StageB 引用电机
 *   型号）；④ 绑定集中存在该 bindingId（未知＝OPT-PATCH-ILLEGAL）；⑤ 绑定
 *   kind 与词表一致性（词表 Continuous 条目允许绑定声明 Quantized〔step>0
 *   的网格化使用形态——类型系统全阶段支持，卡 §5.3 量化变量说明〕；反向
 *   错配＝OPT-PATCH-ILLEGAL）；⑥ 重复 bindingId＝OPT-PATCH-ILLEGAL；
 *   ⑦ 绑定锁定（locked 或未授权 authorized==false——同一语义的防御面，
 *   §5.4）＝OPT-VAR-LOCKED（比较型定位：bindingId＋diagSubject）；⑧ 值
 *   检查：非有限＝OPT-PATCH-ILLEGAL；量化项先按 step 网格 round-half-even
 *   对齐再验边界（对齐后越界仍拒绝——不截断）；连续项越 [lower,upper] 界
 *   ＝OPT-PATCH-ILLEGAL；词表 valueMustBePositive 条目值≤0＝OPT-PATCH-ILLEGAL
 *   （I-MDL-11 传动比值域）；⑨ 枚举下标越 enumValues 界＝OPT-PATCH-ILLEGAL；
 *   ⑩ 离散引用空串＝OPT-PATCH-ILLEGAL。
 *
 * @param bindings [in] 研究定义的绑定集（补丁语义的来源）
 * @param stage    [in] 优化阶段（词表阶段启用集判定）
 * @param items    [in] 待校验补丁项（不要求预排序）
 * @return 校验报告（ok()＝全部通过）
 */
PatchValidationReport validatePatchItems(const std::vector<VariableBinding>& bindings,
                                         OptimizationStage stage,
                                         const std::vector<PatchItem>& items);

/**
 * @brief 构造候选补丁（fail-fast 轨——调用方错误抛 OptimizationError）。
 *
 * 内部先 validatePatchItems，全部通过后做构造规范化并按 bindingId 字典序
 * 排序返回：量化项替换为对齐后网格值；Scalar 项 -0.0 规范化为 +0.0；
 * 各项非当前形态成员清零/清空（枚举项清 scalar/discrete、标量项清
 * enumIndex/discrete 等——"相同语义补丁 ⇒ 相同字节"的构造前提）。
 *
 * @param bindings [in] 绑定集；@param stage [in] 阶段；@param items [in] 补丁项
 * @param label [in] 人类可读标签（不参与身份——I-OPT-8）
 * @return 规范化＋排序后的候选补丁
 *
 * @throws OptimizationError 校验任一拒绝（stableCode() 携带对应 OPT-* 码；
 *         message 含首个问题的定位细节）
 */
CandidatePatch makeCandidatePatch(const std::vector<VariableBinding>& bindings,
                                  OptimizationStage stage,
                                  const std::vector<PatchItem>& items,
                                  std::string label = {});

// =====================================================================
// 量化对齐（卡 §8.2 变量编码——确定性 round-half-even）
// =====================================================================

/**
 * @brief 步长网格对齐（round-half-even——确定性声明，卡 §8.2 原文）。
 *
 * 数学语义：k＝value/step；取最近的整数格；**恰在半格**（k 的小数部分
 * ＝0.5）时舍入到偶数格（IEEE 754-2008 默认舍入语义，与 std::nearbyint
 * 默认 FP 环境一致——但本函数不依赖全局 FP 舍入模式，自持实现，保证跨
 * 平台/线程确定性）。结果可能带浮点表示误差（如 0.3/0.1 的商）——对齐
 * 语义定义在"商的最近格"，商自身的 IEEE 754 舍入对同输入是确定的。
 *
 * @param value [in] 待对齐值（SI 单位；须有限）
 * @param step  [in] 量化步长（>0，与值同单位；须有限）
 * @return 对齐后的网格值（可能与 value 不精确相等——进补丁前替换）
 *
 * @throws OptimizationError(kOptInputInvalid) step≤0/非有限或 value 非有限
 *         （调用方契约违约——步长非法在绑定校验即拒，此处为最后防线）
 */
double quantizeToStepHalfEven(double value, double step);

// =====================================================================
// 补丁覆盖视图（P-OPT-2 数据面——候选物化通道的裁决前承载）
// =====================================================================

/**
 * @brief 补丁覆盖视图条目——一个绑定取值的"物化定位＋SI 真值"投影。
 *
 * P-OPT-2（卡 §16.3）安全设计："接口与数据面就绪；物化实现待裁决"——
 * 本结构即数据面：每条目把补丁值回投到权威字段定位（authorityFieldPath
 * ＋对象 diagSubject）并携带 SI 真值/枚举键文本；runtime 编译链（候选
 * RuntimeSnapshot 物化）消费本视图的落位随裁决（评估链路联调随裁决——
 * 契约 acceptance 4）。传动比条目的 scalarValue 即 c 口径值
 * （c＝Δq_joint/Δθ_motor，无量纲——V12-02/DOPT-12）。
 */
struct CandidateDesignOverlayEntry {
    BindingToken bindingId = {};      ///< 补丁项绑定 token
    std::string authorityFieldPath = {};  ///< 物化目标权威字段（绑定定位）
    std::string diagSubject = {};     ///< 关联对象 ObjectId 文本（对象定位）
    PatchValueTag valueTag = PatchValueTag::Scalar;  ///< 值形态
    std::string unitSymbol = {};      ///< SI 单位符号（"m"/"rad"/"1"；枚举/离散
                                      ///  为空——显示换算由 ui 消费 core::convert，
                                      ///  本值恒 SI 真值不换算，I-OPT-8）
    double scalarValue = 0.0;         ///< Scalar：SI 真值（c 口径见上）
    std::uint32_t enumIndex = 0;      ///< Enum：下标
    std::string enumValue = {};       ///< Enum：键文本（物化可读——如材料键
                                      ///  "aluminum"；MaterialRef 变更载体）
    std::string discreteRef = {};     ///< Discrete：离散引用（R2）
};

/**
 * @brief 补丁覆盖视图（P-OPT-2 数据面聚合）。
 *
 * entries 按 bindingId 字典序；patchId 为源补丁身份（候选编译身份要素——
 * opt.compile-identity 证据项的输入之一，卡 §11.1；完整编译身份在 runtime
 * 侧物化后闭环，随 P-OPT-2 裁决）。
 */
struct CandidateDesignOverlay {
    std::vector<CandidateDesignOverlayEntry> entries = {};  ///< bindingId 升序
    core::ContentIdentity patchId = {};  ///< 源补丁身份（isValid＝非空补丁）
};

/**
 * @brief 由补丁构造覆盖视图（P-OPT-2 数据面；不执行任何物化——卡 §16.3
 *        裁决前允许范围＝研究定义/导出/应用组装面）。
 *
 * 内部先 validatePatchItems（任何拒绝即抛——视图只对合法补丁定义），
 * 再从绑定补全权威字段定位/单位/枚举键文本。
 *
 * @param bindings [in] 绑定集；@param stage [in] 阶段；@param patch [in] 补丁
 * @return 覆盖视图（entries 按 bindingId 升序；空补丁 ⇒ 空条目＋patchId
 *         仍有效＝基线候选的空覆盖）
 *
 * @throws OptimizationError 同 validatePatchItems 拒绝面（fail-fast）
 */
CandidateDesignOverlay buildCandidateDesignOverlay(
    const std::vector<VariableBinding>& bindings, OptimizationStage stage,
    const CandidatePatch& patch);

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_CANDIDATE_PATCH_HPP
