/**
 * @file   PanelStationModel.hpp
 * @brief  工位面板呈现模型（零 Qt）——任务点检查器的字段投影、五姿态规则
 *         联动表单的参数显隐表与来源徽标（卡 §9.8 面板表第 2 行）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（面板组成表第 2 行——"列表＋检查器：位姿
 *     分量约束/容差/三段/姿态规则（五规则联动表单：按 kind 显隐参数——
 *     承接旧 templateParameterVisibilityMask 思路）/等级与启用/来源徽标
 *     （手工/捕获/镜像/模板/导入）"；消费契约＝§9.4 服务＋ui
 *     FormEditCommon（批量粘贴/单位同显/就地错误——UX-05））
 *   - units/requirements.md §4.3（TaskPoint 字段表——投影字段权威）、
 *     §5.3（五姿态规则——kind 与载荷字段对应关系）、§4.3 source 行
 *     （来源标记 UserProvided/ImportMapped＋methodTag）、§5.1（三段进退
 *     轴语义）
 *   - units/ui.md（FormEditCommon.hpp——WP-10-T08 落位的公共编辑规则件；
 *     QuantityFieldSpec/ParamEditSet/IFormEditOutlet——本面板可编辑数量
 *     字段的宿主描述与移交载荷）
 *   - 需求 UX-05（表单批量＋单位同显＋就地错误）、UX-02；任务契约
 *     tasks/foundation/WP-14-T08.json acceptance 1/2
 *
 * 背景说明（零计算逻辑的实现面——acceptance 3）：本头的全部函数是"值→
 * 视图行"的搬运：字段合法性零判定（构造边界在领域服务/编辑器）、单位
 * 换算零参与（可编辑字段交给 ui FormEditCommon 的 ParamEditModel——解析
 * 与换算唯一归 core Units；本头只产 QuantityFieldSpec 描述）、编码解码
 * 零参与。检查器行投影自 TaskPoint 值（工作集权威），来源徽标按来源标记
 * 词表映射（映射即呈现约定，无判定）。
 *
 * ParamEditSet→TaskPoint 的字段回填（applyStationEditSet）：这是"用户
 * 输入值放到哪个字段"的数据装配（编辑差值组装），不是校验/换算——校验
 * 唯一发生在 editor.applyEdit 的域校验链（acceptance 2 的域裁决面）。
 *
 * 线程约束：全部函数纯函数（无共享可变状态），可重入；入参条目仅 UI
 * 线程可变（§3.4）。确定性：行序固定（字段登记序）；同输入同输出。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELSTATIONMODEL_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELSTATIONMODEL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Units.hpp>   // core::UnitToken（QuantityFieldSpec 装配面）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // TaskPoint/WorkRegion/五规则词表
#include <sdurws/ird/ui/FormEditCommon.hpp>  // ui::QuantityFieldSpec/ParamEditSet（WP-10-T08 公共件）

namespace sdurws::ird::requirements {

// =====================================================================
// 检查器字段行（卡 §9.8 第 2 行的行载体）
// =====================================================================

/**
 * @brief 检查器字段行的编辑使能三态（同 modeling FieldEnablement 语义——
 *        独立定义避免跨单元插件私有头依赖，R-2）。
 */
enum class StationFieldEnablement : std::uint8_t {
    Editable,      ///< 可编辑（数量字段经 FormEditCommon 表单；词表字段经下拉）
    ReadOnlyGrey,  ///< 灰显只读（值仍投影显示；只读会话/派生溯源字段）
    Hidden,        ///< 不显示（选中类别无关字段——本面板恒不产出）
};

/**
 * @brief 工位面板检查器的一个字段行（纯值——属性表单的行数据源）。
 *
 * valueText/unitText 两串分离（数值＋单位同显的投影半区，UX-05；数值文本
 * 为确定性 6 位小数裁尾零——locale 无关，NFR-COR-02；单位恒 SI 词面，
 * 零换算）。sourceBadge＝来源徽标（来源标记词表映射——卡 §9.8"来源徽标"
 * 行；无关字段为 nullopt——徽标挂条目级首行）。
 */
struct StationFieldRow {
    std::string fieldKey;   ///< 字段稳定键（呈现与编辑回填的行定位锚——小写连字符）
    std::string label;      ///< 字段标签（工程用语中文——UX-02）
    std::string valueText;  ///< 值文本（确定性文本化；四态缺失＝"未提供"——不伪造数值）
    std::string unitText;   ///< 单位文本（m/rad/s/kg 等 SI 词面；无量纲为空）
    StationFieldEnablement enablement = StationFieldEnablement::Editable;  ///< 使能三态
    std::optional<std::string> sourceBadge;  ///< 来源徽标（仅条目级行携带——词表见 sourceBadgeFor）

    bool operator==(const StationFieldRow& o) const
    {
        return fieldKey == o.fieldKey && label == o.label && valueText == o.valueText
            && unitText == o.unitText && enablement == o.enablement
            && sourceBadge == o.sourceBadge;
    }
    bool operator!=(const StationFieldRow& o) const { return !(*this == o); }
};

/**
 * @brief 条目来源徽标（卡 §9.8"来源徽标（手工/捕获/镜像/模板/导入）"的
 *        词表映射——映射即呈现约定，零判定）。
 *
 * 判序（首中即返——来源互斥事实的投影序）：
 *   ① importProvenance 在场 → "导入"（I-REQ-8：溯源字段是导入事实）；
 *   ② generation 在场且 generatorId=="mirror" → "镜像"；
 *   ③ generation 在场且 generatorId 以 "template:" 起 → "模板"；
 *   ④ generation 在场且 generatorId 以 "array:" 起 → "阵列"（同族批次
 *     徽标——卡面"镜像/模板"的批次语义延伸，generatorId 词表见
 *     TemplateArray.hpp 类注）；
 *   ⑤ source.methodTag=="captured-tcp" → "捕获"（§5.1 来源标记行——
 *     L-R7 捕获产物自证标记）；
 *   ⑥ 其余 → "手工"（UserProvided 无特殊标记——手工输入缺省）。
 *
 * @param point [in] 任务点条目（只读）
 * @return 徽标文本（上述六值之一；纯函数确定性）
 */
