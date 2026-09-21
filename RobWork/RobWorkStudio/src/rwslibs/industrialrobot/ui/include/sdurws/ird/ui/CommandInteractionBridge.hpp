/**
 * @file   CommandInteractionBridge.hpp
 * @brief  命令确认交互桥（§9.2）——IUiCommandInteraction 的 ui 实现：
 *         命令执行线程同步回调→Marshal 至 UI 线程弹确认对话→阻塞等待。
 *
 * 设计依据：
 *   - units/ui.md §9.2（确认流全节：线程调度/principal 采集/回调生命
 *     周期/未确认不提交/无超时/输入变化失效/关闭窗口流程——P-PR-7 交叉
 *     核对结论）、§9.3（确认对话数据契约——FindingRecord 投影→对话
 *     呈现映射）、§2.1.2 C-6（O-31 方向外翻：L5 适配器实现
 *     project::ICommandInteraction 并委托 ui 自有交互回调端口——本头
 *     即该端口的实现承载）、§3.4（线程模型：命令执行线程行＋M-1
 *     Marshal 纪律）、§3.5（文案键体系）；
 *   - project.md §5.3.3 冻结点（isAlive/requestConfirmations 语义——
 *     P-PR-7"对端单侧冻结互为起点不私改"；本实现的线程模型按该冻结
 *     点逐条兑现）；
 *   - diagnostics.md §5.3/§5.6（绑定四元组/关闭窗口失效——ui 侧只标注
 *     提示，权威判定在 project 编译前复核）；
 *   - 需求 SA-15（确认只在命令边界；ui 只交 credentials 不持判定权）、
 *     MDL-06④（确认不豁免编译）、UX-03；P-DIAG-6（对话无自动超时——
 *     findings 默认 expiresAt 空）；P-UI-4（principal＝会话启动采集的
 *     Windows 用户名缓存，不逐次弹问）；
 *   - 任务契约 tasks/foundation/UI-T13.json acceptance 2/5（UI-CMD-4~7
 *     ＋O-31/P-PR-7 处置）。
 *
 * 背景说明（线程模型的本质，见 §9.2 时序图）：requestConfirmations 在
 * **命令执行线程**被同步调用（project 命令槽内、占槽零事务资源）——
 * 桥把对话装配值 Marshal（queued）到 UI 线程，由注入的呈现器打开对话，
 * 命令线程在条件变量上**阻塞等待**用户决议；会话拆除（窗口关闭/项目
 * 切换）经 markSessionDismantled 置存活探针 false，等待被唤醒后返回
 * nullopt（对端按 Aborted(interaction-lost) 处置，无修订、无用户诊断）
 * ——**无永久等待**（拆除必然唤醒；无超时自动确认，P-DIAG-6）。
 *
 * 桥内红线（§9.2 类注释原文"桥内禁止调用命令/查询端口以外任何写入口
 * 防重入死锁"）：本实现只触碰确认对话呈现器与内部等待状态——零命令
 * 网关、零查询端口调用（重入 submit 即死锁：命令槽被本回调占用）。
 *
 * 头文件零 Qt（QObject/Marshal 半区全在实现 TU）；模型层可无 GUI 测试
 * （呈现器以替身注入——§3.1"ui 测试以可控替身承载"）。
 */

#ifndef SDURWS_IRD_UI_COMMANDINTERACTIONBRIDGE_HPP
#define SDURWS_IRD_UI_COMMANDINTERACTIONBRIDGE_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>        // core::ConfirmableFinding/ConfirmationCredential/ConfirmationState（表内登记边）
#include <sdurws/ird/core/Identity.hpp>        // core::ObjectId（作用对象列表——表内登记边）
#include <sdurws/ird/diagnostics/Catalog.hpp>  // diagnostics::IClock（confirmedAtUtc 时间注入——测试可替换）
#include <sdurws/ird/diagnostics/Confirmable.hpp>  // diagnostics::FindingRecord（补全值面——表内登记边）
#include <sdurws/ird/ui/UiPorts.hpp>           // IUiCommandInteraction（本头实现该端口——C-6 外翻承接）
#include <sdurws/ird/ui/UiTypes.hpp>           // TextKey（文案键别名——§3.5）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// §9.3 确认对话数据契约（ui 消费——装配值与决议值）
// =====================================================================

