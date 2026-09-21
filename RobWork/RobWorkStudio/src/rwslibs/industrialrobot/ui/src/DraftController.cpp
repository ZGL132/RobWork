/**
 * @file   DraftController.cpp
 * @brief  草稿控制器实现——保存/应用分离的落盘编排（§8.2/§8.4）、打开
 *         恢复（§8.3）、StaleRevisionRejected 冲突呈现（§8.5）与草稿
 *         局部撤销栈（§8.7）——UI-T12。
 *
 * 实现与设计的逐条对应（review 对照表）：
 *   - saveAll ＝ §8.2 数据流"收集脏模块→向域编辑器要文档→投后台线程
 *     调用 save"的同步版（Manual/CloseDialog——§8.4"关闭流程中的
 *     saveAll(Manual) 在确认对话框上下文同步完成"）；落盘段全在
 *     postToDiskThread 串行执行器内（UI 线程零磁盘 IO 红线的执行面）；
 *   - tickAutosave ＝ §8.4 定时器触发半区（分派即返回；在途跳过＝
 *     "落盘线程串行，save 期间的新脏数据进入下一周期"；完成回执经
 *     postToUiThread Marshal 回 UI 线程消费）；
 *   - restoreOnOpen ＝ §8.3 五步逐行（list→逐模块 tryLoad→损坏 .bak
 *     回退由对端承载→adoptRestoredDocument 交域侧→孤儿/损坏/残留汇总）；
 *   - recoveryBanner ＝ §8.3-4 横幅"一句话汇总＋查看详情/恢复草稿/
 *     放弃"的值装配（孤儿以计数呈现——UX-02 零内部名）；
 *   - onCommandResult ＝ §8.5 冲突处理入口（Committed＝草稿被应用消费
 *     →清脏＋清局部栈；Rejected(stale-revision)＝草稿保留＋暂存冲突
 *     定位数据；不自动重试提交——控制器无命令通道的结构保证）；
 *   - staleConflictData/reEditOnCurrentRevision ＝ §8.5 对话框三选项的
 *     数据装配与[基于当前版本重新编辑]执行半区；
 *   - pushLocalCheckpoint/undoLocalEdit/redoLocalEdit ＝ §8.7 会话栈
 *     （不经命令服务、不产生修订；应用成功即清空——"局部撤销不能撤销
 *     已应用修订"）。
 *
 * 线程纪律（§3.4 M-1＋§8.4）：模型状态只在 UI 线程变更（完成回执经
 * postToUiThread 转回后才触碰表）；磁盘写只出现在 postToDiskThread
 * 任务体内；阻塞等待仅出现在显式同步入口（saveAll/discardDraft——
 * §8.4 关闭对话框上下文的同步完成语义）。落盘任务按值捕获文档与端口
 * shared_ptr（在途存活期不依赖会话绑定存续；关闭后的对端拒绝照常经
 * 返回值轨表达——context-closed token）。
 */

#include <sdurws/ird/ui/IDraftController.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <future>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sdurws::ird {
namespace ui {

namespace {

// ---------------------------------------------------------------------
// 常量（§3.5/§4.1 锚点——数值来源见行尾注释，禁止无出处魔法数）
// ---------------------------------------------------------------------

/// Dev 日志通道 token（≤48 字符——diagnostics 日志通道词法；与壳层
/// "diag/ui/session" 同族，UI-T11 先例）。
constexpr const char* kDraftDevChannel = "diag/ui/draft";

/// 定时保存默认周期：60 s（PM-04-S1 原文"默认 60 s"——装配/编程可改；
/// 用户级 60–600 s 调节归 WP-04-T20，本任务不提前实现）。
constexpr std::chrono::seconds kDefaultAutosaveInterval{60};

/// 模块 token 词法上限：64（对端 DraftService 白名单同口径——project.md
/// §4.1 命名规则"1~64 个 ASCII 字母/数字/下划线/连字符"；控制器在挂接
/// 与落盘分派两处同口径预检——磁盘路径拼装面，提前拦截不产生半途
/// 副作用，不另立语义只承接对端契约）。
constexpr std::size_t kModuleTokenMaxLength = 64;

/**
 * @brief 模块 token 词法校验（§4.1 白名单同口径预检）。
 *
 * 为什么控制器预检而对端还会再校：moduleId 直接参与磁盘路径拼装
 * （drafts/&lt;branch&gt;/&lt;module&gt;.draft.json），越权字符是路径穿越/命名歧义
 * 的入口——控制器在挂接入口拦截让装配错误在启动期暴露，而不是等到
 * 第一次落盘才在对端失败（fail-fast 提前量；语义与对端一致，不放宽
 * 不收紧）。
 */
bool isValidModuleToken(const std::string& token)
{
    if (token.empty() || token.size() > kModuleTokenMaxLength) {
        return false;
    }
    for (const char c : token) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                             || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 落盘时刻的缺省供应（nowUtcIso 未注入时——ISO-8601 UTC 文本，
 *        与对端磁盘格式 §4.4.5 的时刻词形一致；gmtime 消除本地时区
 *        差异，NFR-COR-02 确定性）。
 */
std::string defaultNowUtcIso()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm tmUtc{};
#if defined(_WIN32)
    gmtime_s(&tmUtc, &now);
#else
    gmtime_r(&now, &tmUtc);
#endif
    char buffer[32] = {};
    // 词形："YYYY-MM-DDTHH:MM:SSZ"（31 字节缓冲充裕；strftime 失败仅
    // 可能因缓冲不足——此处不可能，防御性留空串）。
    const std::size_t n = std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
    return std::string(buffer, n);
}

}  // namespace

// =====================================================================
// 实现类型（私有——R-2 纪律：实现类不进公共头，装配入口 createDraft
// Controller 是唯一构造面）
// =====================================================================

class DraftController final : public IDraftController {
public:
    explicit DraftController(DraftControllerDeps deps)
        : m_deps(std::move(deps))
    {
        // 装配错误 fail-fast（AGENTS §3 错误语义）：没有落盘执行器的
        // 控制器只剩"UI 线程直接写盘"一条路——那正是 UI 线程零磁盘 IO
        // 红线（§8.4/D-6）禁止的形态，装配期拦截而不留给运行期。
        if (!m_deps.postToDiskThread || !m_deps.postToUiThread) {
            throw std::invalid_argument(
                "ui/draft: postToDiskThread/postToUiThread 执行器为空"
                "（必填——UI 线程零磁盘 IO 红线要求磁盘写只经串行落盘"
                "执行器进入落盘线程，§3.4/§8.4）");
        }
        // 缺省钟：时长判定用单调钟（墙钟跳变不影响 autosave 周期——
        // §8.4 周期语义）；落盘时刻用系统钟（磁盘格式词形）。
        if (!m_deps.steadyClock) {
            m_deps.steadyClock = []() { return std::chrono::steady_clock::now(); };
        }
        if (!m_deps.nowUtcIso) {
            m_deps.nowUtcIso = defaultNowUtcIso;
        }
    }

