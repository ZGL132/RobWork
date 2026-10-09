/**
 * @file   Settings.hpp
 * @brief  用户级设置存储（PM-14——§7.7）：最近项目／包导出默认勾选／
 *         上次打开目录／命令面板快捷键绑定的持久化载体与存储面。
 *
 * 设计依据：
 *   - units/workflow.md §7.7（用户设置持久化 PM-14——持有内容四项、
 *     "用户目录、不入 .rwdesign；JSON canonical 经 io IJsonWriter"、
 *     "不混入：求解配置等分析设置独立持久化（KIN-13），本单元设置存储
 *     不含任何分析配置字段（I-WF-5）"）、§10.1/§10.2（IUserSettingsStore
 *     接口总表行与 Draft 签名——load/store 两方法逐字兑现）、§10.3
 *     （接口属性表：UserSettings schema "user-settings/1"；错误语义＝
 *     调用方错误 fail-fast WorkflowError；会话内单线程）、§14.3 P-WF-5
 *     （容量/脱敏参数未冻结——上限 10 实现冻结＋脱敏零行为安全默认）
 *   - REQUIREMENTS.md §17 PM-14 原文（"用户设置持久化：最近项目、包导出
 *     默认勾选、上次打开目录（用户级设置，不入 .rwdesign）；求解配置等
 *     分析设置独立持久化（KIN-13），不混入本条"）
 *   - REQUIREMENTS.md KIN-13 原文（"分析求解配置……独立于用户级设置
 *     （PM-14）持久化"——两存储的分离即 I-WF-5 的验收面）
 *   - 任务契约 tasks/foundation/WP-22-T10.json acceptance 1~3（用户级
 *     设置持久化于用户目录不入 .rwdesign；求解配置等分析设置独立持久化
 *     不混入——设置 schema 零分析配置字段用例通过；设置文件损坏给诊断
 *     不崩溃；容量/脱敏参数（P-WF-5）未冻结按安全默认＋留痕）
 *   - units/io.md §5.9（JSON 受限读写器——canonical 写出／版本判定先于
 *     schema 校验／未知键 Reject）、§9.5（IJsonWriter/IStructuredDataReader
 *     契约表）；units/io.md §4.6（原子写出——write 文件目标＝同目录暂存
 *     ＋rename 原子替换，失败目标不变）
 *   - units/ui.md §7.3/§10.4（GlobalShortcutRegistry——注册权威与冲突
 *     拒绝归 ui；PortableText 规范词形；User 绑定集回调载荷＝当前 User
 *     绑定全集。本头只提供**存储面**：绑定数据经 ui HotkeyBindingTable
 *     权威设施写入，本单元不复制注册/冲突判定语义——PA-1）
 *   - Lifecycle.hpp（同单元：PackageSelectionFlags——包导出/另存共用
 *     勾选词表与 defaultSelectionOf 记忆解析；kRecentProjectsCapacity
 *     ——PM-10 冻结上限 10；RecentProjectEntry——最近项目条目事实面）
 *
 * 背景说明（为什么设置存储是 workflow 的编排面而非第五存储单元）：
 * PM-14 的四项内容全部是**生命周期流程的会话便利状态**——最近项目服务
 * （T05 已落位 IRecentProjectsService，本头持久化其列表事实）、包导出/
 * 另存向导的勾选记忆（T07 SaveAsOutcome.selection 回传的登记面）、打开
 * 对话框的初始目录、快捷键改绑（ui 权威设施的落盘半区）。它们不属于
 * 任何业务域（零工程语义），但被生命周期流程统一消费——归 workflow
 * 持有存储面（§7.7"本单元设置存储"）。
 *
 * 与 .rwdesign 的边界（PM-14 原文"不入 .rwdesign"）：本存储的文件路径
 * 由装配层注入（缺省＝defaultUserSettingsFilePath() 的用户目录解析——
 * Windows %APPDATA% 下产品自有目录），与项目目录（.rwdesign）零交集；
 * 结构性保证＝本存储面不接收任何项目存储上下文（签名零 store 参数，
 * 同 T05 打开编排"不动当前项目"的手法）。
 *
 * 与 KIN-13 分析配置的分离（I-WF-5，acceptance 2）：UserSettings 的
 * schema 字段集**封闭**（profile unknownKeyPolicy=Reject——NFR-DEP-04
 * schema 演进走版本升级，不允许静默吞字段）且**零分析配置字段**（初值
 * 策略/迭代上限/求解容差/去重阈值/采样预算等 KIN-13 词形一律不在本
 * schema——WF-VER-221 用例以"冻结键集精确断言＋未知键注入拒绝"双向
 * 钉住）。求解配置的持久化通道归 kinematics 详设（KIN-13 行"独立持久
 * 化"），本存储不为其预留任何旁路。
 *
 * P-WF-5 处置（acceptance 3）：需求只冻结了最近项目上限 10
 * （kRecentProjectsCapacity）；其余容量/脱敏参数未冻结——本存储按安全
 * 默认执行：容量恒为需求冻结值 10（超出部分截断保留最近使用的 10 条，
 * 与 LRU 溢出淘汰同语义，截断事实经开发诊断留痕）；路径**零脱敏**
 * （原样记录——NFR-SEC-07"按配置脱敏"的配置归属未定，不发明数值；
 * 裁决后归 diagnostics/ui 呈现侧配置面，本卡 §7.7 增量同步）。
 *
 * 快捷键绑定的存储面边界（PA-1）：绑定数据的**权威**在 ui
 * GlobalShortcutRegistry（注册边界/冲突拒绝/解绑语义——SA-16），本存储
 * 只持久化其 User 绑定全集的**词形快照**：命令 id 词面＋QKeySequence
 * PortableText 规范文本（ui 侧 canonicalKeyText 的产物词形，L5 装配层
 * 转换）。键序列语义（修饰键书写/大小写收敛）归 ui lookup——本存储
 * 零键序解析知识，仅透明存取字符串对。
 *
 * 错误语义（§10.3）：
 *   - 调用方错误 fail-fast（WorkflowError）：构造时文件路径为空；
 *     store() 写出失败（io 错误原样透传入异常消息——对端 detail 零加工，
 *     D-WF-7 零新增码）。store 不吞错：设置写失败被静默吞掉＝用户以为
 *     已记忆下次却丢失，违反"禁止吞错"纪律；
 *   - 环境错误走诊断＋安全缺省：load() 遇损坏文件（语法/版本/schema
 *     校验失败）＝**登记开发诊断并返回缺省默认值**（设置是会话便利
 *     状态，损坏不应阻断启动——PM-14"损坏给诊断不崩溃"契约 acceptance 3；
 *     诊断通道＝diagnostics::IDevLogSink 注入，Dev 级不入用户目录——
 *     损坏告知属开发/支持侧事实，不产用户级稳定码，R1 零新增 WF- 码）。
 *
 * 线程约束：会话内单线程（§10.3——宿主/装配层单线程访问，非线程共享；
 * 与 RecentProjectsService 同款纪律）。
 */

