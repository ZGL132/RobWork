/**
 * @file   ModelingCommandFlows.hpp
 * @brief  建模域十条命令的 UI 流程编排（UI-T41 批次 A——对话框/文件选择
 *         与建模内核纯函数的装配层；域逻辑零重写，全部经
 *         TemplateFactory/ModelImportMapper/XacroExpandService/
 *         DhExplicitConverter/PropertyEstimator/ModelDiffService/
 *         ModelPackagePort 既有实现类）。
 *
 * 设计依据：
 *   - units/modeling.md §9.7.3（十条域命令语义权威——本文件逐命令对应卡表
 *     行）、§9.7.2 L-9/L-10（权威切换流/导入向导域侧最小链）、§9.7.4
 *     （仅 UI 线程；对话框模态亦 UI 线程）；
 *   - 面板命令按钮→setCommandSubmit→宿主注册表→模块 executeDomainCommand
 *     的唯一路由（PA-1——本文件是被调用端，requirements 域
 *     RequirementsCommandFlows 同构先例）。
 *
 * 批次边界（诚实声明）：本层是"最小真实链路"——导入不经完整向导报告页
 * （L-10 完整形态归后续任务），以确认对话框呈现映射报告摘要；规范包导出
 * 自当前会话草稿闭包（ObjectClosureView 值源），不触项目存储读取路径。
 * 偏差登记于 DTB §5.4（UI-T41 行）。
 *
 * 线程模型：全部 UI 线程（模态对话框＋工作集编辑）。
 * 反馈面：返回值＋中文摘要（面板状态行呈现——UX-02）；失败不抛（域值面
 * 错误如实转述），调用方错误（未知命令 id）fail-fast。
 */

#ifndef IRD_MODELING_PLUGIN_MODELINGCOMMANDFLOWS_HPP
#define IRD_MODELING_PLUGIN_MODELINGCOMMANDFLOWS_HPP

#include <functional>
#include <optional>
#include <QString>
#include <QStringList>
#include <string>

#include "ModelingUiModule.hpp"  // ModuleSessionState（会话权威态——编辑落点）

namespace sdurws {
namespace ird {
namespace core {
class ObjectId;  // 前向声明（选中锚入参——完整类型经 Identity.hpp 随 cpp）
}  // namespace core
}  // namespace ird
}  // namespace sdurws

namespace sdurws::ird::modeling {

/**
 * @brief 命令对话框宿主（测试缝——requirements CommandDialogHost 同案）。
 *
 * 动机：命令流程的交互是模态对话框（QFileDialog/QMessageBox）——gui 测试
 * 进程会被模态阻塞。本抽象把**交互应答**与**流程编排**分离：生产装配用
 * Qt 对话框（行为与直调 Qt 一致）；测试注入预置应答替身。
 *
 * 值面约定：返回 nullopt/false＝用户取消（流程静默终止——非失败）。
 */

/**
 * @brief custom-chain 单轴声明值（UI-T63——从零创建面的表单行载体；纯值）。
 *
 * R1 合规创建面＝六轴全旋转：类型不入本载体（种子恒 Revolute——prismatic/
 * continuous/fixed 的创建声明受 creationEntryGuard 需求分期红线约束，归
 * 所有者裁决通道）。单位：轴线无量纲、零位/限位随关节类型（rad）——
 * Origin 平移 m／姿态 rad（ZYX）。
 */
struct CustomChainJointSpec {
    double axisX = 0.0;   ///< 关节轴线 x 分量（无量纲，可归一化——I-MDL-6）
    double axisY = 0.0;   ///< 关节轴线 y 分量
    double axisZ = 1.0;   ///< 关节轴线 z 分量（缺省 z 轴——T-MDL-1 J1 同款）
    double zero = 0.0;    ///< 零位偏置（rad）
    double lower = -1.5;  ///< 限位下限（rad）
    double upper = 1.5;   ///< 限位上限（rad）
    double x = 0.0;       ///< Origin 平移 x（m，父连杆系）
    double y = 0.0;       ///< Origin 平移 y（m）
    double z = 0.0;       ///< Origin 平移 z（m）
    double roll = 0.0;    ///< Origin 姿态 roll（rad，ZYX）
    double pitch = 0.0;   ///< Origin 姿态 pitch（rad）
    double yaw = 0.0;     ///< Origin 姿态 yaw（rad）

