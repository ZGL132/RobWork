/**
 * @file   ModelingCommandFlows.cpp
 * @brief  建模域命令 UI 流程实现（UI-T41 批次 A——八命令真实链＋两条
 *         诚实缺席，见头注批次边界）。
 *
 * 实现纪律：域判定零入（全部经内核实现类）；文件读取用标准库 fstream
 * （用户经文件对话框自选路径——io 受管路径治理面针对项目内程序化路径，
 * 用户自选路径不适用；完整 io 端口接线随导入向导任务，偏差登记 DTB）。
 */

#include "ModelingCommandFlows.hpp"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "ModelingFlowsInternal.hpp"          // 域内拆分接口（权威切换流——DhConvert 独立 TU）

#include <sdurws/ird/core/Digest.hpp>            // core::ContentDigester（快照摘要——io 快照同源口径）
#include <sdurws/ird/modeling/Codec.hpp>         // RobotDesignCodec/ObjectVariant（闭包字节编码）
#include <sdurws/ird/modeling/Import.hpp>        // ModelImportMapper（URDF/Xacro 映射）
#include <sdurws/ird/modeling/ModelDiff.hpp>     // ModelDiffService（与基线比较）
#include <sdurws/ird/modeling/ObjectTypes.hpp>   // 五对象 token（闭包视图路由键）
#include <sdurws/ird/modeling/Package.hpp>       // ModelPackagePort（MDL-20 导出/导入）
#include "ModelingXacroBridge.hpp"            // 受控展开桥（独立 TU——XacroExpand 不可与 DhConvert 共 TU）
#include <sdurws/ird/ui/UiText.hpp>              // resolveText（按钮文案同源的呈现值）

namespace sdurws::ird::modeling {
namespace {

// =====================================================================
// 对话框宿主（生产装配——真实 Qt 模态；单例无状态）
// =====================================================================

/// 生产宿主实现（与 requirements QtDialogHost 同型——交互应答/编排分离）。
class QtModelingDialogHost final : public ModelingDialogHost {
public:
    std::optional<QString> openFilePath(const QString& title,
                                        const QString& filter) override
    {
        const QString path = QFileDialog::getOpenFileName(nullptr, title, QString(), filter);
        if (path.isEmpty()) { return std::nullopt; }  // 用户取消——流程静默终止
        return path;
    }

    std::optional<QString> saveFilePath(const QString& title,
                                        const QString& defaultName,
                                        const QString& filter) override
    {
        const QString path =
            QFileDialog::getSaveFileName(nullptr, title, defaultName, filter);
        if (path.isEmpty()) { return std::nullopt; }
        return path;
    }

    std::optional<int> chooseItem(const QString& title, const QString& label,
                                  const QStringList& items) override
    {
        const QString picked =
            QInputDialog::getItem(nullptr, title, label, items, 0, false);
        if (picked.isEmpty()) { return std::nullopt; }
        return items.indexOf(picked);
    }

    bool confirmProceed(const QString& title, const QString& text) override
    {
        return QMessageBox::question(nullptr, title, text)
               == QMessageBox::Yes;
    }

    bool confirmImport(const QString& summaryText) override
    {
        return QMessageBox::question(nullptr, QStringLiteral("导入确认"), summaryText)
               == QMessageBox::Yes;
    }