#ifndef SDURWS_IRD_WORKFLOW_SETTINGS_HPP
#define SDURWS_IRD_WORKFLOW_SETTINGS_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/diagnostics/Catalog.hpp>  // diagnostics::IDevLogSink（损坏诊断通道——Dev 级日志，注入可空）
#include <sdurws/ird/workflow/Lifecycle.hpp>   // workflow::PackageSelectionFlags（包导出/另存共用勾选词表——T07 同源，零重复定义）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// schema 词形与冻结键集（user-settings/1——§10.3 接口属性表版本行）
// =====================================================================

/// 设置 schema 标识（io JsonProfile 注册标识；格式所有者＝workflow）。
inline constexpr const char* kUserSettingsProfileId = "user-settings/1";

/// 设置 schema 版本（JSON "schemaVersion" 字段值；升级＝版本演进，不在
/// 本值就地改义——NFR-DEP-04）。
inline constexpr std::int64_t kUserSettingsSchemaVersion = 1;

// ---- 顶层冻结键集（canonical 声明序；I-WF-5 的 schema 边界载体）----
/// 版本字段键（io 版本判定先于一切 schema 校验——io.md §5.9.2）。
inline constexpr const char* kUserSettingsKeySchemaVersion = "schemaVersion";
/// 最近项目列表键（LRU 序字符串数组——首＝最近使用）。
inline constexpr const char* kUserSettingsKeyRecentProjects = "recentProjects";
/// 包导出默认勾选键（可选对象——缺失＝无记忆，load 侧 nullopt）。
inline constexpr const char* kUserSettingsKeyPackageSelection = "packageExportSelection";
/// 上次打开目录键（字符串——空串＝无记录）。
inline constexpr const char* kUserSettingsKeyLastOpenDirectory = "lastOpenDirectory";
/// 快捷键绑定集键（User 绑定全集词形快照——ui 权威设施的落盘半区）。
inline constexpr const char* kUserSettingsKeyHotkeyBindings = "hotkeyBindings";

