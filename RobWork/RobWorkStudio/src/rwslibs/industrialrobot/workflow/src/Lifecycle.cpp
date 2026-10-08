/**
 * @file   Lifecycle.cpp
 * @brief  生命周期入口流程编排的实现——新建项目三步向导（PM-01）＋打开
 *         协议（PM-02 五步的 workflow 面）＋关闭/切换/退出统一确认编排
 *         （PM-03——§7.3）＋方案分支切换（PM-12 零写入会话选择）＋另存
 *         为/包导出/包导入编排（PM-05——§7.4）＋旧格式升级指引数据面/
 *         只读会话数据面/外部源重关联编排（PM-06/07/09——§7.5）＋最近
 *         项目管理（PM-10）＋无项目首页数据面（PM-10）。
 *
 * 设计依据见公共头 Lifecycle.hpp 文件头（本实现文件只补充逐段实现口径；
 * 段落注释对应头注释的编排序——两处同步维护，改逻辑必改两处）。
 *
 * 实现纪律（与本单元既有实现同源）：
 *   - 零 Qt（R-3/NFR-MNT-01——本翻译单元在计算库源码集内）；
 *   - 零 modeling/io/业务域头（R-1——模板/导入只经 IDomainInitSubmitter
 *     端口触达、打开协议只经 project 公共头触达；文件系统操作用标准库）；
 *   - 词表与文案键构造集中（NFR-MNT-03——本文件是生命周期编排词表的
 *     唯一映射点）；
 *   - 错误二分（§10.3）：调用方错误 WorkflowError fail-fast；环境/对端
 *     错误值轨道呈现（failure 三字段——UX-03），零新增稳定码（D-WF-7）。
 */

#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <sdurws/ird/project/QueryPort.hpp>  // project::IProjectQueryPort（包导出源元数据组装——HEAD 修订读取；白名单边公共头）

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <system_error>
#include <thread>
#include <utility>

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 内部工具：文案键构造（本文件唯一拼键点——调用方只消费函数，不手拼）
// =====================================================================

namespace {

/// 步骤校验错误文案键：键形 "wizard.new-project.error.<token>"
/// （ui.md §3.5 文案键体系的向导族前缀；值归 ui 文案资源——本单元只产键）。
ui::TextKey newProjectErrorKey(const char* token)
{
    // 前缀＋token 一次拼接；token 全部来自下方 switch 的字面常量（封闭集）。
    return ui::TextKey(std::string("wizard.new-project.error.") + token);
}

/// 摘要行标签文案键：键形 "wizard.new-project.summary.<token>"。
ui::TextKey newProjectSummaryKey(const char* token)
{
    return ui::TextKey(std::string("wizard.new-project.summary.") + token);
}

/// 路径 → 摘要值文本（UTF-8 无损转换——u8string；Windows 宽字节路径转
/// 窄本地编码（path::string()）遇不可表示字符会抛，UTF-8 无损不抛）。
/// 不做规范化/加引号——呈现修饰归 ui。空路径得空串。
std::string pathSummaryText(const std::filesystem::path& p)
{
    return p.empty() ? std::string() : p.u8string();
}

}  // namespace

// =====================================================================
// 步骤词表（头注释"会话态、冻结序"的兑现）
// =====================================================================

const std::vector<NewProjectStep>& newProjectStepSequence()
{
    // 函数内静态＝首次调用初始化（C++11 魔术静态——并发安全）；序即
    // §7.1 流程 S1→S2→S3，与枚举值序一致（确定性 NFR-COR-02）。
    static const std::vector<NewProjectStep> kSequence = {
        NewProjectStep::ProjectInfo,
        NewProjectStep::InitialSource,
        NewProjectStep::Confirm,
    };
    return kSequence;
}

// =====================================================================
// 词表规范化映射（封闭枚举 → token；防御性空串分支按头注释口径不抛）
// =====================================================================

std::string installPresetToken(InstallPreset preset)
{
    switch (preset) {
    case InstallPreset::Ground:   return kInstallGroundToken;    // "ground"——§7.1 地面
    case InstallPreset::Wall:     return kInstallWallToken;      // "wall"——墙面
    case InstallPreset::Inverted: return kInstallInvertedToken;  // "inverted"——倒挂
    }
    return {};  // 词表外值（三值枚举不可构造——防御性；空串暴露而非伪造 token）
}

std::string templateKindToken(TemplateKind kind)
{
    switch (kind) {
    case TemplateKind::SixAxis:   return kTemplateSixAxisToken;   // "generic-6r"——modeling 登记词形
    case TemplateKind::SevenAxis: return kTemplateSevenAxisToken; // "generic-7r"——P-03 未冻结，启用由对端裁决
    }
    return {};
}

std::string initialSourceToken(InitialSourceKind source)
{
    switch (source) {
    case InitialSourceKind::Template:  return kSourceTemplateToken;  // "template"
    case InitialSourceKind::UrdfXacro: return kSourceUrdfXacroToken; // "urdf-xacro"
    case InitialSourceKind::Blank:     return kSourceBlankToken;     // "blank"
    }
    return {};
}

std::string externalHandlingToken(ExternalResourceHandling handling)
{
    switch (handling) {
    case ExternalResourceHandling::CopyIntoResources:       return kHandlingCopyToken;   // "copy-into-resources"
    case ExternalResourceHandling::RecordExternalReference: return kHandlingRecordToken; // "record-external-reference"
    }
    return {};
}

// =====================================================================
// 输入校验（纯函数——键集与键序即头注释 validateStep 的校验集契约）
// =====================================================================

std::vector<ui::TextKey> validateStep(NewProjectStep step, const NewProjectInputs& inputs)
{
    // 步骤词表封闭性防御（三值枚举不可构造——越界即调用方缺陷，fail-fast）。
    const auto& seq = newProjectStepSequence();
    if (static_cast<std::size_t>(step) >= seq.size()) {
        throw WorkflowError("validateStep: 步骤越界（三步词表外）");
    }

    std::vector<ui::TextKey> errors;

    // 键序即输出序（头注释键序冻结——多条错误时呈现顺序稳定）。
    // 步骤①的"名称＋目录"两键在 InitialSource 步不复检（步骤放行是
    // 前向累积语义：UI 侧保证到达第②步时①已过）；Confirm 步并集复检。
    const bool checkInfo = step == NewProjectStep::ProjectInfo
                        || step == NewProjectStep::Confirm;
    const bool checkSource = step == NewProjectStep::InitialSource
                          || step == NewProjectStep::Confirm;

    if (checkInfo) {
        // 显示名：createNew @pre"displayName 为空"属调用方错误——向导侧
        // 前置呈现（空白即违规；不做 trim——显示名是用户词面，首尾空白
        // 是否折叠归呈现/对端，不复制规则）。
        if (inputs.displayName.empty()) {
            errors.push_back(newProjectErrorKey("display-name-empty"));
        }
        // 目录：空路径＝未选择；已存在且非空目录＝createNew @pre 违约的
        // 前置呈现（存在且为空目录放行——createNew 允许复用空目录）。
        // 存在性检查是环境事实（TOCTOU 窗口存在）——最终裁决在 createNew。
        if (inputs.directory.empty()) {
            errors.push_back(newProjectErrorKey("directory-empty"));
        } else {
            std::error_code ec;  // 双态版本：不抛——校验是呈现性前置，环境
                                 // 失败（如权限不足）交 createNew 侧归位
            if (std::filesystem::exists(inputs.directory, ec) && !ec) {
                if (std::filesystem::is_directory(inputs.directory, ec) && !ec
                    && !std::filesystem::is_empty(inputs.directory, ec) && !ec) {
                    errors.push_back(newProjectErrorKey("directory-exists-nonempty"));
                }
            }
        }
    }

    if (checkSource) {
        // 来源专属必填（对端 @pre 的前置呈现；字符集/格式细节归对端——
        // 不复制 modeling/io 的规则，只挡"空"这一向导可判事实）。
        if (inputs.source == InitialSourceKind::Template
            && inputs.templateLocalName.empty()) {
            errors.push_back(newProjectErrorKey("template-local-name-empty"));
        }
        if (inputs.source == InitialSourceKind::UrdfXacro
            && inputs.sourceFile.empty()) {
            errors.push_back(newProjectErrorKey("source-file-empty"));
        }
    }

    return errors;
}

std::vector<ui::TextKey> validateNewProjectInputs(const NewProjectInputs& inputs)
{
    // 全量＝确认步键集（①②并集——头注释"Confirm 步并集"的独立入口，
    // 供编排器前置校验与测试直调）。
    return validateStep(NewProjectStep::Confirm, inputs);
}

// =====================================================================
// 右侧实时步骤摘要（纯函数——行集裁剪规则即头注释 buildNewProjectSummary）
// =====================================================================

std::vector<WizardSummaryLine> buildNewProjectSummary(const NewProjectInputs& inputs)
{
    std::vector<WizardSummaryLine> lines;

    // 行 1/2：项目信息（两步共用基础行——任何来源都出现）。
    lines.push_back({newProjectSummaryKey("name"), inputs.displayName});
    lines.push_back({newProjectSummaryKey("location"), pathSummaryText(inputs.directory)});

    // 行 3：来源（token 词表——工程用语；解析成中文归 ui UiText）。
    lines.push_back({newProjectSummaryKey("source"), initialSourceToken(inputs.source)});

    // 行 4/5：模板专属（轴数类别＋安装预设——零数值，P-03）。
    if (inputs.source == InitialSourceKind::Template) {
        lines.push_back({newProjectSummaryKey("template"), templateKindToken(inputs.templateKind)});
        lines.push_back({newProjectSummaryKey("installation"), installPresetToken(inputs.installPreset)});
    }

    // 行 6/7：URDF/Xacro 专属（外部源词面＋二选一处置——PM-01/CON-03）。
    if (inputs.source == InitialSourceKind::UrdfXacro) {
        lines.push_back({newProjectSummaryKey("source-file"), pathSummaryText(inputs.sourceFile)});
        lines.push_back({newProjectSummaryKey("external-handling"),
                         externalHandlingToken(inputs.externalHandling)});
    }

    return lines;
}

