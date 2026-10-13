/**
 * @file   PerfBenchmark.hpp
 * @brief  性能基准计时统计夹具（PerfBenchmark）——P95 分位数采样与交互
 *         响应分组测量的协议承载（WP-23-T02；B-06 吞吐中位口径的 T07
 *         消费面预留同型扩展点）。
 *
 * 设计依据：
 *   - units/testkit.md §10.1"性能基准夹具"行（计时统计/吞吐对照口径的
 *     PERF 侧夹具细则随 NFR-PERF-01/05 执行任务届时提出——本头即该行的
 *     WP-23-T02 消费者提案落位：计时统计夹具）；
 *   - doc/industrial-robot-design/performance-baseline.md §4.2（样本量与
 *     P95 计算——每组有效样本 ≥15、剔除各组首次预热、P95＝线性插值分位
 *     数、不得只报统计值）＋§4.3（交互响应测量口径——端到端响应时长、
 *     无响应窗口、禁止 3 次中位数替代 P95）；
 *   - 需求 NFR-PERF-01（P95 ≤200 ms 等达标数值——REQUIREMENTS §19.3，
 *     本头零阈值常量，阈值由消费方测试以需求原文为唯一来源显式传入）；
 *   - 任务契约 tasks/foundation/WP-23-T02.json（≙DTB §2.24 行）。
 *
 * 背景说明：B-01 是 P95 分位数类基准（performance-baseline §2 统计口径
 * 区分明文；与 B-06 中位吞吐互不替代）——本夹具因此实现三件事：
 *   ①分组采样执行器（runInteractionGroup）：预热剔除＋有效样本循环采集
 *     （每组有效 ≥15 由规格承载、由消费方按协议给定）＋原始样本序列全量
 *     留痕（预热与无效样本不删——只标 excluded/invalid 并留原因，协议
 *     §4.2"剔除须登记"、§4.8"不得只报统计值"）；
 *   ②P95 线性插值分位数（percentileLinearInterpolated）：协议 §4.2 公式
 *     逐条实现（h＝(n−1)×q，x⌊h⌋＋(h−⌊h⌋)×(x⌊h⌋+1−x⌊h⌋)）；约定串随
 *     报告输出（"同一报告内不得混用两种分位数约定"的结构保证——全库仅
 *     此一个实现点）；
 *   ③报告（PerfSessionReport→JSON 落盘）：环境登记（§4.1 清单的字段位）、
 *     软件版本（commit＋构建配置）、数据集 datasetId＋版本、逐组原始样本
 *     序列、分位数约定与结果、阈值判定（判定值与出处由消费方传入——本头
 *     不私设达标数值）。报告写出是**拖挂不阻断**：写失败不抛、不改调用
 *     方判定（ReproBenchmark 同款纪律）。
 *
 * 分层纪律：库本体零 gtest（D-06）、零 Qt、零业务域知识——五类交互操作
 * （导航/选择/筛选/编辑/视图切换）的具体驱动形态归消费方测试目标；本头
 * 只承载"计时→统计→留痕"的协议通用半区。B-06（中位吞吐）与本夹具的
 * 分位半区互不替代，其吞吐对照细则归 WP-23-T07 届时提出（本头不预建）。
 *
 * 线程安全：全部纯函数/纯值；runInteractionGroup 在调用线程同步执行
 * （被测操作闭包由调用方保证在其声明的线程模型内调用——本夹具不改写
 * 任何线程语义）。
 */

#ifndef SDURWS_IRD_TESTKIT_PERFBENCHMARK_HPP
#define SDURWS_IRD_TESTKIT_PERFBENCHMARK_HPP

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <sdurws/ird/testkit/JsonLite.hpp>  // JsonValue/dumpJson（报告序列化）

