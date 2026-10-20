/**
 * @file   HostEngineeringPolicy.cpp
 * @brief  A-23 工程策略宿主适配实现。
 *
 * 设计依据：UX-08/ARC-05/MDL-06，requirements-completion计划A-23。
 * 本文件只编排既有policy解析与project事务，不计算第二套阈值或内容身份。
 * 所有Qt控件与发布指针仅UI线程操作；后台任务只持捕获的不可变策略纯值。
 */
#include "HostEngineeringPolicy.hpp"

#include <sdurws/ird/modeling/Codec.hpp>         // RobotDesignCodec（F07 导入候选名称解析——robot-design 根解码，公共头 R-2）
#include <sdurws/ird/modeling/ObjectTypes.hpp>   // kRobotDesignObjectType（根对象 token 单一权威）
#include <sdurws/ird/modeling/RobotDesign.hpp>   // RobotDesign/LinkEntry（localName→ObjectId 解析面）

#include <array>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <cmath>
#include <sdurws/ird/core/Units.hpp>
#include <stdexcept>
#include <map>
#include <sdurws/ird/modeling/Codec.hpp>
#include <sdurws/ird/modeling/ObjectTypes.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <algorithm>

#include <sdurws/ird/policy/SystemDefaultPolicy.hpp>
#include <sdurws/ird/policy/Diagnostics.hpp>

namespace sdurws::ird::ui {
namespace {

/**
 * @brief 将 project 查询端口适配为 policy 的对象字节端口。
 *
 * 适配器只允许读取传入修订的完整闭包；即使 HEAD 已经前进，也不会把
 * 新版本字节混入旧修订的解析（A-23/MDL-06）。
 */
class RevisionBytesSource final : public policy::IPolicyBytesSource {
public:
    RevisionBytesSource(const project::IProjectQueryPort& query,
                        const project::RevisionView& revision)
        : m_query(query), m_revision(revision)
    {
    }

    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        core::ObjectId object, core::ContentVersion version) const override
    {
        const auto it = std::find_if(
            m_revision.objectRefs.begin(), m_revision.objectRefs.end(),
            [object, version](const project::ObjectRef& ref) {
                return ref.objectId == object && ref.contentVersion == version;
            });
        if (it == m_revision.objectRefs.end()) {
            return std::nullopt;
        }
        return m_query.tryObject(object, version);
    }

private:
    const project::IProjectQueryPort& m_query;
    const project::RevisionView& m_revision;
};

/** @brief policy 解析期的修订闭包事实，只读且不缓存可变 HEAD。 */
class RevisionValidationContext final : public policy::IPolicyValidationContext {
public:
    /** @brief 从冻结修订构造存在性/角色目录；query[in]只借用到同步解析返回。 */
    RevisionValidationContext(const project::IProjectQueryPort& query,
                              const project::RevisionView& revision)
    {
        for (const auto& ref : revision.objectRefs) {
            m_objects.emplace(ref.objectId, std::string{});
            if (ref.objectTypeToken != modeling::kRobotDesignObjectType
                && ref.objectTypeToken != modeling::kToolDefinitionObjectType
                && ref.objectTypeToken != modeling::kSceneObjectObjectType) { continue; }
            const auto bytes = query.tryObject(ref.objectId, ref.contentVersion);
            if (!bytes) { continue; } // 字节缺口不虚构角色；涉及该角色的规则交给policy拒绝。
            const auto decoded = modeling::RobotDesignCodec{}.decode(*bytes, modeling::kCurrentFormatVersion);
            if (!decoded.ok()) { continue; }
            const auto& object = decoded.get();
            if (const auto* robot = std::get_if<modeling::RobotDesign>(&object)) {
                for (const auto& joint : robot->joints) { m_objects.emplace(joint.objectId, std::string{}); }
                for (const auto& link : robot->links) { m_objects[link.objectId] = "RobotLink"; }
            } else if (const auto* tool = std::get_if<modeling::ToolDefinition>(&object)) {
                m_objects[tool->objectId] = "Tool";
            } else if (const auto* scene = std::get_if<modeling::SceneObject>(&object)) {
                m_objects[scene->objectId] = std::string{modeling::sceneObjectRoleToken(scene->role)};
            }
        }
    }

    /** @brief 对象存在性含嵌入的关节/连杆；object[in]为稳定ID，未知返回false。 */
    bool objectExists(core::ObjectId object) const override { return m_objects.count(object) != 0; }

    /** @brief 返回建模权威角色token；根/关节等无碰撞角色的对象返回nullopt。 */
    std::optional<std::string> objectRole(core::ObjectId object) const override
    {
        const auto it = m_objects.find(object);
        if (it == m_objects.end() || it->second.empty()) { return std::nullopt; }
        return it->second;
    }

