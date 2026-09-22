/**
 * @file   Template.cpp
 * @brief  模板创建与参数化编辑的实现——清单/草稿创建（§5.1/§9.4.2）、
 *         六轴默认参数表 T-MDL-1、变更摘要、创建入口守卫、逐轴编辑流与
 *         几何生成辅助（§5.2）。
 *
 * 设计依据：units/modeling.md §5.1/§5.2/§6.4/§9.4.2、§4.3/§4.10（值模型
 * 与不变量）；需求 MDL-01/MDL-04/MDL-22、P-03/O-27（七轴仅登记不启用）；
 * 任务契约 tasks/foundation/WP-13-T07.json acceptance 1~5。
 *
 * 实现总纪律：
 *   - 全部函数为纯函数（§3.4 总约定 1）——无共享可变状态、不读时钟/
 *     环境/locale/文件系统；同输入→同输出（NFR-COR-02）。
 *   - 冒烟 header-only 纪律（T03 落位起）：不调用 Rotation3D::identity()/
 *     rw::math::RPY 等框架外联符号——旋转矩阵一律逐元素解析式构造
 *     （Import.cpp rpyToRotation 同款；见 makeLinkPlaceholderCylinder）。
 *   - 错误语义（AGENTS）：调用方契约违约 fail-fast（invalid_argument/
 *     logic_error）；正常业务拒绝走值面（TemplateOutcome/JointEditError）
 *     ——§9.4 前言"非异常出口"。
 */

#include <sdurws/ird/modeling/Template.hpp>

#include <sdurws/ird/core/Digest.hpp>  // ContentDigester——确定性临时句柄派生（SHA-256）
#include <sdurws/ird/modeling/DiagCodes.hpp>  // kMdlTemplateDisabled/kMdlImportTemplateRange——码常量唯一书写点
#include <sdurws/ird/modeling/PropertyEstimation.hpp>  // defaultMaterialDensity——材料密度默认表（单一权威）

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::modeling {

namespace {

// ---------------------------------------------------------------------
// 模板登记清单（§5.1 表——listTemplates 与 createDraft 共用的单一数据源；
// 行序＝§5.1 表行序——确定性）
// ---------------------------------------------------------------------

/**
 * @brief §5.1 模板清单三行（单一数据源——清单页与创建入口读同一份表，
 *        不存在第二份登记）。
 *
 * 注记文本（note 列）逐字对齐卡 §5.1 表第 5 列语义；generic-7r 的
 * enabled=false 即 O-27 处置（P-03 冻结前仅登记不启用——七轴工程数值
 * 未冻结，本单元任何路径不使用、不猜测该数值）。
 */
std::vector<TemplateDescriptor> templateCatalog()
{
    std::vector<TemplateDescriptor> catalog;
    catalog.reserve(3);

    // 行 1：generic-6r——R1 可用；T-MDL-1 为设计默认值（D-MDL-7），
    // 数值锁定随 WP-13-T16 mdl-template-6r 黄金数据集。
    TemplateDescriptor sixAxis;
    sixAxis.templateId = TemplateId{kTemplateIdGeneric6R};
    sixAxis.displayName = "六轴通用串联";
    sixAxis.axisCount = 6;
    sixAxis.authority = AuthorityMode::Explicit;
    sixAxis.installationPresets = {runtime::InstallationPresetToken::Ground,
                                   runtime::InstallationPresetToken::Inverted,
                                   runtime::InstallationPresetToken::Wall};
    sixAxis.enabled = true;
    sixAxis.note =
        "R1 可用；默认参数表 T-MDL-1 为设计默认值（D-MDL-7，非上游冻结"
        "需求值），随 WP-13-T16 mdl-template-6r 黄金数据集数值锁定";

    // 行 2：generic-7r——登记不启用（P-03 七轴模板工程数值未冻结，附录 C；
    // enabled=false，创建入口阻止并提示——AT-20 向导语义建模侧）。
    TemplateDescriptor sevenAxis;
    sevenAxis.templateId = TemplateId{kTemplateIdGeneric7R};
    sevenAxis.displayName = "七轴串联";
    sevenAxis.axisCount = 7;
    // MDL-02：七轴默认且 S-R-S/带偏置拓扑锁定显式——权威模式登记为
    // Explicit（清单呈现语义；数值冻结前不建链，见 createDraft 步②）。
    sevenAxis.authority = AuthorityMode::Explicit;
    sevenAxis.installationPresets = {runtime::InstallationPresetToken::Ground,
                                     runtime::InstallationPresetToken::Inverted,
                                     runtime::InstallationPresetToken::Wall};
    sevenAxis.enabled = false;  // P-03 冻结门——O-27 处置口径
    sevenAxis.note =
        "登记不启用：P-03 七轴模板工程数值未冻结（DTB §4.3 O-27）；数值"
        "冻结并登记后方可启用，冻结前创建入口阻止";

    // 行 3：custom-chain——R1 可用；用户逐轴定义（类型/轴线/零位/限位/
    // 速度/加速度）。axisCount=nullopt＝逐轴（清单面不承诺固定轴数）。
    TemplateDescriptor customChain;
    customChain.templateId = TemplateId{kTemplateIdCustomChain};
    customChain.displayName = "自定义链";
    customChain.axisCount = std::nullopt;
    customChain.authority = AuthorityMode::Explicit;
    customChain.installationPresets = {runtime::InstallationPresetToken::Ground,
                                       runtime::InstallationPresetToken::Inverted,
                                       runtime::InstallationPresetToken::Wall};
    customChain.enabled = true;
    customChain.note = "R1 可用；逐轴定义（类型/轴线/零位/限位/速度/加速度）";

    catalog.push_back(std::move(sixAxis));
    catalog.push_back(std::move(sevenAxis));
    catalog.push_back(std::move(customChain));
    return catalog;
}

// ---------------------------------------------------------------------
// 确定性临时句柄（§5.2——内存编辑态先用临时句柄，提交时回填）
// ---------------------------------------------------------------------

/**
 * @brief 由稳定键派生草稿对象的临时 ObjectId（确定性——同键同句柄）。
 *
 * 派生规则：对稳定键（"ird/modeling/template-draft/<templateId>/<类别>/
 * <序>"）取 SHA-256，摘要前 16 字节即 ObjectId 字节（T05 导入路径
 * §14.6 v0.6 ②i 同款纪律：确定性散列派生 128 位、不经随机源；真实
 * ObjectId 仍由命令 prepare 阶段经 HandlerContext.objectId() 分配回填
 * ——PA-1，本句柄不外泄为持久身份）。全零摘要命中（保留值——core
 * Identity 纪律禁止）在 SHA-256 下实际不可达，此处置尾字节 1 作确定性
 * 兜底（同键仍同句柄，保留值不出现）。
 *
 * 纯函数；线程安全；确定性。
 */
core::ObjectId deriveDraftObjectId(const std::string& stableKey)
{
    core::ContentDigester digester;
    digester.update(stableKey.data(), stableKey.size());
    const core::Digest256 digest = digester.finalize();

    core::ObjectId id;
    for (std::size_t i = 0; i < 16; ++i) {
        id.bytes[i] = digest[i];  // 摘要前 16 字节（确定性——不用随机源）
    }
    if (id.bytes == core::ObjectId{}.bytes) {
        id.bytes[15] = 1;  // 保留值（全零）兜底——确定性不改（实际不可达）
    }
    return id;
}

/**
 * @brief 模板来源标记（§5.1"来源=UserProvided/Template"的统一落点）：
 *        kind=UserProvided（用户选定模板即其基线选择）＋methodTag
 *        "template/<templateId>"（模板轨迹区分——methodTag 语法
 *        [a-z0-9./_-] 合规）。
 */
core::ValueProvenance templateProvenance(const TemplateId& id)
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       "template/" + id);
}