    bool operator==(const CustomChainJointSpec& o) const noexcept
    {
        return axisX == o.axisX && axisY == o.axisY && axisZ == o.axisZ
            && zero == o.zero && lower == o.lower && upper == o.upper
            && x == o.x && y == o.y && z == o.z && roll == o.roll
            && pitch == o.pitch && yaw == o.yaw;
    }
    bool operator!=(const CustomChainJointSpec& o) const noexcept { return !(*this == o); }
};

/**
 * @brief custom-chain 六轴声明集（UI-T63——恰六项；R1 合规创建面轴数固定）。
 */
struct CustomChainDeclaration {
    std::vector<CustomChainJointSpec> joints;  ///< 恰六项（链序 j1..j6）

    bool operator==(const CustomChainDeclaration& o) const noexcept
    {
        return joints == o.joints;
    }
    bool operator!=(const CustomChainDeclaration& o) const noexcept { return !(*this == o); }
};

struct ModelingDialogHost {
    virtual ~ModelingDialogHost() = default;

    /// 导入源路径（QFileDialog::getOpenFileName 应答面）。
    virtual std::optional<QString> openFilePath(const QString& title,
                                                const QString& filter) = 0;
    /// 导出目标路径（QFileDialog::getSaveFileName 应答面）。
    virtual std::optional<QString> saveFilePath(const QString& title,
                                                const QString& defaultName,
                                                const QString& filter) = 0;
    /// 条目选择（QInputDialog::getItem 应答面——返回 items 下标；导入选链）。
    virtual std::optional<int> chooseItem(const QString& title,
                                          const QString& label,
                                          const QStringList& items) = 0;
    /// 破坏性操作确认（QMessageBox::question 应答面——是＝继续）。
    virtual bool confirmProceed(const QString& title, const QString& text) = 0;
    /// 导入报告确认（计数摘要＋行级清单——确认＝草稿落盘）。
    virtual bool confirmImport(const QString& summaryText) = 0;
    /// 结论呈现（权威切换判定报告/diff 报告——纯知会，仅"确定"钮）。
    virtual void showInfo(const QString& title, const QString& text) = 0;

