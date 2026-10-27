/**
 * @file   ReproBenchmark.hpp
 * @brief  确定性复现基准夹具（ReproBenchmark）——同输入＋版本＋配置＋线程＋
 *         种子下"等价候选集合＋稳定排序"的复跑测量与判定（WP-23-T09）。
 *
 * 设计依据：
 *   - units/testkit.md §10.1"性能基准夹具"行（夹具细则届时由 WP-23 消费者
 *     提出——本头即该行的消费者提案落位：确定性复现测量夹具；计时统计仅
 *     作原始样本留痕，不作任何阈值判定）；
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-08 行（达标
 *     口径照抄需求原文）＋§4.6（并行归约与确定性容差——附录 D 第 8 项
 *     1×10⁻¹²，通用比较公式逐元素判定；不要求浮点文件逐字节相同）；
 *   - 需求 NFR-COR-02（等价候选集合与稳定排序——REQUIREMENTS §19.1）；
 *   - 任务契约 tasks/foundation/WP-23-T09.json（≙DTB §2.24 行）。
 *
 * 背景说明：B-08 是"集合等价与容差判定"类基准（performance-baseline §2
 * 统计口径区分明文：不涉及分位数统计）——本夹具因此不做 P95/中位数等
 * 分位数计算，只做三类真实测量并原样记录：
 *   ①同配置复跑腿（rerun）：同输入/版本/配置/线程/种子再跑一次——确定性
 *     基线（NFR-COR-02 的"同一"面）；
 *   ②线程等价腿（threads，可选）：仅改变线程数再跑——并行归约容差面
 *     （附录 D 第 8项：相对 1×10⁻¹²，经 profile 条目承载，本夹具零私设
 *     数值）；
 *   ③逐腿墙钟时长与元素数（steady_clock）——原始样本序列留痕（协议 §4.8
 *     "不得只报统计值"精神；B-08 无时长阈值，超时判定不属本基准）。
 * 集合等价与稳定排序的判定完全委托 SetCheck（TK-T06 两阶段匹配——§5.4），
 * 容差经 ToleranceProfile 条目解析（TK-T04）——本头零业务规则、零容差数值
 * （§5.4.5 业务适配器归域；数值唯一来源＝档案文件）。
 *
 * 报告（toJson/writeReproReportJson）：单用例一份 JSON——确定性上下文
 * （ReproRecord）、逐腿测量、判定结论、最大逐元素偏差观测值。报告写出是
 * **拖挂不阻断**：写失败不抛、不改判定（判定只由集合/顺序结论构成）。
 *
 * 分层纪律：库本体零 gtest（D-06）；业务适配（元素身份键与数值字段投影）
 * 由消费方 _test 翻译单元经 SetMatchTraits 特化注入——本头零域知识。
 *
 * 线程安全：全部纯函数/纯值（profile 只读；run 闭包由调用方保证可重入或
 * 串行调用——本夹具三腿顺序执行，不并发调用 run）。
 */

#ifndef SDURWS_IRD_TESTKIT_REPROBENCHMARK_HPP
#define SDURWS_IRD_TESTKIT_REPROBENCHMARK_HPP

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/testkit/Fixture.hpp>          // ReproRecord（确定性上下文）
#include <sdurws/ird/testkit/JsonLite.hpp>         // JsonValue/dumpJson（报告序列化）
#include <sdurws/ird/testkit/SetCheck.hpp>         // 两阶段集合/顺序断言（TK-T06）
#include <sdurws/ird/testkit/ToleranceProfile.hpp> // 容差档案（TK-T04）

