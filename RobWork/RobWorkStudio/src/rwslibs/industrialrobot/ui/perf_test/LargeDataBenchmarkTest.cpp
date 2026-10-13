/**
 * @file   LargeDataBenchmarkTest.cpp
 * @brief  大规模数据加载/保存/分页基准（performance-baseline §2 B-03／需求
 *         NFR-PERF-03）——5,000 任务点、100,000 采样结果、10,000 候选摘要
 *         三类数据在产品持久化面上的加载、保存与分页浏览测量；"不一次性
 *         把全部明细装入界面"以消费侧分页窗口投影的结构不变量承载
 *         （WP-23-T04）。
 *
 * 设计依据：
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-03 行（达标
 *     口径照抄需求原文——成功加载、保存和分页浏览 5,000 个任务点、
 *     100,000 个采样结果和 10,000 个候选摘要；不一次性把全部明细装入界
 *     面）、§2 统计口径区分行（B-03 为规模/功能类判定，不涉及分位数统计
 *     ——本基准计时仅作原始样本留痕，不作数值阈值判定）、§4.8（报告留
 *     痕——原始样本序列/环境/数据集登记）、§5.3（本卡挂点动作）；
 *   - 需求 NFR-PERF-03（REQUIREMENTS §19.3 原文——规模数值 5,000/
 *     100,000/10,000 照抄，零私改；需求无数值时延阈值，达标判定面＝
 *     三类数据操作全部成功＋"不整体装入界面"结构不变量）；
 *   - 任务契约 tasks/foundation/WP-23-T04.json（≙DTB §2.24 行——输出列
 *     "基准执行与分页优化"）；
 *   - units/ui.md §12.x"结果分批到达"行（消费侧分页（NFR-PERF-03）：明
 *     细面板分页加载，不一次性装全量）；units/io.md §9.3（ICsvReader 单
 *     遍流式——大文件不整体载入）；units/project.md §5.6（归档端口——
 *     results/ 写入口唯一归属）；units/kinematics.md §7.2/§14.6 v0.6
 *     （区域覆盖 canonical 载荷布局 IRDCV01——冻结契约）；units/
 *     optimization.md §11.4（候选 CSV——候选摘要的持久化字段契约）。
 *
 * 无头等价口径（验收记录同步说明，报告 notes 逐条登记）：
 *   本基准在模型/存储层驱动**产品代码本体**，三类数据的"分页浏览"以消费
 *   侧分页窗口投影承载（每页物化至多页大小条明细——Widget 重绘与面板组
 *   件在本口径外，WP-23-T02 先例同款边界申报）：
 *   ①任务点：保存＝RequirementCodec canonical 编码（req-point-set 对象字
 *     节——项目对象库持久化的编码面）＋io 原子文件写出；加载＝字节读回
 *     ＋RequirementCodec 解码＋全量往返等值核对；分页＝解码后条目集上的
 *     窗口投影（任务点表呈现行——name/启用/位姿文本的模型侧等价）。
 *   ②采样结果：保存＝区域覆盖 canonical 载荷编码（逐样本状态表 100,000
 *     条——IRDCV01 冻结布局）＋project::IResultArchivePort 归档（begin→
 *     writeBatch→finalize——results/<run-id>/ 落盘＋manifest 原子发布，
 *     产品运行结果唯一存储入口 O-12）；加载＝query().listRuns→runDir 产
 *     品编址读回＋字节完整性核对（size＋SHA-256）＋冻结布局独立最小解析
 *     （conformance 面——kinematics 公共面只有编码无解码，第二实现仅存
 *     在于本测试目标、不入产品库，如实申报）；分页＝逐样本状态表窗口投
 *     影。样本集与覆盖率计算（generateSampleSet/computeCoverage）为产品
 *     确定性生成器真跑（D-KIN-6），非预录数据。
 *   ③候选摘要：保存＝OptimizationExportProvider 六工件数据面组装（10,000
 *     候选——工件 2 候选 CSV 字段契约）＋io ICsvWriter canonical 写出
 *     （opt-candidates.csv——工件 6 候选包的产品写出适配器随 P-OPT-2 裁
 *     决落位，本基准保存面止于数据面组装＋CSV 写出，边界如实申报）；
 *     加载＝io ICsvReader 单遍流式回读（行回调——峰值内存＝当前行，大
 *     文件不整体载入）；分页＝运行结果聚合 candidates 上的窗口投影（候
 *     选摘要行——身份/状态/指标的模型侧等价）。
 *   上述三点是本基准的登记口径（等价性边界如实申报，不冒充真实界面测量
 *   ——performance-baseline §4.8"未执行/差距如实登记"纪律）。
 *
 * 判定口径（零私设数值——B-03 无数值阈值）：
 *   达标判定为结构与成功性判定：①三类数据的保存/加载/分页操作全部成功
 *   （任何产品调用失败即无效样本或用例失败，不静默）；②规模恰为需求原
 *   文数值（5,000/100,000/10,000——往返计数与等值核对钉住）；③"不整体
 *   装入界面"结构不变量：单次投影物化行数 ≤ 页大小（100——manifest 冻
 *   结），全量翻页后累计行数恰为 N（翻页覆盖全部明细）；④零无效样本
 *   （操作闭包异常即夹具断——无效留痕不判过）。计时样本（预热剔除＋
 *   ≥15 有效）全量落报告留痕，不参与达标判定。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>          // ContentDigester/Digest256/ContentIdentity
#include <sdurws/ird/core/Evaluation.hpp>      // EvaluationMode（运行模式词表）
#include <sdurws/ird/core/Identity.hpp>        // ObjectId/TaskIdentity/RunId 等身份族
#include <sdurws/ird/core/Provenance.hpp>      // ValueProvenance/ProvenanceKind
#include <sdurws/ird/io/AtomicFile.hpp>        // makeAtomicFileWriter（原子文件写出）
#include <sdurws/ird/io/Csv.hpp>               // ICsvReader/ICsvWriter（流式 CSV 设施）
#include <sdurws/ird/kinematics/Coverage.hpp>  // encodeRegionCoveragePayloadCanonical
#include <sdurws/ird/kinematics/Sampling.hpp>  // generateSampleSet/computeCoverage/sampleSetIdentity
#include <sdurws/ird/optimization/CandidatePatch.hpp>  // CandidatePatch/candidateIdOf
#include <sdurws/ird/optimization/Constraint.hpp>      // ConstraintFact（候选约束事实）
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>  // TwoStageRunRecord/TwoStageRunResult
#include <sdurws/ird/optimization/Export.hpp>  // ExportProvider/writeExportBundle 面
#include <sdurws/ird/optimization/Objective.hpp>       // defaultObjectives（StageB 默认目标集）
#include <sdurws/ird/optimization/Run.hpp>     // OptimizationRunResult/assembleRunResult
#include <sdurws/ird/optimization/Types.hpp>   // RunPhase/候选状态词表
#include <sdurws/ird/project/ArchivePort.hpp>  // IResultArchivePort（归档端口）
#include <sdurws/ird/project/PersistenceFormat.hpp>    // RunManifest（运行完整性清单）
#include <sdurws/ird/project/ProjectStore.hpp> // ProjectStoreFactory（存储上下文）
#include <sdurws/ird/project/QueryPort.hpp>    // IProjectQueryPort（listRuns/runDir）
#include <sdurws/ird/requirements/Codec.hpp>   // RequirementCodec（canonical 编解码）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // TaskPoint/PointSet/sortEntriesByObjectId
#include <sdurws/ird/testkit/Fixture.hpp>      // TempDir（并行隔离临时目录）
#include <sdurws/ird/testkit/PerfBenchmark.hpp>        // runInteractionGroup/报告拖挂
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>   // IRD_TEST_INFO（追溯登记）

#include <rw/math/Vector3D.hpp>                // rw::math::Vector3D（采样几何）

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace sdurws::ird;
using testkit::InteractionGroupReport;
using testkit::InteractionGroupSpec;
using testkit::PerfSessionReport;

// =====================================================================
// 数据集冻结参数（datasetId＝ird-perf-b03-largedata，version 1——
// manifest 权威文件 testdata/performance/b03-largedata-manifest.json；
// 本处常量必须与 manifest 逐字一致，报告输出参数供漂移核对）
// =====================================================================

/// 任务点规模（NFR-PERF-03 原文：5,000 个任务点——零增减）。
constexpr std::size_t kTaskPointCount = 5000;
/// 采样结果规模（NFR-PERF-03 原文：100,000 个采样结果——零增减）。
constexpr std::size_t kSampleResultCount = 100000;
/// 候选摘要规模（NFR-PERF-03 原文：10,000 个候选摘要——零增减）。
constexpr std::size_t kCandidateCount = 10000;
/// 分页页大小（manifest 冻结的生成器参数：单页物化明细行数——"不整体
/// 装入界面"结构不变量的窗口上界；工业明细表单页行数的常规形态）。
constexpr std::size_t kPageSize = 100;
/// Quick 筛选批候选数（两级编排产出形态——Quick 在前 Verified 在后；
/// 9,000＋1,000＝10,000 恰为需求规模）。
constexpr std::size_t kQuickCandidateCount = 9000;
/// 采样计划位置 Grid 逐轴计数（100×100×1＝10,000 位置样本——分母一）。
constexpr std::array<std::uint32_t, 3> kGridCounts{100U, 100U, 1U};
/// 采样计划姿态定义（方向数 9×roll 数 1——位姿样本 10,000×9＝90,000——
/// 分母二；两分母合计 100,000＝kSampleResultCount，恰为需求规模）。
constexpr std::uint32_t kDirectionSamples = 9U;
constexpr std::uint32_t kRollSamples = 1U;

// =====================================================================
// 消费侧分页窗口投影（"不一次性把全部明细装入界面"的结构承载半区）
// =====================================================================

/**
 * @brief 一次全量翻页的观测账本（结构不变量的计量面——B-03 判定据）。
 *
 * rowsSeen 与 maxWindow 是"不一次性把全部明细装入界面"的两个判定量：
 * 全量翻页后 rowsSeen 必须恰为明细总数（翻页覆盖全部明细——漏页即断）；
 * maxWindow 必须恒 ≤ 页大小（任一时刻投影面同时物化的明细行数有界——
 * 全量物化一次成型即违约）。纯计数结构；线程约束：单线程测试内使用。
 */