    bool groupDefined(std::string_view) const override
    {
        // 当前 RawPolicyInput 不承载组定义；未找到组必须让 policy 诊断明确失败。
        return false;
    }

private:
    std::map<core::ObjectId, std::string> m_objects; ///< 冻结闭包的目录，不跨修订缓存。
};

constexpr const char* kPolicyObjectToken = "EngineeringPolicy";

/** @brief 从已排除歧义的修订查唯一策略引用；revision[in]只读，零候选返回空。 */
std::optional<project::ObjectRef> policyRef(const project::RevisionView& revision)
{
    std::optional<project::ObjectRef> result;
    for (const auto& ref : revision.objectRefs) {
        if (ref.objectTypeToken != kPolicyObjectToken) {
            continue;
        }
        if (result.has_value()) {
            // 多候选不是“没有策略”；调用方在进入本函数前必须拒绝歧义。
            return std::nullopt;
        }
        result = ref;
    }
    return result;
}

/** @brief 生成未持久化的默认草稿；保留ID仅用于编码运输，绝不写入项目。 */
policy::RawPolicyInput defaultRawPolicy()
{
    const auto defaults = policy::makeSystemDefaultPolicySet();
    policy::RawPolicyInput raw;
    raw.policyObject = policy::systemDefaultPolicyObjectId();
    raw.collision.enabled = defaults.collision.enabled;
    raw.collision.enabledDomains = defaults.collision.enabledDomains;
    raw.collision.excludeAdjacentLinksByDefault = defaults.collision.excludeAdjacentLinksByDefault;
    raw.jointThresholds.finiteRotationTravelLimit = policy::RawThresholdInput{
        defaults.jointThresholds.finiteRotationTravelLimit.siValue(), "rad"};
    raw.jointThresholds.travelLimitCheckEnabled = defaults.jointThresholds.travelLimitCheckEnabled;
    raw.applicability = defaults.applicability;
    raw.origin = defaults.origin;
    return raw;
}

constexpr const char* kEngineeringPolicyObjectToken = "EngineeringPolicy";
constexpr const char* kEngineeringPolicyCommand = "edit-engineering-policy";
constexpr std::uint8_t kEngineeringPolicyPayloadMagic[6] = {'I','R','D','P','E','1'};

/**
 * @brief 封装瞬时命令载荷；不改变PolicyCodec持久化格式。
 * @param allocateNew [in] true首次取号，false更新同ID；撤销/重做沿既有project协议。
 * @param policyBytes [in] 唯一PolicyCodec编码；返回新值，无共享状态，字节序固定小端。
 */
std::vector<std::uint8_t> makePolicyCommandPayload(
    bool allocateNew, const std::vector<std::uint8_t>& policyBytes)
{
    std::vector<std::uint8_t> out(std::begin(kEngineeringPolicyPayloadMagic),
                                  std::end(kEngineeringPolicyPayloadMagic));
    out.push_back(allocateNew ? 1u : 0u);
    const auto size = static_cast<std::uint32_t>(policyBytes.size());
    for (unsigned i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((size >> (8u * i)) & 0xffu));
    }
    out.insert(out.end(), policyBytes.begin(), policyBytes.end());
    return out;
}

/**
 * @brief 校验命令封套；bytes[in]魔数/标志/长度任一错误返回false，绝不部分解码。
 * @param allocateNew [out] 成功时写取号标志；policyBytes[out]成功时写原始策略字节。
 */
bool decodePolicyCommandPayload(const std::vector<std::uint8_t>& bytes,
                                bool* allocateNew,
                                std::vector<std::uint8_t>* policyBytes)
{
    if (bytes.size() < 11
        || !std::equal(std::begin(kEngineeringPolicyPayloadMagic),
                       std::end(kEngineeringPolicyPayloadMagic), bytes.begin())
        || (bytes[6] != 0u && bytes[6] != 1u)) {
        return false;
    }
    const std::uint32_t size = static_cast<std::uint32_t>(bytes[7])
        | (static_cast<std::uint32_t>(bytes[8]) << 8u)
        | (static_cast<std::uint32_t>(bytes[9]) << 16u)
        | (static_cast<std::uint32_t>(bytes[10]) << 24u);
    if (size != bytes.size() - 11u) {
        return false;
    }
    *allocateNew = bytes[6] != 0u;
    policyBytes->assign(bytes.begin() + 11, bytes.end());
    return true;
}

/**
 * @brief 策略编辑的无状态项目命令；registry独占拥有，prepare在命令槽串行运行。
 * 候选在冻结基线上走policy七段校验，成功只声明ObjectWrite及逆命令。
 */
class EngineeringPolicyCommandHandler final : public project::ICommandHandler {
public:
    /** @brief 返回私有业务命令族；不含点，服从project处理器注册约束。 */
    std::string commandType() const override { return kEngineeringPolicyCommand; }
    /** @brief 瞬时封套当前版本1；不是PolicySchema存储版本。 */
    std::uint32_t currentPayloadVersion() const override { return 1; }