    // ---- 会话绑定（§5.2 打开/关闭时序的草稿侧）----

    void bindSession(const DraftSessionBinding& binding) override
    {
        if (m_binding.has_value()) {
            throw std::logic_error(
                "ui/draft: bindSession 已有绑定未解除（unbindSession 先行"
                "——会话状态机界面侧不接纳叠绑，§5.2 单 Open* 纪律同源）");
        }
        // 装配契约校验（fail-fast）：清单端口缺位＝汇总/恢复无从谈起；
        // 可写会话缺写半区＝第一次保存才暴露的装配缺陷——提前到绑定期。
        if (!binding.drafts) {
            throw std::invalid_argument(
                "ui/draft: bindSession 缺草稿清单端口（IUiDraftQueryPort"
                " 必填——汇总合并与恢复编排的磁盘面）");
        }
        if (binding.writable && !binding.store) {
            throw std::logic_error(
                "ui/draft: 可写会话绑定缺写半区端口（IUiDraftStorePort——"
                "save/discard 无处执行，装配违约）");
        }
        m_binding = binding;
        // 新会话零表状态：上一会话的模块表/撤销栈/冲突态不跨会话存活
        // （§8.6 切换处置"会话脏数据丢弃"的表半区；磁盘草稿零触碰——
        // "磁盘草稿不删除，下次打开仍可恢复"）。
        m_order.clear();
        m_modules.clear();
        m_lastAutosaveDispatch = std::chrono::steady_clock::time_point{};
        fireDirtyObserver();
    }

    void unbindSession() override
    {
        // 幂等：未绑定调用＝空操作（关闭后再解除的编排容错）。
        if (!m_binding.has_value()) {
            return;
        }
        m_binding.reset();
        m_order.clear();
        m_modules.clear();
        m_lastAutosaveDispatch = std::chrono::steady_clock::time_point{};
        fireDirtyObserver();
    }

    bool hasSession() const noexcept override
    {
        return m_binding.has_value();
    }

    // ---- 模块挂接（§10.5 attach/detach）----

    void attachModule(const ModuleDraftHandle& handle,
                      IModuleDraftSource& source) override
    {
        // 前置三连（§10.5 前置行原文：模块 id 未占用且会话可写）：
        // 未绑定会话无从谈可写，一并拒绝；只读会话拒绝挂接写源（§5.5）。
        if (!m_binding.has_value()) {
            throw std::logic_error(
                "ui/draft: attachModule 未绑定会话（打开成功并 bindSession"
                " 后才可挂接域模块——§5.2 打开协议⑤步后）");
        }
        if (!m_binding->writable) {
            throw std::logic_error(
                "ui/draft: 只读会话拒绝挂接写源（§5.5 只读禁用清单——"
                "草稿写入口在只读会话不可用）");
        }
        if (!isValidModuleToken(handle)) {
            throw std::invalid_argument(
                "ui/draft: 模块句柄词法违约（§4.1：1~64 个 ASCII 字母/"
                "数字/下划线/连字符——磁盘路径拼装面预检）: " + handle);
        }
        if (m_modules.find(handle) != m_modules.end()) {
            throw std::logic_error(
                "ui/draft: 模块句柄已占用（§10.5 前置行——重复挂接＝装配"
                "违约）: " + handle);
        }
        ModuleEntry entry;
        entry.source = &source;
        // 显示名注册时刻定格（域显示名稳定——汇总/横幅/冲突对话框的
        // 呈现值不随查询时刻漂移，NFR-COR-02）。
        entry.displayName = source.displayName();
        m_modules.emplace(handle, std::move(entry));
        m_order.push_back(handle);
    }

    void detachModule(const ModuleDraftHandle& handle) override
    {
        // 未知句柄＝幂等空操作（域侧重复摘除的编排容错）。
        const auto it = m_modules.find(handle);
        if (it == m_modules.end()) {
            return;
        }
        m_modules.erase(it);
        const auto orderIt = std::find(m_order.begin(), m_order.end(), handle);
        if (orderIt != m_order.end()) {
            m_order.erase(orderIt);
        }
        fireDirtyObserver();
    }