struct PaginationObservation {
    std::size_t pages = 0;      ///< 翻页次数（⌈N/页大小⌉——全量翻页后核对）
    std::size_t rowsSeen = 0;   ///< 累计进入投影的明细行数（全翻页后＝N）
    std::size_t maxWindow = 0;  ///< 单次投影同时物化行数的最大值（≤页大小）
};

/**
 * @brief 执行一次全量翻页（页窗口逐页投影——消费侧分页的执行器半区）。
 *
 * 执行序：从第 0 页起逐页调用 page 闭包（参数＝该页起始行号与行数，末页
 * 可短）；每页闭包返回其实际物化的投影行数——观测账本逐页累计（maxWindow
 * 取逐页最大）。页窗口投影由调用方闭包承载（每类数据一种投影行形态），
 * 本执行器只承载"逐页推进＋有界性观测"——投影面物化多少行完全由闭包
 * 声明，不隐藏任何全量物化路径（账本如实记账，防基准自欺）。
 *
 * @tparam PageFn 可调用对象：std::size_t(begin, count) → std::size_t
 *                （该页物化的投影行数——须 ≤count，违约抛 std::logic_error）
 * @param totalRows [in] 明细总行数（N）
 * @param pageSize  [in] 页大小（窗口上界——>0，0 规格抛调用方错误）
 * @param page      [in] 单页投影闭包
 * @return 翻页观测账本（pages/rowsSeen/maxWindow）
 *
 * @throws std::invalid_argument pageSize==0（无分页语义的规格——fail-fast）
 * @throws std::logic_error 某页闭包声明物化行数 >count（投影闭包违约）
 */
template <class PageFn>
PaginationObservation pageThrough(std::size_t totalRows, std::size_t pageSize,
                                  PageFn&& page)
{
    if (pageSize == 0) {
        throw std::invalid_argument("pageThrough: 页大小为 0（无分页语义）");
    }
    PaginationObservation obs;
    for (std::size_t begin = 0; begin < totalRows; begin += pageSize) {
        const std::size_t count = std::min(pageSize, totalRows - begin);
        const std::size_t materialized = page(begin, count);
        if (materialized > count) {
            throw std::logic_error(
                "pageThrough: 页投影物化行数超过该页行数（投影闭包违约——"
                "第 " + std::to_string(begin) + " 行起 " + std::to_string(count)
                + " 行页声明物化 " + std::to_string(materialized) + " 行）");
        }
        ++obs.pages;
        obs.rowsSeen += materialized;
        obs.maxWindow = std::max(obs.maxWindow, materialized);
    }
    return obs;
}

// =====================================================================
// 公共小件（环境读取/摘要十六进制/会话报告头）
// =====================================================================

/// 环境事实读取（§4.1 登记清单的环境变量可得面——缺失如实留空）。
std::string envOr(const char* name, const char* fallback = "")
{
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

/// 固定内容身份工厂（基准夹具的确定性字面量身份——"cid-"＋60 个 '0'＋4 位
/// 尾字，恰为 64 位小写 hex；程序化拼接消除手写字面量的位数漂移风险）。
core::ContentIdentity fixedContentIdentity(const char* tail4)
{
    return core::ContentIdentity::fromCanonical(
        "cid-" + std::string(60, '0') + tail4);
}

/// 固定 Id128 身份工厂（同上口径——<tag>＋28 个 '0'＋4 位尾字＝32 位 hex，
/// prj-/brn-/rev-/obj- 四段前缀共用；程序化拼接消除位数漂移风险）。
std::string fixedId128Text(const char* tag, const char* tail4)
{
    return std::string(tag) + std::string(28, '0') + tail4;
}

/**
 * @brief 计算 SHA-256 并给小写十六进制文本（manifest 条目 sha256 字段的
 *        填充值——摘要计算经 core::ContentDigester 产品实现，本函数只做
 *        字节→hex 的文本编码，零第二哈希路径）。
 */
std::string sha256Hex(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    const core::Digest256 digest = digester.finalize();
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (std::uint8_t b : digest) {
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 0x0F]);
    }
    return hex;
}

/// 组装会话报告头（标识/版本/数据集/环境——测量组由调用方追加；B-03 为
/// 规模/功能类判定，verdicts 恒空集——零私设数值阈值，判定面在用例断言）。
PerfSessionReport makeSessionHeader(const std::string& caseId)
{
    PerfSessionReport r;
    r.caseId = caseId;
    r.baselineDoc = "performance-baseline #2 B-03";
    // 软件版本锁定（§4.1）：commit 由执行方经 IRD_PERF_GIT_COMMIT 注入
    // （缺省如实留空——commit SHA 在首测报告文档登记）。
    r.gitCommit = envOr("IRD_PERF_GIT_COMMIT");
    r.buildConfig = "Release";
    r.datasetId = "ird-perf-b03-largedata";
    r.datasetVersion = "1";
    r.machineName = envOr("COMPUTERNAME");
    r.cpuInfo = envOr("PROCESSOR_IDENTIFIER")
                + " x" + envOr("NUMBER_OF_PROCESSORS", "?");
    r.osInfo = envOr("OS", "Windows") + " " + envOr("PROCESSOR_ARCHITECTURE");
    // 内存/存储/电源：环境变量不可得——如实指向首测报告的硬件清单
    // （traceability/benchmarks/wp23-t04-largedata-baseline.md §2）。
    r.memoryInfo = "见首测报告硬件清单（manifest 同源）";
    r.storageInfo = "见首测报告硬件清单";
    r.powerPolicyNote = "见首测报告硬件清单";
    r.backgroundNote = "空闲态＝无后台评估（B-03 为规模/功能类基准，无两态"
                       "负载面——§2 统计口径区分行）";
    // 等价性口径与判定口径登记（报告自含——验收对照面）。
    r.notes.push_back(
        "无头模型/存储层等价口径：分页＝消费侧窗口投影（每页 ≤100 行——"
        "manifest 冻结），Widget 重绘与面板组件在口径外；边界如实申报见"
        "用例文件头与首测报告 §4");
    r.notes.push_back(
        "判定口径＝规模/功能类（B-03 需求无数值阈值）：三类数据操作全部"
        "成功＋规模恰为 5,000/100,000/10,000＋单页物化 ≤ 页大小＋全量翻"
        "页累计＝N＋零无效样本；计时仅作原始样本留痕不作判定");
    r.notes.push_back(
        "数据集 manifest＝testdata/performance/b03-largedata-manifest.json"
        "（datasetId ird-perf-b03-largedata@1——参数冻结，升级换版本）");
    return r;
}

/// 分组采样规格工厂（每组 1 次预热＋≥15 有效样本——协议 §4.2 采样纪律
/// 的 B-03 承载：样本量对齐 P95 类基准的统计留痕面，判定面不消费 P95）。
InteractionGroupSpec makeGroupSpec(const char* opId)
{
    InteractionGroupSpec spec;
    spec.opId = opId;
    spec.stateId = "idle";
    spec.warmupRuns = 1;
    spec.minValidSamples = 15;
    return spec;
}

// =====================================================================
// 腿 A——任务点（5,000）：requirements 值模型＋canonical 编解码＋分页
// =====================================================================

/// 确定性任务点构造（内容随序号派生——5000 条互异名称/位姿/工艺标签；
/// 全部物理量 SI：位置 m、距离 m。合法形态与 requirements 测试夹具
/// makeHealthyPoint 同构——过 Codec 编解码校验链的健康点）。
requirements::TaskPoint makeGoldenTaskPoint(std::size_t index)
{
    requirements::TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = "pt-" + std::to_string(index);              // 集合内唯一语义名
    p.processTag = static_cast<requirements::ProcessTag>(  // 工艺标签轮转（11 值词表）
        index % 11);
    p.level = requirements::RequirementLevel::Must;
    p.enabled = (index % 97 != 0);                       // 少量停用点（表内真实形态）
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.1 + 0.001 * static_cast<double>(index % 500),
                                   -0.4 + 0.0008 * static_cast<double>(index % 625),
                                   0.05 + 0.0005 * static_cast<double>(index % 800)),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                    std::nullopt, std::nullopt,
                                    std::string("wp23-t04-fixture")));
    p.pose.constrainedDof.z = true;                      // 受约束 Z（自由度掩码）
    p.tolerance.positionTolerance = 0.001;               // 位置容差（m——I-REQ-5 >0）
    p.work.enabled = true;
    p.work.distanceM = 0.05 + 0.0001 * static_cast<double>(index % 100);  // 作业段（m）
    return p;
}

