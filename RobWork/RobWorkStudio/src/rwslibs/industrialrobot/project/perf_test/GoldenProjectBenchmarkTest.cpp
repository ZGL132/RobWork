/**
 * @file   GoldenProjectBenchmarkTest.cpp
 * @brief  黄金项目（1,000 对象/100 修订）打开热启动基准（performance-baseline
 *         §2 B-04／需求 PM-16）——真实产品命令管线生成的黄金项目在
 *         ProjectStoreFactory 完整打开协议上的冷/热分组 P95 测量（WP-23-T05）。
 *
 * 设计依据：
 *   - REQUIREMENTS §17 PM-16 原文：黄金项目（1,000 对象/100 修订）热启动
 *     打开 P95 ≤ 3 s；固定硬件；冷/热启动分组，每组有效样本 ≥15 次（合计
 *     ≥30），剔除各组首次预热；冷启动重置＝采样前重启进程；P95 采用线性
 *     插值分位数；冷启动组单独报告、超限登记原因与改进项（不阻断验收）；
 *     3 次中位数仅作快速回归参考，不作为 P95 达标证据（禁 3 次中位数——
 *     F-03）；冷启动软指标 P95 ≤ 8 s 为参考目标（M-14，v1.8，不作门禁）；
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-04 行（验收
 *     门禁仅限热启动组——V13 §4 收窄）、§3（黄金项目定义——1,000 对象/
 *     100 修订零增减；生成器明细参数由本卡登记并冻结进数据集 manifest）、
 *     §4.1~§4.4（硬件固定/冷热分组与样本量/项目打开分组口径）、§4.8（报告
 *     留痕——原始样本序列不得只报统计值）、§5.4（本卡挂点动作）；
 *   - 任务契约 tasks/foundation/WP-23-T05.json（≙DTB §2.24 行——禁止项
 *     "冷启动不作门禁"以用例断言面结构承载：冷启动组断言有效性与零无效，
 *     P95 软指标只登记不判定）；
 *   - units/project.md §5.1（ProjectStoreFactory 打开协议——本基准的被测
 *     产品面）、units/testkit.md §4.2.1（performance-baseline 数据集类别）
 *     ＋§10.1（计时统计/进程夹具——PerfBenchmark/ProcessRunner 直接复用，
 *     本卡零新增 testkit 头）。
 *
 * 黄金项目生成器（明细构成＝生成器参数，冻结于 testdata/performance/
 * b04-golden-project-manifest.json——performance-baseline §3.2 委托）：
 *   真实产品命令管线：createNew（PM-01 存储侧组装）→ handlerRegistry()
 *   公共装配通道注册基准处理器（§6.5——UI-T39 公共通道，非私有头）→
 *   100 个数据修订（每修订 1 个 golden-append-batch 命令声明写 10 个
 *   "GoldenData" 对象）→ requestClose 排空释放写锁。对象负载＝2,048 字节
 *   确定性文本（序号派生——可复现）。规模恒等式：100 修订×10 对象＝
 *   1,000 数据对象（PM-16 两数值零偏移）；项目内另有 createNew 初始骨架
 *   修订 r0（机制面，非生成器参数）——修订总数 101 如实登记于 manifest
 *   与报告（"100 修订"＝生成器产出的数据修订计数）。
 *
 * 测量口径（协议 §4.2/§4.4——manifest measurementProtocol 同源）：
 *   - 热启动组（门禁）：进程已运行、黄金项目磁盘数据已就绪（夹具预开一
 *     次——OS 文件缓存与代码路径就绪）的状态下采样；测量区间＝open() 调
 *     用＋HEAD 首读（"自操作发起至项目数据可读"的模型层等价——打开后的
 *     规模/健康核对与关闭排空全部在区间外）；1 次预热剔除＋15 有效样本；
 *     P95（线性插值，testkit 唯一实现点）≤ 3,000 ms 判门禁（阈值源＝
 *     REQUIREMENTS PM-16 原文——零私设）。
 *   - 冷启动组（软指标，不作门禁）：每样本经 testkit TestProcessRunner 启
 *     动全新子进程（sdurws_ird_project_openbench_worker——进程启动＋DLL
 *     加载＋符号绑定全部重置＝PM-16"冷启动重置"），子进程执行同一打开
 *     路径并上报耗时；1 次预热剔除＋15 有效样本；P95 对照 8,000 ms 软指
 *     标**只登记不判定**（超限登记原因与改进项——首测报告）。
 *   - 样本有效性：运行完整结束且测量区间完整方为有效（子进程退出码 0＋
 *     报告可解析／open 无异常且核对通过）；无效样本不计入分母并留痕。
 *
 * 无头等价口径（报告 notes 与首测报告同步登记——如实申报边界）：
 *   ①"项目打开"＝ProjectStoreFactory::open 完整协议（②目录形态与版本＋
 *     ③装载读校验〔HEAD 闭包完整性 SHA-256〕＋⑤孤儿草稿扫描＋Writable
 *     锁获取与恢复扫描）＋HEAD 首读；UI 会话激活/项目树呈现/模型编译面
 *     在口径外（PM-16 测量的是项目加载，PM-16 与 NFR-PERF-01 为不同测试
 *     ——需求原文括注）。
 *   ②冷启动＝全新子进程承载进程启动＋DLL 加载＋RobWork/ird 符号绑定；
 *     RobWorkStudio 插件加载与 Qt 资源解压面在无头口径外（插件宿主进程
 *     未启动）——软指标不作门禁的边界一致性如实申报（协议 §4.4：冷启动
 *     受进程启动、插件加载与框架初始化支配）。
 *   ③黄金项目经真实产品命令管线（submit→七步提交协议→只增发布→HEAD
 *     切换）生成，打开测量消费的是产品持久化真磁盘布局——非预录数据。
 *
 * 判定面（零私设数值——阈值全部照抄需求原文）：
 *   热启动用例：①生成器规模恒等式（100 修订/1,000 数据对象——修订历史
 *   101＝100 数据修订＋1 骨架 r0 的地面事实核对＋对象库磁盘计数打点）；
 *   ②每组 1 预热＋15 有效＋零无效样本；③修订历史装载完整（打开后
 *   branchHistory 可查全量＝闭包内修订清单全部装载入会话索引）＋打开零
 *   恢复诊断＋写权限取得；④P95 ≤ 3,000 ms（门禁——PM-16 原文）。冷启动
 *   用例：①②③同上（子进程内核对，退出码承载）；④P95 软指标**只登记**
 *   ——断言面不含 P95 阈值（DTB 行禁止项"冷启动不作门禁"的结构承载）。
 *
 * 实现口径观测（如实登记，不作规模判据）：当前产品实现的修订清单
 * objectRefs＝元数据＋本修订计划对象（TxEngine §4.4.2 装配口径），HEAD
 * 视图引用数＝11（10 数据＋1 元数据）——与 §4.4.2"全量状态闭包"设计语义
 * 的关系归 product 侧澄清，本基准不私裁；打开的完整性校验面＝闭包全部
 * 修订清单引用并集（~1,111 对象 SHA-256——真实打开成本，实测见报告）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>       // core::DiagnosticRecord（恢复诊断断言面）
#include <sdurws/ird/project/CommandService.hpp>  // ICommandHandler/CommandEnvelope/submit 面
#include <sdurws/ird/project/ProjectStore.hpp>    // ProjectStoreFactory（被测打开协议）
#include <sdurws/ird/project/QueryPort.hpp>       // IProjectQueryPort（HEAD/branchHistory 核对）
#include <sdurws/ird/project/StoreTypes.hpp>      // OpenStoreRequest/OpenMode/IDiagnosticsSink
#include <sdurws/ird/testkit/Fixture.hpp>         // TempDir（并行隔离临时目录）
#include <sdurws/ird/testkit/PerfBenchmark.hpp>   // percentileLinearInterpolated/报告拖挂
#include <sdurws/ird/testkit/ProcessRunner.hpp>   // TestProcessRunner（冷启动全新进程）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO（追溯登记）

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using testkit::InteractionGroupReport;
using testkit::InteractionSample;
using testkit::PerfSessionReport;

// =====================================================================
// 数据集冻结参数（datasetId＝ird-perf-b04-golden-project，version 1——
// manifest 权威文件 testdata/performance/b04-golden-project-manifest.json；
// 本处常量必须与 manifest 逐字一致，报告输出参数供漂移核对）
// =====================================================================

/// 黄金项目数据对象数（REQUIREMENTS PM-16 原文：1,000 对象——零增减）。
constexpr std::size_t kGoldenDataObjectCount = 1000;
/// 黄金项目数据修订数（REQUIREMENTS PM-16 原文：100 修订——零增减；
/// 计数口径＝生成器产出的数据修订；createNew 初始骨架修订 r0 为机制面，
/// 项目修订总数 101 如实登记 manifest）。
constexpr std::size_t kGoldenRevisionCount = 100;
/// 每修订写入的数据对象数（manifest 冻结生成器参数：100×10＝1,000 恒等）。
constexpr std::size_t kObjectsPerRevision = 10;
/// 单个数据对象负载字节数（manifest 冻结生成器参数：确定性文本——工业
/// 对象 JSON 载荷的典型量级；1,000×2 KiB≈2 MiB 对象库）。
constexpr std::size_t kObjectPayloadBytes = 2048;
/// 对象类型 token（基准域自有 token——project 不解释负载，§6.4 D-10）。
constexpr const char* kGoldenObjectTypeToken = "GoldenData";
/// 基准处理器命令 token（§4.4.4 冻结语法 ^[a-z0-9-]{3,64}——无点）。
constexpr const char* kGoldenCommandType = "golden-append-batch";
/// 热启动门禁阈值，单位 ms（REQUIREMENTS PM-16 原文：热启动打开 P95
/// ≤ 3 s——零私设；消费方以需求原文传入——PerfBenchmark 纪律）。
constexpr double kHotThresholdMs = 3000.0;
/// 冷启动软指标阈值，单位 ms（REQUIREMENTS PM-16 M-14 v1.8：P95 ≤ 8 s
/// 参考目标——不作门禁，超限登记原因与改进项）。
constexpr double kColdThresholdMs = 8000.0;
/// 每组预热次数（performance-baseline §4.2：剔除各组首次预热）。
constexpr std::size_t kWarmupRuns = 1;
/// 每组有效样本下限（performance-baseline §4.2：每组有效样本 ≥15 次；
/// 冷/热两组合计 30 ≥ 30 恰好满足需求原文合计下限）。
constexpr std::size_t kMinValidSamples = 15;

/// 冷启动子进程单样本超时上限，单位 ms（夹具安全界——子进程挂起兜底回
/// 收；黄金项目打开的合理耗时远低于此界，超时＝环境异常记无效样本）。
constexpr std::chrono::milliseconds kColdSampleTimeout{120000};

// =====================================================================
// 黄金项目生成器（真实产品命令管线——fixture 一次生成、用例内共享）
// =====================================================================

/**
 * @brief 基准域处理器（"golden-append-batch" v1）——一命令写一批黄金数据
 *        对象。
 *
 * 载荷约定（基准域自有语义——project 不解释负载，D-10 透传）：载荷＝
 * kObjectsPerRevision 行文本（'\n' 分隔），每行一个对象负载（行内格式
 * "obj:<全局序号>:<填充字符>…"，行长恒 kObjectPayloadBytes 字节）。prepare
 * 逐行展开为 ObjectWrite（objectTypeToken＝kGoldenObjectTypeToken）——
 * 每修订恒定 10 个对象，规模恒等式由载荷构造侧保证、处理器侧断言。
 *
 * 线程安全：无状态（prepare 只读入参）——命令槽串行调用（§6.1）。
 */