    void notifySessionDirty(const ModuleDraftHandle& handle) override
    {
        ModuleEntry& entry = requireModule(handle, "notifySessionDirty");
        // 脏代次单调递增：落盘完成回执只在"代次未变"时清脏——save 期间
        // 的新编辑（代次已前进）自动落入下一周期（§8.4"不合并半成品
        // 文档"的判定锚）。
        ++entry.dirtySerial;
        fireDirtyObserver();
    }

    // ---- 汇总（§8.2）----

    DraftingSummary summary() const override
    {
        DraftingSummary out;
        // 磁盘行一次快照（§8.2"磁盘侧以 DraftService 汇总为权威"——
        // IUiDraftQueryPort 短查询，UI 线程）。
        std::vector<DraftRowProjection> rows;
        if (m_binding.has_value() && m_binding->drafts) {
            rows = m_binding->drafts->listDrafts();
        }
        std::map<std::string, const DraftRowProjection*> rowByModule;
        for (const auto& row : rows) {
            if (!row.moduleId.empty()) {
                rowByModule.emplace(row.moduleId, &row);
            }
        }
        // 挂接模块（attach 注册序——NFR-COR-02 稳定呈现，见
        // DraftingSummary 排序纪律）与磁盘行合并。
        for (const auto& handle : m_order) {
            const ModuleEntry& entry = m_modules.at(handle);
            ModuleDraftView view;
            view.moduleId = handle;
            view.displayName = entry.displayName;
            view.sessionDirty = entry.dirtySerial > 0;
            const auto rowIt = rowByModule.find(handle);
            if (rowIt != rowByModule.end()) {
                // 磁盘事实直读（present/stale/基线——判定权威在 project，
                // ui 不复算，§8.2"不在 ui 侧判定冲突细节"）。
                view.diskPresent = true;
                view.stale = rowIt->second->stale;
                view.baseRevisionCanonical = rowIt->second->baseRevisionCanonical;
                rowByModule.erase(rowIt);
            }
            view.lastSaveOriginToken = entry.lastSaveOriginToken;
            view.lastSavedAtUtc = entry.lastSavedAtUtc;
            if (view.sessionDirty || view.diskPresent) {
                out.anyDirty = true;
            }
            out.modules.push_back(std::move(view));
        }
        // 挂接耗尽后的剩余磁盘行＝孤儿草稿（§8.3-4；行序＝对端 moduleId
        // 字典序——追加不重排）。
        for (const auto& [moduleId, row] : rowByModule) {
            ModuleDraftView view;
            view.moduleId = moduleId;
            // 孤儿无挂接源即无显示名——回退模块 token（注册词表标识，
            // 非哈希形态，UX-02 允许；命名呈现归 L5 名称端口的完整方案
            // 随阶段 B 域插件交付复核）。
            view.displayName = row->displayName.empty() ? moduleId : row->displayName;
            view.diskPresent = true;
            view.stale = row->stale;
            view.baseRevisionCanonical = row->baseRevisionCanonical;
            out.anyDirty = true;
            out.modules.push_back(std::move(view));
        }
        return out;
    }

    bool anyUnappliedChanges() const override
    {
        return summary().anyDirty;
    }

    // ---- 保存（§8.2/§8.4——保存/应用分离红线：零修订）----

    SaveOutcome saveAll(const SaveTrigger trigger) override
    {
        requireWritableSession("saveAll");
        // 同步收集→同步等待落盘结局（§8.4"手动保存与定时保存共用同一
        // 入口"——origin 区分；CloseDialog 落盘语义＝Manual）。
        const DraftOrigin origin =
            trigger == SaveTrigger::Autosave ? DraftOrigin::Autosave : DraftOrigin::Manual;
        // 同一次保存的所有模块共用同一落盘时刻（NFR-COR-02——同一批次的
        // 留痕时刻一致）。
        const std::string savedAtUtc = m_deps.nowUtcIso();
        SaveOutcome outcome;
        for (const auto& handle : m_order) {
            ModuleEntry& entry = m_modules.at(handle);
            if (entry.dirtySerial == 0) {
                continue;  // 非脏模块不落盘（§8.2 只收集脏模块）
            }
            // 文档组装在 UI 线程（域源纯内存——零磁盘 IO）；控制器补齐
            // 会话一致性字段（归属三元组强制与绑定一致——防跨项目落盘；
            // 时刻/origin 为落盘批次事实，归控制器而非域源）。
            DraftDocumentProjection document = entry.source->buildDraftDocument();
            validateDocument(handle, document);
            document.projectId = m_binding->projectId;
            document.branchId = m_binding->branchId;
            document.origin = origin;
            document.savedAtUtc = savedAtUtc;
            const std::uint64_t serial = entry.dirtySerial;
            const DraftSaveOutcome save = runSaveBlocking(std::move(document));
            if (save.ok) {
                // 脏代次守卫：仅当保存期间无新编辑（代次未变）才清脏——
                // 期间有编辑则保持脏标记（新脏进入下一周期，§8.4）。
                if (entry.dirtySerial == serial) {
                    entry.dirtySerial = 0;
                    entry.lastSaveOriginToken = draftOriginToken(origin);
                    entry.lastSavedAtUtc = savedAtUtc;
                }
                ++outcome.savedCount;
            } else {
                // 失败保留脏标记（§8.2 失败行）；失败明细进返回值轨，
                // 由调用方/关闭对话框就地反馈。
                ++outcome.failedCount;
                outcome.failedModules.push_back(handle);
                if (!outcome.firstError.has_value()) {
                    outcome.firstError = SaveModuleError{handle, save.errorToken, save.detail};
                }
                emitDevLine("saveAll failed: module=" + handle
                            + " token=" + save.errorToken + " detail=" + save.detail);
            }
        }
        fireDirtyObserver();
        return outcome;
    }