/**
 * @brief localName 合法性（§9.4.2 @pre"非空且合法字符集"；字符集
 *        [A-Za-z0-9_.-]——§4.3-A 行原文）。
 *
 * 纯函数；确定性。
 */
bool isValidLocalName(std::string_view name)
{
    if (name.empty()) { return false; }  // 空串＝无名称语义，直接非法
    for (const char c : name) {
        const bool legal = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                           || (c >= '0' && c <= '9') || c == '_' || c == '.'
                           || c == '-';
        if (!legal) { return false; }
    }
    return true;
}

/**
 * @brief Vector3D 全分量有限（I-MDL-3 的输入面检查）。
 */
bool allFinite(const rw::math::Vector3D<double>& v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

/**
 * @brief 草稿连杆种子（§5.1"连杆几何默认"句的材料半句——钢；几何半句的
 *        schema 边界见 makeLinkPlaceholderCylinder 注，种子不预填几何）。
 *
 * 材料密度取 §5.3 材料密度默认表（单一权威——不写第二处 7850 字面量；
 * NFR-MNT-07 单源精神）；来源标记随模板轨迹。物性数值（mass/com/inertia）
 * 全部 NotProvided——缺失不触发断言、走 DataInsufficient 降级（MDL-06/
 * V15-01），模板不猜测物性（NFR-COR-03）。
 */
LinkEntry makeSeedLink(const std::string& stableKey, const std::string& localName,
                       const core::ValueProvenance& provenance)
{
    LinkEntry link;
    link.objectId = deriveDraftObjectId(stableKey);
    link.localName = localName;

    const std::optional<double> steelDensity = defaultMaterialDensity("steel");
    if (!steelDensity.has_value()) {
        // 材料表键"steel"是本单元 §5.3 表内登记键——查不到＝表被改坏的
        // 实现缺陷，fail-fast（不静默产出无材料种子——§5.1 材料默认句失守）
        throw std::logic_error("modeling/template/material-table: 默认材料表"
                               "缺少登记键 steel（实现缺陷）");
    }
    MaterialRef steel;
    steel.materialId = "steel";
    steel.density = core::SourcedValue<double>::provided(*steelDensity, provenance);
    link.body.material = std::move(steel);
    return link;
}

/**
 * @brief 单字段编辑的实现内核（不触 changes——变更记录由外层按"一次调用
 *        一条"的粒度追加；批量变体逐行复用本内核）。
 *
 * 校验规则与拒绝码见 applyJointFieldEdit 头注（同一段契约——本内核是
 * 其机械执行，接受时已提交 design 变更）。
 */
std::optional<JointEditError> editJointFieldImpl(RobotDesign& design,
                                                 std::size_t jointIndex,
                                                 JointEditField field,
                                                 const JointEditValue& value)
{
    const JointEntry& current = design.joints[jointIndex];
    const std::string subject = "joints[" + std::to_string(jointIndex) + "]";

    switch (field) {
    case JointEditField::Axis: {
        // 备择匹配由外层 fail-fast 保证——本内核按契约直接取用。
        const auto& axis = std::get<rw::math::Vector3D<double>>(value);
        // ③a 权威守卫（C-1/C-2 单一判定——RobotDesign.hpp authorityEditGuard；
        // StandardDH 态 axis 为派生只读，编辑即双真值写入）。
        if (authorityEditGuard(design.authority, AuthorityLockedField::Axis)
                .has_value()) {
            return JointEditError{JointEditErrorCode::AuthorityLocked,
                                  subject + "：StandardDH 权威态下 axis 为派生"
                                            "只读（C-1）——切换权威模式需经"
                                            "转换判定（§7.6）"};
        }
        // ③b 有限性（I-MDL-3：NaN/Inf 非法，不静默置 0）。
        if (!allFinite(axis)) {
            return JointEditError{JointEditErrorCode::ValueNotFinite,
                                  subject + ".axis：含非有限分量（NaN/Inf）"};
        }
        // ③c 可归一化（I-MDL-6 同口径：零轴/次正规极小轴拒绝——稳定范数
        // 判定与 checkInvariants 一致）。
        const double m = std::max({std::abs(axis[0]), std::abs(axis[1]),
                                   std::abs(axis[2])});
        if (m == 0.0 || m < std::numeric_limits<double>::min()) {
            return JointEditError{JointEditErrorCode::AxisNotNormalizable,
                                  subject + ".axis：零轴/次正规轴（I-MDL-6）"};
        }
        // 提交：用户输入覆盖（UserProvided——MDL-09 显式权威一等字段；
        // 不静默归一化——单位向量语义在消费侧解读，I-MDL-6 只拒零/次正规）。
        design.joints[jointIndex].axis =
            core::SourcedValue<rw::math::Vector3D<double>>::provided(axis,
                core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        return std::nullopt;
    }
    case JointEditField::ZeroOffset: {
        // ④ 零位偏置有限性（rad，移动关节 m——两态均权威）。
        const double zeroOffset = std::get<double>(value);
        if (!std::isfinite(zeroOffset)) {
            return JointEditError{JointEditErrorCode::ValueNotFinite,
                                  subject + ".zeroOffset：非有限值（NaN/Inf）"};
        }
        design.joints[jointIndex].zeroOffset = zeroOffset;
        return std::nullopt;
    }
    case JointEditField::Bounds: {
        // ⑤ 限位：Continuous 拒绝（I-MDL-4 后半——不静默清除已有
        // NotApplicable）、有限性、qmin<qmax。
        const auto& bounds = std::get<JointLimits>(value);
        if (current.type == JointType::Continuous) {
            return JointEditError{JointEditErrorCode::TypeBoundsConflict,
                                  subject + ".bounds：Continuous 关节限位应为"
                                            " NotApplicable（I-MDL-4）——不得"
                                            "提供有限限位"};
        }
        if (!std::isfinite(bounds.first) || !std::isfinite(bounds.second)) {
            return JointEditError{JointEditErrorCode::ValueNotFinite,
                                  subject + ".bounds：限位含非有限值"};
        }
        if (!(bounds.first < bounds.second)) {
            return JointEditError{JointEditErrorCode::LimitIntervalInvalid,
                                  subject + ".bounds：qmin≥qmax（I-MDL-4 前半）"};
        }
        design.joints[jointIndex].bounds =
            core::SourcedValue<JointLimits>::provided(bounds,
                core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        return std::nullopt;
    }
    case JointEditField::Type: {
        // ⑥ 类型：与既有 bound/workingRange 的非法组合拒绝（§7.3 组合族
        // ——不静默清除既有值，NFR-COR-03；转换后的限位缺失面归就绪校验
        // 待确认——导入路径同口径）。
        const JointType newType = std::get<JointType>(value);
        if (newType == JointType::Continuous
            && current.bounds.state() == core::FieldState::Provided) {
            return JointEditError{JointEditErrorCode::TypeBoundsConflict,
                                  subject + "：改为 Continuous 须先清除已提供"
                                            "限位（I-MDL-4：Continuous＝"
                                            "NotApplicable）——本编辑流不静默"
                                            "清除"};
        }
        if (newType != JointType::Continuous
            && current.workingRange.state() == core::FieldState::Provided) {
            return JointEditError{JointEditErrorCode::TypeBoundsConflict,
                                  subject + "：改为非 Continuous 与已提供"
                                            " workingRange 冲突（§7.3："
                                            "workingRange 仅 Continuous）"};
        }
        design.joints[jointIndex].type = newType;
        return std::nullopt;
    }
    }
    // 枚举全覆盖，不达此处（switch 无 default——新枚举值漏处理编译器告警）。
    return JointEditError{JointEditErrorCode::ValueNotFinite, "unreachable"};
}

/**
 * @brief 追加一条字段级变更记录（编辑序 append-only——ModelingChangeRecord 注）。
 */
void appendChangeRecord(ModelingWorkingSet& ws, std::size_t jointIndex,
                        JointEditField field)
{
    ModelingChangeRecord record;
    record.subject = "joints[" + std::to_string(jointIndex) + "]";
    // 摘要为人读中文（§9.4.1）；不含数值字面（数值在 design 内——摘要不
    // 引入 locale 相关格式化，确定性 NFR-COR-02）。
    switch (field) {
    case JointEditField::Type:
        record.summary = "修改关节类型为「";
        record.summary += jointTypeToken(ws.design.joints[jointIndex].type);
        record.summary += "」";
        break;
    case JointEditField::Axis:
        record.summary = "修改轴线（Explicit 权威一等字段——MDL-09；改名/改轴"
                         "属设计变更，下游需重算）";
        break;
    case JointEditField::ZeroOffset:
        record.summary = "修改零位偏置（转动 rad／移动 m——零位语义不随权威模式"
                         "切换）";
        break;
    case JointEditField::Bounds:
        record.summary = "修改限位 [qmin,qmax]（转动 rad／移动 m）";
        break;
    }
    ws.changes.push_back(std::move(record));
}

}  // namespace

// =====================================================================
// 六轴默认参数表 T-MDL-1（§5.1——D-MDL-7 设计默认值）
// =====================================================================

std::array<SixAxisJointSpec, 6> sixAxisTemplateDefaults()
{
    // π 常量：double 精度圆周率（字面量换算到最近 double——与 M_PI 同值；
    // 本单元不引 <cmath> 平台宏差异，固定字面量即固定表值，NFR-COR-02）。
    constexpr double kPi = 3.14159265358979323846;
    const double kPiHalf = kPi / 2.0;
    const double kTwoPi = 2.0 * kPi;
    const double kFourPi = 4.0 * kPi;

    // 表 T-MDL-1 逐行抄写（行序＝表行序＝链序；单位 rad／rad·s⁻¹／rad·s⁻²；
    // axis 为连杆系下单位轴。J6 行程 [−2π,+2π]＝4π 阈值边界（附录 D 第 11
    // 项，含于合规侧）——阈值常量归 policy（ARC-05：本地不设第二 4π 常量）。
    return {SixAxisJointSpec{JointType::Revolute,
                             rw::math::Vector3D<double>(0.0, 0.0, 1.0),
                             0.0, JointLimits{-kPi, kPi}, kPi, 2.0 * kPi},
            SixAxisJointSpec{JointType::Revolute,
                             rw::math::Vector3D<double>(0.0, 1.0, 0.0),
                             0.0, JointLimits{-kPiHalf, kPiHalf}, kPi, 2.0 * kPi},
            SixAxisJointSpec{JointType::Revolute,
                             rw::math::Vector3D<double>(0.0, 1.0, 0.0),
                             0.0, JointLimits{-kPi, kPiHalf}, kPi, 2.0 * kPi},
            SixAxisJointSpec{JointType::Revolute,
                             rw::math::Vector3D<double>(1.0, 0.0, 0.0),
                             0.0, JointLimits{-kPi, kPi}, kTwoPi, kFourPi},
            SixAxisJointSpec{JointType::Revolute,
                             rw::math::Vector3D<double>(0.0, 1.0, 0.0),
                             0.0, JointLimits{-kPiHalf, kPiHalf}, kTwoPi, kFourPi},
            SixAxisJointSpec{JointType::Revolute,
                             rw::math::Vector3D<double>(1.0, 0.0, 0.0),
                             0.0, JointLimits{-kTwoPi, kTwoPi}, kTwoPi, kFourPi}};
}

// =====================================================================
// 变更摘要（§9.4.1 buildChangeSummary 的域级实现）
// =====================================================================

std::string buildChangeSummary(const ModelingWorkingSet& ws)
{
    // 确定性格式："[序号] subject：summary"每记录一行（序号从 1 起、记录
    // 序＝编辑发生序）；无变更返回空串（呈现决策归调用方）。
    std::string summary;
    for (std::size_t i = 0; i < ws.changes.size(); ++i) {
        summary += "[";
        summary += std::to_string(i + 1);
        summary += "] ";
        summary += ws.changes[i].subject;
        summary += "：";
        summary += ws.changes[i].summary;
        if (i + 1 < ws.changes.size()) { summary += '\n'; }
    }
    return summary;
}

// =====================================================================
// 模板工厂（§9.4.2）
// =====================================================================

std::vector<TemplateDescriptor> RobotDesignTemplateFactory::listTemplates() const
{
    // 清单序＝§5.1 表行序（确定性）；每次返回新清单（纯值，调用方所有）。
    return templateCatalog();
}

TemplateOutcome RobotDesignTemplateFactory::createDraft(
    const TemplateId& id, runtime::InstallationPresetToken preset,
    std::string_view localName, std::vector<core::DiagnosticRecord>& diags) const
{
    // ---- 步① templateId 存在性（@pre——清单外 id＝调用方契约违约）----
    const std::vector<TemplateDescriptor> catalog = templateCatalog();
    const TemplateDescriptor* descriptor = nullptr;
    for (const TemplateDescriptor& entry : catalog) {
        if (entry.templateId == id) { descriptor = &entry; break; }
    }
    if (descriptor == nullptr) {
        throw std::invalid_argument("modeling/template/unknown-id: 模板登记 id"
                                    "不在清单内（须来自 listTemplates()）: "
                                    + id);
    }

    // ---- 步② enabled 门（P-03/O-27——先于输入校验：模板可用性是创建
    //      入口的首要闸；拒绝不静默替换为六轴、无半成品）----
    if (!descriptor->enabled) {
        // 提示诊断（§9.4.2 @错误 行"附定位诊断"——码已注册 T07 行，禁字符串
        // 拼码）。创建入口尚无对象——subject 为空 optional（无锚可挂）；
        // cause 以 paramSchema 键值承载（template-id/freeze-gate——T07 行）。
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kMdlTemplateDisabled), std::nullopt, std::nullopt,
            std::nullopt, "template-entry:" + id,
            "template-id=" + id + " freeze-gate=P-03",
            "选择已启用模板（如 generic-6r）；七轴模板须待 P-03 数值冻结并"
            "登记后启用（DTB §4.3 O-27）"));
        ModelingError error;
        error.code = ModelingErrorCode::TemplateDisabled;
        error.params.emplace_back("template-id", id);
        error.params.emplace_back("freeze-gate", "P-03");
        error.detail =
            "模板登记未启用：P-03 七轴模板工程数值未冻结（仅登记不启用）";
        return TemplateOutcome::err(std::move(error));
    }

    // ---- 步③ localName 校验（@pre"非空且合法字符集"——IllegalName 值面；
    //      §9.5 无该错误独立码行→不产诊断，定位在错误 params 内）----
    if (!isValidLocalName(localName)) {
        ModelingError error;
        error.code = ModelingErrorCode::IllegalName;
        error.params.emplace_back("field", "localName");
        error.params.emplace_back("charset", "[A-Za-z0-9_.-]");
        error.detail = localName.empty()
                           ? "localName 为空串"
                           : "localName 含合法字符集之外的字符";
        return TemplateOutcome::err(std::move(error));
    }

    // ---- 步④ 前置：安装预设须在模板词表内（Ground/Inverted/Wall——§5.1
    //      表第 4 列）；Custom 需用户给 customEaa（I-MDL-7），模板路径不
    //      接纳——违约 fail-fast。
    if (preset == runtime::InstallationPresetToken::Custom) {
        throw std::invalid_argument(
            "modeling/template/custom-preset: 模板创建路径不接纳 Custom 预设"
            "（customEaa 必填属创建后编辑——经编辑器 SetBasePlacement）");
    }

    const core::ValueProvenance provenance = templateProvenance(id);
    RobotDesign design;  // 缺省＝schemaVersion 单一权威/Explicit/空引用表

    // 根面：呈现名＝用户基名；权威模式＝模板登记值（三模板均 Explicit）；
    // BasePlacement＝预设＋零位地面（MDL-22 V15-04：未显式配置＝地面，
    // 默认值在模板层填入并带来源标记）。customEaa 保持 NotProvided
    // （非 Custom 预设不适用——I-MDL-7 不触发）。
    design.displayName = std::string(localName);
    design.authority = descriptor->authority;
    design.basePlacement.preset = preset;
    design.basePlacement.basePosition =
        core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, 0.0), provenance);

    // 建链：generic-6r 按 T-MDL-1 六行；custom-chain 产 1 轴种子（J1 行为
    // 种子默认——用户逐轴编辑；增轴经编辑器 AddJoint——T08 §9.4.1）。
    // generic-7r 不可达（步②已拒绝——本单元不使用未冻结的 P-03 数值）。
    std::vector<SixAxisJointSpec> axisSpecs;
    if (id == TemplateId{kTemplateIdGeneric6R}) {
        const auto defaults = sixAxisTemplateDefaults();
        axisSpecs.assign(defaults.begin(), defaults.end());
    } else if (id == TemplateId{kTemplateIdCustomChain}) {
        axisSpecs.push_back(sixAxisTemplateDefaults().front());  // J1 行种子
    } else {
        // disabled 模板不可达步④（实现缺陷护栏——不含 P-03 数值语义）。
        throw std::logic_error("modeling/template/unreachable: 已启用模板缺"
                               "建链规格（实现缺陷）: " + id);
    }

    const std::string baseKey = "ird/modeling/template-draft/" + id;
    for (std::size_t i = 0; i < axisSpecs.size(); ++i) {
        const SixAxisJointSpec& spec = axisSpecs[i];
        JointEntry joint;
        // 确定性临时句柄（§5.2——提交时经命令 prepare 回填真实 ObjectId）
        joint.objectId = deriveDraftObjectId(baseKey + "/joint/" + std::to_string(i));
        // 关节名种子：j<序>（链序从 1 起；设计默认值——I-MDL-2 作用域唯一，
        // 用户可经编辑器改名——改名＝新内容，§4.8）
        joint.localName = "j" + std::to_string(i + 1);
        joint.type = spec.type;
        // 轴线/限位为模板提供的权威值（UserProvided＋模板轨迹标记）；原点
        // 种子＝恒位姿（表 T-MDL-1 未登记 origin 列——几何由用户经属性区
        // 编辑；§14.4 D-MDL-7 增量登记的设计默认值）
        joint.axis =
            core::SourcedValue<rw::math::Vector3D<double>>::provided(spec.axis,
                                                                     provenance);
        joint.origin = core::SourcedValue<JointPose>::provided(JointPose{},
                                                               provenance);
        joint.zeroOffset = spec.zeroOffset;
        joint.bounds = core::SourcedValue<JointLimits>::provided(spec.bounds,
                                                                 provenance);
        // workingRange＝NotApplicable（Revolute 不适用——表 T-MDL-1 该列
        // 全行"—"；I-MDL-4"workingRange 仅 Continuous"）
        joint.workingRange =
            core::SourcedValue<JointLimits>::notApplicable();
        design.joints.push_back(std::move(joint));
    }
    // 连杆链：links==joints+1（I-MDL-1）；命名 base/l<序>（基座＋逐段）；
    // 物性 NotProvided（不猜测）＋材料种子钢（§5.1 材料默认句——密度取
    // §5.3 默认表单点权威）。
    design.links.reserve(axisSpecs.size() + 1);
    design.links.push_back(makeSeedLink(baseKey + "/link/0", "base", provenance));
    for (std::size_t i = 0; i < axisSpecs.size(); ++i) {
        design.links.push_back(
            makeSeedLink(baseKey + "/link/" + std::to_string(i + 1),
                         "l" + std::to_string(i + 1), provenance));
    }

    // ---- 步⑤ 出口守卫：已建链跑维度二判定（§6.4 单一实现）作一致性检查
    //      ——generic-6r 必须 FullTemplateRange（否则＝表被改坏的实现缺陷，
    //      fail-fast）；custom-chain 种子轴数不在范围属预期（链未完成——
    //      完整声明链的创建入口阻断经 creationEntryGuard，见其函数注）。
    if (id == TemplateId{kTemplateIdGeneric6R}) {
        std::vector<JointType> chainTypes;
        chainTypes.reserve(design.joints.size());
        for (const JointEntry& joint : design.joints) {
            chainTypes.push_back(joint.type);
        }
        if (judgeChainCapability(chainTypes).kind
            != ChainCapabilityKind::FullTemplateRange) {
            throw std::logic_error("modeling/template/invariant: 六轴模板建链"
                                   "未通过维度二判定（T-MDL-1 表与 §6.4 判定"
                                   "失一致——实现缺陷）");
        }
    }

    ModelingWorkingSet workingSet;
    workingSet.design = std::move(design);
    // changes 留空——创建本身不是编辑（摘要自创建后的编辑起算）。
    return TemplateOutcome::ok(std::move(workingSet));
}

