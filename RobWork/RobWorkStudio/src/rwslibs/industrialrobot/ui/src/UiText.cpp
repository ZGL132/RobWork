/**
 * @file   UiText.cpp
 * @brief  工程用语文案体系的实现——内建中文过渡文案表＋键解析＋参数替换
 *         ＋UX-02 内部身份守卫（§3.5/§6.6）。
 *
 * 设计依据：
 *   - units/ui.md §3.5（文案键体系：键冻结、值归 ui 文案资源——P-DIAG-9
 *     交接；"UiText::resolve(TextKey, params) 是唯一出口"）、§6.6（数值带
 *     单位显示、"不适用"占位不伪造 0、对象定位经名称端口局部名）；
 *   - 需求 UX-02（工程用语——零哈希/Schema 版本/内部插件名进用户可见文本）、
 *     ERR-01（"不适用"显式化）、NFR-COR-02（同键同文——确定性呈现）；
 *   - 任务契约 UI-T09.json acceptance 2（UiText 体系＋UX-02 断言）。
 *
 * 背景说明（表的构成与过渡语义）：内建表覆盖本单元阶段 A 已产出的全部
 * 键族——①七阶段标题 stage.<id>.title（§3.5 冻结族，§6.4 七阶段中文名）；
 * ①b 域命令标题 cmd.<id>.title（WP-24-T03 建模十条起步，UI-T24 增需求域
 * 九条——域命令按钮/菜单的中文语义名，消 F-430 家族面板侧冻结文案）；
 * ②七态短标签 state.<token>.label（§6.3 词表"中文"列——UI-T04 过渡承载
 * 的值源切换，键与值逐字不变，P-UI-1 词表未改一字）；③core 九态短标签
 * （PM-03/PM-11——同上过渡迁移）；④当前性「无法判定」原因两键（P-UI-2
 * 建议口径原文）；⑤门控数据不可用呈现键（§10.2 错误类型行"门控数据
 * 缺失→Blocked＋'门控数据不可用'"的呈现面）；⑥⑦插件标题与装配状态标签
 * （plugin.<id>.title 八键＋plugin.assembly.<state>.label 三键——UI-T10
 * 关于框清单的呈现值，§11.4/UX-02：token 不进用户文本，用户见中文名）。
 * 键族⑨（panel.<domain>.self-nav.note 三键）已随 UI-T36 退役：三域自持
 * 导航 deprecated 横幅移除后无消费方，键表同步删行。
 * 值迁移到资源文件时仅替换本表的值源，键不变——调用方与测试零改动
 * （UI-T03 文案表同案）。
 *
 * 线程安全：表为编译期固定的静态只读数组，全部函数可重入。
 */

#include <sdurws/ird/ui/UiText.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

// ---------------------------------------------------------------------
// 内建中文过渡文案表（键冻结——值过渡承载，见文件头注释）
// ---------------------------------------------------------------------

/// 文案表行（键＋值；值均为中文 UTF-8——§3.5 键值分离的过渡值源）。
struct TextRow
{
    const char* key;    ///< 文案键（§3.5 冻结词形——不可改，改键＝契约变更）
    const char* value;  ///< 中文文本（过渡承载——迁资源文件时仅换值源）
};

/// 键族①：七阶段标题（stage.<id>.title——token 为 StageId 词表小写连字符
/// 形态；中文＝§6.4 七阶段括注名，"轨迹/动力学"含分隔符原文）。
constexpr std::array<TextRow, 7> kStageTitleTable{{
    { "stage.modeling.title",           "建模"         },
    { "stage.requirements.title",       "需求"         },
    { "stage.kinematics.title",         "运动学"       },
    { "stage.trajectory-dynamics.title", "轨迹/动力学" },
    { "stage.selection.title",          "选型"         },
    { "stage.optimization.title",       "优化"         },
    { "stage.reporting.title",          "报告"         },
}};