/// 黄金任务点集（5,000 条——条目按 ObjectId 规范文本字典序存放
/// 〔I-REQ-1 canonical 确定性〕，构造边界 sortEntriesByObjectId 规范化）。
requirements::PointSet makeGoldenPointSet()
{
    requirements::PointSet ps;
    ps.entries.reserve(kTaskPointCount);
    for (std::size_t i = 0; i < kTaskPointCount; ++i) {
        ps.entries.push_back(makeGoldenTaskPoint(i));
    }
    requirements::sortEntriesByObjectId(ps.entries);
    return ps;
}

/**
 * @brief 任务点集 UI 行投影（分页窗口的单页物化——任务点表呈现行的模型
 *        侧等价：名称/启用/位置三分量文本合成）。
 *
 * 等价说明（报告 §4 登记）：真实面板的表格行由 Widget 列模型消费同源字
 * 段；本投影在模型层合成等价行文本（字符串构造即呈现数据的物化代价），
 * Widget 重绘在本口径外。物化行数即闭包返回值——不超窗口上界。
 */
std::size_t projectTaskPointPage(const requirements::PointSet& set,
                                 std::size_t begin, std::size_t count)
{
    for (std::size_t k = 0; k < count; ++k) {
        const requirements::TaskPoint& p = set.entries[begin + k];
        // 行文本合成（呈现数据物化——名称＋启用＋位置 SI 文本）。
        std::string row = p.name;
        row += p.enabled ? "|启用" : "|停用";
        row += "|" + std::to_string(p.pose.position.value()[0])
             + "," + std::to_string(p.pose.position.value()[1])
             + "," + std::to_string(p.pose.position.value()[2]);
        if (row.empty()) {  // 恒假分支——防编译器把投影折叠为空操作
            throw std::logic_error("taskpoint-projection: 空行（夹具漂移）");
        }
    }
    return count;
}

// =====================================================================
// 腿 B——采样结果（100,000）：产品确定性生成器＋归档端口＋冻结布局解析
// =====================================================================

/// 黄金采样面夹具（一次生成、全用例共享——产品生成器真跑，D-KIN-6）。
struct SamplingFixture {
    std::vector<kinematics::SamplingPlan> plans;   ///< 采样计划投影（单计划）
    kinematics::RegionSamplingBudget budget{};     ///< 预算/种子（身份面）
    kinematics::SampleSet samples{};               ///< 确定性样本集（100,000 条）
    kinematics::SampleResultSet results{};         ///< 逐样本结果（双射对齐）
    kinematics::CoverageResult coverage{};         ///< 覆盖率计数（computeCoverage 产出）
    kinematics::RegionCoverageComputation computation{};  ///< 载荷编码输入全量

    SamplingFixture()
    {
        // ---- 计划投影（基座系 {B}，单位 m；Grid 100×100×1＋姿态 9×1——
        // 分母 10,000＋90,000＝100,000，恰为需求规模）----
        kinematics::SamplingPlan plan;
        plan.regionObjectId = core::ObjectId::generate();
        plan.planContentIdentity = fixedContentIdentity("b03e");
        plan.box.center = rw::math::Vector3D<double>(0.4, 0.0, 0.3);
        plan.box.size = rw::math::Vector3D<double>(0.8, 0.8, 0.05);
        plan.position.method =
            kinematics::PositionSamplingDefinition::Method::Grid;
        plan.position.gridCounts = kGridCounts;
        plan.orientation.directionSamples = kDirectionSamples;
        plan.orientation.rollSamples = kRollSamples;
        plan.orientation.methodToken = "grid";
        plans.push_back(std::move(plan));
        budget.seed = 20260913;   // 采样种子（身份决定输入——I-KIN-4 非零）
        budget.threadCount = 1;

        // ---- 产品生成器真跑（样本集＋分母两值——D-KIN-6 确定性）----
        samples = kinematics::generateSampleSet(plans, budget);
        if (samples.samples.size() != kSampleResultCount) {
            throw std::logic_error(
                "sampler-fixture: 样本集规模偏离需求口径（实测 "
                + std::to_string(samples.samples.size()) + "，须 "
                + std::to_string(kSampleResultCount) + "）");
        }

        // ---- 逐样本结果（确定性状态模式：Unreachable 17 | DataInsufficient
        // 19 | 其余 Reached——NotApplicable/NotRun 携必填原因的词表纪律；
        // 五值词表三类覆盖即逐样本状态表的真实形态）----
        results.results.reserve(kSampleResultCount);
        for (const kinematics::SampleRecord& s : samples.samples) {
            kinematics::SampleResultRecord r;
            r.sampleIndex = s.sampleIndex;
            if (s.sampleIndex % 17 == 0) {
                r.state = kinematics::SampleState::Unreachable;
                r.reason = "解析界限：目标距离超出工作半径上界（基准生成器）";
            } else if (s.sampleIndex % 19 == 0) {
                r.state = kinematics::SampleState::DataInsufficient;
                r.reason = "碰撞要求在场而缺检测器（KIN-05——基准生成器）";
            } else {
                r.state = kinematics::SampleState::Reached;
            }
            results.results.push_back(std::move(r));
        }

        // ---- 覆盖率计算（产品唯一实现点——双射核查＋守恒式自检在内）----
        coverage = kinematics::computeCoverage(samples, results);

        // ---- 载荷编码输入组装（绑定六要素＋逐计划对账——对账身份经
        // sampleSetIdentity 产品函数重算，matched=true 冻结凭据）----
        computation.snapshotId = fixedContentIdentity("b031");
        computation.sliceId = fixedContentIdentity("b032");
        computation.configDigest = fixedContentIdentity("b033");
        computation.mode = core::EvaluationMode::Verified;
        computation.seed = budget.seed;
        computation.referenceQ = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6};  // 参考构型（rad）
        kinematics::PlanIdentityCheck check;
        check.regionObjectId = plans.front().regionObjectId;
        check.planContentIdentity = plans.front().planContentIdentity;
        check.computedIdentity =
            kinematics::sampleSetIdentity(plans.front().planContentIdentity,
                                          budget);
        check.matched = true;
        check.plannedPositionSamples = samples.plannedPositionSamples;
        check.plannedPoseSamples = samples.plannedPoseSamples;
        computation.identityChecks.push_back(std::move(check));
        computation.samples = samples;
        computation.results = results;
        computation.coverage = coverage;
    }
};

/// 任务五元组构造（存储绑定面——project 取存储上下文身份，run 每归档一新）。
core::TaskIdentity makeArchiveTask(const core::ProjectId& project,
                                   const core::RevisionId& revision)
{
    core::TaskIdentity task;
    task.project = project;
    task.branch = core::BranchId::generate();
    task.revision = revision;
    task.run = core::RunId::generate();
    task.attempt = core::AttemptId{1};
    return task;
}

/**
 * @brief 一次完整归档保存（区域覆盖 canonical 载荷→results/<run-id>/——
 *        产品运行结果唯一存储入口 O-12：begin→writeBatch→finalize，
 *        manifest 原子发布即"该运行完整"）。
 *
 * @param store        [in] 存储上下文（可写——createNew 交付）
 * @param fixture      [in] 采样夹具（载荷来源）
 * @param revision     [in] 被评估修订（归档绑定原修订——A8）
 * @param outPayload   [out] 本归档的载荷字节（调用方核对用；可空）
 * @param outRunId     [out] 本归档运行身份（查询定位用；可空）
 *
 * @throws 任一产品调用失败（begin/writeBatch/finalize 的 ok==false 或异常）
 *         直接向上传播——保存不成功即无效样本/用例失败，不静默吞错。
 */