class GoldenAppendHandler final : public project::ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return kGoldenCommandType;
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    project::PrepareOutcome prepare(project::HandlerContext& /*ctx*/,
                                    const project::CommandEnvelope& envelope,
                                    const project::RevisionView& /*baseSnapshot*/,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>& diags) override
    {
        // 载荷按行拆分（基准域约定：每行一个对象负载——行数恒 10，
        // 违约即夹具缺陷 fail-fast，不产出半批计划）。
        const std::string text(envelope.payloadCanonical.begin(),
                               envelope.payloadCanonical.end());
        std::size_t begin = 0;
        std::size_t written = 0;
        while (begin < text.size()) {
            const std::size_t end = text.find('\n', begin);
            const std::size_t lineEnd
                = (end == std::string::npos) ? text.size() : end;
            if (lineEnd > begin) {
                project::ObjectWrite write;
                write.objectTypeToken = kGoldenObjectTypeToken;
                write.payloadCanonical.assign(text.begin() + static_cast<std::ptrdiff_t>(begin),
                                              text.begin() + static_cast<std::ptrdiff_t>(lineEnd));
                out.objectWrites.push_back(std::move(write));
                ++written;
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }
        if (written != kObjectsPerRevision) {
            // 载荷行数违约＝基准夹具缺陷（生成器与处理器契约漂移）——
            // 硬断言拒绝（§6.3 行 3——就地阻止，不产出错误规模的修订）。
            diags.push_back(core::DiagnosticRecord::make(
                std::string{"GOLDEN-BATCH-PAYLOAD-MISMATCH"}, std::nullopt,
                std::nullopt, std::nullopt,
                "golden: 载荷行数与每修订对象数不符（夹具缺陷）",
                "lines=" + std::to_string(written)
                    + " expect=" + std::to_string(kObjectsPerRevision),
                "golden: 修正生成器载荷构造"));
            return project::PrepareOutcome::RejectedHardAssert;
        }
        out.summary = "golden batch append (10 objects)";
        return project::PrepareOutcome::Planned;
    }
};

