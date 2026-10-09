/**
 * @file   Comparison.hpp
 * @brief  方案比较视图编排数据面（UX-13/V15-02）——
 *         ISchemeComparisonController（O7）＋两取数端口接缝＋纯函数组装核。
 *
 * 设计依据：
 *   - units/workflow.md §8.1（方案比较视图——2～4 个方案按八项比较指标差异
 *     高亮＋Model Diff 呈现〔结构/参数（DH、轴线、限位）与物性分组、点击
 *     定位对象——UX-13/V15-02〕；指标取数编排归本单元 O7——从各域已归档
 *     结果投影读取（只读；evidence 当前性投影标注过期项）；八项指标词表与
 *     口径归各域/optimization，**本单元不定义指标口径**；Model Diff 呈现
 *     消费 modeling IModelDiffService 产出的 ModelDiffReport，数据实体与
 *     语义归 MDL-08（M-5 分工）——本单元不重复实现 diff 计算（N7）；
 *     不可算项显示"—"（不伪造数值——OPT-07 同源））、§8.2（比较视图数据流
 *     ——用户选择 2~4 方案→基准一致性检查〔EVI-02/RPT-04：不一致拒绝＋
 *     原因提示〕→八项指标取数（投影，只读）→差异高亮数据（逐指标）＋
 *     Model Diff（modeling 数据实体）→呈现〔ui 宿主面；点击定位经名称
 *     端口〕）、§10.1/§10.2（接口总表 ISchemeComparisonController 行＋
 *     Draft 签名——buildComparison(const std::vector<core::BranchId>&)
 *     本头逐字兑现）、§10.3（接口属性表：buildComparison const 并发安全；
 *     错误语义＝调用方错误 fail-fast（WorkflowError）；比较数据会话态——
 *     无 schema 版本）、§9（协作表 modeling 行"（不直链）……Model Diff 均
 *     经公共数据契约/命令端口"；evidence 行"当前性投影"）
 *   - REQUIREMENTS.md §18 UX-13 原文（"方案比较视图：2~4 个方案按八项比较
 *     指标差异高亮；Model Diff 呈现（V15-02 补登记）：基线与候选差异增量表
 *     〔MDL-08 数据实体〕按结构/参数（DH、轴线、限位）与物性分组呈现、
 *     支持点击定位对象——数据实体与语义归 MDL-08（M-5 分工），本条承接
 *     画面呈现"）、§15.0 八项比较指标全量命名（尺寸包络/结构质量/最小
 *     关节裕量——OPT-B 可算；节拍/器件成本/器件质量/关节侧正机械功/最小
 *     驱动裕量——可算性随阶段 C 各域交付）、OPT-07（默认八项比较指标
 *     **全部展示**——可算项计算，不可算项显示"—"）、EVI-02（"方案比较
 *     〔含优化候选比较与报告变体章节〕只能使用一致的需求与工况基准"）、
 *     RPT-04（"多方案对比章节引用一致的需求与工况基准（EVI-02）"）、
 *     MDL-08（数据层差异比较——呈现归 UX-13，M-5 分工）、AT-12（"基线与
 *     候选差异增量表经方案比较视图按结构/参数/物性分组呈现并可点击定位
 *     对象"——呈现侧）、P-OPT-8（optimization.md §17.3 登记——"按现状
 *     根对象 diff；传动比差异条目缺失时给警告（不虚构差异）"）
 *   - 任务契约 tasks/foundation/WP-22-T11.json acceptance 1/2/3
 *
 * 背景说明（本头为什么是"编排数据面"而不是"面板/判定面"）：workflow 是
 * 编排单元——方案比较的**面板控件**归 ui 宿主面（D-WF-6，GUI 不执行——
 * §11.3），指标**口径**归各域/optimization（本单元不定义指标口径），diff
 * **计算**与数据实体归 modeling（MDL-08/N7）。本头承载的是编排产物：
 * 把用户选定的 2~4 个方案分支，经取数端口折叠为"逐指标高亮行＋逐对 diff
 * 分组块"的呈现值，ui 宿主面拿到即可渲染（点击定位的执行链＝条目携带
 * ObjectId 锚→ui 侧经 IUiNameResolver〔⑥名称端口的 ui 自有投影〕解析
 * 局部名并定位对象——R-4 名称解析归 runtime、宿主面归 ui，本单元零名称
 * 知识）。前置的基准一致性检查消费 evidence 公共契约
 * checkComparisonBaselinesConsistent（evidence 在本单元白名单八边内——
 * 检查实现归 evidence，本单元零重实现；PA-1）。
 */

