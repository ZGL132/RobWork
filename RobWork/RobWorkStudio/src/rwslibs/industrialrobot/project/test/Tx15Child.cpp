/**
 * @file   Tx15Child.cpp
 * @brief  PRJ-T15 真进程契约用例的子进程模式实现（PRJ-TX-4/F8 崩溃注入、
 *         PRJ-TX-7 写锁竞争、PRJ-TX-8 在途归档强杀——AT-11/13 自动化载体）。
 *
 * 设计依据：
 *   - units/project.md §7.6 F8 行（TestProcessRunner kill 于 T1～T7 每步后；
 *     预期＝重启后已提交字节不变、未提交忽略＋恢复诊断）、§11 PRJ-TX-7 行
 *     （第二实例只读／持锁进程卡顿不接管／崩溃后并发接管仅一胜者）、
 *     §11 PRJ-TX-8 行（在途归档——进程强杀后无 manifest＝不完整，D-13）、
 *     §5.6（归档端口 begin/writeBatch）、§7.1（七步提交协议——停靠边界
 *     的语义锚点）；
 *   - units/testkit.md §6.4（FaultInterceptor 经生产窄接口 IFileOps 接缝
 *     ——D-10 形态：生产代码不含 testkit 头，fake 编译进测试目标）、§6.5
 *     （TestProcessRunner/EventWatch 消费纪律——正确性判据只来自可观察
 *     事件，本文件的标记文件即父进程 EventWatch 的事件源）。
 *
 * 分派表（mode token → 入口；未知 token 返回 9——分派表违约显性失败）：
 *   crash-commit      七步提交在指定边界停靠（IRD_TX15_PARK 选边界）——
 *                     F8 的"被杀现场制造者"；持锁停靠（父进程重启走接管）
 *   archive-inflight  begin＋writeBatch 后停靠（不 finalize）——PRJ-TX-8
 *                     真进程半边；标记文件携带 run 规范文本
 *   lock-hold         可写打开后持锁停靠——PRJ-TX-7 的"持锁进程"
 *   lock-contend      尝试可写打开、结果写标记后正常退出——PRJ-TX-7 的
 *                     "第二实例/竞争者"（结果判据＝标记文件内容）
 *
 * 停靠（park）语义：到达边界后先写标记文件（flush＋close——父进程
 * EventWatch.awaitFile 的可观察事件），再永久睡眠等待被杀（父进程
 * TestProcessRunner 的 Job Object 兜底终止；若父进程漏杀，ProcessSpec.
 * timeout 到点同样回收——子进程永不自杀，"被杀"必须是父进程的注入动作）。
 *
 * 错误语义：本文件运行于 gtest 之外（main 拦截先于 InitGoogleTest）——
 * 失败一律以非零退出码表达（父进程断言 outcome.kind==Exited 且 exitCode
 * ==0 才视为子进程自洽；非 0＝子进程内失败，父进程用例失败并携带退出码
 * 定位）。StoreFactory 抛出的 StoreError 不捕获——进程以异常终止退出
 * （非零），父进程的 awaitFile 超时路径会显性失败，不留假阳性通道。
 */

#include "Tx15Child.hpp"

#include "Codec.hpp"
#include "ObjectStore.hpp"
#include "RevisionIndex.hpp"
#include "TxEngine.hpp"
#include "win32/AtomicFile.hpp"
#include "win32/IFileOps.hpp"

