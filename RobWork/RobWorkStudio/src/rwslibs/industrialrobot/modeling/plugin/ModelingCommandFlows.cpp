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
#include <cctype>
#include <filesystem>
#include <set>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "ModelingFlowsInternal.hpp"          // 域内拆分接口（权威切换流——DhConvert 独立 TU）

#include <sdurws/ird/core/Digest.hpp>            // core::ContentDigester（快照摘要——io 快照同源口径）
#include <sdurws/ird/io/ResourceIo.hpp>          // io 资源读取服务（F-473——导入依赖树装配半区）
#include <sdurws/ird/modeling/Codec.hpp>         // RobotDesignCodec/ObjectVariant（闭包字节编码）
#include <sdurws/ird/modeling/Import.hpp>        // ModelImportMapper（URDF/Xacro 映射）
#include <sdurws/ird/modeling/ModelDiff.hpp>     // ModelDiffService（与基线比较）
#include <sdurws/ird/modeling/ObjectTypes.hpp>   // 五对象 token（闭包视图路由键）
#include <sdurws/ird/modeling/PropertyEstimation.hpp>  // PropertyEstimator/SegmentSpec（批次C 估算流）
#include <sdurws/ird/modeling/Template.hpp>      // makeLinkPlaceholderCylinder/kPlaceholderCylinderRadius（批次C）
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
        // UI-T41 批次D（D1）：破坏性操作确认默认否——用户未明示即不执行
        // （旧版 confirmOutputOverwrite 同款默认钮纪律）。
        return QMessageBox::question(nullptr, title, text,
                                     QMessageBox::Yes | QMessageBox::No,
                                     QMessageBox::No)
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

// =====================================================================
// 装配路由权威判定（UI-T42——F-466 消账：宿主 UiPlugin 只转发本判定；
// 十条词表与 §9.7.3 卡表同源演化——新增命令漏登此处＝宿主禁用（fail-
// closed，不产生"按钮可用但执行失败"的不合格中间态——requirements 7/2
// 撤牌教训的同型预防）。
// =====================================================================