// =====================================================================
// 创建编排器（头注释"编排序 1~5"的逐段实现——两处同步维护）
// =====================================================================

NewProjectOutcome NewProjectWizardFlow::commit(const NewProjectInputs& inputs,
                                               IDomainInitSubmitter* domainInitSubmitter,
                                               core::IDomainEventBus* eventBus,
                                               project::IDiagnosticsSink* diagnosticsSink)
{
    NewProjectOutcome outcome;

    // ---- 第 1 段：前置校验（调用方契约——fail-fast，不带病前进）----
    // 步骤放行判定已在 UI 侧挡住非法输入；到不了这里还非法属宿主装配
    // 缺陷（如跳过确认步直调 commit），按单元错误语义抛 WorkflowError。
    const std::vector<ui::TextKey> errors = validateNewProjectInputs(inputs);
    if (!errors.empty()) {
        throw WorkflowError("commit: 输入未过全量校验（首错 "
                            + (errors.front().empty() ? std::string("<空键>") : errors.front())
                            + "）——步骤放行判定失效");
    }
    const bool needsDomainInit = inputs.source != InitialSourceKind::Blank;
    if (needsDomainInit && domainInitSubmitter == nullptr) {
        throw WorkflowError("commit: 来源为模板/URDF 但未提供领域初始化提交端口");
    }

    // ---- 第 2 段：项目创建（PM-01 存储侧——project 七步事务）----
    // createNew 的三条承诺是本段的失败语义依托：失败零修订、失败清理
    // 目标目录、取消/失败不留半成品（project.md §5.1 表）。异常在此折叠
    // 为值轨道呈现（环境错误不穿透编排器——向导呈现并保留输入重试）。
    project::OpenStoreResult opened;
    try {
        opened = project::ProjectStoreFactory::createNew(
            inputs.directory, inputs.displayName, eventBus, diagnosticsSink);
    } catch (const std::exception& e) {
        // 环境/对端失败（StoreError/OS 异常等）：cause 透传 what()（含
        // 对端 detail 串——机器可读键值随行，呈现裁剪归 ui），目录残留
        // 由 project 侧"失败清理目标目录"兜底（本层不重复删除——删除
        // 他人创建中的目录属越权写）。
        NewProjectFailure failure;
        failure.context = pathSummaryText(inputs.directory);
        failure.cause = e.what();
        failure.recommendedAction =
            "新建向导：请更换项目位置或检查磁盘/权限后重试（输入已保留）";
        outcome.failure = std::move(failure);
        return outcome;  // created=false、store 空——零半成品
    }

    // ---- 第 3 段：来源分派（blank 即完成；模板/URDF 经①端口领域初始化）----
    if (!needsDomainInit) {
        // 空白来源：止于 createNew 的初始修订 r0（空项目骨架）——没有
        // 基线修订可登记（baselineRevision 缺省 nullopt），创建即完成。
        outcome.created = true;
        outcome.store = std::move(opened.store);
        if (outcome.store) {
            outcome.projectId = outcome.store->projectId();  // prj-（createNew 一次分配）
        }
        return outcome;
    }

    // 领域初始化请求组装（来源参数照抄输入；baselineReadOnly 是编排器
    // 填充的语义登记而非用户输入：URDF/Xacro 项目以不可修改基线修订保存
    // （PM-12/PM-01），模板创建的基线可编辑——普通基线）。
    DomainInitRequest request;
    request.source = inputs.source;
    request.templateKind = inputs.templateKind;
    request.installPreset = inputs.installPreset;
    request.localName = inputs.templateLocalName;
    request.sourceFile = inputs.sourceFile;
    request.externalHandling = inputs.externalHandling;
    request.baselineReadOnly = inputs.source == InitialSourceKind::UrdfXacro;

    const DomainInitResult result =
        domainInitSubmitter->submitInitialization(*opened.store, request);

    if (result.committed) {
        // ---- 第 5 段：成功（领域基线修订已落——created 随诊断透传）----
        outcome.created = true;
        outcome.store = std::move(opened.store);
        if (outcome.store) {
            outcome.projectId = outcome.store->projectId();  // prj-（createNew 一次分配）
        }
        outcome.baselineRevision = result.baselineRevision;
        // 对端告警级诊断原样透传（零加工——提交端口的 diagnostics 约定）。
        outcome.failure.reset();
        return outcome;
    }

    // ---- 第 4 段：领域初始化失败收尾（不留半成品的编排责任段）----
    // 项目骨架已就位但基线修订未落成——此时留下的只有"空骨架"半成品，
    // 必须收尾：关闭上下文（释放写锁）→删除本次创建的目录→失败呈现。
    // 注意顺序：锁未释放前 remove_all 在 Windows 上会因句柄占用失败。
    if (opened.store) {
        // 刚创建的上下文零在途操作——requestClose 返回的在途引用数恒 0
        // （pending==0 时同步走完关闭并释放写锁）；显式弃值（收尾语义已
        // 定，返回值仅观测面——MSVC C4834 消音，不属吞错）。
        (void)opened.store->requestClose();
        opened.store.reset();          // 上下文实例释放（Closed 态残余资源清空）
    }
    std::error_code cleanupEc;  // 双态删除：清理失败不抛——失败事实转为
                                // 呈现材料（UX-03 建议半区），环境错误
                                // 不穿透编排器（同第 2 段口径）
    std::filesystem::remove_all(inputs.directory, cleanupEc);

    NewProjectFailure failure;
    failure.context = pathSummaryText(inputs.directory);
    // 原因/建议：提交端口的 UX-03 半区优先；空则自透传诊断首条兜底；
    // 仍空给通用重试指引（三字段永不缺位——UX-03 呈现承诺）。
    failure.cause = result.causeText;
    if (failure.cause.empty() && !result.diagnostics.empty()) {
        failure.cause = result.diagnostics.front().cause;
    }
    failure.recommendedAction = result.actionText;
    if (failure.recommendedAction.empty()) {
        failure.recommendedAction =
            "新建向导：请检查来源文件与模板可用性后重试（输入已保留）";
    }
    if (cleanupEc) {
        // 清理失败＝残留事实如实呈现（不吞不瞒——用户需知道目录残留并
        // 手动清理；零新增码（D-WF-7），残留定位并入建议动作文本）。
        failure.recommendedAction +=
            "；注意：创建失败的项目目录未能自动清理（" + cleanupEc.message()
            + "），请手动删除：" + failure.context;
    }
    outcome.failure = std::move(failure);
    return outcome;  // created=false、store 空——零半成品（目录已删或残留已呈现）
}

// =====================================================================
// 打开协议编排（PM-02 五步——§7.2；WP-22-T05 落位段）
// =====================================================================

namespace {

/// 路径词面（UX-03 context/file 半区的人读形态——u8string 无损 UTF-8；
/// 同 pathSummaryText：不做规范化/加引号，呈现修饰归 ui）。
std::string openPathText(const std::filesystem::path& p)
{
    return p.empty() ? std::string() : p.u8string();
}

/**
 * @brief 从机器可读键值串 detail 中提取指定键的值（通用提取点——
 *        失败定位键与升级指引三键共用）。
 *
 * project 侧打开失败 detail 是机器可读键值串（"project/<域>: <原因>
 * key=value ..."）。提取规则：取**最后一次**出现的 " <键>=" 词形
 * （失败链/多键场景的最后落点最具体），值域到串尾或下一个 " <词>="
 * 键边界为止。这是**尽力呈现**而非协议解析：找不到键返回空串（调用方
 * 按各自兜底口径处理——file 回退目标路径、升级指引留空不伪造）；含
 * 空格值仅在下一条键存在时可能被截断（cause 全文始终透传兜底）。
 *
 * @param detail   [in] 对端 detail 原文（StoreError::what()）
 * @param keyProbe [in] 键探针词（含前导空格与 '='，如 " path="、
 *                  " document="——前导空格防止同形子串误配，如
 *                  " document=" 不会匹配 " document-format-id="）
 * @return 键值串（空＝detail 无该键）
 */
std::string detailValueAfter(const std::string& detail, const std::string& keyProbe)
{
    const std::size_t keyPos = detail.rfind(keyProbe);
    if (keyPos == std::string::npos) {
        return {};
    }
    const std::size_t valueStart = keyPos + keyProbe.size();

    // 值域终点：其后第一个 " <词>=" 键边界（键词为字母/数字/连字符——
    // project 侧键词表 path/file/detail/revision/schemaVersion/...）。
    std::size_t valueEnd = detail.size();
    for (std::size_t i = valueStart + 1; i < detail.size(); ++i) {
        if (detail[i] != ' ') {
            continue;  // 键边界必以空格起始——非空格继续扫
        }
        std::size_t j = i + 1;
        while (j < detail.size()
               && (std::isalnum(static_cast<unsigned char>(detail[j])) != 0
                   || detail[j] == '-')) {
            ++j;
        }
        // "空格＋至少一个键词字符＋'='"＝下一条键的起点——值域在此截断。
        if (j > i + 1 && j < detail.size() && detail[j] == '=') {
            valueEnd = i;
            break;
        }
    }
    return detail.substr(valueStart, valueEnd - valueStart);
}

/**
 * @brief 从对端 detail 提取失败定位文件（PM-02"失败显示具体文件"）。
 *
 * 定位键为 "path=<路径>" 或 "file=<路径>"——两键取更靠后者（最后落点
 * 最具体）；键提取逻辑委托 detailValueAfter（通用规则见其注）。
 *
 * @param detail [in] 对端 detail 原文（StoreError::what()）
 * @return 定位文件串（空＝detail 无定位键——调用方回退目标路径词面，
 *         file 恒非空的呈现承诺不变）
 */
std::string extractDetailLocation(const std::string& detail)
{
    // 定位键扫描：两个候选键取更靠后者（如 manifest 解析失败的
    // "path=<清单文件> detail=..." 链尾）。rfind 取各自最后一次出现，
    // 位置大者即链上更后的落点；两键都缺时 npos 短路返回空。
    const std::size_t pathKey = detail.rfind(" path=");
    const std::size_t fileKey = detail.rfind(" file=");
    if (pathKey == std::string::npos && fileKey == std::string::npos) {
        return {};
    }
    const std::string& probe =
        (pathKey != std::string::npos
         && (fileKey == std::string::npos || pathKey > fileKey))
            ? std::string(" path=")
            : std::string(" file=");
    return detailValueAfter(detail, probe);
}

/**
 * @brief 按对端稳定码分派建议动作（UX-03 半区三——switch 封闭集词表）。
 *
 * 建议文本是编排器直产的呈现半区（人读中文——同 NewProjectFailure 先例；
 * 键化文案归 diagnostics/ui 文案体系，本面为编排器组装的即时呈现材料）。
 * 分派依据＝StoreErrorCode（对端稳定码——枚举封闭集，新增码＝对端单元卡
 * 增量修订时此处同步，编译期 switch 遗漏即告警）。
 */
std::string recommendActionFor(project::StoreErrorCode code)
{
    switch (code) {
    case project::StoreErrorCode::NotAProject:
        return "所选位置不是项目目录：请选择 .rwdesign 项目目录，"
               "或用新建向导创建项目";
    case project::StoreErrorCode::FormatLegacy:
        return "项目格式较旧：请使用升级工具迁移后再打开（本软件不自动升级）";
    case project::StoreErrorCode::SchemaFuture:
        return "项目由更新版本创建：请升级软件后再打开（升级指引见原因中的版本信息）";
    case project::StoreErrorCode::StoreCorrupt:
        return "项目数据损坏：请按失败文件定位检查磁盘与备份，必要时从备份恢复";
    case project::StoreErrorCode::LockHeldByOther:
        return "项目已被占用：可关闭对方实例后重试，或以只读方式打开查看";
    case project::StoreErrorCode::MediaReadOnly:
    case project::StoreErrorCode::AccessDenied:
        return "请检查存储介质与访问权限后重试";
    default:
        // 其余码（写路径/命令/归档类）不在打开协议失败面——通用兜底指引。
        return "请检查项目目录后重试";
    }
}

}  // namespace