/// 键族①b：域命令标题（cmd.<id>.title——§3.5 键约定；WP-24-T03 首版装配
/// 登记——建模 §9.7.3 十条的中文工程用语值；后续域命令随各自装配增行）。
/// WP-24-T03b 收口增行：域命令处理器级拒绝的诚实反馈文案（非标题键——
/// content submitCommand 对 outcome.messageKey 的呈现值源，ERR-01 因果
/// 如实：拒绝原因由处理器给出，不走通用只读/无项目理由）。
/// UI-T24 P3 增行：requirements §9.8 命令表九条的中文语义名（消账 F-430
/// 家族需求域按钮一族——此前 RequirementsPanelWidget 按钮以原始 commandId
/// 直出，UX-02 内部名泄漏；键登记后 resolveText 才可解析，键表完备性由
/// ui/test 具名用例钉住——F-430 处置约束）。
/// UI-T41 批次B 增行：建模十条命令的 tooltip 键（cmd.<id>.tooltip——B4 去
/// 裸命令 id 悬停文案）＋建模面板呈现键（panel.modeling.*——B7 复合行指引
/// ／B2 诊断历史标题；值＝工程用语中文，UX-02）。
constexpr std::array<TextRow, 42> kDomainCommandTitleTable{{
    { "cmd.modeling.new-from-template.title",          "从模板新建"       },
    { "cmd.modeling.import-urdf.title",                "导入 URDF"        },
    { "cmd.modeling.import-xacro.title",               "导入 Xacro"       },
    { "cmd.modeling.switch-authority.title",           "切换权威参数化"   },
    { "cmd.modeling.estimate-properties.title",        "物性估算"         },
    { "cmd.modeling.generate-placeholder-geometry.title", "生成占位几何"  },
    { "cmd.modeling.diff-baseline.title",              "与基线比较"       },
    { "cmd.modeling.export-package.title",             "导出规范包"       },
    { "cmd.modeling.import-package.title",             "导入规范包"       },
    { "cmd.modeling.reset-home-zero.title",            "复位 Home/Zero"   },
    { "cmd.modeling.export-workcell-xml.title",        "导出 WorkCell XML" },
    { "cmd.modeling.flow-not-assembled",               "该域流程未装配（随后续建模任务提供）" },
    { "cmd.flow-not-assembled.reason",                 "该流程将在后续版本提供" },
    { "reason.no-project",                              "请先打开工业机器人项目" },
    { "reason.readonly",                                "当前项目为只读，不能修改" },
    { "reason.readiness-blocking",                      "存在未就绪阻断项——先处理校验页阻断" },
    // 项目级撤销/重做的"无可撤销修订"禁用原因（UI-T39——撤销可用性由
    // project UndoRedoService 的 tip inverse 推导〔磁盘事实〕，无可撤销
    // 修订时按钮置灰并以此键呈现原因——审核 P1：撤销按钮不得只按命令
    // 出口存在性置可用）。
    { "reason.no-undo-revision",                        "当前没有可撤销的修订" },
    { "reason.no-redo-revision",                        "当前没有可重做的修订" },
    // —— requirements 域九键（行序＝requirements.md §9.8 命令表行序，
    // PanelCommandCatalog::requirementsDomainCommands 登记序——两端顺序同源
    // 该表；值＝工程用语短句，零 id/内部名词形——UX-02）——
    { "cmd.requirements.import-csv.title",             "导入 CSV"         },
    { "cmd.requirements.import-json.title",            "导入 JSON"        },
    { "cmd.requirements.export-copy.title",            "导出副本"         },
    { "cmd.requirements.capture-tcp.title",            "捕获 TCP"         },
    { "cmd.requirements.pick-feature.title",           "拾取几何特征"     },
    { "cmd.requirements.mirror-stations.title",        "镜像工位"         },
    { "cmd.requirements.create-array.title",           "批量阵列"         },
    { "cmd.requirements.apply-template.title",         "应用工艺模板"     },
    { "cmd.requirements.regenerate-linked.title",      "按模板重生成"     },
    // —— UI-T41 批次B：建模十条命令 tooltip（cmd.<id>.tooltip——悬停文案
    // 与按钮标题同源解析；解析空回退命令 id 原文作对账兜底）——
    { "cmd.modeling.new-from-template.tooltip",        "以六轴模板重建草稿；有未应用编辑时先确认丢弃" },
    { "cmd.modeling.import-urdf.tooltip",              "从 URDF 文件映射生成建模草稿（含映射报告确认）" },
    { "cmd.modeling.import-xacro.tooltip",             "从 Xacro 受控展开后映射生成建模草稿" },
    { "cmd.modeling.switch-authority.tooltip",         "DH↔显式权威切换：先行五状态判定，Exact/ExactNonUnique 才落切换" },
    { "cmd.modeling.estimate-properties.tooltip",      "选中连杆批量物性估算（材料与密度可指定）" },
    { "cmd.modeling.generate-placeholder-geometry.tooltip", "按相邻关节原点生成连杆占位圆柱（视觉几何）" },
    { "cmd.modeling.diff-baseline.tooltip",            "与最近应用的基线修订做模型差异比较（只读）" },
    { "cmd.modeling.export-package.tooltip",           "把当前草稿闭包导出为规范模型包（只读会话可用）" },
    { "cmd.modeling.export-workcell-xml.tooltip",      "把已应用修订的编译产物导出为 WorkCell/DWC XML 供外部工具查看（只读会话可用；数据源＝编译快照）" },
    { "cmd.modeling.import-package.tooltip",           "导入本软件导出的规范模型包并替换当前草稿" },
    { "cmd.modeling.reset-home-zero.tooltip",          "呈报位姿集 Home/Zero 参考数据（会话命令，零修订）" },
    // —— UI-T41 批次B：建模面板呈现键（B2 诊断历史标题／B7 复合行指引）——
    { "panel.modeling.field.composite.tooltip",        "该字段为复合行，不支持就地编辑——物性可经『物性估算』、几何可经『生成占位几何』域命令维护" },
    { "panel.modeling.history.title",                  "诊断历史" },
    { "panel.modeling.history.tooltip",                "展开本会话最近 20 条命令回执与编辑原因（不清空覆盖）" },
    { "panel.modeling.tree.accessible",                "建模结构树" },
}};
/// 键族①c：需求面板卡片文案（UI-T37——卡片标题＋"?"帮助位；值＝设计规格
/// 2026-10-01 §3/§4 卡片化分组词表；键前缀 panel.requirements.card.*）。
constexpr std::array<TextRow, 18> kRequirementsPanelCardTable{{
    { "panel.requirements.card.basic.title",        "基础属性" },
    { "panel.requirements.card.basic.help",
      "条目级属性。灰显行＝只读事实（来源/等级等），权威值以工作集为准。" },
    { "panel.requirements.card.pose.title",         "空间与公差" },
    { "panel.requirements.card.pose.help",
      "位置与容差。内部按 SI 单位（m/rad）存储，本面板按词表呈现；缺失态显示『未提供』，不伪造零。" },
    { "panel.requirements.card.dof.title",          "自由度约束" },
    { "panel.requirements.card.dof.help",
      "『约束』＝该方向被夹具固定，评估视为不可动；点击分段钮即时生效并生成一条草稿级撤销记录。" },
    { "panel.requirements.card.segment.title",      "动作阶段" },
    { "panel.requirements.card.segment.help",
      "接近/作业/撤离三段（§5.1）；本卡为呈现面，距离值在空间与公差卡按词表编辑。" },
    { "panel.requirements.card.orientation.title",  "姿态规则" },
    { "panel.requirements.card.orientation.help",
      "姿态规则种类决定参数行显隐（表驱动投影）；参数值单位 rad。" },
    { "panel.requirements.card.region-box.title",   "空间包围盒" },
    { "panel.requirements.card.region-box.help",
      "六面体度量坐标系为浅引用事实（灰显）；中心/尺寸单位 m，尺寸须为正（非退化 I-REQ-6）。" },
    { "panel.requirements.card.region-sampling.title", "采样与达标" },
    { "panel.requirements.card.region-sampling.help",
      "规则网格三轴分割数乘积＝评估离散点总数；覆盖率下限取 0~100%。『未设』＝未配置，不伪造零。" },
    { "panel.requirements.card.region-advanced.title", "高级参数" },
    { "panel.requirements.card.region-advanced.help",
      "顺序键/备注等次要字段——默认折叠，按需展开。" },
    { "panel.requirements.card.condition-detail.title", "工况详情与节拍配置" },
    { "panel.requirements.card.condition-detail.help",
      "工况＝作业条件：名称/等级/启用与负载、事件、目标节拍（s）、无碰撞要求、"
      "最小关节裕量、适用范围——选中上方工况行后在此编辑；必验为派生事实（灰显）。" },
}};
/// 键族①d：需求面板控件文案（UI-T37 返工⑤——工具栏整合/空态提示/撤销
/// 三键标签与 Tooltip；原字面直出的撤销三键随批入表——NFR-MNT-03 单一
/// 文案出口。键前缀 panel.requirements.*，非命令目录 id——区别键族①b）。
constexpr std::array<TextRow, 13> kRequirementsPanelControlTable{{
    { "panel.requirements.import-dropdown.label", "导入 ▾" },
    { "panel.requirements.more-actions.label", "更多操作 ▾" },
    { "panel.requirements.empty-selection.hint",
      "请在左侧需求树或列表中选择对象，或点击上方新增" },
    { "panel.requirements.draft-marker", "草稿" },
    { "panel.requirements.undo-draft.label",  "撤销" },
    { "panel.requirements.undo-draft.tooltip",
      "撤销本次编辑（草稿级撤销栈——尚未应用的编辑回退一级）" },
    { "panel.requirements.redo-draft.label",  "重做" },
    { "panel.requirements.redo-draft.tooltip",
      "重做本次编辑（草稿级重做栈——被撤销的编辑重新生效）" },
    { "panel.requirements.undo-project.label", "撤销上次应用" },
    { "panel.requirements.undo-project.tooltip",
      "撤销最近一次已应用的正式修订（项目级——产生一条新修订，历史只增不改）" },
    // 生命周期工具行禁用原因（UI-T39——审核返工：禁用键必须给出原因，
    // 工程师不必猜测是未开项目、只读还是未选中）。
    { "panel.requirements.lifecycle.tooltip.no-session",
      "未打开需求会话——请先新建或打开项目" },
    { "panel.requirements.lifecycle.tooltip.readonly",
      "当前项目为只读，不能修改需求" },
    { "panel.requirements.lifecycle.tooltip.no-selection",
      "请先在上方列表或需求树中选择一个对象" },
}};

