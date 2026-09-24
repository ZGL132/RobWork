/**
 * @file   ShellSupport.cpp
 * @brief  工作台壳的支撑设施实现：最近项目模型、用户级设置
 *         后台落盘线程、布局记忆与 ui 诊断码描述符表。
 *
 * 设计依据：
 *   - units/ui.md §4.3/PM-10（最近项目上限/去重/失效保留）、§4.5/§4.6/PM-14
 *     （布局记忆用户级、损坏回退、绝不写入 .rwdesign）、§3.4（后台落盘线程
 *     纪律）、§3.5（UI-* 稳定诊断码建议值表——九码逐行）；
 *   - 任务契约 tasks/foundation/UI-T03.json acceptance 1~4；
 *   - diagnostics DiagCodes.hpp（CodeDescriptor 十六字段与注册期验证表——
 *     码描述符必须通过 StableCodeRegistry::registerCode 校验）。
 *
 * 实现口径登记（UI-T06）：壳层命令板（ShellCommandBoard）已由命令注册表
 * （CommandRegistry，src/CommandRegistry.cpp）取代并移除——命令登记/门控/
 * 提交统一归注册表（§7.2/§10.3，SA-16 唯一入口），本文件不再承载命令面。
 *
 * 线程模型：除 UiSettingsWriter::run（自有工作线程）外，全部实现只在
 * UI 线程执行（§3.4 M-1）；uiDiagnosticCodeDescriptors 为纯函数（无状态）。
 */

#include "WorkbenchShell_p.hpp"

#include <QSettings>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string_view>

namespace sdurws {
namespace ird {
namespace ui {
namespace detail {

// =====================================================================
// RecentProjectsModel——最近项目（PM-10/PM-14）
// =====================================================================

void RecentProjectsModel::load(const QStringList& stored)
{
    m_paths.clear();
    m_paths.reserve(static_cast<std::size_t>(stored.size()));
    for (const QString& path : stored) {
        // 逐条规范化再入表：防御外部手工编辑设置文件造成的非规范形态
        // （读取侧同样走去重键口径，保证内存态与 note() 入口一致）。
        m_paths.push_back(canonicalize(path.toStdString()));
    }
    // 去重（保序取首现）＋裁剪上限：设置文件可能被手改出重复/超限形态，
    // 装载即收敛到模型不变量（PM-10 上限与去重语义）。
    std::vector<std::string> deduped;
    for (const auto& path : m_paths) {
        if (std::find(deduped.begin(), deduped.end(), path) == deduped.end()) {
            deduped.push_back(path);
        }
    }
    if (deduped.size() > kMaxRecentProjects) {
        deduped.resize(kMaxRecentProjects);  // 超限裁尾（最近使用序，尾部最旧）
    }
    m_paths.swap(deduped);
}

QStringList RecentProjectsModel::stored() const
{
    QStringList out;
    out.reserve(static_cast<int>(m_paths.size()));
    for (const auto& path : m_paths) {
        out << QString::fromStdString(path);
    }
    return out;
}

void RecentProjectsModel::note(const std::string& canonicalPath)
{
    if (canonicalPath.empty()) {
        return;  // 空路径不是项目事实（调用方违约防御——不产生空条目）
    }
    // 先移除旧位置再插到最前：既完成去重又完成"最近使用置顶"（PM-10）。
    remove(canonicalPath);
    m_paths.insert(m_paths.begin(), canonicalPath);
    if (m_paths.size() > kMaxRecentProjects) {
        m_paths.resize(kMaxRecentProjects);  // 裁掉最旧（表尾）
    }
}

void RecentProjectsModel::remove(const std::string& canonicalPath)
{
    // 幂等移除（PM-10"移除"动作；未在列＝无操作）。
    const auto it = std::find(m_paths.begin(), m_paths.end(), canonicalPath);
    if (it != m_paths.end()) {
        m_paths.erase(it);
    }
}

std::string RecentProjectsModel::canonicalize(const std::string& rawPath)
{
    // weakly_canonical：存在的路径解析真实规范形；不存在的路径做词法
    // 规范化（折叠 "."/".." 与分隔符）——"失效保留/重新选择"场景下路径
    // 可以尚不存在（PM-10 语义要求失效项仍留在表中）。
    std::error_code ec;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(std::filesystem::u8path(rawPath), ec);
    if (ec) {
        // 规范化失败（极端形态：非法字符等）：退回原样字符串，保留条目
        // 可见性（失效保留口径——不让一条坏路径炸掉整个列表）。
        return rawPath;
    }
    return canonical.u8string();
}

// =====================================================================
// UiSettingsWriter——后台落盘线程（§3.4）
// =====================================================================

void UiSettingsWriter::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return;  // 幂等启动（initialize 只调一次，防御重复调用）
    }
    m_stopRequested = false;
    m_running = true;
    // 单写线程（§3.4"1 条"——串行队列纪律的物理载体）。
    m_thread = std::thread([this] { run(); });
}

bool UiSettingsWriter::enqueue(Job job)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || m_stopRequested) {
            return false;  // 未启动/已收口：拒收并如实上抛（不静默丢写）
        }
        m_queue.push_back(std::move(job));
    }
    m_cv.notify_one();
    return true;
}