namespace sdurws::ird::testkit {

// =====================================================================
// 测量值面（真实观测的承载——全部字段由运行产生，零预填）
// =====================================================================

/**
 * @brief 单腿测量（一次真实运行的观测）。
 *
 * "腿"（leg）＝被测计算的一次完整执行。时长为墙钟实测（steady_clock），
 * 单位毫秒（double——亚毫秒分辨率保留）；元素数为该腿产出集大小。
 */
struct ReproLegMeasurement {
    std::string legId;            ///< 腿标识（"reference"/"rerun"/"threads-<n>"）
    std::uint32_t threadCount = 1; ///< 本腿传入被测计算的线程数（执行参数）
    double durationMs = 0.0;      ///< 墙钟时长，单位 ms（实测——B-08 无阈值）
    std::size_t elementCount = 0; ///< 产出元素数（原始观测）
};

/**
 * @brief 单腿判定结论（集合等价＋稳定排序——语义全委托 SetCheck）。
 */
struct ReproLegVerdict {
    std::string legId;                 ///< 对应腿标识（与本腿测量一一对应）
    bool setEquivalent = false;        ///< checkSetEquivalent 结果
    bool sameOrder = false;            ///< checkStableOrder 结果
    std::size_t missingExpected = 0;   ///< 期望侧未匹配元素数（集合违例定位）
    std::size_t extraActual = 0;       ///< 实际侧多余元素数
    std::size_t duplicates = 0;        ///< actual 身份键重复数（阶段 0 违例）
    bool ambiguousMatch = false;       ///< 多个完美匹配在场（不判失败——§5.4.4）
    std::size_t mismatchedValueCount = 0; ///< 数值超容差字段数（identity-value-
                                          ///  mismatch＋missing/extra 最近对详情）
};

/**
 * @brief 复现用例报告（单用例单份——基准执行的原始记录，协议 §4.8 留痕面）。
 *
 * 字段分两组：判定组（集合/顺序结论＋违例定位）与测量组（逐腿时长/元素数
 * ＋最大逐元素偏差观测）。passed 只由判定组构成；测量组如实记录、不参与
 * 通过判定（B-08 达标口径＝集合等价＋稳定排序＋归约容差，无时长阈值）。
 */
struct ReproCaseReport {
    // ---- 用例标识与确定性上下文 ----
    std::string caseId;           ///< 用例标识（报告文件名主体）
    std::string domain;           ///< 域 token（"kinematics"/"trajectory"/…——报告分组）
    std::string inputProvenance;  ///< 输入来源登记（夹具/生成器标识＋关键参数——
                                  ///  自建输入的 datasetId 替代登记，协议 §4.8）
    ReproRecord repro;            ///< 确定性上下文（seed/threadCount/版本——唯一来源）
    std::string toleranceProfile; ///< 容差档案 "<id>@<version>"（数值唯一来源）

    // ---- 测量组（真实运行产生）----
    std::vector<ReproLegMeasurement> legs;  ///< 逐腿测量（执行序）
    double maxAbsDeviation = 0.0;           ///< 匹配对逐元素 |a−b| 最大观测值（SI）
    double maxRelativeDeviation = 0.0;      ///< 逐元素 |a−b|/|ref| 最大观测值
                                            ///  （仅 |ref|>0 的字段参与——零参考退化
                                            ///  面由绝对分量承载，C4）
    std::size_t comparedValueCount = 0;     ///< 实际逐元素比较过的数值总数