/// 键族②：七态短标签（state.<token>.label——§6.3 词表 token＋"中文"列；
/// 与 StatusWordModelTest 钉住的过渡值逐字一致——值源切换零漂移）。
constexpr std::array<TextRow, 7> kStatusWordLabelTable{{
    { "state.empty-project.label",     "空项目"   },
    { "state.incomplete.label",        "未完成"   },
    { "state.computing.label",         "计算中"   },
    { "state.results-stale.label",     "结果过期" },
    { "state.data-insufficient.label", "数据不足" },
    { "state.failed.label",            "失败"     },
    { "state.computable.label",        "可计算"   },
}};

/// 键族③：core 九态短标签（state.<token>.label——token 段与 core
/// toToken(TaskState) 词表逐字一致；中文＝§6.3 九态短标签行原文）。
constexpr std::array<TextRow, 9> kTaskStateLabelTable{{
    { "state.queued.label",      "排队中" },
    { "state.preparing.label",   "准备中" },
    { "state.running.label",     "计算中" },
    { "state.paused.label",      "已暂停" },
    { "state.canceling.label",   "取消中" },
    { "state.canceled.label",    "已取消" },
    { "state.completed.label",   "已完成" },
    { "state.failed.label",      "失败"   },
    { "state.interrupted.label", "已中断" },
}};