OpenTargetKind classifyOpenTarget(const std::filesystem::path& path)
{
    // 空路径＝无目标（宿主应拦截取消态；此处按 Unknown 呈现面兜底——
    // run 入口另有 fail-fast 前置，本纯函数不抛以保"分流判定"纯面）。
    if (path.empty()) {
        return OpenTargetKind::Unknown;
    }

    // 第一判定：实测目录（.rwdesign 目录形态——形态细检归 open ②步
    // not-a-project，入口分流不复制对端规则——PA-1）。错误码版 is_directory
    // 不抛：不可达（权限/竞态删除）→ false 落入词面判定。
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec) && !ec) {
        return OpenTargetKind::ProjectDirectory;
    }

    // 第二判定：扩展名词面（.rwpack——大小写不敏感；Windows 词面惯例，
    // 与资源管理器双击语义一致）。包文件不要求实测存在（拖放/对话框
    // 词面识别——不存在包的失败由导入通道呈现）。
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    if (extension == kPackageExtensionToken) {
        return OpenTargetKind::PackageFile;
    }

    // 其余（普通文件/无扩展/其他扩展）→ 不可识别（失败呈现面）。
    return OpenTargetKind::Unknown;
}

OpenProjectOutcome OpenProjectFlow::run(OpenSource source,
                                        const std::filesystem::path& path,
                                        core::IDomainEventBus* eventBus,
                                        project::IDiagnosticsSink* diagnosticsSink)
{
    // ---- 第 1 段：前置校验（调用方契约违约 fail-fast）----
    // 空路径＝宿主把"取消"漏传到了编排器（取消不是错误也不进编排——
    // UX-03；对话框返回空路径应就地结束向导）。
    if (path.empty()) {
        throw WorkflowError(
            "OpenProjectFlow::run: 空路径（取消应在宿主侧拦截，不得进入打开编排）");
    }

    OpenProjectOutcome outcome;
    outcome.source = source;

    // ---- 第 2 段：协议①入口分流（格式识别——§7.2 表①行 workflow 面）。
    // 分流目标：目录→打开通道（第 3 段）；包→导入通道（WP-22-T07 落位，
    // 本批登记分流零副作用）；不可识别→失败呈现。
    const OpenTargetKind kind = classifyOpenTarget(path);
    outcome.targetKind = kind;
    if (kind == OpenTargetKind::Unknown) {
        OpenProjectFailure failure;
        failure.context = openPathText(path);
        failure.file = openPathText(path);  // 具体文件＝目标本身（AT-20 观测点恒非空）
        failure.cause =
            "无法识别为 .rwdesign 项目目录或 .rwpack 包文件";
        failure.recommendedAction =
            "请选择 .rwdesign 项目目录或 .rwpack 包文件后重试";
        outcome.failure = std::move(failure);
        return outcome;  // opened=false——零副作用（未触达 store 侧）
    }
    if (kind == OpenTargetKind::PackageFile) {
        // 分流登记返回：不解包、不建目录、不触碰任何盘面（包导入编排
        // 归 WP-22-T07——本批三入口识别到位即验收面；failure 置空＝
        // 分流不是错误，调用方按 targetKind 路由包导入向导）。
        return outcome;
    }

    // ---- 第 3 段：协议②③⑤服务侧（ProjectStoreFactory::open——PRJ-T08）。
    // 激活前失败不影响当前项目（§8.7）：open 完整构造候选上下文才返回，
    // 任何失败路径不写当前项目；编排器签名不接收当前 store——"不动当前
    // 项目"（AT-20）在此为结构性保证，测试以双项目事实复核。
    project::OpenStoreRequest request;
    request.path = path;
    request.mode = project::OpenMode::Writable;  // 请求写权限；被持锁时
                                                 // 服务侧降级只读（PM-07
                                                 // 不阻塞等待）
    request.eventBus = eventBus;
    request.diagnostics = diagnosticsSink;
    try {
        project::OpenStoreResult result =
            project::ProjectStoreFactory::open(request);

        // ---- 第 5 段：成功（协议⑤激活材料移交——会话激活与门控 epoch
        // 重建由调用方执行：store 交接即激活的编排面事实）。
        outcome.opened = true;
        outcome.readonly = !result.writable;  // PM-07 降级只读的如实登记
        outcome.store = std::move(result.store);
        if (outcome.store) {
            outcome.projectId = outcome.store->projectId();
            outcome.canonicalPath = outcome.store->canonicalPath();
        }
        outcome.recovery = std::move(result.recovery);
        return outcome;
    } catch (const project::StoreError& e) {
        // ---- 第 4 段：失败转呈现（环境/对端错误——值轨道，UX-03 三字段；
        // D-WF-7 零新增码：cause＝detail 原文透传含对端稳定码语义，码的
        // 用户文案归 diagnostics 供文案链路）。
        OpenProjectFailure failure;
        failure.context = openPathText(path);
        failure.cause = e.what();
        // 具体文件＝detail 定位键提取（"path="/"file="）；无定位键（如
        // 跨文件不一致类）回退目标路径——file 恒非空（AT-20 呈现承诺）。
        failure.file = extractDetailLocation(e.what());
        if (failure.file.empty()) {
            failure.file = openPathText(path);
        }
        failure.recommendedAction = recommendActionFor(e.code());
        // PM-06 升级指引数据面（§7.5 行一）：仅旧格式/未来版本两稳定码
        // 失败时自 detail 提取三键（document/supported/upgrade——零加工
        // 透传，parseUpgradeGuidance）；其余码（not-a-project/store-
        // corrupt/锁类）无升级语义，字段恒 nullopt。"不自动升级"的结构
        // 性保证＝本编排器无任何写入口，呈现面之外无升级动作路径。
        if (e.code() == project::StoreErrorCode::FormatLegacy
            || e.code() == project::StoreErrorCode::SchemaFuture) {
            failure.upgradeGuidance = parseUpgradeGuidance(e.what());
        }
        outcome.failure = std::move(failure);
        return outcome;  // opened=false——零副作用（服务侧已保证无半构造上下文）
    } catch (const std::exception& e) {
        // 非对端分类的标准异常（环境异常面）——同样值轨道呈现，不穿透
        // 编排器（打开失败是用户流程事件，不是进程故障）。
        OpenProjectFailure failure;
        failure.context = openPathText(path);
        failure.cause = e.what();
        failure.file = openPathText(path);
        failure.recommendedAction = "请检查项目目录后重试";
        outcome.failure = std::move(failure);
        return outcome;
    }
}

// =====================================================================
// 旧格式/未来版本升级指引数据面（PM-06——§7.5 行一；WP-22-T08）
// =====================================================================

UpgradeGuidance parseUpgradeGuidance(const std::string& detail)
{
    // 三键独立提取（detailValueAfter 通用规则——" <键>=" 词形取值，值域
    // 到下一键边界）。键探针带前导空格与 '='：" document=" 不会误配
    // " document-format-id="（第 10 字符 '-' ≠ '='）——formatId 不符
    // 形态的 format-legacy detail 中 document 字段如实留空。
    // 找不到的键留空串（尽力呈现——不伪造数值/入口，PM-06 升级指引
    // 数据面的零加工纪律，D-WF-7）。
    UpgradeGuidance guidance;
    guidance.documentVersion = detailValueAfter(detail, " document=");
    guidance.supportedVersion = detailValueAfter(detail, " supported=");
    guidance.upgradeToolEntry = detailValueAfter(detail, " upgrade=");
    return guidance;
}

// =====================================================================
// 只读会话呈现数据面（PM-07——§7.5 行二；WP-22-T08）
// =====================================================================

ReadOnlySessionNotice buildReadOnlySessionNotice(const project::LockInfo& lockInfo)
{
    // 降级语义的裁决面归 project（Writable 请求被 OS 锁竞争拒绝时降级
    // ReadOnly——PM-07 不阻塞等待）；本组装点只做呈现数据折叠：
    //   - 持有者三值原样透传（PID 0＝未知口径不重写——撕裂读容忍，
    //     LockHolderRecord 注释；呈现层按"未知"呈现而非显示 PID=0）；
    //   - lockHeldByOther＝非本上下文持有且持有者 PID 非零（有可提示的
    //     他方记录）——显式只读打开/介质只读形态无他方记录（isSelf=
    //     false 且 pid=0），走 explicit 提示键；
    //   - editingDisabled/applyCommitDisabled 恒 true（PM-07"禁编辑与
    //     应用提交"的入口禁用数据——写路径权威拒绝归 project 收口，
    //     本面不复制权限判定，PA-1）。
    ReadOnlySessionNotice notice;
    notice.holderPid = lockInfo.holder.pid;
    notice.holderHost = lockInfo.holder.host;
    notice.holderHeartbeatUtc = lockInfo.holder.heartbeatUtc;
    notice.lockHeldByOther = !lockInfo.isSelf && lockInfo.holder.pid != 0;
    notice.noticeKey = notice.lockHeldByOther ? kReadOnlyLockHeldKey
                                              : kReadOnlyExplicitKey;
    return notice;  // readOnly/editingDisabled/applyCommitDisabled 取默认 true
}