/*
 * 实现口径说明（WP-22-T11，DTB §5.4 已随单元卡 §10.2 v1.2 登记段同步）：
 *   - 取数走**注入端口**而非直连对端服务（ITitleFactPort/ICloseDrainPort
 *     同款"端口即接缝"先例，实现归 L5 装配层）：①ISchemeMetricPort——
 *     每方案的指标列＋比较基准＋当前性投影快照（L5 桥接各域归档结果投影
 *     /evidence 查询/project branchTips().label；指标词表/单位/口径由对端
 *     透传，本单元零指标口径知识——§8.1 红线）；②IComparisonDiffPort——
 *     两方案分支的 Model Diff 投影（L5 桥接 modeling IModelDiffService；
 *     条目为 modeling ModelDiffEntry 的**值语义同构投影**，字符串化通道
 *     承载零语义增殖——optimization CandidateDiffEntry 同款先例：R-1 无
 *     modeling 编译边，接口对象经 canonical/文本通道合法载荷 §12.3）。
 *   - 基线选择编排：**首方案为基线**，其余方案逐一与基线成对 diff（AT-12
 *     "基线与候选差异增量表"的编排半区；方案选择 UI 的基线指定归 ui 宿主
 *     面——本编排按调用序冻结基线位，确定性 NFR-COR-02）。
 *   - P-OPT-8 兑现（acceptance 3）：diff 条目永远以端口实际产出为准——
 *     编排核**零补造条目**；传动比差异受 modeling diff 范围限制（根对象
 *     diff 不覆盖 robot-drivetrain 部件对象），"范围外"的判定知识在对端
 *     （L5 桥接据 modeling diff 实际范围产出警告），本单元只**透传**警告
 *     至呈现数据（kComparisonWarningRatioOutOfScope 词形登记位——与
 *     optimization kDiffWarningRatioOutOfScope 同串，两卡同步义务）；
 *     diff 通道未装配（available==false）同样降级为警告＋空条目——诚实
 *     呈现"差异面可能不全"，不虚构差异（ITitleFactPort"环境失败折叠为
 *     安全缺省不炸宿主"同款纪律）。
 *   - 错误语义（§10.3 行）：调用方错误 fail-fast（WorkflowError）——方案
 *     数越界〔非 2~4〕、重复分支、分支不可用（@pre"方案分支存在且可读"
 *     违约）、指标列不同构（端口对端契约违约——列错位会让高亮行张冠李戴）、
 *     未知 diff 分组 token（对端违约）；环境/对端错误——diff 通道不可用
 *     走降级警告（非错误不炸宿主）；**基准不一致不是错误**——EVI-02/RPT-04
 *     的拒绝是比较语义的正常分支，走返回值 Rejected（含差异维度清单＋文案
 *     键），绝不产出混基准比较数据（acceptance 2）。
 *   - 差异高亮判定：逐指标对全部方案的可算值做**精确相等**比较（零容差——
 *     无任何容差口径登记，不发明阈值 N9/I-WF-3；≥2 个可算值且不全相等
 *     ⇒differs 高亮位）。"—"承载＝value==nullopt（OPT-07 不可算项；
 *     OPT-VER 观测点"'—'语义 null≠0"同口径——nullopt 绝不参与数值比较）。
 */

#ifndef SDURWS_IRD_WORKFLOW_COMPARISON_HPP
#define SDURWS_IRD_WORKFLOW_COMPARISON_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>      // core::BranchId/core::ObjectId——
                                              // 方案分支与点击定位锚（§10.3：本单元
                                              // 不生成任何新 ID 类型）
#include <sdurws/ird/evidence/Verdict.hpp>   // evidence::ComparisonBaseline/
                                              // checkComparisonBaselinesConsistent/
                                              // BaselineDifferenceDimension——基准一致
                                              // 性检查的公共契约消费（evidence 白名单
                                              // 八边内；检查实现归 evidence，零重实现）
#include <sdurws/ird/ui/UiProjections.hpp>   // ui::CurrentnessProjection——当前性投影
                                              // 值载体（C-7 搬运零重算，v1.0 同款）
#include <sdurws/ird/workflow/Types.hpp>     // WorkflowError——调用方契约违约异常

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 词表与常量（文案键值权威归 ui 文案表——UX-02 键/值半区分工）
// =====================================================================

/** @brief 方案数下界（UX-13"2~4 个方案"——含 2）。 */
inline constexpr std::size_t kMinComparisonSchemes = 2;

/** @brief 方案数上界（UX-13"2~4 个方案"——含 4）。 */
inline constexpr std::size_t kMaxComparisonSchemes = 4;

/**
 * @brief 不可算指标的呈现占位符（OPT-07"不可算项显示'—'"——EM DASH）。
 *
 * 这是**呈现符号**不是文案（无语义可翻译；OPT-07 原文字面），故不经
 * 文案键承载——值单元格的 displayText 由组装核直接渲染为本常量。可算项
 * 数值绝不伪装成占位、占位也绝不伪装成数值（"'—'语义 null≠0"）。
 */
inline constexpr const char* kMetricUnavailableDisplay = "\xE2\x80\x94";  // U+2014 "—"

/**
 * @brief 比较基准不一致——拒绝主原因文案键（EVI-02/RPT-04）。
 *
 * 键串为本头新增键半区（值归 ui 文案表，两卡增量同步义务——v1.0 恢复
 * 横幅六键同款口径）：拒绝提示的主文案"比较基准不一致"半区；逐维度明细
 * 行经 comparisonBaselineDimensionKey() 的维度键族（UX-03：原因含定位）。
 */