void archiveCoveragePayloadOnce(project::ProjectStore& store,
                                const SamplingFixture& fixture,
                                const core::RevisionId& revision,
                                std::vector<std::uint8_t>* outPayload,
                                core::RunId* outRunId)
{
    // ---- 编码（IRDCV01 canonical——逐样本状态表 100,000 条）----
    const core::TaskIdentity task =
        makeArchiveTask(store.projectId(), revision);
    const std::vector<std::uint8_t> payload =
        kinematics::encodeRegionCoveragePayloadCanonical(fixture.computation,
                                                         task);

    // ---- 归档会话（runDir＝<项目根>/results/<run-id 规范文本>——白名单
    // 编址从 canonicalPath 推导，与实现归一形态一致）----
    const core::RunId runId = task.run;
    project::ArchiveRequest request;
    request.task = task;
    request.runDir = store.canonicalPath() / "results" / runId.toCanonical();
    request.runKind = "region-coverage";
    request.evaluationKey = kinematics::kRegionCoverageEvaluationKey;
    const project::ArchiveSessionRef session = store.archive().begin(request);

    // ---- 批次写入（单工件——完整载荷一份；字节原样存储不解释）----
    project::ArchiveBatch batch;
    project::ArchiveItem item;
    item.relPath = "coverage-payload.bin";
    item.bytes = payload;
    batch.items.push_back(std::move(item));
    const project::ArchiveStatus written =
        store.archive().writeBatch(session, batch);
    if (!written.ok) {
        throw std::runtime_error(std::string("archive writeBatch 失败（")
                                 + written.error->what() + "）");
    }

    // ---- finalize（manifest 原子发布——条目 sha256/size 经产品侧对盘核验，
    // 不符即拒绝发布；通过即该运行完整可消费）----
    project::RunManifest manifest;
    manifest.taskIdentity = task;
    project::RunManifestItem manifestItem;
    manifestItem.relPath = "coverage-payload.bin";
    manifestItem.sha256 = sha256Hex(payload);
    manifestItem.sizeBytes = payload.size();
    manifest.items.push_back(std::move(manifestItem));
    manifest.runKind = request.runKind;
    manifest.evaluationKey = request.evaluationKey;
    manifest.finalizedAtUtc = "2026-10-13T00:00:00Z";
    const project::ArchiveStatus finalized =
        store.archive().finalize(session, manifest);
    if (!finalized.ok) {
        throw std::runtime_error(std::string("archive finalize 失败（")
                                 + finalized.error->what() + "）");
    }

    if (outPayload != nullptr) { *outPayload = payload; }
    if (outRunId != nullptr) { *outRunId = runId; }
}

// ---- IRDCV01 冻结布局的独立最小解析（conformance 面——见文件头申报②）----

/// 解析产物行（逐样本状态表的紧凑投影——分页窗口的数据源）。
struct SampleStateRow {
    std::uint64_t sampleIndex = 0;   ///< 对齐键（＝SampleRecord::sampleIndex）
    std::uint8_t state = 0;          ///< 五值词表值（SampleState 枚举原始值）
};

/// 布局读取游标（小端定宽读取——越界即抛，防静默截断）。
class PayloadCursor {
public:
    explicit PayloadCursor(const std::vector<std::uint8_t>& bytes)
        : m_bytes(bytes)
    {
    }

    void skip(std::size_t n)
    {
        if (m_pos + n > m_bytes.size()) {
            throw std::runtime_error("payload-cursor: 越界跳跃（布局解析失败）");
        }
        m_pos += n;
    }

    std::uint8_t u8()
    {
        if (m_pos + 1 > m_bytes.size()) {
            throw std::runtime_error("payload-cursor: u8 越界");
        }
        return m_bytes[m_pos++];
    }

    std::uint32_t u32()
    {
        if (m_pos + 4 > m_bytes.size()) {
            throw std::runtime_error("payload-cursor: u32 越界");
        }
        const std::uint32_t v = static_cast<std::uint32_t>(m_bytes[m_pos])
            | (static_cast<std::uint32_t>(m_bytes[m_pos + 1]) << 8)
            | (static_cast<std::uint32_t>(m_bytes[m_pos + 2]) << 16)
            | (static_cast<std::uint32_t>(m_bytes[m_pos + 3]) << 24);
        m_pos += 4;
        return v;
    }

    std::uint64_t u64()
    {
        std::uint64_t v = 0;
        for (int i = 7; i >= 0; --i) {
            v = (v << 8) | m_bytes.at(m_pos + static_cast<std::size_t>(i));
        }
        m_pos += 8;
        return v;
    }

    std::string str()
    {
        const std::uint32_t len = u32();
        if (m_pos + len > m_bytes.size()) {
            throw std::runtime_error("payload-cursor: 字符串越界");
        }
        std::string s(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_pos),
                      m_bytes.begin()
                          + static_cast<std::ptrdiff_t>(m_pos + len));
        m_pos += len;
        return s;
    }

    std::size_t pos() const noexcept { return m_pos; }

private:
    const std::vector<std::uint8_t>& m_bytes;  ///< 载荷字节（借用——调用方保活）
    std::size_t m_pos = 0;                     ///< 读游标（字节位）
};

/**
 * @brief 解析区域覆盖 canonical 载荷的逐样本状态表（冻结布局 IRDCV01——
 *        kinematics.md §14.6 v0.6 登记；本解析是测试侧 conformance 读面，
 *        与 encodeRegionCoveragePayloadCanonical 的字节布局逐段对齐）。
 *
 * 布局序（照编码实现）：magic 7B "IRDCV01"＋u32 版本＋四标记 u8×4＋绑定
 * 块（3×32B 身份＋mode u8＋seed u64＋f64 向量＋任务五元组＋评估键串＋契
 * 约版本 u32）＋计划块（u32 计数×逐计划 97B）＋覆盖计数块（2×6×u64）＋
 * 状态表（u32 计数×逐条 u64 索引＋16B 归属＋kind/state u8＋outcome 
 * presence u8［+u8］＋collision u8＋reason 串）。
 *
 * @param payload [in] 载荷字节（归档批次文件读回）
 * @param expectedCount [in] 期望状态表条数（100,000——计数核对钉）
 * @param outRows [out] 解析出的逐样本状态行（sampleIndex 升序——表序）
 * @return 逐样本状态表起点字节位（布局对账留痕——报告登记）
 *
 * @throws std::runtime_error 魔数/版本/计数不符或任何越界（加载失败即
 *         断，绝不静默截断——协议"加载成功"的判定前提）
 */
std::size_t parseCoveragePayloadStateTable(
    const std::vector<std::uint8_t>& payload, std::size_t expectedCount,
    std::vector<SampleStateRow>* outRows)
{
    PayloadCursor c(payload);
    // 魔数＋版本（IRDCV01——7B ASCII＋u32 v1）。
    static const char kMagic[] = "IRDCV01";
    for (std::size_t i = 0; i < 7; ++i) {
        if (c.u8() != static_cast<std::uint8_t>(kMagic[i])) {
            throw std::runtime_error("payload: 魔数不符（非区域覆盖 canonical）");
        }
    }
    if (c.u32() != 1U) {
        throw std::runtime_error("payload: codec 版本非 1（异版载荷拒绝）");
    }
    // 标记块（四标记）＋绑定块（3×32B 身份＋mode＋seed＋参考构型向量＋
    // 任务五元组＋评估键＋契约版本）——本解析只消费状态表，跳过语义面。
    c.skip(4);                       // incomplete/downgraded/两 defined 标记
    c.skip(32 * 3);                  // snapshotId/sliceId/configDigest
    c.skip(1);                       // mode
    c.skip(8);                       // seed
    const std::uint32_t refQCount = c.u32();
    c.skip(static_cast<std::size_t>(refQCount) * 8);  // f64×n 参考构型
    c.skip(16 * 4 + 8);              // 任务五元组（4×Id128＋attempt）
    c.str();                         // 评估键串
    c.u32();                         // 契约版本
    // 计划块（逐计划 16＋32＋32＋1＋8＋8＝97B）。
    const std::uint32_t planCount = c.u32();
    c.skip(static_cast<std::size_t>(planCount) * 97);
    // 覆盖计数块（双口径各六计数——2×6×u64）。
    c.skip(2 * 6 * 8);
    // 逐样本状态表（本解析的目的面）。
    const std::uint32_t rowCount = c.u32();
    if (rowCount != expectedCount) {
        throw std::runtime_error("payload: 状态表计数不符（实测 "
                                 + std::to_string(rowCount) + "，期望 "
                                 + std::to_string(expectedCount) + "）");
    }
    outRows->reserve(rowCount);
    for (std::uint32_t k = 0; k < rowCount; ++k) {
        SampleStateRow row;
        row.sampleIndex = c.u64();
        c.skip(16);                  // 归属区域 ObjectId
        c.u8();                      // kind
        row.state = c.u8();
        if (c.u8() != 0U) {          // outcomeKind presence
            c.u8();                  // outcome 值（presence=1 时跟随）
        }
        c.u8();                      // collisionNotEvaluated
        c.str();                     // reason
        outRows->push_back(row);
    }
    return c.pos();
}

// =====================================================================
// 腿 C——候选摘要（10,000）：运行结果聚合＋六工件数据面＋流式 CSV
// =====================================================================

/// 黄金候选面夹具（10,000 候选的运行结果聚合——两级编排产出形态：Quick
/// 9,000 筛选批＋Verified 1,000 正式批，Quick 在前 Verified 在后保持
/// 编排序；补丁逐候选取异值——候选身份互异，摘要行真实可辨）。
struct CandidateFixture {
    optimization::OptimizationRunSpec spec{};      ///< 研究定义（导出绑定面）
    optimization::OptimizationRunResult run{};     ///< 运行结果聚合（归档态）

