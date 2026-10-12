/**
 * @file   CrashCapture.hpp
 * @brief  应用壳侧崩溃捕获装配（OS 级捕获→CrashReportInput 组装→既有
 *         CrashReportWriter 写用户目录）——WP-24-T02 承接 diagnostics.md
 *         §7.6 冻结的分工面："OS 级崩溃捕获机制与应用壳装配归 WP-24-T02，
 *         Writer 设施归 diagnostics（沿用不重写）"。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.6（崩溃诊断文件六段内容/写出位置＝用户目录
 *     〔不入 .rwdesign——D-10〕/写出失败→stderr 放弃/AT-11 观测点"写出且
 *     脱敏"；分工冻结见该节与 CrashReport.hpp 头注）、§7.7（脱敏规则——
 *     凭据零命中、路径按策略；脱敏在 Writer 侧统一执行，捕获点不做预脱敏）、
 *     §12.1 协作行（"崩溃诊断文件与 OS 级崩溃捕获的完整链路——WP-24-T02
 *     协作"）；ARCHITECTURE §4.1（冷启动路径——应用壳装配；§5.4 用户目录）；
 *   - 需求 PM-17（异常退出写出诊断文件）、AT-11（进程崩溃后重启：诊断文件
 *     写出且脱敏）、NFR-SEC-07（凭据类不记录、路径按配置脱敏）；
 *   - 任务契约 tasks/foundation/WP-24-T02.json（acceptance 1：应用入口最先
 *     安装捕获〔Windows 结构化异常/CRT 终止路径〕，捕获点组装 CrashReportInput
 *     经既有 CrashReportWriter 写用户目录；捕获路径防重入）。
 *
 * 背景说明（为什么捕获设施独立成本文件、且零 Qt）：
 *   崩溃可在任何线程、任何时刻发生——捕获回调本身必须满足"崩溃路径纪律"：
 *   防重入（一次性闸门，防止过滤器/信号处理器递归把报告路径变成二次崩溃）、
 *   noexcept（任何次生异常都被 Writer 的 noexcept 链与本地 catch 消化）、
 *   不做 Qt 调用（Qt 对象在崩溃现场可能已不一致——L2 纪律在捕获路径同样
 *   成立）、不新增复杂分配（分配交给 Writer 既有实现，其 noexcept 契约已
 *   把失败降级为 nullopt）、看门狗自我了断（报告路径若意外挂起——例如磁盘
 *   设备假死——看门狗线程超时终止进程，绝不把用户卡死在崩溃中）。
 *
 *   本文件是 ui 单元私有装配面头（studio/ 目录，不入 include/——R-2），
 *   仅应用壳装配面（StudioMain）与同单元测试目标消费。全部设施为纯
 *   C++＋Win32，零 Qt 头——捕获回调禁 Qt 的结构性保证（头文件层面成立）。
 *
 * 线程约束：
 *   - installCrashCapture / setCrashSnapshotSource：应用主线程（启动序）；
 *   - emitCrashReportTo / 各 OS 回调：任意线程（崩溃线程）——上下文内共享
 *     量均为原子量；闸门保证报告路径全局串行且一次性；
 *   - 看门狗线程：安装时创建，随上下文析构收编（仅测试面构造的上下文会
 *     析构；产品全局上下文有意泄漏至进程终局——见 installCrashCapture 注）。
 */

#ifndef SDURWS_IRD_UI_STUDIO_CRASHCAPTURE_HPP
#define SDURWS_IRD_UI_STUDIO_CRASHCAPTURE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>             // core::ProjectId/TaskIdentity（P-DIAG-1 内嵌——§7.6）
#include <sdurws/ird/diagnostics/CrashReport.hpp>   // diagnostics::CrashReportWriter/CrashReportInput（既有设施——沿用不重写）
#include <sdurws/ird/diagnostics/Catalog.hpp>       // diagnostics::SystemClock（IClock 产品实现）
#include <sdurws/ird/diagnostics/Logging.hpp>       // diagnostics::FileLogFileOps（文件接缝产品实现）
#include <sdurws/ird/diagnostics/Redaction.hpp>     // diagnostics::RedactionService（§7.7 脱敏）

