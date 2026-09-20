/**
 * @file   Bundle.hpp
 * @brief  证据包组装——IArchiveWriter 注入契约（reporting 定义、io ZIP 薄适配，
 *         P-RPT-7）＋EvidenceBundleRequest/结果＋IEvidenceBundleSource 注入缝
 *         ＋IEvidenceBundleAssembler 及其产品实现（§7.6/§9.6，RPT-T10 产物）。
 *
 * 设计依据：
 *   - units/reporting.md §7.6（证据包导出全流程原文：EvidenceBundleRequest
 *     三字段→逐结果 envelope＋归档工件清单→组装 IEvidenceBundleAssembler
 *     〔reporting 所有〕四类条目→IArchiveWriter 逐条目写入→finish 原子替换；
 *     取消/失败→清理临时、目标不变；要素缺失→BundleIncomplete 全量列出，
 *     不产出半包；要素边界——快照/结果/复现要素的数据契约归 evidence，
 *     reporting 拥有打包格式与组装，ZIP 字节封装归 io〔注入〕；bundle 不是
 *     第二套项目格式——不含 HEAD/锁/.staging，D-15/P-RPT-7）、§9.6
 *     （IArchiveWriter 契约原文三方法——open/addEntry/finish；条目名纯相对
 *     正斜杠〔SP-2 口径——绝对/上溯拒绝〕；finish 原子替换到目标，异常/
 *     失败→清理临时、目标不变；非法调用行——条目名绝对/上溯＝io SP-2 拒绝）、
 *     §9 章首（取消＝协作检查点）、§3.3（注入模式：reporting 定义最小契约，
 *     L5 装配适配他单元事实面）、§13（RPT-03 行设计落点＝§7.6/§9.6）
 *   - 需求 RPT-03（证据包导出要素——快照/结果引用与复现要素）、NFR-COR-04
 *     （结论定位到快照与证据——bundle 是追溯链的交付形态：envelope→
 *     snapshotId 链逐字段入包）、NFR-COR-02（同输入同包——totalDigest 排除
 *     generatedAtUtc，D-04 同源）、PM-05（包导出协作/失败不留目标）、
 *     UX-03（取消非错误——检查点逐条目）
 *   - 任务契约 tasks/foundation/RPT-T10.json acceptance 1~5
 *
 * 背景说明（证据包在报告链中的位置与"谁拥有什么"）：
 *   证据包（ird-evidence-bundle/1）是一份**独立交付格式**：把一份已冻结报告
 *   所引用的全部结果证据按"引用"形态（refs-only）打包成单个外部文件，供
 *   离线复现与追溯审计。要素边界（§7.6 原文）逐条落位于本头的类型分工：
 *   - 快照/结果/复现要素的**数据契约**归 evidence（ReproductionBlock/
 *     ResultEnvelope/EvidenceItem 等值类型——本头只消费不重定义，P-RPT-9
 *     基线＝evidence.md v0.1 Draft）；
 *   - **打包格式与组装**归 reporting（bundle.json 的 schema、条目编址、
 *     totalDigest 规范域——本头冻结）；
 *   - **ZIP 字节封装**归 io（IArchiveWriter 的真实实现是 io 在其 ZipChannel
 *     之上的薄适配——P-RPT-7；io 未暴露通用 ZIP 写出公共接口前，测试经
 *     FakeArchiveWriter 注入，reporting 不私建第二套 ZIP 实现——NFR-DEP-03
 *     离线约束＋第三方依赖唯一渠道经 vcpkg）；
 *   - bundle 与项目镜像（.rwpack）严格区分：不含 HEAD/锁/.staging 等项目
 *     运行时要素（D-15/P-RPT-7——"项目镜像"语义归 project/io）。
 *
 * 复现要素的"不自算"纪律（§7.6 reproduction.json 注释行——acceptance 1）：
 *   reproduction.json 中的版本/种子/线程数/容差**一律取自 evidence 事实**，
 *   reporting 不解析、不换算、不聚合：
 *   - 版本列＝report.reproduction()（§4.2 复现要素字段——构建器经
 *     IReportResultSource::tryReproduction 冻结的 evidence ReproductionBlock
 *     值拷贝）逐字段原样承载；
 *   - 种子/线程数/容差的值位于快照的配置 canonical 字节内（evidence.md
 *     §4.1.2 Configuration 行："求解随机种子、线程配置与预算的身份归属＝
 *     Configuration 依赖条目"；N-5/N-9——配置 schema 归域，evidence 与
 *     reporting 均不解释），bundle 以**引用**形态承载
 *     （configKindToken＋contentIdentity——IEvidenceBundleSource::
 *     tryConfigurationRefs 投影），消费方复现时按快照物化配置 canonical
 *     取值。这是"refs-only 形态"在复现要素上的直接推力。
 *
 * IArchiveWriter 的放弃路径（§9.6 契约的实现义务澄清——落位登记 DTB §5.4）：
 *   §9.6 原文三方法无显式 abandon；"异常/失败→清理临时、目标不变"由 finish
 *   的失败路径承担（其签名注释原文），而"调用方在 finish 之前放弃会话"
 *   （协作取消/上游失败——本单元装配器的检查点逐条目取消即此形态）的清理
 *   义务由**实现的析构函数承担（RAII）**：析构时若存在未 finish 的临时区
 *   则清理之、目标保持不变。该澄清与 ReportBuildSession 的 RAII 析构守卫
 *   同型（§7.1 先例），不改动 §9.6 三方法签名。
 *
 * 线程契约：
 *   - IEvidenceBundleAssembler::assemble 并发安全（无共享可变状态；每次调用
 *     独立会话——§9.5 同则）；同一 (目标, 报告) 并发装配由调用方串行化
 *     （外部目标的文件系统语义归 io 实现）；
 *   - IArchiveWriter 会话单线程（open→addEntry×N→finish 顺序使用——§9.6
 *     线程行"会话单线程"同口径）；同一 writer 实例的两会话须串行；
 *   - 注入引用（resolver/resultSource/bundleSource/ioFactory）并发只读安全
 *     由实现方承诺（§9.1/§9.5 同则）；
 *   - 取消令牌调用方保证对 assemble 线程可见（§9 章首块同口径）。
 *
 * 确定性：同一 (报告值, 注入事实, 条目集) → bundle.json 的 totalDigest 逐
 *   字节相等（条目名/键序/编码全部 canonical——totalDigest 的规范化域排除
 *   generatedAtUtc，D-04：生成时刻是生成事实非内容事实；NFR-COR-02）。
 */

