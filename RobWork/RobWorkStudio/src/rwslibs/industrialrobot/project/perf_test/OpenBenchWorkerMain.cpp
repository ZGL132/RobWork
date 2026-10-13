/**
 * @file   OpenBenchWorkerMain.cpp
 * @brief  `sdurws_ird_project_openbench_worker`——黄金项目打开基准的冷启动
 *         采样子进程（WP-23-T05；performance-baseline §2 B-04／需求 PM-16）。
 *
 * 设计依据：
 *   - REQUIREMENTS §17 PM-16 原文："冷启动重置＝采样前重启进程"——本进程
 *     即该重置语义的产品承载：父测量目标（sdurws_ird_project_perf_test）
 *     每个冷启动样本经 testkit TestProcessRunner 启动一个**全新**的本进程
 *     （进程启动＋DLL 加载＋RobWork/ird 符号绑定全部重置——与已运行进程的
 *     热启动组形成协议要求的冷/热分组），子进程内执行一次完整的项目打开
 *     并把打开耗时写入报告文件后正常退出；
 *   - doc/industrial-robot-design/performance-baseline.md §4.2（冷启动组
 *     定义）、§4.4（冷启动软指标 P95 ≤8 s 不作门禁——本进程只测量与上报，
 *     判定归父进程，且冷启动超限不阻断）；
 *   - 先例：execution/worker/main.cpp（WP-08-T03——独立 worker 可执行目标
 *     同款构建形态：测试设施 exe 直链产品库，零 testkit 链接〔子进程报告
 *     为固定字段单行 JSON，手写序列化——不引入 testkit 依赖〕）。
 *
 * 与父进程的契约（本文件即协议文本）：
 *   - 命令行：`<本程序> <项目目录> <报告文件路径>`（恰 3 个 argv，否则
 *     退出码 2）；
 *   - 执行序：ProjectStoreFactory::open（Writable——完整打开协议②③⑤，
 *     与热启动组同一打开路径）→HEAD 首读→规模与健康核对→requestClose→
 *     报告落盘→退出码 0；
 *   - 报告文件＝单行 JSON（固定字段序，ird-openbench/1）——父进程以
 *     "openMs" 字段为测量值、退出码 0＋报告可解析为样本有效性判据；
 *   - 退出码：0＝成功；2＝参数错误；3＝打开异常；4＝打开后核对失败
 *     （规模/健康——黄金项目损坏类缺陷）；5＝报告写失败。
 *
 * 线程模型：单线程顺序执行（打开→核对→关闭）；无控制台输出契约之外的
 * 诊断通道（stderr 仅失败定位打点——父进程留痕）。
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <sdurws/ird/project/ProjectStore.hpp>  // ProjectStoreFactory（打开唯一入口）
#include <sdurws/ird/project/QueryPort.hpp>     // IProjectQueryPort（HEAD 首读）
#include <sdurws/ird/project/StoreTypes.hpp>    // OpenStoreRequest/OpenMode/IDiagnosticsSink

// 命名空间别名（可读性——本文件全局层使用 sdurws::ird::project／
// sdurws::ird::core 面的类型；core::DiagnosticRecord 经 StoreTypes 的
// include 链 DiagData.hpp 携带）。
namespace project = sdurws::ird::project;
namespace core = sdurws::ird::core;

namespace {

/// 黄金项目修订历史期望总数（101＝100 数据修订＋1 骨架 r0——PM-16 原文
/// 规模"100 修订"按生成器产出的数据修订计数，createNew 初始骨架修订为
/// 机制面；打开完成＝闭包内修订清单全部装载入会话索引、branchHistory 可
/// 查全量）。与父测量目标及 b04 manifest 同源常量——三处任一漂移即核对
/// 失败显性暴露。
constexpr std::uint64_t kGoldenRevisionTotal = 101;

/**
 * @brief 丢弃型诊断 sink（§5.0 IDiagnosticsSink 测试面实现）。
 *
 * 冷启动测量只关心打开耗时与数据完整性——诊断的**观察面**归父进程的热
 * 启动组（捕获型 sink 断言零诊断）；本进程把 sink 注入为丢弃实现，打开
 * 协议的 sink 上报行为照常发生（不改变被测路径），只是本进程不保存。
 */
class NullDiagnosticsSink final : public project::IDiagnosticsSink {
public:
    void report(const core::DiagnosticRecord& /*record*/) override {}
    void reportDev(const std::string& /*channel*/,
                   const std::string& /*message*/) override {}
};

/// 单行 JSON 数值转义安全检查（报告字段全为程序生成的数字/布尔——无
/// 需转义字符；字符串字段缺失即本进程协议违约，此处只写数值面）。
void appendField(std::string& json, const char* key, double value,
                 bool first)
{
    if (!first) {
        json += ",";
    }
    json += "\"";
    json += key;
    json += "\":";
    // %.3f 固定三位小数（毫秒级耗时——父进程解析方同口径；NaN/Inf 不可达
    // ：steady_clock 差值恒有限）。
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    json += buf;
}

