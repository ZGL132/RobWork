/**
 * @file   UiTypes.hpp
 * @brief  ui 单元公共值类型词表——命令 id／文案键／菜单路径／阶段词表等
 *         跨头共享的基础类型（§3.3 公共头布局登记的 UiTypes.hpp 落位）。
 *
 * 设计依据：
 *   - units/ui.md §3.3（公共头布局："UiTypes.hpp＝七态词表、StageId、
 *     CommandId/TextKey、UiSessionState 等本单元公共值类型"——本头为该行
 *     的首个落位（UI-T06），当时仅承载命令设施所需的词表别名；七态词表已
 *     随 UI-T04 以"首消费冻结"机制落位 UiProjections.hpp（§16.7 v0.6），
 *     其余条目随归属任务增量落位，不预建——NFR-MNT-04）；
 *   - §7.1（命令项模型：CommandId＝"点分小写，全局唯一"；TextKey＝§3.5
 *     文案键体系；MenuPath＝菜单与面板分组）；
 *   - UI-T09 增量（§16.7 v1.1）：阶段词表与 StageStatusModel 汇聚投影值
 *     类型落位本头（§3.3 "StageId 等本单元公共值类型"行的首消费兑现）——
 *     StageId/StageViewStatus/UiStateToken（§10.2/§6.4）与
 *     DomainReadinessItem/StageReadinessSnapshot（§6.5 单侧冻结形状）。
 *     落位于此而非 IStageNavigationModel.hpp 的原因：C-12 门控端口
 *     （UiPorts.hpp 的 IUiStageGate）与导航接口（IStageNavigationModel.hpp）
 *     双方都要消费这些值类型，公共值类型必须有双方都能 include 的第三头，
 *     否则两接口头互相 include 成环（§3.3 布局红线——UiTypes 即该第三头）；
 *   - NFR-MNT-03（词表唯一权威：本头是这些别名的唯一定义点，各公共头
 *     include 本头取用，禁止第二处重复定义）。
 *
 * 背景说明（为什么是别名而不是强类型）：命令 id／文案键在本单元内是
 * **呈现层标识**——它们的唯一性/词法约束由注册边界校验（§7.2 id 句法
 * 校验）与键命名约定（§3.5 "cmd.<id>.title" 族）承载，不参与内容寻址/
 * 身份计算（与 core 五类 Id128 强类型的场景不同）。std::string 别名把
 * 校验责任留给唯一的注册入口（CommandRegistry::registerCommand），
 * 避免"强类型＋宽松转换路径"的双权威。StageId/StageViewStatus 则是
 * enum class 强类型——它们是跨单元（workflow 消费 §6.5 快照）的投影
 * 词表，拼写错误必须在编译期拦截。
 *
 * 线程安全：纯值类型（并发只读安全）。
 */

#ifndef SDURWS_IRD_UI_UITYPES_HPP
#define SDURWS_IRD_UI_UITYPES_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>  // core::EngineeringStatus（§6.5 verdict 字段——表内登记边直用 core 词表，不镜像枚举）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 命令设施词表（§7.1 命令项模型——UI-T06 首消费落位）
// =====================================================================

/**
 * @brief 命令 id（§7.1 原文："点分小写，全局唯一"）。
 *
 * 词法（§7.2 注册协议第 1 步校验；词形权威＝§7.1 冻结表原文——表内
 * workbench.commandPalette／view.displayMode／view.resetHome 等 id 段含
 * 大写，"点分小写"按冻结词形落位为"点分段＋段字符 [A-Za-z0-9-]＋必含
 * 点"，措辞差登记 ui.md §16.7 v0.8）。全局唯一由注册边界拒绝重复
 * （§7.2 第 2 步——UI-CMD-DUPLICATE）保证，本别名不做词法自证。
 */
using CommandId = std::string;

/**
 * @brief 文案键（§3.5 文案键体系——UX-02 工程用语的键半区）。
 *
 * 键约定（§3.5 冻结）："cmd.<id>.title"、"cmd.<id>.kw.<n>"（模糊搜索
 * 关键字）、"stage.<id>.title"、"state.<token>.label"、"diag.<code-lower>.
 * title/detail" 等；**值（中英文资源）归 ui 文案资源文件**（P-DIAG-9
 * 交接），UiText::resolve（UI-T09）是唯一解析出口。UI-T09 落地前，
 * 面板/菜单以过渡文案表承载中文值（statusWordTransitionalLabel 同案——
 * 键不变，迁移时只换值源）。
 */
using TextKey = std::string;

/**
 * @brief 菜单与面板分组路径（§7.1："菜单与面板分组（"文件/新建"）"）。
 *
 * 形态："/" 分段的中文分组路径（如 "文件/新建"、"视图/工具"）——段词表
 * ＝§4.1 六菜单（文件/编辑/视图/阶段/工具/帮助）。命令面板按首段分组
 * 显示（§7.4"Tab 补全类别"的分组建模）。
 */
using MenuPath = std::string;

// =====================================================================
// 阶段导航与 StageStatusModel 词表（UI-T09 首消费落位——§10.2/§6.4/§6.5）
// =====================================================================