#ifndef SDURWS_IRD_REPORTING_BUNDLE_HPP
#define SDURWS_IRD_REPORTING_BUNDLE_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>   // core::DiagnosticRecord（失败诊断载体）
#include <sdurws/ird/core/Digest.hpp>     // core::Digest256/ContentIdentity/ContentDigester
#include <sdurws/ird/core/Identity.hpp>   // core::RunId（结果编址键）

#include <sdurws/ird/reporting/Builder.hpp>   // IReportResultSource/ReportCancelToken（§7.6 输入①）
#include <sdurws/ird/reporting/Errors.hpp>    // ReportError/ReportErrorCode
#include <sdurws/ird/reporting/Export.hpp>    // IReportResolver（reportId→报告值解析缝）
#include <sdurws/ird/reporting/Identity.hpp>  // ReportId
#include <sdurws/ird/reporting/Render.hpp>    // IReportIoFactory（JSON canonical 写出注入——SA-12）

namespace sdurws::ird::reporting {

// =====================================================================
// IArchiveWriter——归档写出注入契约（§9.6 原文三方法；reporting 定义、
// io 实现——ZipChannel 之上的薄适配，P-RPT-7）
// =====================================================================

/**
 * @brief 归档写出器（§9.6 原文契约——证据包的 ZIP 字节封装注入面）。
 *
 * 职责分工（P-RPT-7）：本接口只约定 reporting 侧需要的**会话语义**（开目标、
 * 逐条目追加、finish 原子发布），ZIP 容器格式/压缩/临时区管理全部归 io 实现
 * ——reporting 不感知字节封装细节，也不私建第二套 ZIP 实现（NFR-DEP-03：
 * 第三方依赖唯一渠道经 vcpkg，归 io 统一接入）。io 未暴露通用 ZIP 写出公共
 * 接口前（P-RPT-7/P-IO-1 合并裁决），测试经 FakeArchiveWriter 注入
 * （test/FakeArchiveWriter.hpp——随本任务交付的最小替身）。
 *
 * 会话纪律（§9.6 注释逐条）：
 *   - open：准备目标与**同卷临时区**（同卷保证 finish 的原子替换可在同一
 *     文件系统内完成——跨卷移动无原子性）；重复 open＝调用方违约；
 *   - addEntry：逐条目追加（只增）；条目名**纯相对正斜杠**（SP-2 口径——
 *     绝对路径/反斜杠/上溯段〔".."〕/空名一律拒绝——§9.6 非法调用行
 *     "IArchiveWriter 条目名绝对/上溯（io SP-2 拒绝）"；同名重复追加＝违约）；
 *   - finish：**原子替换到目标**（临时区整体发布为目标文件）；异常/失败→
 *     清理临时、目标不变（签名注释原文——失败路径的清理义务在实现侧）；
 *   - 放弃路径（本头文件头"IArchiveWriter 的放弃路径"节）：不调用 finish
 *     即废弃会话（协作取消/上游失败）→ 实现在析构中清理临时（RAII）、
 *     目标不变。
 *
 * finish 返回值＝本包字节面的摘要（实现自定义规范化域——容器级完整性
 * 凭据；与 bundle.json 内 totalDigest 无关：后者是 reporting 侧条目集的
 * 内容摘要，两者互不替代）。装配器不消费该返回值。
 *
 * 错误轨：契约违约（重复 open/非法条目名/终态后使用）＝ fail-fast
 * （std::invalid_argument 或 std::logic_error）；环境/写出失败＝ ReportError
 * 或派生 std::runtime_error（装配器统一捕获映射为 Failed——不吞错，
 * NFR-COR-03）。
 *
 * 线程约束：会话单线程（open→addEntry×N→finish 顺序使用；见文件头线程契约）。
 */
class IArchiveWriter {
public:
    virtual ~IArchiveWriter() = default;