#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include <sdurws/ird/testkit/Fault.hpp>

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::ReferenceEventBus;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;
using sdurws::ird::project::ArchiveRequest;
using sdurws::ird::project::ArchiveBatch;
using sdurws::ird::project::ArchiveItem;
using sdurws::ird::project::ArchiveSessionRef;
using sdurws::ird::project::HeadRecord;
using sdurws::ird::project::ObjectRefPair;
using sdurws::ird::project::OpenStoreRequest;
using sdurws::ird::project::OpenStoreResult;
using sdurws::ird::project::ProjectStore;
using sdurws::ird::project::ProjectStoreFactory;
using sdurws::ird::project::RevisionManifest;
using sdurws::ird::project::codec::parseHeadRecord;
using sdurws::ird::project::codec::parseRevisionManifest;
using sdurws::ird::project::objstore::ObjectStore;
using sdurws::ird::project::revindex::RevisionIndex;
using sdurws::ird::project::revindex::TipUpdate;
using sdurws::ird::project::tx::CommitPlan;
using sdurws::ird::project::tx::PlannedObject;
using sdurws::ird::project::tx::TxEngine;
using sdurws::ird::project::win32::FileResult;
using sdurws::ird::project::win32::IFileOps;
using sdurws::ird::project::win32::Win32FileOps;
namespace fp = sdurws::ird::project::win32::faultpoint;
namespace tk = sdurws::ird::testkit;

namespace sdurws::ird::project::tx15 {
namespace {

// =====================================================================
// 环境变量与标记文件辅助（参数传递纪律见 Tx15Child.hpp 文件头）
// =====================================================================

/// 读宽字符环境变量（缺失/空＝空串——调用方判空后以非零退出码失败）。
std::wstring envWide(const wchar_t* name)
{
    const DWORD need = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (need == 0) {
        return {};
    }
    std::wstring value(static_cast<std::size_t>(need), L'\0');
    ::GetEnvironmentVariableW(name, value.data(), need);
    value.resize(static_cast<std::size_t>(need) - 1);  // 去掉结尾 NUL
    return value;
}

/// 写标记文件（父进程 EventWatch 的事件源）：原子性不追求——标记内容
/// 单行短文本，轮询方以"文件存在且含期望行"为判据（awaitLineInFile 语义）。
void writeMarker(const fs::path& marker, const std::string& content)
{
    std::ofstream out(marker, std::ios::binary | std::ios::trunc);
    out << content;
    out.flush();
}

/// 永久停靠：等待父进程 TestProcessRunner::kill()（Job Object 终止进程
/// 树——进程被杀时本循环不返回；Sleep 是可中断等待的最简形态，句柄零占用）。
void parkForever()
{
    for (;;) {
        ::Sleep(60000);
    }
}

/// 整读文本文件（子进程装载 HEAD/清单用——磁盘事实由本进程直读，
/// 不经被测通道，前置不依赖被测正确性）。
std::string readAll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

// =====================================================================
// F8 停靠文件操作 fake——testkit FaultInterceptor 经 IFileOps 接缝
// =====================================================================

/**
 * @brief 停靠型 IFileOps fake（testkit.md D-10 形态：生产窄接口、测试侧
 *        fake，装饰真实 Win32FileOps）。
 *
 * 与普通故障注入 fake 的差别：命中计划点时**不注入失败**，而是写停靠
 * 标记后永久停靠——"注入"的动作是进程崩溃本身（由父进程 kill 执行），
 * 本 fake 只负责把事务停在 §7.6 F8 要求的步骤边界上。命中裁决复用
 * testkit FaultInterceptor（FaultPlan{faultPoint, occurrence=1}）——
 * 接缝消费与 §7.6 表头"testkit FaultInterceptor 经 IFileOps 接缝"同源，
 * 故障点 ID 一字不差取自 IFileOps.hpp 的 faultpoint 权威登记。
 *
 * post 语义（T5 边界专用）：提交点（HEAD 原子替换）成功**之后**停靠——
 * 真进程"于 T5 后被杀"的现场（已提交＋清理未跑）。pre 语义：转发**之前**
 * 停靠——下一动作永不发生（T1/T2T3/T4/T6 边界）。
 */
class ParkFileOps final : public IFileOps {
public:
    /// 停靠点：故障点 ID＋pre/post 形态（post＝转发成功后停靠）。
    struct ParkPoint {
        const char* faultPoint;
        bool post;
    };