/// 键族④：当前性「无法判定」原因（P-UI-2 建议口径原文——D-12 同稿）。
constexpr std::array<TextRow, 2> kUnevaluableLabelTable{{
    { "state.currentness.unevaluable.cross-context.label",
      "当前性无法判定（上下文变化）" },
    { "state.currentness.unevaluable.unresolved-dependency.label",
      "当前性无法判定（依赖缺失）" },
}};

/// 键族⑤：阶段呈现辅助键（blocked/failed 展开缺项清单时的计数摘要行——
/// §6.4"失败定位：点击展开'缺项清单'"的汇总呈现；{0}＝缺项计数）与门控
/// 数据不可用呈现键（§10.2 错误类型行原文"门控数据不可用"——门控适配器
/// 在数据源不可得时以 blocked＋本键表达，模型透传）。
constexpr std::array<TextRow, 2> kStagePresentationTable{{
    { "stage.missing.items.count", "缺项 {0} 项" },
    { "stage.gate.unavailable.reason", "门控数据不可用" },
}};

/// 键族⑥：插件标题（plugin.<id>.title——UI-T10 冻结登记，§3.5 键族增量；
/// token 与 units/ui.md §11.1 静态白名单词表逐字一一对应。中文名是关于框
/// 插件清单"标题"列的呈现值——UX-02"界面禁止出现内部插件名"：token 本身
/// 只作键词根，用户只见此处登记的中文名）。
constexpr std::array<TextRow, 8> kPluginTitleTable{{
    { "plugin.modeling.title",      "建模"   },
    { "plugin.requirements.title",  "需求"   },
    { "plugin.kinematics.title",    "运动学" },
    { "plugin.trajectory.title",    "轨迹"   },
    { "plugin.dynamics.title",      "动力学" },
    { "plugin.selection.title",     "选型"   },
    { "plugin.optimization.title",  "优化"   },
    { "plugin.workflow.title",      "工作流" },
}};