    // ---- 打开恢复（§8.3）----

    RestoreOutcome restoreOnOpen() override
    {
        requireSession("restoreOnOpen");
        RestoreOutcome outcome;
        // ①清单（对端 moduleId 字典序——NFR-COR-02）；②逐模块 tryLoad：
        // 读轨直调（恢复在打开路径上——与五步协议同上下文，§8.3 原文
        // 未要求后台化；写轨才受"UI 线程零磁盘 IO"约束，§3.4 线程表
        // 只把"草稿 save 调用"列入落盘线程行）。
        const std::vector<DraftRowProjection> rows = m_binding->drafts->listDrafts();
        for (const auto& row : rows) {
            if (row.moduleId.empty()) {
                continue;  // 无关联键的行无法寻址——不虚构归属
            }
            const auto entryIt = m_modules.find(row.moduleId);
            if (entryIt == m_modules.end()) {
                // ④孤儿草稿：磁盘有、会话无挂接源（§8.3-4——横幅一句话
                // 汇总；放弃走显式 discardDraft，可写会话才可用）。
                outcome.orphanModuleIds.push_back(row.moduleId);
                continue;
            }
            // ②③损坏回退 .bak 由对端 tryLoad 承载（RecoveredFromBackup
            // 位）；旧损坏文件对端保留供人工核查（PM-08），ui 不删文件
            // （§10.5 非法行"删除/改写磁盘草稿文件"）。
            const DraftLoadOutcome load = m_binding->store
                ? m_binding->store->tryLoad(row.moduleId)
                : DraftLoadOutcome{};
            if (load.status == DraftLoadOutcome::Status::Loaded
                || load.status == DraftLoadOutcome::Status::RecoveredFromBackup) {
                // ⑤恢复进域编辑器（阶段 A：登记机制——桩模块验证协议；
                // 恢复来源差异（current/​.bak）不进域侧语义）。
                entryIt->second.source->adoptRestoredDocument(load.document);
                outcome.restoredModules.push_back(row.moduleId);
                if (load.status == DraftLoadOutcome::Status::RecoveredFromBackup) {
                    outcome.backupRecoveredModules.push_back(row.moduleId);
                }
            } else if (load.status == DraftLoadOutcome::Status::Corrupt) {
                // .bak 亦不可用——损坏清单（横幅呈现；不影响其余模块）。
                outcome.corruptModules.push_back(row.moduleId);
                emitDevLine("restore corrupt: module=" + row.moduleId
                            + " token=" + load.errorToken + " detail=" + load.detail);
            }
            // Missing＝清单与盘面瞬时不一致——以 Missing 为准，不计损坏
            // 不虚构恢复（present=false 安全空值纪律）。
            if (load.newResidueDropped) {
                ++outcome.newResidueDropped;
            }
        }
        fireDirtyObserver();
        return outcome;
    }

    DraftRecoveryBannerProjection recoveryBanner(const RestoreOutcome& outcome) const override
    {
        DraftRecoveryBannerProjection banner;
        banner.backupRecovered = outcome.corruptRecovered();
        banner.orphanCount = outcome.orphanModuleIds.size();
        banner.newResidueDropped = outcome.newResidueDropped;
        // 已恢复模块显示名（UX-02——有源模块用注册显示名；已摘除的模块
        // 查不到名，跳过不虚构）。
        for (const auto& moduleId : outcome.restoredModules) {
            const auto it = m_modules.find(moduleId);
            if (it != m_modules.end()) {
                banner.restoredModuleNames.push_back(it->second.displayName);
            }
        }
        banner.present = !outcome.restoredModules.empty()
                         || !outcome.corruptModules.empty()
                         || banner.orphanCount > 0
                         || banner.newResidueDropped > 0;
        // [放弃]＝写操作（§8.3-4——只读会话不可用；绑定缺席亦不可用）。
        banner.discardAvailable = m_binding.has_value() && m_binding->writable;
        if (banner.present) {
            banner.messageKey = kDraftRecoveryBannerSummaryKey;
            // 动作键固定序（PM-15 原文序：查看详情/恢复草稿/放弃）。
            banner.actionKeys = {kDraftRecoveryActionViewDetailKey,
                                 kDraftRecoveryActionRestoreKey,
                                 kDraftRecoveryActionDiscardKey};
        }
        return banner;
    }

    DraftDiscardOutcome discardDraft(const ModuleDraftHandle& handle) override
    {
        requireWritableSession("discardDraft");
        if (!isValidModuleToken(handle)) {
            throw std::invalid_argument(
                "ui/draft: discardDraft 句柄词法违约（§4.1）: " + handle);
        }
        // 写轨——落盘线程串行执行（与 save 同一线程纪律），同步等待
        // 结局（横幅[放弃]的就地反馈）。
        std::shared_ptr<IUiDraftStorePort> store = m_binding->store;
        std::promise<DraftDiscardOutcome> promise;
        std::future<DraftDiscardOutcome> future = promise.get_future();
        m_deps.postToDiskThread([&promise, store, handle]() {
            promise.set_value(store->discard(handle));
        });
        const DraftDiscardOutcome outcome = future.get();
        if (!outcome.ok) {
            emitDevLine("discard failed: module=" + handle
                        + " token=" + outcome.errorToken + " detail=" + outcome.detail);
        }
        fireDirtyObserver();  // 磁盘 present 位可能变化（标题 `*` 联动）
        return outcome;
    }