/**
 * @brief 一次批载荷构造（10 行×2,048 字节确定性文本——全局对象序号派生）。
 *
 * 行格式："obj:<序号>:" ＋ 填充字符至行满 2,048 字节；填充字符＝'A'+(序号
 * %26)（确定性——同序号同内容，可复现；负载内容不影响打开成本的面＝字
 * 节量与 SHA-256 校验，确定性文本保证跨轮次可比）。
 */
std::vector<std::uint8_t> buildBatchPayload(std::size_t firstObjectIndex)
{
    std::string payload;
    payload.reserve(kObjectsPerRevision * (kObjectPayloadBytes + 1));
    for (std::size_t i = 0; i < kObjectsPerRevision; ++i) {
        const std::size_t objectIndex = firstObjectIndex + i;
        std::string line = "obj:" + std::to_string(objectIndex) + ":";
        const char fill = static_cast<char>('A' + (objectIndex % 26));
        line.resize(kObjectPayloadBytes, fill);   // 填充至恒定行长
        payload.append(line);
        payload.push_back('\n');                  // 行分隔（处理器拆分面）
    }
    return std::vector<std::uint8_t>(payload.begin(), payload.end());
}

/// 捕获型诊断 sink（打开零恢复诊断断言面——黄金项目健康性判据之一）。
struct CapturingSink final : public project::IDiagnosticsSink {
    std::vector<core::DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;
    void report(const core::DiagnosticRecord& record) override
    {
        reports.push_back(record);
    }
    void reportDev(const std::string& channel, const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }
};

/**
 * @brief 黄金项目夹具：真实产品命令管线生成 1,000 对象/100 修订的磁盘项
 *        目，生成后关闭排空（写锁释放——打开采样/子进程都重新拿锁）。
 *
 * 生命周期：TempDir RAII（析构递归删除——keepOnFailure 默认保留失败现场
 * 供排查）；生成失败以 ok==false＋error 文本承载（用例 ASSERT 后短路）。
 * 非线程安全：单用例线程内串行使用。
 */
class GoldenProjectFixture {
public:
    testkit::TempDir dir{std::string("wp23-t05-golden")};
    ///< 黄金项目 .rwdesign 目录（TempDir 内——子进程可访问的绝对路径）。
    std::filesystem::path projectDir;
    bool ok = false;
    std::string error;