bool UiSettingsWriter::drainAndStop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            return true;  // 从未启动＝无未完成写任务（幂等收口）
        }
        m_stopRequested = true;
    }
    m_cv.notify_all();
    m_thread.join();  // 有界等待：工作线程只清空微秒级写任务即退出（§10.1 有界）
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool drained = m_queue.empty();
        m_running = false;
        return drained;
    }
}

void UiSettingsWriter::run()
{
    // 工作线程私有 QSettings 实例（QSettings 可重入；本类保证单线程串行，
    // 无跨线程共享实例——PM-14 用户级存储的唯一下游）。
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       kSettingsOrg, kSettingsApp);
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopRequested || !m_queue.empty(); });
            if (m_queue.empty()) {
                return;  // 收口且队列清空——线程退出（drainAndStop 的 join 返回点）
            }
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }
        job(settings);  // 值任务：不触碰任何 Widget（§3.4 纪律）
    }
}

// =====================================================================
// LayoutMemory——布局记忆读写与损坏判别（§4.5/PM-14）
// =====================================================================

LayoutMemory::LoadResult LayoutMemory::load(QSettings& settings)
{
    LoadResult out;
    // 无版本键＝无记忆（出厂首次启动）——§4.5 只对"存在但损坏/版本不识别"
    // 走诊断回退，首次启动静默用出厂布局（不制造假告警）。
    if (!settings.contains(QString(kGroup) + '/' + kKeyVersion)) {
        out.kind = LoadResult::Kind::Absent;
        return out;
    }

    const int version = settings.value(QString(kGroup) + '/' + kKeyVersion, -1).toInt();
    const QByteArray geometry =
        settings.value(QString(kGroup) + '/' + kKeyGeometry).toByteArray();
    const QByteArray state =
        settings.value(QString(kGroup) + '/' + kKeyState).toByteArray();
    // 可见性键缺失时按出厂可见处理（部分写入的容错——三区默认恒可恢复）。
    out.visibleLeft =
        settings.value(QString(kGroup) + '/' + kKeyVisibleLeft, true).toBool();
    out.visibleRight =
        settings.value(QString(kGroup) + '/' + kKeyVisibleRight, true).toBool();
    out.visibleBottom =
        settings.value(QString(kGroup) + '/' + kKeyVisibleBottom, true).toBool();

    // 版本不识别＝§4.5"恢复失败"两触发之一（另一为数据损坏）——未来版本
    // 的载荷结构本版本无法解释，按损坏同路径处理。
    if (version != kLayoutFormatVersion) {
        out.kind = LoadResult::Kind::Corrupt;
        return out;
    }
    // 空载荷（键存在但内容为空/类型不符）＝损坏。
    if (geometry.isEmpty() || state.isEmpty()) {
        out.kind = LoadResult::Kind::Corrupt;
        return out;
    }

    out.geometry = geometry;
    out.state = state;
    out.kind = LoadResult::Kind::Restored;
    return out;
}

void LayoutMemory::store(QSettings& settings, const QByteArray& geometry,
                         const QByteArray& state, bool visibleLeft,
                         bool visibleRight, bool visibleBottom)
{
    // 版本先行：读取侧以版本键判"有无记忆"（见 load）。
    settings.setValue(QString(kGroup) + '/' + kKeyVersion, kLayoutFormatVersion);
    settings.setValue(QString(kGroup) + '/' + kKeyGeometry, geometry);
    settings.setValue(QString(kGroup) + '/' + kKeyState, state);
    settings.setValue(QString(kGroup) + '/' + kKeyVisibleLeft, visibleLeft);
    settings.setValue(QString(kGroup) + '/' + kKeyVisibleRight, visibleRight);
    settings.setValue(QString(kGroup) + '/' + kKeyVisibleBottom, visibleBottom);
}