inline constexpr const char* kComparisonBaselineMismatchKey = "comparison.baseline-mismatch";

/**
 * @brief 比较基准不一致——建议动作文案键（UX-03 三字段之"建议动作"）。
 *
 * 值归 ui 文案表（工程用语——"切换到一致基准的方案后再比较"语义半区）。
 */
inline constexpr const char* kComparisonBaselineMismatchActionKey =
    "comparison.baseline-mismatch.action";

/**
 * @brief diff 通道不可用——降级警告限定语 token（非诊断码——呈现/测试
 *        定位用）。
 *
 * 与 optimization kDiffWarningSourceUnavailable 同串（"diff-preview-
 * unavailable"）：两单元各自登记同串常量（R-1 无对端编译边，不代持其
 * 契约常量——DOPT-6 同款规避），语义同源＝"差异面来源缺失，呈现请如实
 * 告知用户该组差异可能不全"。
 */
inline constexpr const char* kComparisonWarningDiffSourceUnavailable =
    "diff-preview-unavailable";

/**
 * @brief 传动比差异超出当前 diff 范围——限定语警告 token（P-OPT-8）。
 *
 * 与 optimization kDiffWarningRatioOutOfScope 同串（"ratio-diff-out-of-
 * scope"）：P-OPT-8 处置口径"按现状根对象 diff；传动比差异条目缺失时给
 * 警告（不虚构差异）"——**警告的产生归 diff 端口实现方**（L5 桥接据
 * modeling diff 实际范围判定），本单元透传至呈现数据并在注释登记两卡
 * 同步义务。条目永远以 diff 实际产出为准，绝不因警告补造条目。
 */
inline constexpr const char* kComparisonWarningRatioOutOfScope = "ratio-diff-out-of-scope";

// =====================================================================
// 指标取数投影（ISchemeMetricPort 的取数值——只读投影纪律）
// =====================================================================

/**
 * @brief 单方案指标投影快照（§8.1"从各域已归档结果投影读取（只读）"的
 *        取数值——L5 端口折叠产物，编排核零加工透传）。
 *
 * 字段级来源锚：
 *   - branch/label：方案分支身份与显示名（branchTips() 投影透传——§8.1
 *     "分支清单与 baseRevisionId 来自 project ProjectMetadata 查询"；
 *     label 是 UX-02 工程用语呈现值）；
 *   - metrics：指标列（词表/方向/单位/可算性**归对端**——optimization
 *     metricDefinitions() 词表的投影或各域归档结果投影，本单元零指标
 *     口径知识 §8.1 红线；列序＝对端冻结序，各方案同构）；
 *   - baseline：比较基准身份（envelope 提取归端口实现方——evidence.md
 *     §6.5 ComparisonBaseline"从各结果的 envelope 提取"；EVI-02/RPT-04
 *     检查输入）；
 *   - currentness：该方案结果当前性投影（搬运不计算——C-7 同款；§8.1
 *     "evidence 当前性投影标注过期项"的事实载体，呈现面据此对过期项
 *     附加限定，UX-10"结果过期附原因"）。
 *
 * 值语义；线程安全：纯值（端口每次调用返回完整快照——快照投影纪律）。
 */
struct SchemeMetricFacts {
    /**
     * @brief 单个指标单元格的取数半区（端口侧——呈现渲染由组装核完成）。
     */
    struct Metric {
        /// 指标身份 token（对端词表透传——如 optimization MetricId 词形的
        /// 稳定 token；本单元不解释其语义，仅作行对齐键与透传）。
        std::string metricKey;
        /// 指标列头文案键（对端权威透传——值归 ui 文案表）。
        std::string labelKey;
        /// SI 单位符号（对端权威透传——"m"/"kg"/"s"/"J"/"1"；空串＝单位
        /// 口径未冻结，呈现侧不伪造单位）。
        std::string unitToken;
        /// 指标值（SI 真值；nullopt＝**不可算**——OPT-07"不可算项显示
        /// '—'"的数据面承载，呈现绝不伪造数值）。
        std::optional<double> value;
        /// 该指标来源结果过期（evidence 当前性投影的指标级标注——端口
        /// 从归档结果投影折叠；true＝呈现面附加过期限定）。
        bool stale = false;
    };

    /// 方案分支身份（brn- 规范文本承载——buildComparison 入参逐位对应）。
    core::BranchId branch{};
    /// 方案分支在投影快照中可用（true＝分支存在且归档结果可读——@pre
    /// "方案分支存在且可读"的端口侧判定；false＝编排核 fail-fast）。
    bool available = false;
    /// 方案显示名（branchTips().label 透传；UTF-8）。
    std::string label;
    /// 指标列（列序＝对端冻结序；各方案同构——同构性由组装核校验违约
    /// fail-fast，防高亮行列错位）。
    std::vector<Metric> metrics;
    /// 比较基准身份（EVI-02/RPT-04 检查输入——envelope 提取归端口实现方）。
    evidence::ComparisonBaseline baseline{};
    /// 方案结果当前性投影（搬运零重算——nullopt＝不可判定计算形态，
    /// 不得当作 Current 使用；语义锚 evidence §8.1）。
    ui::CurrentnessProjection currentness{};