// =====================================================================
// 创建入口链型守卫（§2.1 创建列 ❌；§6.4 判定复用）
// =====================================================================

std::optional<ChainCapability> creationEntryGuard(
    const std::vector<JointType>& declaredChain,
    std::vector<core::DiagnosticRecord>& diags)
{
    // 判定复用（§6.4 尾段）：结论唯一来自 judgeChainCapability——与导入
    // 报告同一实现，不存在第二份判定。
    const ChainCapability capability = judgeChainCapability(declaredChain);
    if (capability.kind == ChainCapabilityKind::FullTemplateRange) {
        return std::nullopt;  // 放行——不产诊断（无阻断事实）
    }
    // 阻断＋提示（TEMPLATE-RANGE info——T05 行码复用：同码同语义"超出
    // 首版产品模板范围"；创建入口尚无对象，subject 为空 optional，
    // context 以 "template-entry" 定位）。
    diags.push_back(core::DiagnosticRecord::make(
        std::string(kMdlImportTemplateRange), std::nullopt, std::nullopt,
        std::nullopt, "template-entry",
        "movable-axes=" + std::to_string(capability.movableAxes)
            + " prismatic-present="
            + (capability.containsPrismatic ? "true" : "false"),
        "创建入口阻止：4/5 轴或含 prismatic 链超出首版产品模板范围——仅"
        "导入识别与草稿兼容编辑可用（类型保留，不静默降级——V12-01）"));
    return capability;
}