namespace sdurws {
namespace ird {
namespace ui {
namespace studio {

// =====================================================================
// 快照源（§7.6 内容六段中"壳侧运行期才能取到的段"的供给接缝）
// =====================================================================

/**
 * @brief 崩溃现场快照段（六段中除进程信息与异常信息外的四段——由装配方在
 *        崩溃时刻经快照源产出；取不到的段如实留空，不伪造数据）。
 *
 * 值语义；纯载体。字段语义与 diagnostics::CrashReportInput 对应成员一致。
 */
struct CrashContextSegments {
    /// 最近开发日志行快照（通常取 LoggingPipeline::devTailSnapshot——§7.6
    /// 第③段；Writer 侧逐行再脱敏双保险）。
    std::vector<std::string> devLogTail;
    /// 活动任务清单（五元组原样内嵌——P-DIAG-1；§7.6 第④段）。
    std::vector<core::TaskIdentity> activeTasks;
    /// 活动 finding 清单（findingId/状态，不含载荷；§7.6 第⑤段）。
    std::vector<diagnostics::CrashReportActiveFinding> activeFindings;
    /// 打开的项目身份（projectId 规范文本，不含路径原文；§7.6 第⑥段）。
    std::optional<core::ProjectId> openProject;
};

/**
 * @brief 崩溃现场快照源接口（壳侧会话态向捕获设施的单向供给缝）。
 *
 * 为什么是接口而不是直连：诊断管线/任务登记表/finding 服务属于会话装配
 * （插件壳初始化期装配），进程入口（main）装捕获时点它们尚不存在——捕获
 * 设施只认本接口，装配序后段（UiPlugin::initialize）经 setCrashSnapshotSource
 * 把"已装配的会话态"接上来（装配解耦，不产生入口→插件的逆向依赖）。
 *
 * 实现契约：snapshot() 在崩溃线程被调用——必须尽力而为、不得依赖 Qt、
 * 允许内部失败（返回部分/空段）；设施侧已对其整体 try/catch（见
 * emitCrashReportTo），实现方抛异常不会击穿报告路径。
 */
class CrashReportSnapshotSource {
public:
    virtual ~CrashReportSnapshotSource() = default;