    bool operator==(const SchemeMetricFacts& o) const;
    bool operator!=(const SchemeMetricFacts& o) const { return !(*this == o); }
};

// =====================================================================
// Model Diff 投影（IComparisonDiffPort 的取数值——MDL-08 数据实体的
// 字符串化通道投影，零语义增殖；diff 计算归 modeling，本单元零 diff 实现）
// =====================================================================

/**
 * @brief 两方案 diff 的一条增量条目（modeling.md §9.4.9 ModelDiffEntry 的
 *        **值语义同构投影**——optimization CandidateDiffEntry 同款先例）。
 *
 * 零语义增殖：字段集与 modeling 实体一一对应（定位四元组＋field＋变化
 * 双标记＋两侧确定性文本摘要）；group/kind 以 modeling 稳定 token 文本
 * 承载（"structure"/"parameters"/"properties"；"added"/"removed"/
 * "modified"）。与 optimization 投影的两点形态差（均为呈现承载、零语义
 * 增殖）：objectId 取 core::ObjectId 强类型（本比较视图的点击定位锚直接
 * 对接 ui::IUiNameResolver::resolveObjectId——V15-02；根对象字段条目为
 * 全零 ObjectId，isValid()==false）；group 分拣在编排核完成（见
 * ComparisonDiffBlock 三组分块——UX-13"按结构/参数（DH、轴线、限位）与
 * 物性分组呈现"），条目仍携带 group token 备查。
 *
 * 排序契约归 modeling diff 实现（组内 objectId→字段序→subjectPath→kind
 * 稳定序），本单元**不重排**（对端产出直通——optimization T07 同款纪律）。
 *
 * 线程安全：纯值。
 */
struct SchemeDiffEntry {
    /// 差异分组 token（modeling 三值词表——"structure"/"parameters"/
    /// "properties"；未知 token＝对端违约，编排核 fail-fast）。
    std::string group;
    /// 变化三态 token（"added"/"removed"/"modified"——观察方向＝candidate
    /// 相对 baseline，语义归 modeling 实体）。
    std::string kind;
    /// 点击定位锚（ARC-04 跨修订稳定；isValid()==false＝根对象字段条目——
    /// 无对象可定位，呈现面禁用点击）。
    core::ObjectId objectId{};
    /// 值模型内字段定位路径（如 "joints[2].axis"——呈现列键素材）。
    std::string subjectPath;
    /// 叶字段名（如 "axis"/"mass"——呈现列键）。
    std::string field;
    /// 值/状态面不同（语义归 modeling 实体——透传）。
    bool valueChanged = false;
    /// 来源标记不同（语义归 modeling 实体——透传）。
    bool provenanceChanged = false;
    /// 基线侧确定性值摘要（空串＝该侧不存在——语义归 modeling 实体）。
    std::string baselineText;
    /// 候选侧确定性值摘要（空串＝该侧不存在）。
    std::string candidateText;

    bool operator==(const SchemeDiffEntry& o) const;
    bool operator!=(const SchemeDiffEntry& o) const { return !(*this == o); }
};

/**
 * @brief 一次两方案 diff 的投影快照（IComparisonDiffPort 返回值）。
 *
 * entries/warnings 语义（P-OPT-8）：条目永远以 diff 实际产出为准（空差集
 * ＝空条目表——合法）；warnings 是"呈现面请如实告知用户该组差异可能不全"
 * 的限定语 token（kComparisonWarning* 常量集），由端口实现方按 modeling
 * diff 实际范围产出——**编排核零补造条目、零吞警告**（透传）。
 *
 * available==false：diff 通道未装配或输入不可还原（环境面）——编排核以
 * kComparisonWarningDiffSourceUnavailable 降级呈现（空条目＋警告），不炸
 * 宿主、不虚构差异（ITitleFactPort"环境失败折叠为安全缺省"同款纪律）。
 *
 * 线程安全：纯值。
 */
struct SchemeDiffFacts {
    /// diff 通道可用（true＝entries 为真实 diff 产出；false＝通道未装配
    /// ——entries 必空，由编排核补降级警告）。
    bool available = false;
    /// 差异条目（modeling diff 实际产出——零虚构；组内序＝对端稳定序）。
    std::vector<SchemeDiffEntry> entries;
    /// 限定语警告 token（kComparisonWarning* 词形；P-OPT-8 主承载——
    /// 端口实现方产出，编排核透传）。
    std::vector<std::string> warnings;
};

// =====================================================================
// 比较呈现数据（buildComparison 的编排产物——ui 宿主面渲染输入）
// =====================================================================

/**
 * @brief 一行指标 × N 方案的呈现值（八项指标差异高亮的行载体）。
 *
 * 行序＝端口返回的指标列序（对端冻结序透传——本单元不重排指标）；
 * cells[i] 与 buildComparison 入参 schemes[i] 逐位对应。
 *
 * 差异高亮语义（differs）：≥2 个可算值且**不全精确相等** ⇒ true——
 * 高亮是"该指标在方案间存在差异"的呈现信号（UX-13"差异高亮"）；零容差
 * 精确比较（无容差口径登记不发明阈值）；nullopt（不可算）不参与比较、
 * 绝不当作 0（"'—'语义 null≠0"）。
 *
 * 值语义；线程安全：纯值。
 */