/// 键族⑦：插件装配状态标签（plugin.assembly.<state>.label——UI-T10 冻结
/// 登记；三态与 ui::PluginAssemblyStatus 枚举一一对应。关于框清单"装配
/// 状态"列的呈现值——"未装配"是白名单占位常态的如实呈现，不虚构"已
/// 装配"；"装配失败"对应 §11.3 装配失败降级态）。
constexpr std::array<TextRow, 3> kPluginAssemblyLabelTable{{
    { "plugin.assembly.not-assembled.label", "未装配"   },
    { "plugin.assembly.ok.label",            "已装配"   },
    { "plugin.assembly.failed.label",        "装配失败" },
}};

/// 键族⑦a：跨域就绪判定词（verdict.<status>——UI-T44；四值与
/// core::EngineeringStatus 枚举一一对应，映射函数
/// engineeringStatusDisplayName 单点消费）。值＝工程用语中文（UX-02）：
/// "待提交后判定"承载 NotApplicable 的映射语义（呈现约定——§6.5 值语义
/// 直投：ReadyWithNotes＝输入完整但结论非"可行"，草稿预检口径不替代
/// 命令 prepare 现场重估）；"输入不完整"承载 DataInsufficient（REQ-06
/// 缺项呈现归域面板就绪条，摘要卡只给判定词）。
constexpr std::array<TextRow, 4> kVerdictTable{{
    { "verdict.feasible",              "可行"         },
    { "verdict.engineering-infeasible", "工程不可行"  },
    { "verdict.data-insufficient",     "输入不完整"   },
    { "verdict.not-applicable",        "待提交后判定" },
}};