    // ---- 判定组（SetCheck 结论）----
    std::vector<ReproLegVerdict> verdicts;  ///< 逐腿判定（与 legs 除 reference 腿
                                            ///  外一一对应——reference 是对照基线）
    bool passed = false;                    ///< 全部腿 setEquivalent∧sameOrder
    std::vector<std::string> violations;    ///< 未通过原因（可读——失败定位首因）
};

// =====================================================================
// 报告输出（拖挂不阻断——写失败不抛、不改判定）
// =====================================================================

/// JSON 字符串值工厂（JsonValue 聚合面的小型类型安全包装——防窄化误排）。
inline JsonValue jsonStr(std::string s)
{
    JsonValue v;
    v.kind = JsonValue::Kind::String;
    v.text = std::move(s);
    return v;
}

/// JSON 数值值工厂（double 原样——dumpJson 数值格式化确定性）。
inline JsonValue jsonNum(double d)
{
    JsonValue v;
    v.kind = JsonValue::Kind::Number;
    v.number = d;
    return v;
}

/// JSON 布尔值工厂。
inline JsonValue jsonBool(bool b)
{
    JsonValue v;
    v.kind = JsonValue::Kind::Bool;
    v.boolean = b;
    return v;
}

/**
 * @brief 读取报告输出目录环境变量（可选拖挂的开关）。
 *
 * @param envName [in] 环境变量名（默认 "IRD_REPRO_REPORT_DIR"——基准执行方
 *                     按需设置；未设或空串＝不写报告文件）
 * @return 已设返回目录路径（原样，不做词法归一）；未设返回 nullopt
 *
 * 线程安全：std::getenv 只读（与并发写该变量的外部程序存在理论竞争——
 * 测试进程内无人写该变量，实践中安全）。
 */
inline std::optional<std::filesystem::path>
reproReportDirFromEnv(const char* envName = "IRD_REPRO_REPORT_DIR")
{
    const char* v = std::getenv(envName);
    if (v == nullptr || *v == '\0') { return std::nullopt; }
    return std::filesystem::path(v);
}

/**
 * @brief 把报告序列化为单行 JSON（键序＝装配序——确定性输出，NFR-COR-02）。
 *
 * 序列化经 JsonLite（TK-T02——受限 JSON 唯一入口；键序＝插入序）。
 * 数值以 double 原样写入（dumpJson 的数值格式化确定性——同报告同文本）。
 */
inline std::string reproReportToJson(const ReproCaseReport& r)
{
    JsonValue o;
    o.kind = JsonValue::Kind::Object;

    // ---- 标识与确定性上下文（复现要素齐全——协议 §4.8 报告字段）----
    o.members.push_back({"schemaVersion", jsonStr("ird-repro-report/1")});
    o.members.push_back({"caseId", jsonStr(r.caseId)});
    o.members.push_back({"domain", jsonStr(r.domain)});
    o.members.push_back({"inputProvenance", jsonStr(r.inputProvenance)});
    JsonValue seed;
    seed.kind = JsonValue::Kind::Number;
    seed.number.setUnsigned(r.repro.seed);
    o.members.push_back({"seed", std::move(seed)});
    o.members.push_back({"threadCount", jsonNum(static_cast<double>(r.repro.threadCount))});
    o.members.push_back({"softwareVersion", jsonStr(r.repro.softwareVersion)});
    o.members.push_back({"gitCommit", jsonStr(r.repro.gitCommit)});
    o.members.push_back({"toleranceProfile", jsonStr(r.toleranceProfile)});

    // ---- 逐腿测量（原始样本序列——不得只报统计值）----
    JsonValue legsArr;
    legsArr.kind = JsonValue::Kind::Array;
    for (const ReproLegMeasurement& leg : r.legs) {
        JsonValue l;
        l.kind = JsonValue::Kind::Object;
        l.members.push_back({"legId", jsonStr(leg.legId)});
        l.members.push_back({"threadCount", jsonNum(static_cast<double>(leg.threadCount))});
        l.members.push_back({"durationMs", jsonNum(leg.durationMs)});
        l.members.push_back({"elementCount", jsonNum(static_cast<double>(leg.elementCount))});
        legsArr.items.push_back(std::move(l));
    }
    o.members.push_back({"legs", std::move(legsArr)});

    // ---- 偏差观测（真实测量值——非判定阈值）----
    o.members.push_back({"maxAbsDeviation", jsonNum(r.maxAbsDeviation)});
    o.members.push_back({"maxRelativeDeviation", jsonNum(r.maxRelativeDeviation)});
    o.members.push_back({"comparedValueCount", jsonNum(static_cast<double>(r.comparedValueCount))});

    // ---- 逐腿判定 ----
    JsonValue verdictsArr;
    verdictsArr.kind = JsonValue::Kind::Array;
    for (const ReproLegVerdict& v : r.verdicts) {
        JsonValue jv;
        jv.kind = JsonValue::Kind::Object;
        jv.members.push_back({"legId", jsonStr(v.legId)});
        jv.members.push_back({"setEquivalent", jsonBool(v.setEquivalent)});
        jv.members.push_back({"sameOrder", jsonBool(v.sameOrder)});
        jv.members.push_back({"missingExpected", jsonNum(static_cast<double>(v.missingExpected))});
        jv.members.push_back({"extraActual", jsonNum(static_cast<double>(v.extraActual))});
        jv.members.push_back({"duplicates", jsonNum(static_cast<double>(v.duplicates))});
        jv.members.push_back({"ambiguousMatch", jsonBool(v.ambiguousMatch)});
        jv.members.push_back({"mismatchedValueCount", jsonNum(static_cast<double>(v.mismatchedValueCount))});
        verdictsArr.items.push_back(std::move(jv));
    }
    o.members.push_back({"verdicts", std::move(verdictsArr)});

    // ---- 总判定与违例原因 ----
    o.members.push_back({"passed", jsonBool(r.passed)});
    JsonValue violArr;
    violArr.kind = JsonValue::Kind::Array;
    for (const std::string& v : r.violations) {
        violArr.items.push_back(jsonStr(v));
    }
    o.members.push_back({"violations", std::move(violArr)});

    return dumpJson(o, false);
}

/**
 * @brief 把报告写入 <dir>/<caseId>.json（目录按需创建）。
 *
 * @param dir    [in] 输出目录（不存在则递归创建——创建/写失败均不抛）
 * @param report [in] 待写报告
 * @return 写入成功 true；目录创建失败/打开失败/写失败 false（拖挂不阻断
 *         ——调用方自行决定是否记失败；判定不受影响）
 */
inline bool writeReproReportJson(const std::filesystem::path& dir,
                                 const ReproCaseReport& report)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) { return false; }
    std::ofstream out(dir / (report.caseId + ".json"), std::ios::binary | std::ios::trunc);
    if (!out) { return false; }
    const std::string text = reproReportToJson(report);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// =====================================================================