/// 损坏/越界诊断的开发日志通道 token（diagnostics §7.2 LogChannel 词形
/// "workflow/<域>"；Dev 级不入用户诊断目录——§6.2 分流）。
inline constexpr const char* kUserSettingsDevChannel = "workflow/user-settings";

// =====================================================================
// 设置值对象（UserSettings——存储 schema 的内存映像，纯值类型）
// =====================================================================

/**
 * @brief 一条快捷键绑定记录（ui HotkeyBinding 的**词形快照**——存储面）。
 *
 * 两个字符串字段均为透明透传（本存储零键序/命令语义知识——PA-1）：
 *   - commandId＝ui CommandId 词面（已注册命令 id；命令跨版本消失时由
 *     ui restoreUserBindings 侧丢弃——§10.4"命令已不存在……的条目丢弃"，
 *     本存储不预过滤——历史数据如实存取）；
 *   - keyPortableText＝QKeySequence PortableText 规范文本（ui 侧
 *     canonicalKeyText 产物，如 "Ctrl+Alt+C"）；**空串＝解绑登记**
 *     （用户显式解绑了该命令的默认键——空串经 L5 装配层映射回
 *     rebind(id, nullopt) 语义；不持久化解绑登记会令默认键在下次启动
 *     复活，违背用户意图）。
 *
 * 线程安全：纯值类型，并发只读。
 */
struct HotkeyBindingRecord {
    /// 命令 id 词面（ui CommandId——透明透传，非空约定由 ui 权威侧保证）。
    std::string commandId;
    /// 键序列 PortableText 规范词形（空串＝解绑登记——见类型注）。
    std::string keyPortableText;