    /**
     * @brief 开启归档会话（准备目标与同卷临时区——§9.6 原文注释）。
     * @param target [in] 归档目标文件路径（外部路径——最终交付物；父目录
     *               不存在时实现可创建或拒绝，属 io 实现口径）
     *
     * @throws std::logic_error 本 writer 已处于会话中（重复 open＝违约）
     * @throws ReportError / 派生 std::runtime_error 目标不可达等环境失败
     *         （调用方得到 Failed——临时区由实现清理）
     */
    virtual void open(const std::filesystem::path& target) = 0;

    /**
     * @brief 追加单个条目（只增——§9.6 原文签名）。
     * @param entryName [in] 条目名（包内相对路径）；**纯相对正斜杠**——
     *                  绝对路径/反斜杠/".." 上溯段/空名拒绝（SP-2 口径，
     *                  §9.6 非法调用行）；同名重复追加拒绝
     * @param bytes     [in] 条目完整字节（装配器交付 canonical 字节——
     *                  JSON 文档经注入 io 写出器序列化，SA-12）
     *
     * @throws std::invalid_argument 条目名违反 SP-2/同名重复（调用方违约
     *         ——fail-fast）
     * @throws ReportError / 派生 std::runtime_error 写出环境失败（注入
     *         点——RP-BUN-1"IArchiveWriter 异常/失败"观测路径）
     */
    virtual void addEntry(const std::string& entryName,
                          const std::vector<std::uint8_t>& bytes) = 0;