    /**
     * @brief 基于baseSnapshot[in]产出out[out]完整写入/逆载荷；diags[out]保留拒绝原因。
     * ctx[in]只借用到本次返回，envelope[in]原始载荷不修改；失败不产生写计划。
     */
    project::PrepareOutcome prepare(
        project::HandlerContext& ctx, const project::CommandEnvelope& envelope,
        const project::RevisionView& baseSnapshot, project::CommandPlan& out,
        std::vector<core::DiagnosticRecord>& diags) override
    {
        auto reject = [&](const std::string& reason) {
            diags.push_back(core::DiagnosticRecord::make(
                std::string{policy::policyDiagCode(policy::PolicyDiagCode::PolicyObjectInvalid)},
                policy::systemDefaultPolicyObjectId(),std::nullopt,std::nullopt,
                "工程策略/命令准备",reason,"修正策略输入或重新打开编辑器后重试"));
            return project::PrepareOutcome::RejectedInvalidInput;
        };
        bool allocateNew = false;
        std::vector<std::uint8_t> encoded;
        if (!decodePolicyCommandPayload(envelope.payloadCanonical, &allocateNew, &encoded)) {
            return reject("命令封套、策略身份或基线对象不一致");
        }
        policy::RawPolicyInput input;
        try {
            input = policy::PolicyCodec::decode(encoded);
        } catch (const policy::PolicyError& error) {
            return reject(error.what());
        }

        std::optional<project::ObjectRef> existing;
        for (const auto& ref : baseSnapshot.objectRefs) {
            if (ref.objectTypeToken == kEngineeringPolicyObjectToken) {
                if (existing) {
                    return reject("命令封套、策略身份或基线对象不一致");
                }
                existing = ref;
            }
        }
        if (allocateNew) {
            if (existing) {
                // 首次创建的重做会重放allocateNew载荷；仅当当前同OID对象确实
                // 是首次撤销恢复的系统默认时允许复用，不能覆盖已有用户策略。
                const auto bytes = ctx.query().tryObject(existing->objectId,existing->contentVersion);
                if (!bytes) { return reject("命令封套、策略身份或基线对象不一致"); }
                const auto previous = policy::PolicyCodec::decode(*bytes);
                RevisionValidationContext context(ctx.query(),baseSnapshot);
                const auto resolved = policy::resolvePolicy(previous,context);
                if (previous.origin.kind != policy::PolicyOriginKind::SystemDefault || !resolved.policy
                    || resolved.policy->contentIdentity != policy::makeSystemDefaultPolicySet().contentIdentity) {
                    return reject("命令封套、策略身份或基线对象不一致");
                }
                input.policyObject = existing->objectId;
            } else { input.policyObject = ctx.objectId(); }
        } else {
            if (!existing || input.policyObject != existing->objectId) {
                return reject("命令封套、策略身份或基线对象不一致");
            }
        }

        RevisionValidationContext validation(ctx.query(), baseSnapshot);
        const policy::PolicyParseResult parsed = policy::resolvePolicy(input, validation);
        if (!parsed.policy) {
            diags = parsed.diagnostics;
            return project::PrepareOutcome::RejectedInvalidInput;
        }

        out.objectWrites.push_back(project::ObjectWrite{
            input.policyObject, kEngineeringPolicyObjectToken,
            policy::PolicyCodec::encode(input)});
        out.summary = "编辑工程策略";
        out.inverseCommandType = kEngineeringPolicyCommand;
        if (existing) {
            const auto oldBytes = ctx.query().tryObject(existing->objectId, existing->contentVersion);
            if (!oldBytes) { return reject("命令封套、策略身份或基线对象不一致"); }
            out.inversePayloadCanonical = makePolicyCommandPayload(false, *oldBytes);
        } else {
            // 首次撤销保留project分配的稳定身份，以默认语义写新版本恢复原行为；
            // 不删除历史对象，不引入 tombstone 或第二种存储格式（PA-2）。
            auto previous = defaultRawPolicy();
            previous.policyObject = input.policyObject;
            out.inversePayloadCanonical = makePolicyCommandPayload(false, policy::PolicyCodec::encode(previous));
        }
        return project::PrepareOutcome::Planned;
    }
};

/**
 * @brief 可选阈值行：空文本表示未设置；单位转换仅使用core，保留17位往返精度。
 * 控件均由Qt父树管理，仅UI线程使用；不持有策略对象或项目指针。
 */
struct ThresholdEditor {
    QLineEdit* value = nullptr;
    QComboBox* unit = nullptr;
    /** @brief 创建字段；initial[in]为原显示值/单位，tokens[in]限定量纲，name[in]用于定位。 */
    ThresholdEditor(QFormLayout* form, const QString& label, const char* name,
                    const std::optional<policy::RawThresholdInput>& initial,
                    const QStringList& tokens)
    {
        auto* row = new QWidget(form->parentWidget());
        auto* layout = new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0);
        value = new QLineEdit(row); value->setObjectName(name);
        value->setPlaceholderText(QStringLiteral("未设置"));
        unit = new QComboBox(row); unit->setObjectName(QString::fromLatin1(name)+"_unit");
        unit->addItems(tokens);
        if (initial) {
            value->setText(QString::number(initial->value, 'g', 17));
            if (unit->findText(QString::fromStdString(initial->unitToken)) < 0) {
                unit->addItem(QString::fromStdString(initial->unitToken));
            }
            unit->setCurrentText(QString::fromStdString(initial->unitToken));
        }
        layout->addWidget(value, 1); layout->addWidget(unit); form->addRow(label,row);
        // 切换显示单位必须保持物理量；非法文本暂不换算，确认时原位定位。
        auto previous = std::make_shared<QString>(unit->currentText());
        QObject::connect(unit, &QComboBox::currentTextChanged, row,
            [thisValue=value, previous, row](const QString& next) {
                bool ok = false; const double number = thisValue->text().toDouble(&ok);
                if (ok && !thisValue->text().trimmed().isEmpty()) {
                    const auto from = core::UnitToken::find(previous->toStdString());
                    const auto to = core::UnitToken::find(next.toStdString());
                    if (from && to) {
                        try { thisValue->setText(QString::number(core::convert(number,*from,*to),'g',17)); }
                        catch (const std::exception& error) { thisValue->setToolTip(QString::fromUtf8(error.what())); }
                    }
                }
                *previous = next;
            });
    }
    /** @brief 读取原始输入；非法/非有限文本定位字段并抛出可见编辑错误，未设置返回nullopt。 */
    std::optional<policy::RawThresholdInput> read() const
    {
        if (value->text().trimmed().isEmpty()) { return std::nullopt; }
        bool ok = false; const double number = value->text().toDouble(&ok);
        if (!ok || !std::isfinite(number)) {
            value->setFocus(); value->selectAll();
            throw std::invalid_argument("阈值必须是有限数字，留空表示未设置");
        }
        return policy::RawThresholdInput{number, unit->currentText().toStdString()};
    }
};