    /// @brief 采集崩溃现场快照段（崩溃线程调用——尽力而为，见类注契约）。
    virtual CrashContextSegments snapshot() = 0;
};

// =====================================================================
// 发射状态（捕获点的可观测结果——测试断言面）
// =====================================================================

/// 捕获点发射结果（发射核心 emitCrashReportTo 的返回值；测试与通道共用）。
enum class CrashEmitStatus : std::uint8_t {
    NotInstalled,  ///< 未安装（installCrashCapture 未成功——回调不应到达此态）
    Written,       ///< 诊断文件写出成功（Writer 返回路径）
    WriteFailed,   ///< 写出失败（Writer 已 stderr 降级——§7.6"失败放弃"语义）
    GateClosed,    ///< 防重入闸门已闭合（重入/并发第二次发射被拒——一次性）
};

// =====================================================================
// 捕获上下文（可测核心——OS 回调与单元测试共用的发射主体）
// =====================================================================

/**
 * @brief 捕获上下文（Writer 依赖＋崩溃目录＋快照源＋防重入闸门＋看门狗
 *        事件对）。
 *
 * 生命周期：产品路径由 installCrashCapture 在静态存储期构造并**有意泄漏**
 * （崩溃可在 static 析构之后发生——上下文必须活到进程终局，泄漏是崩溃
 * 设施的标准形态）；测试路径经 makeCrashCaptureContext 构造、析构收编
 * 看门狗线程与句柄。
 *
 * 线程约束：source/gate 为原子量；其余成员在安装后只读（崩溃线程并发读
 * 安全）；看门狗事件句柄仅 Win32 API 触碰（void*——本头不引入 windows.h，
 * 保持公共消费面的干净）。
 */
struct CrashCaptureContext {
    /// 崩溃诊断文件目标目录（用户目录——ARCH §5.4/D-10；由装配方解析）。
    std::filesystem::path directory;
    /// 进程/版本信息（§7.6 第①段；安装期固定——崩溃期无环境查询）。
    std::string processInfo;
    /// 快照源（可空——空＝四段如实留空；原子量，装配序后段可更新）。
    std::atomic<CrashReportSnapshotSource*> source{nullptr};
    /// 防重入闸门（一次性：首次发射置位后永不复位——崩溃后进程应当终结，
    /// 第二次报告没有意义且会递归）。
    std::atomic_flag gate = ATOMIC_FLAG_INIT;
    /// 看门狗布防/撤防事件（auto-reset；0＝未创建——非 Windows 装配面）。
    void* watchdogArm = nullptr;
    void* watchdogDone = nullptr;
    /// 看门狗超时（毫秒；报告路径自我了断上限——见 kCrashReportWatchdog）。
    std::chrono::milliseconds watchdogTimeout{10000};
    /// 看门狗线程收编标记（仅析构路径置位——测试面收编用）。
    std::atomic<bool> shuttingDown{false};
    /// 看门狗线程句柄（void* 同上；join 收编用）。
    void* watchdogThread = nullptr;

    /// 设施本体（安装期构造——Writer 依赖注入形态见各成员头文件契约）。
    diagnostics::SystemClock clock;                    ///< 时钟（文件名/writtenUtc 打点）
    diagnostics::FileLogFileOps fileOps;               ///< 文件接缝（开-写-关）
    std::unique_ptr<diagnostics::RedactionService> redaction;  ///< 脱敏服务（§7.7；failureSink 静默）
    std::unique_ptr<diagnostics::CrashReportWriter> writer;    ///< 写出设施（既有——DIAG-T08）