    /**
     * @brief 终结会话：原子替换到目标（§9.6 原文签名与注释——"原子替换到
     *        目标；异常/失败→清理临时、目标不变"）。
     * @return 本包字节面摘要（实现自定义规范化域——容器级凭据；装配器
     *         不消费）
     *
     * @throws ReportError / 派生 std::runtime_error 发布失败（临时区已被
     *         实现清理、目标不变——可重试）
     */
    virtual core::Digest256 finish() = 0;
};

// =====================================================================
// 证据包格式常量（bundle 格式归 reporting——§7.6 要素边界；字面为格式
// 契约的一部分，一经交付不得改动——持久化判别字段，runtime 同款纪律）
// =====================================================================

/// bundle.json 的 schemaVersion 字面（§7.6 原文 "ird-evidence-bundle/1"）。
inline constexpr std::string_view kBundleSchemaVersion = "ird-evidence-bundle/1";

/// 包清单条目名（bundle 自描述清单——六字段，见 EvidenceBundleAssembler）。
inline constexpr std::string_view kBundleManifestName = "bundle.json";

/// 复现要素条目名（§7.6 原文 reproduction.json——顶层条目）。
inline constexpr std::string_view kBundleReproductionName = "reproduction.json";

/// totalDigest 规范化编码域的格式魔数（8 字节 ASCII——ReportCodec
/// "IRDRPTD1/IRDRPT1" 与 manifest "IRDRPTM1" 同族；总摘要编码域的判别头）。
inline constexpr std::string_view kBundleDigestMagic = "IRDRPTB1";

/// totalDigest 编码器版本（编码域自带版本——升级走新版本号，不静默变义）。
inline constexpr std::uint32_t kBundleDigestCodecVersion = 1;

/**
 * @brief 逐运行快照引用条目的包内条目名（§7.6 "snapshot/<run>/" 的文件化）。
 *
 * ZIP 容器以"路径式条目名"表达目录层级——每运行的输入快照引用元数据落于
 * snapshot/<runId 规范文本>/snapshot.json。runId 以其规范文本（"run-<32hex>"）
 * 入名：字符集受限（小写 hex），天然满足 SP-2 纯相对正斜杠约束。
 *
 * @param runId [in] 运行身份（保留值→调用方违约——本函数不做校验，装配
 *              前已核对）
 * @return 条目名（如 "snapshot/run-0ab3.../snapshot.json"）
 */
std::string bundleSnapshotEntryName(core::RunId runId);

/**
 * @brief 逐运行结果引用条目的包内条目名（§7.6 "results/<run>/" 的文件化）。
 * @param runId [in] 运行身份
 * @return 条目名（如 "results/run-0ab3.../result.json"）
 */
std::string bundleResultEntryName(core::RunId runId);

/**
 * @brief evidence 证据项状态的包内 JSON 词面（results/<run>/ 证据清单
 *        status 列的序列化形式）。
 *
 * 权威声明（O-13 同则——消费侧零重定义）：evidence::EvidenceItemStatus
 * 枚举值本身不作任何改动/合并（类型恒等由契约测试 static_assert 常驻
 * 自证）；本函数只是 bundle 格式对五值的**序列化词面映射**（kebab 稳定
 * 字面——持久化判别字段，一经交付不得改动），词表语义权威仍在 evidence
 * （§6.2 五态判定后果原文）。全枚举 switch 无 default——evidence 侧追加
 * 枚举值时本表编译期告警暴露遗漏（Errors.hpp token() 同款防漏机制）。
 *
 * @param status [in] evidence 证据项状态（五值全表）
 * @return 稳定词面（"satisfied"/"missing"/"invalid"/"unverified"/
 *         "not-applicable"；静态存储期）
 */
std::string_view bundleItemStatusToken(evidence::EvidenceItemStatus status) noexcept;

// =====================================================================
// 请求与结果（§7.6 原文形状）
// =====================================================================

/**
 * @brief 证据包组装请求（§7.6 原文三字段：EvidenceBundleRequest{reportId,
 *        runs[]〔默认＝报告 resultRefs〕, target(外部路径)}）。
 *
 * 字段语义：
 *   - reportId：已冻结报告的对象身份——包的身份锚（bundle.json 的 reportId
 *     列）；须可经 IReportResolver 解析（不可解析＝Usage）；
 *   - runs：显式选择的结果集。**空向量＝缺省语义**（§7.6 原文"默认＝报告
 *     resultRefs"——装配器取报告的全部结果引用）；非空时每一项必须是报告
 *     resultRefs 的成员且无重复（越界/重复＝Usage——包是"这份报告的证据
 *     包"，选报告未引用的结果会让包与其身份锚脱钩）；
 *   - target：外部目标文件路径（最终交付物——ZIP 容器；空路径＝Usage）。
 *
 * 值语义；线程安全：纯值（assemble 并发调用各持独立请求副本）。
 */
struct EvidenceBundleRequest {
    /// 报告对象身份（包的身份锚——bundle.json reportId 列；非零保留值）。
    ReportId reportId{};
    /// 显式结果集（空＝缺省取报告 resultRefs 全集；非空须 ⊆ resultRefs）。
    std::vector<core::RunId> runs;
    /// 外部目标文件路径（ZIP 容器交付物；非空——io 实现按其口径处理父目录）。
    std::filesystem::path target;
};

/**
 * @brief 组装结果状态（三态——§7.6"取消/失败→清理临时、目标不变"与
 *        UX-03 的结果侧承载；ArchiveOutcome/ReportBuildOutcome 同款形态）。
 */
enum class BundleStatus : std::uint8_t {
    Completed,  ///< completed——包已发布到目标（totalDigest 在场）
    Canceled,   ///< canceled——协作取消（UX-03：正常取消非错误；error 恒
                ///< 缺席、totalDigest 恒缺席；临时清理由 writer RAII 承担、
                ///< 目标不变）
    Failed,     ///< failed——要素缺失/写出失败（error 在场＋diagnostics
                ///< 随交付；目标不变、零半包）
};

/// 取组装状态稳定 token（kebab 形；静态存储期——NFR-COR-02 同口径）。
std::string_view token(BundleStatus status) noexcept;

/**
 * @brief 证据包组装结果（BundleStatus 三形态约定）：
 *   - Completed＝totalDigest 在场（与 bundle.json 内 totalDigest 列同值）、
 *     error 缺席、diagnostics 为空；
 *   - Canceled＝其余三成员全空（UX-03——与构建器/归档 outcome 的取消形态
 *     同款：全空即取消信号，调用方持令牌可判别取消与失败）；
 *   - Failed＝error 在场（BundleIncomplete 时 detail 全量列出缺失要素——
 *     不产出半包；写出失败时为通道错误），diagnostics 随交付。
 *
 * 值语义；线程安全：纯值。
 */
struct EvidenceBundleOutcome {
    BundleStatus status = BundleStatus::Failed;        ///< 结果状态
    /// 包内容总摘要（Completed 在场——bundle.json totalDigest 列同值）。
    std::optional<core::Digest256> totalDigest;
    /// 过程诊断（Failed 随交付；成功/取消为空——UX-03/NFR-COR-03）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// 失败错误（Failed 在场；Canceled/成功恒缺席）。
    std::optional<ReportError> error;
};

// =====================================================================
// IEvidenceBundleSource——归档事实注入缝（§7.6 输入②"归档工件清单"＋
// 快照冻结凭据的投影承载；reporting 定义最小契约，L5 装配以 project
// RunManifest/evidence 快照事实适配——§3.3 注入模式）
// =====================================================================

/**
 * @brief 配置引用条目（reproduction.json 逐运行配置引用集的元素）。
 *
 * 承载快照的配置 canonical **引用**（不承载字节——refs-only）：种子/线程数/
 * 容差等确定性要素的值在配置 canonical 字节内（域 schema 归域——evidence
 * N-5/N-9 同则，reporting 不解释），消费方按 contentIdentity 从快照物化
 * canonical 后按域 schema 读值。两字段与 evidence ConfigEntry 的同名投影
 * 一一对应（类型恒等由契约测试 static_assert 常驻自证——本结构是
 * reporting 侧最小值投影，非第二定义）。
 *
 * 值语义；线程安全：纯值。
 */
struct BundleConfigRef {
    /// 配置种类 token（归域；非空＋无 NUL——evidence ConfigEntry 投影）。
    std::string configKindToken;
    /// 配置内容身份（非零——按它物化 canonical 字节；evidence 投影）。
    core::ContentIdentity contentIdentity;
};

/// 全字段精确相等（纯值比较）。
bool operator==(const BundleConfigRef& a, const BundleConfigRef& b) noexcept;
bool operator!=(const BundleConfigRef& a, const BundleConfigRef& b) noexcept;

/**
 * @brief 证据包归档事实注入源（§7.6 输入②的注入承载——逐运行三项事实）。
 *
 * 为什么需要独立注入缝：§7.6 逐结果输入①＝resultSource.tryEnvelope(runId)
 * （复用 Builder.hpp 的 IReportResultSource——原文同名方法），而"归档工件
 * 清单（project runDir 逐字节读取授权）"与快照冻结凭据（caseSet 摘要、配置
 * canonical 引用）是 project/evidence 侧事实——reporting 无其事实面且禁止
 * 自算（§7.6 reproduction.json 注释行"数值来自 evidence 复现块与配置
 * canonical，不自算"；NFR-COR-04 追溯链的数据源纪律）。本接口即 §3.3 注入
 * 模式的最小承载：L5 装配以 project RunManifest（归档工件清单——envelope
 * 工件行摘要）＋evidence 快照事实（requiredCaseSetId/configurationRefs 投影）
 * 实现之；reporting 只消费投影值，不触碰 project/evidence 内部编址。
 *
 * 三方法统一约定：命中→值拷贝；该运行的事实不可解析/缺位→nullopt（装配器
 * 据此计入 BundleIncomplete 缺失清单——全量列出，不短路）。返回 nullopt 是
 * **数据缺位**信号而非错误——调用方（装配器）以要素缺失语义处置。
 *
 * 生命周期：L5 装配持有实现实例，装配器仅借用（构造引用须覆盖 assemble()
 * 调用期间）。线程契约：并发只读安全（实现方承诺——§9.1 注入源同则）。
 */
class IEvidenceBundleSource {
public:
    virtual ~IEvidenceBundleSource() = default;