    ParkFileOps(std::shared_ptr<IFileOps> real, ParkPoint point,
                fs::path marker)
        : m_fault(std::move(real), planFor(point.faultPoint)),
          m_point(point), m_marker(std::move(marker))
    {
    }

    // ---- IFileOps：停靠候选四方法走 step（命中即停靠）；其余直转发 ----

    FileResult openWriteThrough(const std::wstring& path,
                                HANDLE* handle) override
    {
        return step(fp::kOpen,
                    [&] { return m_fault.real()->openWriteThrough(path, handle); });
    }

    FileResult writeChunk(HANDLE handle, const char* data,
                          std::size_t length) override
    {
        return m_fault.real()->writeChunk(handle, data, length);
    }

    FileResult flush(HANDLE handle) override
    {
        return m_fault.real()->flush(handle);
    }

    FileResult closeHandle(HANDLE handle) override
    {
        return m_fault.real()->closeHandle(handle);
    }

    FileResult publishNew(const std::wstring& tempPath,
                          const std::wstring& targetPath) override
    {
        return step(fp::kPublishNew,
                    [&] { return m_fault.real()->publishNew(tempPath, targetPath); });
    }

    FileResult replaceExisting(const std::wstring& tempPath,
                               const std::wstring& targetPath) override
    {
        return step(fp::kReplaceExisting,
                    [&] { return m_fault.real()->replaceExisting(tempPath, targetPath); });
    }

    FileResult createDirectories(const std::wstring& path) override
    {
        return m_fault.real()->createDirectories(path);
    }

    FileResult removeTree(const std::wstring& path) override
    {
        return step(fp::kRemoveTree,
                    [&] { return m_fault.real()->removeTree(path); });
    }

private:
    /// 单触发计划（occurrence=1＝该故障点首次命中即停靠——子进程内每事务
    /// 恰一次提交，无多义性）。
    static tk::FaultPlan planFor(const char* faultPoint)
    {
        tk::FaultPlan plan;
        plan.triggers.push_back(tk::FaultTrigger{faultPoint, 1});
        return plan;
    }

    /// 停靠裁决＋转发（pre＝转发前停靠；post＝转发成功后停靠）。
    template <class Fn>
    FileResult step(const char* faultPoint, Fn&& forward)
    {
        // 命中序号＝本故障点的第 N 次调用（子进程内单线程递增）。
        const std::uint64_t occurrence = ++m_counts[faultPoint];
        const bool hit = m_fault.shouldFire(faultPoint, occurrence);
        if (hit && !m_point.post) {
            parkAndNeverReturn();  // pre 停靠：下一动作（事务第 N+1 步首动作）永不发生
        }
        const FileResult result = forward();
        if (hit && m_point.post && result.ok) {
            parkAndNeverReturn();  // post 停靠：本动作（提交点）已成功落盘
        }
        return result;
    }

    /// 停靠：标记落盘（父进程 awaitFile 的判据）→永久等待被杀。
    [[noreturn]] void parkAndNeverReturn()
    {
        writeMarker(m_marker, "parked=1\n");
        parkForever();
    }