namespace sdurws::ird::testkit {

// =====================================================================
// P95 线性插值分位数（协议 §4.2 公式的唯一实现点）
// =====================================================================

/**
 * @brief 线性插值分位数（performance-baseline §4.2 原文公式的直接实现）。
 *
 * 公式（照抄协议原文）：样本升序排列，位置 h＝(n−1)×q（n 为有效样本数），
 * P(q)＝x⌊h⌋＋(h−⌊h⌋)×(x⌊h⌋+1−x⌊h⌋)。
 *
 * @param samples [in] 原始样本序列（任意序——函数内部升序排序副本，不入参
 *                序列不被修改）；空序列属调用方契约违约（无样本无从谈
 *                分位数——fail-fast 抛出，不返回伪值）
 * @param q       [in] 分位位次 ∈ [0,1]（0.95＝P95；越界抛 std::out_of_range
 *                ——调用方错误 fail-fast）
 * @return 分位数值（与样本同单位——本库调用面统一毫秒）
 *
 * @throws std::invalid_argument samples 为空
 * @throws std::out_of_range q 越界
 *
 * 复杂度：O(n log n)（排序主导——基准样本量 10⁰~10² 量级，微秒级）。
 * 分位数约定登记串：p95Convention 常量（见下）——报告与实现同源。
 */
inline double percentileLinearInterpolated(const std::vector<double>& samples,
                                           double q)
{
    if (samples.empty()) {
        throw std::invalid_argument(
            "percentileLinearInterpolated: 空样本序列（无样本无从谈分位数）");
    }
    if (q < 0.0 || q > 1.0) {
        throw std::out_of_range(
            "percentileLinearInterpolated: 分位位次 q 越界（须 ∈ [0,1]）");
    }
    // 升序排列副本（协议"样本升序排列"——不入参序列保持原样）。
    std::vector<double> sorted(samples);
    std::sort(sorted.begin(), sorted.end());
    // h＝(n−1)×q；⌊h⌋ 与小数部分分离（n≥1 保证下标不越界——h≤n−1）。
    const double h = static_cast<double>(sorted.size() - 1) * q;
    const auto lower = static_cast<std::size_t>(h);          // ⌊h⌋
    const double frac = h - static_cast<double>(lower);       // h−⌊h⌋
    if (lower + 1 >= sorted.size()) {
        return sorted[lower];  // frac==0 或 h 恰为末位——纯样本值（插值退化）
    }
    return sorted[lower] + frac * (sorted[lower + 1] - sorted[lower]);
}

/// 分位数约定登记串（报告 p95Convention 字段唯一取值——§4.2"登记所用
/// 分位数约定"；实现点与登记串同文件同源，防两处漂移）。
inline const char* p95ConventionToken() noexcept
{
    return "linear-interpolation:h=(n-1)*q;x=floor+f*(next-floor)";
}

// =====================================================================
// 交互响应分组采样（§4.3 测量口径的执行器半区）
// =====================================================================

/**
 * @brief 单次执行样本（原始观测——有效/预热/无效三态全量留痕）。
 *
 * 协议依据（§4.2）：预热样本不计入统计但须可追溯（"剔除各组首次预热"）；
 * 无效样本（测量区间不完整——本夹具以"操作闭包抛出异常"承载）不计入
 * 分母且留痕登记失效原因（"留痕登记失效原因"）。
 */
struct InteractionSample {
    double durationMs = 0.0;  ///< 本次执行墙钟时长，单位 ms（steady_clock 实测）
    bool excluded = false;    ///< true＝预热样本（不计入分位数统计）
    bool valid = true;        ///< false＝无效样本（测量区间不完整——不计入分母）
    std::string invalidReason; ///< 无效原因（valid==false 时非空——异常 what()）
};

/**
 * @brief 分组采样规格（一组＝某操作 × 某负载态的完整采样轮）。
 *
 * 默认值即协议下限（§4.2：每组有效样本 ≥15；剔除各组首次预热——预热
 * 默认 1 次）。消费方可加大不可减小（减小＝收窄协议、验收对抗项）。
 */
struct InteractionGroupSpec {
    std::string opId;               ///< 操作标识（"nav"/"select"/"filter"/"edit"/"viewswitch"）
    std::string stateId;            ///< 负载态标识（"idle"/"background"——§4.3 两态）
    std::size_t warmupRuns = 1;     ///< 预热次数（§4.2 默认剔除首次预热——≥1）
    std::size_t minValidSamples = 15; ///< 有效样本下限（§4.2 每组 ≥15）
};

/**
 * @brief 分组测量报告（原始样本序列＋P95 统计——协议 §4.8 留痕面）。
 *
 * samples 是**全量执行序列**（执行序：预热在前、采样在后；无效样本原位
 * 留痕）——统计面（p95/max/min）只由 valid∧!excluded 的样本构成。p95 在
 * 构造时由 runInteractionGroup 计算填入；字段零预填零虚构。
 */
struct InteractionGroupReport {
    std::string opId;    ///< 操作标识（继承自规格）
    std::string stateId; ///< 负载态标识（继承自规格）
    std::vector<InteractionSample> samples; ///< 全量执行序列（执行序留痕）
    std::size_t warmupRuns = 0;      ///< 实际执行的预热次数（回填自规格）
    std::size_t validCount = 0;      ///< 有效样本数（分位数分母）
    std::size_t invalidCount = 0;    ///< 无效样本数（不计入分母——原因在样本内）
    double p95Ms = 0.0;              ///< P95（线性插值——仅 valid∧!excluded 参与统计）
    double maxValidMs = 0.0;         ///< 有效样本最大值（无响应窗口观测的样本面）
    double minValidMs = 0.0;         ///< 有效样本最小值（分布下沿——报告完整性）
};

/**
 * @brief 执行一组交互响应采样（预热剔除＋有效样本循环＋P95 统计）。
 *
 * 执行序（全部真实测量，零预填——协议"测量数字必须真实产生"）：
 *   ①预热腿：执行 spec.warmupRuns 次，样本标记 excluded（不计入统计——
 *     §4.2"剔除各组首次预热"）；
 *   ②采样腿：继续执行直到有效样本数达 spec.minValidSamples；操作闭包
 *     抛出的异常被捕获→该次记为无效样本（原因＝异常消息）并继续——
 *     无效不中断采样（协议 §4.2"无效样本不计入分母"），但**逐例留痕**，
 *     消费方测试须断言 invalidCount==0（夹具不吞错——异常如实进报告，
 *     判定面由消费方收口）；
 *   ③统计：P95/max/min 只由 valid∧!excluded 样本计算（空有效集——即
 *     有效样本不可能为零的规格下不可能到达——抛 std::invalid_argument
 *     防御）。
 *
 * @tparam OpFn 可调用对象：void()（被测交互操作的一次完整执行——"自操作
 *              发起至用户可感知完成"的端到端区间由闭包内界划定；本夹具
 *              只在闭包外沿计时，不感知内部结构）
 * @param spec [in] 分组规格（opId/stateId/预热/样本下限）
 * @param op   [in] 被测操作闭包（每样本调用一次；不得重入本夹具）
 * @return 分组报告（原始序列＋统计；无异常路径——操作异常已折叠为无效
 *         样本，规格违约〔minValidSamples==0〕抛 std::invalid_argument）
 *
 * @throws std::invalid_argument minValidSamples==0（无统计意义的规格——
 *         调用方错误 fail-fast）
 *
 * 复杂度：O((预热＋有效＋无效)×(操作代价＋log n 统计))。
 */
template <class OpFn>
InteractionGroupReport runInteractionGroup(const InteractionGroupSpec& spec,
                                           OpFn&& op)
{
    if (spec.minValidSamples == 0) {
        throw std::invalid_argument(
            "runInteractionGroup: minValidSamples==0（无效样本下限——"
            "协议 §4.2 每组有效样本 ≥15，0 规格无统计意义）");
    }

    InteractionGroupReport r;
    r.opId = spec.opId;
    r.stateId = spec.stateId;
    r.warmupRuns = spec.warmupRuns;

    // ---- 预热腿（§4.2：剔除各组首次预热——样本留痕、标记不计入统计）----
    for (std::size_t i = 0; i < spec.warmupRuns; ++i) {
        InteractionSample s;
        s.excluded = true;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            op();
        } catch (const std::exception& e) {
            s.valid = false;
            s.invalidReason = e.what();
        }
        const auto t1 = std::chrono::steady_clock::now();
        s.durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.samples.push_back(s);
    }