/// 键族⑧：诊断/确认/任务呈现键（UI-T13 冻结登记，§16.7 v1.5——§9.1
/// 诊断呈现〔比较型占位/恢复横幅 PM-15/日志面板〕＋§9.2 确认对话〔批量
/// 决议/失效标注/策略绑定提示——MDL-06④ 确认不豁免校验〕＋§9.4 任务
/// 呈现〔后台只读 P-UI-7/暂停能力反馈/强杀独立确认/排队文案 UI-EXEC-1/
/// 归档子标/重跑/历史/当前性徽标——零「正式通过」字样〕）。键值分离：
/// 值为中文过渡承载，迁资源文件只换值源。
constexpr std::array<TextRow, 32> kPresentationTable{{
    // —— §9.1 比较型三要素占位（ERR-01/MDL-06：不伪造数值）——
    { "ui.diag.comparison.invalid",       "无效（{0}）" },
    { "ui.diag.comparison.not-provided",  "未提供"       },
    // —— §9.1 恢复横幅（PM-15：一句话汇总＋三动作）——
    { "ui.recovery.banner.summary.ignored-saves", "上次会话有未完成的保存，已忽略" },
    { "ui.recovery.banner.summary.interrupted",   "有任务在上次会话被中断"         },
    { "ui.recovery.banner.summary.orphan-drafts", "检测到 {0} 份未保存草稿"        },
    { "ui.recovery.banner.action.details",        "查看详情" },
    { "ui.recovery.banner.action.restore",        "恢复草稿" },
    { "ui.recovery.banner.action.discard",        "放弃"     },
    // —— §9.1 Tier-U 日志面板（Dev 级仅跳转，不内嵌显示）——
    { "ui.logpanel.empty",    "暂无用户日志"       },
    { "ui.logpanel.dev.jump", "打开开发日志文件"   },
    // —— §9.2/§9.3 确认对话（SA-15/MDL-06④/P-DIAG-6）——
    { "ui.dlg.confirm.title",           "确认计算前提"                                   },
    { "ui.dlg.confirm.instruction",     "以下项超出策略阈值，需逐项确认后命令才能继续"   },
    { "ui.dlg.confirm.no-skip",         "确认后仍会执行完整断言与编译（确认不豁免校验）" },
    { "ui.dlg.confirm.option.confirm",  "确认" },
    { "ui.dlg.confirm.option.reject",   "拒绝" },
    { "ui.dlg.confirm.option.confirm-all", "全部确认" },
    { "ui.dlg.confirm.stale-input",     "输入已变化，本次确认可能失效——建议取消后重新确认" },
    { "ui.dlg.confirm.policy-bound",    "已随当前策略版本绑定" },
    // —— §9.4 任务呈现（TASK-01/02/03、P-UI-7、UI-EXEC-1）——
    { "ui.task.background.readonly",  "项目已关闭，后台任务只读"                   },
    { "ui.task.pause.unsupported",    "该任务不支持暂停"                           },
    { "ui.task.force.title",          "强制终止任务"                               },
    { "ui.task.force.consequence",    "任务将记为失败，最近检查点保留，可从检查点续跑" },
    { "ui.task.force.needs-confirm",  "强制终止前需在确认对话中确认"               },
    { "ui.task.queue.waiting-resource", "排队中（等待资源）" },
    { "ui.task.archive.reserved",     "待归档"   },
    { "ui.task.archive.archiving",    "归档中"   },
    { "ui.task.archive.archived",     "已归档"   },
    { "ui.task.archive.failed",       "归档失败" },
    { "ui.task.rerun",                "重跑"     },
    { "ui.task.history.view",         "查看历史结果" },
    { "ui.task.currentness.current",    "结果当前"   },
    { "ui.task.currentness.superseded", "结果已过期" },
}};

// 键族⑨（panel.<domain>.self-nav.note 三键）已随 UI-T36 退役：三域自持
// 导航 deprecated 横幅移除后无消费方，键表同步删行（查找表缺键＝
// resolveText 上抛的 fail-fast 语义反向充当退役守卫——回归断言见
// StageNavigationModelTest 的键退役守卫用例）。