/**
 * @brief 确认对话单条目（§9.3"每条 finding"框——比较型三要素/作用
 *        对象/文案键/基线显示/失效标注的装配值）。
 *
 * 数据来源双层：core::ConfirmableFinding 必有（§5.3.3 冻结签名只传
 * core 发现）；FindingRecord 侧字段（subjectScope 列表/confirmTextKey/
 * optionKeys/baseRevision）经装配依赖的补全回调从"经 project 间接读"
 * 面取回（IUiFindingQueryPort 同源——§9.2 规则表"非命令期 pending
 * 查看"行同一条数据通路）；无补全回调时以 core 级数据呈现（标题/详情
 * 键按 diag.<code-lower>.* 码表键约定推导，选项用冻结的确认/拒绝二键）
 * ——零虚构（缺数据就不显示对应行，不编造）。
 */
struct ConfirmDialogItem {
    /// 底层发现（比较型三要素/subject/localName/context/cause/
    /// recommendedAction——主文区数据，core 契约类型直用）。
    core::ConfirmableFinding finding;
    /// 标题文案键（diag.&lt;code-lower&gt;.title——码表键约定 P-DIAG-9；
    /// GUI 经 UiText 解析，模型不解析不缓存）。
    TextKey titleKey;
    /// 详情文案键（diag.&lt;code-lower&gt;.detail——同上）。
    TextKey detailKey;
    /// 确认提示文案键（FindingRecord.confirmTextKey 补全；缺省＝
    /// ui.dlg.confirm.option.confirm——冻结通用确认键）。
    TextKey confirmTextKey;
    /// 选项文案键（按钮面；缺省＝[确认][拒绝]二键——SA-15 通用决议；
    /// 补全回调可给 FindingRecord.optionKeys 全集）。
    std::vector<TextKey> optionKeys;
    /// 作用对象列表（§9.3"subjectScope→影响对象列表"；无补全＝仅
    /// record.subject 单对象；空＝无对象行）。
    std::vector<core::ObjectId> subjectScope;
    /// 基线修订显示文本（§9.3"baseRevision 显示"；空＝不显示——补全
    /// 缺席时不虚构修订身份；显示形态由 GUI 决定，模型不拼接）。
    std::string baseRevisionCanonical;
    /// 输入变化标注（§9.2 时序图"对话标注'输入已变化'"——对话打开
    /// 前发生过修订提交时为真；提示不代替判定，权威失效由 project
    /// 编译前复核决定）。
    bool staleInputHint = false;
};

/**
 * @brief 确认对话装配值（§9.3"批量一次呈现（逐条选择/全部确认）"）。
 */
struct ConfirmDialogData {
    /// 条目集（一次命令一批——§9.2"无排队设施：不同命令的确认不共享
    /// callbackToken"；空集不会出现——桥对空 findings 直接返回）。
    std::vector<ConfirmDialogItem> items;
    /// 对话标题文案键（固定值 ui.dlg.confirm.title）。
    TextKey titleKey;
    /// 说明文案键（固定值 ui.dlg.confirm.instruction）。
    TextKey instructionKey;
    /// "确认不豁免校验"提示键（MDL-06④——固定值 ui.dlg.confirm.
    /// no-skip；确认≠成功，硬断言与双编译照常执行）。
    TextKey noSkipHintKey;
    /// 策略绑定提示键（§9.3"policyContentId→'策略版本'折叠提示（不
    /// 显示哈希原文，显示'已随当前策略版本绑定'）"——固定值）。
    TextKey policyBoundHintKey;
    /// "全部确认"动作键（批量条目 >1 时呈现——逐条选择的快捷面）。
    TextKey confirmAllKey;
    /// "输入已变化"横幅键（本批任一条目 staleInputHint 时呈现于批级）。
    TextKey staleInputKey;
    /// 确认主体显示（P-UI-4——会话启动采集缓存的 Windows 用户名；
    /// 呈现于对话页脚，用户可见"以谁的身份确认"）。
    std::string principal;
    /// Dev 折叠区行（§9.3"sourceCommandType/commandPayloadDigest→仅
    /// Dev 折叠区（默认隐藏，UX-02）"——行内容为补全回调提供的键/摘要
    /// 形态文本；空＝无折叠区。**永不进入用户主文区**——渲染归 GUI 的
    /// 折叠半区，默认收起）。
    std::vector<std::string> devFoldLines;
};