struct ComparisonMetricRow {
    /// 单方案单元格（呈现值——displayText 已渲染，ui 直显）。
    struct Cell {
        /// 指标值（nullopt＝不可算——OPT-07；与 displayText 一致）。
        std::optional<double> value;
        /// 呈现文本：可算→确定性数字文本（17 位有效数字 classic locale
        /// ——NFR-COR-02 同 modeling 摘要渲染口径）；不可算→"—"
        /// （kMetricUnavailableDisplay——OPT-07 原文占位符）。
        std::string displayText;
        /// 该单元格来源结果过期（端口 stale 位透传——呈现面附加限定）。
        bool stale = false;

        bool operator==(const Cell& o) const
        {
            return value == o.value && displayText == o.displayText
                && stale == o.stale;
        }
        bool operator!=(const Cell& o) const { return !(*this == o); }
    };

    /// 指标身份 token（对端词表透传——机器可判读行键）。
    std::string metricKey;
    /// 指标列头文案键（对端权威透传——值归 ui 文案表）。
    std::string labelKey;
    /// SI 单位符号（对端权威透传；空串＝未冻结不伪造）。
    std::string unitToken;
    /// 逐方案单元格（与方案序一致——列错位＝对端同构性违约，组装核已挡）。
    std::vector<Cell> cells;
    /// 差异高亮位（见类型注释——零容差精确比较）。
    bool differs = false;

    bool operator==(const ComparisonMetricRow& o) const;
    bool operator!=(const ComparisonMetricRow& o) const { return !(*this == o); }
};

/**
 * @brief 一对方案的 Model Diff 分组呈现块（UX-13"按结构/参数（DH、轴线、
 *        限位）与物性分组呈现"的块载体；AT-12 呈现侧）。
 *
 * 分组规则：端口条目按 group token 分拣到 structure/parameters/properties
 * 三组（modeling 词表三值——分拣是**呈现组织**不是 diff 语义实现：不解释
 * 字段含义、不增删条目、不重排组内序）；未知 token＝对端违约 fail-fast。
 *
 * baseline/candidate 方向语义归 modeling 实体（Added＝candidate 相对
 * baseline 新增……）；基线位＝首方案（编排决定——见文件头实现口径说明）。
 *
 * 值语义；线程安全：纯值。
 */
struct ComparisonDiffBlock {
    /// 基线方案分支（首方案）。
    core::BranchId baseline{};
    /// 候选方案分支（第 i 方案，i≥1）。
    core::BranchId candidate{};
    /// 基线方案显示名（label 透传——块头呈现素材）。
    std::string baselineLabel;
    /// 候选方案显示名（label 透传）。
    std::string candidateLabel;

    /// 结构组条目（模型构成——根元字段/权威/基座/集合成员/链序/引用表/
    /// 资源清单；组内序＝对端稳定序直通）。
    std::vector<SchemeDiffEntry> structure;
    /// 参数组条目（DH、轴线、限位——axis/origin/zeroOffset/bounds/
    /// workingRange/dhDerived；MDL-08 分组语义归 modeling）。
    std::vector<SchemeDiffEntry> parameters;
    /// 物性组条目（质量/质心/惯量/材料；MDL-05 分层）。
    std::vector<SchemeDiffEntry> properties;

    /// 限定语警告 token（kComparisonWarning* 词形透传——P-OPT-8"范围外
    /// 给警告不虚构差异"；含 diff 通道不可用的降级警告）。
    std::vector<std::string> warnings;

    bool operator==(const ComparisonDiffBlock& o) const;
    bool operator!=(const ComparisonDiffBlock& o) const { return !(*this == o); }
};

/**
 * @brief 方案比较视图数据（buildComparison 接受态产物——§8.2 数据流的
 *        "呈现"节点输入；ui 宿主面按需渲染：指标表逐行渲染高亮位、diff
 *        块分组渲染、条目点击经 objectId 定位）。
 *
 * 确定性：同（schemes, 端口快照）同输出（NFR-COR-02——全部组装步骤纯
 * 函数）。会话态数据（无 schema 版本——§10.3 版本行）。
 */
struct ComparisonViewData {
    /// 方案显示名清单（与入参 schemes 同序——列头/块头呈现素材）。
    std::vector<std::string> schemeLabels;
    /// 指标行集（端口指标列序——八项全部展示：可算项带值，不可算项
    /// displayText＝"—"；OPT-07"全部展示"的编排面兑现）。
    std::vector<ComparisonMetricRow> metricRows;
    /// diff 分组块集（n-1 块——首方案为基线，其余逐对；空差集＝空条目
    /// 的空块仍产出入列，呈现面渲染"无差异"而不是缺块）。
    std::vector<ComparisonDiffBlock> diffs;