// =====================================================================
// 关闭/切换/退出统一确认编排（PM-03——§7.3；WP-22-T06）
// =====================================================================

std::string closeScenarioKey(CloseKind kind)
{
    // 场景键＝前缀＋kind token（封闭集字面拼接——唯一映射点，NFR-MNT-03；
    // 值归 ui 文案资源，UX-02 键/值半区分工）。
    switch (kind) {
    case CloseKind::Close:  return std::string(kCloseFlowScenarioPrefix) + "close";
    case CloseKind::Switch: return std::string(kCloseFlowScenarioPrefix) + "switch";
    case CloseKind::Exit:   return std::string(kCloseFlowScenarioPrefix) + "exit";
    }
    return {};  // 词表外值（三值枚举不可构造——防御性；空串暴露而非伪造键）
}

std::vector<std::string> closeTaskStateTokens(const std::vector<core::TaskState>& states)
{
    // 短标签数据源纪律（§7.3 注二）：词表归 core/execution，workflow 只
    // 取数呈现——逐态经 core::toToken 冻结表（小写连字符，core.md §4.7）
    // 转换，零新增状态词（SA-12）、零重排序（输入序即输出序）。
    std::vector<std::string> tokens;
    tokens.reserve(states.size());
    for (const core::TaskState state : states) {
        tokens.emplace_back(core::toToken(state));
    }
    return tokens;
}