/**
 * @brief 评估阶段词表（§10.2 冻结枚举；§6.4 顺序＝UX-12 七阶段序）。
 *
 * 七阶段固定顺序（§6.4 原文）：modeling（建模）→ requirements（需求）→
 * kinematics（运动学）→ trajectory-dynamics（轨迹/动力学）→ selection
 * （选型）→ optimization（优化）→ reporting（报告）。枚举序＝该呈现序，
 * stageViews 的"七项固定顺序"即按此序产出；token 形态（小写连字符）用于
 * 文案键 stage.<id>.title（§3.5）与 StageStatusModel 的跨单元消费。
 */
enum class StageId : std::uint8_t {
    Modeling,          ///< 建模（token "modeling"——§6.4 七阶段第 1）
    Requirements,      ///< 需求（token "requirements"）
    Kinematics,        ///< 运动学（token "kinematics"）
    TrajectoryDynamics,///< 轨迹/动力学（token "trajectory-dynamics"）
    Selection,         ///< 选型（token "selection"）
    Optimization,      ///< 优化（token "optimization"）
    Reporting,         ///< 报告（token "reporting"）
};

/**
 * @brief 阶段呈现状态词表（§6.4 阶段呈现状态表六行——ui 呈现态，非门控判定）。
 *
 * 业务判定归 workflow 门控，ui 不复制（§6.4 表头原文）——本枚举只是该表
 * "阶段呈现状态"列的承载：completed/in-progress/blocked/unavailable/
 * not-started/view-only。其中 in-progress（用户所在，会话态）与 view-only
 * （writable=false，§5.5）由导航模型按会话事实合成，其余四态透传门控输出
 * （合成优先级见 IStageNavigationModel.hpp 的 stageViews 契约注释）。
 */
enum class StageViewStatus : std::uint8_t {
    Completed,   ///< 已完成（阶段就绪且关键产物当前——门控输出＋当前性投影）
    InProgress,  ///< 进行中（当前激活阶段——StageNavigationModel.currentStage 会话态）
    Blocked,     ///< 阻塞（门控未通过且存在待办——附原因＋下一步建议）
    Unavailable, ///< 不可用（上游阶段未完成——锁定入口，显示解锁条件摘要）
    NotStarted,  ///< 未开始（可进入但从未访问——门控输出）
    ViewOnly,    ///< 只读查看（只读项目：阶段可查看、不可执行——writable=false §5.5）
};

/**
 * @brief 七态 token 的呈现承载别名（§10.2 StageView.sevenState 字段类型）。
 *
 * 值域＝§6.3 七态冻结 token（"empty-project"/"incomplete"/…，经
 * statusWordToken 取得——词表唯一权威在 UiProjections.hpp 的 StatusWord，
 * 本别名只作 StageView 字段的呈现承载，不另立词表——NFR-MNT-03）。空串
 * ＝该阶段无七态数据源（呈现层显示占位，不虚构状态词）。
 */
using UiStateToken = std::string;

/**
 * @brief 单个评估域的就绪投影（§6.5 单侧冻结形状——供 workflow 消费）。
 *
 * P-UI-6 处置（契约 acceptance 3）：workflow 门控三方契约未定稿前按 §6.5
 * 本形状实现（字段逐字对齐 ui.md §6.5 代码块——谈判起点，workflow.md 产出
 * 后核对，不兼容时按影响面增量同步、不私改对端）。字段语义（§6.5 原文）：
 * 数据来自该域插件经注册端口上报的只读投影（IUiDomainReadinessSource），
 * StageStatusModel 只汇聚、不计算门控、不判定就绪（输入完整性结论来自域
 * 就绪校验结果本身——N-11 无第二套门控状态机）。
 */
struct DomainReadinessItem {
    /// 域注册键（"kinematics"/"trajectory"/…——域注册词表，§6.5 原文）。
    std::string domainKey;
    /// 最近正式判定（core 词表直用——表内登记边；无则 NotApplicable，§6.5）。
    core::EngineeringStatus verdict = core::EngineeringStatus::NotApplicable;
    /// 就绪校验结论（REQ-06——true＝输入完整；结论权威在域就绪校验，ui 透传）。
    bool inputComplete = false;
    /// 缺项文案键清单（缺项明细归域/VerdictTrace——键经 UiText 解析呈现；
    /// UX-02：界面只见键与局部名，零哈希/内部标识）。
    std::vector<TextKey> missingItemKeys;
    /// 该域是否存在在途任务（true＝computing 呈现素材之一——§6.3 触发面）。
    bool hasActiveTask = false;
};

/**
 * @brief 每阶段一份的域就绪汇聚快照（§6.5 StageReadinessSnapshot——
 *        StageStatusModel.readinessSnapshot 的返回形状，供 workflow 消费）。
 *
 * epoch 标注（契约 acceptance 2）：snapshot 携带构建时会话纪元（§6.2——
 * epoch 不符当前会话的快照由消费方丢弃，迟到数据不进当前呈现）。值拷贝
 * 语义：workflow 可在任意线程拉取（§10.2 线程行"值拷贝"），拿到的是
 * 构建时刻的完整快照，后续重建不影响已取出的副本。
 */
struct StageReadinessSnapshot {
    /// 所属阶段（§6.5 原文字段）。
    StageId stage = StageId::Modeling;
    /// 域就绪项（按域源注册序稳定排列——NFR-COR-02；空＝该阶段无已注册域）。
    std::vector<DomainReadinessItem> domains;
    /// 构建纪元（§6.2 会话纪元——单调 uint64，§6.5 原文注释"构建纪元"）。
    std::uint64_t epoch = 0;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_UITYPES_HPP
