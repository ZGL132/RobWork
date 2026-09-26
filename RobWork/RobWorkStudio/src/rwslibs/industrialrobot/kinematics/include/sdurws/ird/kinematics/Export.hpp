/**
 * @file   Export.hpp
 * @brief  结果导出值行（KIN-08/§7.4）——把运动学结果整理为 JSON/CSV
 *         **值行数据**（含 snapshotId/对象引用 pointOid/regionOid）的
 *         纯函数面；导出文件头声明结果来源（results/<run-id>/ 归档或
 *         会话内存）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Export.hpp 行——T09 增列，
 *     登记随 §14.6 v0.9）、§7.4（结果筛选与导出——"本单元把结果整理为
 *     值行（JSON/CSV 行数据，含 snapshotId/对象引用），写出经 io 通道
 *     （插件侧触发）；导出不产生修订（AT-04）；导出自 results/<run-id>/
 *     归档或会话内存结果（来源在导出文件头声明）"）、§9.8（L-K10 结果
 *     导出数据流、kinematics.export-results 命令——readOnlyAllowed）
 *   - REQUIREMENTS KIN-08（JSON/CSV 导出）、AT-04（会话预览/导出不产生
 *     项目修订——显式应用才经命令端口）
 *   - 任务契约 tasks/foundation/WP-15-T09.json acceptance 2
 *
 * 背景说明（为什么本头只有"值行"没有文件写出）：架构分工（§7.4/§2.3
 * 不拥有清单）——CSV/JSON **写出**归 io 通道（AtomicFile；触发方在插件
 * 侧），本单元只交付"数据整理"：结果值 → 行结构 → JSON/CSV 文本。本头
 * 全部函数为纯函数（无项目/命令/归档通道可触），"导出不产生修订"
 * （AT-04）由结构保证：类型面上不存在任何可产生修订的依赖。
 *
 * 确定性（NFR-COR-01/02 在导出面的落点）：同一值行包两次编码逐字节一致
 * ——JSON 字段定序、CSV 列定序、浮点数经最短往返十进制表示（表示唯一
 * 且可精确回读——std::to_chars 最短往返保证）、无时间戳/环境熵
 * 入文本（来源与身份字段全部来自结果绑定值）。
 *
 * 线程安全：全部纯函数可重入（§3.4 总约定）。
 */

#ifndef IRD_KINEMATICS_EXPORT_HPP
#define IRD_KINEMATICS_EXPORT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>      // ContentIdentity（快照身份）
#include <sdurws/ird/core/Evaluation.hpp>  // EvaluationMode（模式词表）
#include <sdurws/ird/core/Identity.hpp>    // ObjectId（对象引用）
#include <sdurws/ird/kinematics/KinTypes.hpp>   // IkSolutionSet/FilteredSolutionRecord
#include <sdurws/ird/kinematics/Sampling.hpp>   // RegionCoverageComputation（覆盖行来源）

namespace sdurws::ird::kinematics {

// =====================================================================
// 导出文件头值模型（§7.4"来源在导出文件头声明"的承载）
// =====================================================================

/**
 * @brief 导出结果的来源词表（§7.4 两分——归档或会话内存）。
 */
enum class KinExportSource : std::uint8_t {
    /// results/<run-id>/ 归档结果（project 归档通道产出；runId 必填）。
    ArchivedRun = 1,
    /// 会话内存结果（会话级单点求解等未归档产物；runId 必空）。
    SessionMemory = 2,
};

/**
 * @brief 导出文件头（JSON 顶层字段/CSV '#' 注释头的数据来源）。
 *
 * 值语义纯结构；线程安全（并发只读）。runId 纪律：ArchivedRun 必须携带
 * 归档 run 标识、SessionMemory 必须为空串——buildKinExportPackage 校验
 * （调用方错误的 fail-fast 轨，§9.1）。
 */
struct KinExportHeader {
    /// 结果来源（导出文件头声明义务——§7.4）。
    KinExportSource source = KinExportSource::SessionMemory;
    /// 归档 run 标识（execution RunId 文本；仅 ArchivedRun 非空）。
    std::string runId;
    /// 结果绑定快照内容身份（provenance→快照身份——§2.5 报告行承接）。
    core::ContentIdentity snapshotId;
    /// 评估模式（入导出头——Quick 结果的筛选效力门禁归汇总层，此处如实
    /// 承载模式值供消费方识别）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;