// 复现用例执行（三腿顺序——判定委托 SetCheck，测量逐腿留痕）
// =====================================================================

/// 用例规格（执行前装配——全部值语义）。
struct ReproCaseSpec {
    std::string caseId;           ///< 用例标识（报告文件名主体）
    std::string domain;           ///< 域 token（报告分组）
    std::string inputProvenance;  ///< 输入来源登记（夹具/生成器标识＋关键参数）
    ReproRecord repro;            ///< 确定性上下文（seed/threadCount——被测计算
                                  ///  经自身注入点消费，本夹具不改写任何全局态）
    /// 线程等价腿的线程数（并行归约容差面）：0＝不启用；与 repro.threadCount
    /// 相等＝与复跑腿重合（等价于不启用）。典型值：参考腿 1 线程、本值 8。
    std::uint32_t parallelThreadCount = 0;
};

/**
 * @brief 执行一次确定性复现用例（三腿顺序＋两两对照）。
 *
 * 执行序（每腿独立计时——steady_clock 墙钟实测）：
 *   1. reference 腿：run(repro.threadCount)——对照基线；
 *   2. rerun 腿：run(repro.threadCount)——同输入/版本/配置/线程/种子复跑
 *      （NFR-COR-02"同一"面——集合等价＋稳定排序判定）；
 *   3. threads 腿（可选）：run(parallelThreadCount)——仅线程数不同的并行
 *      归约对照（附录 D 第 8 项容差——数值经 profile 条目，本夹具零私设）。
 * 每条非 reference 腿与 reference 做两件判定：checkSetEquivalent（两阶段
 * 匹配——TK-T06）＋checkStableOrder（按位稳定序）；匹配对逐元素累计偏差
 * 观测（|a−b| 与 |a−b|/|ref|——|ref|=0 的字段不进相对统计，零参考退化面
 * 由绝对分量承载——附录 D C4）。
 *
 * @tparam T     元素类型（消费方域值投影）
 * @tparam RunFn 可调用对象：std::uint32_t threadCount → std::vector<T>
 *               （被测计算入口——种子/配置由闭包按 ReproRecord 注入；同
 *               spec 多次调用须产出可复现结果——这正是被测性质本身）
 * @param spec    [in] 用例规格
 * @param profile [in] 容差档案（集合/顺序断言的数值唯一来源）
 * @param traits  [in] 匹配 traits（身份键＋数值字段投影——业务适配归域）
 * @param run     [in] 被测计算入口（见上；不并发调用——三腿顺序执行）
 * @return 报告（测量＋判定；passed＝全部腿等价且同序；异常语义＝run/profile
 *         自身契约（TestKitError 等原样传播——本夹具不吞错）
 *
 * 复杂度：O(腿数 × (run 代价 + n² 集合匹配))——B-08 抽样规模（百级元素）
 * 下 SetCheck Kuhn 匹配可行（testkit.md §5.4 护栏 100,000）。
 */