    bool operator==(const ComparisonViewData& o) const;
    bool operator!=(const ComparisonViewData& o) const { return !(*this == o); }
};

/**
 * @brief 比较拒绝结果（EVI-02/RPT-04——基准不一致时**不产出**比较数据，
 *        改为拒绝＋原因提示；§8.2 数据流"不一致→拒绝比较＋原因提示"）。
 *
 * 拒绝不是错误：比较基准不一致是用户可理解、可修复的业务分支（换方案
 * 或重算对齐基准），走返回值而非异常——异常留给调用方契约违约（fail-fast）。
 *
 * 呈现三字段（UX-03）：原因（reasonKey 主文案＋detailKeys 逐维度明细）、
 * 建议动作（actionKey）、对象/上下文（dimensions 维度词表——evidence
 * 检查产出透传，本单元零重判定）。
 *
 * 值语义；线程安全：纯值。
 */
struct ComparisonRejection {
    /// 差异维度清单（evidence checkComparisonBaselinesConsistent 产出
    /// 透传——去重、按枚举声明序；本单元零基准判定实现）。
    std::vector<evidence::BaselineDifferenceDimension> dimensions;
    /// 拒绝主原因文案键（kComparisonBaselineMismatchKey——恒填充）。
    std::string reasonKey;
    /// 建议动作文案键（kComparisonBaselineMismatchActionKey——恒填充）。
    std::string actionKey;
    /// 逐维度明细键（与 dimensions 一一对应——comparisonBaselineDimensionKey
    /// 构造；UX-03"原因含定位"的明细半区）。
    std::vector<std::string> detailKeys;

    bool operator==(const ComparisonRejection& o) const
    {
        return dimensions == o.dimensions && reasonKey == o.reasonKey
            && actionKey == o.actionKey && detailKeys == o.detailKeys;
    }
    bool operator!=(const ComparisonRejection& o) const { return !(*this == o); }
};

/**
 * @brief buildComparison 的编排结果（Draft @return 未点名值承载——v0.5
 *        OpenProjectOutcome 同款登记口径；acceptance 2"拒绝比较并提示"
 *        的返回值通道）。
 *
 * 互斥契约：accepted==true ⇒ view 有值、rejection 无值；false 反之
 * （测试钉住——两态并存或双空均为编排缺陷）。
 */
struct ComparisonOutcome {
    /// true＝基准一致、比较数据已组装；false＝基准不一致已拒绝。
    bool accepted = false;
    /// 接受态产物（accepted==true 时必有值）。
    std::optional<ComparisonViewData> view;
    /// 拒绝态产物（accepted==false 时必有值）。
    std::optional<ComparisonRejection> rejection;

    bool operator==(const ComparisonOutcome& o) const
    {
        return accepted == o.accepted && view == o.view
            && rejection == o.rejection;
    }
    bool operator!=(const ComparisonOutcome& o) const { return !(*this == o); }
};

// =====================================================================
// 纯函数组装核（D-WF-3 同型——同输入同输出；模型测试直调面）
// =====================================================================

/**
 * @brief 渲染指标值的确定性呈现文本（值单元格 displayText 的唯一渲染点
 *        ——NFR-MNT-03）。
 *
 * 规则：有限值→17 位有效数字、classic locale（小数点 '.'、无千分位——
 * 同 modeling 值摘要渲染口径，NFR-COR-02 跨平台字节稳定）；非有限值
 * （NaN/Inf）→"—"（不可算同占位——非有限值不是可呈现的工程量，OPT-07
 * 不可算面同口径；绝不渲染 "nan"/"inf" 字样误导用户）。
 *
 * @param v [in] 指标值（SI 真值；单位语义由字段定义承载，本函数不附加）
 * @return 呈现文本（UTF-8/ASCII；确定性）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出。
 */
std::string renderMetricValueText(double v);

/**
 * @brief 基准差异维度 → 明细文案键（拒绝提示逐维度行的唯一构造点——
 *        NFR-MNT-03，禁止调用方自拼键串）。
 *
 * 键形："comparison.baseline-mismatch.dim.<token>"（token＝维度稳定词形：
 * project/branch/input-baseline/required-case-set/sample-sets——与
 * evidence::BaselineDifferenceDimension 枚举声明序一一对应的 kebab 词形，
 * 值归 ui 文案表）。
 *
 * @param dimension [in] 差异维度（evidence 检查产出）
 * @return 明细文案键（词表外值防御性返回空串——不伪造键，adviceTitleKey
 *         同款未知 token 口径）
 *
 * @threadSafe const 纯函数，可并发。
 */
std::string comparisonBaselineDimensionKey(
    evidence::BaselineDifferenceDimension dimension);