    // ---- 采样腿（循环到有效样本达标——无效样本不计入分母但逐例留痕）----
    std::vector<double> validMs;  // 统计输入（valid∧!excluded 的时长序列）
    validMs.reserve(spec.minValidSamples);
    while (r.validCount < spec.minValidSamples) {
        InteractionSample s;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            op();
        } catch (const std::exception& e) {
            // 无效样本：测量区间不完整（操作中途失败）——原因留痕后继续
            // （协议 §4.2"不计入分母，并留痕登记失效原因"；消费方测试
            // 断言 invalidCount==0 收口，夹具不静默吞错）。
            s.valid = false;
            s.invalidReason = e.what();
        }
        const auto t1 = std::chrono::steady_clock::now();
        s.durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (s.valid) {
            ++r.validCount;
            validMs.push_back(s.durationMs);
        } else {
            ++r.invalidCount;
        }
        r.samples.push_back(s);
    }

    // ---- 统计面（只由有效样本构成——P95 线性插值，§4.2 公式）----
    r.p95Ms = percentileLinearInterpolated(validMs, 0.95);
    r.maxValidMs = *std::max_element(validMs.begin(), validMs.end());
    r.minValidMs = *std::min_element(validMs.begin(), validMs.end());
    return r;
}

// =====================================================================
// 阈值判定记录（判定值与出处由消费方传入——本头零私设达标数值）
// =====================================================================