    // ---- 定时保存（§8.4）----

    std::size_t tickAutosave(const std::chrono::steady_clock::time_point now) override
    {
        // 未绑定/只读会话＝零分派（只读不落盘，§5.5；定时器常驻运行）。
        if (!m_binding.has_value() || !m_binding->writable) {
            return 0;
        }
        // 在途守卫：有落盘在途即跳过本轮——串行落盘线程纪律（§8.4"同一
        // 时刻至多一个 save 在途"；期间新脏数据由脏代次保证进入下一轮）。
        if (m_autosavesInFlight > 0) {
            return 0;
        }
        // 双触发防御：距上次分派不足一个周期＝忽略（L5 定时器周期与
        // 周期设置由装配对齐，此处兜底防重复分派）。
        if (m_lastAutosaveDispatch.time_since_epoch().count() != 0
            && now - m_lastAutosaveDispatch < m_autosaveInterval) {
            return 0;
        }
        m_lastAutosaveDispatch = now;
        const std::string savedAtUtc = m_deps.nowUtcIso();
        std::size_t dispatched = 0;
        for (const auto& handle : m_order) {
            ModuleEntry& entry = m_modules.at(handle);
            if (entry.dirtySerial == 0) {
                continue;
            }
            DraftDocumentProjection document = entry.source->buildDraftDocument();
            validateDocument(handle, document);
            document.projectId = m_binding->projectId;
            document.branchId = m_binding->branchId;
            document.origin = DraftOrigin::Autosave;
            document.savedAtUtc = savedAtUtc;
            dispatchAutosaveSave(handle, std::move(document), entry.dirtySerial,
                                 savedAtUtc);
            ++dispatched;
        }
        return dispatched;
    }

    void setAutosaveInterval(const std::chrono::seconds interval) override
    {
        // 非正周期拒绝（周期无"立即"语义——正周期是双触发防御的前提）。
        if (interval <= std::chrono::seconds::zero()) {
            throw std::invalid_argument(
                "ui/draft: 定时保存周期必须为正值（PM-04-S1 语义——60 s "
                "默认；用户级 60–600 s 调节归 WP-04-T20）");
        }
        m_autosaveInterval = interval;
    }

    std::chrono::seconds autosaveInterval() const noexcept override
    {
        return m_autosaveInterval;
    }

    // ---- 应用回执（§8.5）----

    void onCommandResult(const ModuleDraftHandle& handle,
                         const CommandResultProjection& result) override
    {
        ModuleEntry& entry = requireModule(handle, "onCommandResult");
        if (result.status == CommandResultProjection::Status::Committed) {
            // 应用成功＝草稿被消费（§8.6"磁盘草稿由 project 侧处置——
            // 应用即消费；ui 清会话脏标记"）。局部撤销栈同时清空（§8.7
            // "局部撤销不能撤销已应用修订"——修订历史归项目命令栈）。
            entry.dirtySerial = 0;
            entry.staleConflictPending = false;
            entry.staleDetail = StaleRevisionDetailProjection{};
            entry.undoStack.clear();
            entry.redoStack.clear();
            entry.lastAppliedRevisionCanonical = result.newRevision.has_value()
                ? result.newRevision->toCanonical()
                : std::string{};
            // 本会话保存轨迹指向一个已不存在的草稿——清零（呈现层不再
            // 显示过期的"最近保存"时刻）。
            entry.lastSaveOriginToken.clear();
            entry.lastSavedAtUtc.clear();
            emitDevLine("draft applied: module=" + handle
                        + " revision=" + entry.lastAppliedRevisionCanonical);
        } else if (result.status == CommandResultProjection::Status::Rejected
                   && result.rejectionReason == "stale-revision") {
            // 冲突未决：草稿保留（§8.5"拒绝永远不销毁草稿"——脏标记保持，
            // 编辑不中断）；暂存冲突定位数据供对话框装配。"不自动重试
            // 提交"由结构保证——控制器不持有命令网关，无任何提交通道。
            entry.staleConflictPending = true;
            if (result.staleDetail.has_value()) {
                entry.staleDetail = *result.staleDetail;
            }
            emitDevLine("draft apply rejected (stale-revision): module=" + handle
                        + " draftBase=" + entry.staleDetail.draftBaseRevision
                        + " tip=" + entry.staleDetail.currentTipRevision);
        } else {
            // 其余结局（其他拒绝/中止/失败）：草稿原样保留（编辑不中断）；
            // 呈现反馈归调用方（提交侧），控制器只留 Dev 轨迹。
            emitDevLine("draft apply outcome (draft retained): module=" + handle);
        }
        fireDirtyObserver();
    }

