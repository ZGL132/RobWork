/**
 * @file   ReportCodec.hpp
 * @brief  ReportCodec——报告双编码（Data/Full）的 canonical 编码与摘要
 *         （§4.4；reporting 单元**私有实现头**——§3.1 组成表把 ReportCodec
 *         列于 src/ 实现侧，不入 include/〔R-2〕；单元内消费方：ReportModel.cpp
 *         （make 身份计算）与本单元测试、后续构建器 RPT-T05/归档协调 RPT-T09）。
 *
 * 设计依据：
 *   - units/reporting.md §4.4（内容身份计算——编码规则原文全条：确定性
 *     二进制 magic `IRDRPT1`/`IRDRPTD1`＋字段按名序/章节按 order＋长度前缀
 *     ＋大端＋presence 字节＋UTF-8 禁 NUL＋浮点 IEEE754 位模式、NaN/±Inf
 *     编码入口拒绝〔evidence D-06 同源〕＋编码器版本号入编码＋纯函数
 *     同输入同字节〔NFR-COR-02〕；往返契约 parse(encode(x))==x——RPT-T03
 *     单测锁定）、§4.1（摘要一律 SHA-256 经 core ContentDigester 唯一
 *     算法；比较用字节等值）、§3.1 src/ 行（ReportCodec 归实现侧）
 *   - 需求 RPT-01（幂等导出以内容摘要判定）、NFR-COR-02（确定性）、
 *     CON-05（内容寻址——SHA-256 唯一算法）
 *   - 任务契约 tasks/foundation/RPT-T03.json acceptance 4（双编码分离与
 *     确定性二进制规则）＋acceptance 1（往返用例）
 *
 * 双编码分离（§4.4 字段表——进入编码字段**穷举**，逐项锁定）：
 *   - ReportCodec-Data（magic "IRDRPTD1"）→ dataIdentity：身份三元组＋
 *     revisionSeq＋level＋snapshotId/sliceId＋resultRefs 的 (runId,
 *     snapshotId, sliceId, inputBaselineId, caseScope) 集＋unitPreference＋
 *     选中章节 (sectionId, selected) 集。排除（§4.4 排除列原文）：评审
 *     元数据、entries 内容、诊断、生成时间/者。
 *   - ReportCodec-Full（magic "IRDRPT1"）→ contentIdentity：dataIdentity
 *     全部字段＋全部章节完整内容（entries/missingItems/status/diagnostics）
 *     ＋ReviewMetadata＋sectionModelVersion。排除：生成时间/生成者/生成器
 *     版本（"生成信息记录于工件清单"）；ReportId（§4.1——不承载内容信息、
 *     幂等判定不用它）、supersedes/reportVersion（演化链由
 *     ReviewMetadata.changeLog 承载入身份）、报告级汇总字段
 *     （evidenceRefs/diagRefs 为章节绑定去重并集、currentnessSummary/
 *     coverageSummary 为生成时刻投影、reproduction 随快照事实——均非
 *     §4.4 进入编码列成员，详见各字段注释）。注意 revisionSeq **不**在
 *     排除之列：§4.4 Data 行"进入编码"列显式含 revisionSeq，Full 的
 *     "dataIdentity 全部字段"经 Data 编码整体嵌入——revisionSeq 因此同入
 *     contentIdentity（§4.2 字段表"不参与身份语义"指其展示排序用途；
 *     身份编码归属以 §4.4 进入/排除表为准——契约 acceptance 4 的逐项
 *     锁定面）。
 *
 * "字段按名序"的实施口径：每个被编码结构体的字段按**字段名升序**（字节
 * 序比较）写入——各结构体的 canonical 顺序在其编码函数处逐字注释（复核
 * 与测试可对照）；章节按 order 序（§4.4 原文——输入必须已按 order 严格
 * 递增，违者 DataInvalid，不做静默重排）。
 *
 * 枚举编码口径：全部枚举以 uint8 声明序写入。各枚举声明序的冻结纪律见
 * 各自单元卡（core §4.7 词表/evidence EvidenceItemStatus"一经交付不得
 * 改动/插入"等）；上游词表迁移触发本编码器升版（版本号入编码——升版＝
 * 全体报告身份变化，走设计变更评审，§4.4 原文）。
 *
 * 线程安全：全部函数为可重入纯函数（无共享状态）。
 * 确定性：同输入同字节（无时钟/随机/locale 依赖——NFR-COR-02）。
 */