    bool operator==(const KinExportHeader& o) const
    {
        return source == o.source && runId == o.runId
            && snapshotId == o.snapshotId && mode == o.mode;
    }
    bool operator!=(const KinExportHeader& o) const { return !(*this == o); }
};

// =====================================================================
// 值行模型：解行/过滤诊断行/区域覆盖行
// =====================================================================

/**
 * @brief 单个可行解的导出值行（IkSolutionSet::solutions 逐解一行——
 *        rank 即 sorted() 稳定序位置）。
 *
 * 全部物理量单位（AGENTS §2.5）：q 逐自由度 rad|m（权威 SI）；残差
 * 位置 m／姿态 rad；裕量/可操作度/条件数为无量纲量（与 KinematicSolution
 * 同口径）。身份/引用字段：snapshotId（来源结果绑定）、pointOid/
 * conditionId（对象引用——§6.1 targetRef 同源）。值语义纯结构；线程安全。
 */
struct KinSolutionExportRow {
    /// 稳定序位置（0 基计数——生产端 solutions[] 已按 §6.3 四键排序，
    /// rank 即该序上的下标；无量纲）。
    std::uint64_t rank = 0;
    /// 来源快照内容身份（逐行携带——CSV 消费方无需回查文件头即可溯源）。
    core::ContentIdentity snapshotId;
    /// 任务点对象身份（对象引用——§6.1 targetRef.pointOid 同源）。
    core::ObjectId pointOid;
    /// 工况对象身份（可空——单点求解为空；批量通道逐工况必填）。
    std::optional<core::ObjectId> conditionId;
    /// 权威关节向量（rad|m；链序）。
    std::vector<double> q;
    /// 收敛/复验残差：位置 m、姿态 rad。
    double positionResidual = 0.0;
    double orientationResidual = 0.0;
    /// 有界关节最小裕量（无量纲；全无界＝+∞——经 to_chars 输出 "inf"）。
    double minimumJointMargin = 0.0;
    /// 可操作度（无量纲，D-KIN-2）。
    double manipulability = 0.0;
    /// 条件数（无量纲，D-KIN-2）。
    double conditionNumber = 0.0;
    /// 碰撞评价是否在场（false＝证据缺失——消费方不得据此行解读无碰撞）。
    bool collisionEvaluated = false;
    /// 碰撞判定（生产端不变式：解行恒 false——碰撞解在过滤诊断行）。
    bool collisionInCollision = false;
    /// 产生本解的初值下标（溯源——排序第 4 键素材）。
    std::uint32_t sourceInitIndex = 0;
    /// 本初值迭代次数（计数，无量纲）。
    std::uint32_t iterations = 0;
    /// 构型签名（记录键——I-KIN-3 全精度定宽十六进制串）。
    std::string signature;
};

/**
 * @brief 单条被硬过滤解的诊断导出值行（§6.1"为什么少了解"的导出面——
 *        原因/对象对/指标逐行承载）。
 *
 * 与 KinSolutionExportRow 的分工：本行**不是可行解**，消费方不得把它
 * 当作候选（§6.1"不计入解集排序序列、不进入可行素材"）。值语义纯结构；
 * 线程安全。
 */
struct KinFilteredSolutionExportRow {
    /// 来源快照内容身份（同解行）。
    core::ContentIdentity snapshotId;
    /// 任务点对象身份（同解行）。
    core::ObjectId pointOid;
    /// 工况对象身份（可空——同解行）。
    std::optional<core::ObjectId> conditionId;
    /// 过滤原因 token（小写连字符词表：residual-recheck／joint-limit／
    /// collision——SolutionFilterReason 三值的一一映射，导出面词表在
    /// src/Export.cpp 唯一书写）。
    std::string reason;
    /// 被过滤构型（rad|m；链序——诊断复算素材）。
    std::vector<double> q;
    /// 复验残差（m／rad——首个命中阶段的当次值）。
    double positionResidual = 0.0;
    double orientationResidual = 0.0;
    /// 构型级指标（无量纲——与解行同口径）。
    double minimumJointMargin = 0.0;
    double manipulability = 0.0;
    /// 碰撞对象对数量（仅 Collision 原因非零；对象对本体为 ObjectId 强
    /// 类型对，值行只携数量与 canonical 文本列——见 collisionPairs）。
    std::uint64_t collisionPairCount = 0;
    /// 碰撞对象对 canonical 文本（成对展平 "obj-a|obj-b|obj-a2|obj-b2"——
    /// 竖线分隔；非 Collision 原因为空串。canonical 文本经 core
    /// ObjectId::toCanonical，无名称拼接——R-4）。
    std::string collisionPairs;
    /// 产生该候选的初值下标。
    std::uint32_t sourceInitIndex = 0;
    /// 该候选的迭代次数。
    std::uint32_t iterations = 0;
    /// 构型签名（记录键）。
    std::string signature;
};

/**
 * @brief 单区域单口径的覆盖计数导出值行（§7.2 逐样本状态表按
 *        (regionOid, 样本种类) 分组 tally——"对象引用 regionOid"的落点）。
 *
 * 六计数与 CoverageTotals 同口径（整数计数，无量纲；分母＝该区域该口径
 * 的计划样本总数），但**不携带降级/定义标记**——那些是聚合结论
 * （CoverageResult，判定归 evidence 汇总），值行只整理事实计数，不在
 * 导出面复制覆盖判定语义。值语义纯结构；线程安全。
 */
struct KinCoverageExportRow {
    /// 来源快照内容身份（同解行）。
    core::ContentIdentity snapshotId;
    /// 区域对象身份（对象引用——SamplingPlanRef.regionObjectId 同源）。
    core::ObjectId regionOid;
    /// 样本口径 token（"position"｜"pose"——SampleKind 两值的小写词形）。
    std::string kind;
    /// 分母＝该区域该口径计划样本总数（计数）。
    std::uint64_t planned = 0;
    /// Reached 样本数（分子态）。
    std::uint64_t reached = 0;
    /// Unreachable 样本数（解析界限确定性证明）。
    std::uint64_t unreachable = 0;
    /// DataInsufficient 样本数（搜索未果/缺检测器）。
    std::uint64_t dataInsufficient = 0;
    /// NotRun 样本数（取消未派发——partial）。
    std::uint64_t notRun = 0;
    /// NotApplicable 样本数（词表完备性保留）。
    std::uint64_t notApplicable = 0;