/** @brief 双对象规则表；所有行完整保留，冲突/角色/组存在性统一交policy验证。 */
class PairEditor {
public:
    QTableWidget* table; ///< Qt父树持有；列次序为两个目标的类型/值、级别、理由。
    /** @brief 在page[in,out]创建表格；rules[in]原值投影，name[in]唯一控件名。 */
    PairEditor(QWidget* page, const std::vector<policy::PairRule>& rules, const char* name)
    {
        auto* layout = new QVBoxLayout(page);
        table = new QTableWidget(0,6,page); table->setObjectName(name);
        table->setHorizontalHeaderLabels({QStringLiteral("目标一类型"),QStringLiteral("目标一"),
            QStringLiteral("目标二类型"),QStringLiteral("目标二"),QStringLiteral("级别"),QStringLiteral("理由（必填）")});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(table);
        auto* add = new QPushButton(QStringLiteral("添加规则"),page); add->setObjectName(QString::fromLatin1(name)+"_add");
        auto* remove = new QPushButton(QStringLiteral("删除选中规则"),page);
        layout->addWidget(add); layout->addWidget(remove);
        QObject::connect(add,&QPushButton::clicked,page,[this] { append(std::nullopt); });
        QObject::connect(remove,&QPushButton::clicked,page,[this] { if(table->currentRow()>=0) table->removeRow(table->currentRow()); });
        for(const auto& rule:rules) { append(rule); }
    }
    /** @brief 添加新行或无损回显已有行；rule[in]空时不发明对象引用。 */
    void append(const std::optional<policy::PairRule>& rule)
    {
        const int row=table->rowCount(); table->insertRow(row);
        for(int side=0;side<2;++side) {
            auto* kind=new QComboBox(table); kind->addItems({QStringLiteral("对象"),QStringLiteral("角色"),QStringLiteral("组")});
            const auto* target=rule ? (side==0?&rule->first:&rule->second) : nullptr;
            if(target) kind->setCurrentIndex(static_cast<int>(target->kind));
            table->setCellWidget(row,side*2,kind);
            const std::string text=!target?"":target->kind==policy::ScopeTargetKind::Object?target->object.toCanonical():
                target->kind==policy::ScopeTargetKind::Role?target->roleToken:target->groupName;
            table->setItem(row,side*2+1,new QTableWidgetItem(QString::fromStdString(text)));
        }
        auto* level=new QComboBox(table); level->addItems({QStringLiteral("必须 Must"),QStringLiteral("建议 Should")});
        if(rule) level->setCurrentIndex(static_cast<int>(rule->level)); table->setCellWidget(row,4,level);
        table->setItem(row,5,new QTableWidgetItem(rule?QString::fromStdString(rule->reason):QString{}));
    }
    /** @brief 读取每行；对象ID语法错误定位到单元格，语义错误由policy统一返回。 */
    std::vector<policy::PairRule> read() const
    {
        std::vector<policy::PairRule> result;
        for(int row=0;row<table->rowCount();++row) {
            policy::PairRule rule;
            for(int side=0;side<2;++side) {
                const auto text=table->item(row,side*2+1)->text().trimmed().toStdString();
                auto& target=side==0?rule.first:rule.second;
                target.kind=static_cast<policy::ScopeTargetKind>(static_cast<QComboBox*>(table->cellWidget(row,side*2))->currentIndex());
                if(target.kind==policy::ScopeTargetKind::Object) {
                    const auto id=core::ObjectId::tryFromCanonical(text);
                    if(!id || !id->isValid()) { table->setCurrentCell(row,side*2+1); table->setFocus(); throw std::invalid_argument("对象规则：请输入有效对象ID"); }
                    target.object=*id;
                } else if(target.kind==policy::ScopeTargetKind::Role) { target.roleToken=text; }
                else { target.groupName=text; }
            }
            rule.level=static_cast<policy::PolicyRuleLevel>(static_cast<QComboBox*>(table->cellWidget(row,4))->currentIndex());
            rule.reason=table->item(row,5)->text().toStdString(); result.push_back(std::move(rule));
        }
        return result;
    }
};

/** @brief 以空白/逗号分隔输入对象集合；空集表示不限，错误由字段焦点定位。 */
std::vector<core::ObjectId> readObjects(QLineEdit* field)
{
    std::vector<core::ObjectId> result;
    const auto tokens=field->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")),Qt::SkipEmptyParts);
    for(const auto& token:tokens) {
        const auto id=core::ObjectId::tryFromCanonical(token.toStdString());
        if(!id || !id->isValid()) { field->setFocus(); throw std::invalid_argument("适用范围/来源：对象ID无效"); }
        result.push_back(*id);
    }
    return result;
}