// =====================================================================
// 逐轴编辑流（§5.2）
// =====================================================================

std::string_view jointEditFieldToken(JointEditField field) noexcept
{
    switch (field) {
    case JointEditField::Type: return "type";
    case JointEditField::Axis: return "axis";
    case JointEditField::ZeroOffset: return "zeroOffset";
    case JointEditField::Bounds: return "bounds";
    }
    return "type";  // 全枚举已覆盖，不达此处（无 default——漏项编译器告警）
}

std::string_view jointEditErrorCodeToken(JointEditErrorCode code) noexcept
{
    switch (code) {
    case JointEditErrorCode::AuthorityLocked: return "authority-locked";
    case JointEditErrorCode::ValueNotFinite: return "value-not-finite";
    case JointEditErrorCode::AxisNotNormalizable: return "axis-not-normalizable";
    case JointEditErrorCode::LimitIntervalInvalid: return "limit-interval-invalid";
    case JointEditErrorCode::TypeBoundsConflict: return "type-bounds-conflict";
    }
    return "value-not-finite";  // 全枚举已覆盖，不达此处
}

std::optional<JointEditError> applyJointFieldEdit(ModelingWorkingSet& ws,
                                                  std::size_t jointIndex,
                                                  JointEditField field,
                                                  const JointEditValue& value)
{
    // ① 越界＝调用方契约违约（fail-fast——AGENTS 错误语义）。
    if (jointIndex >= ws.design.joints.size()) {
        throw std::invalid_argument(
            "modeling/template/edit-index: 关节下标越界: joints["
            + std::to_string(jointIndex) + "]");
    }
    // ② field 与 value 备择匹配检查（variant 备择序＝枚举声明序——Type/
    //    Axis/ZeroOffset/Bounds 对 JointType/V3/double/JointLimits）。
    const bool matched =
        (field == JointEditField::Type
         && std::holds_alternative<JointType>(value))
        || (field == JointEditField::Axis
            && std::holds_alternative<rw::math::Vector3D<double>>(value))
        || (field == JointEditField::ZeroOffset
            && std::holds_alternative<double>(value))
        || (field == JointEditField::Bounds
            && std::holds_alternative<JointLimits>(value));
    if (!matched) {
        throw std::invalid_argument(
            "modeling/template/edit-variant: value 备择与 field 不匹配: "
            + std::string(jointEditFieldToken(field)));
    }

    // 先校验后提交（拒绝时工作集字节不变）；接受则追加一条变更记录。
    const std::optional<JointEditError> rejection =
        editJointFieldImpl(ws.design, jointIndex, field, value);
    if (rejection.has_value()) { return rejection; }
    appendChangeRecord(ws, jointIndex, field);
    return std::nullopt;
}