    StaleConflictDialogData
    staleConflictData(const ModuleDraftHandle& handle) const override
    {
        const auto it = m_modules.find(handle);
        if (it == m_modules.end()) {
            throw std::logic_error(
                "ui/draft: staleConflictData 未知模块句柄: " + handle);
        }
        const ModuleEntry& entry = it->second;
        StaleConflictDialogData data;
        if (!entry.staleConflictPending) {
            return data;  // present=false＝无未决冲突（呈现层不渲染）
        }
        data.present = true;
        data.moduleDisplayName = entry.displayName;
        // 对照值原样（canonical 文本——呈现层直接对比显示，零二次加工；
        // UX-02 守卫作用于位置参数通道，对照值不经该通道）。
        data.currentTipRevision = entry.staleDetail.currentTipRevision;
        data.draftBaseRevision = entry.staleDetail.draftBaseRevision;
        data.advancedSummary = entry.staleDetail.advancedSummary;
        data.involvedObjectNames = entry.staleDetail.involvedObjectNames;
        // 三选项键固定（§8.5 原文序与推荐位——[基于当前版本重新编辑]
        // 推荐/[查看差异对象]/[取消]）。
        data.messageKey = kDraftStaleConflictTitleKey;
        data.reEditActionKey = kDraftStaleConflictReEditActionKey;
        data.viewDiffActionKey = kDraftStaleConflictViewDiffActionKey;
        data.cancelActionKey = kDraftStaleConflictCancelActionKey;
        return data;
    }

    void reEditOnCurrentRevision(const ModuleDraftHandle& handle) override
    {
        ModuleEntry& entry = requireModule(handle, "reEditOnCurrentRevision");
        if (!entry.staleConflictPending) {
            throw std::logic_error(
                "ui/draft: reEditOnCurrentRevision 无未决冲突（只应在冲突"
                "对话框决议[基于当前版本重新编辑]时调用——§8.5）: " + handle);
        }
        // 域源以当前 tip 重建编辑基线（用户迁移未应用修改的域侧半区）；
        // 旧检查点基于过期基线——清空（恢复它们等于复活过期内容）。
        const std::string tip = entry.staleDetail.currentTipRevision;
        entry.source->rebuildOnRevision(tip);
        entry.staleConflictPending = false;
        entry.staleDetail = StaleRevisionDetailProjection{};
        entry.dirtySerial = 0;  // §8.5"ui 重置该模块会话脏标记"（用户
        entry.undoStack.clear();  // 迁移后的编辑由域源重新通知置脏）
        entry.redoStack.clear();
        fireDirtyObserver();
    }

    // ---- 草稿局部撤销栈（§8.7）----

    void pushLocalCheckpoint(const ModuleDraftHandle& handle) override
    {
        ModuleEntry& entry = requireModule(handle, "pushLocalCheckpoint");
        // 快照当前文档为回退点（域源纯内存组装——零 IO）；新检查点使
        // 重做栈失效（标准撤销栈纪律：分叉历史不可重放）。
        entry.undoStack.push_back(entry.source->buildDraftDocument());
        entry.redoStack.clear();
    }

    bool undoLocalEdit(const ModuleDraftHandle& handle) override
    {
        ModuleEntry& entry = requireModule(handle, "undoLocalEdit");
        if (entry.undoStack.empty()) {
            // 栈空＝无可撤销——应用成功后栈已清空，本返回值就是"局部
            // 撤销不能撤销已应用修订"（§8.7）的执行面。
            return false;
        }
        // 当前活态先压重做栈（撤销/重做对称回放），再回退到上一检查点
        // （adoptRestoredDocument 承载值交回域侧——不经命令服务、零修订，
        // 与项目命令撤销栈互不越界的结构保证之一：控制器无命令通道）。
        entry.redoStack.push_back(entry.source->buildDraftDocument());
        const DraftDocumentProjection document = entry.undoStack.back();
        entry.undoStack.pop_back();
        entry.source->adoptRestoredDocument(document);
        return true;
    }

    bool redoLocalEdit(const ModuleDraftHandle& handle) override
    {
        ModuleEntry& entry = requireModule(handle, "redoLocalEdit");
        if (entry.redoStack.empty()) {
            return false;
        }
        entry.undoStack.push_back(entry.source->buildDraftDocument());
        const DraftDocumentProjection document = entry.redoStack.back();
        entry.redoStack.pop_back();
        entry.source->adoptRestoredDocument(document);
        return true;
    }

private:
    // -----------------------------------------------------------------
    // 模块表条目（§8.2 ModuleDraftTable——会话态，UI 线程独占访问）
    // -----------------------------------------------------------------

    /// 单模块会话条目（生命周期＝模块挂接到显式摘除/会话解除）。
    struct ModuleEntry {
        /// 域草稿源（弱引用——所有权在域编辑器，§10.5 所有权行）。
        IModuleDraftSource* source = nullptr;
        /// 显示名（挂接时刻定格——UX-02 呈现值）。
        std::string displayName;
        /// 会话脏代次（0＝干净；notifySessionDirty 递增——"save 期间的
        /// 新脏进入下一周期"的判定锚，§8.4）。
        std::uint64_t dirtySerial = 0;
        /// 局部撤销栈（检查点文档值——§8.7 会话栈；随会话销毁不持久化）。
        std::vector<DraftDocumentProjection> undoStack;
        /// 局部重做栈（与撤销栈对称；新检查点清空）。
        std::vector<DraftDocumentProjection> redoStack;
        /// StaleRevisionRejected 冲突未决位（§8.5——onCommandResult 置位，
        /// 决议/应用成功清除）。
        bool staleConflictPending = false;
        /// 冲突定位数据（命令结果附带——对话框数据源）。
        StaleRevisionDetailProjection staleDetail;
        /// 本会话最近保存 origin token（空＝本会话未保存过）。
        std::string lastSaveOriginToken;
        /// 本会话最近保存时刻（ISO-8601 UTC）。
        std::string lastSavedAtUtc;
        /// 最近应用的修订（canonical——呈现/审计轨迹；应用后草稿已消费）。
        std::string lastAppliedRevisionCanonical;
    };