/** @brief 将ID集合投影为可编辑文本；集合语义和规范排序仍归policy解析。 */
QString objectText(const std::vector<core::ObjectId>& values)
{
    QStringList text; for(const auto& value:values) text.push_back(QString::fromStdString(value.toCanonical()));
    return text.join(" ");
}

/** @brief 可选阈值直投SI只读摘要；value[in]未设置则present=false，绝不补造阈值。 */
PolicyThresholdProjection thresholdProjection(
    const std::optional<policy::PolicyThreshold>& value)
{
    PolicyThresholdProjection out;
    if (value) {
        out.present = true;
        out.siValue = value->siValue();
    }
    return out;
}

/** @brief 已发布策略映射为摘要值；set[in]不可变，单位恒为SI，显示换算归摘要卡。 */
PolicySummaryProjection project(const policy::EngineeringPolicySet& set)
{
    PolicySummaryProjection out;
    out.available = true;
    out.collisionDomainEnabled = set.collision.enabled;
    for (const auto domain : set.collision.enabledDomains) {
        switch (domain) {
        case policy::CollisionDomain::Self: out.enabledDomainTokens.emplace_back("self"); break;
        case policy::CollisionDomain::Environment: out.enabledDomainTokens.emplace_back("environment"); break;
        case policy::CollisionDomain::Tool: out.enabledDomainTokens.emplace_back("tool"); break;
        case policy::CollisionDomain::Scene: out.enabledDomainTokens.emplace_back("scene"); break;
        }
    }
    if (set.collision.safetyClearance) {
        out.safetyClearance.present = true;
        out.safetyClearance.siValue = set.collision.safetyClearance->siValue();
    }
    out.mandatoryPairCount = set.collision.mandatoryPairs.size();
    out.filterPairCount = set.collision.excludedPairs.size();
    out.nearLimitRatio = thresholdProjection(set.jointThresholds.nearLimitRatio);
    out.conditionNumberWarning = thresholdProjection(set.jointThresholds.conditionNumberWarning);
    out.travelLimit.present = true;
    out.travelLimit.siValue = set.jointThresholds.finiteRotationTravelLimit.siValue();
    return out;
}

}  // namespace

HostEngineeringPolicy::HostEngineeringPolicy(EditCallback editCallback)
    : m_policy(policy::makeSystemDefaultPolicySet()), m_editCallback(std::move(editCallback))
{
}

PolicySummaryProjection HostEngineeringPolicy::summary() const
{
    if (m_revisionSource && m_revisionResolver) {
        const policy::PolicyResolution resolved =
            m_revisionResolver(m_revisionSource());
        if (resolved.policy) {
            return project(*resolved.policy);
        }
        // 项目策略解析失败时保留不可用事实，不能用默认阈值掩盖坏数据。
        return {};
    }
    return project(m_policy);
}

void HostEngineeringPolicy::requestPolicyEdit()
{
    if (m_editCallback) {
        m_editCallback();
    }
}

void HostEngineeringPolicy::setRevisionResolver(RevisionResolver resolver)
{
    m_revisionResolver = std::move(resolver);
}

void HostEngineeringPolicy::setRevisionSource(
    std::function<project::RevisionView()> source)
{
    m_revisionSource = std::move(source);
}

void HostEngineeringPolicy::bindProjectStore(project::ProjectStore* store)
{
    if (store == nullptr) {
        m_store = nullptr;
        m_publishedPolicy.reset();
        m_storeOwner.reset();
        m_revisionSource = {};
        m_revisionResolver = {};
        return;
    }
    m_storeOwner.reset(); // 新绑定不保留旧项目；shared重载的实参在此调用期间自行保活。
    m_store = store;
    setRevisionSource([store] { return store->closed() ? project::RevisionView{} : store->query().head(); });
    setRevisionResolver([this, store](const project::RevisionView& revision) {
        if (store->closed()) {
            policy::PolicyResolution rejected;
            rejected.diagnostics.push_back(core::DiagnosticRecord::make(
                std::string{policy::policyDiagCode(policy::PolicyDiagCode::ObjectMissing)},
                policy::systemDefaultPolicyObjectId(), std::nullopt, std::nullopt,
                "工程策略/项目", "项目已关闭", "打开项目后重试"));
            return rejected;
        }
        const auto count = std::count_if(revision.objectRefs.begin(), revision.objectRefs.end(),
            [](const project::ObjectRef& ref) { return ref.objectTypeToken == kPolicyObjectToken; });
        if (count > 1) {
            policy::PolicyResolution rejected;
            rejected.diagnostics.push_back(core::DiagnosticRecord::make(
                std::string{policy::policyDiagCode(policy::PolicyDiagCode::PolicyObjectInvalid)},
                std::find_if(revision.objectRefs.begin(),revision.objectRefs.end(),
                    [](const project::ObjectRef& ref){return ref.objectTypeToken==kPolicyObjectToken;})->objectId,
                std::nullopt, std::nullopt,
                "工程策略/修订闭包", "同一修订包含多个工程策略对象，无法确定唯一权威",
                "修复项目策略对象冲突后重试"));
            return rejected;
        }
        const auto selected = policyRef(revision);
        if (!selected) {
            policy::PolicyResolution fallback;
            fallback.policy.emplace(m_policy);
            return fallback;
        }
        RevisionBytesSource bytes(store->query(), revision);
        RevisionValidationContext context(store->query(), revision);
        policy::PolicyProvider provider(
            bytes, context, nullptr,
            policy::CollisionBackendDescriptor{"ui.policy", "1", "policy"});
        return provider.resolvePolicy(
            policy::PolicyResolutionRequest{selected->objectId,
                                            selected->contentVersion});
    });
}