void appendField(std::string& json, const char* key, std::uint64_t value,
                 bool first)
{
    if (!first) {
        json += ",";
    }
    json += "\"";
    json += key;
    json += "\":";
    json += std::to_string(value);
}

void appendField(std::string& json, const char* key, bool value, bool first)
{
    if (!first) {
        json += ",";
    }
    json += "\"";
    json += key;
    json += "\":";
    json += value ? "true" : "false";
}

}  // namespace

/**
 * @brief 进程入口：打开黄金项目一次→核对→关闭→报告落盘。
 *
 * @return 0 成功；2/3/4/5 见文件头退出码表（父进程按退出码判样本有效性）。
 */
int main(int argc, char** argv)
{
    // ---- 参数检查（恰 3 个 argv——父进程装配错误即 2，不静默猜测）----
    if (argc != 3) {
        std::fprintf(stderr,
                     "ird-openbench-worker: 用法 <项目目录> <报告文件>\n");
        return 2;
    }
    const std::string projectDirText = argv[1];
    const std::string reportFileText = argv[2];

    // ---- 打开请求（Writable＝完整打开协议——锁获取＋恢复扫描，与热启动
    // 组同一打开路径；测量口径 manifest 冻结面）----
    project::OpenStoreRequest request;
    request.path = std::filesystem::path(projectDirText);
    request.mode = project::OpenMode::Writable;
    NullDiagnosticsSink sink;
    request.diagnostics = &sink;

    // ---- 计时区间：open() 调用＋HEAD 首读（与热启动组同窗口——"自操作
    // 发起至项目数据可读"的模型层等价；后续核对/关闭在区间外）----
    const auto t0 = std::chrono::steady_clock::now();
    project::OpenStoreResult opened;
    project::RevisionView head;
    try {
        opened = project::ProjectStoreFactory::open(request);
        head = opened.store->query().head();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ird-openbench-worker: 打开失败：%s\n", e.what());
        return 3;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double openMs
        = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // ---- 打开后核对（区间外——黄金项目健康性判据，失败＝数据/协议缺陷
    // 而非性能结果；父进程按退出码 4 记无效样本并留痕）。规模判据＝修订
    // 历史装载完整（101＝100 数据修订＋1 骨架 r0——打开把闭包内修订清单
    // 全部装载入会话索引后 branchHistory 才可查全量；HEAD 视图引用集的
    // 实现口径观测随报告登记，不作规模判据）----
    const bool healthy = opened.writable
                      && opened.recovery.headIntegrityVerified
                      && opened.recovery.diagnostics.empty();
    std::uint64_t historyCount = 0;
    if (healthy) {
        const std::vector<project::RevisionView> history
            = opened.store->query().branchHistory(head.branch, 10000);
        historyCount = history.size();
    }
    if (!healthy || historyCount != kGoldenRevisionTotal) {
        std::fprintf(stderr,
                     "ird-openbench-worker: 打开后核对失败（writable=%d "
                     "integrity=%d diagnostics=%zu history=%llu expect %llu）\n",
                     opened.writable ? 1 : 0,
                     opened.recovery.headIntegrityVerified ? 1 : 0,
                     opened.recovery.diagnostics.size(),
                     static_cast<unsigned long long>(historyCount),
                     static_cast<unsigned long long>(kGoldenRevisionTotal));
        return 4;
    }

    // ---- 关闭排空（测量区间外——打开协议的收尾半区；排空失败同样计
    // 核对失败：锁未释放会污染后续样本）----
    const std::uint32_t pending = opened.store->requestClose();
    const bool closeOk = opened.store->closed();
    if (!closeOk) {
        std::fprintf(stderr,
                     "ird-openbench-worker: 关闭排空未完成（pending=%u）\n",
                     pending);
        return 4;
    }
    opened.store.reset();

    // ---- 报告落盘（单行 JSON 固定字段序——ird-openbench/1；写失败＝5，
    // 父进程记无效样本——测量区间虽完整但结果不可观测，不得计入分母）----
    std::string json = "{\"schemaVersion\":\"ird-openbench/1\"";
    appendField(json, "openMs", openMs, false);
    appendField(json, "historyCount", historyCount, false);
    appendField(json, "headRefCount",
                static_cast<std::uint64_t>(head.objectRefs.size()), false);
    appendField(json, "writable", opened.writable, false);
    appendField(json, "closeOk", closeOk, false);
    json += "}\n";
    {
        std::ofstream out(reportFileText, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "ird-openbench-worker: 报告文件无法打开：%s\n",
                         reportFileText.c_str());
            return 5;
        }
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
        out.flush();
        if (!out) {
            std::fprintf(stderr, "ird-openbench-worker: 报告写入失败：%s\n",
                         reportFileText.c_str());
            return 5;
        }
    }
    return 0;
}
