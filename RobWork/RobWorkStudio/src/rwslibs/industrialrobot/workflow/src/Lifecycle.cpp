/**
 * @file   Lifecycle.cpp
 * @brief  生命周期入口流程编排的实现——新建项目三步向导（PM-01）＋打开
 *         协议（PM-02 五步的 workflow 面）＋最近项目管理（PM-10）＋无
 *         项目首页数据面（PM-10）。
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

#include <algorithm>
#include <cctype>
#include <exception>
#include <system_error>
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
 * @brief 从对端 detail 提取失败定位文件（PM-02"失败显示具体文件"）。
 *
 * project 侧打开失败 detail 是机器可读键值串（"project/<域>: <原因>
 * key=value ..."），定位键为 "path=<路径>" 或 "file=<路径>"。提取规则：
 * 取**最后一次**出现的定位键（失败链的最后落点最具体），值域到串尾或
 * 下一个 " <词>=" 键边界为止。这是**尽力呈现**而非协议解析：找不到定位
 * 键返回空串（调用方回退目标路径词面——file 恒非空的呈现承诺不变）；
 * 含空格路径仅在下一条键存在时可能被截断（cause 全文始终透传兜底）。
 *
 * @param detail [in] 对端 detail 原文（StoreError::what()）
 * @return 定位文件串（空＝detail 无定位键）
 */
std::string extractDetailLocation(const std::string& detail)
{
    // 定位键扫描：两个候选键取更靠后者（最后落点最具体——如 manifest
    // 解析失败的 "path=<清单文件> detail=..." 链尾）。
    const std::size_t pathKey = detail.rfind(" path=");
    const std::size_t fileKey = detail.rfind(" file=");
    std::size_t keyPos = std::string::npos;
    if (pathKey != std::string::npos
        && (fileKey == std::string::npos || pathKey > fileKey)) {
        keyPos = pathKey;
    } else {
        keyPos = fileKey;  // 两键都缺时为 npos——下方统一短路返回空
    }
    if (keyPos == std::string::npos) {
        return {};
    }
    const std::size_t valueStart = keyPos + 6;  // 跳过 " path="/" file="（6 字节）

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