LayoutMemory::FlagsLoadResult LayoutMemory::loadFlags(QSettings& settings)
{
    // 嵌入式 Dock 宿主形态的旗标半区读取（ui.md §10.1 v1.10——O-38 裁决②）：
    // 几何/位形键不在本形态读写范围（它们是顶层窗口事实，属框架主窗口），
    // 损坏判别只覆盖版本键与三区旗标——判别口径与 load 同源：
    //   无版本键＝无记忆（出厂首次，静默默认）；版本不识别或旗标类型不符
    //   ＝Corrupt（§4.5 同路径：整段丢弃＋Dev 诊断＋回退默认）。
    FlagsLoadResult out;
    if (!settings.contains(QString(kGroup) + '/' + kKeyVersion)) {
        out.kind = FlagsLoadResult::Kind::Absent;
        return out;
    }

    const int version = settings.value(QString(kGroup) + '/' + kKeyVersion, -1).toInt();
    // 旗标类型判别：toBool 对非布尔/非可转字符串返回 false 且无法与
    // "用户真的选了隐藏"区分——以 canConvert 钉住类型面（与 load 的空载荷
    // 判别同纪律：键在但类型不符＝损坏，不猜默认值）。
    const QVariant left = settings.value(QString(kGroup) + '/' + kKeyVisibleLeft, true);
    const QVariant right = settings.value(QString(kGroup) + '/' + kKeyVisibleRight, true);
    const QVariant bottom = settings.value(QString(kGroup) + '/' + kKeyVisibleBottom, true);
    if (version != kLayoutFormatVersion || !left.canConvert<bool>()
        || !right.canConvert<bool>() || !bottom.canConvert<bool>()) {
        out.kind = FlagsLoadResult::Kind::Corrupt;
        return out;
    }

    out.visibleLeft = left.toBool();
    out.visibleRight = right.toBool();
    out.visibleBottom = bottom.toBool();
    out.kind = FlagsLoadResult::Kind::Restored;
    return out;
}

void LayoutMemory::storeFlags(QSettings& settings, bool visibleLeft,
                              bool visibleRight, bool visibleBottom)
{
    // 嵌入式宿主的旗标半区写入：只动版本键＋三旗标——顶层形态留下的
    // 几何/位形键原样保留（QSettings 按键写入保留组内其余键），两种宿主
    // 形态的记忆面互不覆盖（同组同键共享用户级设置——PM-14）。
    settings.setValue(QString(kGroup) + '/' + kKeyVersion, kLayoutFormatVersion);
    settings.setValue(QString(kGroup) + '/' + kKeyVisibleLeft, visibleLeft);
    settings.setValue(QString(kGroup) + '/' + kKeyVisibleRight, visibleRight);
    settings.setValue(QString(kGroup) + '/' + kKeyVisibleBottom, visibleBottom);
}

void LayoutMemory::discard(QSettings& settings)
{
    // 损坏段整段丢弃（§4.5 原文）：整组移除后下次 store 从干净状态重建，
    // 不残留半损坏键。
    settings.remove(kGroup);
}

// =====================================================================
// uiDiagnosticCodeDescriptors——ui 稳定诊断码表（§3.5 九码）
// =====================================================================