/// 全表拼接视图（查找入口——各键族数组顺序拼接，避免维护一份重复大表）。
/// 注意 state.failed.label 在键族②与③中重复登记（七态与九态同键同值
/// "失败"——§6.3 两表原文即同文），查找取先命中者，值一致故无歧义。
/// UI-T24 勘误：键族①b（域命令标题）此前漏出本盘点面（registeredTextKeys
/// 未含 kDomainCommandTitleTable——建模十键登记时引入的疏漏），随需求域
/// 九键增行一并补全——盘点面与查找面必须一致（"登记即盘点"）。
const TextRow* findRow(const TextKey& key)
{
    // 表小（66 行）且调用频率为呈现路径，线性扫描足够（NFR-PERF-01 预算内）；
    // 换哈希表反而引入构建期初始化顺序顾虑——呈现函数必须任何时刻可调用。
    for (const auto& row : kStageTitleTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kDomainCommandTitleTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kRequirementsPanelCardTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kRequirementsPanelControlTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kStatusWordLabelTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kTaskStateLabelTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kUnevaluableLabelTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kStagePresentationTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kPluginTitleTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kPluginAssemblyLabelTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kVerdictTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kPresentationTable) {
        if (key == row.key) { return &row; }
    }
    return nullptr;
}

// ---------------------------------------------------------------------
// 位置占位符解析（{0}、{1}…——resolveText 两参形态的替换引擎）
// ---------------------------------------------------------------------

/**
 * @brief 判定 text 的 [begin, begin+n) 是否为一个合法位置占位符 {k}。
 *
 * 合法条件：首字符 '{'、末字符 '}'、中间为纯十进制数字、无前导零
 * （"0" 合法、"01" 非法——前导零形态必非任何调用点产出，按字面文本
 * 处置而不是占位符，避免歧义替换）。命中时经 outIndex 返回 k。
 */
bool parsePlaceholder(const std::string& text, std::size_t begin, std::size_t n,
                      std::size_t& outIndex)
{
    // 形态前置：最少 "{}"+1 位数字＝3 字符；两端括号。
    if (n < 3 || text[begin] != '{' || text[begin + n - 1] != '}') {
        return false;
    }
    // 中段逐字符校验十进制数字；同时累积索引值（位数超出 size_t 表达
    // 能力的文本必非本体系产出——按字面处置，累积截断无害）。
    std::size_t index = 0;
    for (std::size_t i = begin + 1; i + 1 < begin + n; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') {
            return false;
        }
        index = index * 10 + static_cast<std::size_t>(c - '0');
    }
    // 前导零拒绝（见函数注释——"01" 不是占位符）。
    if (n > 3 && text[begin + 1] == '0') {
        return false;
    }
    outIndex = index;
    return true;
}

}  // namespace

// =====================================================================
// 键解析（§3.5 唯一出口）
// =====================================================================

std::string resolveText(const TextKey& key)
{
    // 缺键＝调用方契约违约（拼写错误/漏登记）——fail-fast 而不是回显键名
    // 或空串（静默降级会把缺陷渲染给用户，见头文件契约注释）。
    const TextRow* row = findRow(key);
    if (row == nullptr) {
        throw std::invalid_argument("ui/uitext/unknown-key: 未登记文案键 " + key);
    }
    return std::string(row->value);
}