/**
 * @brief 组装指标行集（SchemeMetricFacts 集 → 逐指标高亮行——§8.2
 *        "差异高亮数据（逐指标）"节点的纯函数半区）。
 *
 * 编排规则（逐步）：
 *   1 前置校验：facts 为空 → WorkflowError（调用方错误——编排器已保证
 *     ≥2 方案，空集属上游组装违约）；任一 facts.available==false →
 *     WorkflowError（@pre"方案分支存在且可读"违约——fail-fast）。
 *   2 列同构校验：以首方案指标列为基准（metricKey 逐位），其余方案逐一
 *     对齐——任一错位 → WorkflowError（端口对端契约违约：列错位会让
 *     高亮行张冠李戴，绝不容忍）。
 *   3 逐指标组装行：metricKey/labelKey/unitToken 自首方案列透传（对端
 *     权威——零加工）；逐方案 Cell{value, displayText=renderMetricValueText,
 *     stale}；differs＝可算值 ≥2 且不全精确相等（零容差——见类型注）。
 *
 * @param facts [in] 逐方案指标投影快照（与方案序一致；≥2 份——编排器
 *              已校验，本函数复核）
 * @return 指标行集（行序＝端口指标列序——不重排）
 *
 * @throws WorkflowError 空集/不可用快照/指标列不同构（调用方或对端契约
 *         违约——fail-fast）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出。
 */
std::vector<ComparisonMetricRow> assembleMetricRows(
    const std::vector<SchemeMetricFacts>& facts);

/**
 * @brief 组装一对方案的 diff 分组块（SchemeDiffFacts → 三组分块＋警告
 *        透传——§8.2"Model Diff（modeling 数据实体）"节点的纯函数半区）。
 *
 * 编排规则（逐步）：
 *   1 通道可用性分派：facts.available==false → 空三组＋追加
 *     kComparisonWarningDiffSourceUnavailable 降级警告（环境面诚实降级
 *     ——不虚构条目）；可用 → 按条目 group token 分拣三组（"structure"/
 *     "parameters"/"properties"——modeling 词表；未知 token → WorkflowError
 *     对端违约 fail-fast），组内序＝对端序直通（不重排——排序契约归
 *     modeling diff 实现）。
 *   2 警告透传：facts.warnings 逐条追加（零加工零吞——P-OPT-8 通道；
 *     条目集仍逐条来自端口产出，绝不因警告补造条目）。
 *   3 块头：baseline/candidate 身份与 label 透传（呈现素材）。
 *
 * @param facts          [in] diff 投影快照（端口产出）
 * @param baseline       [in] 基线方案分支（首方案）
 * @param candidate      [in] 候选方案分支
 * @param baselineLabel  [in] 基线方案显示名（透传）
 * @param candidateLabel [in] 候选方案显示名（透传）
 * @return diff 分组块（空差集＝三组空表的空块——合法值对象，呈现面渲染
 *         "无差异"）
 *
 * @throws WorkflowError 未知分组 token（对端契约违约——fail-fast）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出。
 */
ComparisonDiffBlock assembleDiffBlock(const SchemeDiffFacts& facts,
                                      const core::BranchId& baseline,
                                      const core::BranchId& candidate,
                                      const std::string& baselineLabel,
                                      const std::string& candidateLabel);

// =====================================================================
// O7 方案比较编排服务（§10.2 Draft 接口 ISchemeComparisonController 逐字
// 兑现＋端口组合实现——比较视图唯一公共入口）
// =====================================================================

/**
 * @brief 方案比较指标取数端口（§8.1"从各域已归档结果投影读取"的接缝——
 *        实现归 L5 装配层，本单元不实现）。
 *
 * 谁实现：L5 装配层——从 project 查询面（branchTips().label）、各域归档
 * 结果投影（指标值/可算性/单位——对端词表）与 evidence 当前性投影折叠。
 * 本单元是消费方，不实现端口（编排单元不拥有指标口径——§8.1 红线/N9）。
 *
 * 错误语义：实现方对"分支不存在/归档结果不可读"折叠为 available=false
 * （编排核 fail-fast——@pre 违约的判定材料）；环境失败不抛越本端口的
 * 异常（快照值语义无错误轨——ITitleFactPort 同款纪律）。
 *
 * @threadSafe const 只读——buildComparison 的并发安全由本方法保证
 *            （§10.3 buildComparison 行 const 并发安全的对端承诺）。
 */
class ISchemeMetricPort {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~ISchemeMetricPort() = default;

    /**
     * @brief 收集逐方案指标投影快照（每次调用返回完整快照）。
     *
     * @param schemes [in] 方案分支清单（与 buildComparison 入参一致；
     *                调用方持有，本方法不修改）
     * @return 快照集（与 schemes 同序同长——逐位对应；分支不可用项
     *         available=false，不剔除不重排）
     */
    virtual std::vector<SchemeMetricFacts> collect(
        const std::vector<core::BranchId>& schemes) const = 0;
};

/**
 * @brief 方案 diff 取数端口（§8.1"消费 modeling IModelDiffService::diff
 *        产出"的接缝——实现归 L5 装配层，本单元不实现）。
 *
 * 谁实现：L5 装配层——从 project 读两分支基线设计的 canonical 字节、经
 * modeling 公共契约还原工作集并调 IModelDiffService::diff，把 ModelDiff-
 * Report 逐条目文本化（SchemeDiffEntry 同构投影——optimization
 * ICandidateDiffSource 同款桥接先例）；P-OPT-8 警告在此产出（对端据
 * modeling diff 实际范围判定"传动比差异范围外"）。
 *
 * 错误语义：通道未装配/输入不可还原 → 返回 available=false（环境面诚实
 * 降级——编排核补降级警告，不炸宿主）；**不虚构条目**（P-OPT-8 红线）。
 *
 * @threadSafe const 只读——buildComparison 的并发安全由本方法保证。
 */