    void showInfo(const QString& title, const QString& text) override
    {
        QMessageBox::information(nullptr, title, text);
    }
};

}  // namespace

ModelingDialogHost& qtModelingDialogHost()
{
    static QtModelingDialogHost host;  // 无状态单例（进程级——UI 线程消费）
    return host;
}

bool executeModelingCommand(const std::string& commandId,
                            ModuleSessionState& session,
                            const ModelingFlowDeps& deps,
                            std::string& summary)
{
    return executeModelingCommand(commandId, session, deps,
                                  qtModelingDialogHost(), summary);
}

namespace {

// =====================================================================
// 流程内部辅助（纯函数/小工具——全部 UI 线程调用）
// =====================================================================

/// 读取本地文件全部字节（二进制；用户对话框自选路径——见文件头注）。
std::optional<std::vector<std::uint8_t>> readFileBytes(const QString& path,
                                                       std::string& error)
{
    std::ifstream in(path.toStdWString(), std::ios::binary);
    if (!in.is_open()) {
        error = "无法打开文件（不存在或无读取权限）";
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>()};
    if (bytes.empty()) {
        error = "文件为空（0 字节）——不是可导入的模型文档";
        return std::nullopt;
    }
    return bytes;
}

/// 构造入口资源快照（digest＝SHA-256 权威判据——io ResourceSnapshot 同源）。
io::ResourceSnapshot makeEntrySnapshot(const QString& path,
                                       const std::vector<std::uint8_t>& bytes)
{
    io::ResourceSnapshot snapshot;
    std::error_code ec;
    snapshot.finalPath = std::filesystem::weakly_canonical(path.toStdWString(), ec);
    if (ec) { snapshot.finalPath = path.toStdWString(); }  // 规范化失败退原始路径（追溯提示，不作身份）
    snapshot.sizeBytes = bytes.size();
    snapshot.mtimeUtc = 0;  // 预筛非权威字段——本链不取时钟（NFR-COR-02）
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    snapshot.contentDigest = digester.finalize();
    return snapshot;
}

/// 导入报告的人读摘要（确认对话框文本——四清单计数＋分支/错误如实呈现）。
QString importSummaryText(const ImportReport& report)
{
    QString text = QStringLiteral("导入映射报告（源：%1）\n\n"
                                  "字段映射：%2 项\n默认补全：%3 项\n"
                                  "忽略项：%4 项\n不支持项：%5 项\n"
                                  "待确认：%6 项\n错误项：%7 项")
                       .arg(QString::fromStdString(report.sourceLabel))
                       .arg(qulonglong(report.mapped.size()))
                       .arg(qulonglong(report.defaults.size()))
                       .arg(qulonglong(report.ignored.size()))
                       .arg(qulonglong(report.unsupported.size()))
                       .arg(qulonglong(report.pendingConfirms.size()))
                       .arg(qulonglong(report.errors.size()));
    if (!report.branches.empty()) {
        text += QStringLiteral("\n\n检测到分支链 %1 条：").arg(report.branches.size());
        for (const ImportBranchItem& b : report.branches) {
            text += QStringLiteral("\n  · %1").arg(QString::fromStdString(b.branchRoot));
        }
    }
    text += report.submittable
                ? QStringLiteral("\n\n该草稿可提交修订。")
                : QStringLiteral("\n\n该草稿存在不可提交面（错误项），仅可编辑检视。");
    return text;
}

// =====================================================================
// 规范包导出闭包视图（ObjectClosureView 的工作集适配——会话草稿五对象
// 的确定性编码字节源；与 L5 存储闭包同构——PA-1 不触 project 读取路径）
// =====================================================================

/// 闭包字节源（tryObjectByToken/tryObject 两方法——CanonicalBridge 契约）。
class WorkingSetClosureView final : public ObjectClosureView {
public:
    /// @param ws [in] 会话工作集（非 owning——调用期存活；仅 UI 线程可变）
    explicit WorkingSetClosureView(const ModelingWorkingSet& ws) : m_ws(ws) {}

    std::optional<ClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        // 根对象路由（§9.4.5——恰一个 robot-design）；其余 token 无根级对象。
        if (objectTypeToken != std::string_view(kRobotDesignObjectType)) {
            return std::nullopt;
        }
        return encodeObject(std::string{kRobotDesignObjectType},
                            ObjectVariant{m_ws.design});
    }

    std::optional<ClosureObject> tryObject(const core::ObjectId& objectId) const override
    {
        // 部件对象按身份取回（五对象表后四行——工作集已解码值视图直查）。
        for (const ToolDefinition& t : m_ws.toolObjects) {
            if (t.objectId == objectId) {
                return encodeObject(std::string(kToolDefinitionObjectType), ObjectVariant{t});
            }
        }
        for (const SceneObject& s : m_ws.sceneObjects) {
            if (s.objectId == objectId) {
                return encodeObject(std::string(kSceneObjectObjectType), ObjectVariant{s});
            }
        }
        if (m_ws.poseSetObject.has_value()
            && m_ws.poseSetObject->objectId == objectId) {
            return encodeObject(std::string(kNamedPoseSetObjectType),
                                ObjectVariant{*m_ws.poseSetObject});
        }
        if (m_ws.drivetrainObject.has_value()
            && m_ws.drivetrainObject->objectId == objectId) {
            return encodeObject(std::string(kRobotDrivetrainObjectType),
                                ObjectVariant{*m_ws.drivetrainObject});
        }
        return std::nullopt;  // 闭包外身份（固化资源对象不在草稿值视图——如实缺席）
    }

private:
    /// 确定性编码（Codec 单一权威——同对象同字节；失败上抛＝工作集违约）。
    static std::optional<ClosureObject> encodeObject(std::string token,
                                                     const ObjectVariant& variant)
    {
        const RobotDesignCodec codec;
        const auto encoded = codec.encode(variant, kCurrentFormatVersion);
        if (!encoded.ok()) {
            throw std::runtime_error("modeling 命令流：闭包对象编码失败（"
                                     + encoded.error().detail + "）");
        }
        ClosureObject object;
        object.objectTypeToken = std::move(token);
        object.bytes = encoded.get();
        return object;
    }