    CandidateFixture()
    {
        // ---- 身份面（固定字面量——确定性；ExportTest 同源口径）----
        const core::ProjectId project = core::ProjectId::fromCanonical(
            fixedId128Text("prj-", "b003"));
        const core::BranchId branch = core::BranchId::fromCanonical(
            fixedId128Text("brn-", "b003"));
        const core::RevisionId revision = core::RevisionId::fromCanonical(
            fixedId128Text("rev-", "b003"));
        const core::ContentIdentity snapshot = fixedContentIdentity("c0d3");
        const core::ObjectId baselineRoot = core::ObjectId::fromCanonical(
            fixedId128Text("obj-", "b003"));
        const core::ContentVersion baselineCv = core::ContentVersion::fromCanonical(
            "cv-" + std::string(60, '0') + "b03c");

        // ---- 已校验配置（StageB 默认目标集——validateConfiguration 全过）----
        optimization::OptimizationConfiguration config;
        config.objectives =
            optimization::defaultObjectives(optimization::OptimizationStage::StageB);
        optimization::validateConfiguration(config);

        // ---- 研究定义（"opt" Profile 三元组——validateRunSpec 全过）----
        spec.project = project;
        spec.branch = branch;
        spec.revision = revision;
        spec.snapshotId = snapshot;
        spec.config = config;
        spec.profile.profileId = "opt";
        spec.profile.version = "1.0";
        spec.profile.contentIdentity = fixedContentIdentity("c0d4");
        spec.createdBy = "wp23-t04-fixture";
        spec.createdAtUtc = std::chrono::system_clock::time_point{
            std::chrono::milliseconds{1728777600000LL}};  // 固定纪元（确定性）
        optimization::validateRunSpec(spec);

        // ---- 两级编排产出（10,000 候选：Quick 9,000〔screeningOnly〕＋
        // Verified 1,000；补丁标量逐候选取异——身份互异）----
        optimization::TwoStageRunResult orchestrated;
        orchestrated.runPhase = optimization::RunPhase::Completed;
        orchestrated.runCompleted = true;
        orchestrated.quickRecords.reserve(kQuickCandidateCount);
        orchestrated.verifiedRecords.reserve(kCandidateCount
                                             - kQuickCandidateCount);
        for (std::size_t i = 0; i < kCandidateCount; ++i) {
            const bool quick = i < kQuickCandidateCount;
            // 补丁项（单绑定标量——值随序号取异，patchIdentity/candidateId
            // 随之互异；Scalar 通道由 canonicalize 按成员形态判定）。
            optimization::CandidatePatch patch;
            optimization::PatchItem item;
            item.bindingId = "mdl.probe.length";
            item.scalarValue = 100.0 + static_cast<double>(i) * 0.5;
            patch.items.push_back(item);
            patch.label = "cand-" + std::to_string(i);

            optimization::TwoStageRunRecord r;
            r.candidateId = optimization::candidateIdOf(baselineRoot,
                                                        baselineCv, patch);
            r.sliceId = fixedContentIdentity("c0d5");
            r.isBaseline = (i == 0);
            r.patch = std::move(patch);
            r.mode = quick ? core::EvaluationMode::Quick
                           : core::EvaluationMode::Verified;
            r.screeningOnly = quick;               // Quick 批恒 true（类型学屏障）
            r.status = optimization::CandidateStatus::Feasible;
            r.engineeringStatus = core::EngineeringStatus::Feasible;
            r.formalPassEligible = !quick;         // Quick 恒 false（模式资格）
            // 八项指标（三实值＋五 StageB"—"——导出候选 CSV 的指标列源）。
            r.metrics.metrics.resize(optimization::kMetricCount);
            r.metrics.metrics[static_cast<std::size_t>(optimization::MetricId::Envelope)]
                = {optimization::MetricId::Envelope,
                   2.5 + 0.001 * static_cast<double>(i),
                   optimization::kMetricGapStage, "wp23-t04"};
            r.metrics.metrics[static_cast<std::size_t>(
                optimization::MetricId::StructuralMass)]
                = {optimization::MetricId::StructuralMass, 10.0 + 0.0001 * static_cast<double>(i),
                   optimization::kMetricGapStage, "wp23-t04"};
            r.metrics.metrics[static_cast<std::size_t>(
                optimization::MetricId::MinJointMargin)]
                = {optimization::MetricId::MinJointMargin, 0.4,
                   optimization::kMetricGapStage, "wp23-t04"};
            for (int m = 3; m < 8; ++m) {
                r.metrics.metrics[static_cast<std::size_t>(m)]
                    = {static_cast<optimization::MetricId>(m),
                       std::optional<double>{},
                       optimization::kMetricGapStage, "StageB 恒—"};
            }
            // 约束事实（正式面证据链——每候选一条 satisfied 事实）。
            optimization::ConstraintFact fact;
            fact.constraintId = optimization::ConstraintId::JointLimit;
            fact.verdict = optimization::ConstraintVerdict::Satisfied;
            fact.evaluationKey = std::string(optimization::kOptStaticScreenKey);
            fact.subject = "obj-probe-joint";
            fact.actualText = "0.31";
            fact.requiredText = ">= 0.05";
            fact.unitSymbol = "1";
            fact.detail = "关节限位执行完成无 Must 违例（基准生成器）";
            r.evaluation.candidateId = r.candidateId;
            r.evaluation.constraintFacts = {std::move(fact)};

            if (quick) {
                orchestrated.quickRecords.push_back(std::move(r));
            } else {
                orchestrated.verifiedRecords.push_back(std::move(r));
            }
        }
        // 审计计数（如实记账——生成/评估规模与编排产出一致）。
        orchestrated.audit.candidatesGenerated =
            static_cast<std::uint32_t>(kCandidateCount);
        orchestrated.audit.quickEvaluated =
            static_cast<std::uint32_t>(kQuickCandidateCount);
        orchestrated.audit.verifiedEvaluated = static_cast<std::uint32_t>(
            kCandidateCount - kQuickCandidateCount);

        // ---- 聚合工厂（一致性校验单点——归档态直出）----
        run = optimization::assembleRunResult(
            optimization::OptimizationRunId::generate(), project, branch,
            revision, snapshot, baselineRoot, baselineCv, config, orchestrated,
            {}, optimization::ArchivePhase::Archived);
        if (run.candidates.size() != kCandidateCount) {
            throw std::logic_error(
                "candidate-fixture: 候选规模偏离需求口径（实测 "
                + std::to_string(run.candidates.size()) + "，须 "
                + std::to_string(kCandidateCount) + "）");
        }
    }

    /// 六工件数据面组装（导出提供器产品实现——契约三元组记录面对齐当前面）。
    optimization::ExportBundleData buildBundle() const
    {
        optimization::ExportRequest request;
        request.run = run;
        request.spec = spec;
        request.currentProfile = spec.profile;   // 当前装配面＝记录面（无漂移）
        request.evaluatorContractVersionAtRun =
            optimization::kOptStaticScreenContractVersion;
        request.exportContractVersionAtRun =
            optimization::kOptExportContractVersion;
        request.formal = false;   // 限定语导出（当前性未投影——qualifier 承载）
        static const optimization::OptimizationExportProvider kProvider;
        return kProvider.buildExportBundle(request);
    }
};

/// 候选摘要行投影（分页窗口的单页物化——候选面板呈现行的模型侧等价：
/// 候选身份/状态/三项实值指标文本合成；物化行数即返回值——不超窗口）。
std::size_t projectCandidatePage(const optimization::OptimizationRunResult& run,
                                 std::size_t begin, std::size_t count)
{
    for (std::size_t k = 0; k < count; ++k) {
        const optimization::TwoStageRunRecord& c = run.candidates[begin + k];
        std::string row = c.candidateId.toCanonical();
        row += "|";
        row += core::toToken(c.engineeringStatus);
        for (std::size_t m = 0; m < 3; ++m) {
            const auto& entry = c.metrics.metrics[m];
            row += entry.valueSi.has_value()
                       ? "|" + std::to_string(*entry.valueSi)
                       : "|-";
        }
        if (row.empty()) {  // 恒假分支——防编译器把投影折叠为空操作
            throw std::logic_error("candidate-projection: 空行（夹具漂移）");
        }
    }
    return count;
}

/// 候选 CSV 会话写出（六工件的工件 2——候选摘要 canonical 持久化承载）。
void writeCandidatesCsv(const optimization::ExportBundleData& bundle,
                        const std::filesystem::path& target)
{
    auto writer = io::makeCsvWriter();
    io::CsvWriteOptions options;   // rwDefault 方言＋标识行（导出恒 true）
    const auto opened =
        writer->open(io::CsvOutputTarget::file(target), options);
    if (!opened) {
        throw std::runtime_error("candidates csv open 失败（写通道拒绝）");
    }
    const auto header = writer->writeHeader(bundle.candidatesCsvHeader);
    if (!header) {
        throw std::runtime_error("candidates csv 表头写出失败");
    }
    for (const auto& row : bundle.candidatesCsvRows) {
        const auto written = writer->writeRow(row);
        if (!written) {
            throw std::runtime_error("candidates csv 行写出失败");
        }
    }
    const auto finished = writer->finish();
    if (!finished) {
        throw std::runtime_error("candidates csv finish 失败（目标未就位）");
    }
}