    /// @brief 析构（收编看门狗线程并关闭事件句柄——定义于 CrashCapture.cpp；
    ///        产品全局上下文不析构，本析构仅测试面路径可达）。
    ~CrashCaptureContext();
};

/**
 * @brief 构造捕获上下文（测试面工厂；产品安装走 installCrashCapture）。
 *
 * @param directory       [in] 崩溃诊断文件目标目录（用户目录/测试临时目录）
 * @param processInfo     [in] 进程/版本信息（§7.6 第①段原文）
 * @param source          [in] 快照源（可空＝四段留空；非拥有，须覆盖上下文
 *                        生命周期）
 * @param watchdogTimeout [in] 看门狗超时（产品默认 kCrashReportWatchdog；
 *                        测试可注入短时限验证自我了断）
 *
 * @return 上下文（永远非空——构造失败经异常表达，调用方 fail-fast；仅测试
 *         装配面调用，无环境错误恢复语义）
 *
 * @throws std::bad_alloc 等资源异常（事件/线程创建失败——测试装配违约）
 */
std::unique_ptr<CrashCaptureContext> makeCrashCaptureContext(
    std::filesystem::path directory, std::string processInfo,
    CrashReportSnapshotSource* source,
    std::chrono::milliseconds watchdogTimeout = std::chrono::milliseconds{10000});

// =====================================================================
// 发射核心（OS 回调的委托体——单元测试直接调用的可测面）
// =====================================================================

/**
 * @brief 执行一次崩溃报告发射（组装 CrashReportInput 六段→Writer 写盘）。
 *
 * 崩溃路径纪律逐条落点：
 *   - 防重入：入口闸门 test_and_set——同进程第二次发射（过滤器重入/递归
 *     崩溃/并发崩溃）直接 GateClosed 返回，不产生第二份文件；
 *   - noexcept：全程无异常逃逸（Writer 自身 noexcept；快照源与组装面的
 *     任何异常由本地 catch 归类为 WriteFailed——崩溃路径绝不抛出）；
 *   - 看门狗：进入即布防（arm 事件），退出撤防（done 事件）——报告路径
 *     挂起超过 watchdogTimeout 时看门狗线程以 kCrashWatchdogSelfExitCode
 *     终止进程（自我了断——绝不把用户卡死在崩溃中）；
 *   - 取不到的段如实留空（快照源为空/抛异常→四段空表），不伪造数据。
 *
 * @param ctx       [in] 捕获上下文（安装/工厂产出）
 * @param type      [in] 异常类型标签（如 "seh-0xC0000005 access-violation"、
 *                  "crt-abort"——崩溃点已知形态的稳定文本；过长截断）
 * @param message   [in] 异常消息原文（可含现场文本——Writer 经 safeSummary
 *                  脱敏＋512 B 截断，捕获点不做预脱敏）
 *
 * @return 发射状态（Written/WriteFailed/GateClosed/NotInstalled）
 *
 * @throws 无（noexcept）
 */
CrashEmitStatus emitCrashReportTo(CrashCaptureContext& ctx,
                                  std::string_view type,
                                  std::string_view message) noexcept;

// =====================================================================
// 产品安装面（应用入口调用）
// =====================================================================

/// 看门狗默认超时（报告路径自我了断上限；10 s 足以覆盖单文件同步写，
/// 又远小于用户可感知的"卡死"阈值——工程默认，契约 knownPitfalls 之外
/// 的本文件口径登记于 WP-24-T02 note）。
inline constexpr std::chrono::milliseconds kCrashReportWatchdog{10000};

/// 看门狗自我了断专用退出码（0xC0DE7001："C0DE"＝诊断位面助记，0x7001
/// 区分于 CRT abort 惯例码 3 与常见异常码 0xC0000005 族——父进程/运维可
/// 据此识别"报告路径挂起被看门狗终止"的形态）。
inline constexpr std::uint32_t kCrashWatchdogSelfExitCode = 0xC0DE7001u;

/**
 * @brief 安装 OS 级崩溃捕获（应用入口最先调用——PM-17/AT-11 链路起点）。
 *
 * 安装面（Windows）：
 *   - SetUnhandledExceptionFilter：结构化异常（访问违例/除零等硬件异常——
 *     产品崩溃主体形态；Windows 原生承接面，信号处置器不与之竞争）；
 *   - signal(SIGABRT)：abort()/raise 家族（CRT 终止路径——SEH 不覆盖的
 *     形态；SIGSEGV/SIGILL/SIGFPE **不登记**——CRT 信号处置器在位时会
 *     把硬件异常翻译成信号回调并抢先于 unhandled filter，丢失异常码/
 *     地址细节，实施期实测口径登记于 WP-24-T02 note）；
 *   - _set_invalid_parameter_handler/_set_purecall_handler：CRT 参数违约
 *     与纯虚调用（release CRT 终止路径——不装则直接静默终结无报告）；
 *   - std::set_terminate：未捕获 C++ 异常（提取 active exception 消息后
 *     报告——比 abort 路径多一层类型/消息保真）；
 *   - SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX)：抑制
 *     WER/硬错误对话框——诊断报告即产品语义的崩溃出口，弹窗会挂起自动化
 *     与无人值守场景（AT-11 的"崩溃后重启"要求进程可确定性终结）。
 *   不装 std::set_new_handler：报告路径自身需要内存，OOM 时报告必然失败
 *   （Writer noexcept 降级）——装 new_handler 只会把报告路径变成分配风暴
 *   源，工程取舍登记于 WP-24-T02 note。
 *
 * 幂等性：重复安装为无操作（返回 false）——捕获是进程级单例语义，二次
 * 安装会覆盖过滤器链（Windows 只保留最后一个 filter），必须拒绝。
 *
 * @param directory       [in] 崩溃诊断文件目录（用户目录——调用方解析，
 *                        本函数不查询进程环境）
 * @param processInfo     [in] 进程/版本信息（§7.6 第①段）
 * @param source          [in] 快照源（可空；后续可经 setCrashSnapshotSource
 *                        更新——装配序后段接线）
 * @param watchdogTimeout [in] 看门狗超时（缺省 kCrashReportWatchdog）
 *
 * @return true＝安装成功；false＝已安装过（幂等拒绝）或平台不支持
 *         （非 Windows——本文件设施的平台面诚实降态）
 *
 * @throws 无（内部失败路径不抛——安装失败意味着报告路径不可用，但绝不
 *         阻断应用启动：启动契约 PM-17 的诊断面是尽力而为）
 */
bool installCrashCapture(std::filesystem::path directory, std::string processInfo,
                         CrashReportSnapshotSource* source = nullptr,
                         std::chrono::milliseconds watchdogTimeout = kCrashReportWatchdog) noexcept;

/**
 * @brief 更新全局上下文的快照源（装配序后段接线缝——UiPlugin::initialize
 *        在会话诊断栈就绪后调用，把 §7.6 第③段 dev 日志尾接上）。
 *
 * 未安装时为无操作（开发期动态装载通道的宿主进程不装捕获——静默无害）。
 * 线程约束：应用主线程（与安装同一线程序；原子发布对崩溃线程可见）。
 *
 * @param source [in] 快照源（可空＝解除；非拥有，须覆盖进程生命周期——
 *               注册方以成员/静态存储期持有）
 */
void setCrashSnapshotSource(CrashReportSnapshotSource* source) noexcept;

// =====================================================================
// 受控崩溃测试通道（防误触——语义见函数注；产品参数面入口在 StudioMain）
// =====================================================================

/**
 * @brief 受控崩溃通道（仅测试放行：--ird-crash-test 参数面/进程级契约
 *        测试子进程消费——正常用户输入不可能触达）。
 *
 * 防误触设计（四道闸，缺一不触发）：
 *   ①参数面全 token 精确匹配 "--ird-crash-test"（ird- 前缀产品命名空间，
 *     不与框架/Qt 参数冲突）；
 *   ②模式白名单封闭 {"av","abort"}——未知值以非零码退出（fail-fast），
 *     绝不落入"未知＝正常启动"的静默通道；
 *   ③命中即短路——不创建任何窗口、不进入事件循环、不写任何用户数据，
 *     副作用只有"安装捕获→受控崩溃→诊断文件落盘"；
 *   ④崩溃形态本身是进程终结——误触发的后果（进程退出＋崩溃文件）显性
 *     可见而非静默。
 *
 * 模式语义（受控故障注入两形态——AT-11 真实失败能力的触发器）：
 *   av     受控空指针写入（volatile 写 nullptr——不可被优化器消除）→
 *          SEH 过滤器路径（Windows 崩溃主体形态，退出码＝异常码）；
 *   abort  raise(SIGABRT)→CRT 信号路径（abort 家族形态）。
 * 注意：本函数不返回（两种模式都终结进程）——"hang"形态（报告路径挂起
 * →看门狗自我了断）不进产品参数面：其注桩必须在报告路径内部（快照源
 * 挂起），产品面不保留该桩；进程级契约测试经测试子进程自有快照源实现
 * （StudioCrashChild——登记于 WP-24-T02 note）。
 *
 * @param mode [in] 通道模式 token（白名单见上；ASCII 窄串）
 *
 * @throws 无（noexcept 不承诺——故障注入本身不返回；模式校验失败路径
 *         _exit(2) 退出）
 */
[[noreturn]] void triggerCrashTestChannel(const std::string& mode);

}  // namespace studio
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_STUDIO_CRASHCAPTURE_HPP