namespace {

/// 关闭失败的 UX-03 三字段装配（封闭槽位文案——编排器直产人读呈现面，
/// 同 OpenProjectFlow::run 先例；D-WF-7 零新增稳定码：cause 透传对端
/// detail 原文，码的用户文案归 diagnostics 供文案链路）。
CloseFlowFailure makeCloseFailure(std::string context,
                                  std::string cause,
                                  std::string action)
{
    CloseFlowFailure failure;
    failure.context = std::move(context);
    failure.cause = std::move(cause);
    failure.recommendedAction = std::move(action);
    return failure;
}

/// 轮询等待存储上下文进入 Closed（A7"等待"选项的实现锚点——project §9.7
/// Draining 排空协议：requestClose 拒绝新写并等待在途引用〔在途归档会话/
/// 在途事务/草稿落盘〕归零后自动收尾；project.md"ui 关闭对话框'等待'
/// 分支轮询 closed()"明文口径）。
///
/// 轮询无上限＝A7 语义本身（project.md §9.7："本契约不设超时参数，
/// Draining 可能长期等待是'等待'选项的语义本身"）；有界性由 execution
/// 终态路径全部释放归档引用（"不存在无 abandon 的终结路径"）＋L5 关闭
/// 控制器超阈值强制 abandonAll 兜底保证——编排线程只观察不推进（睡眠
/// 1 ms 一拍，避免忙等烧核；closed() 为内部互斥的快照查询，并发安全）。
void awaitStoreClosed(project::ProjectStore& store)
{
    while (!store.closed()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

/// 草稿三选的共用执行段（CloseFlow 与 SchemeBranchSwitchFlow 的 D1/C1
/// 复用——两流程的三选词表与动作语义完全一致，PM-12 明文复用 PM-04 规则）。
///
/// @param kind          [in] 对话框上下文（Close/Switch/Exit）
/// @param scenarioKey   [in] 场景文案键（项目关闭族／方案分支切换）
/// @param draftModules  [in] 未应用草稿模块清单（非空前置——调用方保证）
/// @param decisions     [in] 决策收集端口
/// @param drafts        [in] 草稿处置端口
/// @param contextText   [in] 失败呈现的上下文词面（场景描述）
/// @return nullopt＝三选完成（保存/放弃成功——流程继续）；非空＝编排
///         终止（Aborted/Failed＋中止阶段/失败呈现已装配进返回值对——
///         first=是否中止〔true=用户取消〕，second=失败呈现〔仅 Failed〕）
std::optional<std::pair<bool, std::optional<CloseFlowFailure>>>
runDraftDispositionStage(CloseKind kind,
                         const std::string& scenarioKey,
                         std::vector<std::string> draftModules,
                         ICloseDecisionPort& decisions,
                         ICloseDraftPort& drafts,
                         const std::string& contextText)
{
    // 决策点（C1）：呈现材料＝草稿模块清单（"将丢失哪些编辑"）＋场景键。
    CloseDialogData data;
    data.kind = kind;
    data.scenarioKey = scenarioKey;
    data.draftModules = std::move(draftModules);
    const DraftDisposition disposition = decisions.collectDraftDisposition(data);

    // 取消＝中止整个流程（PM-03"取消可中止"——AT-20/21 观测点）：编排
    // 立即终止，草稿保持原状、零排空零关闭（取消不是错误——UX-03）。
    if (disposition == DraftDisposition::Cancel) {
        return std::make_pair(true, std::nullopt);
    }

    // 保存（PM-04 保存语义——仅落 drafts/ 零修订）或放弃：动作失败如实
    // 呈现并终止（不带病排空——失败语义独立成立）。
    const bool actionOk = (disposition == DraftDisposition::Save)
                              ? drafts.saveDrafts()
                              : drafts.discardDrafts();
    if (!actionOk) {
        return std::make_pair(
            false,
            std::make_optional(makeCloseFailure(
                contextText,
                "未应用草稿的保存/放弃动作未能完成",
                "请检查磁盘可用性与项目写权限后重试关闭")));
    }
    return std::nullopt;  // 三选完成——流程继续
}

}  // namespace

CloseFlowOutcome CloseFlow::run(CloseKind kind,
                                project::ProjectStore& currentStore,
                                const CloseFlowRequest& request)
{
    // ---- 第 1 段：前置校验（调用方装配违约 fail-fast）----
    // 三端口缺一即无法编排（决策/草稿/排空各承担流程的一段）；候选路径
    // 与 kind 的匹配约束见 CloseFlowRequest 注（Switch 必填、其余必空——
    // 携带错位说明宿主把请求装配错了对象）。
    if (request.decisions == nullptr || request.drafts == nullptr
        || request.drain == nullptr) {
        throw WorkflowError(
            "CloseFlow::run: 决策/草稿/排空端口任一为空（装配违约）");
    }
    if (kind == CloseKind::Switch && request.candidatePath.empty()) {
        throw WorkflowError(
            "CloseFlow::run: Switch 请求缺少候选项目路径（装配违约）");
    }
    if (kind != CloseKind::Switch && !request.candidatePath.empty()) {
        throw WorkflowError(
            "CloseFlow::run: 非 Switch 请求携带了候选项目路径（装配违约）");
    }

    CloseFlowOutcome outcome;

    // ---- 第 2 段：草稿三选（D1/C1——未应用草稿？）----
    // 合一取数（空清单＝无草稿——跳过决策点，零决策调用）。
    std::vector<std::string> draftModules =
        request.drafts->unappliedDraftModules();
    if (!draftModules.empty()) {
        const auto stageResult = runDraftDispositionStage(
            kind, closeScenarioKey(kind), std::move(draftModules),
            *request.decisions, *request.drafts, "关闭项目");
        if (stageResult.has_value()) {
            if (stageResult->first) {
                // 用户取消——中止整个流程（排空/关闭零发生，当前项目原状）。
                outcome.result = CloseFlowOutcome::Result::Aborted;
                outcome.abortedAt = CloseFlowOutcome::AbortStage::DraftPrompt;
            } else {
                // 草稿处置失败——Failed 呈现（不带病排空）。
                outcome.result = CloseFlowOutcome::Result::Failed;
                outcome.failure = std::move(stageResult->second);
            }
            return outcome;
        }
    }

    // ---- 第 3 段：任务二选（D2/C2——运行中任务？）----
    // 先取九态清单（对话框内嵌任务清单——PM-03"9 态短标签"数据源），
    // 有非终态任务才进入决策点。
    bool skipSchedulerDrain = false;  // 协作取消分支已含排空（免重复 shutdown）
    if (request.drain->hasActiveTask(request.projectId)) {
        CloseDialogData data;
        data.kind = kind;
        data.scenarioKey = closeScenarioKey(kind);
        data.taskStates = request.drain->taskStates(request.projectId);
        const RunningTaskDecision decision =
            request.decisions->collectRunningTaskDecision(data);
        switch (decision) {
        case RunningTaskDecision::CancelFlow:
            // 取消流程——中止（草稿已按用户决策处置；排空/关闭零发生）。
            outcome.result = CloseFlowOutcome::Result::Aborted;
            outcome.abortedAt = CloseFlowOutcome::AbortStage::TaskPrompt;
            return outcome;
        case RunningTaskDecision::CooperativeCancel:
            // 协作取消（CANCEL 节点）：逐任务 requestCancel→取消协议
            // （worker 回收＋临时目录清理＋归档 abandon——"取消即清理
            // 临时区"，承载归 execution）→排空。失败如实呈现。
            outcome.cooperativeCancelled = true;
            if (!request.drain->cooperativeCancel()) {
                outcome.result = CloseFlowOutcome::Result::Failed;
                outcome.failure = std::make_optional(makeCloseFailure(
                    "关闭项目",
                    "协作取消未能在承诺窗口内完成排空",
                    "请查看任务清单中的任务状态后重试关闭"));
                return outcome;
            }
            skipSchedulerDrain = true;  // 取消分支已含排空——第 4 段免重复
            break;
        case RunningTaskDecision::Wait:
            // 等待（A7"等待"选项）：排空动作统一在第 4 段执行——此处只
            // 进入后续流程（观测位在第 4 段置位）。
            break;
        }
    }

    // ---- 第 4 段：调度排空（DRAIN 前半——execution §7.5）----
    // 无任务（D2"否"边）与"等待"选择都走幂等排空（shutdown(
    // CancelQueuedAndWait)＋轮询 drained——无任务时即刻满足）；协作取消
    // 分支的排空已在其端口实现内完成（skipSchedulerDrain）。
    if (!skipSchedulerDrain) {
        outcome.waitedForArchiveDrain = true;
        if (!request.drain->waitDrain()) {
            outcome.result = CloseFlowOutcome::Result::Failed;
            outcome.failure = std::make_optional(makeCloseFailure(
                "关闭项目",
                "任务排空未能在承诺窗口内完成（在途运行未全部终结）",
                "请查看任务清单中的任务状态后重试关闭"));
            return outcome;
        }
    }

    // ---- 第 5 段：候选验证（SWITCH——仅 Switch；在存储上下文关闭之前）----
    // 切换＝关闭后候选验证成功才切上下文（§7.3 SWITCH 节点）：候选经
    // ProjectStoreFactory::open 完整构造才算成功（激活前失败不影响当前
    // 项目——project §8.7；open 从不写当前项目）。验证失败时当前 store
    // 未被触碰（第 6 段的 requestClose 尚未发生）——"验证失败不动当前
    // 项目"在此为编排序保证（契约测试以真实落盘双项目复核）。
    std::unique_ptr<project::ProjectStore> candidate;
    if (kind == CloseKind::Switch) {
        project::OpenStoreRequest openRequest;
        openRequest.path = request.candidatePath;
        openRequest.mode = project::OpenMode::Writable;  // 请求写权限；被持锁
                                                         // 时服务侧降级只读
                                                         // （PM-07 不阻塞等待）
        try {
            project::OpenStoreResult opened =
                project::ProjectStoreFactory::open(openRequest);
            candidate = std::move(opened.store);
        } catch (const project::StoreError& e) {
            // 候选打开失败——UX-03 呈现（cause＝detail 原文透传，含对端
            // 稳定码语义与 path=/file= 定位键——D-WF-7 零加工）。
            outcome.result = CloseFlowOutcome::Result::Failed;
            outcome.failure = std::make_optional(makeCloseFailure(
                request.candidatePath.u8string(), e.what(),
                "当前项目保持原状；请解决候选项目的问题后重新发起切换"));
            return outcome;
        } catch (const std::exception& e) {
            // 非对端分类的环境异常——同样值轨道呈现，不穿透编排器。
            outcome.result = CloseFlowOutcome::Result::Failed;
            outcome.failure = std::make_optional(makeCloseFailure(
                request.candidatePath.u8string(), e.what(),
                "当前项目保持原状；请检查候选项目目录后重新发起切换"));
            return outcome;
        }
    }

    // ---- 第 6 段：存储上下文排空（DRAIN 后半——A7 等待实现锚点）----
    // requestClose：Active→Draining（拒绝新写），在途引用归零后自动收尾
    // （在途归档完成＋草稿落盘完成＝A7 的排空完成定义）；幂等——Closed
    // 态重入只返回当前在途数。在途>0 时轮询 closed()（"等待"选项即等待
    // 此完成——见 awaitStoreClosed 注）。
    (void)currentStore.requestClose();
    awaitStoreClosed(currentStore);

    // ---- 第 7 段：结果（Proceed——切换上下文的材料移交）----
    // Switch：候选移交调用方激活（界面会话切至候选——S7 时序）；Close/
    // Exit：界面会话结束由调用方执行（退出复用同一流程——kind 仅登记）。
    outcome.result = CloseFlowOutcome::Result::Proceed;
    if (kind == CloseKind::Switch) {
        outcome.candidateStore = std::move(candidate);
    }
    return outcome;
}

// =====================================================================
// 方案分支切换编排（PM-12——§7.3 注三条；零写入会话选择；WP-22-T06）
// =====================================================================

SchemeBranchSwitchOutcome SchemeBranchSwitchFlow::run(
    const SchemeBranchSwitchRequest& request,
    ICloseDecisionPort& decisions,
    ICloseDraftPort& drafts)
{
    // ---- 第 1 段：前置校验（调用方装配违约 fail-fast）----
    // 同分支切换＝宿主装配缺陷：分支清单呈现层应禁用当前分支项（选择面
    // 已挡），到不了编排核——零写入承诺不覆盖这种无意义调用（fail-fast
    // 暴露装配错误，不静默放行）。空目标用 isValid()（全零保留值纪律——
    // core.md §4.1 U-1；toCanonical 对全零值产出"brn-"＋全零文本而非
    // 空串，不能以空文本判空）。
    const std::string currentText = request.currentBranch.toCanonical();
    const std::string targetText = request.targetBranch.toCanonical();
    if (!request.targetBranch.isValid() || targetText == currentText) {
        throw WorkflowError(
            "SchemeBranchSwitchFlow::run: 目标分支为空或与当前分支相同"
            "（同分支切换＝装配违约）");
    }

    SchemeBranchSwitchOutcome outcome;

    // ---- 第 2 段：草稿处置前置（PM-12→PM-04 规则）----
    // 分支切换前先处置未应用草稿（PM-12 明文引用 PM-04 规则——三选词表
    // 与动作语义复用 CloseFlow 同段；保存＝在原分支落盘）。注意：此处
    // 的草稿落盘/放弃是**用户显式决策**的 PM-04 写，不属于"切换写"——
    // PM-12 零写入承诺的对象是切换动作本身（无修订、不写 HEAD）。
    std::vector<std::string> draftModules = drafts.unappliedDraftModules();
    if (!draftModules.empty()) {
        const auto stageResult = runDraftDispositionStage(
            CloseKind::Switch, kSchemeSwitchScenarioKey,
            std::move(draftModules), decisions, drafts, "切换方案分支");
        if (stageResult.has_value()) {
            if (stageResult->first) {
                // 用户取消——切换零发生（草稿原状、活动分支不变）。
                outcome.result = SchemeBranchSwitchOutcome::Result::Aborted;
                outcome.abortedAt = CloseFlowOutcome::AbortStage::DraftPrompt;
            } else {
                outcome.result = SchemeBranchSwitchOutcome::Result::Failed;
                outcome.failure = std::move(stageResult->second);
            }
            return outcome;
        }
    }

    // ---- 第 3 段：切换批准（Proceed——零写入的结构性落点）----
    // 编排核全程不触存储上下文（run 签名零 store 参数——不存在产生修订
    // 或写 HEAD 的代码路径）；活动分支登记由宿主会话层经 project 会话
    // 接口应用（project.md §4.5：切换＝纯会话选择——不产生修订、不写
    // 任何文件含 HEAD；HEAD 记录的是"最后一次提交所在分支"）。URDF
    // 基线修订只读的编辑禁令归 project/modeling 侧强制（N3/N5），切换
    // 本身允许切至基线分支查看——编排核不越权拒绝（PA-1）。
    outcome.result = SchemeBranchSwitchOutcome::Result::Proceed;
    return outcome;
}

// =====================================================================
// 另存为与包导出/导入编排（PM-05——§7.4；WP-22-T07）
// =====================================================================

namespace {

/// 词法规范化＋去尾分隔（同径判定的收敛形）。
///
/// 为什么不只 lexically_normal：按标准语义，以 dot/dot-dot 收尾的路径
/// 规约后保留尾部分隔符（如 "a/b/.." → "a/"——最后 filename 已被删除，
/// 分隔符作为"该路径指向目录"的痕迹保留），与 "a/b" 的规范化形不相等。
/// 同径判定需要"无尾分隔"的收敛形：filename() 为空（以分隔符结尾）时
/// 取一次 parent_path() 收尾。
std::filesystem::path normalizedPathForCompare(const std::filesystem::path& p)
{
    std::filesystem::path n = p.lexically_normal();
    if (!n.empty() && n.filename().empty()) {
        n = n.parent_path();
    }
    return n;
}

/// 词面同径判定（normalizedPathForCompare 词法收敛——不触盘、不解析符号
/// 链接；与 recentProjectKey 同款"词法收敛为键"纪律，用于"目标与源相同"
/// 的前置呈现与 fail-fast 判定。大小写不收敛——Windows 词面大小写差异
/// 的最终裁决在执行面/project 规范路径，本判定只拦明显同径词面）。
bool samePathLexical(const std::filesystem::path& a, const std::filesystem::path& b)
{
    if (a.empty() || b.empty()) {
        return false;  // 空路径不参与同径判定（空由各自的 empty 键呈现）
    }
    return normalizedPathForCompare(a) == normalizedPathForCompare(b);
}

/// .rwpack 扩展名词面（大小写不敏感——Windows 词面惯例，与
/// classifyOpenTarget 的包识别同口径；PA-1：词面识别是入口面，包本体
/// 合法性的最终裁决在执行面魔数/结构校验）。
bool hasRwpackExtension(const std::filesystem::path& p)
{
    std::string extension = p.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return extension == kPackageExtensionToken;
}

/// 目录存在且非空（前置呈现检查——TOCTOU 窗口的最终裁决在执行面；
/// 错误码版 API 不抛：不可达→false 呈现为可放行，执行面再拒）。
bool directoryExistsNonEmpty(const std::filesystem::path& p)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(p, ec) || ec) {
        return false;  // 不存在/不可达＝不满足"已存在且非空"
    }
    return !std::filesystem::is_empty(p, ec) && !ec;
}

/// 生成"残留清理未完成"类失败的建议动作（AT-20 观测位为假的呈现半区
/// ——残留事实如实呈现＋手动清理指引；三处编排共用同一文案，唯一映射点）。
std::string leftoverCleanupAction()
{
    return "存在未清理的残留目录：请手动删除后重试（取消/失败流程会先尝试自动清理）";
}

}  // namespace

PackageSelectionFlags defaultSelectionOf(
    const std::optional<PackageSelectionFlags>& remembered)
{
    // 记忆有值→原样采用（PM-05"记忆默认"——上次确认的勾选即本次初始值）；
    // 无值→全选缺省（完整目录复制语义——§7.4 行一，不发明部分复制默认）。
    return remembered.value_or(PackageSelectionFlags{});
}