#ifndef SDURWS_IRD_REPORTING_SRC_REPORTCODEC_HPP
#define SDURWS_IRD_REPORTING_SRC_REPORTCODEC_HPP

#include <cstdint>
#include <vector>

#include <sdurws/ird/reporting/ReportModel.hpp>

namespace sdurws::ird::reporting {

/// ReportCodec-Full 的编码主体（§4.4 进入编码字段集——dataIdentity 全部
/// 字段＋全部章节完整内容＋ReviewMetadata＋sectionModelVersion）。
///
/// 与 ReviewReportFields 的分工：本结构**只含**入身份字段——make() 从
/// ReviewReportFields 显式抽取（逐字段映射即 §4.4 字段表的代码化），杜绝
/// 生成信息/报告级汇总字段被误编入身份。
struct ReportFullFields {
    /// dataIdentity 全部字段（§4.4"dataIdentity 全部字段"）。
    ReportSourceSpec data;
    /// 全部章节完整内容（entries/missingItems/status/diagnostics——§4.4 原文；
    /// 完整 ReviewReportSection，含 currentness/renderHint 等章节语义内容）。
    std::vector<ReviewReportSection> sections;
    /// 评审/签署元数据（§4.4——ReviewMetadata 全体入 contentIdentity）。
    ReviewMetadata review;
    /// 章节契约版本（§4.4——sectionModelVersion 入 contentIdentity）。
    std::string sectionModelVersion;

    bool operator==(const ReportFullFields& o) const
    {
        return data == o.data && sections == o.sections && review == o.review
               && sectionModelVersion == o.sectionModelVersion;
    }
    bool operator!=(const ReportFullFields& o) const { return !(*this == o); }
};

/**
 * @brief 报告双编码器（§4.4 ReportCodec——Data/Full 两套 canonical 编码＋
 *        SHA-256 摘要；纯函数集合，无实例状态）。
 *
 * 错误语义：编码/解码边界上的数据违约（NUL 字节、非有限浮点、magic/
 * 版本不符、字节截断/尾随、章节 order 未递增）一律抛
 * ReportError(ReportErrorCode::DataInvalid)——fail-fast，不做宽容归一化
 * （持久化文本/字节的二义在入口拒绝，AGENTS.md 错误语义）。
 */
class ReportCodec {
public:
    /// 编码器版本（入编码——升版＝全体报告身份变化，走设计变更评审）。
    static constexpr std::uint32_t kCodecVersion = 1;

    /// Full 编码 magic（§4.4 原文字面 "IRDRPT1"——7 字节 ASCII）。
    static constexpr std::string_view kMagicFull = "IRDRPT1";
    /// Data 编码 magic（§4.4 原文字面 "IRDRPTD1"——8 字节 ASCII）。
    static constexpr std::string_view kMagicData = "IRDRPTD1";

    // ---- Data 编码（dataIdentity 面） ----

    /// 编码数据源规格为 canonical 字节（§4.4 ReportCodec-Data）。
    /// @throws ReportError(DataInvalid) 字符串含 NUL/枚举越界等编码违约
    static std::vector<std::uint8_t> encodeData(const ReportSourceSpec& spec);

    /// 解码 Data canonical 字节（encodeData 的逆——parse(encode(x))==x）。
    /// @throws ReportError(DataInvalid) magic/版本不符、截断、尾随字节、
    ///         字符串 NUL、非有限浮点
    static ReportSourceSpec parseData(const std::vector<std::uint8_t>& bytes);

    /// dataIdentity 摘要（SHA-256 经 core ContentDigester 唯一算法——§4.1）。
    static core::ContentIdentity digestData(const ReportSourceSpec& spec);

    // ---- Full 编码（contentIdentity 面） ----

    /// 编码完整身份字段集为 canonical 字节（§4.4 ReportCodec-Full）。
    /// @throws ReportError(DataInvalid) 章节未按 order 严格递增、字符串
    ///         NUL、非有限浮点（NaN/±Inf 编码入口拒绝——evidence D-06 同源）
    static std::vector<std::uint8_t> encodeFull(const ReportFullFields& full);

    /// 解码 Full canonical 字节（encodeFull 的逆）。
    /// @throws ReportError(DataInvalid) 同 parseData；另含章节 order 违约
    static ReportFullFields parseFull(const std::vector<std::uint8_t>& bytes);

    /// contentIdentity 摘要（SHA-256——§4.1 唯一算法；幂等判定依据）。
    static core::ContentIdentity digestFull(const ReportFullFields& full);
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_SRC_REPORTCODEC_HPP