    bool operator==(const KinCoverageExportRow& o) const
    {
        return snapshotId == o.snapshotId && regionOid == o.regionOid
            && kind == o.kind && planned == o.planned && reached == o.reached
            && unreachable == o.unreachable
            && dataInsufficient == o.dataInsufficient && notRun == o.notRun
            && notApplicable == o.notApplicable;
    }
    bool operator!=(const KinCoverageExportRow& o) const { return !(*this == o); }
};

// =====================================================================
// 值行包与行构建（纯函数——结果值 → 行结构）
// =====================================================================

/**
 * @brief 导出值行包（三类行＋文件头——JSON/CSV 编码的唯一输入）。
 *
 * 值语义聚合；线程安全（并发只读）。行序纪律：解行保持生产端稳定序
 * （§6.3 四键）、诊断行保持 filteredRecords 原序（初值序）、覆盖行按
 * (regionOid 首现序, position→pose)——三者皆确定性（NFR-COR-02）。
 */
struct KinExportPackage {
    /// 导出文件头（来源声明＋快照/模式）。
    KinExportHeader header;
    /// 可行解行（保持稳定序）。
    std::vector<KinSolutionExportRow> solutions;
    /// 过滤诊断行（保持 filteredRecords 原序）。
    std::vector<KinFilteredSolutionExportRow> filteredSolutions;
    /// 区域覆盖行（确定性分组序）。
    std::vector<KinCoverageExportRow> coverage;
};

/**
 * @brief 从点解集构建解行＋诊断行（§6.1 两族记录的一一整理；不解构、
 *        不重排——rank/solutions 序即输入序）。
 *
 * @param set [in] 已求解解集（生产端不变式：solutions 已稳定排序；
 *                 本函数按值读取、零修改）
 * @return 解行（solutions.size() 条）——诊断行经
 *         buildFilteredSolutionExportRows 单独构建（两类行消费场景不同：
 *         候选表/诊断解释）
 *
 * 纯函数；线程安全；确定性（同输入同行集同序）。
 */
std::vector<KinSolutionExportRow> buildSolutionExportRows(const IkSolutionSet& set);

/**
 * @brief 从点解集构建过滤诊断行（§6.1 filteredRecords 逐条一行）。
 *
 * @param set [in] 已求解解集（同上——零修改）
 * @return 诊断行（filteredRecords.size() 条；保持原序；reason 取导出词
 *                 表 token——KinFilteredSolutionExportRow::reason 注）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<KinFilteredSolutionExportRow> buildFilteredSolutionExportRows(
    const IkSolutionSet& set);

/**
 * @brief 从区域覆盖计算构建覆盖行（逐 (regionOid, kind) 分组 tally）。
 *
 * 分组规则（确定性；登记随卡 §14.6 v0.9）：regionOid 按 samples 首现序；
 * 每区域固定 position 行在前、pose 行在后（只输出该区域计划中实际存在
 * 的口径——零样本口径不产行，分母信息归 CoverageResult 聚合面）。计数
 * 逐样本 tally（results 与 samples 按 sampleIndex 双射——生产端不变式，
 * 违例属调用方错误：logic_error fail-fast，与 computeCoverage 的守恒
 * 自检同轨）。
 *
 * @param computation [in] 区域覆盖计算结果（冻结值——零修改）
 * @return 覆盖行（确定性序——见分组规则）
 *
 * 纯函数；线程安全；确定性。
 * @throws std::logic_error 若 results 与 samples 的 sampleIndex 双射不成立
 *         （§7.2 完整性前提——导出面不做静默修补）
 */
std::vector<KinCoverageExportRow> buildCoverageExportRows(
    const RegionCoverageComputation& computation);

/**
 * @brief 组装值行包并做文件头纪律校验（runId 两分纪律——结构体注）。
 *
 * @param header       [in] 导出文件头（来源/身份/模式）
 * @param solutions    [in] 解行（可空——会话单点无归档解集等场景）
 * @param filtered     [in] 诊断行（可空）
 * @param coverage     [in] 覆盖行（可空）
 * @return 值行包（逐字段值搬移——零共享）
 *
 * @throws std::invalid_argument 若 header 违反来源两分纪律：
 *         ArchivedRun 而 runId 为空、或 SessionMemory 而 runId 非空
 *         （调用方错误 fail-fast——导出文件头的来源声明是 §7.4 契约，
 *         不允许矛盾形态静默落盘）。
 *
 * 纯函数；线程安全。
 */
KinExportPackage buildKinExportPackage(KinExportHeader header,
                                       std::vector<KinSolutionExportRow> solutions,
                                       std::vector<KinFilteredSolutionExportRow> filtered,
                                       std::vector<KinCoverageExportRow> coverage);

// =====================================================================
// 文本编码（行结构 → JSON/CSV 文本——写出本身归 io 通道，§7.4）
// =====================================================================

/**
 * @brief 编码为 JSON 文本（导出副本；单文档、字段定序、2 空格缩进）。
 *
 * 文档形状（字段定序＝编码序，消费方按名读取——增列走 formatVersion）：
 *   { "format":"ird-kin-export", "formatVersion":1, "source":…,
 *     "runId":…(仅归档), "snapshotId":"cid-…", "mode":…,
 *     "solutions":[…], "filteredSolutions":[…], "coverage":[…] }
 * 浮点经最短往返十进制表示；字符串经 JSON 转义（引号/反斜杠/控制字符
 * ——确定性逐字节输出）。
 *
 * @param package [in] 值行包（零修改）
 * @return JSON 文本（UTF-8；末尾带换行）
 *
 * 纯函数；线程安全；确定性（同包同字节——NFR-COR-01）。
 */
std::string encodeKinExportJson(const KinExportPackage& package);

/**
 * @brief 编码为 CSV 文本（导出副本；'#' 注释头声明来源——§7.4）。
 *
 * 文本形状：'#' 注释头（format/source/run-id/snapshot-id/mode 五行——
 * run-id 行仅归档来源携带）＋三段表（solutions/filtered-solutions/
 * coverage），每段以 "# section: <名>" 起始、首行为列头行；空段仍输出
 * 列头（形状稳定，消费方按列名解析）。CSV 转义按 RFC 4180（含逗号/
 * 引号/换行的字段加引号、内部引号加倍）；浮点同 JSON（最短往返）。
 *
 * @param package [in] 值行包（零修改）
 * @return CSV 文本（UTF-8；末尾带换行）
 *
 * 纯函数；线程安全；确定性（同包同字节——NFR-COR-01）。
 */
std::string encodeKinExportCsv(const KinExportPackage& package);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_EXPORT_HPP