/**
 * @brief 单组阈值判定记录（报告 verdicts 元素——达标数值的唯一来源是
 *        消费方以需求原文传入的 thresholdMs 与其出处登记）。
 *
 * 设计动机：200 ms 等达标数值属需求语义（REQUIREMENTS NFR-PERF-01 原文），
 * 夹具不内嵌任何业务阈值（零私设数值纪律——ReproBenchmark 的容差经档案
 * 承载同型）；本结构只做"消费方判定结论"的留痕载体。
 */
struct PerfThresholdVerdict {
    std::string opId;        ///< 操作标识（与组一一对应）
    std::string stateId;     ///< 负载态标识
    double thresholdMs = 0.0; ///< 阈值，单位 ms（消费方按需求原文传入）
    std::string thresholdSource; ///< 阈值出处登记（如 "REQUIREMENTS NFR-PERF-01"）
    double measuredP95Ms = 0.0;  ///< 实测 P95（对应组报告值——回填自测量）
    bool pass = false;           ///< 判定结论（measuredP95Ms ≤ thresholdMs）
};

// =====================================================================
// 报告输出（拖挂不阻断——写失败不抛、不改调用方判定）
// =====================================================================

/// JSON 字符串值工厂（JsonValue 聚合面的小型类型安全包装——防窄化误排）。
inline JsonValue perfJsonStr(std::string s)
{
    JsonValue v;
    v.kind = JsonValue::Kind::String;
    v.text = std::move(s);
    return v;
}

/// JSON 数值值工厂（double 原样——dumpJson 数值格式化确定性）。
inline JsonValue perfJsonNum(double d)
{
    JsonValue v;
    v.kind = JsonValue::Kind::Number;
    v.number = d;
    return v;
}

/// JSON 布尔值工厂。
inline JsonValue perfJsonBool(bool b)
{
    JsonValue v;
    v.kind = JsonValue::Kind::Bool;
    v.boolean = b;
    return v;
}

/**
 * @brief 读取报告输出目录环境变量（可选拖挂的开关——ReproBenchmark 同款）。
 *
 * @param envName [in] 环境变量名（默认 "IRD_PERF_REPORT_DIR"——基准执行方
 *                     按需设置；未设或空串＝不写报告文件）
 * @return 已设返回目录路径（原样，不做词法归一）；未设返回 nullopt
 */
inline std::optional<std::filesystem::path>
perfReportDirFromEnv(const char* envName = "IRD_PERF_REPORT_DIR")
{
    const char* v = std::getenv(envName);
    if (v == nullptr || *v == '\0') { return std::nullopt; }
    return std::filesystem::path(v);
}