std::vector<ui::TextKey> validateSaveAsInputs(const std::filesystem::path& sourceDir,
                                              const std::filesystem::path& targetDir)
{
    // 键序＝头文件声明序（target-empty → target-same-as-source →
    // target-exists-nonempty）——确定性 NFR-COR-02 同型；键构造集中本处
    // （NFR-MNT-03——调用方只消费键，不手拼）。
    std::vector<ui::TextKey> keys;
    if (targetDir.empty()) {
        keys.push_back(ui::TextKey(std::string(kSaveAsErrorKeyPrefix) + "target-empty"));
    } else if (samePathLexical(sourceDir, targetDir)) {
        keys.push_back(
            ui::TextKey(std::string(kSaveAsErrorKeyPrefix) + "target-same-as-source"));
    }
    if (directoryExistsNonEmpty(targetDir)) {
        keys.push_back(
            ui::TextKey(std::string(kSaveAsErrorKeyPrefix) + "target-exists-nonempty"));
    }
    return keys;
}

SaveAsOutcome SaveAsFlow::run(project::ProjectStore& source,
                              const SaveAsRequest& request,
                              ISaveAsPort& saveAsPort,
                              IFlowCancelToken* cancel,
                              const FlowProgressCallback& progress,
                              core::IDomainEventBus* eventBus,
                              project::IDiagnosticsSink* diagnosticsSink)
{
    // ---- 第 1 段：前置校验（调用方契约违约 fail-fast）----
    // 呈现面校验（validateSaveAsInputs）已挡用户输入错误；到编排核仍
    // 违约＝宿主装配缺陷（放行了不该放行的输入）——fail-fast 暴露，
    // 不静默替用户重定向目标。
    if (request.targetDir.empty()) {
        throw WorkflowError("SaveAsFlow::run: 目标目录为空（取消应在宿主侧拦截）");
    }
    if (samePathLexical(source.canonicalPath(), request.targetDir)) {
        throw WorkflowError(
            "SaveAsFlow::run: 目标目录与源项目相同（同目录另存＝装配违约）");
    }

    SaveAsOutcome outcome;
    // 记忆登记面：确认勾选在所有路径恒回传（PM-05"记忆默认"的编排半区
    // ——调用方持久化归用户设置存储 PM-14/WP-22-T10）。
    outcome.selection = request.selection;

    // ---- 第 2 段：复制执行（PM-05 存储侧——§7.4 行一"复制执行归
    // project"；WP-04-T18 契约未生成，经 ISaveAsPort 端口触达——契约
    // note 豁免 dependsOn 边，L5 桥接 project 命令面/存储侧）。
    const ISaveAsPort::Execution execution =
        saveAsPort.executeCopy(source, request, cancel, progress);

    // ---- 第 3 段：取消分派（取消不是错误——UX-03：Canceled 态 failure
    // 置空零诊断；但"取消即清理"（AT-20）是流程承诺——清理观测位为假
    // ＝有残留，如实转 Failed 呈现，不带病报取消成功）。
    if (execution.cancelled) {
        if (execution.targetLeftClean) {
            outcome.result = SaveAsOutcome::Result::Canceled;
            return outcome;
        }
        SaveAsFailure failure;
        failure.context = openPathText(request.targetDir);
        failure.file = openPathText(request.targetDir);
        failure.cause = "取消后目标目录清理未完成，存在残留";
        failure.recommendedAction = leftoverCleanupAction();
        outcome.failure = std::move(failure);
        outcome.result = SaveAsOutcome::Result::Failed;
        return outcome;
    }

    // ---- 第 4 段：复制失败（环境/对端错误——值轨道呈现，UX-03 四字段；
    // 目标零残留由端口契约承诺——"失败不留半成品"同 createNew 口径）。
    if (!execution.copied) {
        SaveAsFailure failure;
        failure.context = openPathText(request.targetDir);
        failure.file = openPathText(request.targetDir);
        failure.cause = execution.cause.empty() ? "另存复制失败" : execution.cause;
        failure.recommendedAction = execution.action.empty()
            ? "请检查目标位置与磁盘状态后重试"
            : execution.action;
        outcome.failure = std::move(failure);
        outcome.result = SaveAsOutcome::Result::Failed;
        return outcome;
    }

    // ---- 第 5 段：按打开协议进入（PM-05 原文"换新 projectId 后按打开
    // 协议进入"——§7.2 五步复用：复制产物经完整打开协议②③⑤校验后才
    // 算进入；产物损坏在此暴露为打开失败，不以"复制成功"伪装进入成功）。
    // "不动当前项目"结构性成立：open 激活前失败不影响当前项目（§8.7），
    // 源 store 未被触碰（本编排核对源只读消费）。
    OpenProjectOutcome opened = OpenProjectFlow::run(OpenSource::Dialog,
                                                     request.targetDir,
                                                     eventBus, diagnosticsSink);
    if (opened.opened) {
        outcome.result = SaveAsOutcome::Result::Entered;
        outcome.readonly = opened.readonly;  // PM-07 降级只读如实登记
        outcome.store = std::move(opened.store);
        outcome.projectId = opened.projectId;  // 新项目身份（存储侧另存时分配的新 id）
        outcome.canonicalPath = std::move(opened.canonicalPath);
        return outcome;
    }

    // 打开失败转呈现（复制成功但进入失败＝环境失败——UX-03 四字段，
    // cause/file 自打开呈现转录，保持具体文件定位）。
    SaveAsFailure failure;
    failure.context = openPathText(request.targetDir);
    if (opened.failure.has_value()) {
        failure.file = opened.failure->file;
        failure.cause = opened.failure->cause;
        failure.recommendedAction = opened.failure->recommendedAction;
    } else {
        failure.file = openPathText(request.targetDir);
        failure.cause = "复制完成但按打开协议进入失败";
        failure.recommendedAction = "请检查目标目录后重试";
    }
    outcome.failure = std::move(failure);
    outcome.result = SaveAsOutcome::Result::Failed;
    return outcome;
}

std::vector<ui::TextKey> validatePackageExportInput(
    const std::filesystem::path& targetFile)
{
    // 键序＝头文件声明序（target-empty → target-extension）；导出目标已
    // 存在＝合法（OverwriteAtomic 原子覆盖是导出默认——io §7.2⑥），不
    // 做存在性校验。
    std::vector<ui::TextKey> keys;
    if (targetFile.empty()) {
        keys.push_back(
            ui::TextKey(std::string(kPackageExportErrorKeyPrefix) + "target-empty"));
    } else if (!hasRwpackExtension(targetFile)) {
        keys.push_back(
            ui::TextKey(std::string(kPackageExportErrorKeyPrefix) + "target-extension"));
    }
    return keys;
}

PackageExportOutcome PackageExportFlow::run(project::ProjectStore& source,
                                            const PackageExportRequest& request,
                                            IPackageExportPort& exportPort,
                                            IFlowCancelToken* cancel,
                                            const FlowProgressCallback& progress)
{
    // ---- 第 1 段：前置校验（调用方契约违约 fail-fast——同 SaveAsFlow
    // 口径：呈现面已挡用户输入错误，到编排核仍违约＝装配缺陷）。
    if (request.targetFile.empty()) {
        throw WorkflowError("PackageExportFlow::run: 目标文件为空");
    }
    if (!hasRwpackExtension(request.targetFile)) {
        throw WorkflowError(
            "PackageExportFlow::run: 目标文件扩展名非 .rwpack（呈现面已挡，"
            "到编排核即违约）");
    }

    PackageExportOutcome outcome;

    // ---- 第 2 段：源元数据组装（§7.1 rwpack.json 契约的 workflow 面——
    // 源项目身份与 HEAD 修订；编排核对源 store 只读消费，零写面调用——
    // "导出失败保证项目状态不变"（§7.4，MDL-20 同型口径）的编排侧结构
    // 性保证。读取异常（store 已关闭等）→ 环境错误值轨道 Failed）。
    PackageExportRequest filled = request;
    try {
        filled.sourceProjectId = source.projectId().toCanonical();
        filled.headRevisionId = source.query().head().id.toCanonical();
    } catch (const std::exception& e) {
        PackageExportFailure failure;
        failure.context = openPathText(request.targetFile);
        failure.file = openPathText(request.targetFile);
        failure.cause = e.what();
        failure.recommendedAction = "请确认项目已打开且可读后重试";
        outcome.failure = std::move(failure);
        outcome.result = PackageExportOutcome::Result::Failed;
        return outcome;
    }

    // ---- 第 3 段：导出执行（ZIP 传输封装归执行端口——P-IO-1 注入形态，
    // L5 桥接 io 包导出器与 project 快照视图；编排面零 io 类型零解析）。
    const IPackageExportPort::Execution execution =
        exportPort.exportPackage(source, filled, cancel, progress);

    // ---- 第 4 段：取消分派（取消不是错误——UX-03；"取消即清理临时区"
    // （PM-05）是流程承诺——清理观测位为假＝临时区残留，如实转 Failed，
    // 不带病报取消成功）。
    if (execution.cancelled) {
        if (execution.temporaryAreaCleaned) {
            outcome.result = PackageExportOutcome::Result::Canceled;
            return outcome;
        }
        PackageExportFailure failure;
        failure.context = openPathText(request.targetFile);
        failure.file = openPathText(request.targetFile);
        failure.cause = "取消后导出临时区清理未完成，存在残留";
        failure.recommendedAction = leftoverCleanupAction();
        outcome.failure = std::move(failure);
        outcome.result = PackageExportOutcome::Result::Failed;
        return outcome;
    }

    // ---- 第 5 段：导出失败（目标保持先前状态——原子替换未发生；UX-03
    // 四字段，cause/action 自端口 Execution）。
    if (!execution.exported) {
        PackageExportFailure failure;
        failure.context = openPathText(request.targetFile);
        failure.file = openPathText(request.targetFile);
        failure.cause = execution.cause.empty() ? "包导出失败" : execution.cause;
        failure.recommendedAction = execution.action.empty()
            ? "请检查目标位置与磁盘状态后重试（原有文件未受影响）"
            : execution.action;
        outcome.failure = std::move(failure);
        outcome.result = PackageExportOutcome::Result::Failed;
        return outcome;
    }

    // ---- 第 6 段：成功（完整性自检通过的统计登记——目标 .rwpack 就位）。
    outcome.result = PackageExportOutcome::Result::Completed;
    outcome.entryCount = execution.entryCount;
    outcome.totalBytes = execution.totalBytes;
    return outcome;
}