    GoldenProjectFixture()
    {
        projectDir = dir.path() / "golden.rwdesign";
        build();
    }

private:
    /// 生成序（真实产品命令管线——五步，任何一步失败即 ok=false 短路）。
    void build()
    {
        try {
            // ---- 第 1 步：createNew（PM-01 存储侧——初始骨架修订 r0＋
            // 权威元数据 M0；创建者即首个写权限持有者）----
            project::OpenStoreResult created
                = project::ProjectStoreFactory::createNew(
                    projectDir, "wp23-t05 golden project");
            if (created.store == nullptr || !created.writable) {
                error = "createNew 未取得可写上下文";
                return;
            }

            // ---- 第 2 步：注册基准处理器（§6.5 装配期——handlerRegistry()
            // 公共通道，UI-T39；非私有头——测试面合法装配）----
            created.store->handlerRegistry().registerHandler(
                std::make_unique<GoldenAppendHandler>());

            // ---- 第 3 步：主分支身份（信封 branch 字段来源）----
            const core::BranchId branch
                = created.store->query().currentMetadata().record.primaryBranchId;

            // ---- 第 4 步：100 个数据修订（每修订 1 命令×10 对象——任何
            // 提交非 Committed 即生成失败，不产出错误规模的项目）----
            for (std::size_t rev = 0; rev < kGoldenRevisionCount; ++rev) {
                project::CommandEnvelope envelope;
                envelope.branch = branch;
                envelope.commandType = kGoldenCommandType;
                envelope.payloadFormatVersion = 1;
                envelope.payloadCanonical
                    = buildBatchPayload(rev * kObjectsPerRevision);
                const project::CommandResult result
                    = created.store->commands().submit(envelope);
                if (!result.committed()) {
                    error = "第 " + std::to_string(rev + 1)
                            + " 个修订提交未达成 Committed";
                    return;
                }
            }

            // ---- 第 5 步：磁盘地面事实核对（规模恒等式——生成器规模即
            // 需求规模的自证；修订历史 101＝100 数据修订＋1 骨架 r0）----
            const project::RevisionView head = created.store->query().head();
            const std::vector<project::RevisionView> history
                = created.store->query().branchHistory(branch, 10000);
            // 生成面磁盘事实打点（stderr 留痕——headRefs＝HEAD 视图引用数
            // 〔实现口径观测，随首测报告登记〕；oidDirs＝对象库一级 oid 目
            // 录数＝1,000 数据对象＋101 元数据对象 oid〔每修订元数据为新
            // oid——TxEngine 装配口径，与数据对象合计 1,101〕）。
            {
                std::size_t oidDirs = 0;
                std::error_code ec;
                for (auto it = std::filesystem::directory_iterator(
                         projectDir / "objects", ec);
                     it != std::filesystem::directory_iterator(); it.increment(ec)) {
                    if (!ec) { ++oidDirs; }
                }
                std::fprintf(stderr,
                             "[b04-golden] disk-facts headRefs=%zu history=%zu"
                             " oidDirs=%zu\n",
                             head.objectRefs.size(), history.size(), oidDirs);
                std::fflush(stderr);
            }
            if (history.size() != kGoldenRevisionCount + 1) {
                error = "生成规模核对失败（revisions="
                        + std::to_string(history.size()) + "）";
                return;
            }

            // ---- 第 6 步：关闭排空（写锁释放——打开采样与冷启动子进程
            // 都需要重新获取锁；排空未完成＝夹具污染，生成失败）----
            created.store->requestClose();
            if (!created.store->closed()) {
                error = "生成后关闭排空未完成";
                return;
            }
            created.store.reset();
            ok = true;
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
};

// =====================================================================
// 采样与报告辅助（目标内采样执行器——T03 CancelTimingRig 先例同款形态；
// P95 一律经 testkit percentileLinearInterpolated 唯一实现点）
// =====================================================================

/// 环境事实读取（§4.1 登记清单的环境变量可得面——缺失如实留空）。
std::string envOr(const char* name, const char* fallback = "")
{
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

/// 原始样本记录（目标内采样执行器的留痕面——三态全量，协议 §4.2）。
struct OpenSample {
    double openMs = 0.0;      ///< 本样本测量区间时长，单位 ms（steady_clock 实测）
    bool excluded = false;    ///< true＝预热样本（不计入统计）
    bool valid = true;        ///< false＝无效样本（不计入分母）
    std::string invalidReason; ///< 无效原因（valid==false 时非空——留痕登记）
};

/**
 * @brief 由原始样本序列组装分组报告（T03 makeGroupReport 同款——统计面
 *        经 testkit 单点函数；调用方保证 valid∧!excluded 非空）。
 */
InteractionGroupReport makeGroupReport(const std::string& opId,
                                       const std::string& stateId,
                                       const std::vector<OpenSample>& samples)
{
    InteractionGroupReport g;
    g.opId = opId;
    g.stateId = stateId;
    g.samples.reserve(samples.size());
    std::vector<double> validMs;
    for (const OpenSample& s : samples) {
        InteractionSample e;
        e.durationMs = s.openMs;
        e.excluded = s.excluded;
        e.valid = s.valid;
        e.invalidReason = s.invalidReason;
        g.samples.push_back(std::move(e));
        if (s.valid && !s.excluded) {
            ++g.validCount;
            validMs.push_back(s.openMs);
        } else if (!s.valid) {
            ++g.invalidCount;
        } else {
            ++g.warmupRuns;
        }
    }
    if (!validMs.empty()) {
        g.p95Ms = testkit::percentileLinearInterpolated(validMs, 0.95);
        g.maxValidMs = *std::max_element(validMs.begin(), validMs.end());
        g.minValidMs = *std::min_element(validMs.begin(), validMs.end());
    }
    return g;
}

/// 组装会话报告头（标识/版本/数据集/环境——测量组由调用方追加）。
PerfSessionReport makeSessionHeader(const std::string& caseId)
{
    PerfSessionReport r;
    r.caseId = caseId;
    r.baselineDoc = "performance-baseline #2 B-04";
    // 软件版本锁定（§4.1）：commit 由执行方经 IRD_PERF_GIT_COMMIT 注入
    // （缺省如实留空——commit SHA 在首测报告文档登记）。
    r.gitCommit = envOr("IRD_PERF_GIT_COMMIT");
    r.buildConfig = "Release";
    r.datasetId = "ird-perf-b04-golden-project";
    r.datasetVersion = "1";
    r.machineName = envOr("COMPUTERNAME");
    r.cpuInfo = envOr("PROCESSOR_IDENTIFIER")
                + " x" + envOr("NUMBER_OF_PROCESSORS", "?");
    r.osInfo = envOr("OS", "Windows") + " " + envOr("PROCESSOR_ARCHITECTURE");
    // 内存/存储/电源：环境变量不可得——如实指向首测报告的硬件清单
    // （traceability/benchmarks/wp23-t05-golden-project-baseline.md §2）。
    r.memoryInfo = "见首测报告硬件清单（manifest 同源）";
    r.storageInfo = "见首测报告硬件清单";
    r.powerPolicyNote = "见首测报告硬件清单";
    r.backgroundNote = "空闲态（无后台评估任务——B-04 为项目打开分组基准，"
                       "无两态负载面）";
    return r;
}

/// 单组阈值判定记录（thresholdMs/thresholdSource 由消费方按需求原文传入
/// ——本文件两常量照抄 PM-16 原文；measuredP95Ms 回填自测量组）。
testkit::PerfThresholdVerdict makeP95Verdict(const InteractionGroupReport& g,
                                             double thresholdMs,
                                             const char* thresholdSource)
{
    testkit::PerfThresholdVerdict v;
    v.opId = g.opId;
    v.stateId = g.stateId;
    v.thresholdMs = thresholdMs;
    v.thresholdSource = thresholdSource;
    v.measuredP95Ms = g.p95Ms;
    v.pass = g.p95Ms <= thresholdMs;
    return v;
}

/**
 * @brief 解析冷启动子进程报告（ird-openbench/1 单行 JSON——固定字段序）。
 *
 * 只提取测量面两字段（openMs/historyCount——子进程文件头即协议文本）；
 * 解析失败返回 false（父进程记无效样本并留痕——不猜测数值）。
 */
bool parseOpenBenchReport(const std::filesystem::path& file, double& openMs,
                          std::uint64_t& historyCount)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    const std::string openMsKey = "\"openMs\":";
    const std::string historyKey = "\"historyCount\":";
    const auto msPos = text.find(openMsKey);
    const auto histPos = text.find(historyKey);
    if (msPos == std::string::npos || histPos == std::string::npos) {
        return false;
    }
    char* msEnd = nullptr;
    const double ms = std::strtod(text.c_str() + msPos + openMsKey.size(), &msEnd);
    char* histEnd = nullptr;
    const unsigned long long history
        = std::strtoull(text.c_str() + histPos + historyKey.size(), &histEnd, 10);
    if (msEnd == text.c_str() + msPos + openMsKey.size() || histEnd == nullptr
        || histEnd == text.c_str() + histPos + historyKey.size()) {
        return false;
    }
    openMs = ms;
    historyCount = history;
    return true;
}

// =====================================================================
// 热启动组（门禁）——PM-16：热启动打开 P95 ≤ 3 s，每组有效 ≥15
// =====================================================================

/**
 * @brief 热启动采样执行器（进程已运行＋磁盘数据就绪状态下的反复打开）。
 *
 * 每样本执行序（测量区间内只有 open＋HEAD 首读——收尾全部在区间外）：
 *   ①区间起点前：上一样本的上下文已在样本末尾排空析构（无跨样本残留）；
 *   ②区间内：ProjectStoreFactory::open（Writable——锁获取＋恢复扫描＋
 *     HEAD 闭包完整性校验）→ query().head()（首读——项目数据可读）；
 *   ③区间外核对：writable／1,001 引用（1,000 数据＋1 元数据）／零恢复
 *     诊断／requestClose 排空完成——任一失败该样本记无效并留痕。
 * 预热 1 次（excluded 留痕）＋循环至 15 有效；无效安全界 150（夹具缺陷
 * 显性失败——协议 §4.2 补采失控防线，PerfBenchmark 夹具同口径）。
 *
 * @param fixture 黄金项目夹具（已生成且写锁已释放）
 * @param sink    捕获型诊断 sink（区间外核对消费——零恢复诊断断言）
 * @return 原始样本序列（预热＋有效＋无效全量——协议 §4.8 留痕面）
 *
 * @throws std::runtime_error 无效样本超过安全界（夹具缺陷——不静默）
 */
std::vector<OpenSample> runHotStartGroup(GoldenProjectFixture& fixture,
                                         CapturingSink& sink)
{
    std::vector<OpenSample> samples;
    samples.reserve(kWarmupRuns + kMinValidSamples);
    std::size_t validCount = 0;
    std::size_t invalidCount = 0;
    std::size_t executed = 0;   ///< 已执行样本数（预热段判定依据——独立于
                                ///  validCount：预热样本即使有效也不计入
                                ///  有效计数，若以 validCount 判预热段会
                                ///  恒真循环——调测期已证）
    const std::size_t kInvalidCeiling = kMinValidSamples * 10;

    while (validCount < kMinValidSamples) {
        OpenSample s;
        s.excluded = executed < kWarmupRuns;  // 首组预热剔除
        ++executed;
        project::OpenStoreRequest request;
        request.path = fixture.projectDir;
        request.mode = project::OpenMode::Writable;
        request.diagnostics = &sink;
        // ---- 测量区间：open()＋HEAD 首读（口径——manifest 冻结面）----
        const auto t0 = std::chrono::steady_clock::now();
        try {
            project::OpenStoreResult opened
                = project::ProjectStoreFactory::open(request);
            const project::RevisionView head = opened.store->query().head();
            const auto t1 = std::chrono::steady_clock::now();
            s.openMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

            // ---- 区间外核对（失败＝无效样本——留痕不计入分母）----
            if (!opened.writable) {
                s.valid = false;
                s.invalidReason = "打开未取得写权限（Writable 请求被降级）";
            } else if (!opened.recovery.headIntegrityVerified) {
                s.valid = false;
                s.invalidReason = "HEAD 闭包完整性校验未通过";
            } else if (!opened.recovery.diagnostics.empty()) {
                s.valid = false;
                s.invalidReason = "打开产出恢复诊断（黄金项目应零诊断）："
                                  + opened.recovery.diagnostics.front().code;
            } else {
                // 规模核对（打开完成＝100 修订全部装载入会话索引可查——
                // 101＝100 数据修订＋1 骨架 r0；HEAD 视图引用集的实现口径
                // 观测随报告登记，不作规模判据）。
                const std::vector<project::RevisionView> history
                    = opened.store->query().branchHistory(
                        head.branch, 10000);
                if (history.size() != kGoldenRevisionCount + 1) {
                    s.valid = false;
                    s.invalidReason = "修订历史装载不完整："
                                      + std::to_string(history.size());
                } else {
                    // 关闭排空（区间外——失败同样计无效：锁未释放污染下一样本）
                    opened.store->requestClose();
                    if (!opened.store->closed()) {
                        s.valid = false;
                        s.invalidReason = "关闭排空未完成";
                    }
                }
            }
        } catch (const std::exception& e) {
            const auto t1 = std::chrono::steady_clock::now();
            s.openMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            s.valid = false;
            s.invalidReason = std::string("打开异常：") + e.what();
        }
        if (s.valid && !s.excluded) {
            ++validCount;
        } else if (!s.valid) {
            ++invalidCount;
            if (invalidCount > kInvalidCeiling) {
                throw std::runtime_error(
                    "热启动组无效样本超过安全界（invalid="
                    + std::to_string(invalidCount) + "——黄金项目/打开协议"
                    "持续失败属夹具缺陷，最后原因：" + s.invalidReason + "）");
            }
        }
        // 逐样本 stderr 打点（测量过程实时可见——T03 invalid 打点先例；
        // 序号＝excluded 预热位或第 n 个有效/无效样本）。
        std::fprintf(stderr,
                     "[b04-hot] sample (excluded=%d valid=%d): %.3f ms%s\n",
                     s.excluded ? 1 : 0, s.valid ? 1 : 0, s.openMs,
                     s.valid ? "" : (" reason=" + s.invalidReason).c_str());
        std::fflush(stderr);
        samples.push_back(std::move(s));
    }
    return samples;
}

// =====================================================================
// 用例 1——热启动门禁（PM-16/V13 §4：验收门禁仅限热启动组）
// =====================================================================

TEST(GoldenProjectOpen, HotStartP95_Within3s_DT_PM16)
{
    IRD_TEST_INFO("PM-16", {"B-04"}, std::nullopt);

    // ---- 黄金项目生成（真实产品命令管线——规模恒等式在夹具内自证）----
    GoldenProjectFixture fixture;
    ASSERT_TRUE(fixture.ok)
        << "黄金项目生成失败：" << fixture.error;

    // ---- 预打开一次（热启动组前置＝"进程已运行、项目所需资源已就绪"
    // ——协议 §4.2 分组定义；本打开同时暖热 OS 文件缓存与代码路径，
    // 计为采样序之外的组级准备，不进入样本序列）----
    {
        CapturingSink warmupSink;
        project::OpenStoreRequest request;
        request.path = fixture.projectDir;
        request.mode = project::OpenMode::Writable;
        request.diagnostics = &warmupSink;
        project::OpenStoreResult opened
            = project::ProjectStoreFactory::open(request);
        ASSERT_TRUE(opened.writable);
        opened.store->requestClose();
        ASSERT_TRUE(opened.store->closed());
    }

    // ---- 采样（1 预热＋15 有效；无效留痕——断言零无效）----
    CapturingSink sink;
    std::vector<OpenSample> samples;
    ASSERT_NO_FATAL_FAILURE(
        samples = runHotStartGroup(fixture, sink));
    const InteractionGroupReport group
        = makeGroupReport("open-warm", "process-running", samples);

    // 断言②：样本纪律（15 有效＋零无效——黄金项目健康打开不应失败）。
    ASSERT_EQ(group.validCount, kMinValidSamples)
        << "热启动组有效样本不足（协议 §4.2 每组 ≥15）";
    ASSERT_EQ(group.invalidCount, 0u)
        << "热启动组出现无效样本（留痕见 stderr 与报告 samples 序列）";

    // 断言③：打开健康面（捕获 sink 零用户级诊断——恢复诊断在样本内已
    // 逐例核对，此处收口 sink 全程零上报）。
    ASSERT_TRUE(sink.reports.empty())
        << "打开过程产出用户级诊断（黄金项目应零诊断）";

    // 断言④：热启动门禁（阈值 3,000 ms＝REQUIREMENTS PM-16 原文——V13
    // §4 收窄：验收门禁仅限热启动组；P95＝线性插值——夹具唯一实现点）。
    const testkit::PerfThresholdVerdict verdict
        = makeP95Verdict(group, kHotThresholdMs,
                         "REQUIREMENTS PM-16（热启动打开 P95 ≤ 3 s——V13 §4 门禁仅限热启动组）");
    EXPECT_TRUE(verdict.pass)
        << "热启动 P95 超门禁：实测 " << group.p95Ms
        << " ms > 3,000 ms（PM-16 原文口径——原始样本序列见报告）";

    // ---- 报告落盘（IRD_PERF_REPORT_DIR 拖挂；写失败如实断言——证据面
    // 必须在盘，验收要读）----
    testkit::PerfSessionReport session
        = makeSessionHeader("b04-warm-open");
    session.groups.push_back(group);
    session.verdicts.push_back(verdict);
    session.allPassed = verdict.pass;
    session.notes.push_back(
        "无头模型/存储层等价口径：'项目打开'＝ProjectStoreFactory::open"
        " 完整协议（②目录与版本＋③装载读校验〔HEAD 闭包 SHA-256〕＋⑤孤"
        "儿草稿扫描＋Writable 锁获取与恢复扫描）＋HEAD 首读；UI 会话激活/"
        "项目树呈现/模型编译面在口径外（PM-16 与 NFR-PERF-01 为不同测试）");
    session.notes.push_back(
        "测量区间＝open() 调用＋HEAD 首读（'自操作发起至项目数据可读'的"
        "模型层等价）；打开后核对与关闭排空全部在区间外（目标内采样执行器"
        "——T03 CancelTimingRig 先例；P95 经 testkit 唯一实现点线性插值）");
    session.notes.push_back(
        "判定口径＝热启动门禁组（P95 ≤ 3,000 ms，阈值源 REQUIREMENTS "
        "PM-16 原文；每组 1 预热剔除＋15 有效＋零无效——协议 §4.2/§4.4）");
    session.notes.push_back(
        "黄金项目生成器＝真实产品命令管线（createNew→handlerRegistry 公共"
        "通道注册基准处理器→100 数据修订×10 对象＝1,000 数据对象；另有 "
        "createNew 骨架修订 r0＝机制面，项目修订总数 101）；数据集 "
        "manifest＝testdata/performance/b04-golden-project-manifest.json"
        "（ird-perf-b04-golden-project@1——参数冻结，升级换版本）");
    const auto reportDir = testkit::perfReportDirFromEnv();
    if (reportDir.has_value()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*reportDir, session))
            << "基准报告写入失败（IRD_PERF_REPORT_DIR=" << reportDir->string()
            << "）——留痕面缺失";
    }
}

