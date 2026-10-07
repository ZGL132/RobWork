/**
 * @file   Lifecycle.cpp
 * @brief  新建项目三步向导编排的实现（O4 子集）——输入校验、实时摘要
 *         组装、词表规范化映射与创建编排（PM-01；units/workflow.md §7.1）。
 *
 * 设计依据见公共头 Lifecycle.hpp 文件头（本实现文件只补充逐段实现口径；
 * 段落注释对应头注释的"编排序 1~5"——两处同步维护，改逻辑必改两处）。
 *
 * 实现纪律（与本单元既有实现同源）：
 *   - 零 Qt（R-3/NFR-MNT-01——本翻译单元在计算库源码集内）；
 *   - 零 modeling/io/业务域头（R-1——模板/导入只经 IDomainInitSubmitter
 *     端口触达；文件系统清理用标准库）；
 *   - 词表与文案键构造集中（NFR-MNT-03——本文件是向导词表的唯一映射点）；
 *   - 错误二分（§10.3）：调用方错误 WorkflowError fail-fast；环境/对端
 *     错误值轨道呈现（failure 三字段——UX-03），零新增稳定码（D-WF-7）。
 */

#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <algorithm>
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

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