    const ModelingWorkingSet& m_ws;  ///< 会话工作集（非 owning）
};

/// 规范包导出流程（export-package——目标路径用户选定，原子写归 io 端口）。
bool runExportPackage(ModuleSessionState& session, ModelingDialogHost& host,
                      std::string& summary)
{
    const auto pickedPath = host.saveFilePath(
        QStringLiteral("导出规范模型包"), QStringLiteral("model.irdbundle"),
        QStringLiteral("规范模型包 (*.irdbundle);;所有文件 (*)"));
    if (!pickedPath.has_value()) { return false; }  // 用户取消
    const QString& path = *pickedPath;
    if (session.draft.design.joints.empty()) {
        summary = "草稿为空——无规范内容可导出";
        return false;
    }
    WorkingSetClosureView closure(session.draft);
    PackageExportTarget target;
    target.targetFile = path.toStdWString();
    // 替换策略缺省 OverwriteAtomic（io 原子替换——失败保留先前输出）；
    // createdAtUtc 留空（服务不取时钟——确定性口径；记录字段可省略）。
    ModelPackagePort port;
    std::vector<core::DiagnosticRecord> diags;
    const PackageExportOutcome outcome = port.exportPackage(closure, target, diags);
    if (!outcome.ok) {
        summary = "导出失败：" + outcome.error.detail;
        return false;
    }
    summary = "规范包已导出：" + path.toStdString();
    return true;
}

}  // namespace

// =====================================================================
// 十命令统一入口（逐命令流程——语义权威＝modeling.md §9.7.3 卡表行）
// =====================================================================