namespace {

/// §3.5 表行的描述符构造参数（码/分类/严重/可重试性——表列的编译期形态）。
struct UiCodeRow {
    const char* code;                          ///< 稳定码（UI 前缀＝所有权声明）
    diagnostics::DiagnosticCategory category;  ///< 分类（§4.3 词表）
    diagnostics::DiagnosticSeverity severity;  ///< 默认严重（归属码表不变）
    diagnostics::RetryKind retryable;          ///< 可重试性（§4.4 动作族机器锚）
};

/// 九码逐行（行序＝ui.md §3.5 表行序）。分类统一 Internal（§3.5"internal"
/// 列——防御性/界面内部事实）；可重试性按 §4.4 动作族机械映射：需要用户
/// 采取措施后可重试的（改绑/重提交/重试落盘）＝UserRetry，结论/报告类
/// ＝Never（映射规则同 DiagCodes.cpp 内置表表头登记）。
constexpr UiCodeRow kUiCodeRows[] = {
    // 冲突键被拒：用户改绑后可重试（fix-input 族）。
    {"UI-HOTKEY-CONFLICT",       diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Warning,  diagnostics::RetryKind::UserRetry},
    // 装配期重复注册：装配清单修订问题，非重试面。
    {"UI-CMD-DUPLICATE",         diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Dev,      diagnostics::RetryKind::Never},
    // 未知命令提交：修正 id 后可重试。
    {"UI-CMD-UNKNOWN",           diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Warning,  diagnostics::RetryKind::UserRetry},
    // 上下文不可执行：上下文变化后可重试。
    {"UI-CMD-NOT-EXECUTABLE",    diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Info,     diagnostics::RetryKind::UserRetry},
    // 插件装配失败：降级占位，重试无独立语义（inspect 族）。
    {"UI-PLUGIN-ASSEMBLY-FAILED", diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Error,    diagnostics::RetryKind::Never},
    // 布局记忆损坏：回退默认已自愈，无重试语义（Dev 级）。
    {"UI-LAYOUT-RESTORE-FAILED", diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Dev,      diagnostics::RetryKind::Never},
    // 自动落盘失败：保留脏标记，用户/定时器可重试（fix-input 族）。
    {"UI-DRAFT-AUTOSAVE-FAILED", diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Warning,  diagnostics::RetryKind::UserRetry},
    // 迟到写请求被拒：不重试（重试即再违约——SA-17 上下文已释放）。
    {"UI-SESSION-CONTEXT-INVALID", diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Warning,  diagnostics::RetryKind::Never},
    // Qt 平台异常：进程级故障面（PM-17 异常诊断路径；Dev 级）。
    {"UI-QT-PLATFORM-FAULT",     diagnostics::DiagnosticCategory::Internal,
     diagnostics::DiagnosticSeverity::Error,    diagnostics::RetryKind::Never},
};

}  // namespace

std::vector<diagnostics::CodeDescriptor> buildUiCodeDescriptors()
{
    std::vector<diagnostics::CodeDescriptor> out;
    out.reserve(std::size(kUiCodeRows));
    for (const auto& row : kUiCodeRows) {
        diagnostics::CodeDescriptor d;
        d.code = row.code;
        d.ownerUnit = "ui";  // 前缀-所有权一致（§4.5 前缀表：UI → ui）
        d.category = row.category;
        d.severity = row.severity;
        // 文案键（P-DIAG-9 键/值分离——命名约定 diag.<code-lower>.title/detail；
        // 值归 ui 文案资源随 UI-T09 落地，键在注册期即唯一性冻结）。
        const std::string lower = [&] {
            std::string s = row.code;
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }();
        d.titleKey = "diag." + lower + ".title";
        d.detailKey = "diag." + lower + ".detail";
        // 无参码（UI-T03 阶段无占位参数——§4.5 必填字段的显式空形态；
        // 占位参数随 UI-T13 呈现任务按需增量登记）。
        d.paramSchema = "[]";
        d.confirmable = false;          // 阶段 A 无可确认域码（同内置表口径）
        d.requiresComparison = false;   // confirmable=false ⇒ 恒 false（§4.5）
        d.retryable = row.retryable;
        // Dev 码强制三 false（§4.5 注册期验证——userVisible/reportable/
        // historical；非 Dev 码按 §3.5"用户可见"列：UI-QT-PLATFORM-FAULT
        // 用户可见＝否 → 仅 userVisible=false，其余两轴照常）。
        if (row.severity == diagnostics::DiagnosticSeverity::Dev) {
            d.userVisible = false;
            d.reportable = false;
            d.historical = false;
        } else {
            d.userVisible = (row.code != std::string_view{"UI-QT-PLATFORM-FAULT"});
            d.reportable = true;
            d.historical = true;
        }
        d.registryVersion = 1;  // 首次登记（§4.5：元数据演进才递增）
        d.deprecated = false;
        out.push_back(std::move(d));
    }
    return out;
}

}  // namespace detail
}  // namespace ui

namespace ui {

std::vector<diagnostics::CodeDescriptor> uiDiagnosticCodeDescriptors()
{
    return detail::buildUiCodeDescriptors();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