    // -----------------------------------------------------------------
    // 内部助手（单一职责小块；全部 UI 线程）
    // -----------------------------------------------------------------

    /// 校验会话已绑定（未绑定＝调用次序违约——fail-fast）。
    void requireSession(const char* entry) const
    {
        if (!m_binding.has_value()) {
            throw std::logic_error(
                std::string("ui/draft: ") + entry + " 未绑定会话"
                "（bindSession 前置——§5.2 打开协议⑤步后）");
        }
    }

    /// 校验会话已绑定且可写（只读/未绑定＝§5.5 契约违约——fail-fast；
    /// 静默返回全败结局会制造"保存被拒是常态"的假象，比异常更糟）。
    void requireWritableSession(const char* entry) const
    {
        requireSession(entry);
        if (!m_binding->writable) {
            throw std::logic_error(
                std::string("ui/draft: ") + entry
                + " 只读会话拒绝（§5.5 draft.save 为写操作禁用——"
                  "CloseDialogData.saveDraftsAvailable=false 时呈现层不得"
                  "给出保存入口；编程调用属契约违约）");
        }
    }

    /// 取模块条目（未知句柄＝路由违约——fail-fast，不静默忽略）。
    ModuleEntry& requireModule(const ModuleDraftHandle& handle, const char* entry)
    {
        const auto it = m_modules.find(handle);
        if (it == m_modules.end()) {
            throw std::logic_error(
                std::string("ui/draft: ") + entry + " 未知模块句柄: " + handle);
        }
        return it->second;
    }

    /**
     * @brief 落盘前的文档契约校验（调用方＝域源——错误在 UI 线程
     *        fail-fast，不把半成品投进落盘线程）。
     *
     * moduleId 与挂接句柄一致（错位＝域源组装违约）；token 词法与
     * schemaVersion 非零（0＝未填写保留值——落盘 0 版本文档属装配
     * 缺陷，对端将按格式校验拒绝，提前拦截）。
     */
    void validateDocument(const ModuleDraftHandle& handle,
                          const DraftDocumentProjection& document) const
    {
        if (document.moduleId != handle || !isValidModuleToken(document.moduleId)) {
            throw std::invalid_argument(
                "ui/draft: 草稿文档 moduleId 与挂接句柄不一致或词法违约"
                "（域源组装契约）: 文档=" + document.moduleId + " 句柄=" + handle);
        }
        if (document.schemaVersion == 0) {
            throw std::invalid_argument(
                "ui/draft: 草稿文档 schemaVersion 未填写（0＝保留值——域"
                "组装方须携带格式版本，§4.4.5）: " + handle);
        }
    }