class IComparisonDiffPort {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~IComparisonDiffPort() = default;

    /**
     * @brief 生成基线→候选方向的 diff 投影快照。
     *
     * @param baseline  [in] 基线方案分支
     * @param candidate [in] 候选方案分支
     * @return diff 快照（available=false＝通道未装配——entries 必空）
     */
    virtual SchemeDiffFacts diff(const core::BranchId& baseline,
                                 const core::BranchId& candidate) const = 0;
};

/**
 * @brief 方案比较视图编排服务接口（§10.2 Draft 逐字——O7 的公共入口；
 *        呈现归 ui 宿主面，本接口只产出编排数据）。
 *
 * 消费方式：ui 宿主面/L5 装配持有本接口引用，用户确认比较时调用一次
 * （现取现组装——无缓存，事实新鲜度由端口快照保证）。
 */
class ISchemeComparisonController {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~ISchemeComparisonController() = default;

    /**
     * @brief 组装 2~4 方案比较数据（八项指标投影取数＋Model Diff 消费）。
     *
     * @param schemes [in] 方案分支清单（2~4 个；首方案为基线——编排
     *                决定见文件头实现口径说明）
     * @return 接受态（比较数据）或拒绝态（基准不一致＋原因——EVI-02/
     *         RPT-04；拒绝不是错误）
     *
     * @pre 方案分支存在且可读；基准不一致 → 拒绝并给原因（EVI-02）——
     *      Draft @pre 原文；分支不可读走 WorkflowError（调用方错误
     *      fail-fast——前置校验材料来自端口快照 available 位）
     *
     * @throws WorkflowError 方案数非 2~4／重复分支／分支不可用快照／
     *         指标列不同构／未知 diff 分组 token（调用方或对端契约违约
     *         ——fail-fast；逐条语义见各数据类型注释）
     *
     * @threadSafe const 并发安全（§10.3 行——两端口 const 的对端承诺
     *            传导）。
     * @determinism 同（schemes, 端口快照）同输出（NFR-COR-02）。
     */
    virtual ComparisonOutcome buildComparison(
        const std::vector<core::BranchId>& schemes) const = 0;
};

/**
 * @brief 方案比较编排服务实现（ISchemeComparisonController 的端口组合
 *        形态——两取数端口的编排装配点；StatusProjectionProvider 同款）。
 *
 * 编排流程（§8.2 数据流逐步兑现）：
 *   1 前置校验：方案数 ∈ [2,4]（UX-13）；无重复分支（自比无比较语义）——
 *     违约 WorkflowError fail-fast。
 *   2 指标投影取数：metricPort.collect(schemes)（只读投影）。
 *   3 可用性检查：任一 available==false → WorkflowError（@pre 违约）。
 *   4 基准一致性检查（EVI-02/RPT-04）：收集全部 baseline →
 *     evidence::checkComparisonBaselinesConsistent（公共契约消费——检查
 *     实现归 evidence，零重实现）；不一致 → Rejected{dimensions 透传＋
 *     键半区填充}（不产出混基准比较数据——acceptance 2）。
 *   5 指标行组装：assembleMetricRows（高亮判定＋"—"渲染）。
 *   6 diff 取数与组装：i∈[1,n) 逐对 diffPort.diff(schemes[0], schemes[i])
 *     → assembleDiffBlock（三组分拣＋警告透传；通道不可用降级警告）。
 *   7 返回 Accepted{view}。
 *
 * 生命周期与所有权：两端口以引用注入（非 owning——调用方保证端口存活
 * 期覆盖本服务）；本服务无会话状态（每次调用现取现组装），析构零动作。
 *
 * 线程约束：buildComparison() const 并发安全（前置＝两端口 const 并发
 * 安全）。构造前置：两引用天然非空（引用语义）——无空指针违约空间。
 */
class SchemeComparisonController final : public ISchemeComparisonController {
public:
    /**
     * @brief 构造（装配点——L5/测试装配两端口引用）。
     *
     * @param metricPort [in] 指标取数端口（非 owning——存活期覆盖本服务）
     * @param diffPort   [in] diff 取数端口（非 owning——同上）
     */
    SchemeComparisonController(const ISchemeMetricPort& metricPort,
                               const IComparisonDiffPort& diffPort) noexcept;

    /// @copydoc ISchemeComparisonController::buildComparison
    ComparisonOutcome buildComparison(
        const std::vector<core::BranchId>& schemes) const override;

private:
    const ISchemeMetricPort* m_metricPort;   ///< 指标取数端口（非 owning——见构造注）
    const IComparisonDiffPort* m_diffPort;   ///< diff 取数端口（非 owning——同上）
};

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_COMPARISON_HPP