std::string resolveText(const TextKey& key, const std::vector<std::string>& args)
{
    // 先取值文本（缺键fail-fast 同无参形态），再做占位符校验＋替换。
    const TextRow* row = findRow(key);
    if (row == nullptr) {
        throw std::invalid_argument("ui/uitext/unknown-key: 未登记文案键 " + key);
    }
    const std::string value(row->value);

    // ---- 第 1 步：扫值文本，收集占位符引用并校验越界 ----------------
    // 每个 {k} 必须 k < args.size()——引用越界＝文案与调用点参数面不齐
    // （调用方错误，见头文件替换纪律）。
    std::vector<bool> used(args.size(), false);
    bool hasPlaceholder = false;
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] == '{') {
            // 找配对 '}'（占位符不嵌套——值为工程用语短句，形态受控）。
            const std::size_t close = value.find('}', i);
            if (close != std::string::npos) {
                std::size_t index = 0;
                if (parsePlaceholder(value, i, close - i + 1, index)) {
                    hasPlaceholder = true;
                    if (index >= args.size()) {
                        throw std::invalid_argument(
                            "ui/uitext/arg-missing: 键 " + key + " 引用 {"
                            + std::to_string(index) + "} 但调用方仅提供 "
                            + std::to_string(args.size()) + " 个参数");
                    }
                    used[index] = true;
                    i = close + 1;
                    continue;
                }
            }
        }
        ++i;
    }

    // ---- 第 2 步：参数冗余校验（每个传入参数必须在文本中出现）--------
    // 传了没用的参数＝调用方误传（同上——替换纪律对称半区）。
    for (std::size_t i = 0; i < used.size(); ++i) {
        if (!used[i]) {
            throw std::invalid_argument(
                "ui/uitext/arg-unused: 键 " + key + " 未引用参数位置 "
                + std::to_string(i) + "（参数面与文案不齐）");
        }
    }

    // ---- 第 3 步：无占位符即原样返回（args 必为空——已被第 2 步保证）--
    if (!hasPlaceholder) {
        return value;
    }

    // ---- 第 4 步：参数守卫＋顺序拼装 --------------------------------
    // 每个参数先过内部身份守卫（哈希形态拒绝——UX-02 红线在呈现边界
    // 执行；参数是动态文本进入用户可见面的唯一通道）。
    for (const auto& arg : args) {
        ensureNoInternalIdentity(arg);
    }
    std::string out;
    out.reserve(value.size() + 16);
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] == '{') {
            const std::size_t close = value.find('}', i);
            if (close != std::string::npos) {
                std::size_t index = 0;
                if (parsePlaceholder(value, i, close - i + 1, index)) {
                    // index < args.size() 已由第 1 步保证（同一文本同一解析器）。
                    out += args[index];
                    i = close + 1;
                    continue;
                }
            }
        }
        out += value[i];
        ++i;
    }
    return out;
}

// =====================================================================
// 数值＋单位显示（§6.6）
// =====================================================================

std::string notApplicableText()
{
    // §6.6 原文占位——"不适用"字段显示"不适用"，不伪造 0（ERR-01）。
    return "不适用";
}

// =====================================================================
// 键清单盘点面（值不暴露——P-DIAG-9 键值分离，值唯一出口是 resolveText）
// =====================================================================

std::vector<TextKey> registeredTextKeys()
{
    // 按各族登记序拼接；state.failed.label 双族重复登记只输出一次
    // （集合语义——键清单是"已登记键"的盘点，不是物理行清单）。
    // 计数：7（阶段标题）＋20（域命令①b——UI-T24 起含需求域九键）＋7（七态）
    // ＋9（九态，去重后 8）＋2＋2＋8＋3＋32＋3（键族⑨——UI-T25，三域）＝92
    // （reserve 供读面参考，非精确）。
    std::vector<TextKey> keys;
    keys.reserve(92);
    for (const auto& row : kStageTitleTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kDomainCommandTitleTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kRequirementsPanelCardTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kRequirementsPanelControlTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kStatusWordLabelTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kTaskStateLabelTable) {
        // 七态族已登记的同键（state.failed.label——两族同值"失败"）去重。
        const TextKey key(row.key);
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            keys.push_back(key);
        }
    }
    for (const auto& row : kUnevaluableLabelTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kStagePresentationTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kPluginTitleTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kPluginAssemblyLabelTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kVerdictTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kPresentationTable) {
        keys.emplace_back(row.key);
    }
    return keys;
}

// =====================================================================
// UX-02 内部身份守卫
// =====================================================================
void ensureNoInternalIdentity(const std::string& text)
{
    // 滑窗扫描：任意连续 64 个十六进制字符＝SHA-256 摘要呈现形态
    // （CON-05 内容寻址——本产品唯一哈希形态，绝无呈现语义）。
    auto isHex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
    };
    std::size_t run = 0;  // 当前连续十六进制段长度
    for (const char c : text) {
        if (isHex(c)) {
            ++run;
            if (run >= 64) {
                // 命中即 fail-fast（调用方装配/接线错误——把内容身份当
                // 参数传入呈现层）；错误信息不带摘要内容（避免摘要经
                // 异常消息再进入日志呈现面）。
                throw std::invalid_argument(
                    "ui/uitext/internal-identity: 参数含 64 位十六进制摘要"
                    "形态（UX-02 禁止进用户可见文本）");
            }
        } else {
            run = 0;
        }
    }
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