JointBatchEditOutcome applyJointFieldEditBatch(ModelingWorkingSet& ws,
                                               JointEditField field,
                                               const std::vector<JointBatchEditItem>& items)
{
    // 逐行独立校验并提交（§9.4.1 BatchPartial：非法行保留原值、合法行不
    // 连带回滚）；越界/备择不匹配属调用方契约违约——任何一行命中即
    // fail-fast（批量装配错误是编程错误，不是行级数据错误）。
    for (const JointBatchEditItem& item : items) {
        if (item.jointIndex >= ws.design.joints.size()) {
            throw std::invalid_argument(
                "modeling/template/batch-index: 批量行下标越界: joints["
                + std::to_string(item.jointIndex) + "]");
        }
        const bool matched =
            (field == JointEditField::Type
             && std::holds_alternative<JointType>(item.value))
            || (field == JointEditField::Axis
                && std::holds_alternative<rw::math::Vector3D<double>>(item.value))
            || (field == JointEditField::ZeroOffset
                && std::holds_alternative<double>(item.value))
            || (field == JointEditField::Bounds
                && std::holds_alternative<JointLimits>(item.value));
        if (!matched) {
            throw std::invalid_argument(
                "modeling/template/batch-variant: 批量行 value 备择与批量字段"
                "不匹配: " + std::string(jointEditFieldToken(field)));
        }
    }

    JointBatchEditOutcome outcome;
    for (const JointBatchEditItem& item : items) {
        // 逐行提交（顺序语义：前序行影响后序行的校验上下文——如重复行
        // 后值覆盖前值；拒绝行不影响其它行）。
        const std::optional<JointEditError> rejection =
            editJointFieldImpl(ws.design, item.jointIndex, field, item.value);
        if (rejection.has_value()) {
            outcome.rejectedRows.push_back(item.jointIndex);
            outcome.lastError = rejection;
            continue;
        }
        ++outcome.appliedCount;
    }

    // 批量产出单条变更摘要（UX-05：一次调用一个批量变体＝一条记录）；
    // 全行拒绝不追加（无变更即无摘要）。
    if (outcome.appliedCount > 0) {
        ModelingChangeRecord record;
        record.subject = "joints[*]." + std::string(jointEditFieldToken(field));
        record.summary = "批量修改 " + std::string(jointEditFieldToken(field))
                         + "：应用 " + std::to_string(outcome.appliedCount)
                         + " 行，拒绝 " + std::to_string(outcome.rejectedRows.size())
                         + " 行";
        ws.changes.push_back(std::move(record));
    }
    return outcome;
}