std::vector<ui::TextKey> validatePackageImportInput(const PackageImportRequest& request)
{
    // 键序＝头文件声明序（pack-empty → target-empty → pack-extension →
    // target-exists）；预算/路径穿越/包结构的最终裁决在执行面全量校验
    // （PA-1——编排面零预算数值零包解析，I-WF-3 同精神）。
    std::vector<ui::TextKey> keys;
    if (request.packFile.empty()) {
        keys.push_back(
            ui::TextKey(std::string(kPackageImportErrorKeyPrefix) + "pack-empty"));
    }
    if (request.targetDir.empty()) {
        keys.push_back(
            ui::TextKey(std::string(kPackageImportErrorKeyPrefix) + "target-empty"));
    }
    if (!request.packFile.empty() && !hasRwpackExtension(request.packFile)) {
        keys.push_back(
            ui::TextKey(std::string(kPackageImportErrorKeyPrefix) + "pack-extension"));
    }
    if (!request.targetDir.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(request.targetDir, ec) && !ec) {
            // 目标已存在＝发布预检必拒（NeverOverwrite——io §7.4 对目录
            // 不适用覆盖）——前置呈现，执行面仍兜底。
            keys.push_back(
                ui::TextKey(std::string(kPackageImportErrorKeyPrefix) + "target-exists"));
        }
    }
    return keys;
}

PackageImportOutcome PackageImportFlow::run(const PackageImportRequest& request,
                                            IPackageImportPort& importPort,
                                            IFlowCancelToken* cancel,
                                            const FlowProgressCallback& progress)
{
    // ---- 第 1 段：前置校验（调用方契约违约 fail-fast——同前两编排器）。
    if (request.packFile.empty()) {
        throw WorkflowError("PackageImportFlow::run: 包文件为空");
    }
    if (request.targetDir.empty()) {
        throw WorkflowError("PackageImportFlow::run: 目标目录为空");
    }
    if (!hasRwpackExtension(request.packFile)) {
        throw WorkflowError(
            "PackageImportFlow::run: 包文件扩展名非 .rwpack（呈现面已挡，"
            "到编排核即违约）");
    }

    PackageImportOutcome outcome;

    // ---- 第 2 段：导入执行（校验→发布→清理在端口内折叠——§7.4 行三
    // "校验执行归 io、发布归 project"；编排核零发布语义的结构性保证：
    // 本签名无任何 rename/发布动作，只消费端口回传的执行事实）。
    outcome.execution = importPort.importPackage(request, cancel, progress);
    const PackageImportExecution& execution = outcome.execution;

    // ---- 第 3 段：取消分派（取消不是错误——UX-03：Canceled 态 failure
    // 置空且诊断恒空；"失败不留目标目录"（PM-05/NFR-SEC-01/02）是流程
    // 承诺——清理观测位为假＝目标/临时区残留，如实转 Failed 呈现）。
    if (execution.cancelled) {
        if (execution.targetLeftClean) {
            outcome.result = PackageImportOutcome::Result::Canceled;
            return outcome;
        }
        PackageImportFailure failure;
        failure.context = openPathText(request.packFile);
        failure.file = openPathText(request.targetDir);
        failure.cause = "取消后目标目录/临时区清理未完成，存在残留";
        failure.recommendedAction = leftoverCleanupAction();
        outcome.failure = std::move(failure);
        outcome.result = PackageImportOutcome::Result::Failed;
        return outcome;
    }

    // ---- 第 4 段：校验失败（防护拒绝/结构损坏/哈希不符——校验报告
    // 材料恒在 Outcome.execution〔diagnostics 透传对端稳定码＋脱敏详情，
    // D-WF-7 零加工〕；failure 承载 UX-03 半区，file＝包文件定位）。
    if (!execution.verified) {
        PackageImportFailure failure;
        failure.context = openPathText(request.packFile);
        failure.file = openPathText(request.packFile);
        failure.cause = execution.cause.empty() ? "包导入全量校验未通过" : execution.cause;
        failure.recommendedAction = execution.action.empty()
            ? "请核对包文件来源与完整性后重试（目标目录未受影响）"
            : execution.action;
        outcome.failure = std::move(failure);
        outcome.result = PackageImportOutcome::Result::Failed;
        return outcome;
    }

    // ---- 第 5 段：校验通过但发布未完成（端口契约违约事实——校验与
    // 发布在端口内折叠，两者必须同真；如实呈现差异，不伪造完成）。
    if (!execution.published) {
        PackageImportFailure failure;
        failure.context = openPathText(request.packFile);
        failure.file = openPathText(request.targetDir);
        failure.cause = "包校验通过但发布未完成";
        failure.recommendedAction = "请检查目标位置后重试";
        outcome.failure = std::move(failure);
        outcome.result = PackageImportOutcome::Result::Failed;
        return outcome;
    }

    // ---- 第 6 段：成功（校验报告行集经 buildPackageImportReportView
    // 组装呈现——目标目录已就位，进入项目由调用方经打开协议执行）。
    outcome.result = PackageImportOutcome::Result::Completed;
    return outcome;
}

std::vector<PackageImportReportLine> buildPackageImportReportView(
    const PackageImportExecution& execution)
{
    // 行集固定序（头文件声明：final-state → manifest-entries →
    // verified-entries → total-bytes → diagnostics-count〔仅非空〕 →
    // target-clean）；值文本全部为工程用语 token 或十进制数字（UX-02——
    // 零哈希/Schema/内部插件名；单位＝字节经键语义注明，值保持裸数字，
    // 格式化修饰归 ui）。
    std::vector<PackageImportReportLine> lines;

    // 行 1：终态 token（verified|canceled|failed——机器可判词形）。
    PackageImportReportLine finalState;
    finalState.labelKey = ui::TextKey(std::string(kPackageImportReportKeyPrefix) + "final-state");
    if (execution.verified) {
        finalState.valueText = "verified";
    } else if (execution.cancelled) {
        finalState.valueText = "canceled";
    } else {
        finalState.valueText = "failed";
    }
    lines.push_back(std::move(finalState));

    // 行 2~4：汇总计数（manifest 条目总数/哈希通过条目数/累计展开字节）。
    PackageImportReportLine manifest;
    manifest.labelKey = ui::TextKey(std::string(kPackageImportReportKeyPrefix) + "manifest-entries");
    manifest.valueText = std::to_string(execution.manifestEntries);
    lines.push_back(std::move(manifest));

    PackageImportReportLine verified;
    verified.labelKey = ui::TextKey(std::string(kPackageImportReportKeyPrefix) + "verified-entries");
    verified.valueText = std::to_string(execution.verifiedEntries);
    lines.push_back(std::move(verified));

    PackageImportReportLine bytes;
    bytes.labelKey = ui::TextKey(std::string(kPackageImportReportKeyPrefix) + "total-bytes");
    bytes.valueText = std::to_string(execution.totalBytes);
    lines.push_back(std::move(bytes));

    // 行 5：诊断计数（仅 diagnostics 非空时——零占位行纪律，同
    // buildNewProjectSummary 的行集裁剪）。
    if (!execution.diagnostics.empty()) {
        PackageImportReportLine diagCount;
        diagCount.labelKey =
            ui::TextKey(std::string(kPackageImportReportKeyPrefix) + "diagnostics-count");
        diagCount.valueText = std::to_string(execution.diagnostics.size());
        lines.push_back(std::move(diagCount));
    }

    // 行 6：清理观测（yes|no——目标与临时区清理承诺的如实登记）。
    PackageImportReportLine clean;
    clean.labelKey = ui::TextKey(std::string(kPackageImportReportKeyPrefix) + "target-clean");
    clean.valueText = execution.targetLeftClean ? "yes" : "no";
    lines.push_back(std::move(clean));

    return lines;
}

// =====================================================================
// 外部源重关联编排（PM-09——§7.5 行三；WP-22-T08）
// =====================================================================

namespace {

/// 重关联失败的 UX-03 三字段装配（封闭槽位文案——编排器直产人读呈现
/// 面，同 OpenProjectFlow::run 先例；D-WF-7 零新增稳定码：cause 透传
/// 对端原因原文，码的用户文案归 diagnostics 供文案链路）。
RelinkFailure makeRelinkFailure(std::string context,
                                std::string cause,
                                std::string action)
{
    RelinkFailure failure;
    failure.context = std::move(context);
    failure.cause = std::move(cause);
    failure.recommendedAction = std::move(action);
    return failure;
}

/// 重关联资源定位词形（RelinkFailure.context 半区——对象身份规范文本
/// ＋登记路径的人读组合；UX-03"对象/上下文"半区，路径为空只出身份）。
std::string relinkContextText(const RelinkRequest& request,
                              const ExternalSourceStatus& status)
{
    std::string text = request.resource.isValid()
                           ? request.resource.toCanonical()
                           : std::string();
    if (!status.absolutePath.empty()) {
        text += "（" + status.absolutePath + "）";
    }
    return text;
}

}  // namespace