/**
 * @brief 对话决议类别（呈现器返回值——三态词表冻结）。
 */
enum class ConfirmDialogOutcome : std::uint8_t {
    Confirmed,  ///< 全部确认（用户意图已表达——凭据照常组装，四元组
                ///  有效性归 project 编译前复核，§9.2"已完成输入→照常放行"）
    Rejected,   ///< 拒绝（任一条目拒绝＝整体拒绝——对端按
                ///  Rejected(confirmations-rejected) 处置，无修订）
    Abandoned,  ///< 会话拆除中放弃（对话框被拆除路径关闭——与等待方
                ///  超先的 interaction-lost 同义，桥按 nullopt 处置）
};

/// 对话决议值。
struct ConfirmDialogResolution {
    ConfirmDialogOutcome outcome = ConfirmDialogOutcome::Abandoned;
};

// =====================================================================
// 确认对话呈现器端口（ui 内部呈现缝——GUI 半区随 §12.1 第三层落位）
// =====================================================================

/**
 * @brief 确认对话呈现器的 ui 内部端口（桥〔模型层〕与对话 Widget
 *        〔GUI 层〕的缝——§12.1 分层：机制半区模型可测，呈现半区归
 *        gui_test 层）。
 *
 * 线程契约：showConfirmDialog 只在 **UI 线程**被桥调用（Marshal 之后）；
 * 实现应模态至用户决议（GUI 半区典型形态＝窗口模态对话），或在会话
 * 拆除（窗口正在销毁）时尽快返回 Abandoned——呈现器应观察自身窗口的
 * 拆除状态，桥不轮询呈现器。
 */
class IUiConfirmDialogPresenter {
public:
    virtual ~IUiConfirmDialogPresenter() = default;

    /**
     * @brief 打开确认对话并等待决议（UI 线程；模态至决议/拆除）。
     *
     * @param data [in] 对话装配值（值拷贝——呈现器不留引用）
     * @return 决议（用户关闭/取消＝Rejected——未确认不提交）
     */
    virtual ConfirmDialogResolution
    showConfirmDialog(const ConfirmDialogData& data) = 0;

    /**
     * @brief 对话打开期收到修订提交（§9.2 时序图"若等待期收到
     *        RevisionCommitted：对话标注'输入已变化'"的实时半区——
     *        GUI 据此在已打开的对话上追加标注；无打开对话时为 no-op）。
     */
    virtual void noteInputChanged() = 0;
};

// =====================================================================
// CommandInteractionBridge（§9.2——IUiCommandInteraction 的 ui 实现）
// =====================================================================

/**
 * @brief 命令确认交互桥（§9.2 类块原文的落位实现——C-6 外翻后实现
 *        ui 自有 IUiCommandInteraction 端口；确认对话/principal 采集/
 *        Marshal 语义零变化）。
 *
 * 生命周期：L5 装配期在 **UI 线程**构造（Marshal 上下文在构造线程
 * 建立）；会话拆除时调 markSessionDismantled（对端此后经 isAlive 探针
 * 得 false——Aborted(interaction-lost)）。对端（project）在回调期间持
 * 实现对象强引用的装配契约由 L5 保证（§5.3.3 原文）。
 *
 * 线程模型（§9.2 类注释四步的兑现）：
 *   ① requestConfirmations（命令执行线程）装配对话值→
 *   ② QMetaObject 排队投递到 UI 线程，呈现器打开对话（模态）→
 *   ③ 命令线程在条件变量上阻塞等待（占命令槽、零事务资源、无超时——
 *      P-DIAG-6）→
 *   ④ markSessionDismantled 置探针 false 并唤醒等待→返回 nullopt
 *      （Aborted(interaction-lost)）；若用户已先完成决议，结果优先
 *      （"用户意图已表达且输入未变→照常放行"，§9.2 关闭窗口行）。
 */