    /**
     * @brief 取运行归档 envelope 工件的字节摘要（§7.6 results/<run>/
     *        "envelope 摘要"的数据源——归档工件清单事实投影）。
     *
     * @param runId [in] 运行身份
     * @return 归档 envelope 工件的 SHA-256 字节摘要（ RunManifest 工件行的
     *         事实投影——reporting 不自算摘要、不重读归档文件重算）；
     *         不可解析→nullopt
     *
     * 线程安全：并发只读安全。
     */
    virtual std::optional<core::Digest256> tryEnvelopeDigest(core::RunId runId) const = 0;

    /**
     * @brief 取运行来源快照的必验工况集冻结凭据（§7.6 snapshot/<run>/
     *        "caseSet 摘要"的数据源）。
     *
     * "caseSet 摘要"＝evidence RequiredCaseSet::requiredCaseSetId（§4.1.3：
     * "冻结凭据＝entries 规范编码摘要"——evidence builder/codec 计算，非
     * 申报值）；本方法是其按运行的投影。reporting 不重算工况集摘要
     * （SHA-256 唯一算法的调用权在 evidence 侧——CR-02 不私设第二哈希路径
     * 的消费面纪律）。
     *
     * @param runId [in] 运行身份
     * @return 必验工况集冻结凭据（cid- 32 字节身份）；不可解析→nullopt
     *
     * 线程安全：并发只读安全。
     */
    virtual std::optional<core::ContentIdentity> tryCaseSetDigest(core::RunId runId) const = 0;