RelinkOutcome RelinkFlow::run(const RelinkRequest& request,
                              IExternalRelinkPort& relinkPort,
                              IRelinkDecisionPort& decisionPort)
{
    // ---- 第 1 段：前置校验（调用方契约违约 fail-fast）----
    // 全零 ObjectId＝保留值"未设置"（core Identity.hpp 保留值纪律）——
    // 宿主把无效资源传到重关联入口属装配缺陷（对话框应先选定资源），
    // fail-fast 暴露而非静默 NotNeeded 掩盖。
    if (!request.resource.isValid()) {
        throw WorkflowError("RelinkFlow::run: 资源对象身份无效（全零保留值"
                            "——重关联入口须携带有效资源 ObjectId）");
    }

    RelinkOutcome outcome;

    // ---- 第 2 段：检测（io 数据透传——NFR-REL-04）。
    // 缺失/变化检测执行归 io（登记基准与现路径内容比对），编排核只透传
    // 结论与对照材料；检测是纯读面（零写入零提交）。probe 抛＝环境失败
    // ——值轨道折叠（重关联失败是用户流程事件，不是进程故障）。
    try {
        outcome.status = relinkPort.probe(request);
    } catch (const std::exception& e) {
        outcome.result = RelinkOutcome::Result::Failed;
        outcome.failure = makeRelinkFailure(
            request.resource.toCanonical(), e.what(),
            "请检查外部源所在存储介质与访问权限后重试");
        return outcome;
    }

    // ---- 第 3 段：无事实早退（NotNeeded——零确认零提交）。
    // 外部源在位且内容与登记基准一致（state==Ok）＝无重关联事实——
    // 此时提交新修订只会产生无语义的历史噪音，与"显式提交"语义相悖
    // （PM-09：重关联提交承载的是"外部源事实变化后的重新关联"，不是
    // 无条件的修订制造）；status 照常回传供入口呈现。
    if (outcome.status.state == ExternalSourceState::Ok) {
        outcome.result = RelinkOutcome::Result::NotNeeded;
        return outcome;
    }

    // ---- 第 4 段：用户显式确认（PM-09"显式提交"的编排兑现——无用户
    // 确认不提交）。呈现材料＝检测状态（缺失/变化＋登记/现内容对照）。
    // 取消＝放弃（零提交零诊断——取消非错误，UX-03）；确认收集抛＝
    // 宿主面故障，值轨道折叠。
    try {
        const RelinkDisposition disposition =
            decisionPort.confirmRelink(outcome.status);
        if (disposition == RelinkDisposition::Cancel) {
            outcome.result = RelinkOutcome::Result::Canceled;
            return outcome;  // failure 空、revisionId 空——零提交
        }
    } catch (const std::exception& e) {
        outcome.result = RelinkOutcome::Result::Failed;
        outcome.failure = makeRelinkFailure(
            relinkContextText(request, outcome.status), e.what(),
            "请重试重关联操作；若反复失败请重启应用");
        return outcome;
    }

    // ---- 第 5 段：显式提交执行（端口内折叠 project ①命令端口 submit
    // ——重关联命令 token/载荷归 project 存储侧 WP-04-T17，编排核零
    // 命令知识）。提交抛＝环境失败，值轨道折叠。
    IExternalRelinkPort::RelinkExecution execution;
    try {
        execution = relinkPort.relink(request, outcome.status);
    } catch (const std::exception& e) {
        outcome.result = RelinkOutcome::Result::Failed;
        outcome.failure = makeRelinkFailure(
            relinkContextText(request, outcome.status), e.what(),
            "重关联提交失败：请检查项目写权限后重试（当前项目未受影响）");
        return outcome;
    }

    // ---- 第 6 段：结果分派。"提交成功必须有修订"是编排承诺（AT-21
    // 观测点"显式提交产生新修订"）——端口报成功但修订身份无效＝端口
    // 实现违约，如实转 Failed 呈现（不带病报成功，同 T07 清理观测位
    // 纪律）。
    if (execution.relinked && execution.revisionId.isValid()) {
        outcome.result = RelinkOutcome::Result::Relinked;
        outcome.revisionId = execution.revisionId;  // 新修订——AT-21 回传
        return outcome;
    }
    outcome.result = RelinkOutcome::Result::Failed;
    std::string action = execution.action;
    if (action.empty()) {
        // 兜底自诊断（UX-03 三字段永不缺位——端口未给建议时给通用指引）。
        action = "请重试重关联操作；若反复失败请检查外部源可读性与项目"
                 "写权限（当前项目未受影响）";
    }
    outcome.failure = makeRelinkFailure(
        relinkContextText(request, outcome.status),
        execution.cause.empty() ? "重关联未完成（执行端口未报告成功）"
                                : execution.cause,
        std::move(action));
    return outcome;
}

// =====================================================================
// 最近项目管理（PM-10——§7.8；容量/去重/失效标记/移除）
// =====================================================================

RecentProjectsService::RecentProjectsService(std::size_t maxEntries)
    : m_maxEntries(maxEntries)
{
    // 零容量＝调用方契约违约（fail-fast——空列表服务无业务意义；
    // PM-10 冻结容量 1×10，生产装配用缺省值）。
    if (m_maxEntries == 0) {
        throw WorkflowError(
            "RecentProjectsService: 容量上限不得为 0（PM-10 冻结值为 10）");
    }
}

std::vector<RecentProjectEntry> RecentProjectsService::list() const
{
    // 失效项保留（PM-10）：逐项实测位置可用性——"项目位置不可用"＝该
    // 路径当前不是一个可进入的目录（不存在或被替换为普通文件）；这是
    // 位置事实不是项目有效性事实（目录在但 project.json 损坏的判定归
    // 打开协议②步——PA-1 不越权）。环境事实：同列表不同时点可不同。
    std::vector<RecentProjectEntry> snapshot = m_entries;
    std::error_code ec;
    for (RecentProjectEntry& entry : snapshot) {
        entry.locationAvailable =
            std::filesystem::is_directory(entry.canonicalPath, ec) && !ec;
    }
    return snapshot;
}

void RecentProjectsService::record(const std::string& canonicalPath)
{
    // 前置：空路径记录无意义（调用方契约违约——打开成功必然产生非空
    // canonicalPath；空值到达此处是装配缺陷，fail-fast）。
    if (canonicalPath.empty()) {
        throw WorkflowError("RecentProjectsService::record: 空路径");
    }

    // 去重键收敛（唯一规范化点——recentProjectKey：lexically_normal 词法
    // 规范化，不触盘、确定性；大小写收敛由上游规范路径来源保证）。
    const std::filesystem::path key = recentProjectKey(canonicalPath);

    // 去重后置顶（LRU）：已存在→原位摘除再插首；不存在→插首。
    // 线性扫描：容量≤10（PM-10 冻结），扫描成本可忽略——不做哈希索引
    // （KISS：小容量线性序即最简确定性实现）。
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [&key](const RecentProjectEntry& e) {
                           return e.canonicalPath == key;
                       }),
        m_entries.end());

    RecentProjectEntry entry;
    entry.canonicalPath = key;
    // locationAvailable 不在此实测（list 时实时检查——记录时的存在性
    // 是过时事实，持久化恢复后的失效项正是靠 list 实测标记）。
    entry.locationAvailable = true;
    m_entries.insert(m_entries.begin(), std::move(entry));

    // 上限裁剪（PM-10"上限 10"）：LRU 尾部溢出淘汰（最老的先出）；失效
    // 项同样参与容量（"保留"指不因失效自动剔除，不是容量豁免）。
    if (m_entries.size() > m_maxEntries) {
        m_entries.resize(m_maxEntries);
    }
}

void RecentProjectsService::remove(const std::string& canonicalPath)
{
    // 空路径同样 fail-fast（与 record 同一入口契约——remove("") 不是
    // 合法的用户动作词面，到达此处是装配缺陷）。
    if (canonicalPath.empty()) {
        throw WorkflowError("RecentProjectsService::remove: 空路径");
    }

    // 同键收敛匹配移除；不在列表＝幂等无操作（首页呈现刷新与列表变化
    // 的竞态容忍——移除是用户处置动作，不是需要仲裁的状态迁移）。
    const std::filesystem::path key = recentProjectKey(canonicalPath);
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [&key](const RecentProjectEntry& e) {
                           return e.canonicalPath == key;
                       }),
        m_entries.end());
}

std::filesystem::path recentProjectKey(const std::string& canonicalPath)
{
    // 词法规范化（去冗余分隔符与 . / .. 段——同义词面收敛为一个键）；
    // 不做 weakly_canonical（触盘且依赖存在性——失效项（已删除路径）
    // 也必须有稳定键）。UTF-8 窄串 → path：MSVC 下按源/执行字符集解释，
    // 测试面全用 ASCII 词面规避编码歧义（生产面路径来自
    // std::filesystem 原生 API，无窄串转码问题）。
    return std::filesystem::path(canonicalPath).lexically_normal();
}

// =====================================================================
// 无项目首页数据面（PM-10——§7.8；三入口＋项目状态摘要＋入口禁用数据）
// =====================================================================

HomeScreenData buildNoProjectHomeScreen(
    const std::vector<RecentProjectEntry>& recentEntries)
{
    HomeScreenData data;

    // 项目状态摘要（PM-10"与项目状态摘要"——无项目态固定键；用户呈现
    // 词归 ui 文案资源——UX-10"空项目"状态词归 ui，§7.9 分工）。
    data.statusKey = kHomeStatusNoProjectKey;

    // 入口清单（固定序八项——头注释 HomeScreenData 冻结序；禁用集＝
    // PM-10"无项目时禁用七阶段/运行/应用/报告入口（仅留项目菜单）"的
    // 逐项承载——呈现层按 enabled=false 渲染禁用态，数据与呈现两权分立
    // ——D-WF-6/R-WF-4）。
    data.entries = {
        {kHomeEntryNewProject, true},       ///< 三入口之一：新建（向导 PM-01）
        {kHomeEntryOpenProject, true},      ///< 三入口之一：打开（协议 PM-02）
        {kHomeEntryRecentProjects, true},   ///< 三入口之一：最近项目
        {kHomeEntryProjectMenu, true},      ///< "仅留项目菜单"的留侧入口
        {kHomeEntryStageNavigation, false}, ///< 七阶段导航——无项目禁用
        {kHomeEntryRun, false},             ///< 运行入口——无项目禁用
        {kHomeEntryApply, false},           ///< 应用（提交）入口——无项目禁用
        {kHomeEntryReport, false},          ///< 报告入口——无项目禁用
    };

    // 最近项目内容面：服务列表逐项透传（失效项保留——PM-10；可用性
    // 标记沿用服务实测值，本函数不复查——单一事实源）。
    data.recentItems.reserve(recentEntries.size());
    for (const RecentProjectEntry& entry : recentEntries) {
        RecentHomeItem item;
        item.canonicalPath = entry.canonicalPath;
        item.locationAvailable = entry.locationAvailable;
        data.recentItems.push_back(std::move(item));
    }
    return data;
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