/// 候选 CSV 单遍流式回读（io ICsvReader——行回调消费，峰值内存＝当前行；
/// 返回数据行数；表头列数经出参登记——字段契约核对面）。
std::uint64_t streamReadCandidatesCsv(const std::filesystem::path& file,
                                      std::size_t* outHeaderColumns)
{
    auto reader = io::makeCsvReader();
    io::IBudgetGuard* noBudget = nullptr;  // 基准轻量场景不记账（io §9.3 等价调整）
    io::IoCancelToken* noCancel = nullptr;
    const auto dialect = reader->probe(file, noBudget, noCancel);
    if (!dialect) {
        throw std::runtime_error("candidates csv probe 失败（方言不可辨）");
    }
    io::CsvReadOptions options;
    // 表头物理行号＝2（io §5.6 口径：方言标识行占物理行 1——本软件导出恒
    // 携带标识行〔CsvWriteOptions::emitDialectMarker 缺省 true〕，工件 2 的
    // 21 列字段契约表头在标识行之后，即物理行 2）。
    options.headerRow = 2;
    std::uint64_t dataRows = 0;
    std::size_t headerColumns = 0;
    const auto table = reader->read(
        file, options,
        [&dataRows](std::uint64_t, io::CsvRowView&& row) {
            ++dataRows;
            return true;                   // 继续（流式全量消费）
        },
        noBudget, noCancel, {});
    if (!table) {
        throw std::runtime_error("candidates csv read 失败（流式读取拒绝）");
    }
    headerColumns = table.value.report.header.size();
    if (outHeaderColumns != nullptr) { *outHeaderColumns = headerColumns; }
    return dataRows;
}

}  // namespace

// =====================================================================
// 基准用例——腿 A：任务点（5,000）加载/保存/分页
// =====================================================================

/**
 * 基准用例（B-03 腿 A）：5,000 任务点的 canonical 保存/加载与分页浏览。
 * 判定四面（零私设数值——B-03 无时延阈值）：
 *   ①保存/加载操作全部成功（canonical 往返＋io 原子写出通道）；
 *   ②规模恰为需求原文 5,000（往返计数＋全量等值核对钉住）；
 *   ③"不整体装入界面"：单页物化 ≤100 行、全量翻页累计＝5,000、50 页；
 *   ④三组（保存/加载/分页）零无效样本（每组 1 预热＋15 有效——留痕面）。
 */
TEST(LargeDataBenchmark, TaskPointsFiveThousandSaveLoadPagedBrowse_WP23T04_B03)
{
    IRD_TEST_INFO("NFR-PERF-03", {"B-03"}, std::nullopt);

    // ---- 黄金任务点集（5,000 条——确定性生成，一次构建全程共享）----
    const requirements::PointSet golden = makeGoldenPointSet();
    ASSERT_EQ(golden.entries.size(), kTaskPointCount)
        << "夹具规模偏离需求口径（NFR-PERF-03：5,000 任务点）";

    // ---- 保存/加载的落盘位（TempDir 并行隔离——testkit §6.2）----
    const testkit::TempDir dir("wp23-t04-taskpoints");
    const std::filesystem::path target = dir.path() / "req-point-set.bin";

    // 产品原子写出器（io §9.10——暂存＋原子替换，保存通道与产品装配同源）。
    const auto atomicWriter = io::makeAtomicFileWriter();

    // ---- 无锚定校验腿（untimed）：一次完整保存＋加载往返等值核对——
    // 分组测量的产物可信前提（编码/解码/字节落盘全链真跑一次并核对）。----
    std::vector<std::uint8_t> savedBytes;
    {
        const auto encoded = requirements::RequirementCodec{}.encode(
            requirements::RequirementObjectVariant(golden),
            requirements::kCurrentRequirementFormatVersion);
        ASSERT_TRUE(encoded.ok())
            << "5,000 任务点 canonical 编码失败：" << encoded.error().detail;
        savedBytes = encoded.get();
        auto prepared = atomicWriter->prepare(
            target, io::ReplacePolicy::OverwriteAtomic);
        ASSERT_TRUE(prepared) << "原子写出 prepare 失败（任务点保存面）";
        const auto written = prepared.value.write(
            std::string_view(reinterpret_cast<const char*>(savedBytes.data()),
                             savedBytes.size()));
        ASSERT_TRUE(written) << "原子写出单次写入失败（任务点保存面）";
        ASSERT_TRUE(atomicWriter->commit(prepared.value))
            << "原子写出 commit 失败（任务点保存面）";
    }
    {
        // 读回＋解码＋全量等值（加载面核对——往返逐条目相等）。
        std::ifstream in(target, std::ios::binary);
        ASSERT_TRUE(in) << "保存产物不可读（任务点加载面）";
        const std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        ASSERT_EQ(bytes.size(), savedBytes.size())
            << "落盘字节数与编码产物不符";
        const auto decoded = requirements::RequirementCodec{}.decode(
            bytes, requirements::kCurrentRequirementFormatVersion);
        ASSERT_TRUE(decoded.ok())
            << "5,000 任务点 canonical 解码失败：" << decoded.error().detail;
        const auto* loadedSet =
            std::get_if<requirements::PointSet>(&decoded.get());
        ASSERT_NE(loadedSet, nullptr) << "解码对象非任务点集合（wireType 漂移）";
        ASSERT_EQ(loadedSet->entries.size(), kTaskPointCount)
            << "解码条目数偏离需求规模";
        EXPECT_TRUE(*loadedSet == golden)
            << "canonical 往返不等值（5,000 任务点全量逐条目比对失败）";
    }

    // ---- 三组采样（保存/加载/分页——每组 1 预热＋15 有效）----
    std::vector<InteractionGroupReport> groups;
    groups.reserve(3);

    // 组 1：保存（canonical 编码＋原子写出 commit——持久化编码面全链）。
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("taskpoint-save"), [&] {
            const auto encoded = requirements::RequirementCodec{}.encode(
                requirements::RequirementObjectVariant(golden),
                requirements::kCurrentRequirementFormatVersion);
            if (!encoded.ok()) {
                throw std::runtime_error("编码失败：" + encoded.error().detail);
            }
            auto prepared = atomicWriter->prepare(
                target, io::ReplacePolicy::OverwriteAtomic);
            if (!prepared) { throw std::runtime_error("prepare 失败"); }
            const auto& bytes = encoded.get();
            if (!prepared.value.write(
                    std::string_view(
                        reinterpret_cast<const char*>(bytes.data()),
                        bytes.size()))) {
                throw std::runtime_error("写入失败");
            }
            if (!atomicWriter->commit(prepared.value)) {
                throw std::runtime_error("commit 失败");
            }
        }));
    // 组 2：加载（字节读回＋canonical 解码＋规模核对——消费侧入口全链）。
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("taskpoint-load"), [&] {
            std::ifstream in(target, std::ios::binary);
            if (!in) { throw std::runtime_error("保存产物不可读"); }
            std::vector<std::uint8_t> bytes(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
            const auto decoded = requirements::RequirementCodec{}.decode(
                bytes, requirements::kCurrentRequirementFormatVersion);
            if (!decoded.ok()) {
                throw std::runtime_error("解码失败：" + decoded.error().detail);
            }
            const auto* loadedSet =
                std::get_if<requirements::PointSet>(&decoded.get());
            if (loadedSet == nullptr
                || loadedSet->entries.size() != kTaskPointCount) {
                throw std::runtime_error("解码规模/类型偏离需求口径");
            }
        }));
    // 组 3：分页浏览（50 页 ×100 行窗口投影——全量翻页一遍）。
    PaginationObservation paging;
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("taskpoint-page"), [&] {
            const requirements::PointSet* view = &golden;
            // 逐页窗口投影（模型层等价——呈现行物化 ≤页大小）。
            PaginationObservation obs = pageThrough(
                view->entries.size(), kPageSize,
                [view](std::size_t begin, std::size_t count) {
                    return projectTaskPointPage(*view, begin, count);
                });
            paging = obs;  // 观测账本留最后一样本（结构判定用）
        }));

    // ---- 判定（样本纪律＋结构不变量——判据全部来自需求原文与 manifest）----
    PerfSessionReport session = makeSessionHeader("b03-taskpoints");
    for (const InteractionGroupReport& g : groups) {
        ASSERT_EQ(g.validCount, std::size_t{15})
            << g.opId << " 有效样本不足（协议 §4.2 下限）";
        ASSERT_EQ(g.invalidCount, std::size_t{0})
            << g.opId << " 存在无效样本（操作异常——详见报告留痕）";
        session.notes.push_back(
            std::string(g.opId) + ": 15 有效样本, P95="
            + std::to_string(g.p95Ms) + " ms, max=" + std::to_string(g.maxValidMs)
            + " ms（留痕不作判定）");
    }
    session.groups = groups;
    EXPECT_EQ(paging.pages, std::size_t{50})
        << "翻页次数偏离（5,000/100 页大小）";
    EXPECT_EQ(paging.rowsSeen, kTaskPointCount)
        << "全量翻页累计行数偏离 N（翻页未覆盖全部明细）";
    EXPECT_LE(paging.maxWindow, kPageSize)
        << "单次投影物化行数超页大小（不整体装入界面违约——NFR-PERF-03）";
    session.allPassed = paging.pages == 50 && paging.rowsSeen == kTaskPointCount
                        && paging.maxWindow <= kPageSize;
    session.notes.push_back(
        "分页观测：50 页×100 行, 累计物化 "
        + std::to_string(paging.rowsSeen) + "/5000, 单页最大 "
        + std::to_string(paging.maxWindow) + " 行（结构不变量判定面）");
    session.notes.push_back(
        "保存/加载面＝RequirementCodec canonical 编解码＋io 原子文件写出"
        "（对象库持久化编码面；项目命令管线归 draft/apply——边界如实申报）");

    // 报告落盘（拖挂——IRD_PERF_REPORT_DIR 未设则跳过；写失败不阻断判定）。
    if (const auto reportDir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*reportDir, session))
            << "报告写失败（拖挂不阻断——检查 IRD_PERF_REPORT_DIR）";
    }
}