    tk::FaultInterceptor<IFileOps> m_fault;  ///< testkit 拦截器（计划＋日志）
    ParkPoint m_point;                       ///< 停靠点配置
    fs::path m_marker;                       ///< 停靠标记文件路径
    std::map<const char*, std::uint64_t> m_counts;  ///< 各故障点命中序号
};

// =====================================================================
// 模式：crash-commit——F8 的"被杀现场制造者"
// =====================================================================

/**
 * @brief 七步提交停靠子进程：打开健康项目→装配平行事务栈→按
 *        IRD_TX15_PARK 指定边界停靠。
 *
 * 为什么 store 与 TxEngine 并存：store（可写打开）只为真实持有写锁句柄
 * （被杀后父进程重启走失权接管路径——与生产崩溃现场同构）；提交动作由
 * 直驱的 TxEngine 执行（其 IFileOps 接缝可停靠——store 内部件的接缝不可
 * 从公共 API 注入）。两者无写并发：store 打开后不再做任何写操作。
 *
 * 停靠边界（IRD_TX15_PARK token → §7.1 步骤边界）：
 *   T1   kOpen 第 1 次（第 2 步首动作前）＝暂存目录已建、零文件写入
 *   T2T3 kPublishNew 第 1 次（第 4 步首动作前）＝暂存写＋验证完成
 *        （第 3 步纯读无接缝——与 TxEngineTest F8_AfterStage 同口径合并）
 *   T4   kReplaceExisting 第 1 次（第 5 步提交点前）＝对象/清单全发布
 *   T5   kReplaceExisting 第 1 次成功后（提交点后）＝已提交＋清理未跑
 *   T6   kRemoveTree 第 1 次（第 7 步清理前）＝事件已发布（第 6 步完成）
 */
int runCrashCommitChild()
{
    const fs::path dir(envWide(L"IRD_TX15_DIR"));
    const fs::path marker(envWide(L"IRD_TX15_MARKER"));
    // 宽→窄（停靠 token 为 ASCII——单次调用取值后再构造，禁止双临时
    // 迭代器跨容器（UB——曾因此 token 比对失配走 9 号分派违约出口）。
    const std::wstring parkWide = envWide(L"IRD_TX15_PARK");
    const std::string park(parkWide.begin(), parkWide.end());
    if (dir.empty() || marker.empty() || park.empty()) {
        return 8;  // 装配违约（父进程环境变量缺失）——显性失败
    }

    // 停靠点解析（token→faultpoint＋pre/post——分派表违约返回 9）。
    ParkFileOps::ParkPoint point{fp::kOpen, false};
    if (park == "T1") {
        point = {fp::kOpen, false};
    } else if (park == "T2T3") {
        point = {fp::kPublishNew, false};
    } else if (park == "T4") {
        point = {fp::kReplaceExisting, false};
    } else if (park == "T5") {
        point = {fp::kReplaceExisting, true};
    } else if (park == "T6") {
        point = {fp::kRemoveTree, false};
    } else {
        return 9;
    }

    // 第一步：可写打开父进程预建的健康项目（r0）——真实持有写锁。
    OpenStoreRequest request;
    request.path = dir;
    OpenStoreResult opened = ProjectStoreFactory::open(request);
    if (!opened.store->writable()) {
        return 3;  // 环境违约：项目应无人持有（父进程已关闭）
    }
    ProjectStore& store = *opened.store;
    const sdurws::ird::project::ProjectMetadataView meta
        = store.query().currentMetadata();

    // 第二步：装配平行事务栈（TxEngineTest 同型——部件经构造注入）。
    auto objects = std::make_unique<ObjectStore>(
        dir / "objects", dir / ".staging",
        ObjectStore::kDefaultCacheBudgetBytes, nullptr);
    auto index = std::make_unique<RevisionIndex>();
    // 磁盘直读装载会话索引（前置不依赖被测通道）：HEAD＋r0 清单。
    const HeadRecord head = parseHeadRecord(readAll(dir / "HEAD"));
    const RevisionManifest r0Manifest = parseRevisionManifest(readAll(
        dir / "revisions" / head.revisionId.toCanonical() / "manifest.json"));
    index->registerRevision(r0Manifest);
    index->registerMetadata(meta.ref, meta.record);
    index->adoptHeadSeq(head.revisionSeq);

    // 第三步：停靠 fake（testkit FaultInterceptor 消费点）＋引擎＋总线
    // （总线非空＝第 6 步事件发布真实执行——T6 边界才有意义）。
    auto realOps = std::make_shared<Win32FileOps>();
    ParkFileOps ops(realOps, point, marker);
    ReferenceEventBus bus;
    TxEngine engine(&ops, dir, objects.get(), index.get(), &bus, nullptr);

    // 第四步：构造第二修订提交计划（TxEngineTest::makePlan 同型——
    // 分支＝主分支、父＝当前 tip、单一域对象、tip 推进声明）。
    CommitPlan plan;
    plan.revisionId = RevisionId::generate();
    plan.branchId = meta.record.primaryBranchId;
    plan.parentRevisionId = head.revisionId;
    plan.committedAtUtc = "2026-09-17T00:00:00Z";
    PlannedObject obj;
    obj.oid = sdurws::ird::core::ObjectId::generate();
    obj.objectTypeToken = "RobotDesign";
    obj.payload.assign(64, 'X');  // 域负载（canonical 字节代餐）
    plan.newObjects.push_back(std::move(obj));
    TipUpdate tip;
    tip.branchId = meta.record.primaryBranchId;
    tip.newTip = plan.revisionId;
    plan.metadataDelta.tipUpdate = tip;
    plan.command.commandType = "crash-boundary-probe";
    plan.command.payloadFormatVersion = 1;
    plan.command.payloadCanonical = "{\"probe\":1}";
    plan.command.summary = "PRJ-T15 F8 crash boundary child";

    // 第五步：执行提交——停靠点处写出标记并被父进程强杀（正常路径不可
    // 达；若无停靠点命中，commit 完成＝子进程自洽性自证，返回 0）。
    engine.commit(plan, head, meta.record, meta.ref);
    (void)store;  // store 保活至进程终局（写锁句柄随进程消亡——真实现场）
    return 0;
}

// =====================================================================
// 模式：archive-inflight——PRJ-TX-8 真进程半边
// =====================================================================

/**
 * @brief 在途归档停靠子进程：可写打开→begin＋writeBatch（不 finalize）→
 *        标记携带 run 规范文本→停靠等待被杀。
 *
 * 被杀现场＝results/<run-id>/ 已有批次文件但无 manifest.json——D-13
 * "manifest 在＝运行完整"判据的反例面（父进程重启后断言 listRuns 不列）。
 */
int runArchiveInflightChild()
{
    const fs::path dir(envWide(L"IRD_TX15_DIR"));
    const fs::path marker(envWide(L"IRD_TX15_MARKER"));
    if (dir.empty() || marker.empty()) {
        return 8;
    }

    OpenStoreRequest request;
    request.path = dir;
    OpenStoreResult opened = ProjectStoreFactory::open(request);
    if (!opened.store->writable()) {
        return 3;
    }
    ProjectStore& store = *opened.store;

    // 任务五元组：项目/分支/修订取自当前存储事实（head 修订——合法归档
    // 绑定），run 身份本进程生成（登记信息透传口径——§5.6）。
    TaskIdentity task;
    task.project = store.projectId();
    task.branch = store.query().currentMetadata().record.primaryBranchId;
    task.revision = store.query().head().id;
    task.run = RunId::generate();
    task.attempt = AttemptId{1};

    ArchiveRequest archiveRequest;
    archiveRequest.task = task;
    archiveRequest.runDir = dir / "results" / task.run.toCanonical();
    archiveRequest.runKind = "kinematics-eval";
    archiveRequest.evaluationKey = "eval-key-1";

    // begin（在途票据持有）＋一批次写入——finalize 故意不调（强杀现场）。
    ArchiveSessionRef session = store.archive().begin(archiveRequest);
    if (!static_cast<bool>(session)) {
        return 4;  // begin 拒绝＝前置违约（可写上下文应受理）
    }
    ArchiveItem item;
    item.relPath = "inflight-result.json";
    const std::string bytes = "in-flight archive payload";
    item.bytes.assign(bytes.begin(), bytes.end());
    const sdurws::ird::project::ArchiveStatus written
        = store.archive().writeBatch(session, ArchiveBatch{{item}});
    if (!written.ok) {
        return 5;  // 批次写失败＝环境违约（正常磁盘不应失败）
    }

    // 停靠：标记携带 run 身份（父进程据此定位 results/<run> 目录断言
    // "批次在、manifest 不在"）。
    writeMarker(marker,
                "parked=1\nrun=" + task.run.toCanonical() + "\n");
    parkForever();
}

// =====================================================================
// 模式：lock-hold / lock-contend——PRJ-TX-7 的两个角色
// =====================================================================

/**
 * @brief 持锁停靠子进程：可写打开成功→标记（pid＋writable=1）→持锁停靠。
 *        模拟"持锁进程存活（心跳线程照常跳动）"的现场——卡顿不接管
 *        （D-03）的正例载体：进程活着，锁就不可抢。
 */
int runLockHoldChild()
{
    const fs::path dir(envWide(L"IRD_TX15_DIR"));
    const fs::path marker(envWide(L"IRD_TX15_MARKER"));
    if (dir.empty() || marker.empty()) {
        return 8;
    }
    OpenStoreRequest request;
    request.path = dir;
    OpenStoreResult opened = ProjectStoreFactory::open(request);
    if (!opened.store->writable()) {
        writeMarker(marker, "writable=0\n");  // 环境违约也要可观测
        return 3;
    }
    writeMarker(marker, "pid="
                            + std::to_string(::GetCurrentProcessId())
                            + "\nwritable=1\n");
    // store 保活（锁句柄不释放）直至被杀——析构不执行＝崩溃现场的锁残留。
    (void)opened.store;
    parkForever();
}

/**
 * @brief 竞争者子进程：尝试可写打开→把结果（自身 pid／是否取得写权限/
 *        观察到的持有者 pid）写入标记→按结果分流：**胜利者持锁停靠**
 *        （等待被杀——保证并发竞争窗口真实重叠：两个竞争者的获取尝试
 *        必须在时间上交叠，"仅一胜者"才是内核原子裁决的观测，否则先到
 *        者退出释放锁会让后到者照常获取、出现双"writable=1"的假象）；
 *        **失败者正常退出**（退出码 0——降级只读是合法结果，非错误）。
 *
 * 结果判据＝标记文件内容（父进程 EventWatch.awaitLineInFile 等待期望行
 * ——testkit §6.5"正确性判据只来自可观察事件"）。打开失败（环境违约）
 * 以非零退出码显性失败。
 */
int runLockContendChild()
{
    const fs::path dir(envWide(L"IRD_TX15_DIR"));
    const fs::path marker(envWide(L"IRD_TX15_MARKER"));
    if (dir.empty() || marker.empty()) {
        return 8;
    }
    OpenStoreRequest request;
    request.path = dir;
    OpenStoreResult opened = ProjectStoreFactory::open(request);
    ProjectStore& store = *opened.store;  // 打开失败走异常→非零退出
    const sdurws::ird::project::LockInfo info = store.lockInfo();
    writeMarker(marker,
                "pid=" + std::to_string(::GetCurrentProcessId())
                    + "\nwritable=" + (store.writable() ? "1" : "0")
                    + "\nholder=" + std::to_string(info.holder.pid) + "\n");
    if (store.writable()) {
        // 胜利者：持锁停靠（锁句柄保活至被杀——重叠窗口的载体）。
        parkForever();
    }
    return 0;  // 失败者（降级只读）：析构释放资源后正常退出
}

}  // namespace

// =====================================================================
// 分派入口（main 拦截 --ird-wp04-t15-child 后调用——Tx15Child.hpp 契约）
// =====================================================================

int runChild(const char* mode)
{
    const std::string m = mode;
    if (m == "crash-commit") { return runCrashCommitChild(); }
    if (m == "archive-inflight") { return runArchiveInflightChild(); }
    if (m == "lock-hold") { return runLockHoldChild(); }
    if (m == "lock-contend") { return runLockContendChild(); }
    return 9;  // 未知模式——分派表违约（LockContractTest 同口径）
}

}  // namespace sdurws::ird::project::tx15