template <class T, class RunFn>
ReproCaseReport runReproCase(const ReproCaseSpec& spec,
                             const ToleranceProfile& profile,
                             const SetMatchTraits<T>& traits,
                             RunFn&& run)
{
    ReproCaseReport r;
    r.caseId = spec.caseId;
    r.domain = spec.domain;
    r.inputProvenance = spec.inputProvenance;
    r.repro = spec.repro;
    r.toleranceProfile = profile.profileId + "@" + profile.version;

    // ---- 参考腿（对照基线——判定组的 expected 侧）----
    ReproLegMeasurement refLeg;
    refLeg.legId = "reference";
    refLeg.threadCount = spec.repro.threadCount;
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<T> reference = run(spec.repro.threadCount);
    const auto t1 = std::chrono::steady_clock::now();
    refLeg.durationMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    refLeg.elementCount = reference.size();
    r.legs.push_back(refLeg);

    // ---- 逐对照腿（rerun＋threads——每腿：测量、集合/顺序判定、偏差观测）----
    // 腿表装配：复跑腿恒在；线程腿按 spec.parallelThreadCount 有效性追加
    // （0 或与参考线程数相同＝与复跑腿语义重合，不重复跑）。
    std::vector<std::pair<std::string, std::uint32_t>> legs;
    legs.push_back({"rerun", spec.repro.threadCount});
    if (spec.parallelThreadCount != 0
        && spec.parallelThreadCount != spec.repro.threadCount) {
        legs.push_back({"threads-" + std::to_string(spec.parallelThreadCount),
                        spec.parallelThreadCount});
    }

    r.passed = true;
    for (const auto& [legId, threadCount] : legs) {
        // 腿执行与计时（真实墙钟——原始样本留痕）。
        ReproLegMeasurement m;
        m.legId = legId;
        m.threadCount = threadCount;
        const auto ta = std::chrono::steady_clock::now();
        const std::vector<T> actual = run(threadCount);
        const auto tb = std::chrono::steady_clock::now();
        m.durationMs = std::chrono::duration<double, std::milli>(tb - ta).count();
        m.elementCount = actual.size();
        r.legs.push_back(m);

        // 集合等价＋稳定排序（判定全委托 SetCheck——本夹具零自设匹配规则）。
        const SetCheckResult set = checkSetEquivalent(reference, actual, profile, traits);
        const OrderCheckResult order = checkStableOrder(reference, actual, traits, profile);

        ReproLegVerdict v;
        v.legId = legId;
        v.setEquivalent = set.equivalent;
        v.sameOrder = order.sameOrder;
        v.missingExpected = set.missingExpected.size();
        v.extraActual = set.extraActual.size();
        v.duplicates = set.duplicates.size();
        v.ambiguousMatch = set.ambiguousMatch;
        v.mismatchedValueCount = set.mismatchedPairs.size();
        r.verdicts.push_back(v);

        // 违例定位（可读原因——首因不全报，逐类一条）。
        if (!set.equivalent) {
            if (!set.duplicates.empty()) {
                r.violations.push_back(legId + ": actual 身份键重复（"
                                       + std::to_string(set.duplicates.size()) + " 键）");
            } else {
                r.violations.push_back(
                    legId + ": 集合不等价（缺失 " + std::to_string(set.missingExpected.size())
                    + "／多余 " + std::to_string(set.extraActual.size()) + "／数值超容差对 "
                    + std::to_string(set.mismatchedPairs.size()) + "）");
            }
        }
        if (!order.sameOrder) {
            r.violations.push_back(legId + ": 稳定排序不一致（首个错位 "
                                   + std::to_string(order.firstDivergence) + "）");
        }

        // 偏差观测（匹配对逐元素——测量面，不参与判定）。
        for (const MatchPair& p : set.matched) {
            const auto ef = traits.numerics(reference[p.expectedIndex]);
            const auto af = traits.numerics(actual[p.actualIndex]);
            if (ef.size() != af.size()) { continue; }   // 字段面不一致——判定已兜底
            for (std::size_t k = 0; k < ef.size(); ++k) {
                const double diff = af[k].valueSi - ef[k].valueSi;
                const double absDiff = diff < 0 ? -diff : diff;
                if (absDiff > r.maxAbsDeviation) { r.maxAbsDeviation = absDiff; }
                const double refAbs = ef[k].valueSi < 0 ? -ef[k].valueSi : ef[k].valueSi;
                if (refAbs > 0.0) {
                    const double rel = absDiff / refAbs;
                    if (rel > r.maxRelativeDeviation) { r.maxRelativeDeviation = rel; }
                }
                ++r.comparedValueCount;
            }
        }

        // 判定汇总（等价∧同序才保持通过——歧义不判失败，§5.4.4 原文）。
        if (!set.equivalent || !order.sameOrder) { r.passed = false; }
    }

    return r;
}

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_REPROBENCHMARK_HPP