// =====================================================================
// 基准用例——腿 B：采样结果（100,000）归档/加载/分页
// =====================================================================

/**
 * 基准用例（B-03 腿 B）：100,000 采样结果的归档保存/加载与分页浏览。
 * 判定四面（零私设数值）：
 *   ①归档保存全部成功（canonical 载荷＋归档端口 begin/writeBatch/finalize
 *     ——manifest 原子发布即完整）；
 *   ②加载成功（listRuns→runDir 产品编址读回＋字节完整性核对＋冻结布局
 *     独立解析 100,000 条状态行）；
 *   ③"不整体装入界面"：单页物化 ≤100 行、全量翻页累计＝100,000、1,000 页；
 *   ④三组零无效样本。
 * 样本集/结果/覆盖率由产品生成器真跑（generateSampleSet/computeCoverage
 * ——D-KIN-6 确定性），非预录数据。
 */
TEST(LargeDataBenchmark, SampleResultsHundredThousandArchiveLoadPagedBrowse_WP23T04_B03)
{
    IRD_TEST_INFO("NFR-PERF-03", {"B-03"}, std::nullopt);

    // ---- 黄金采样面夹具（产品生成器真跑——100,000 样本＋结果＋覆盖率）----
    const SamplingFixture fixture;

    // ---- 存储上下文（createNew——临时目录内真实项目，写权限就绪）----
    const testkit::TempDir dir("wp23-t04-archive");
    auto opened = project::ProjectStoreFactory::createNew(
        dir.path() / "b03-project", "B-03 大规模数据基准项目");
    ASSERT_NE(opened.store, nullptr) << "存储上下文创建失败";
    std::unique_ptr<project::ProjectStore> store = std::move(opened.store);
    const core::RevisionId revision = core::RevisionId::generate();

    // ---- 无锚定校验腿（untimed）：一次完整归档＋产品编址加载核对——
    // 指定运行（designated run）供加载组反复消费。----
    std::vector<std::uint8_t> designatedPayload;
    core::RunId designatedRun;
    archiveCoveragePayloadOnce(*store, fixture, revision, &designatedPayload,
                               &designatedRun);

    // 产品查询面读回＋冻结布局解析（加载全链一次核对——字节完整性＋计数；
    // 解析失败抛异常＝用例失败——加载不成功即断，不静默）。
    std::vector<SampleStateRow> designatedRows;
    {
        const auto runs = store->query().listRuns(revision);
        ASSERT_GE(runs.size(), std::size_t{1}) << "归档运行未被查询面发现";
        const std::filesystem::path runDir = store->query().runDir(designatedRun);
        const std::filesystem::path payloadFile = runDir / "coverage-payload.bin";
        std::ifstream in(payloadFile, std::ios::binary);
        ASSERT_TRUE(in) << "归档批次文件不可读（产品编址 runDir 失效）";
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        ASSERT_EQ(bytes.size(), designatedPayload.size())
            << "归档字节数偏离（sizeBytes 核对失败）";
        EXPECT_EQ(sha256Hex(bytes), sha256Hex(designatedPayload))
            << "归档字节完整性偏离（SHA-256 核对失败）";
        parseCoveragePayloadStateTable(bytes, kSampleResultCount,
                                       &designatedRows);
        ASSERT_EQ(designatedRows.size(), kSampleResultCount)
            << "解析状态行数偏离需求规模";
    }

    // ---- 三组采样（归档保存/加载/分页——每组 1 预热＋15 有效）----
    std::vector<InteractionGroupReport> groups;
    groups.reserve(3);

    // 组 1：归档保存（canonical 载荷编码＋begin/writeBatch/finalize 全链——
    // 每样本一次新运行归档，manifest 原子发布）。
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("sampleresult-archive-save"), [&] {
            archiveCoveragePayloadOnce(*store, fixture, revision, nullptr,
                                       nullptr);
        }));
    // 组 2：加载（listRuns→runDir 读回＋完整性核对＋冻结布局解析——消费
    // 侧入口全链）。
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("sampleresult-load"), [&] {
            const auto runs = store->query().listRuns(revision);
            if (runs.empty()) { throw std::runtime_error("运行清单为空"); }
            const std::filesystem::path runDir =
                store->query().runDir(designatedRun);
            std::ifstream in(runDir / "coverage-payload.bin", std::ios::binary);
            if (!in) { throw std::runtime_error("归档批次文件不可读"); }
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                            std::istreambuf_iterator<char>());
            if (bytes.size() != designatedPayload.size()
                || sha256Hex(bytes) != sha256Hex(designatedPayload)) {
                throw std::runtime_error("归档字节完整性偏离");
            }
            std::vector<SampleStateRow> rows;
            parseCoveragePayloadStateTable(bytes, kSampleResultCount, &rows);
            if (rows.size() != kSampleResultCount) {
                throw std::runtime_error("解析状态行数偏离需求规模");
            }
        }));
    // 组 3：分页浏览（1,000 页 ×100 行窗口投影——全量翻页一遍）。
    PaginationObservation paging;
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("sampleresult-page"), [&] {
            const std::vector<SampleStateRow>* view = &designatedRows;
            PaginationObservation obs = pageThrough(
                view->size(), kPageSize,
                [view](std::size_t begin, std::size_t count) {
                    // 逐样本状态行投影（呈现行物化——索引＋状态文本）。
                    for (std::size_t k = 0; k < count; ++k) {
                        const SampleStateRow& row = (*view)[begin + k];
                        std::string cell = std::to_string(row.sampleIndex);
                        cell += "|";
                        cell += std::to_string(row.state);
                        if (cell.empty()) {  // 恒假分支——防投影被折叠
                            throw std::logic_error("样本状态投影空行（夹具漂移）");
                        }
                    }
                    return count;
                });
            paging = obs;  // 观测账本留最后一样本（结构判定用）
        }));

    // ---- 判定（样本纪律＋结构不变量＋归档完整性）----
    PerfSessionReport session = makeSessionHeader("b03-sampleresults");
    for (const InteractionGroupReport& g : groups) {
        ASSERT_EQ(g.validCount, std::size_t{15})
            << g.opId << " 有效样本不足（协议 §4.2 下限）";
        ASSERT_EQ(g.invalidCount, std::size_t{0})
            << g.opId << " 存在无效样本（操作异常——详见报告留痕）";
        session.notes.push_back(
            std::string(g.opId) + ": 15 有效样本, P95="
            + std::to_string(g.p95Ms) + " ms, max=" + std::to_string(g.maxValidMs)
            + " ms（留痕不作判定）");
    }
    session.groups = groups;
    // 归档面证据：保存组每次归档一个新运行——查询面可见数＝1（指定）＋16
    // （保存组）＝17；全部含 manifest（D-13 只认完整运行）。
    {
        const auto runs = store->query().listRuns(revision);
        EXPECT_EQ(runs.size(), std::size_t{17})
            << "归档运行可见数偏离（1 指定＋16 保存组——manifest 发布核对）";
        session.notes.push_back(
            "归档可见运行 " + std::to_string(runs.size())
            + " 个（全部经 finalize manifest 发布——D-13 完整性判据）");
    }
    EXPECT_EQ(paging.pages, std::size_t{1000})
        << "翻页次数偏离（100,000/100 页大小）";
    EXPECT_EQ(paging.rowsSeen, kSampleResultCount)
        << "全量翻页累计行数偏离 N（翻页未覆盖全部明细）";
    EXPECT_LE(paging.maxWindow, kPageSize)
        << "单次投影物化行数超页大小（不整体装入界面违约——NFR-PERF-03）";
    // 状态模式核对（独立解析的分区计数与产品覆盖率计算的计数交叉核对——
    // 两面同源互证：样本集先全部位置样本后全部位姿样本（生成序黄金锁定
    // ——计划内位置段 sampleIndex [0, 分母一)，位姿段其余），两轴计数各自
    // 与 coverage.position/.orientation 对应分区一致）。
    {
        const std::size_t positionEnd =
            static_cast<std::size_t>(fixture.samples.plannedPositionSamples);
        std::size_t posUnreachable = 0;
        std::size_t posDataInsufficient = 0;
        std::size_t poseUnreachable = 0;
        std::size_t poseDataInsufficient = 0;
        for (std::size_t k = 0; k < designatedRows.size(); ++k) {
            const auto state = static_cast<kinematics::SampleState>(
                designatedRows[k].state);
            const bool inPositionSegment = k < positionEnd;
            if (state == kinematics::SampleState::Unreachable) {
                inPositionSegment ? ++posUnreachable : ++poseUnreachable;
            } else if (state == kinematics::SampleState::DataInsufficient) {
                inPositionSegment ? ++posDataInsufficient : ++poseDataInsufficient;
            }
        }
        EXPECT_EQ(posUnreachable, fixture.coverage.position.unreachable)
            << "位置轴解析 Unreachable 与产品覆盖率计算不一致（两面互证失败）";
        EXPECT_EQ(posDataInsufficient,
                  fixture.coverage.position.dataInsufficient)
            << "位置轴解析 DataInsufficient 与产品覆盖率计算不一致（互证失败）";
        EXPECT_EQ(poseUnreachable, fixture.coverage.orientation.unreachable)
            << "位姿轴解析 Unreachable 与产品覆盖率计算不一致（两面互证失败）";
        EXPECT_EQ(poseDataInsufficient,
                  fixture.coverage.orientation.dataInsufficient)
            << "位姿轴解析 DataInsufficient 与产品覆盖率计算不一致（互证失败）";
        EXPECT_GT(posUnreachable + poseUnreachable, std::size_t{0})
            << "Unreachable 分区缺失（状态模式漂移）";
        EXPECT_GT(posDataInsufficient + poseDataInsufficient, std::size_t{0})
            << "DataInsufficient 分区缺失（状态模式漂移）";
        session.notes.push_back(
            "状态表模式：位置轴 U/DI=" + std::to_string(posUnreachable) + "/"
            + std::to_string(posDataInsufficient) + ", 位姿轴 U/DI="
            + std::to_string(poseUnreachable) + "/"
            + std::to_string(poseDataInsufficient)
            + "（独立解析×产品覆盖率计算分段交叉核对一致）");
    }
    session.allPassed = paging.pages == 1000
                        && paging.rowsSeen == kSampleResultCount
                        && paging.maxWindow <= kPageSize;
    session.notes.push_back(
        "分页观测：1,000 页×100 行, 累计物化 "
        + std::to_string(paging.rowsSeen) + "/100000, 单页最大 "
        + std::to_string(paging.maxWindow) + " 行（结构不变量判定面）");
    session.notes.push_back(
        "加载面＝listRuns/runDir 产品编址＋冻结布局 IRDCV01 独立最小解析"
        "（conformance 面——kinematics 公共面只有编码无解码，第二实现仅在"
        "本测试目标，不入产品库——边界如实申报）");

    if (const auto reportDir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*reportDir, session));
    }
}