    /**
     * @brief 同步落盘（saveAll/discardDraft 的执行半区——分派到落盘
     *        线程后阻塞等待；§8.4"关闭流程 saveAll 在确认对话框上下文
     *        同步完成"原文，等待≠执行 IO，零磁盘 IO 红线不动）。
     *
     * 文档与端口按值捕获（在途存活期独立于会话绑定）；端口实现若以
     * 异常表达调用方违约（装配器缺陷——控制器已预检的契约在适配器内
     * 再度失败），异常经 promise 通道原样转回 UI 线程重抛（禁吞错，
     * fail-fast 语义不因线程边界弱化）。
     */
    DraftSaveOutcome runSaveBlocking(DraftDocumentProjection document)
    {
        std::shared_ptr<IUiDraftStorePort> store = m_binding->store;
        std::promise<DraftSaveOutcome> promise;
        std::future<DraftSaveOutcome> future = promise.get_future();
        m_deps.postToDiskThread([&promise, store, document = std::move(document)]() mutable {
            try {
                promise.set_value(store->save(document));
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        });
        return future.get();
    }

    /**
     * @brief 异步落盘分派（tickAutosave 的执行半区——分派即返回；完成
     *        回执经 postToUiThread Marshal 回 UI 线程消费，M-1 纪律）。
     */
    void dispatchAutosaveSave(const ModuleDraftHandle& handle,
                              DraftDocumentProjection document,
                              const std::uint64_t serial,
                              const std::string& savedAtUtc)
    {
        ++m_autosavesInFlight;
        std::shared_ptr<IUiDraftStorePort> store = m_binding->store;
        m_deps.postToDiskThread(
            [this, handle, document = std::move(document), serial, store, savedAtUtc]() mutable {
                try {
                    const DraftSaveOutcome save = store->save(document);
                    m_deps.postToUiThread([this, handle, serial, save, savedAtUtc]() {
                        consumeAutosaveResult(handle, serial, save, savedAtUtc);
                    });
                } catch (...) {
                    // 装配器缺陷路径：异常转回 UI 线程重抛（fail-fast），
                    // 在途计数在此收口（控制器状态不自锁）。
                    m_deps.postToUiThread([this, e = std::current_exception()]() {
                        --m_autosavesInFlight;
                        std::rethrow_exception(e);
                    });
                }
            });
    }

    /**
     * @brief autosave 完成回执消费（UI 线程——M-1 转回点）。
     *
     * 成功且期间无新编辑（代次未变）→清脏＋记录保存轨迹；期间有编辑
     * →保持脏（下一周期再存）；失败→保留脏＋UI-DRAFT-AUTOSAVE-FAILED
     * （§3.5 码表：用户可见 Warning，StoreError 详情透传）。模块已摘除
     * →只收口在途计数＋Dev 轨迹（域侧生命周期先行是合法编排）。
     */
    void consumeAutosaveResult(const ModuleDraftHandle& handle,
                               const std::uint64_t serial,
                               const DraftSaveOutcome& save,
                               const std::string& savedAtUtc)
    {
        --m_autosavesInFlight;
        const auto it = m_modules.find(handle);
        if (it == m_modules.end()) {
            emitDevLine("autosave completed after detach (dropped): module=" + handle);
            return;
        }
        ModuleEntry& entry = it->second;
        if (save.ok) {
            if (entry.dirtySerial == serial) {
                entry.dirtySerial = 0;
                entry.lastSaveOriginToken = draftOriginToken(DraftOrigin::Autosave);
                entry.lastSavedAtUtc = savedAtUtc;
            }
        } else {
            // 失败保留脏标记（§8.2 失败行——"定时草稿落盘失败（保留脏
            // 标记；StoreError 详情透传）"码表原文）；下一周期自动重试
            // 属分派语义（新周期重新收集），不是本回执的重试动作。
            emitAutosaveFailedDiagnostic(handle, save);
        }
        fireDirtyObserver();
    }

    /**
     * @brief UI-DRAFT-AUTOSAVE-FAILED 双通道出线（目录 Warning＋Dev 明文；
     *     空目录/空日志场景显式声明跳过——不虚构条目，UI-T11 同款纪律）。
     */
    void emitAutosaveFailedDiagnostic(const ModuleDraftHandle& handle,
                                      const DraftSaveOutcome& save)
    {
        emitDevLine("UI-DRAFT-AUTOSAVE-FAILED: module=" + handle
                    + " token=" + save.errorToken + " detail=" + save.detail);
        if (!m_deps.diagFactory || !m_deps.diagSink) {
            return;
        }
        // 产码经工厂唯一入口（码已随 uiDiagnosticCodeDescriptors 注册——
        // 未注册码被工厂拒绝，产码纪律与 UI-T11 实测同源）。
        const core::DiagnosticRecord record = core::DiagnosticRecord::make(
            "UI-DRAFT-AUTOSAVE-FAILED",
            core::ObjectId::generate(),
            std::nullopt,  // localName（模块级事件——域对象定位不适用）
            std::nullopt,  // runtimeName（同上）
            "自动保存草稿失败（模块 " + handle + "）——未保存标记已保留",
            "落盘被拒绝：" + save.errorToken
                + (save.detail.empty() ? std::string{} : "；" + save.detail),
            "检查磁盘空间与访问权限后手动保存；未保存数据未丢失",
            std::nullopt);  // 比较型三要素（非数值判定场景——缺省不携带）
        diagnostics::DiagContext context;
        context.sourceUnit = "ui";  // §4.2 sourceUnit 词表含 ui
        context.sourceInterface = "draft.autosave";
        context.project = m_binding.has_value()
            ? std::optional<core::ProjectId>(m_binding->projectId)
            : std::nullopt;
        m_deps.diagSink->append(m_deps.diagFactory->create(record, context));
    }

    /// Dev 日志（允许为空＝显式声明的无日志场景——UI-T11 同款）。
    void emitDevLine(const std::string& message) const
    {
        if (m_deps.devLog) {
            m_deps.devLog->logDev(kDraftDevChannel, message);
        }
    }

    /**
     * @brief anyDirty 翻转回调（anyUnappliedChanges 单一事实源——生产者
     *     接线：L5 把它接到会话控制器，未保存标记经上下文投影出线）。
     *  仅在翻转沿回调（重复 true/true 不重复通知——接线侧零抖动）。
     */
    void fireDirtyObserver()
    {
        const bool dirty = anyUnappliedChanges();
        if (m_deps.onSessionDirtyChanged && dirty != m_lastEmittedDirty) {
            m_lastEmittedDirty = dirty;
            m_deps.onSessionDirtyChanged(dirty);
        }
    }

    /// 装配依赖（构造后只读——执行器/诊断面运行期不变）。
    DraftControllerDeps m_deps;
    /// 会话绑定（无会话＝nullopt——hasSession 的唯一事实面）。
    std::optional<DraftSessionBinding> m_binding;
    /// 模块注册序（attach 序——汇总呈现序，NFR-COR-02）。
    std::vector<ModuleDraftHandle> m_order;
    /// 模块表（句柄→条目——§8.2 ModuleDraftTable 承载）。
    std::map<ModuleDraftHandle, ModuleEntry> m_modules;
    /// 定时保存周期（默认 60 s——PM-04-S1；可配）。
    std::chrono::seconds m_autosaveInterval{kDefaultAutosaveInterval};
    /// 上次 autosave 分派时刻（双触发防御锚；epoch＝从未分派）。
    std::chrono::steady_clock::time_point m_lastAutosaveDispatch{};
    /// 在途 autosave 计数（§8.4"同一时刻至多一个 save 在途"的控制器半区
    /// ——>0 时新 tick 跳过；变更只在 UI 线程）。
    std::size_t m_autosavesInFlight = 0;
    /// 翻转回调的上一值（沿触发锚）。
    bool m_lastEmittedDirty = false;
};

// =====================================================================
// 装配入口（§10.5——L5 应用壳唯一构造面）
// =====================================================================

std::unique_ptr<IDraftController> createDraftController(DraftControllerDeps deps)
{
    return std::make_unique<DraftController>(std::move(deps));
}

}  // namespace ui
}  // namespace ird