/// 单组采样报告 → JSON 对象（原始样本序列全量输出——协议 §4.8"不得只报
/// 统计值"；键序＝装配序，确定性输出）。
inline JsonValue interactionGroupToJson(const InteractionGroupReport& g)
{
    JsonValue o;
    o.kind = JsonValue::Kind::Object;
    o.members.push_back({"opId", perfJsonStr(g.opId)});
    o.members.push_back({"stateId", perfJsonStr(g.stateId)});
    o.members.push_back({"warmupRuns", perfJsonNum(static_cast<double>(g.warmupRuns))});
    o.members.push_back({"validCount", perfJsonNum(static_cast<double>(g.validCount))});
    o.members.push_back({"invalidCount", perfJsonNum(static_cast<double>(g.invalidCount))});
    o.members.push_back({"p95Ms", perfJsonNum(g.p95Ms)});
    o.members.push_back({"maxValidMs", perfJsonNum(g.maxValidMs)});
    o.members.push_back({"minValidMs", perfJsonNum(g.minValidMs)});
    JsonValue arr;
    arr.kind = JsonValue::Kind::Array;
    for (const InteractionSample& s : g.samples) {
        JsonValue e;
        e.kind = JsonValue::Kind::Object;
        e.members.push_back({"durationMs", perfJsonNum(s.durationMs)});
        e.members.push_back({"excluded", perfJsonBool(s.excluded)});
        e.members.push_back({"valid", perfJsonBool(s.valid)});
        e.members.push_back({"invalidReason", perfJsonStr(s.invalidReason)});
        arr.items.push_back(std::move(e));
    }
    o.members.push_back({"samples", std::move(arr)});
    return o;
}

/**
 * @brief 基准会话报告（一次基准执行的完整留痕——协议 §4.8 必含字段面）。
 *
 * 字段分四组：标识与版本组（commit/构建配置——§4.1 软件版本锁定）、环境
 * 组（§4.1 登记清单的字段位——由消费方以环境事实填充，本头不探测硬件）、
 * 数据集组（datasetId＋版本——§4.8）、测量与判定组（逐组报告＋阈值判定
 * ＋原始样本）。allPassed 只由消费方判定结论聚合（verdicts 全 pass），
 * 本头不参与任何达标判定。
 */
struct PerfSessionReport {
    std::string caseId;            ///< 用例标识（报告文件名主体）
    std::string baselineDoc;       ///< 基准清单行登记（如 "performance-baseline §2 B-01"）
    std::string gitCommit;         ///< 软件版本锁定：commit SHA（§4.1；可空＝未注入）
    std::string buildConfig;       ///< 构建配置（Release/Debug——§4.1）
    std::string datasetId;         ///< 数据集标识（§4.8；可空＝无数据集）
    std::string datasetVersion;    ///< 数据集版本
    // ---- 环境登记（§4.1 清单的字段位——消费方以环境事实填充）----
    std::string machineName;       ///< 机器标识（§4.1"同一台机器"）
    std::string cpuInfo;           ///< CPU 描述
    std::string osInfo;            ///< 操作系统版本
    std::string memoryInfo;        ///< 内存配置
    std::string storageInfo;       ///< 存储配置
    std::string powerPolicyNote;   ///< 电源策略（§4.1 登记项；如实登记可得形态）
    std::string backgroundNote;    ///< 测量期间后台进程基线说明（§4.1）
    // ---- 测量与判定 ----
    std::vector<InteractionGroupReport> groups;      ///< 逐组报告（opId×stateId）
    std::vector<PerfThresholdVerdict> verdicts;      ///< 阈值判定（消费方传入）
    bool allPassed = false;        ///< verdicts 全 pass（消费方聚合结论的回填）
    std::vector<std::string> notes; ///< 备注（如实登记——如等价性口径说明指针）
};

/**
 * @brief 会话报告序列化为单行 JSON（键序＝装配序——确定性输出）。
 *
 * 序列化经 JsonLite（TK-T02——受限 JSON 唯一入口）。分位数约定串恒输出
 * （p95ConventionToken——"同一报告内不得混用两种分位数约定"的登记面）。
 */