void HostEngineeringPolicy::bindProjectStore(std::shared_ptr<project::ProjectStore> store)
{
    bindProjectStore(store.get());
    m_storeOwner = std::move(store);
    refreshPublishedPolicy();
}

void HostEngineeringPolicy::refreshPublishedPolicy()
{
    m_publishedPolicy.reset();
    if (!m_store || m_store->closed()) { return; }
    const auto resolved=resolvePolicyForRevision(m_store->query().head());
    if (resolved.policy) { m_publishedPolicy=std::make_shared<const policy::EngineeringPolicySet>(*resolved.policy); }
}

void HostEngineeringPolicy::registerCommandHandler(project::HandlerRegistry& registry)
{
    registry.registerHandler(std::make_unique<EngineeringPolicyCommandHandler>());
}

project::CommandResult HostEngineeringPolicy::submit(
    project::ProjectStore& store, const project::RevisionView& base,
    const policy::RawPolicyInput& input, bool allocateNew)
{
    project::CommandEnvelope envelope;
    envelope.branch = base.branch;
    envelope.expectedRevision = base.id;
    envelope.commandType = kEngineeringPolicyCommand;
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical = makePolicyCommandPayload(allocateNew, policy::PolicyCodec::encode(input));
    return store.commands().submit(envelope);
}

void HostEngineeringPolicy::edit(QWidget* parent)
{
    edit(parent, {});
}