std::string sourceBadgeFor(const TaskPoint& point);

/**
 * @brief 投影工位面板检查器字段行（§9.8 第 2 行——位姿分量约束/容差/
 *        三段/姿态规则/等级与启用）。
 *
 * 字段面（行序＝登记序，确定性）：
 *   - 条目级行：名称/等级/启用/来源（来源行带 sourceBadge）；
 *   - 位姿分量约束：六分量受约束掩码逐行（x/y/z/roll/pitch/yaw——
 *     "受约束/自由"事实直投，§4.3 constrainedDof）；
 *   - 容差两行：位置容差 m、姿态容差 rad（I-REQ-5 要求值——直投）；
 *   - 三段三行：接近/作业/撤离（§5.1——启用＋轴＋距离 m 直投；作业段
 *     enabled 恒 true）；
 *   - 姿态规则：规则种类行＋按 kind 显隐的参数行（visibility 表见
 *     orientationRuleParamKeys——联动表单的投影半区）；
 *   - 顺序键/备注（可选字段——未设＝"未设"占位）。
 *
 * @param point    [in] 任务点条目（工作集权威值——只读）
 * @param writable [in] 会话可写性（false→Editable 行降级灰显——L-R12 只读
 *                  门控的行半区；门控映射与 modeling applyReadOnlyGate 同源）
 * @return 检查器行序列（纯投影——不缓存）
 *
 * 纯函数；确定性；不抛。
 */
std::vector<StationFieldRow> stationFieldsFor(const TaskPoint& point, bool writable);

// =====================================================================
// 五规则联动表单（§9.8"按 kind 显隐参数"的参数键词表）
// =====================================================================

/**
 * @brief 姿态规则按 kind 显隐的参数键清单（联动表单的显隐表——"承接旧
 *        templateParameterVisibilityMask 思路"的落位：表即数据，显隐即
 *        清单成员判定，零逻辑分支）。
 *
 * 参数键词表（与 applyStationEditSet 的回填键一致——单一词表两处消费）：
 *   - Fixed          → {"fixed-rpy-r", "fixed-rpy-p", "fixed-rpy-y"}（rad）
 *   - AlignFrame     → {"target-frame"}（引用——非数量字段）
 *   - AlignGeometryNormal → {"target-scene", "feature", "invert-normal"}
 *   - PointAtTarget  → {"target-point-x", "target-point-y", "target-point-z"}（m）
 *   - ToolRollFree   → {"roll-min", "roll-max"}（rad）
 *
 * @param kind [in] 姿态规则种类（五值词表）
 * @return 该种类可见的参数键（卡 §5.3 载荷字段对照——纯数据表）
 *
 * 纯函数；确定性。
 */
std::vector<std::string> orientationRuleParamKeys(OrientationRuleKind kind);

/**
 * @brief 工位检查器可编辑数量字段的宿主描述集（ui FormEditCommon 装配面
 *        ——批量粘贴/单位同显/就地错误的公共规则件输入，UX-05）。
 *
 * 字段集＝检查器中的数量编辑面：容差两字段＋三段距离＋Fixed 欧拉角＋
 * PointAtTarget 目标点＋ToolRollFree 区间。SI 单位锚定（m/rad——core
 * UnitToken::find 取得），bounds 按 I-REQ-5/校验面登记（容差 >0；距离 >0；
 * 角度不设界——规则参数合法性归域校验链，呈现层不做范围暗示）。
 * makeQuantityFieldSpec 装配期 fail-fast 保证单位一致性（WP-10-T08 契约）。
 *
 * @return 字段描述集（键＝orientationRuleParamKeys/检查器行 fieldKey 同词表；
 *          注册序＝本函数实现序——呈现稳定序）
 *
 * 纯函数；确定性；不抛（makeQuantityFieldSpec 的装配违约除外——那是
 * 实现缺陷 fail-fast）。
 */
std::vector<ui::QuantityFieldSpec> stationQuantitySpecs();

/**
 * @brief 把确认后的表单修改集回填为任务点编辑差值（ParamEditSet→TaskPoint
 *        字段装配——acceptance 2"控件提交→applyEdit"的值组装半区）。
 *
 * 装配规则（数据搬运——零校验零换算）：按 fieldKey 词表把 SI 真值写入
 * base 拷贝的对应字段；身份/名称/引用/来源等非表单字段原样保留（检查器
 * 表单只覆盖数量编辑面）。返回的条目交 editor.applyEdit（upsert）——
 * 合法性由域校验链裁决（拒绝时工作集不变是编辑器域内强保证）。
 *
 * @param base   [in] 当前条目值（工作集权威——只读）
 * @param edits  [in] 表单确认的修改集（SI 真值；ui ParamEditModel 产出）
 * @param known  [out] 回填命中的字段键（注册序；空修改集→空——调用方
 *               据此跳过 applyEdit，不产生空编辑）
 * @return 回填后的条目候选（base 拷贝＋已命中字段新值）
 *
 * @throws std::invalid_argument edits 携带词表外键（表单模型与回填词表
 *               漂移＝实现缺陷——fail-fast 不静默丢弃）
 *
 * 纯函数；确定性。
 */
TaskPoint applyStationEditSet(const TaskPoint& base, const ui::ParamEditSet& edits,
                              std::vector<std::string>& known);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELSTATIONMODEL_HPP