inline std::string perfReportToJson(const PerfSessionReport& r)
{
    JsonValue o;
    o.kind = JsonValue::Kind::Object;

    // ---- 标识与版本（§4.1 软件版本锁定＋§4.8 留痕字段）----
    o.members.push_back({"schemaVersion", perfJsonStr("ird-perf-report/1")});
    o.members.push_back({"caseId", perfJsonStr(r.caseId)});
    o.members.push_back({"baselineDoc", perfJsonStr(r.baselineDoc)});
    o.members.push_back({"p95Convention", perfJsonStr(p95ConventionToken())});
    o.members.push_back({"gitCommit", perfJsonStr(r.gitCommit)});
    o.members.push_back({"buildConfig", perfJsonStr(r.buildConfig)});

    // ---- 数据集（§4.8：datasetId＋版本）----
    o.members.push_back({"datasetId", perfJsonStr(r.datasetId)});
    o.members.push_back({"datasetVersion", perfJsonStr(r.datasetVersion)});

    // ---- 环境登记（§4.1 清单——消费方填充的环境事实）----
    o.members.push_back({"machineName", perfJsonStr(r.machineName)});
    o.members.push_back({"cpuInfo", perfJsonStr(r.cpuInfo)});
    o.members.push_back({"osInfo", perfJsonStr(r.osInfo)});
    o.members.push_back({"memoryInfo", perfJsonStr(r.memoryInfo)});
    o.members.push_back({"storageInfo", perfJsonStr(r.storageInfo)});
    o.members.push_back({"powerPolicyNote", perfJsonStr(r.powerPolicyNote)});
    o.members.push_back({"backgroundNote", perfJsonStr(r.backgroundNote)});

    // ---- 逐组测量（原始样本序列全量——§4.8）----
    JsonValue groupsArr;
    groupsArr.kind = JsonValue::Kind::Array;
    for (const InteractionGroupReport& g : r.groups) {
        groupsArr.items.push_back(interactionGroupToJson(g));
    }
    o.members.push_back({"groups", std::move(groupsArr)});

    // ---- 阈值判定（数值来源与出处由消费方传入——本头零私设）----
    JsonValue verdictsArr;
    verdictsArr.kind = JsonValue::Kind::Array;
    for (const PerfThresholdVerdict& v : r.verdicts) {
        JsonValue e;
        e.kind = JsonValue::Kind::Object;
        e.members.push_back({"opId", perfJsonStr(v.opId)});
        e.members.push_back({"stateId", perfJsonStr(v.stateId)});
        e.members.push_back({"thresholdMs", perfJsonNum(v.thresholdMs)});
        e.members.push_back({"thresholdSource", perfJsonStr(v.thresholdSource)});
        e.members.push_back({"measuredP95Ms", perfJsonNum(v.measuredP95Ms)});
        e.members.push_back({"pass", perfJsonBool(v.pass)});
        verdictsArr.items.push_back(std::move(e));
    }
    o.members.push_back({"verdicts", std::move(verdictsArr)});

    // ---- 总判定与备注 ----
    o.members.push_back({"allPassed", perfJsonBool(r.allPassed)});
    JsonValue notesArr;
    notesArr.kind = JsonValue::Kind::Array;
    for (const std::string& n : r.notes) {
        notesArr.items.push_back(perfJsonStr(n));
    }
    o.members.push_back({"notes", std::move(notesArr)});

    return dumpJson(o, false);
}

/**
 * @brief 把会话报告写入 <dir>/<caseId>.json（目录按需创建）。
 *
 * @param dir    [in] 输出目录（不存在则递归创建——创建/写失败均不抛）
 * @param report [in] 待写报告
 * @return 写入成功 true；目录创建失败/打开失败/写失败 false（拖挂不阻断
 *         ——判定不受影响；调用方自行决定是否留痕写失败）
 */
inline bool writePerfReportJson(const std::filesystem::path& dir,
                                const PerfSessionReport& report)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) { return false; }
    std::ofstream out(dir / (report.caseId + ".json"), std::ios::binary | std::ios::trunc);
    if (!out) { return false; }
    const std::string text = perfReportToJson(report);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_PERFBENCHMARK_HPP