class CommandInteractionBridge final : public IUiCommandInteraction {
public:
    /// FindingRecord 补全回调（§9.3 双层数据的取回缝——可空；实现方
    /// 典型形态＝经 IUiFindingQueryPort 同源的"经 project 间接读"面按
    /// 发现匹配记录；ui 测试以替身注入）。
    using FindingEnricher =
        std::function<std::optional<diagnostics::FindingRecord>(
            const core::ConfirmableFinding&)>;

    /**
     * @brief 装配依赖（L5 装配期注入；指针/共享引用非 owning——生存期
     *        须覆盖桥）。
     */
    struct Deps {
        /// 确认对话呈现器（**必注入**——无呈现器的桥无法履行确认流，
        /// 工厂 fail-fast）。
        std::shared_ptr<IUiConfirmDialogPresenter> presenter;
        /// 时钟（可空——confirmedAtUtc 的测试注入；空＝system_clock 直通）。
        const diagnostics::IClock* clock = nullptr;
        /// FindingRecord 补全回调（可空——见 ConfirmDialogItem 注释；
        /// 空＝core 级数据呈现，缺行不虚构）。
        FindingEnricher enrich = nullptr;
        /// principal 采集器（可空——测试注入固定主体；空＝Windows
        /// 用户名采集〔P-UI-4 建议口径〕，采集失败降级"unknown-user"）。
        std::function<std::string()> principalProvider = nullptr;
    };

    /**
     * @brief 构造（UI 线程；principal 会话启动采集缓存一次——不逐次
     *        弹问，P-UI-4）。
     *
     * @param deps [in] 装配依赖（presenter 空→抛 std::invalid_argument）
     */
    explicit CommandInteractionBridge(Deps deps);

    /**
     * @brief 析构（实现文件定义——Marshal 上下文为私有嵌套类，
     *         unique_ptr 成员的删除器须在完整类型可见处实例化）。
     */
    ~CommandInteractionBridge() override;

    /// 不可拷贝/不可移动（Marshal 上下文与等待注册表绑定 this 语义）。
    CommandInteractionBridge(const CommandInteractionBridge&) = delete;
    CommandInteractionBridge& operator=(const CommandInteractionBridge&) = delete;

    // ---- IUiCommandInteraction（§5.3.3 冻结签名的镜像——C-6 端口）----

    /**
     * @brief 会话存活探针（任意线程；原子读——§9.2"isAlive()：绑定
     *        UiSessionController 存活探针……拆除时置 false"）。
     */
    [[nodiscard]] bool isAlive() const noexcept override;

    /**
     * @brief 请求用户确认（命令执行线程同步回调——线程模型见类注释；
     *        空集 findings→空凭据向量：无事可确认，防御性放行不产生
     *        对话）。
     *
     * @param findings [in] 待确认集（比较型可确认诊断）
     * @return 凭据向量（全部确认——principal＝会话缓存，confirmedAtUtc
     *         ＝组装时刻）；nullopt＝整体拒绝/会话失效
     */
    std::optional<std::vector<core::ConfirmationCredential>>
    requestConfirmations(
        const std::vector<core::ConfirmableFinding>& findings) override;

    // ---- 会话拆除与失效标注（ui 侧驱动面——L5/会话控制器接线）----

    /**
     * @brief 会话拆除通知（窗口关闭/项目切换的 Draining 之后调用——
     *        置存活探针 false＋唤醒全部在等命令线程；此后 isAlive 恒
     *        false，对端主动取消路径由此触发，§5.6-5）。
     */
    void markSessionDismantled() noexcept;