    /**
     * @brief 取运行来源快照的配置 canonical 引用集（reproduction.json 的
     *        种子/线程数/容差承载引用——见 BundleConfigRef 注释）。
     *
     * @param runId [in] 运行身份
     * @return 配置引用集（快照 configurationRefs 的投影，保序承载——
     *         evidence 不排序、reporting 亦不重排；空向量＝快照合法地无
     *         配置条目〔P-EV-7 同精神的空集合法形态〕）；不可解析→nullopt
     *
     * 线程安全：并发只读安全。
     */
    virtual std::optional<std::vector<BundleConfigRef>>
    tryConfigurationRefs(core::RunId runId) const = 0;
};

// =====================================================================
// IEvidenceBundleAssembler——组装接口（§7.6"组装（IEvidenceBundleAssembler，
// reporting 所有）"）与产品实现
// =====================================================================

/**
 * @brief 证据包组装器接口（§7.6——服务级，无会话状态）。
 *
 * 契约（§7.6 流程行逐条）：
 *   - 前置：reportId 可解析为冻结报告；runs（缺省＝报告 resultRefs）⊆
 *     报告 resultRefs 且无重复；target 非空；
 *   - 后置：成功＝目标文件为完整包（四类条目齐备——不产出半包）；失败/
 *     取消＝目标不变、零临时残留；
 *   - 错误：BundleIncomplete（要素缺失——detail 全量列出）/Usage（调用方
 *     违约）/ExportFailed（写出通道失败）；
 *   - 取消：检查点逐条目（收集阶段逐运行/写出阶段逐条目——§10.1 RP-BUN-1
 *     观测点"检查点逐条目"）。
 */
class IEvidenceBundleAssembler {
public:
    virtual ~IEvidenceBundleAssembler() = default;