    /**
     * @brief custom-chain 六轴声明表单（UI-T63——从零创建面；nullopt＝取消）。
     *
     * 生产＝Qt 模态表单（六行×[类型锁 Revolute｜轴线 xyz｜零位｜限位下/上限｜
     * Origin xyz/r/p/y]——R1 合规创建面＝六轴全旋转，4/5 轴与含 prismatic
     * 链的创建确认仍受 creationEntryGuard 红线约束〔需求分期口径，本批不
     * 触〕）；替身＝测试缝预置应答。seedDefaults＝表单预填种子（T-MDL-1
     * J1 行——sixAxisTemplateDefaults().front() 单一权威）。
     */
    virtual std::optional<std::vector<CustomChainJointSpec>> declareCustomChain(
        const QString& title, const CustomChainJointSpec& seedDefaults) = 0;
};

/// 生产宿主（真实 Qt 模态——flows 的缺省装配；无状态）。
ModelingDialogHost& qtModelingDialogHost();

/**
 * @brief custom-chain 单轴声明的语义即时校验（UI-T66——F-523① 的录入
 *        辅助面；**域拒绝面兜底语义不变**——本函数只是把域裁决的两个
 *        同口径规则前置到表单关闭前，定位到行提示）。
 *
 * 规则（与域判定同口径）：
 *   - I-MDL-4 限位有序：lower < upper（Revolute 限位组合约束的表单侧
 *     前置——域侧对应 TypeBoundsConflict 族拒绝语义）；
 *   - I-MDL-6 轴非零：轴线三分量不全为零（可归一化前提的表单侧前置）。
 *
 * @param axisX/axisY/axisZ [in] 轴线分量（无量纲）
 * @param lower/upper       [in] 限位下/上限（rad）
 * @return nullopt＝通过；有值＝拒绝原因（人读中文，含需求 ID 锚——
 *         表单 errLabel 直投）
 *
 * 纯函数；确定性（同输入同文案）；零 Qt 依赖（可单元直测）。
 */
std::optional<std::string> customChainRowSemanticCheck(double axisX, double axisY,
                                                       double axisZ,
                                                       double lower, double upper);

/**
 * @brief 命令流程的宿主侧依赖（UI-T41——模块注入的回调面；零模块类型
 *        依赖，测试可全替身）。
 */
struct ModelingFlowDeps {
    /// 模板重种子（new-from-template 落点——模块 seedTemplateSession 同义）。
    std::function<void()> reseedTemplate;
    /// custom-chain 声明重种子（UI-T63——从零创建落点：域原语组合
    /// createDraft(custom-chain)→addJointAt→applyJointFieldEdit 逐轴声明；
    /// false＝声明非法/域拒绝，原因入 summary）。
    std::function<bool(const CustomChainDeclaration&, std::string&)> reseedCustomChain;
    /// 就绪重算（草稿变更后——模块 recomputeReadiness 同义）。
    std::function<void()> recomputeReadiness;
    /// 当前选中锚（面板会话选中态——estimate-properties 的目标解析输入）。
    std::function<std::optional<core::ObjectId>()> selectedAnchor;
    /// WC/DWC XML 外供导出（UI-T56——export-workcell-xml 落点）：宿主注入
    /// 的整体回调（取编译快照→域 exportWorkCellXml→摘要形成）。快照缺席
    /// ＝false＋原因入 summary（诚实缺席不虚构产物）；目标路径来自
    /// saveFilePath 替身/真身。
    std::function<bool(const std::string& targetPath, std::string& summary)> exportWorkCellXml;
};

/**
 * @brief 执行一条建模域命令的 UI 流程（十条统一入口——测试缝重载）。
 *
 * 路由语义（§9.7.3 卡表逐行）：new-from-template＝模板重种子（有未应用
 * 编辑先确认）；import-urdf/import-xacro＝选文件→内核映射→报告确认→
 * 草稿落盘；switch-authority＝L-9 判定先行（Exact/ExactNonUnique 才落
 * 切换）；estimate-properties＝选中连杆物性估算（来源徽标 estimated）；
 * generate-placeholder-geometry＝相邻关节原点占位圆柱批量生成；
 * diff-baseline＝与最近应用基线的 ModelDiff 查看（只读）；
 * export-package/import-package＝MDL-20 规范包导出/导入；reset-home-zero
 * ＝位姿集参考数据读取呈报（会话命令零修订——MDL-17/§4.6 行 386）。
 *
 * @param commandId [in] 域命令 id（点分小写——未知 id＝调用方违约 fail-fast）
 * @param session   [in,out] 会话权威态（草稿编辑落点——仅 UI 线程）
 * @param deps      [in] 宿主侧回调（见 ModelingFlowDeps）
 * @param host      [in] 对话框宿主（生产＝qtModelingDialogHost()）
 * @param summary   [out] 中文回执/原因（面板状态行呈现；取消＝空串）
 * @return true＝流程执行完成（含域内拒绝的诚实应答——summary 带原因）；
 *         false＝用户取消（summary 空）或流程失败（summary 带原因）
 */
bool executeModelingCommand(const std::string& commandId,
                            ModuleSessionState& session,
                            const ModelingFlowDeps& deps,
                            ModelingDialogHost& host,
                            std::string& summary);

/// 生产装配重载（host＝qtModelingDialogHost()——模块 executeDomainCommand 消费）。
bool executeModelingCommand(const std::string& commandId,
                            ModuleSessionState& session,
                            const ModelingFlowDeps& deps,
                            std::string& summary);

/**
 * @brief 已装配域命令的权威判定（UI-T42——F-466 消账：宿主注册表路由集
 *        的唯一事实源从 UiPlugin 字面集合迁移至此，与 executeModelingCommand
 *        的路由词表同 TU 同步演化——"装配集∧目录集一致"有了可断言面）。
 *
 * @param commandId [in] 域命令 id（点分小写）
 * @return true＝该命令有真实执行链（宿主按 assembled 放行）；false＝保持
 *         注册期禁用（flow-not-assembled 诚实路径）
 */
bool isAssembledModelingCommand(const std::string& commandId);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_MODELINGCOMMANDFLOWS_HPP