// =====================================================================
// 用例 2——冷启动软指标（PM-16/M-14：P95 ≤ 8 s 参考目标——单独报告，
// 超限登记原因与改进项，不阻断验收；DTB 行禁止项"冷启动不作门禁"）
// =====================================================================

TEST(GoldenProjectOpen, ColdStartP95_SoftTarget_ReportOnly_DT_PM16)
{
    IRD_TEST_INFO("PM-16", {"B-04"}, std::nullopt);

    // ---- 黄金项目生成（本用例独立一份——与热启动组互不共享磁盘状态）----
    GoldenProjectFixture fixture;
    ASSERT_TRUE(fixture.ok)
        << "黄金项目生成失败：" << fixture.error;

    // ---- 子进程可执行路径（构建期注入——不存在＝装配错误 fail-fast）----
    const std::filesystem::path workerExe(IRD_PROJECT_OPENBENCH_WORKER_EXE);
    ASSERT_TRUE(std::filesystem::exists(workerExe))
        << "冷启动采样子进程不存在：" << workerExe.string();

    // ---- 采样（每样本全新子进程——PM-16"冷启动重置＝采样前重启进程"；
    // 1 预热＋15 有效）----
    std::vector<OpenSample> samples;
    samples.reserve(kWarmupRuns + kMinValidSamples);
    std::size_t validCount = 0;
    std::size_t invalidCount = 0;
    std::size_t executed = 0;   ///< 已执行样本数（预热段判定依据——独立于
                                ///  validCount，热启动组同款修正）
    const std::size_t kInvalidCeiling = kMinValidSamples * 10;
    std::size_t sampleSeq = 0;

    while (validCount < kMinValidSamples) {
        OpenSample s;
        s.excluded = executed < kWarmupRuns;
        ++executed;
        // 每样本独立报告文件名（预热与无效样本的文件都留存——不覆盖）。
        const std::filesystem::path reportFile = fixture.dir.path()
            / ("openbench-" + std::to_string(sampleSeq++) + ".json");
        testkit::TestProcessRunner runner;
        testkit::ProcessSpec spec;
        spec.executable = workerExe;
        spec.args.push_back(fixture.projectDir.string());
        spec.args.push_back(reportFile.string());
        spec.timeout = kColdSampleTimeout;
        try {
            // 全新进程启动（CREATE_SUSPENDED→Job Object→Resume——子进程
            // 承载进程启动＋DLL 加载＋符号绑定的重置面）；outcome() 阻塞
            // 至退出/超时（超时先杀树——夹具兜底）。
            runner.start(spec, fixture.dir.path());
            const testkit::ProcessOutcome outcome = runner.outcome();
            // 有效判据：正常退出（0）＋报告可解析（测量区间完整）。
            double openMs = 0.0;
            std::uint64_t historyCount = 0;
            if (outcome.kind == testkit::ProcessExitKind::Exited
                && outcome.exitCode == 0
                && parseOpenBenchReport(reportFile, openMs, historyCount)) {
                s.openMs = openMs;
                if (historyCount != kGoldenRevisionCount + 1) {
                    s.valid = false;
                    s.invalidReason = "子进程报告修订历史数偏离 101："
                                      + std::to_string(historyCount);
                }
            } else if (outcome.kind == testkit::ProcessExitKind::Exited
                       && outcome.exitCode != 0) {
                s.valid = false;
                s.invalidReason = "子进程打开后核对失败（退出码 "
                                  + std::to_string(outcome.exitCode)
                                  + "——2 参数/3 打开异常/4 核对/5 报告写）";
            } else if (outcome.kind
                       == testkit::ProcessExitKind::TerminatedByTimeout) {
                s.valid = false;
                s.invalidReason = "子进程超时被回收（120 s 安全界）";
            } else {
                s.valid = false;
                s.invalidReason = "子进程未正常退出（kind="
                                  + std::to_string(static_cast<int>(outcome.kind))
                                  + "）";
            }
        } catch (const std::exception& e) {
            s.valid = false;
            s.invalidReason = std::string("子进程启动失败：") + e.what();
        }
        if (s.valid && !s.excluded) {
            ++validCount;
        } else if (!s.valid) {
            ++invalidCount;
            std::fprintf(stderr,
                         "[b04-cold] invalid sample (nth=%zu): %s\n",
                         invalidCount, s.invalidReason.c_str());
            std::fflush(stderr);
            if (invalidCount > kInvalidCeiling) {
                FAIL() << "冷启动组无效样本超过安全界（invalid="
                       << invalidCount << "——最后原因：" << s.invalidReason
                       << "）";
                return;
            }
        }
        samples.push_back(std::move(s));
    }

    const InteractionGroupReport group
        = makeGroupReport("open-cold", "fresh-process", samples);

    // 断言②：样本纪律（15 有效＋零无效——两组合计 30 满足需求原文合计
    // 下限；冷启动组**不含 P95 阈值断言**——软指标不作门禁的结构承载）。
    ASSERT_EQ(group.validCount, kMinValidSamples)
        << "冷启动组有效样本不足（协议 §4.2 每组 ≥15）";
    ASSERT_EQ(group.invalidCount, 0u)
        << "冷启动组出现无效样本（留痕见 stderr 与报告 samples 序列）";

    // ---- 软指标对照（只登记不判定——超限登记原因与改进项，不阻断验收；
    // PM-16/M-14 v1.8 原文口径）----
    const testkit::PerfThresholdVerdict verdict
        = makeP95Verdict(group, kColdThresholdMs,
                         "REQUIREMENTS PM-16 冷启动软指标（M-14，v1.8——P95 ≤ 8 s 参考目标，不作门禁）");
    if (!verdict.pass) {
        std::fprintf(stderr,
                     "[b04-cold] SOFT-TARGET MISS（登记不阻断）：P95=%.3f ms"
                     " > 8,000 ms——按 PM-16 登记原因与改进项（首测报告）\n",
                     group.p95Ms);
        std::fflush(stderr);
    }

    // ---- 报告落盘（单独报告面——冷启动组独立 JSON）----
    testkit::PerfSessionReport session
        = makeSessionHeader("b04-cold-open");
    session.groups.push_back(group);
    session.verdicts.push_back(verdict);
    // allPassed 只反映软指标对照结论——**不作门禁**：即便 false，本用例
    // 判定面（样本纪律＋零无效）已独立断言，验收不因软指标超限阻断。
    session.allPassed = verdict.pass;
    session.notes.push_back(
        "冷启动组单独报告（PM-16 原文：冷启动组单独报告、超限登记原因与"
        "改进项〔不阻断验收〕；软指标 P95 ≤ 8 s＝M-14 v1.8 参考目标不作"
        "门禁——DTB WP-23-T05 行禁止项'冷启动不作门禁'的结构承载：本组"
        "断言面＝样本纪律＋零无效，P95 超限只登记）");
    session.notes.push_back(
        "冷启动重置＝每样本全新子进程（sdurws_ird_project_openbench_worker"
        "——CREATE_SUSPENDED→Job Object→Resume；进程启动＋DLL 加载＋符号"
        "绑定全部重置）；子进程执行与热启动组同一打开路径（Writable 完整"
        "协议）并把 open＋HEAD 首读耗时写入 ird-openbench/1 报告文件");
    session.notes.push_back(
        "无头边界如实申报：RobWorkStudio 插件加载与 Qt 资源解压面在本口径"
        "外（插件宿主未启动）——冷启动受进程启动、插件加载与框架初始化支"
        "配（协议 §4.4），插件面缺失使本组软指标读数低于真实 Studio 冷启"
        "动，登记为口径边界而非达标依据");
    session.notes.push_back(
        "数据集 manifest＝testdata/performance/b04-golden-project-manifest"
        ".json（ird-perf-b04-golden-project@1——参数冻结，升级换版本）");
    const auto reportDir = testkit::perfReportDirFromEnv();
    if (reportDir.has_value()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*reportDir, session))
            << "基准报告写入失败（IRD_PERF_REPORT_DIR=" << reportDir->string()
            << "）——留痕面缺失";
    }
}

}  // namespace
