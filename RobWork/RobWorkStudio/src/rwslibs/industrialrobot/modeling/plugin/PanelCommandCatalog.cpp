/**
 * @file   PanelCommandCatalog.cpp
 * @brief  建模域命令登记目录实现——十条 CommandId 与装配值（卡 §9.7.3）。
 *
 * 设计依据：units/modeling.md §9.7.3 表逐行（登记序＝表行序）、ui.md
 * §10.9/§11.1/§11.2、§6.5、§7.3（SA-16）；契约 WP-13-T15 acceptance 4。
 * 实现纪律：清单即数据（零逻辑）；行序确定性＝卡表行序；命令文案键按
 * ui §3.5 键约定派生（值解析归 UiText——键即契约）。
 */

#include "PanelCommandCatalog.hpp"

#include <sdurws/ird/modeling/CommandHandlers.hpp>  // kCmdApply* token（project 侧无点词表——命名空间分离守卫的另一半）

namespace sdurws::ird::modeling {

// =====================================================================
// 十条域命令（行序＝卡 §9.7.3 表行序——登记序确定性）
// =====================================================================

std::vector<ui::CommandDescriptor> modelingDomainCommands()
{
    // 行构造辅助：公共要素（ownerUnit/category/bindable/无默认快捷键）在
    // 此集中；行内只写差异（id/scope/readOnlyAllowed/menuPath）——清单的
    // 单一事实来源＝卡 §9.7.3 表，本函数是其逐一值化。
    auto make = [](const char* id, ui::CommandScope scope, bool readOnlyAllowed,
                   const char* menuPath) {
        ui::CommandDescriptor d;
        d.id = ui::CommandId(id);  // CommandId＝字符串强别名（点分小写——§7.2 句法校验归注册表）
        d.ownerUnit = "modeling";  // 白名单 token（§11.1 八 token 词表）
        d.titleKey = ui::TextKey(std::string("cmd.") + id + ".title");  // §3.5 键约定
        d.scope = scope;
        d.readOnlyAllowed = readOnlyAllowed;  // 卡表值——L-7 只读门控的命令半区
        d.bindable = true;                    // 可经 HotkeyBindingTable 用户级绑定（SA-16——默认零绑定）
        d.menuPath = menuPath;                // 面板/菜单分组（域工具区"建模"分组）
        return d;
    };

    return {
        // ① 模板新建（TemplateFactory→草稿）——readOnlyAllowed=false。
        make("modeling.new-from-template", ui::CommandScope::Project, false, "建模/模板"),
        // ② URDF 导入向导（io 预检→§6.1 管线）。
        make("modeling.import-urdf", ui::CommandScope::Project, false, "建模/导入"),
        // ③ Xacro 导入向导（与 URDF 两条独立命令——卡面明示不合并）。
        make("modeling.import-xacro", ui::CommandScope::Project, false, "建模/导入"),
        // ④ DH↔显式权威切换流（L-9——Exact/ExactNonUnique 才放行）。
        make("modeling.switch-authority", ui::CommandScope::Project, false, "建模/权威"),
        // ⑤ 选中连杆批量物性估算（MDL-05——估算命令面）。
        make("modeling.estimate-properties", ui::CommandScope::Project, false, "建模/物性"),
        // ⑥ 连杆占位几何生成辅助（§5.2——导入资源缺失的补救面）。
        make("modeling.generate-placeholder-geometry", ui::CommandScope::Project, false,
             "建模/几何"),
        // ⑦ 与基线 Model Diff 查看（只读对比——readOnlyAllowed=true）。
        make("modeling.diff-baseline", ui::CommandScope::Project, true, "建模/对比"),
        // ⑧ MDL-20 规范包导出（只读导出——readOnlyAllowed=true）。
        make("modeling.export-package", ui::CommandScope::Project, true, "建模/规范包"),
        // ⑨ MDL-20 规范包导入（写路径——与⑧两条独立命令，readOnlyAllowed=false）。
        make("modeling.import-package", ui::CommandScope::Project, false, "建模/规范包"),
        // ⑩ 复位 Home/Zero（会话姿态——KIN-06 语义零修订；scope=Session、
        // readOnlyAllowed=true——只读会话可用的会话操作，AT-04）。
        make("modeling.reset-home-zero", ui::CommandScope::Session, true, "建模/姿态"),
    };
}

std::vector<std::string> modelingProjectCommandTokens()
{
    // project commandType 词表（无点语法——project §4.4.4 ^[a-z0-9-]{3,64}）：
    // 建模域已落位的命令处理器族 token（CommandHandlers.hpp 冻结常量直用
    // ——不私写字面量，单一权威）。
    return {std::string(kCmdApplyRobotDesign), std::string(kCmdApplyToolDefinition),
            std::string(kCmdApplySceneObjects), std::string(kCmdApplyNamedPoses)};
}

// =====================================================================
// 装配描述符
// =====================================================================

PanelRegistrationRecord modelingPanelRegistration()
{
    PanelRegistrationRecord rec;
    rec.pluginId = "modeling";                 // §11.1 静态白名单 token
    rec.titleKey = ui::TextKey("stage.modeling.title");  // 阶段面板标题键（§3.5）
    rec.stage = ui::StageId::Modeling;         // 挂位阶段（卡 §9.7.1 域工具区行）
    rec.capabilities.providesStagePanel = true;          // 建模结构树/属性区/工具区/就绪条/预览
    rec.capabilities.providesReadonlyProjection = true;  // §6.5 汇聚源（就绪投影）
    rec.capabilities.registersCommands = true;           // 十条域命令（§9.7.3）
    rec.advanced = false;                                // 主面板（非 UX-04 高级面板）
    return rec;
}

// =====================================================================
// 域就绪投影
// =====================================================================

std::vector<ui::DomainReadinessItem> modelingReadinessProjection(const ModelReadinessReport& report)
{
    ui::DomainReadinessItem item;
    item.domainKey = "modeling";  // 域注册键（§6.5 词表）

    // verdict/inputComplete：就绪三态→汇聚词表直投（零判定——映射即呈现
    // 约定：Ready 族＝可行＋输入完整；NotReady＝输入不完整——REQ-06）。
    switch (report.status) {
    case ReadinessStatus::Ready:
        item.verdict = core::EngineeringStatus::Feasible;
        item.inputComplete = true;
        break;
    case ReadinessStatus::ReadyWithNotes:
        // 有警告/待确认/缺项预告——输入已完整（无 Blocking），结论非"可行"
        // （呈现中性 NotApplicable＝待提交后 prepare 现场判定——§8.2 结果
        // 流向：本投影不替代命令内重估，防 TOCTOU）。
        item.verdict = core::EngineeringStatus::NotApplicable;
        item.inputComplete = true;
        break;
    case ReadinessStatus::NotReady:
        item.verdict = core::EngineeringStatus::DataInsufficient;  // 输入不完整（缺项/阻断定位在条上）
        item.inputComplete = false;
        break;
    }

    // 缺项键：呈现级 notes 的层 token 派生（"missing.L<n>"——UX-02：键与
    // 局部名，零哈希/内部标识；明细全文在就绪条，不重复入键）。
    for (const ReadinessNote& n : report.notes) {
        if (!n.blocking) {
            item.missingItemKeys.push_back(ui::TextKey("missing."
                                                       + std::string(readinessLayerToken(n.layer))));
        }
    }

    // hasActiveTask：建模域无在途任务呈现面（任务呈现归 ui——N-11）。
    item.hasActiveTask = false;

    return {item};
}

// =====================================================================
// L-7 只读门控
// =====================================================================

std::vector<PropertyFieldRow> applyReadOnlyGate(std::vector<PropertyFieldRow> rows, bool writable)
{
    // 只读会话（writable=false）：可编辑行一律降级灰显（L-7"编辑控件禁用"）；
    // 已灰显/隐藏行不变（灰显不因可写性复活——DH 派生只读与只读会话是
    // 两个独立灰显源，§7.2×L-7 正交）。
    if (!writable) {
        for (PropertyFieldRow& r : rows) {
            if (r.enablement == FieldEnablement::Editable) {
                r.enablement = FieldEnablement::ReadOnlyGrey;
            }
        }
    }
    return rows;
}

}  // namespace sdurws::ird::modeling