    /**
     * @brief 修订提交通知（等待期收到 RevisionCommitted 时由投影管线
     *        调用——已打开的对话经呈现器实时标注"输入已变化"；此后新
     *        打开的对话带失效标注——"凭据失效提示重开对话"，§9.2；
     *        标注不代替判定，权威复核在 project）。
     */
    void noteRevisionCommitted();

    /**
     * @brief 会话缓存的确认主体（P-UI-4——构造时采集一次；呈现/日志
     *        用，确认判定不依赖该值）。
     */
    const std::string& principal() const noexcept { return m_principal; }

private:
    /// Marshal 上下文（实现 TU 内定义——QObject 子类，前置声明即可）。
    class MarshalContext;
    /// 单次等待的共享状态（实现 TU 内定义——互斥/条件变量/一次性结果）。
    struct PendingWait;

    /// 组装凭据（全部确认路径——confirmedAtUtc 由组装时刻填充，§9.2
    /// principal 行原文）。
    std::vector<core::ConfirmationCredential>
    assembleCredentials(std::size_t count) const;

    Deps m_deps;                          ///< 装配依赖（呈现器 shared 持有）
    std::unique_ptr<MarshalContext> m_context;  ///< Marshal 上下文（构造线程＝UI 线程）
    std::string m_principal;              ///< 会话缓存确认主体（P-UI-4）
    std::atomic<bool> m_alive{true};      ///< 存活探针（任意线程原子读——§9.2）
    std::uint64_t m_revisionEpoch{0};     ///< 修订代次（noteRevisionCommitted 递增——新对话失效标注判别）
    std::mutex m_pendingMutex;            ///< 在等注册表互斥（markSessionDismantled 唤醒面）
    std::vector<std::shared_ptr<PendingWait>> m_pending;  ///< 在等命令线程集
};

// =====================================================================
// 确认对话装配纯函数（§9.3 映射的唯一权威实现——模型层可测）
// =====================================================================

/**
 * @brief 装配确认对话数据（§9.2 时序图"对话装配"框＋§9.3 映射表）。
 *
 * 装配规则（逐字段）：
 *   - 每条 finding 一条目：titleKey/detailKey＝diag.&lt;code-lower&gt;.
 *     .title/.detail（码表键约定——键不解析，GUI 经 UiText）；三要素
 *     呈现文本由呈现层以 formatComparison（IDiagnosticPresentation
 *     Model.hpp）渲染——本函数只搬数据；
 *   - subjectScope/confirmTextKey/optionKeys/baseRevision：enrich 命中
 *     FindingRecord 时取记录值；未命中/未注入→core 级缺省（record.
 *     subject 单对象＋冻结确认/拒绝二键＋不显示基线）——零虚构；
 *   - staleInputHint：revisionCommittedSince > 0（对话打开前发生过
 *     修订提交）——批级横幅＋条目标注；
 *   - Dev 折叠区：enrich 命中时以"command-type=<token>"行承载来源
 *     命令类型（载荷摘要为哈希形态——永不进入用户文本，UX-02，
 *     故折叠区只呈现命令类型 token，不呈现摘要内容）。
 *
 * @param findings [in] 待确认集（原样搬入）
 * @param principal [in] 会话缓存确认主体（页脚显示）
 * @param revisionCommittedSince [in] 本批 findings 创建后是否发生过
 *                                 修订提交（桥按代次判别后传入）
 * @param enrich [in] FindingRecord 补全回调（可空）
 * @return 对话装配值（items 与 findings 一一对应——顺序保持）
 */
ConfirmDialogData assembleConfirmDialogData(
    const std::vector<core::ConfirmableFinding>& findings,
    const std::string& principal,
    bool revisionCommittedSince,
    const CommandInteractionBridge::FindingEnricher& enrich);

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_COMMANDINTERACTIONBRIDGE_HPP