    bool operator==(const HotkeyBindingRecord& o) const noexcept
    {
        return commandId == o.commandId && keyPortableText == o.keyPortableText;
    }
    bool operator!=(const HotkeyBindingRecord& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 用户级设置值（PM-14 四项持有内容的内存映像——schema
 *        "user-settings/1" 的唯一业务承载）。
 *
 * ★ I-WF-5 声明（acceptance 2）：本结构的字段集**封闭且零分析配置**
 * ——不存在任何 KIN-13 词形（初值策略/迭代上限/求解容差/去重阈值/
 * 采样预算/线程数等）字段；求解配置的持久化独立走其详设通道（KIN-13
 * "独立于用户级设置持久化"）。字段集的封闭性由 io profile 的
 * unknownKeyPolicy=Reject 双向保证（写侧只产冻结键、读侧拒绝一切未
 * 声明键——分析配置字段无论从哪一侧都进不了本存储）。
 *
 * 字段语义与缺省值（即 load 的缺省默认值——首次使用零配置可运行）：
 *   - recentProjects：最近项目规范路径（LRU 序，首＝最近使用；去重与
 *     上限维护归 RecentProjectsService 会话面，本存储只持久化列表事实；
 *     持久化值超上限时 load 侧防御性截断——P-WF-5 安全默认）；
 *   - packageExportSelection：包导出/另存的勾选记忆（nullopt＝无记忆
 *     ——defaultSelectionOf 解析为全选缺省；有值＝上次确认勾选原样）；
 *   - lastOpenDirectory：上次打开目录词面（打开对话框初始目录；空串＝
 *     无记录）；
 *   - hotkeyBindings：User 绑定全集词形快照（空＝用户未改绑——全部
 *     使用 ui 装配默认）。
 *
 * 线程安全：纯值类型，并发只读。
 */
struct UserSettings {
    /// 最近项目规范路径（LRU 序——首＝最近使用；上限 kRecentProjectsCapacity）。
    std::vector<std::string> recentProjects;
    /// 包导出/另存勾选记忆（nullopt＝无记忆——PM-05"记忆默认"的存储半区）。
    std::optional<PackageSelectionFlags> packageExportSelection;
    /// 上次打开目录（UTF-8 词面；空串＝无记录——打开对话框回退系统缺省）。
    std::string lastOpenDirectory;
    /// 快捷键 User 绑定全集（词形快照——语义权威归 ui，见 HotkeyBindingRecord 注）。
    std::vector<HotkeyBindingRecord> hotkeyBindings;

    bool operator==(const UserSettings& o) const
    {
        return recentProjects == o.recentProjects
            && packageExportSelection == o.packageExportSelection
            && lastOpenDirectory == o.lastOpenDirectory
            && hotkeyBindings == o.hotkeyBindings;
    }
    bool operator!=(const UserSettings& o) const { return !(*this == o); }
};

// =====================================================================
// 存储接口（§10.2 Draft 签名逐字——IUserSettingsStore）
// =====================================================================

/**
 * @brief 用户级设置存储接口（§10.2 Draft 签名——PM-14 的存储权威面）。
 *
 * 两方法契约（§10.2 注释逐字）：
 *   - load()：返回当前设置；文件不存在＝首次使用，返回**缺省默认值**
 *     （零诊断——缺文件不是错误）；文件损坏＝**给诊断不崩溃**（经构造
 *     注入的 IDevLogSink 登记 Dev 诊断后返回缺省默认值——实现方契约）；
 *   - store()：原子写出（io 写出器文件目标＝同目录暂存＋rename 原子
 *     替换——write 成功＝目标完整就位，失败/中断＝先前文件原样保留，
 *     io.md §4.6）；JSON canonical 经 io IJsonWriter（同语义同字节）。
 *
 * 线程约束：会话内单线程（§10.3——非线程共享对象）。
 */
class IUserSettingsStore {
public:
    virtual ~IUserSettingsStore() = default;

    /**
     * @brief 读取用户设置（损坏安全——任何失败路径都返回可用默认值）。
     * @return 当前设置（文件缺失/损坏→缺省默认值；损坏时实现方经诊断
     *         通道留痕——本接口无异常路径，调用方零 try 负担）
     */
    virtual UserSettings load() const = 0;

    /**
     * @brief 原子写出用户设置（canonical JSON——同语义同字节）。
     * @param settings [in] 待写出的设置值（只读——本方法零改写入参）
     *
     * @throws WorkflowError 文件路径空（构造已挡——防御性再断言）或 io
     *         写出失败（磁盘满/权限/父目录不可建——io 错误码与 detail
     *         原样透传入异常消息，零吞错；先前文件经原子替换语义原样
     *         保留，不因本次失败损坏）
     */
    virtual void store(const UserSettings& settings) = 0;
};

// =====================================================================
// 标准实现（io JSON 通道装配——canonical 写出＋profile 校验读入）
// =====================================================================

/**
 * @brief 用户级设置存储的标准实现（io JsonProfile "user-settings/1"
 *        格式所有者；PM-14 四项内容的 canonical JSON 持久化）。
 *
 * 装配形态：文件路径由 L5 装配层注入（缺省＝defaultUserSettingsFilePath()
 * 的用户目录解析——生产装配用缺省路径，测试注入临时目录路径实现真实
 * 落盘隔离）；IDevLogSink 注入可空（空＝损坏诊断静默——仅测试/无日志
 * 场景，生产装配必须注入以兑现"损坏给诊断"）。
 *
 * 持久化位置纪律（PM-14"不入 .rwdesign"）：本类不接收任何项目存储
 * 上下文（签名零 store 参数——结构性保证）；路径语义由调用方承诺为
 * 用户级目录（缺省工厂已保证），本类不复核路径归属（路径校验归调用方
 * 装配决策——复核会复制调用方知识，PA-1）。
 *
 * JSON 形状（canonical——profile 声明序即写出序；2 空格缩进＋LF＋
 * UTF-8 无 BOM，io §5.9.3）：
 * @code
 * {
 *   "schemaVersion": 1,
 *   "recentProjects": ["D:/projects/a", "D:/projects/b"],
 *   "packageExportSelection": {
 *     "includeResults": true,
 *     "includeReports": false,
 *     "includeDrafts": true
 *   },
 *   "lastOpenDirectory": "D:/projects",
 *   "hotkeyBindings": [
 *     { "commandId": "wf.example", "key": "Ctrl+Alt+E" }
 *   ]
 * }
 * @endcode
 * packageExportSelection 缺失＝无记忆（nullopt）；其余四键恒写出
 * （空列表/空串也是事实——"写即完整快照"）。
 */
class UserSettingsStore final : public IUserSettingsStore {
public:
    /**
     * @brief 构造存储（绑定目标文件与诊断通道）。
     *
     * @param filePath [in] 设置文件路径（用户级目录——缺省工厂
     *                 defaultUserSettingsFilePath()；父目录可不存在——
     *                 store 侧自持创建。非空为前置）
     * @param devLog   [in] 开发日志通道（损坏/越界诊断的登记面；可空＝
     *                 静默——诊断语义见类注"错误语义"段）
     *
     * @throws WorkflowError filePath 为空路径（无文件可绑定的存储没有
     *         存在意义——调用方契约违约，fail-fast）
     */
    explicit UserSettingsStore(std::filesystem::path filePath,
                               diagnostics::IDevLogSink* devLog = nullptr);

    UserSettings load() const override;
    void store(const UserSettings& settings) override;

    /// 绑定的设置文件路径（只读访问——测试/装配自检面）。
    const std::filesystem::path& filePath() const noexcept { return m_filePath; }

private:
    std::filesystem::path m_filePath;   ///< 设置文件路径（构造后不变）
    diagnostics::IDevLogSink* m_devLog; ///< 开发日志通道（非拥有——调用方保证生命周期覆盖本存储；可空）
};

/**
 * @brief 缺省用户设置文件路径（生产装配的缺省绑定值——用户目录解析）。
 *
 * 解析规则（跨平台词面；产品运行面＝Windows）：
 *   - Windows：`%APPDATA%/industrialrobot/user-settings.json`
 *     （APPDATA 环境变量非空时）；
 *   - 其他平台：`$HOME/.industrialrobot/user-settings.json`
 *     （HOME 环境变量非空时）；
 *   - 两者皆不可得：返回空路径（装配层应注入显式路径替代——构造空
 *     路径即 fail-fast，不存在"静默落到当前目录"的隐式行为）。
 *
 * 目录段词形说明：产品自有目录段＝industrialrobot（与 .rwdesign 项目
 * 目录零交集——PM-14；产品面源码零 RobWork 字面量是 R-4 红线扫描的
 * 登记口径，目录段命名同守该口径）。
 *
 * 目录段不代建（构造/本函数零盘上副作用——首次 store 时自持创建）。
 * 返回词面为 UTF-8 窄串经 path 构造（系统 API 原生编码由环境变量保证
 * ——APPHOME 家族变量在 Windows 为 ANSI 代码页词面，std::filesystem::
 * path 窄串构造按执行字符集解释，与 getenv 语义一致）。
 *
 * @return 缺省设置文件路径（见解析规则；可能为空——调用方须处理）
 */
std::filesystem::path defaultUserSettingsFilePath();

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_SETTINGS_HPP