bool executeModelingCommand(const std::string& commandId,
                            ModuleSessionState& session,
                            const ModelingFlowDeps& deps,
                            ModelingDialogHost& host,
                            std::string& summary)
{
    summary.clear();

    // ---- modeling.new-from-template（模板新建——TemplateFactory→草稿）----
    if (commandId == "modeling.new-from-template") {
        if (!session.draft.changes.empty()) {
            // 有未应用编辑——破坏性重建先确认（SA-15 确认流的最小形态：
            // 面板级编辑丢弃不涉项目修订，QMessageBox 即够）。
            const bool proceed = host.confirmProceed(
                QStringLiteral("从模板新建"),
                QStringLiteral("当前草稿有 %1 条未应用编辑，从模板重建将全部丢弃。是否继续？")
                    .arg(session.draft.changes.size()));
            if (!proceed) { return false; }
        }
        if (!deps.reseedTemplate) {
            summary = "模板重种子出口未接线（装配缺陷）";
            return false;
        }
        deps.reseedTemplate();
        summary = "已从模板重建草稿（generic-6r）——未应用编辑已清空";
        return true;
    }

    // ---- modeling.import-urdf / import-xacro（导入——§6.1 映射管线最小链）----
    if (commandId == "modeling.import-urdf" || commandId == "modeling.import-xacro") {
        const bool isXacro = (commandId == "modeling.import-xacro");
        const QString path = [&] {
            const auto picked = host.openFilePath(
                isXacro ? QStringLiteral("导入 Xacro") : QStringLiteral("导入 URDF"),
                isXacro ? QStringLiteral("Xacro (*.xacro *.xml);;所有文件 (*)")
                        : QStringLiteral("URDF (*.urdf *.xml);;所有文件 (*)"));
            return picked.value_or(QString());
        }();
        if (path.isEmpty()) { return false; }  // 用户取消

        std::string error;
        const auto bytes = readFileBytes(path, error);
        if (!bytes.has_value()) {
            summary = "导入中止：" + error;
            return false;
        }

        std::vector<core::DiagnosticRecord> diags;
        const ModelImportMapper mapper;
        ImportOptions options;  // 单可动链默认（selectedMainBranch 空）
        ImportOutcome outcome;
        if (isXacro) {
            // Xacro 受控展开（P-MDL-4——护栏归 io/语义归 modeling）：展开经
            // ModelingXacroBridge（独立 TU——XacroExpand/DhConvert 同名值类型
            // 不可共 TU）；无用户参数代入（替换表空），产物走与 URDF 同映射。
            const ModelXacroExpandResult expanded = expandXacroBytes(*bytes, path);
            if (!expanded.ok) {
                summary = "Xacro 展开失败：" + expanded.errorDetail;
                return false;
            }
            ValidatedSource expandedSource;
            expandedSource.bytes = expanded.expandedBytes;
            expandedSource.entrySnapshot = expanded.entrySnapshot;  // 来源身份保持
            XacroProvenance provenance;
            provenance.sourceDigest = expanded.sourceDigest;
            provenance.sourceAbsPath = path.toStdString();
            outcome = mapper.mapXacroExpanded(expandedSource, provenance, options, diags);
        } else {
            ValidatedSource source;
            source.bytes = *bytes;
            source.entrySnapshot = makeEntrySnapshot(path, *bytes);
            outcome = mapper.mapUrdf(source, options, diags);
        }

        // 多可动分支：报告分支清单——用户显式选链后重映射一层（§6.4 维度一；
        // 每次调用解析一层分裂——选中分支内部再分裂经下一轮循环处理）。
        // Xacro 路径的选链重映射随导入向导任务补全（展开产物单次有效——诚实缺席）。
        if (isXacro && outcome.draft.has_value() == false
            && !outcome.report.branches.empty()) {
            summary = "Xacro 文件含多条可动分支链——显式选链映射随导入向导任务补全";
            return false;
        }
        if (!isXacro && outcome.draft.has_value() == false
            && !outcome.report.branches.empty()) {
            QStringList candidates;
            for (const ImportBranchItem& b : outcome.report.branches) {
                candidates << QString::fromStdString(b.branchRoot);
            }
            const auto picked = host.chooseItem(
                QStringLiteral("选择主链"),
                QStringLiteral("该文件含多条可动分支链——请选择建模主链（每次解析一层分裂）："),
                candidates);
            if (!picked.has_value()) { return false; }
            options.selectedMainBranch = outcome.report.branches[*picked].branchRoot;
            diags.clear();
            ValidatedSource source;
            source.bytes = *bytes;
            source.entrySnapshot = makeEntrySnapshot(path, *bytes);
            outcome = mapper.mapUrdf(source, options, diags);
        }

        if (outcome.error.has_value() && !outcome.draft.has_value()) {
            summary = "导入失败（" + std::string(importErrorCodeToken(outcome.error->code))
                      + "）：" + outcome.error->detail;
            return false;
        }
        if (!outcome.draft.has_value()) {
            summary = "导入未产出草稿——详见映射报告（错误项 "
                      + std::to_string(outcome.report.errors.size()) + " 条）";
            return false;
        }

        // 报告确认（四清单计数摘要——确认后草稿落盘）。
        if (!host.confirmImport(importSummaryText(outcome.report))) {
            return false;
        }
        session.draft = ModelingWorkingSet{};
        session.draft.design = *outcome.draft;  // 导入草稿为新根（无 project 身份——应用时分配）
        ModelingChangeRecord record;
        record.subject = "design";
        record.summary = isXacro ? "导入 Xacro 草稿（源已展开映射）" : "导入 URDF 草稿";
        session.draft.changes.push_back(std::move(record));
        if (deps.recomputeReadiness) { deps.recomputeReadiness(); }
        summary = (isXacro ? "Xacro 导入完成：" : "URDF 导入完成：")
                  + std::to_string(outcome.report.mapped.size()) + " 项映射、"
                  + std::to_string(outcome.report.errors.size()) + " 项错误——草稿已更新，请经『应用草稿』提交";
        return true;
    }

    // ---- modeling.switch-authority（权威切换——L-9；独立 TU 承载）----
    if (commandId == "modeling.switch-authority") {
        return executeSwitchAuthorityFlow(session, host, summary);
    }

    // ---- modeling.diff-baseline（与基线比较——只读；L-6 差异定位数据面）----
    if (commandId == "modeling.diff-baseline") {
        if (!session.baselineSnapshot.has_value()) {
            summary = "尚无已应用基线可比（草稿未经『应用草稿』提交过）";
            return false;
        }
        const ModelDiffService service;
        const ModelDiffReport report =
            service.diff(*session.baselineSnapshot, session.draft);
        QString text = QStringLiteral("与基线修订的差异（Model Diff）\n\n"
                                      "结构组：%1 条\n参数组：%2 条\n物性组：%3 条")
                           .arg(report.structure.size())
                           .arg(report.parameters.size())
                           .arg(report.properties.size());
        if (report.structure.empty() && report.parameters.empty()
            && report.properties.empty()) {
            text += QStringLiteral("\n\n两工作集逐字段全等——无差异。");
        }
        host.showInfo(QStringLiteral("与基线比较"), text);
        summary = "与基线比较完成：结构 " + std::to_string(report.structure.size())
                  + "／参数 " + std::to_string(report.parameters.size())
                  + "／物性 " + std::to_string(report.properties.size()) + " 条差异";
        return true;
    }

    // ---- modeling.export-package（规范包导出——只读会话亦可用）----------
    if (commandId == "modeling.export-package") {
        return runExportPackage(session, host, summary);
    }

    // ---- modeling.import-package（规范包导入——仅识别本软件工件）--------
    if (commandId == "modeling.import-package") {
        const auto pickedPath = host.openFilePath(
            QStringLiteral("导入规范模型包"),
            QStringLiteral("规范模型包 (*.irdbundle *.zip);;所有文件 (*)"));
        if (!pickedPath.has_value()) { return false; }
        const QString& path = *pickedPath;
        // 包端口消费 entrySnapshot.finalPath 定位包文件（内容唯一经 ZipChannel
        // 读取——bytes/dependencyTree 由调用链路携带但本端口不复读）。
        std::vector<std::uint8_t> placeholderBytes{0x50, 0x4B};  // "PK" 容器魔数占位（端口不复读——见 Package.hpp 契约）
        ValidatedSource source;
        source.entrySnapshot = makeEntrySnapshot(path, placeholderBytes);
        ModelPackagePort port;
        std::vector<core::DiagnosticRecord> diags;
        const PackageImportOutcome outcome = port.importPackage(source, diags);
        if (!outcome.ok) {
            summary = "规范包导入失败：" + outcome.error.detail;
            return false;
        }
        // 导入回读＝根对象值直取（报告值已解码——PackageImportReport.design）
        // 入草稿（与 URDF 导入同落点——应用时分配新根）。
        const RobotDesign* design = &outcome.report.design;
        if (!host.confirmImport(QStringLiteral("规范包导入报告\n\n"
                                                "部件对象：%1\n固化资源：%2\n\n确认后将替换当前草稿。")
                                    .arg(outcome.report.parts.size())
                                    .arg(outcome.report.solidifiedResources.size()))) {
            return false;
        }
        session.draft = ModelingWorkingSet{};
        session.draft.design = *design;
        ModelingChangeRecord record;
        record.subject = "design";
        record.summary = "导入规范模型包（MDL-20 round-trip 入草稿）";
        session.draft.changes.push_back(std::move(record));
        if (deps.recomputeReadiness) { deps.recomputeReadiness(); }
        summary = "规范包导入完成——草稿已更新，请经『应用草稿』提交";
        return true;
    }

    // ---- modeling.reset-home-zero（会话命令——零修订；MDL-17/§4.6）-------
    if (commandId == "modeling.reset-home-zero") {
        // 复位目标参考＝位姿集保留键（homeConfiguration/zeroConfiguration——
        // §4.6 行 386）；三维会话姿态承载面随 View3D 阶段 B（UI-T33）落位，
        // 本命令当前呈现参考数据（V-27：位姿集参考数据读取不产生写）。
        if (!session.draft.poseSetObject.has_value()) {
            summary = "无位姿集参考数据（草稿未含命名位姿集）——无可复位的会话姿态";
            return false;
        }
        QString text = QStringLiteral("复位 Home/Zero（会话姿态参考）\n");
        for (const auto& entry : session.draft.poseSetObject->entries) {
            if (entry.key == "zero" || entry.key == "home") {
                QString q;
                for (double v : entry.jointConfiguration) {
                    q += QStringLiteral("%1 ").arg(v, 0, 'g', 6);
                }
                text += QStringLiteral("\n%1：[%2]").arg(
                    QString::fromStdString(entry.key), q);
            }
        }
        host.showInfo(QStringLiteral("复位 Home/Zero"), text);
        summary = "Home/Zero 参考数据已呈现（会话命令——零修订；姿态承载面随三维视图落位）";
        return true;
    }

    // 未知命令 id＝调用方违约（PA-1 路由面——fail-fast，不静默吞）。
    throw std::invalid_argument("modeling 命令流：未知域命令 id（" + commandId + "）");
}

}  // namespace sdurws::ird::modeling