void HostEngineeringPolicy::edit(
    QWidget* parent,
    const std::vector<std::pair<std::string, std::string>>&
        importedExcludedCandidates)
{
    // 模态循环会处理关闭/切换项目事件；共享别名锚住原存储，提交前同时核对
    // 原项目仍绑定、仍可写、基线仍是表单打开时修订，禁止误写新项目。
    const auto owner=m_storeOwner;
    auto* store=m_store;
    if(!store || store->closed() || !store->writable()) { return; }
    const auto base=store->query().head();
    const auto count=std::count_if(base.objectRefs.begin(),base.objectRefs.end(),
        [](const project::ObjectRef& ref) {return ref.objectTypeToken==kPolicyObjectToken;});
    if(count>1) { QMessageBox::warning(parent,QStringLiteral("工程策略"),QStringLiteral("同一修订存在多个策略对象，请先修复冲突")); return; }
    const auto ref=policyRef(base);
    auto input=defaultRawPolicy();
    if(ref) {
        const auto bytes=store->query().tryObject(ref->objectId,ref->contentVersion);
        if(!bytes) { QMessageBox::warning(parent,QStringLiteral("工程策略"),QStringLiteral("策略对象缺失，未写入")); return; }
        try { input=policy::PolicyCodec::decode(*bytes); }
        catch(const policy::PolicyError& error) { QMessageBox::warning(parent,QStringLiteral("工程策略"),QString::fromUtf8(error.what())); return; }
    }

    // ---- F07/P-MDL-3：导入排除候选预填（半区说明见头注）。名称→ObjectId
    //      解析数据源＝当前修订 robot-design 根对象字节（modeling 公共
    //      Codec 解码——R-2 合规；名称作用域＝建模根内 localName，与导入
    //      候选的源语义一致，不做前缀拼接/剥离——R-4）。未解析名不静默
    //      丢弃——警示行如实列出；预填仅进表单，确认才产生策略修订。
    QString seedNote;
    if (!importedExcludedCandidates.empty()) {
        std::map<std::string, core::ObjectId> linkIds;
        const bool modelReadable = [&] {
            constexpr std::string_view kRobotDesignToken{modeling::kRobotDesignObjectType};
            std::optional<project::ObjectRef> root;
            for (const auto& ref : base.objectRefs) {
                if (ref.objectTypeToken == std::string(kRobotDesignToken)) {
                    if (root.has_value()) { return false; }  // 多根歧义＝不解析
                    root = ref;
                }
            }
            if (!root.has_value()) { return false; }
            const auto bytes = store->query().tryObject(root->objectId,
                                                        root->contentVersion);
            if (!bytes) { return false; }
            try {
                const modeling::RobotDesignCodec codec;
                const auto decoded = codec.decode(
                    modeling::Bytes{bytes->begin(), bytes->end()},
                    modeling::kCurrentFormatVersion);
                if (!decoded.ok()
                    || decoded.get().index() != 0u) { return false; }
                for (const auto& link :
                     std::get<modeling::RobotDesign>(decoded.get()).links) {
                    linkIds.emplace(link.localName, link.objectId);
                }
                return true;
            } catch (...) { return false; }  // 解码面异常＝不可解析（诚实缺席）
        }();
        const auto lookup = [&](const std::string& name)
            -> std::optional<core::ObjectId> {
            const auto it = linkIds.find(name);
            if (it == linkIds.end()) { return std::nullopt; }
            return it->second;
        };
        std::size_t seeded = 0;
        QStringList unresolved;
        for (const auto& [name1, name2] : importedExcludedCandidates) {
            const auto o1 = lookup(name1);
            const auto o2 = lookup(name2);
            if (!modelReadable || !o1.has_value() || !o2.has_value()) {
                unresolved << QString::fromStdString(name1 + " ↔ " + name2);
                continue;
            }
            const bool duplicated = std::any_of(
                input.collision.excludedPairs.begin(),
                input.collision.excludedPairs.end(),
                [&](const policy::PairRule& r) {
                    return (r.first.object == *o1 && r.second.object == *o2)
                           || (r.first.object == *o2 && r.second.object == *o1);
                });
            if (duplicated) { continue; }  // 既有策略已含该对＝不重复预填
            input.collision.excludedPairs.push_back(policy::PairRule::make(
                policy::ScopeTarget::makeObject(*o1),
                policy::ScopeTarget::makeObject(*o2),
                policy::PolicyRuleLevel::Should,
                "导入候选（URDF disable_collisions）——经确认写入策略"));
            ++seeded;
        }
        seedNote = QStringLiteral("｜导入候选预填 %1 对").arg(seeded);
        if (!unresolved.isEmpty()) {
            seedNote += QStringLiteral("；未解析 %1 对（模型未应用或名称不在"
                                       "当前修订，未预填）：")
                        .arg(unresolved.size())
                        + unresolved.join(QStringLiteral("、"));
        }
    }
    QDialog dialog(parent); dialog.setObjectName("ird_policy_editor");
    dialog.setWindowTitle(QStringLiteral("修改工程策略")); dialog.resize(900,680);
    auto* layout=new QVBoxLayout(&dialog);
    auto* source=new QLabel((ref?QStringLiteral("来源：项目策略；确认产生一个新修订，取消不写入"):
        QStringLiteral("来源：系统缺省；首次确认后创建项目策略，未设置阈值保持未设置"))+seedNote,&dialog);
    source->setObjectName("policy_source"); source->setWordWrap(true); layout->addWidget(source);
    auto* tabs=new QTabWidget(&dialog); layout->addWidget(tabs,1);
    auto* general=new QWidget(tabs); auto* form=new QFormLayout(general); tabs->addTab(general,QStringLiteral("碰撞与阈值"));
    auto check=[&](const char* name,const QString& label,bool value) {
        auto* field=new QCheckBox(label,general);field->setObjectName(name);field->setChecked(value);form->addRow(field);return field;
    };
    auto* collision=check("policy_collision",QStringLiteral("启用碰撞检查"),input.collision.enabled);
    std::vector<QCheckBox*> domains;
    const QStringList domainLabels{QStringLiteral("自身"),QStringLiteral("环境"),QStringLiteral("工具"),QStringLiteral("场景")};
    for(int i=0;i<4;++i) {
        domains.push_back(check(("policy_domain_"+std::to_string(i)).c_str(),domainLabels[i],
            std::find(input.collision.enabledDomains.begin(),input.collision.enabledDomains.end(),static_cast<policy::CollisionDomain>(i))!=input.collision.enabledDomains.end()));
    }
    ThresholdEditor clearance(form,QStringLiteral("安全间距"),"policy_clearance",input.collision.safetyClearance,{"m","mm"});
    auto* adjacent=check("policy_adjacent",QStringLiteral("默认过滤相邻连杆"),input.collision.excludeAdjacentLinksByDefault);
    auto* travelEnabled=check("policy_travel_enabled",QStringLiteral("启用有限旋转行程检查"),input.jointThresholds.travelLimitCheckEnabled);
    ThresholdEditor travel(form,QStringLiteral("有限旋转行程上限"),"policy_travel",input.jointThresholds.finiteRotationTravelLimit,{"rad","deg"});
    ThresholdEditor nearLimit(form,QStringLiteral("近限位比（可留空）"),"policy_near",input.jointThresholds.nearLimitRatio,{"1"});
    ThresholdEditor condition(form,QStringLiteral("条件数警戒值（可留空）"),"policy_condition",input.jointThresholds.conditionNumberWarning,{"1"});
    auto* mandatoryPage=new QWidget(tabs);tabs->addTab(mandatoryPage,QStringLiteral("必检对象对"));
    PairEditor mandatory(mandatoryPage,input.collision.mandatoryPairs,"policy_mandatory");
    auto* excludedPage=new QWidget(tabs);tabs->addTab(excludedPage,QStringLiteral("忽略对象对"));
    PairEditor excluded(excludedPage,input.collision.excludedPairs,"policy_excluded");
    auto* scopePage=new QWidget(tabs); auto* scope=new QFormLayout(scopePage);tabs->addTab(scopePage,QStringLiteral("适用范围与来源"));
    auto line=[&](const char* name,const QString& label,const QString& value) {
        auto* field=new QLineEdit(value,scopePage);field->setObjectName(name);scope->addRow(label,field);return field;
    };
    std::vector<QCheckBox*> modes;
    const QStringList modeNames{"Preview","Quick","Verified"};
    for(int i=0;i<3;++i) {
        auto* field=new QCheckBox(modeNames[i],scopePage);field->setObjectName("policy_mode_"+QString::number(i));
        field->setChecked(std::find(input.applicability.modes.begin(),input.applicability.modes.end(),static_cast<core::EvaluationMode>(i))!=input.applicability.modes.end());
        scope->addRow(field);modes.push_back(field);
    }
    auto* models=line("policy_models",QStringLiteral("适用模型ID（空＝不限）"),objectText(input.applicability.modelObjects));
    auto* tasks=line("policy_tasks",QStringLiteral("适用任务ID（空＝不限）"),objectText(input.applicability.taskObjects));
    auto* cases=line("policy_cases",QStringLiteral("适用工况ID（空＝不限）"),objectText(input.applicability.caseObjects));
    auto* origin=new QComboBox(scopePage);origin->setObjectName("policy_origin");
    origin->addItems({QStringLiteral("模板"),QStringLiteral("导入"),QStringLiteral("用户编辑"),QStringLiteral("系统缺省")});
    origin->setCurrentIndex(static_cast<int>(input.origin.kind));scope->addRow(QStringLiteral("来源类别"),origin);
    auto* sourceObject=line("policy_source_object",QStringLiteral("来源对象ID（可留空）"),input.origin.sourceObject?QString::fromStdString(input.origin.sourceObject->toCanonical()):QString{});
    auto* note=line("policy_note",QStringLiteral("来源备注"),input.origin.note?QString::fromStdString(*input.origin.note):QString{});
    auto* anchor=line("policy_anchor",QStringLiteral("数值契约锚（受保护）"),QString::fromStdString(input.numericContractAnchor));anchor->setReadOnly(true);
    auto* compatibility=line("policy_compatibility",QStringLiteral("兼容注记（受保护）"),input.compatibilityNotes?QString::fromStdString(*input.compatibilityNotes):QString{});compatibility->setReadOnly(true);
    auto* error=new QLabel(&dialog);error->setObjectName("policy_error");error->setWordWrap(true);error->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(error);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        try {
            if(store!=m_store || store->closed() || !store->writable() || store->query().head().id!=base.id
                || store->query().head().branch!=base.branch) {
                error->setText(QStringLiteral("项目已关闭、切换、只读或基线已改变；未提交，请取消后重开编辑器"));return;
            }
            auto candidate=input; // 从原完整对象复制，受保护字段及未知管理信息不丢失。
            candidate.collision.enabled=collision->isChecked();candidate.collision.enabledDomains.clear();
            for(int i=0;i<4;++i) if(domains[i]->isChecked())candidate.collision.enabledDomains.push_back(static_cast<policy::CollisionDomain>(i));
            candidate.collision.safetyClearance=clearance.read();candidate.collision.excludeAdjacentLinksByDefault=adjacent->isChecked();
            candidate.collision.mandatoryPairs=mandatory.read();candidate.collision.excludedPairs=excluded.read();
            candidate.jointThresholds.travelLimitCheckEnabled=travelEnabled->isChecked();
            candidate.jointThresholds.finiteRotationTravelLimit=travel.read();candidate.jointThresholds.nearLimitRatio=nearLimit.read();candidate.jointThresholds.conditionNumberWarning=condition.read();
            candidate.applicability.modes.clear();for(int i=0;i<3;++i)if(modes[i]->isChecked())candidate.applicability.modes.push_back(static_cast<core::EvaluationMode>(i));
            candidate.applicability.modelObjects=readObjects(models);candidate.applicability.taskObjects=readObjects(tasks);candidate.applicability.caseObjects=readObjects(cases);
            candidate.origin.kind=static_cast<policy::PolicyOriginKind>(origin->currentIndex());
            const auto ids=readObjects(sourceObject);if(ids.size()>1)throw std::invalid_argument("来源对象最多一个");
            candidate.origin.sourceObject=ids.empty()?std::nullopt:std::optional<core::ObjectId>{ids.front()};
            candidate.origin.note=note->text().isEmpty()?std::nullopt:std::optional<std::string>{note->text().toStdString()};
            RevisionValidationContext validation(store->query(),base);
            const auto checked=policy::resolvePolicy(candidate,validation);
            if(!checked.policy) {
                QStringList details;
                for(const auto& d:checked.diagnostics)details.push_back(QString::fromStdString(d.context+"："+d.cause+"；"+d.recommendedAction));
                error->setText(details.join("\n"));
                // 诊断中的字段路径由policy提供，UI只负责把焦点带到对应页，不重新判策略。
                const auto text=details.join(" ");
                if(text.contains("mandatory")||text.contains("excluded")||text.contains("规则"))tabs->setCurrentIndex(1);
                else if(text.contains("applicability")||text.contains("适用"))tabs->setCurrentIndex(3);
                else tabs->setCurrentIndex(0);
                return;
            }
            const auto result=submit(*store,base,candidate,!ref);
            if(result.committed()) { dialog.accept();return; }
            QStringList details;for(const auto& d:result.diagnostics)details.push_back(QString::fromStdString(d.context+"："+d.cause));
            error->setText(details.isEmpty()?QStringLiteral("命令未提交，项目可能已只读或基线过期"):details.join("\n"));
        } catch(const std::exception& failure) {
            // 用户草稿的编码/解析错误允许原位修正；异常原文可见，不关闭表单伪装成功。
            error->setText(QString::fromUtf8(failure.what()));
        }
    });
    dialog.exec();
}

policy::PolicyResolution HostEngineeringPolicy::resolvePolicyForRevision(
    const project::RevisionView& revision) const
{
    if (m_revisionResolver) {
        return m_revisionResolver(revision);
    }
    policy::PolicyResolution result;
    result.policy.emplace(m_policy);
    return result;
}

}  // namespace sdurws::ird::ui