// =====================================================================
// 几何生成辅助（§5.2 v0.2）
// =====================================================================

GeneratedGeometry makeLinkPlaceholderCylinder(
    const rw::math::Vector3D<double>& segmentStart,
    const rw::math::Vector3D<double>& segmentEnd,
    std::string resourceRefId, double radius)
{
    // @pre 校验（调用方契约违约——fail-fast）。
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        throw std::invalid_argument(
            "modeling/template/placeholder-radius: 圆柱半径须为正有限值（m）");
    }
    if (resourceRefId.empty()) {
        throw std::invalid_argument(
            "modeling/template/placeholder-ref: resourceRefId 不得为空串");
    }

    // 段向量与长度（相邻关节原点连线——单位 m，连杆系下）。
    const double dx = segmentEnd[0] - segmentStart[0];
    const double dy = segmentEnd[1] - segmentStart[1];
    const double dz = segmentEnd[2] - segmentStart[2];
    const double lengthSq = dx * dx + dy * dy + dz * dz;
    if (!(lengthSq > 0.0)) {
        throw std::invalid_argument(
            "modeling/template/placeholder-length: 起终点重合——零长度圆柱"
            "无几何意义");
    }
    const double length = std::sqrt(lengthSq);
    const double ux = dx / length;  // 连线单位方向（geom 系 z 轴目标方向）
    const double uy = dy / length;
    const double uz = dz / length;

    // 旋转：把 geom 系 z 轴旋到连线方向的最小旋转（Rodrigues 解析式逐元素
    // 展开——冒烟 header-only 纪律：不用 Rotation3D::identity()/EAA 等
    // 框架外联符号）。轴 v = z0×u / |z0×u|，z0=(0,0,1) → z0×u = (−uy, ux, 0)，
    // sinθ = sqrt(ux²+uy²)，cosθ = uz。轴线恰为 ±z 时 s==0（确定性特例：
    // +z 恒等、−z 绕 x 轴 π）。
    const double s = std::sqrt(ux * ux + uy * uy);
    const double c = uz;
    rw::math::Rotation3D<double> rotation(
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0);  // 逐元素恒等（外联符号禁用——见上注）
    if (s == 0.0) {
        if (c < 0.0) {
            // u = −z0：绕 x 轴旋转 π（diag(1,−1,−1)——确定性特例分支）
            rotation = rw::math::Rotation3D<double>(
                1.0, 0.0, 0.0,
                0.0, -1.0, 0.0,
                0.0, 0.0, -1.0);
        }
        // u = +z0：保持恒等（R·z0 = z0 = u）
    } else {
        // Rodrigues 展开：R = I + s·K + (1−c)·K²，K＝skew(v)（推导见头注；
        // 代入 v=(−uy/s, ux/s, 0) 化简——z 列恰为 (ux, uy, c)=u，逐元素式：
        const double oneMinusC = 1.0 - c;
        const double s2Inv = 1.0 / (s * s);
        rotation = rw::math::Rotation3D<double>(
            1.0 + oneMinusC * (uy * uy * s2Inv - 1.0),
            -oneMinusC * (ux * uy) * s2Inv,
            ux,
            -oneMinusC * (ux * uy) * s2Inv,
            1.0 + oneMinusC * (ux * ux * s2Inv - 1.0),
            uy,
            -ux,
            -uy,
            c);
    }

    GeneratedGeometry result;
    result.geometry.resourceRefId = std::move(resourceRefId);
    result.geometry.localTransform = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(
            (segmentStart[0] + segmentEnd[0]) / 2.0,  // 中心＝连线中点（m）
            (segmentStart[1] + segmentEnd[1]) / 2.0,
            (segmentStart[2] + segmentEnd[2]) / 2.0),
        rotation);
    result.geometry.kind = GeometryKind::Primitive;  // 占位原语（视觉用）
    // 来源标记：GeometricEstimate＋methodTag 区分辅助类型（§5.2 原文）。
    result.provenance = core::ValueProvenance::make(
        core::ProvenanceKind::GeometricEstimate, std::nullopt, std::nullopt,
        "link-placeholder-cylinder");
    return result;
}

GeneratedGeometry copyVisualToCollision(const GeometryRef& visual)
{
    // 复制＝同资源、同位姿、同类别（不重画/不凸包简化——上游未授权）；
    // 碰撞几何≠碰撞判定（判定唯一归 policy——§4.3-B 注）。
    GeneratedGeometry result;
    result.geometry = visual;
    result.provenance = core::ValueProvenance::make(
        core::ProvenanceKind::GeometricEstimate, std::nullopt, std::nullopt,
        "collision-copy");
    return result;
}

}  // namespace sdurws::ird::modeling