bool isAssembledModelingCommand(const std::string& commandId)
{
    static const std::set<std::string> kAssembled{
        "modeling.new-from-template", "modeling.import-urdf",
        "modeling.import-xacro",      "modeling.switch-authority",
        "modeling.estimate-properties", "modeling.generate-placeholder-geometry",
        "modeling.diff-baseline",     "modeling.export-package",
        "modeling.import-package",    "modeling.reset-home-zero",
    };
    return kAssembled.count(commandId) != 0;
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

/// relPath 折叠（io §6.5 约定：正斜杠＋折叠小写——Import.cpp 树查找同款）。
std::string foldResourceKey(const std::string& ref)
{
    std::string key;
    key.reserve(ref.size());
    for (const char ch : ref) {
        key.push_back(ch == '\\' ? '/'
                                 : static_cast<char>(std::tolower(
                                       static_cast<unsigned char>(ch))));
    }
    return key;
}

/// 提取 XML 文本中的 mesh 引用（仅 <mesh> 元素的 filename/file 属性——
/// io §6.5 mesh 边同源的最小装配输入；<xacro:*> 宏面在展开产物中已代入，
/// 无残余标签）。本提取仅为树装配输入，权威识别在映射器：提取遗漏→
/// 映射器 SourceInconsistent（fail-closed——不产生伪成功）。
std::vector<std::string> extractMeshRefs(const std::string& xmlText)
{
    static const std::regex kMeshAttr(
        "<mesh\\b[^>]*?\\b(?:filename|file)\\s*=\\s*(\"([^\"]*)\"|'([^']*)')");
    std::vector<std::string> refs;
    for (std::sregex_iterator it(xmlText.begin(), xmlText.end(), kMeshAttr), end;
         it != end; ++it) {
        const std::string quoted = (*it)[1].str();
        if (quoted.size() >= 2) {
            refs.push_back(quoted.substr(1, quoted.size() - 2));
        }
    }
    return refs;
}

/**
 * @brief 缺失容忍的导入依赖树装配（F-473——URDF/Xacro 导入命令流的
 *        ValidatedSource 补树半区）。
 *
 * 背景：映射器契约要求"URDF 含 mesh 引用时 bytes 与 dependencyTree 必须
 * 同源"（否则 SourceInconsistent 拒绝、无草稿）；io 整树扫描
 * （IResourceReader::dependencyTree）对缺失引用整树拒绝（§6.5"无部分
 * 产物"），而映射器容忍缺失叶（V-08"应用可过（Warning）"）——两条契约
 * 的夹缝使含 mesh 的 URDF 经生产流恒被拒绝。本辅助按黄金测试
 * （GoldenImportTest V-06/V-08 装配纪律）补齐装配半区：
 *   - 入口文档＝exists=true＋调用方 entrySnapshot（bytes 真实摘要）；
 *   - 逐 mesh 引用调 io snapshot：成功＝io 真实快照（digest 身份）；
 *     失败（缺失/预算/网格护栏/路径违约）＝exists=false 缺失叶——映射器
 *     以 V-08 事实（Recorded 缺失）承载，不阻断导入（不虚构存在性）；
 *   - 远距方案（package:// 等含 "://"）不入树（io 不可达——映射器以
 *     ros-uri 不支持项承载，黄金件同款约定）；
 *   - 同键去重＋relPath 字典序（io 产物稳定序——映射器不依赖序）。
 *
 * @param xmlText       [in] 入口（或 xacro 展开后）的 XML 文本（UTF-8）
 * @param entrySnapshot [in] 入口文档快照（makeEntrySnapshot/桥产物）
 * @param baseDir       [in] 引用解析基目录（入口文档所在目录——管辖根）
 * @param rootRel       [in] 入口相对键（折叠形——报告 sourceLabel 同源）
 * @return 依赖树（无环；允许缺失叶——映射器消费形态）
 */
io::ResourceDependencyTree assembleImportDependencyTree(
    const std::string& xmlText,
    const io::ResourceSnapshot& entrySnapshot,
    const std::filesystem::path& baseDir,
    const std::string& rootRel)
{
    io::ResourceDependencyTree tree;
    tree.rootRel = rootRel;

    io::ResourceNode entry;
    entry.relPath = rootRel;
    entry.exists = true;                 // 入口文档已读取成功（bytes 非空）
    entry.snapshot = entrySnapshot;
    tree.nodes.push_back(std::move(entry));

    const io::IResourceReaderPtr reader = io::makeResourceReader();
    io::ResourceOpenSpec spec;
    spec.role = io::PathRole::UserSource;  // 用户自选导入文件的角色同源

    for (const std::string& rawRef : extractMeshRefs(xmlText)) {
        const std::string relKey = foldResourceKey(rawRef);
        // 远距方案不入树（黄金件约定）；空引用不入树（不虚构资源）。
        if (relKey.empty() || relKey.find("://") != std::string::npos) {
            continue;
        }
        // 同键去重（同一 mesh 被视觉/碰撞多次引用＝一节点；映射器清单侧
        // 亦同键去重——两半区口径一致）。
        bool seen = false;
        for (const io::ResourceNode& n : tree.nodes) {
            if (n.relPath == relKey) { seen = true; break; }
        }
        if (seen) { continue; }

        io::ResourceNode node;
        node.relPath = relKey;
        // io snapshot 拒绝（含存在但被预算/网格护栏拦截）一律按缺失叶入树
        // ——exists=true 必须携带真实快照（ResourceNode 契约），拦截面
        // 误标为缺失属诚实降级（V-08 警告路径可见，不阻断、不伪存在）。
        const auto snapshot = reader->snapshot(
            baseDir / std::filesystem::u8path(rawRef), spec, nullptr, nullptr);
        if (snapshot) {
            node.exists = true;
            node.snapshot = snapshot.value;
        } else {
            node.exists = false;
        }
        tree.nodes.push_back(std::move(node));

        io::ResourceEdge edge;
        edge.fromRel = rootRel;
        edge.toRel = relKey;
        edge.kind = io::ResourceEdgeKind::Mesh;
        tree.edges.push_back(std::move(edge));
    }

    std::sort(tree.nodes.begin(), tree.nodes.end(),
              [](const io::ResourceNode& a, const io::ResourceNode& b) {
                  return a.relPath < b.relPath;
              });
    return tree;
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

/// 导出默认文件名派生（UI-T41 批次C C6——旧版"输出文件名跟随模型名"亮点的
/// 最小承接）：根对象 displayName 清洗为合法文件名基（Windows 保留字符→
/// '_'、控制字符剔除、首尾空白裁剪）；空/全非法回退 "model"。
std::string sanitizeFileBaseName(const std::string& name)
{
    std::string cleaned;
    cleaned.reserve(name.size());
    for (const char c : name) {
        const bool illegal = (c == '\\' || c == '/' || c == ':' || c == '*'
                              || c == '?' || c == '"' || c == '<' || c == '>'
                              || c == '|');
        if (illegal) {
            cleaned.push_back('_');
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            cleaned.push_back(c);
        }  // 控制字符（<0x20）直接剔除
    }
    const std::size_t first = cleaned.find_first_not_of(" \t");
    const std::size_t last = cleaned.find_last_not_of(" \t");
    if (first == std::string::npos) { return "model"; }
    return cleaned.substr(first, last - first + 1);
}

/// 规范包导出流程（export-package——目标路径用户选定，原子写归 io 端口）。
bool runExportPackage(ModuleSessionState& session, ModelingDialogHost& host,
                      std::string& summary)
{
    const auto pickedPath = host.saveFilePath(
        // C6 默认名派生：模型显示名清洗（改名跟随——对话框预填即默认产物
        // 名，用户可改）。
        QStringLiteral("导出规范模型包"),
        QString::fromStdString(sanitizeFileBaseName(session.draft.design.displayName)
                               + ".irdbundle"),
        QStringLiteral("规范模型包 (*.irdbundle);;所有文件 (*)"));
    if (!pickedPath.has_value()) { return false; }  // 用户取消
    const QString& path = *pickedPath;
    if (session.draft.design.joints.empty()) {
        summary = "草稿为空——无规范内容可导出";
        return false;
    }

    // UI-T41 批次D（D1）：写入前清单化确认（借鉴旧版 confirmOutputOverwrite
    // ——列出将写入/替换的目标，默认否）。内容清单＝根对象＋部件闭包计数；
    // 存在性检查→"替换现有文件"明示；原子替换语义（失败保留原文件）随行。
    const std::filesystem::path targetPath = path.toStdWString();
    const bool fileExists = std::filesystem::exists(targetPath);
    const ModelingWorkingSet& draft = session.draft;
    QString confirmText =
        QStringLiteral("将写入规范模型包：\n  · %1%2\n\n包含对象：\n"
                       "  · 根对象 robot-design（关节 %3／连杆 %4）\n"
                       "  · 工具 %5／场景 %6／位姿集 %7／传动 %8\n\n"
                       "写出采用 io 原子替换——失败保留先前文件。是否继续？")
            .arg(path, fileExists ? QStringLiteral("（将替换现有文件）")
                                  : QStringLiteral())
            .arg(draft.design.joints.size())
            .arg(draft.design.links.size())
            .arg(draft.toolObjects.size())
            .arg(draft.sceneObjects.size())
            .arg(draft.poseSetObject.has_value() ? 1 : 0)
            .arg(draft.drivetrainObject.has_value() ? 1 : 0);
    if (!draft.changes.empty()) {
        confirmText += QStringLiteral("\n注意：当前草稿有 %1 条未应用编辑——导出内容为草稿现状。")
                           .arg(draft.changes.size());
    }
    if (!host.confirmProceed(QStringLiteral("导出确认"), confirmText)) {
        return false;  // 默认否——用户未明示即不写盘
    }

    WorkingSetClosureView closure(session.draft);
    PackageExportTarget target;
    target.targetFile = targetPath;
    // 替换策略缺省 OverwriteAtomic（io 原子替换——失败保留先前输出）；
    // createdAtUtc 留空（服务不取时钟——确定性口径；记录字段可省略）。
    ModelPackagePort port;
    std::vector<core::DiagnosticRecord> diags;
    const PackageExportOutcome outcome = port.exportPackage(closure, target, diags);
    if (!outcome.ok) {
        summary = "导出失败：" + outcome.error.detail;
        return false;
    }

    // UI-T41 批次D（D2 发布即所见——旧版"事务化发布"承诺的最小承接）：
    // 导出后立即经包端口回读校验（manifest 校验＋逐条目 SHA-256 复算＋根
    // 对象解码——端口 importPackage 全链），失败如实回报（导出成功≠可用）。
    // 运行时真实加载校验（编译链）依赖 runtime 绑定面，归后续任务链——
    // 此处诚实注明，不虚构"加载成功"。
    ValidatedSource verifySource;
    verifySource.entrySnapshot.finalPath = targetPath;
    std::vector<std::uint8_t> containerProbe{0x50, 0x4B};  // 端口不复读字节——ZipChannel 唯一读取口
    verifySource.bytes = containerProbe;
    std::vector<core::DiagnosticRecord> verifyDiags;
    const PackageImportOutcome verify = port.importPackage(verifySource, verifyDiags);
    if (!verify.ok) {
        summary = "规范包已写出，但回读校验失败：" + verify.error.detail
                  + "——请检查目标文件后再分发";
        return false;  // 校验失败＝不可信产物（诚实拒绝，不粉饰为成功）
    }
    summary = "规范包已导出并通过回读校验（对象条目 "
              + std::to_string(verify.report.parts.size()) + "，条目数 "
              + std::to_string(outcome.entryCount) + "）：" + path.toStdString()
              + "——运行时加载校验归后续任务链";
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

        // ValidatedSource 装配（bytes＋入口快照＋缺失容忍依赖树——F-473）：
        // 映射器要求"含 mesh 引用的文档必须携带同源依赖树"（否则
        // SourceInconsistent 拒绝、无草稿），io 整树扫描对缺失引用整树拒绝
        // （无部分产物）——两契约夹缝使含 mesh 的模型此前经本流恒被拒。
        // 按黄金测试（GoldenImportTest V-06/V-08）装配纪律在流半区补树，
        // 装配语义见 assembleImportDependencyTree 头注。Xacro 树自展开产物
        // 提取、基目录＝入口文档目录：单文件 xacro 精确；include 子目录内
        // 的 mesh 引用可能标缺失（V-08 警告可见）——多文件精确解析随导入
        // 向导任务（与选链重映射同批）。
        const std::filesystem::path fsPath(path.toStdWString());
        const std::string rootRel =
            foldResourceKey(fsPath.filename().u8string());
        const auto assembleSource =
            [&](const std::vector<std::uint8_t>& documentBytes,
                const std::string& xmlText,
                const io::ResourceSnapshot& entrySnapshot) {
                ValidatedSource source;
                source.bytes = documentBytes;
                source.entrySnapshot = entrySnapshot;
                source.dependencyTree = assembleImportDependencyTree(
                    xmlText, entrySnapshot, fsPath.parent_path(), rootRel);
                return source;
            };

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
            ValidatedSource expandedSource = assembleSource(
                expanded.expandedBytes,
                std::string(expanded.expandedBytes.begin(),
                            expanded.expandedBytes.end()),
                expanded.entrySnapshot);  // 来源身份保持
            XacroProvenance provenance;
            provenance.sourceDigest = expanded.sourceDigest;
            provenance.sourceAbsPath = path.toStdString();
            outcome = mapper.mapXacroExpanded(expandedSource, provenance, options, diags);
        } else {
            const auto source = assembleSource(
                *bytes, std::string(bytes->begin(), bytes->end()),
                makeEntrySnapshot(path, *bytes));
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
            const auto source = assembleSource(
                *bytes, std::string(bytes->begin(), bytes->end()),
                makeEntrySnapshot(path, *bytes));
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
        // 审核修正（UI-T41 批次D R1——恰一根不变量）：导入＝**替换既有模型
        // 内容**——已回填的项目根身份必须保留，否则下次 draft.apply 按
        // allocateNew 重复建根（requirements 域 UI-T35 P1-2 同型缺陷 F-461
        // 的建模侧翻版）；无根（纯草稿会话）保持 nullopt＝应用时分配。
        const std::optional<core::ObjectId> previousRoot = session.draft.rootObjectId;
        session.draft = ModelingWorkingSet{};
        session.draft.rootObjectId = previousRoot;
        session.draft.design = *outcome.draft;
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

    // ---- modeling.estimate-properties（选中连杆批量物性估算——§5.3）----
    if (commandId == "modeling.estimate-properties") {
        // 目标解析：面板会话选中锚→连杆（deps.selectedAnchor——未选中/非
        // 连杆＝调用侧错误面，诚实拒绝不抛）。
        const auto anchor = deps.selectedAnchor ? deps.selectedAnchor()
                                                : std::optional<core::ObjectId>{};
        if (!anchor.has_value()) {
            summary = "请先在结构树选中一个连杆再执行物性估算";
            return false;
        }
        const ModelingWorkingSet& draft = session.draft;
        std::size_t linkIndex = draft.design.links.size();
        for (std::size_t i = 0; i < draft.design.links.size(); ++i) {
            if (draft.design.links[i].objectId == *anchor) { linkIndex = i; break; }
        }
        if (linkIndex >= draft.design.links.size()) {
            summary = "选中对象不是本模型连杆——估算目标无效";
            return false;
        }
        // 段元几何（旧版 autoLink 同款近似——§5.2）：连杆 i 的占位段＝该
        // 连杆系原点→其驱动关节 j_i 安装原点（joints[i].origin，连杆系下）；
        // 末端法兰连杆（links.size()==joints.size()+1 的尾元素）无驱动关节
        // ——诚实拒绝（法兰几何归后续任务）。位姿数学复用内核
        // makeLinkPlaceholderCylinder（z 对齐＋中点定位——段系在连杆系下）。
        if (linkIndex >= draft.design.joints.size()) {
            summary = "末端法兰连杆无驱动关节原点可构段元——请经导入或后续法兰几何任务";
            return false;
        }
        const auto& origin = draft.design.joints[linkIndex].origin;
        if (!origin.tryValue().has_value()) {
            summary = "驱动关节原点未提供——无法推导连杆段元（先补关节原点）";
            return false;
        }
        const rw::math::Vector3D<double> segmentEnd(origin.tryValue()->d());
        const double length = segmentEnd.norm2();
        if (!(length > 0.0)) {
            summary = "段元零长度（关节原点与连杆系原点重合）——无估算几何意义";
            return false;
        }
        // 材料选择（§5.3 默认密度表五键——chooseItem 应答面）。
        const std::pair<const char*, const char*> materials[] = {
            {"steel", "钢（7850 kg/m³）"}, {"aluminum", "铝（2700 kg/m³）"},
            {"cast-iron", "铸铁（7200 kg/m³）"}, {"titanium-alloy", "钛合金（4430 kg/m³）"},
            {"engineering-plastic", "工程塑料（1200 kg/m³）"},
        };
        QStringList items;
        for (const auto& [id, label] : materials) { items << QString::fromUtf8(label); }
        const auto pickedMaterial = host.chooseItem(QStringLiteral("选择连杆材料"),
                                                    QStringLiteral("估算采用 §5.3 默认密度表："),
                                                    items);
        if (!pickedMaterial.has_value()) { return false; }  // 用户取消

        // 段元组装（单段实心圆柱——半径＝占位圆柱设计默认值；密度解析在
        // estimateLink 内〔先 density 已提供值再查默认表〕，此处只给键）。
        SegmentSpec segment;
        segment.primitive = SolidCylinderSpec{kPlaceholderCylinderRadius, length};
        segment.linkFromSegment = makeLinkPlaceholderCylinder(
            rw::math::Vector3D<double>(0.0, 0.0, 0.0), segmentEnd,
            "estimate-segment", kPlaceholderCylinderRadius).geometry.localTransform;
        segment.material.materialId = materials[*pickedMaterial].first;

        const PropertyEstimator estimator;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = estimator.estimateLink({segment}, diags);
        if (!outcome.ok()) {
            summary = "物性估算失败：" + outcome.error().detail;
            return false;
        }
        const EstimatedLinkProperties& props = outcome.get();
        // 写回（§5.3 规则 1——估算结果一律 GeometricEstimate＋公式表标记）：
        // 覆盖既有用户值前确认（旧值来源非估算＝用户手填，破坏性覆盖须确认）。
        LinkEntry& link = session.draft.design.links[linkIndex];
        const bool overwritingUser =
            link.body.mass.state() == core::FieldState::Provided
            && link.body.mass.provenance().kind == core::ProvenanceKind::UserProvided;
        if (overwritingUser
            && !host.confirmProceed(QStringLiteral("覆盖确认"),
                                    QStringLiteral("该连杆已有用户手填物性——估算结果将覆盖。是否继续？"))) {
            return false;
        }
        link.body.mass = core::SourcedValue<double>::provided(props.massKg, props.provenance);
        link.body.centerOfMass =
            core::SourcedValue<rw::math::Vector3D<double>>::provided(
                props.centerOfMass, props.provenance);
        link.body.inertia =
            core::SourcedValue<InertiaTensor>::provided(props.inertia, props.provenance);
        ModelingChangeRecord record;
        record.subject = "links[" + std::to_string(linkIndex) + "].body";
        record.summary = std::string("物性估算（材料 ") + materials[*pickedMaterial].first
                         + "，单段圆柱近似）";
        session.draft.changes.push_back(std::move(record));
        if (deps.recomputeReadiness) { deps.recomputeReadiness(); }
        summary = "物性估算完成：质量 " + std::to_string(props.massKg) + " kg（来源＝估算）——请经『应用草稿』提交";
        return true;
    }

    // ---- modeling.generate-placeholder-geometry（占位圆柱生成——§5.2）----
    if (commandId == "modeling.generate-placeholder-geometry") {
        const auto anchor = deps.selectedAnchor ? deps.selectedAnchor()
                                                : std::optional<core::ObjectId>{};
        if (!anchor.has_value()) {
            summary = "请先在结构树选中一个连杆再生成占位几何";
            return false;
        }
        ModelingWorkingSet& draft = session.draft;
        std::size_t linkIndex = draft.design.links.size();
        for (std::size_t i = 0; i < draft.design.links.size(); ++i) {
            if (draft.design.links[i].objectId == *anchor) { linkIndex = i; break; }
        }
        if (linkIndex >= draft.design.links.size()) {
            summary = "选中对象不是本模型连杆——生成目标无效";
            return false;
        }
        if (linkIndex >= draft.design.joints.size()) {
            summary = "末端法兰连杆无驱动关节原点参考——占位几何归后续法兰任务";
            return false;
        }
        const auto& origin = draft.design.joints[linkIndex].origin;
        if (!origin.tryValue().has_value()) {
            summary = "驱动关节原点未提供——无法定位占位圆柱（先补关节原点）";
            return false;
        }
        const auto& link = draft.design.links[linkIndex];
        const std::string refId = "placeholder-" + link.localName;
        // 内核纯函数生成（确定性——同输入同位姿字节；resourceRefId＝会话内
        // 作用域键）。★ schema 边界（内核注释原文）：占位原语的 resourceManifest
        // 登记形态"随 schema 澄清落位"（图元参数无 schema 落点）——本流程不
        // 伪造清单条目，悬空状态经面板几何行"未入资源清单"警示如实呈现。
        GeneratedGeometry generated = makeLinkPlaceholderCylinder(
            rw::math::Vector3D<double>(0.0, 0.0, 0.0),
            origin.tryValue()->d(), refId, kPlaceholderCylinderRadius);
        draft.design.links[linkIndex].visual = std::move(generated.geometry);
        ModelingChangeRecord record;
        record.subject = "links[" + std::to_string(linkIndex) + "].visual";
        record.summary = "生成占位圆柱（半径 " + std::to_string(kPlaceholderCylinderRadius)
                         + " m，几何引用未入资源清单——schema 边界）";
        session.draft.changes.push_back(std::move(record));
        if (deps.recomputeReadiness) { deps.recomputeReadiness(); }
        summary = "占位圆柱已生成并写入视觉几何（资源清单登记随 schema 澄清落位）——请经『应用草稿』提交";
        return true;
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
        // 入草稿（与 URDF 导入同落点——已回填根则同根替换，恰一根不变量 R1）。
        const RobotDesign* design = &outcome.report.design;
        // UI-T41 批次D（D1）：确认文本清单化——对象计数＋草稿覆盖后果（未
        // 应用编辑将丢弃）＋应用语义（同根替换/新根分配如实区分）。
        QString importConfirm =
            QStringLiteral("规范包导入报告\n\n部件对象：%1\n固化资源：%2\n\n"
                           "确认后将替换当前草稿。")
                .arg(outcome.report.parts.size())
                .arg(outcome.report.solidifiedResources.size());
        if (!session.draft.changes.empty()) {
            importConfirm += QStringLiteral("\n注意：当前草稿 %1 条未应用编辑将丢弃。")
                                 .arg(session.draft.changes.size());
        }
        importConfirm += session.draft.rootObjectId.has_value()
                             ? QStringLiteral("\n应用语义：替换既有模型根（不新建）。")
                             : QStringLiteral("\n应用语义：经『应用草稿』分配新模型根。");
        if (!host.confirmImport(importConfirm)) {
            return false;
        }
        // 审核修正（R1——恰一根不变量，语义同 URDF/Xacro 导入侧）：保留
        // 已回填的项目根身份——导入＝同根内容替换，不重复建根。
        const std::optional<core::ObjectId> previousRoot = session.draft.rootObjectId;
        session.draft = ModelingWorkingSet{};
        session.draft.rootObjectId = previousRoot;
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