// =====================================================================
// 基准用例——腿 C：候选摘要（10,000）导出/加载/分页
// =====================================================================

/**
 * 基准用例（B-03 腿 C）：10,000 候选摘要的导出保存/加载与分页浏览。
 * 判定四面（零私设数值）：
 *   ①导出保存全部成功（六工件数据面组装＋工件 2 候选 CSV canonical 写出
 *     ——10,000 行落盘）；
 *   ②加载成功（io ICsvReader 单遍流式回读——10,000 数据行全量交付，峰
 *     值内存＝当前行）；
 *   ③"不整体装入界面"：单页物化 ≤100 行、全量翻页累计＝10,000、100 页；
 *   ④三组零无效样本。
 * 候选面由产品工厂真跑（assembleRunResult＋buildExportBundle——候选归属
 * 运行结果聚合 OPT-08；Quick 9,000 筛选批在前＋Verified 1,000 正式批）。
 */
TEST(LargeDataBenchmark, CandidateSummariesTenThousandExportLoadPagedBrowse_WP23T04_B03)
{
    IRD_TEST_INFO("NFR-PERF-03", {"B-03"}, std::nullopt);

    // ---- 黄金候选面夹具（10,000 候选运行结果聚合——归档态）----
    const CandidateFixture fixture;
    ASSERT_EQ(fixture.run.candidates.size(), kCandidateCount)
        << "夹具规模偏离需求口径（NFR-PERF-03：10,000 候选摘要）";

    // ---- 落盘位（TempDir 并行隔离）----
    const testkit::TempDir dir("wp23-t04-candidates");
    const std::filesystem::path csvTarget = dir.path() / "opt-candidates.csv";

    // ---- 无锚定校验腿（untimed）：一次完整导出＋流式回读核对——分组测量
    // 的产物可信前提（数据面组装/CSV 写出/流式读回全链真跑并核对）。----
    {
        const optimization::ExportBundleData bundle = fixture.buildBundle();
        ASSERT_EQ(bundle.candidatesCsvRows.size(), kCandidateCount)
            << "候选 CSV 数据面行数偏离需求规模";
        ASSERT_EQ(bundle.candidatesCsvHeader.size(), std::size_t{21})
            << "候选 CSV 字段契约列数漂移（v1 冻结 21 列）";
        writeCandidatesCsv(bundle, csvTarget);
        std::size_t headerColumns = 0;
        const std::uint64_t dataRows =
            streamReadCandidatesCsv(csvTarget, &headerColumns);
        EXPECT_EQ(dataRows, std::uint64_t{kCandidateCount})
            << "流式回读数据行数偏离需求规模";
        EXPECT_EQ(headerColumns, std::size_t{21})
            << "回读表头列数偏离字段契约";
    }

    // ---- 三组采样（导出保存/加载/分页——每组 1 预热＋15 有效）----
    std::vector<InteractionGroupReport> groups;
    groups.reserve(3);

    // 组 1：导出保存（六工件数据面组装＋候选 CSV canonical 写出——保存面
    // 全链；工件 6 候选包的产品写出适配器随 P-OPT-2 落位，不在本面）。
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("candidate-export-save"), [&] {
            const optimization::ExportBundleData bundle = fixture.buildBundle();
            if (bundle.candidatesCsvRows.size() != kCandidateCount) {
                throw std::runtime_error("候选 CSV 行数偏离需求规模");
            }
            writeCandidatesCsv(bundle, csvTarget);
        }));
    // 组 2：加载（io ICsvReader 单遍流式回读——行回调全量交付）。
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("candidate-load"), [&] {
            std::size_t headerColumns = 0;
            const std::uint64_t dataRows =
                streamReadCandidatesCsv(csvTarget, &headerColumns);
            if (dataRows != std::uint64_t{kCandidateCount}
                || headerColumns != std::size_t{21}) {
                throw std::runtime_error("流式回读规模/字段契约偏离");
            }
        }));
    // 组 3：分页浏览（100 页 ×100 行窗口投影——运行结果聚合上的全量翻页）。
    PaginationObservation paging;
    groups.push_back(testkit::runInteractionGroup(
        makeGroupSpec("candidate-page"), [&] {
            const optimization::OptimizationRunResult* view = &fixture.run;
            PaginationObservation obs = pageThrough(
                view->candidates.size(), kPageSize,
                [view](std::size_t begin, std::size_t count) {
                    return projectCandidatePage(*view, begin, count);
                });
            paging = obs;  // 观测账本留最后一样本（结构判定用）
        }));

    // ---- 判定（样本纪律＋结构不变量）----
    PerfSessionReport session = makeSessionHeader("b03-candidates");
    for (const InteractionGroupReport& g : groups) {
        ASSERT_EQ(g.validCount, std::size_t{15})
            << g.opId << " 有效样本不足（协议 §4.2 下限）";
        ASSERT_EQ(g.invalidCount, std::size_t{0})
            << g.opId << " 存在无效样本（操作异常——详见报告留痕）";
        session.notes.push_back(
            std::string(g.opId) + ": 15 有效样本, P95="
            + std::to_string(g.p95Ms) + " ms, max=" + std::to_string(g.maxValidMs)
            + " ms（留痕不作判定）");
    }
    session.groups = groups;
    EXPECT_EQ(paging.pages, std::size_t{100})
        << "翻页次数偏离（10,000/100 页大小）";
    EXPECT_EQ(paging.rowsSeen, kCandidateCount)
        << "全量翻页累计行数偏离 N（翻页未覆盖全部明细）";
    EXPECT_LE(paging.maxWindow, kPageSize)
        << "单次投影物化行数超页大小（不整体装入界面违约——NFR-PERF-03）";
    session.allPassed = paging.pages == 100 && paging.rowsSeen == kCandidateCount
                        && paging.maxWindow <= kPageSize;
    session.notes.push_back(
        "分页观测：100 页×100 行, 累计物化 "
        + std::to_string(paging.rowsSeen) + "/10000, 单页最大 "
        + std::to_string(paging.maxWindow) + " 行（结构不变量判定面）");
    session.notes.push_back(
        "保存/加载面＝六工件数据面组装＋工件 2 候选 CSV（21 列字段契约）"
        "＋io ICsvReader 单遍流式回读；工件 6 候选包的产品写出适配器随 "
        "P-OPT-2 裁决落位——保存面止于数据面＋CSV 写出（边界如实申报）");

    if (const auto reportDir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*reportDir, session));
    }
}