    /**
     * @brief 组装证据包（§7.6 全流程：解析→收集→逐条目写出→finish）。
     *
     * @param request [in] 组装请求（身份/结果集/目标——合法性见类型注释）
     * @param cancel  [in] 协作取消令牌（可空＝不取消；检查点＝收集阶段
     *                逐运行＋写出阶段逐条目）
     *
     * @return 组装结果（三形态约定见 EvidenceBundleOutcome 注释）
     *
     * 复杂度：O(R·(E＋C))（逐运行事实收集；R＝运行数、E＝证据项数、
     * C＝配置数）＋O(N log N)（条目名排序——N＝条目数）。非热点路径。
     */
    virtual EvidenceBundleOutcome assemble(const EvidenceBundleRequest& request,
                                           const ReportCancelToken* cancel = nullptr) = 0;
};

/**
 * @brief 证据包组装器产品实现（§7.6 流程的编排体——RPT-T10 交付主体）。
 *
 * assemble 编排明细（acceptance 1~3 的承载）：
 *   1. 请求校验（Usage fail-fast）：reportId 非零、target 非空；经
 *      IReportResolver 解析报告值（不可解析＝Usage；解析结果 reportId 与
 *      请求不一致＝Usage——解析缝违约）；runs 去重核对（重复＝Usage）与
 *      resultRefs 成员核对（越界＝Usage）；runs 为空→取报告 resultRefs
 *      全集（§7.6 缺省语义）。
 *   2. 取消检查点（入轨）→Canceled（尚无会话——零临时）。
 *   3. **收集阶段（零 IO——先全量收集，保证"要素缺失→拒绝、不产出半包"
 *      且零临时文件）**：逐运行（按 runId 规范文本稳定排序——条目与
 *      sourceRuns 的确定性）取 ①tryEnvelope（evidence 包络只读值——证据
 *      清单数据源）②tryReproduction（复现块——report.reproduction 之外
 *      的逐运行一致性旁证位；本实现以报告级复现块为版本列数据源〔§4.2
 *      冻结值〕，逐运行复现块缺位按要素缺失计）③tryEnvelopeDigest
 *      ④tryCaseSetDigest ⑤tryConfigurationRefs；复现块完整性核对
 *      （productVersion/evidenceContractVersion 非空——§4.1.2 必填字段；
 *      缺失/空值计入缺失清单）。收集阶段逐运行取消检查点。任一缺失→
 *      **不开启 writer**：Failed＋BundleIncomplete，detail 全量列出全部
 *      缺失项（"<要素>@<runId>" 形态逐项列出——acceptance 2"全量列出"）。
 *   4. **写出阶段**：writer.open(target)（失败→Failed 透传——writer 自理
 *      临时清理）；逐条目以 canonical 顺序写出：
 *      snapshot/<run>/snapshot.json → results/<run>/result.json →
 *      reproduction.json → bundle.json（清单最后写——totalDigest 依赖全部
 *      其余条目）；JSON 文档经注入 IReportIoFactory 的 JSON 写出器序列化
 *      （SA-12——reporting 不自建序列化器），逐条目前取消检查点（检查点
 *      逐条目——取消→Canceled，writer 由其属主析构清理临时〔RAII——
 *      文件头"放弃路径"节〕，目标不变）。
 *   5. totalDigest 计算：规范化编码域＝magic "IRDRPTB1"＋编码器版本 u32
 *      ＋条目数 u32＋逐条目（按条目名字节序稳定排序）：名长 u32＋名字节＋
 *      条目 SHA-256 32 字节（整数大端——ReportCodec §4.4 同口径），SHA-256
 *      经 core ContentDigester 唯一算法；**排除 bundle.json 自身**（其
 *      generatedAtUtc 是生成事实——D-04；排除后同输入同摘要，NFR-COR-02）。
 *   6. writer.finish()（原子替换到目标；失败→writer 清理临时、目标不变，
 *      装配器映射 Failed——ExportFailed 或 writer 携带的原码）→Completed
 *      ＋totalDigest。
 *
 * 注入面（构造引用为借用——L5 装配/测试持有，覆盖 assemble() 存续期）：
 *   - IReportResolver（Export.hpp 落位偏差①的解析缝复用）：reportId→报告值；
 *   - IReportResultSource（Builder.hpp——§7.6 输入①同名方法复用）：
 *     tryEnvelope/tryReproduction；
 *   - IEvidenceBundleSource（本头——§7.6 输入②注入承载）；
 *   - IArchiveWriter（本头——ZIP 字节封装注入，P-RPT-7）；
 *   - IReportIoFactory（Render.hpp——JSON canonical 写出注入，SA-12）。
 *
 * 线程契约：assemble 并发安全（全部注入引用只读消费、无共享可变状态）；
 * 会话单线程使用 writer（其契约）。
 */
class EvidenceBundleAssembler final : public IEvidenceBundleAssembler {
public:
    /**
     * @brief 注入构造（L5 装配绑定——引用借用语义，见类注）。
     *
     * @param resolver     [in] 报告值解析器（borrow；并发只读）
     * @param resultSource [in] 归档结果注入源（borrow；§7.6 输入①）
     * @param bundleSource [in] 归档事实注入源（borrow；§7.6 输入②）
     * @param writer       [in] 归档写出器（borrow、**可写引用**——ZIP
     *                     通道适配，P-RPT-7）
     * @param ioFactory    [in] io 注入工厂（borrow；JSON canonical 写出
     *                     载体——SA-12）
     */
    EvidenceBundleAssembler(const IReportResolver& resolver,
                            IReportResultSource& resultSource,
                            IEvidenceBundleSource& bundleSource,
                            IArchiveWriter& writer,
                            const IReportIoFactory& ioFactory) noexcept;

    /// 非拷贝（借用语义——拷贝即双属主）。
    EvidenceBundleAssembler(const EvidenceBundleAssembler&) = delete;
    EvidenceBundleAssembler& operator=(const EvidenceBundleAssembler&) = delete;

    EvidenceBundleOutcome assemble(const EvidenceBundleRequest& request,
                                   const ReportCancelToken* cancel = nullptr) override;

private:
    const IReportResolver* m_resolver;         ///< 报告值解析器（借用）
    IReportResultSource* m_resultSource;       ///< 归档结果注入源（借用）
    IEvidenceBundleSource* m_bundleSource;     ///< 归档事实注入源（借用）
    IArchiveWriter* m_writer;                  ///< 归档写出器（借用、可写）
    const IReportIoFactory* m_ioFactory;       ///< io 注入工厂（借用）
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_BUNDLE_HPP
