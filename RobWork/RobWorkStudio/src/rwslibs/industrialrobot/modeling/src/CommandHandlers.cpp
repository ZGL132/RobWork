/**
 * @file   CommandHandlers.cpp
 * @brief  建模命令处理器族实现——AssertionSuite 断言分域、命令载荷编解码、
 *         prepare 公共管线与五命令钩子（units/modeling.md §9.3/§9.4.4/
 *         §9.4.8/§9.5 T08 行的落位面；任务契约 WP-13-T08）。
 *
 * 实现纪律（文件级总注）：
 *   - 产码唯一经 AssertionSuite（码常量唯一书写点＝DiagCodes.hpp——禁字
 *     符串拼码，§9.5 尾段）；全部诊断记录经 core::DiagnosticRecord::make
 *     （C-3 校验：码句法＋context/cause/recommendedAction 非空）。
 *   - 判定数学零重写：惯量特征值唯一来自 src/InertiaMath.hpp（I-MDL-5
 *     单一实现点）；行程比较唯一来自 policy::JointLimitEvaluator（POL-T08
 *     唯一实现；阈值唯一来源＝EngineeringPolicySet.jointThresholds——
 *     本文件无任何 4π/阈值字面量，ARC-05/NFR-MNT-07）。
 *   - 确定性（NFR-COR-02）：全部函数纯或只读；数值文本格式化固定
 *     %.17g（policy 侧同口径）；无时钟/locale/环境读取。
 */

#include <sdurws/ird/modeling/CommandHandlers.hpp>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include <sdurws/ird/modeling/Codec.hpp>       // RobotDesignCodec（对象字节解码门/写入编码）
#include "InertiaMath.hpp"  // 单元私有：惯量 SPD＋三角不等式特征值单一实现（I-MDL-5——R-2 src/ 私有头）

namespace sdurws::ird::modeling {

namespace {

// =====================================================================
// 内部小工具（确定性诊断文本/数值格式化——无 locale）
// =====================================================================

/// 数值的稳定文本形态（%.17g——位级可往返；诊断 comparison 伴随文案用）。
std::string formatDouble(double v)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

/// comparison 单侧值构造（数值＋单位 token——UX-03 三要素的数据面）。
core::ComparativeValue comparativeValue(double v, std::string_view unitSymbol)
{
    core::ComparativeValue out;
    // 数值侧来源＝derived-readonly（就绪/断言派生的只读事实——非用户输入，
    // 非估算；methodTag 标记产码面，供报告侧溯源）。
    out.quantity = core::SourcedValue<double>::provided(
        v, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                       std::nullopt, std::nullopt,
                                       std::string("mdl-assertion")));
    const auto unit = core::UnitToken::find(unitSymbol);
    // 单位 token 取自 core 注册表（kg/kg*m^2/rad/m 均已注册——找不到＝
    // 实现缺陷，fail-fast 不伪造"无单位"）。
    if (!unit.has_value()) {
        throw std::logic_error("mdl: 未注册单位 token（实现缺陷）: "
                               + std::string(unitSymbol));
    }
    out.unit = *unit;
    return out;
}

/// comparison 字段组装（actual/expected 两侧）。
core::ComparativeFields comparison(double actual, std::string_view actualUnit,
                                   double expected, std::string_view expectedUnit)
{
    core::ComparativeFields f;
    f.actual = comparativeValue(actual, actualUnit);
    f.expected = comparativeValue(expected, expectedUnit);
    return f;
}

/// 诊断记录组装（码常量＋定位三要素＋中文语义文案——全部产码点的汇聚）。
core::DiagnosticRecord makeRecord(const std::string_view code,
                                  const core::ObjectId& subject,
                                  const std::string& localName,
                                  std::string context,
                                  std::string cause,
                                  std::string recommendedAction,
                                  std::optional<core::ComparativeFields> cmp = std::nullopt)
{
    // localName 空串＝对象无局部名口径（如根对象）→nullopt（不伪造空名）。
    std::optional<std::string> name;
    if (!localName.empty()) { name = localName; }
    return core::DiagnosticRecord::make(std::string(code), subject, name,
                                        std::nullopt, std::move(context),
                                        std::move(cause), std::move(recommendedAction),
                                        std::move(cmp));
}

/// 物性字段的可读定位（诊断 context 用——"links[2]（bodyName）"形态）。
std::string subjectContext(const std::string& kind, const std::string& localName)
{
    if (localName.empty()) { return kind; }
    return kind + "「" + localName + "」";
}

/// 载荷槽字节解码（token 路由到五变体；解码门校验链失败＝nullopt）。
std::optional<ObjectVariant> decodeSlotObject(const PayloadObjectSlot& slot)
{
    RobotDesignCodec codec;  // 无状态实现——按调用构造（确定性等价）
    auto decoded = codec.decode(slot.objectBytes, kCurrentFormatVersion);
    if (!decoded.ok()) { return std::nullopt; }
    return decoded.get();  // 值拷贝（fresh 解码产物——Expected 无移动取出面）
}

/// 类型化对象编码（回填后的候选值→canonical 字节；编码仅版本可拒——
/// 版本恒当前，失败即实现缺陷，fail-fast）。
std::vector<std::uint8_t> encodeObjectOrThrow(const ObjectVariant& object)
{
    RobotDesignCodec codec;
    auto encoded = codec.encode(object, kCurrentFormatVersion);
    if (!encoded.ok()) {
        throw std::logic_error("mdl: 候选对象编码失败（实现缺陷）: "
                               + encoded.error().detail);
    }
    return encoded.get();  // 值拷贝（fresh 编码产物）
}

/// 钩子通用"无效输入"产出（域结构校验失败——RejedInvalidInput 透传）。
DecodeOutcome invalidInput()
{
    DecodeOutcome out;
    out.outcome = project::PrepareOutcome::RejectedInvalidInput;
    return out;
}

}  // namespace

// =====================================================================
// AssertionSuite——断言分域（唯一产码点）
// =====================================================================

AssertionSuite::AssertionSuite(Ports ports) noexcept : m_ports(ports) {}

void AssertionSuite::assertBodyPhysical(const core::ObjectId& subject,
                                        const std::string& localName,
                                        const BodyData& body,
                                        std::vector<core::DiagnosticRecord>& blockers,
                                        std::vector<core::DiagnosticRecord>& missingWarnings) const
{
    // ---- 断言①：已提供质量 m>0（缺失不触发——NotProvided 走预告）----
    const auto mass = body.mass.tryValue();
    if (mass.has_value()) {
        if (!(*mass > 0.0)) {
            // 就地阻止＋精确定位：subject=ObjectId＋localName＋比较型三要素
            //（actual=m、expected=0、单位 kg）——DTB 禁止项的对位实现。
            blockers.push_back(makeRecord(
                kMdlAssertMassNonpositive, subject, localName,
                subjectContext("物性断言①（质量）", localName),
                "已提供质量 m=" + formatDouble(*mass) + " kg ≤ 0（MDL-06 断言①）",
                "修正质量为正值，或清空为缺失（缺失走 DataInsufficient 降级）",
                comparison(*mass, "kg", 0.0, "kg")));
        }
    } else if (body.mass.state() == core::FieldState::NotProvided) {
        missingWarnings.push_back(makeRecord(
            kMdlReadinessPhysicsMissing, subject, localName,
            subjectContext("物性缺失预告（质量）", localName),
            "质量未提供——动力学评估将走 DataInsufficient 降级（V15-01，不阻断应用）",
            "补全质量，或接受降级（正式动力学结论将标注数据不足）"));
    }
    // Invalid 态（已提供但非法原串）由构造/解码边界拒绝——不到达本层
    //（解码门 I-MDL-3/编码 presence 校验），不在此重复判定（NFR-MNT-04）。

    // ---- 断言②③：已提供惯量 SPD＋三角不等式（InertiaMath 单一实现）----
    const auto inertia = body.inertia.tryValue();
    if (inertia.has_value()) {
        // 特征值数学唯一来自 inertiamath（与 I-MDL-5 不变量同源——无第二实现）。
        const std::array<double, 3> eig = inertiamath::symmetricEigenvalues3x3(*inertia);
        if (!(eig[0] > 0.0)) {
            // SPD 违例：不继续三角判定（非正定张量无椭球语义——与不变量
            // 层同口径，避免一个张量报两条无关违例干扰修正回路）。
            blockers.push_back(makeRecord(
                kMdlAssertInertiaNotSpd, subject, localName,
                subjectContext("物性断言②（惯量正定）", localName),
                "惯量非对称正定：最小特征值 λmin=" + formatDouble(eig[0])
                    + " kg·m² ≤ 0（对称化后）",
                "修正惯量张量（六分量）为对称正定",
                comparison(eig[0], "kg*m^2", 0.0, "kg*m^2")));
        } else if (eig[2] > eig[1] + eig[0]) {
            // 三角不等式（严格比较——解析特征值无浮点放宽，与不变量层同口径）。
            blockers.push_back(makeRecord(
                kMdlAssertInertiaTriangle, subject, localName,
                subjectContext("物性断言③（惯量三角）", localName),
                "惯性椭球三角不等式不满足：λmax=" + formatDouble(eig[2])
                    + " ＞ λmid+λmin=" + formatDouble(eig[1] + eig[0]) + "（kg·m²）",
                "修正惯量张量（六分量）满足 λmax ≤ λmid＋λmin",
                comparison(eig[2], "kg*m^2", eig[1] + eig[0], "kg*m^2")));
        }
    } else if (body.inertia.state() == core::FieldState::NotProvided) {
        missingWarnings.push_back(makeRecord(
            kMdlReadinessPhysicsMissing, subject, localName,
            subjectContext("物性缺失预告（惯量）", localName),
            "惯量张量未提供——动力学评估将走 DataInsufficient 降级（V15-01，不阻断应用）",
            "补全惯量（或经物性估算 mdl-property-formula/1 估算），或接受降级"));
    }
}

void AssertionSuite::assertJointLimitIntervals(const RobotDesign& design,
                                               std::vector<core::DiagnosticRecord>& blockers) const
{
    for (std::size_t i = 0; i < design.joints.size(); ++i) {
        const JointEntry& joint = design.joints[i];
        const std::string unit = (joint.type == JointType::Prismatic) ? "m" : "rad";
        if (joint.type == JointType::Revolute || joint.type == JointType::Prismatic) {
            // 断言④前半：已提供限位的区间有序性（qmin<qmax；单位随类型）。
            const auto bounds = joint.bounds.tryValue();
            if (bounds.has_value() && !(bounds->first < bounds->second)) {
                blockers.push_back(makeRecord(
                    kMdlAssertLimitInterval, joint.objectId, joint.localName,
                    "限位断言④（区间）joints[" + std::to_string(i) + "].bounds",
                    "qmin=" + formatDouble(bounds->first) + " ≥ qmax="
                        + formatDouble(bounds->second) + " " + unit,
                    "修正限位区间（qmin＜qmax）",
                    comparison(bounds->first, unit, bounds->second, unit)));
            }
            // 缺失 bounds 不是本断言面（§8.2 L4 仅列区间错/未确认两 Blocking
            // 分支；待确认面归导入域 MDL-IMPORT-PENDING-CONFIRM）。
        } else if (joint.type == JointType::Continuous) {
            // 断言④后半＋MDL-12：工程工作范围——未确认（NotProvided）或
            // 已提供但非有限区间（端点非有限/min≥max）→阻断；已确认有限
            // 范围→通过（豁免限位与行程断言——continuous 无 bounds）。
            const auto range = joint.workingRange.tryValue();
            if (!range.has_value()) {
                if (joint.workingRange.state() == core::FieldState::NotProvided) {
                    blockers.push_back(makeRecord(
                        kMdlAssertRangeNotFinite, joint.objectId, joint.localName,
                        "范围断言④（continuous）joints[" + std::to_string(i)
                            + "].workingRange",
                        "工程工作范围未确认（未提供）——多圈关节须先确认有限工作范围（MDL-12）",
                        "确认并填写工程工作范围（rad，qmin'＜qmax'）"));
                }
                // NotApplicable/Invalid 态由构造/解码边界拒绝——不到达本层。
            } else {
                const bool finiteEnds = std::isfinite(range->first)
                                     && std::isfinite(range->second);
                if (!finiteEnds || !(range->first < range->second)) {
                    blockers.push_back(makeRecord(
                        kMdlAssertRangeNotFinite, joint.objectId, joint.localName,
                        "范围断言④（continuous）joints[" + std::to_string(i)
                            + "].workingRange",
                        "工程工作范围非有限区间：[" + formatDouble(range->first) + ", "
                            + formatDouble(range->second) + "] rad（MDL-12）",
                        "确认为有限区间（端点有限且 qmin'＜qmax'）",
                        comparison(range->first, "rad", range->second, "rad")));
                }
            }
        }
        // Fixed：无限位/范围语义（§4.3-A）——不检查。
    }
}

AssertionSuite::TravelEvaluation AssertionSuite::evaluateTravelLimits(
    const RobotDesign& design,
    const policy::EngineeringPolicySet& policy,
    std::vector<core::ConfirmableFinding>& confirmables,
    std::vector<core::DiagnosticRecord>& evalDiagnostics) const
{
    // 装配契约：评估器端口与名称上下文必须就绪（R-4：名称映射装配注入）。
    if (m_ports.jointLimitEvaluator == nullptr || m_ports.nameContext == nullptr) {
        throw std::logic_error("mdl: 行程校验端口未装配（实现/装配缺陷）");
    }

    // ---- 关节表装配（仅旋转族——行程阈值"仅适用有限限位旋转关节"，policy §4.4）----
    policy::JointLimitQuery query;
    query.joints.reserve(design.joints.size());
    for (const JointEntry& joint : design.joints) {
        if (joint.type == JointType::Revolute) {
            policy::JointLimitSpec spec;
            spec.jointObject = joint.objectId;
            spec.localName = joint.localName;
            spec.isContinuous = false;
            if (const auto bounds = joint.bounds.tryValue(); bounds.has_value()) {
                spec.qMin = bounds->first;    // SI rad（有限限位）
                spec.qMax = bounds->second;   // SI rad
            }
            // 缺失 bounds＝"未声明限位的有限关节"——评估器按全不适用口径。
            query.joints.push_back(std::move(spec));
        } else if (joint.type == JointType::Continuous) {
            // continuous：行程豁免，但须带已确认工程范围（装配契约——缺失
            // 到达此处的唯一可能＝调用方未先跑 assertJointLimitIntervals，
            // 按类注 @pre 属实现缺陷，fail-fast 不伪造事实）。
            const auto range = joint.workingRange.tryValue();
            if (!range.has_value()) {
                throw std::logic_error(
                    "mdl: continuous 关节缺已确认工作范围即进入行程校验（@pre 违约）");
            }
            policy::JointLimitSpec spec;
            spec.jointObject = joint.objectId;
            spec.localName = joint.localName;
            spec.isContinuous = true;
            spec.engineeringRange = *range;   // SI rad，first＜second（断言层已保证）
            query.joints.push_back(std::move(spec));
        }
        // Prismatic/Fixed：排除（行程阈值不适用）。
    }
    if (query.joints.empty()) {
        return TravelEvaluation::NoExceeded;  // 无可评估关节——无产出
    }
    // 构型表＝单行全零（行程是限位派生事实，与构型无关；评估器的裕量/
    // 违例面在本场景非消费目标；至少 1 构型＝查询契约）。
    query.configurations.emplace_back(query.joints.size(), 0.0);

    // ---- 评估（阈值唯一来源＝policy.jointThresholds——本文件无第二常量）----
    const policy::JointLimitEvaluation evaluation =
        m_ports.jointLimitEvaluator->evaluate(query, policy, *m_ports.nameContext);

    if (evaluation.status != policy::CollisionEvaluationStatus::Completed
        || !evaluation.finalized) {
        // 评估未终态化（名称不可解析等——POLICY-CLL-*）：行程合法性未确认
        // ＝不得放行；诊断透传，调用方据此阻断（不静默跳过）。
        evalDiagnostics.insert(evalDiagnostics.end(), evaluation.diagnostics.begin(),
                               evaluation.diagnostics.end());
        return TravelEvaluation::EvaluationFailed;
    }

    // ---- TravelLimitExceeded → MDL-06-TRAVEL-LIMIT ConfirmableFinding ----
    bool produced = false;
    for (const policy::JointLimitFinding& finding : evaluation.findings) {
        if (finding.kind != policy::JointLimitFindingKind::TravelLimitExceeded) {
            // 区间/范围类发现在断言层已就地阻断（短路优先级）——此处不再
            // 重复产出（NFR-MNT-04 无重复判定）；NearLimit/LimitViolated
            // 属 kinematics 消费面（硬过滤素材），不构成本命令的确认项。
            continue;
        }
        produced = true;
        core::DiagnosticRecord record = makeRecord(
            kMdl06TravelLimit, finding.jointObject, finding.localName,
            subjectContext("行程上限校验（MDL-06④）", finding.localName),
            "有限限位旋转关节实际行程 T=" + formatDouble(finding.actualValue)
                + " rad ＞ 策略阈值 L=" + formatDouble(finding.thresholdValue)
                + " rad（policyContentId=" + policy.contentIdentity.toCanonical()
                + "；T＞L 才超限——边界含于合规侧）",
            "确认放行（SA-15——凭据与绑定四元组随命令摘要留痕），或将行程调整至阈值内",
            comparison(finding.actualValue, "rad", finding.thresholdValue, "rad"));
        // 可确认发现：C-1（record 必为比较型）由工厂强制——comparison 已携。
        confirmables.push_back(core::ConfirmableFinding::make(std::move(record)));
    }
    return produced ? TravelEvaluation::FindingsProduced : TravelEvaluation::NoExceeded;
}

void AssertionSuite::assertClosureReferences(const RobotDesign& design,
                                             const ModelingWorkingSet& closureView,
                                             std::vector<core::DiagnosticRecord>& blockers) const
{
    // 闭包视图查找面（对象身份→类型 token 匹配）；根对象身份不计入部件表。
    auto tokenOf = [&closureView](const core::ObjectId& oid) -> std::optional<std::string_view> {
        for (const ToolDefinition& t : closureView.toolObjects) {
            if (t.objectId == oid) { return kToolDefinitionObjectType; }
        }
        for (const SceneObject& s : closureView.sceneObjects) {
            if (s.objectId == oid) { return kSceneObjectObjectType; }
        }
        if (closureView.poseSetObject.has_value()
            && closureView.poseSetObject->objectId == oid) {
            return kNamedPoseSetObjectType;
        }
        if (closureView.drivetrainObject.has_value()
            && closureView.drivetrainObject->objectId == oid) {
            return kRobotDrivetrainObjectType;
        }
        return std::nullopt;
    };

    // 逐引用核对：存在＋token 匹配（I-MDL-9 闭包半段——值模型层无闭包
    // 上下文，§4.10 范围注记归本断言）。subject＝被引对象身份（精确定位
    // 到缺失引用）。
    auto checkRef = [&](const core::ObjectId& oid, std::string_view expectToken,
                        std::string_view tableName) {
        const auto token = tokenOf(oid);
        if (!token.has_value()) {
            blockers.push_back(makeRecord(
                kMdlReadinessRefMissing, oid, std::string(),
                std::string("引用完整性（") + std::string(tableName) + "）",
                "引用对象不在闭包（obj-" + oid.toCanonical().substr(4) + " 无对应对象）",
                "修复引用（指向闭包内存在对象）或移除该引用"));
            return;
        }
        if (*token != expectToken) {
            blockers.push_back(makeRecord(
                kMdlReadinessRefMissing, oid, std::string(),
                std::string("引用完整性（") + std::string(tableName) + "）",
                "引用对象类型 token 不匹配：期望 " + std::string(expectToken)
                    + "，实际 " + std::string(*token),
                "修复引用（指向同类型闭包对象）"));
        }
    };
    for (const core::ObjectId& oid : design.toolRefs) {
        checkRef(oid, kToolDefinitionObjectType, "toolRefs");
    }
    for (const core::ObjectId& oid : design.sceneRefs) {
        checkRef(oid, kSceneObjectObjectType, "sceneRefs");
    }
    if (design.poseSetRef.has_value()) {
        checkRef(*design.poseSetRef, kNamedPoseSetObjectType, "poseSetRef");
    }
    if (design.drivetrainRef.has_value()) {
        checkRef(*design.drivetrainRef, kRobotDrivetrainObjectType, "drivetrainRef");
    }
}

void AssertionSuite::checkResourceStates(const RobotDesign& design,
                                         std::vector<core::DiagnosticRecord>& warnings) const
{
    for (std::size_t i = 0; i < design.resourceManifest.size(); ++i) {
        const ResourceRef& res = design.resourceManifest[i];
        if (res.state != ResourceState::Recorded) {
            continue;  // Solidified→通过（CON-03 固化激励——免复查）
        }
        // Recorded→未固化提示（Warning 不阻断；缺失/变化探测归 io 护栏与
        // runtime 编译复核——就绪层不读文件系统，只承载状态机事实）。
        warnings.push_back(makeRecord(
            kMdlReadinessResourceState, core::ObjectId{}, std::string(),
            "资源状态（resourceManifest[" + std::to_string(i) + "]）",
            "资源 " + res.resourceId + " 处于 Recorded（未固化）——外部文件可能缺失/变化",
            "重关联外部资源或执行固化（CON-03——固化后免复查）"));
        // 说明：本码的稳定 subject 语义＝受影响资源；资源条目以字符串 id
        // 编址（非 ObjectId）——subject 以空承载，定位经 context（core C-3
        // 允许瞬时面 subject 为空；本码 Warning 不入命令阻断面）。
    }
}

void AssertionSuite::assertSchemaVersions(const ModelingWorkingSet& ws,
                                          std::vector<core::DiagnosticRecord>& blockers) const
{
    // 主版本≠当前支持值即拒绝（NFR-DEP-04：大于＝未来版本不猜测、小于＝
    // 无升级器不可读）。解码门对入存字节已强制同一谓词——本断言为携带
    // 内存构造对象的工作集（模板/编辑器路径）的防御面。
    auto checkOne = [&](std::uint32_t version, std::uint32_t supported,
                        std::string_view objectType,
                        const std::optional<core::ObjectId>& subject) {
        if (version == supported) { return; }
        std::string cause = "对象 schema 主版本 " + std::to_string(version)
            + " 超出本程序支持（当前 " + std::to_string(supported) + "）";
        blockers.push_back(makeRecord(
            kMdlReadinessSchemaUnsupported,
            subject.value_or(core::ObjectId{}), std::string(),
            std::string("schema 版本（") + std::string(objectType) + "）",
            std::move(cause), "升级程序或重新编辑（无自动升级器——NFR-DEP-04）"));
    };
    checkOne(ws.design.schemaVersion, kRobotDesignSchemaVersion,
             kRobotDesignObjectType, ws.rootObjectId);
    for (const ToolDefinition& t : ws.toolObjects) {
        checkOne(t.schemaVersion, kToolDefinitionSchemaVersion, kToolDefinitionObjectType,
                 t.objectId);
    }
    for (const SceneObject& s : ws.sceneObjects) {
        checkOne(s.schemaVersion, kSceneObjectSchemaVersion, kSceneObjectObjectType,
                 s.objectId);
    }
    if (ws.poseSetObject.has_value()) {
        checkOne(ws.poseSetObject->schemaVersion, kNamedPoseSetSchemaVersion,
                 kNamedPoseSetObjectType, ws.poseSetObject->objectId);
    }
    if (ws.drivetrainObject.has_value()) {
        checkOne(ws.drivetrainObject->schemaVersion, kRobotDrivetrainSchemaVersion,
                 kRobotDrivetrainObjectType, ws.drivetrainObject->objectId);
    }
}

// =====================================================================
// 命令载荷编解码（确定性——字段定序、小端长度前缀、无填充）
// =====================================================================

namespace {

/// 载荷 magic（"IRDMCP1"——modeling command payload v1；版本演进随
/// kCommandPayloadVersion 与 magic 尾数字同步）。
constexpr std::uint8_t kPayloadMagic[7] = {'I', 'R', 'D', 'M', 'C', 'P', '1'};

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

void putBytes(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t n)
{
    putU32(out, static_cast<std::uint32_t>(n));
    out.insert(out.end(), data, data + n);
}

void putString(std::vector<std::uint8_t>& out, std::string_view s)
{
    putBytes(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

/// 严格读取的游标视图（越界即失败——不产出半成品）。
struct PayloadReader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;

    bool readU32(std::uint32_t* out)
    {
        if (pos + 4 > size) { return false; }
        *out = static_cast<std::uint32_t>(data[pos])
             | (static_cast<std::uint32_t>(data[pos + 1]) << 8)
             | (static_cast<std::uint32_t>(data[pos + 2]) << 16)
             | (static_cast<std::uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return true;
    }
    bool readBytes(std::vector<std::uint8_t>* out)
    {
        std::uint32_t n = 0;
        if (!readU32(&n)) { return false; }
        if (pos + n > size) { return false; }  // 截断/越界——整体失败
        out->assign(data + pos, data + pos + n);
        pos += n;
        return true;
    }
    bool readString(std::string* out)
    {
        std::vector<std::uint8_t> raw;
        if (!readBytes(&raw)) { return false; }
        out->assign(raw.begin(), raw.end());
        return true;
    }
    bool atEnd() const { return pos == size; }
};

}  // namespace

std::vector<std::uint8_t> encodeCommandPayload(const CommandPayload& payload)
{
    std::vector<std::uint8_t> out;
    out.reserve(64 + payload.objects.size() * 32);
    out.insert(out.end(), kPayloadMagic, kPayloadMagic + sizeof(kPayloadMagic));
    putU32(out, kCommandPayloadVersion);
    putU32(out, payload.mode == CommandPayload::Mode::Restore ? 1u : 0u);
    putU32(out, static_cast<std::uint32_t>(payload.objects.size()));
    for (const PayloadObjectSlot& slot : payload.objects) {
        out.push_back(slot.allocateNew ? 1u : 0u);
        const std::string oid = slot.objectId.toCanonical();  // 全零→"obj-00..0"保留文本
        putString(out, oid);
        putString(out, slot.objectTypeToken);
        putBytes(out, slot.objectBytes.data(), slot.objectBytes.size());
    }
    return out;
}

std::optional<CommandPayload> tryDecodeCommandPayload(const std::vector<std::uint8_t>& bytes)
{
    // 框架完整性：magic/长度逐字节核对（任一失败＝nullopt，不猜测）。
    if (bytes.size() < sizeof(kPayloadMagic) + 12) { return std::nullopt; }
    for (std::size_t i = 0; i < sizeof(kPayloadMagic); ++i) {
        if (bytes[i] != kPayloadMagic[i]) { return std::nullopt; }
    }
    PayloadReader reader{bytes.data(), bytes.size(), sizeof(kPayloadMagic)};
    CommandPayload payload;
    std::uint32_t version = 0;
    std::uint32_t mode = 0;
    std::uint32_t count = 0;
    if (!reader.readU32(&version) || !reader.readU32(&mode) || !reader.readU32(&count)) {
        return std::nullopt;
    }
    // 版本不受理（NFR-DEP-04——旧版本拒绝并给升级指引；升级指引由拒绝
    // 语义承载：处理器受理集合＝{kCommandPayloadVersion}）。
    if (version != kCommandPayloadVersion) { return std::nullopt; }
    if (mode > 1u) { return std::nullopt; }
    payload.mode = (mode == 1u) ? CommandPayload::Mode::Restore : CommandPayload::Mode::Apply;
    payload.objects.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        PayloadObjectSlot slot;
        std::uint32_t allocate = 0;
        if (reader.pos >= reader.size) { return std::nullopt; }
        allocate = reader.data[reader.pos++];
        if (allocate > 1u) { return std::nullopt; }
        slot.allocateNew = (allocate == 1u);
        std::string oidText;
        if (!reader.readString(&oidText)) { return std::nullopt; }
        // 身份文本严格解析（tag/长度/字符集——含全零保留文本）。
        auto oid = core::ObjectId::tryFromCanonical(oidText);
        if (!oid.has_value()) { return std::nullopt; }
        slot.objectId = *oid;
        if (!reader.readString(&slot.objectTypeToken)) { return std::nullopt; }
        // token 词表核对（五对象之外＝无效载荷——路由失联面）。
        if (slot.objectTypeToken != kRobotDesignObjectType
            && slot.objectTypeToken != kToolDefinitionObjectType
            && slot.objectTypeToken != kSceneObjectObjectType
            && slot.objectTypeToken != kNamedPoseSetObjectType
            && slot.objectTypeToken != kRobotDrivetrainObjectType) {
            return std::nullopt;
        }
        if (!reader.readBytes(&slot.objectBytes)) { return std::nullopt; }
        payload.objects.push_back(std::move(slot));
    }
    if (!reader.atEnd()) { return std::nullopt; }  // 尾随字节＝破损
    return payload;
}

// =====================================================================
// IModelingCommandHandler——prepare 公共管线
// =====================================================================

IModelingCommandHandler::IModelingCommandHandler(HandlerServices services,
                                                 bool requiresDualCompile) noexcept
    : m_services(std::move(services)),
      m_suite(m_services.assertionPorts),   // 共用断言面（NFR-MNT-04——声明序在 m_services 后）
      m_requiresDualCompile(requiresDualCompile)
{}

std::uint32_t IModelingCommandHandler::currentPayloadVersion() const
{
    return kCommandPayloadVersion;  // 受理集合＝{当前版本}（PRJ-T10 口径）
}

namespace {

/// 基线闭包的一个建模对象条目（oid/token/原始字节——inverse 快照素材，
/// D-MDL-9：逆载荷＝受影响对象前一 (oid,cv) canonical 字节集）。
struct BaselineEntry {
    core::ObjectId oid;
    std::string token;
    std::vector<std::uint8_t> bytes;
};

/// 基线重建的完整产物（工作集视图＋原始字节表）。
struct BaselineSnapshot {
    ModelingWorkingSet ws;
    std::vector<BaselineEntry> entries;  // 闭包内全部 modeling 对象（含根）
};

/**
 * @brief 基线工作集重建（§9.3"重建基线工作集（expectedRevision 闭包→
 *        reader 同源解码）"＋防御性复核）。
 *
 * 逐闭包对象：token 路由到五对象值（RobotDesignCodec 同源解码）；非
 * modeling token（元数据/策略等）跳过。字节取数走 ctx.query().object()
 * 强语义（引用由 baseSnapshot 保证存在；摘要校验失败/关闭＝数据侧异常
 * 透传——不吞不改）。
 *
 * @throws std::invalid_argument 防御性复核失败：envelope.expectedRevision
 *         与 baseSnapshot.id 不一致（S2 已拦截过期基线——到达即调用方
 *         契约违约，fail-fast）
 * @throws std::logic_error 基线对象字节解码失败（摘要校验过的存储字节
 *         不可解码＝数据损坏/实现缺陷——防线纵深断言）
 * @throws StoreError ctx.query().object() 的数据侧异常（透传）
 */
BaselineSnapshot rebuildBaseline(project::HandlerContext& ctx,
                                 const project::CommandEnvelope& envelope,
                                 const project::RevisionView& baseSnapshot)
{
    // ---- 防御性复核（expectedRevision==基线——S2 已拦截，此处为纵深）----
    if (envelope.expectedRevision.has_value()
        && !(*envelope.expectedRevision == baseSnapshot.id)) {
        throw std::invalid_argument(
            "mdl: prepare 基线防御性复核失败——expectedRevision 与 baseSnapshot 不一致"
            "（stale-revision 应由 project S2 拦截；直接调用 prepare 须遵守 §5.3.2 契约）");
    }

    BaselineSnapshot snap;
    RobotDesignCodec codec;
    for (const project::ObjectRef& ref : baseSnapshot.objectRefs) {
        const std::string_view token = ref.objectTypeToken;
        const bool isModeling =
            token == kRobotDesignObjectType || token == kToolDefinitionObjectType
            || token == kSceneObjectObjectType || token == kNamedPoseSetObjectType
            || token == kRobotDrivetrainObjectType;
        if (!isModeling) { continue; }  // 元数据/策略等非 modeling 对象——跳过

        // 强语义取数＋同源解码（reader 同源——§9.3 原文；解码失败＝数据
        // 损坏面，fail-fast——不产出半成品基线）。
        std::vector<std::uint8_t> bytes = ctx.query().object(ref.objectId, ref.contentVersion);
        auto decoded = codec.decode(bytes, kCurrentFormatVersion);
        if (!decoded.ok()) {
            throw std::logic_error("mdl: 基线对象解码失败（数据损坏面）: "
                                   + ref.objectId.toCanonical() + " — "
                                   + decoded.error().detail);
        }
        ObjectVariant value = decoded.get();  // 值拷贝（fresh 解码产物）

        BaselineEntry entry;
        entry.oid = ref.objectId;
        entry.token = ref.objectTypeToken;
        entry.bytes = std::move(bytes);
        snap.entries.push_back(std::move(entry));

        // token 路由进工作集视图（恰一根——多根＝存储违约，同上 fail-fast）。
        std::visit(
            [&](auto& typed) {
                using T = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<T, RobotDesign>) {
                    if (snap.ws.rootObjectId.has_value()) {
                        throw std::logic_error("mdl: 基线闭包含多个根对象（存储违约）");
                    }
                    snap.ws.rootObjectId = ref.objectId;
                    snap.ws.design = typed;
                } else if constexpr (std::is_same_v<T, ToolDefinition>) {
                    snap.ws.toolObjects.push_back(typed);
                } else if constexpr (std::is_same_v<T, SceneObject>) {
                    snap.ws.sceneObjects.push_back(typed);
                } else if constexpr (std::is_same_v<T, PoseSet>) {
                    if (snap.ws.poseSetObject.has_value()) {
                        throw std::logic_error("mdl: 基线闭包含多个位姿集（存储违约）");
                    }
                    snap.ws.poseSetObject = typed;
                } else {
                    static_assert(std::is_same_v<T, DrivetrainDesign>, "五变体全覆盖");
                    if (snap.ws.drivetrainObject.has_value()) {
                        throw std::logic_error("mdl: 基线闭包含多个传动对象（存储违约）");
                    }
                    snap.ws.drivetrainObject = typed;
                }
            },
            value);
    }
    return snap;
}

/// 基线中查找对象（oid→条目；不存在＝nullptr——调用方决定语义）。
const BaselineEntry* findBaselineEntry(const BaselineSnapshot& snap, const core::ObjectId& oid)
{
    for (const BaselineEntry& e : snap.entries) {
        if (e.oid == oid) { return &e; }
    }
    return nullptr;
}

/// 候选工作集里按 oid 替换既有部件对象（Restore 路径）；命中返回 true。
bool replacePartInCandidate(ModelingWorkingSet& ws, const core::ObjectId& oid,
                            const ObjectVariant& value)
{
    return std::visit(
        [&](const auto& typed) -> bool {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, ToolDefinition>) {
                for (ToolDefinition& t : ws.toolObjects) {
                    if (t.objectId == oid) { t = typed; return true; }
                }
            } else if constexpr (std::is_same_v<T, SceneObject>) {
                for (SceneObject& s : ws.sceneObjects) {
                    if (s.objectId == oid) { s = typed; return true; }
                }
            } else if constexpr (std::is_same_v<T, PoseSet>) {
                if (ws.poseSetObject.has_value() && ws.poseSetObject->objectId == oid) {
                    ws.poseSetObject = typed;
                    return true;
                }
            } else if constexpr (std::is_same_v<T, DrivetrainDesign>) {
                if (ws.drivetrainObject.has_value()
                    && ws.drivetrainObject->objectId == oid) {
                    ws.drivetrainObject = typed;
                    return true;
                }
            } else {
                (void)oid;  // RobotDesign 根不在此替换（专用路径）
            }
            return false;
        },
        value);
}

/// 行程校验相关性（"工作集含有限限位旋转关节"——§9.4.4 非法调用行的
/// prepare 侧镜像；continuous 行程豁免、无界旋转不消费阈值）。
bool hasTravelRelevantJoints(const RobotDesign& design)
{
    for (const JointEntry& j : design.joints) {
        if (j.type == JointType::Revolute && j.bounds.tryValue().has_value()) {
            return true;
        }
    }
    return false;
}

/// 受影响对象的前一版本逆槽集（D-MDL-9：受影响对象前一 (oid,cv) canonical
/// 字节集；基线中不存在的对象＝本次新增——无前版，跳过）。
std::vector<PayloadObjectSlot> buildInverseSlots(const BaselineSnapshot& baseline,
                                                 const std::vector<core::ObjectId>& affected)
{
    std::vector<PayloadObjectSlot> slots;
    slots.reserve(affected.size());
    for (const core::ObjectId& oid : affected) {
        const BaselineEntry* entry = findBaselineEntry(baseline, oid);
        if (entry == nullptr) {
            continue;  // 首次新增对象——无前版可逆
        }
        PayloadObjectSlot slot;
        slot.allocateNew = false;
        slot.objectId = entry->oid;
        slot.objectTypeToken = entry->token;
        slot.objectBytes = entry->bytes;  // 前一版本 canonical 字节（原样快照）
        slots.push_back(std::move(slot));
    }
    return slots;
}

/// 命令 token→中文摘要标题（§9.3"中文命令摘要"——人读面的固定词表）。
std::string_view commandTitle(std::string_view token)
{
    if (token == kCmdApplyRobotDesign) { return "应用机器人设计"; }
    if (token == kCmdApplyToolDefinition) { return "应用工具定义"; }
    if (token == kCmdApplySceneObjects) { return "应用场景对象"; }
    if (token == kCmdApplyNamedPoses) { return "应用命名位姿集"; }
    if (token == kCmdApplyDrivetrainDesign) { return "应用传动设计"; }
    return token;  // 词表外 token（防御——原样承载不猜测）
}

/// 中文命令摘要（对象/字段/确认留痕/资源状态——§9.3；确定性模板）。
std::string buildSummary(std::string_view commandToken,
                         std::size_t writeCount,
                         const core::ObjectId* rootOid,
                         std::size_t addedCount,
                         std::size_t replacedCount,
                         std::size_t confirmableCount,
                         std::size_t warningCount)
{
    // 摘要四段（确定性序）：对象（写入计数＋新增/替换拆分＋根锚）→
    // 确认留痕（待确认计数——凭据与绑定四元组由 project S4/S6 落盘）→
    // 资源/物性提示（Warning 预告计数）。
    std::string summary(commandTitle(commandToken));
    summary += "：写入对象 " + std::to_string(writeCount) + " 项（新增 "
             + std::to_string(addedCount) + "、替换 " + std::to_string(replacedCount) + "）";
    if (rootOid != nullptr) {
        summary += "；根 " + rootOid->toCanonical();
    }
    if (confirmableCount > 0) {
        summary += "；行程上限待确认 " + std::to_string(confirmableCount) + " 项";
    }
    if (warningCount > 0) {
        summary += "；资源/物性提示 " + std::to_string(warningCount) + " 项";
    }
    return summary;
}

}  // namespace

project::PrepareOutcome IModelingCommandHandler::prepare(
    project::HandlerContext& ctx,
    const project::CommandEnvelope& envelope,
    const project::RevisionView& baseSnapshot,
    project::CommandPlan& out,
    std::vector<core::DiagnosticRecord>& diags)
{
    // ---- ① 载荷框架解码（§9.3 decode 失败→RejectedInvalidInput）----
    std::optional<CommandPayload> payload = tryDecodeCommandPayload(envelope.payloadCanonical);
    if (!payload.has_value()) {
        return project::PrepareOutcome::RejectedInvalidInput;  // invalid-payload
    }
    // 版本受理双检（S1 已拦截——防线纵深；受理集合＝{当前版本}）。
    if (envelope.payloadFormatVersion != kCommandPayloadVersion) {
        return project::PrepareOutcome::RejectedInvalidInput;
    }

    // ---- ② 基线重建＋防御性复核（失败 fail-fast——见 rebuildBaseline 注）----
    BaselineSnapshot baseline = rebuildBaseline(ctx, envelope, baseSnapshot);

    // ---- ③ 子类钩子：差值解码＋命令形状校验＋取号回填＋写入集表达 ----
    //（钩子按 §9.4.8 语义把对象写入集表达进 out.objectWrites——其余字段
    //  归基类；拒绝路径基类清空计划——拒绝态计划不可消费。）
    DecodeOutcome dec = decodeAndPlan(ctx, *payload, baseline.ws, out);
    if (dec.outcome != project::PrepareOutcome::Planned) {
        out.objectWrites.clear();
        return dec.outcome;  // RejectedInvalidInput（域口径——槽形状/身份/一致性）
    }

    // ---- ④ 断言分域（AssertionSuite——与就绪校验共用，NFR-MNT-04）----
    std::vector<core::DiagnosticRecord> blockers;
    std::vector<core::DiagnosticRecord> warnings;
    m_suite.assertSchemaVersions(dec.candidate, blockers);
    m_suite.assertClosureReferences(dec.candidate.design, dec.candidate, blockers);
    m_suite.assertJointLimitIntervals(dec.candidate.design, blockers);
    for (const LinkEntry& link : dec.candidate.design.links) {
        m_suite.assertBodyPhysical(link.objectId, link.localName, link.body,
                                   blockers, warnings);
    }
    for (const ToolDefinition& tool : dec.candidate.toolObjects) {
        m_suite.assertBodyPhysical(tool.objectId, tool.localName, tool.body,
                                   blockers, warnings);
    }
    m_suite.checkResourceStates(dec.candidate.design, warnings);

    // 行程上限策略校验（MDL-06④）——命令域门控：行程 Confirmable 属
    // apply-robot-design 的断言域（§9.3 命令清单表"断言"列仅根命令行含
    // "行程上限"；工具/场景/位姿/传动命令不改变关节行程事实，不重复消费
    // ④端口——NFR-MNT-04）。策略不可解析→Blocking＋④端口诊断传导，不
    // 静默跳过行程校验。
    std::vector<core::ConfirmableFinding> confirmables;
    if (dec.travelRelevant && hasTravelRelevantJoints(dec.candidate.design)) {
        if (m_services.policyProvider == nullptr) {
            throw std::logic_error("mdl: ④策略端口未装配（装配缺陷）");
        }
        const policy::PolicyResolution resolution = m_services.policyProvider->resolvePolicy(
            policy::PolicyResolutionRequest{m_services.policyObject,
                                            m_services.policyVersion});
        if (!resolution.policy.has_value()) {
            // L11：策略不可解析——行程合法性未确认＝就地阻止；④端口的全量
            // 诊断（POLICY-* 已注册码）传导进 diags（注册码借道数据流——
            // 非本单元产码，PA-1 不越权）。
            out.objectWrites.clear();  // 拒绝态计划不可消费（project 不消费）
            diags.insert(diags.end(), resolution.diagnostics.begin(),
                         resolution.diagnostics.end());
            diags.insert(diags.end(), blockers.begin(), blockers.end());
            diags.insert(diags.end(), warnings.begin(), warnings.end());
            return project::PrepareOutcome::RejectedHardAssert;
        }
        std::vector<core::DiagnosticRecord> evalDiags;
        const AssertionSuite::TravelEvaluation travel = m_suite.evaluateTravelLimits(
            dec.candidate.design, *resolution.policy, confirmables, evalDiags);
        if (travel == AssertionSuite::TravelEvaluation::EvaluationFailed) {
            // 评估未终态化（名称不可解析等）——行程校验未执行＝不静默放行。
            out.objectWrites.clear();
            diags.insert(diags.end(), evalDiags.begin(), evalDiags.end());
            diags.insert(diags.end(), blockers.begin(), blockers.end());
            diags.insert(diags.end(), warnings.begin(), warnings.end());
            return project::PrepareOutcome::RejectedHardAssert;
        }
    }

    if (!blockers.empty()) {
        // 硬断言失败：逐项定位诊断（MDL-ASSERT-*/READINESS-*，subject＋
        // localName＋比较型三要素）——就地阻止＋精确定位（MDL-06）；无修订。
        out.objectWrites.clear();
        diags.insert(diags.end(), blockers.begin(), blockers.end());
        diags.insert(diags.end(), warnings.begin(), warnings.end());
        return project::PrepareOutcome::RejectedHardAssert;
    }

    // ---- ⑤ 计划最终化（S4 确认编排/S5 双编译/S6 事务归 project）----
    //（objectWrites 已由钩子表达进 out——此处只补齐其余计划字段。）
    out.requiresDualCompile = m_requiresDualCompile;
    // 待确认集：稳定排序（对象 id 字典序）——与就绪报告同一排序纪律。
    std::sort(confirmables.begin(), confirmables.end(),
              [](const core::ConfirmableFinding& a, const core::ConfirmableFinding& b) {
                  const std::string ta = a.record.subject.has_value()
                                           ? a.record.subject->toCanonical()
                                           : std::string();
                  const std::string tb = b.record.subject.has_value()
                                           ? b.record.subject->toCanonical()
                                           : std::string();
                  if (ta != tb) { return ta < tb; }
                  return a.record.code < b.record.code;
              });
    out.confirmableFindings = std::move(confirmables);

    // inverse：受影响对象前一 (oid,cv) canonical 字节集（D-MDL-9）；全部
    // 受影响对象均无前版（首次应用）＝不可逆声明 nullopt（UndoRedoService
    // 不提供越过首修订的撤销——§6.9 空历史语义）。
    std::vector<PayloadObjectSlot> inverseSlots = buildInverseSlots(baseline, dec.affectedOids);
    if (!inverseSlots.empty()) {
        CommandPayload inverse;
        inverse.mode = CommandPayload::Mode::Restore;
        inverse.objects = std::move(inverseSlots);
        out.inverseCommandType = std::string(commandType());
        out.inversePayloadCanonical = encodeCommandPayload(inverse);
    }

    // 新增/替换拆分（摘要对象面——affectedOids 与基线的交集为替换）。
    std::size_t replaced = 0;
    for (const core::ObjectId& oid : dec.affectedOids) {
        if (findBaselineEntry(baseline, oid) != nullptr) { ++replaced; }
    }
    const std::size_t added = dec.affectedOids.size() - replaced;

    // 中文命令摘要（对象/字段/确认留痕/资源状态——随修订持久化）。
    out.summary = buildSummary(commandType(), out.objectWrites.size(),
                               dec.candidate.rootObjectId.has_value()
                                   ? &*dec.candidate.rootObjectId
                                   : nullptr,
                               added, replaced,
                               out.confirmableFindings.size(), warnings.size());

    // 物性缺失预告随 diags 留痕（Planned 也产出——§9.3"转 Warning 诊断随
    // 计划留痕"；持久化编排归调用方/L5 sink）。
    diags.insert(diags.end(), warnings.begin(), warnings.end());
    return project::PrepareOutcome::Planned;
}

// =====================================================================
// 五命令钩子（§9.3 命令清单表逐行——槽形状语义见各类注）
// =====================================================================

namespace {

/// Restore 模式的共用钩子体（§6.9：快照逆放——全部槽显式身份且存在于
/// 基线；写入＝载荷携带的前一版本 canonical 字节原样直写；候选＝基线中
/// 对应对象替换为解码值，断言管线照常执行——新修订的确认须重新绑定）。
DecodeOutcome decodeRestoreCommon(const CommandPayload& payload,
                                  const ModelingWorkingSet& baseline,
                                  project::CommandPlan& out)
{
    DecodeOutcome result;
    if (payload.objects.empty()) { return invalidInput(); }
    // 候选＝基线底（逆放对象就地替换——未涉及对象保持基线态）。
    result.candidate = baseline;
    result.candidate.rootObjectId = baseline.rootObjectId;
    bool rootRestored = false;  // 根被逆放＝关节行程事实随本次变更（MDL-06④域）
    std::vector<core::ObjectId> affected;
    affected.reserve(payload.objects.size());
    for (const PayloadObjectSlot& slot : payload.objects) {
        // 逆放槽契约：显式身份＋存在于基线＋token 一致＋字节可解码。
        if (slot.allocateNew) { return invalidInput(); }
        if (!slot.objectId.isValid()) { return invalidInput(); }
        const auto decoded = decodeSlotObject(slot);
        if (!decoded.has_value()) { return invalidInput(); }
        // 基线存在性＋token 一致性（恢复不存在的身份＝无效载荷）。
        bool exists = false;
        if (baseline.rootObjectId.has_value() && *baseline.rootObjectId == slot.objectId
            && slot.objectTypeToken == kRobotDesignObjectType
            && std::holds_alternative<RobotDesign>(*decoded)) {
            exists = true;
            result.candidate.design = std::get<RobotDesign>(*decoded);
            result.candidate.rootObjectId = slot.objectId;
            rootRestored = true;
        } else if (replacePartInCandidate(result.candidate, slot.objectId, *decoded)) {
            exists = true;
        }
        if (!exists) { return invalidInput(); }
        // 写入＝前一版本字节原样（(oid,cv) 快照——不重编码，位级保真）。
        project::ObjectWrite write;
        write.objectId = slot.objectId;
        write.objectTypeToken = slot.objectTypeToken;
        write.payloadCanonical = slot.objectBytes;
        out.objectWrites.push_back(std::move(write));
        affected.push_back(slot.objectId);
    }
    result.affectedOids = std::move(affected);
    result.travelRelevant = rootRestored;
    result.outcome = project::PrepareOutcome::Planned;
    return result;
}

/// 根部件引用挂载（按 token 把新对象身份挂入根引用表；"至多一份"约束在
/// 位姿集/传动面强制）。前置：root 为可变候选根。
bool attachPartRef(RobotDesign& root, std::string_view token, const core::ObjectId& oid)
{
    if (token == kToolDefinitionObjectType) {
        root.toolRefs.push_back(oid);
        return true;
    }
    if (token == kSceneObjectObjectType) {
        root.sceneRefs.push_back(oid);
        return true;
    }
    if (token == kNamedPoseSetObjectType) {
        if (root.poseSetRef.has_value()) { return false; }  // 至多一份
        root.poseSetRef = oid;
        return true;
    }
    if (token == kRobotDrivetrainObjectType) {
        if (root.drivetrainRef.has_value()) { return false; }  // 至多一份
        root.drivetrainRef = oid;
        return true;
    }
    return false;  // 根对象不能作为部件槽挂载
}

/// 部件槽解码＋身份解析（新→取号回填值内 objectId；旧→须存在于基线同
/// token）。失败返回 false（调用方转 invalid-payload）。
bool resolvePartSlot(project::HandlerContext& ctx,
                     const PayloadObjectSlot& slot,
                     const ModelingWorkingSet& baseline,
                     ObjectVariant* outValue,
                     core::ObjectId* outOid,
                     bool* outIsNew)
{
    auto decoded = decodeSlotObject(slot);
    if (!decoded.has_value()) { return false; }
    // 槽身份与对象内身份一致性（值模型自述身份＝槽身份——防错位装配）。
    const core::ObjectId* inner = std::visit(
        [](const auto& typed) -> const core::ObjectId* {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, RobotDesign>) {
                return nullptr;  // 根对象无内嵌身份（闭包引用承载）
            } else {
                return &typed.objectId;
            }
        },
        *decoded);
    if (inner != nullptr && !(*inner == slot.objectId)) { return false; }
    if (slot.allocateNew) {
        if (slot.objectId.isValid()) { return false; }  // 新对象槽身份须为保留值
        if (inner == nullptr) { return false; }         // 部件必有内嵌身份
        const core::ObjectId allocated = ctx.objectId();  // PA-1：取号唯一归 project
        std::visit([&allocated](auto& typed) {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (!std::is_same_v<T, RobotDesign>) { typed.objectId = allocated; }
        }, *decoded);
        *outOid = allocated;
        *outIsNew = true;
    } else {
        if (!slot.objectId.isValid()) { return false; }
        // 既有对象：须存在于基线且 token 一致（写未知身份＝无效载荷）。
        bool exists = false;
        if (std::holds_alternative<ToolDefinition>(*decoded)) {
            for (const ToolDefinition& t : baseline.toolObjects) {
                if (t.objectId == slot.objectId) { exists = true; break; }
            }
        } else if (std::holds_alternative<SceneObject>(*decoded)) {
            for (const SceneObject& s : baseline.sceneObjects) {
                if (s.objectId == slot.objectId) { exists = true; break; }
            }
        } else if (std::holds_alternative<PoseSet>(*decoded)) {
            exists = baseline.poseSetObject.has_value()
                  && baseline.poseSetObject->objectId == slot.objectId;
        } else if (std::holds_alternative<DrivetrainDesign>(*decoded)) {
            exists = baseline.drivetrainObject.has_value()
                  && baseline.drivetrainObject->objectId == slot.objectId;
        }
        if (!exists) { return false; }
        *outOid = slot.objectId;
        *outIsNew = false;
    }
    *outValue = std::move(*decoded);
    return true;
}

}  // namespace

DecodeOutcome ApplyRobotDesignHandler::decodeAndPlan(project::HandlerContext& ctx,
                                                     const CommandPayload& payload,
                                                     const ModelingWorkingSet& baseline,
                                                     project::CommandPlan& out)
{
    if (payload.mode == CommandPayload::Mode::Restore) {
        return decodeRestoreCommon(payload, baseline, out);
    }
    DecodeOutcome result;
    if (payload.objects.empty()) { return invalidInput(); }

    // ---- 根槽（首槽，token=robot-design）----
    const PayloadObjectSlot& rootSlot = payload.objects[0];
    if (rootSlot.objectTypeToken != kRobotDesignObjectType) { return invalidInput(); }
    const auto rootDecoded = decodeSlotObject(rootSlot);
    if (!rootDecoded.has_value() || !std::holds_alternative<RobotDesign>(*rootDecoded)) {
        return invalidInput();
    }
    RobotDesign root = std::get<RobotDesign>(*rootDecoded);

    project::ObjectWrite rootWrite;
    rootWrite.objectTypeToken = std::string(kRobotDesignObjectType);
    if (rootSlot.allocateNew) {
        if (rootSlot.objectId.isValid()) { return invalidInput(); }  // 新槽＝保留值
        if (baseline.rootObjectId.has_value()) { return invalidInput(); }  // 恰一根
        const core::ObjectId rootOid = ctx.objectId();
        rootWrite.objectId = rootOid;
        result.candidate.rootObjectId = rootOid;
        result.affectedOids.push_back(rootOid);
    } else {
        if (!rootSlot.objectId.isValid()) { return invalidInput(); }
        if (!baseline.rootObjectId.has_value()
            || !(*baseline.rootObjectId == rootSlot.objectId)) {
            return invalidInput();  // 替换根须与基线根同身份
        }
        rootWrite.objectId = rootSlot.objectId;
        result.candidate.rootObjectId = rootSlot.objectId;
        result.affectedOids.push_back(rootSlot.objectId);
    }

    // ---- 部件槽（0..n——新挂引用/旧换字节）----
    for (std::size_t i = 1; i < payload.objects.size(); ++i) {
        const PayloadObjectSlot& slot = payload.objects[i];
        ObjectVariant value;
        core::ObjectId oid{};
        bool isNew = false;
        if (!resolvePartSlot(ctx, slot, baseline, &value, &oid, &isNew)) {
            return invalidInput();
        }
        project::ObjectWrite write;
        write.objectId = oid;
        write.objectTypeToken = slot.objectTypeToken;
        if (isNew) {
            // 新部件：挂入根引用表（"至多一份"违者＝无效载荷）＋进候选视图。
            if (!attachPartRef(root, slot.objectTypeToken, oid)) { return invalidInput(); }
            std::visit([&result](const auto& typed) {
                using T = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<T, ToolDefinition>) {
                    result.candidate.toolObjects.push_back(typed);
                } else if constexpr (std::is_same_v<T, SceneObject>) {
                    result.candidate.sceneObjects.push_back(typed);
                } else if constexpr (std::is_same_v<T, PoseSet>) {
                    result.candidate.poseSetObject = typed;
                } else if constexpr (std::is_same_v<T, DrivetrainDesign>) {
                    result.candidate.drivetrainObject = typed;
                } else {
                    // 根不作为部件槽（attachPartRef 已拒绝）——不可达防御。
                }
            }, value);
        } else {
            // 既有部件：候选视图内字节替换（引用表不动）。
            if (!replacePartInCandidate(result.candidate, oid, value)) {
                return invalidInput();
            }
        }
        write.payloadCanonical = encodeObjectOrThrow(value);
        out.objectWrites.push_back(std::move(write));
        result.affectedOids.push_back(oid);
    }

    // ---- 根写入（引用表已回填——编码于回填后）----
    result.candidate.design = root;
    rootWrite.payloadCanonical = encodeObjectOrThrow(ObjectVariant(root));
    out.objectWrites.insert(out.objectWrites.begin(), std::move(rootWrite));
    // 行程校验域归属：根命令的断言列含"行程上限 Confirmable"（§9.3 表行 1
    // ——根写入即关节行程事实随本次变更）。
    result.travelRelevant = true;
    result.outcome = project::PrepareOutcome::Planned;
    return result;
}

DecodeOutcome ApplyToolDefinitionHandler::decodeAndPlan(project::HandlerContext& ctx,
                                                        const CommandPayload& payload,
                                                        const ModelingWorkingSet& baseline,
                                                        project::CommandPlan& out)
{
    if (payload.mode == CommandPayload::Mode::Restore) {
        return decodeRestoreCommon(payload, baseline, out);
    }
    DecodeOutcome result;
    // 槽形状：恰一个 tool-definition 槽；基线须已有根（引用增量挂载点）。
    if (payload.objects.size() != 1
        || payload.objects[0].objectTypeToken != kToolDefinitionObjectType) {
        return invalidInput();
    }
    if (!baseline.rootObjectId.has_value()) { return invalidInput(); }
    ObjectVariant value;
    core::ObjectId oid{};
    bool isNew = false;
    if (!resolvePartSlot(ctx, payload.objects[0], baseline, &value, &oid, &isNew)) {
        return invalidInput();
    }
    const ToolDefinition& tool = std::get<ToolDefinition>(value);

    project::ObjectWrite toolWrite;
    toolWrite.objectId = oid;
    toolWrite.objectTypeToken = std::string(kToolDefinitionObjectType);
    if (isNew) {
        // 新工具：根 toolRefs 追加（"＋根引用表增量"——根对象一并写入）。
        RobotDesign root = baseline.design;
        root.toolRefs.push_back(oid);
        result.candidate.design = std::move(root);
        result.candidate.rootObjectId = baseline.rootObjectId;
        project::ObjectWrite rootWrite;
        rootWrite.objectId = baseline.rootObjectId;
        rootWrite.objectTypeToken = std::string(kRobotDesignObjectType);
        rootWrite.payloadCanonical =
            encodeObjectOrThrow(ObjectVariant(result.candidate.design));
        out.objectWrites.push_back(std::move(rootWrite));
        result.affectedOids.push_back(*baseline.rootObjectId);
        result.candidate.toolObjects.push_back(tool);
    } else {
        // 既有工具：字节替换（引用稳定——根不写）。
        result.candidate = baseline;
        auto& tools = result.candidate.toolObjects;
        tools.erase(std::remove_if(tools.begin(), tools.end(),
                                   [&oid](const ToolDefinition& t) { return t.objectId == oid; }),
                    tools.end());
        tools.push_back(tool);  // C++17 无 vector erase_if 自由函数——erase-remove 惯用法
    }
    toolWrite.payloadCanonical = encodeObjectOrThrow(value);
    out.objectWrites.push_back(std::move(toolWrite));
    result.affectedOids.push_back(oid);
    result.outcome = project::PrepareOutcome::Planned;
    return result;
}

DecodeOutcome ApplySceneObjectsHandler::decodeAndPlan(project::HandlerContext& ctx,
                                                      const CommandPayload& payload,
                                                      const ModelingWorkingSet& baseline,
                                                      project::CommandPlan& out)
{
    if (payload.mode == CommandPayload::Mode::Restore) {
        return decodeRestoreCommon(payload, baseline, out);
    }
    DecodeOutcome result;
    // 槽形状：1..n 个 scene-object 槽（批量）；基线须已有根。
    if (payload.objects.empty()) { return invalidInput(); }
    if (!baseline.rootObjectId.has_value()) { return invalidInput(); }

    // 候选＝基线底（增量就地累积）；根写入占位（首个新增槽时插入一次，
    // 字节在末尾统一编码——sceneRefs 收集完毕后的确定性单点）。
    result.candidate = baseline;
    result.candidate.rootObjectId = baseline.rootObjectId;
    bool rootWriteInserted = false;
    auto ensureRootWrite = [&]() {
        if (rootWriteInserted) { return; }
        project::ObjectWrite rootWrite;
        rootWrite.objectId = baseline.rootObjectId;
        rootWrite.objectTypeToken = std::string(kRobotDesignObjectType);
        // payloadCanonical 占位（空）——末尾统一编码回填。
        out.objectWrites.insert(out.objectWrites.begin(), std::move(rootWrite));
        result.affectedOids.push_back(*baseline.rootObjectId);
        rootWriteInserted = true;
    };

    for (const PayloadObjectSlot& slot : payload.objects) {
        if (slot.objectTypeToken != kSceneObjectObjectType) { return invalidInput(); }
        ObjectVariant value;
        core::ObjectId oid{};
        bool isNew = false;
        if (!resolvePartSlot(ctx, slot, baseline, &value, &oid, &isNew)) {
            return invalidInput();
        }
        const SceneObject& scene = std::get<SceneObject>(value);
        project::ObjectWrite write;
        write.objectId = oid;
        write.objectTypeToken = std::string(kSceneObjectObjectType);
        if (isNew) {
            // 批量新增：根 sceneRefs 逐个追加（根对象只写一次）。
            ensureRootWrite();
            result.candidate.design.sceneRefs.push_back(oid);
            result.candidate.sceneObjects.push_back(scene);
        } else {
            // 既有场景对象：字节替换（引用稳定）。
            auto& scenes = result.candidate.sceneObjects;
            scenes.erase(std::remove_if(scenes.begin(), scenes.end(),
                                        [&oid](const SceneObject& sc) { return sc.objectId == oid; }),
                         scenes.end());
            scenes.push_back(scene);
        }
        write.payloadCanonical = encodeObjectOrThrow(value);
        out.objectWrites.push_back(std::move(write));
        result.affectedOids.push_back(oid);
    }
    // 根字节统一编码（sceneRefs 已收集完毕）。
    if (rootWriteInserted) {
        out.objectWrites.front().payloadCanonical =
            encodeObjectOrThrow(ObjectVariant(result.candidate.design));
    }
    result.outcome = project::PrepareOutcome::Planned;
    return result;
}

DecodeOutcome ApplyNamedPosesHandler::decodeAndPlan(project::HandlerContext& ctx,
                                                    const CommandPayload& payload,
                                                    const ModelingWorkingSet& baseline,
                                                    project::CommandPlan& out)
{
    if (payload.mode == CommandPayload::Mode::Restore) {
        return decodeRestoreCommon(payload, baseline, out);
    }
    DecodeOutcome result;
    // 槽形状：恰一个 named-pose-set 槽；基线须已有根；纯参考数据——无物性
    // 断言（断言管线照常执行但不产生该域发现）。
    if (payload.objects.size() != 1
        || payload.objects[0].objectTypeToken != kNamedPoseSetObjectType) {
        return invalidInput();
    }
    if (!baseline.rootObjectId.has_value()) { return invalidInput(); }
    ObjectVariant value;
    core::ObjectId oid{};
    bool isNew = false;
    if (!resolvePartSlot(ctx, payload.objects[0], baseline, &value, &oid, &isNew)) {
        return invalidInput();
    }
    const PoseSet& poseSet = std::get<PoseSet>(value);

    project::ObjectWrite write;
    write.objectId = oid;
    write.objectTypeToken = std::string(kNamedPoseSetObjectType);
    if (isNew) {
        // 新位姿集：根 poseSetRef 置入（原须未设置——"至多一份"§4.6）。
        if (baseline.design.poseSetRef.has_value()) { return invalidInput(); }
        result.candidate = baseline;
        result.candidate.design.poseSetRef = oid;
        result.candidate.poseSetObject = poseSet;
        project::ObjectWrite rootWrite;
        rootWrite.objectId = baseline.rootObjectId;
        rootWrite.objectTypeToken = std::string(kRobotDesignObjectType);
        rootWrite.payloadCanonical =
            encodeObjectOrThrow(ObjectVariant(result.candidate.design));
        out.objectWrites.push_back(std::move(rootWrite));
        result.affectedOids.push_back(*baseline.rootObjectId);
    } else {
        // 既有位姿集：字节替换。
        result.candidate = baseline;
        result.candidate.poseSetObject = poseSet;
    }
    write.payloadCanonical = encodeObjectOrThrow(value);
    out.objectWrites.push_back(std::move(write));
    result.affectedOids.push_back(oid);
    result.outcome = project::PrepareOutcome::Planned;
    return result;
}

DecodeOutcome ApplyDrivetrainDesignHandler::decodeAndPlan(project::HandlerContext& ctx,
                                                          const CommandPayload& payload,
                                                          const ModelingWorkingSet& baseline,
                                                          project::CommandPlan& out)
{
    if (payload.mode == CommandPayload::Mode::Restore) {
        return decodeRestoreCommon(payload, baseline, out);
    }
    DecodeOutcome result;
    // 槽形状：恰一个 robot-drivetrain 槽；基线须已有根。
    if (payload.objects.size() != 1
        || payload.objects[0].objectTypeToken != kRobotDrivetrainObjectType) {
        return invalidInput();
    }
    if (!baseline.rootObjectId.has_value()) { return invalidInput(); }
    ObjectVariant value;
    core::ObjectId oid{};
    bool isNew = false;
    if (!resolvePartSlot(ctx, payload.objects[0], baseline, &value, &oid, &isNew)) {
        return invalidInput();
    }
    const DrivetrainDesign& drivetrain = std::get<DrivetrainDesign>(value);

    project::ObjectWrite write;
    write.objectId = oid;
    write.objectTypeToken = std::string(kRobotDrivetrainObjectType);
    if (isNew) {
        // 新传动：根 drivetrainRef 置入（原须未设置——"至多一份"§4.7）。
        if (baseline.design.drivetrainRef.has_value()) { return invalidInput(); }
        result.candidate = baseline;
        result.candidate.design.drivetrainRef = oid;
        result.candidate.drivetrainObject = drivetrain;
        project::ObjectWrite rootWrite;
        rootWrite.objectId = baseline.rootObjectId;
        rootWrite.objectTypeToken = std::string(kRobotDesignObjectType);
        rootWrite.payloadCanonical =
            encodeObjectOrThrow(ObjectVariant(result.candidate.design));
        out.objectWrites.push_back(std::move(rootWrite));
        result.affectedOids.push_back(*baseline.rootObjectId);
    } else {
        // 既有传动：字节替换（SEL-10 回填/OPT StageB 编辑路径）。
        result.candidate = baseline;
        result.candidate.drivetrainObject = drivetrain;
    }
    write.payloadCanonical = encodeObjectOrThrow(value);
    out.objectWrites.push_back(std::move(write));
    result.affectedOids.push_back(oid);
    result.outcome = project::PrepareOutcome::Planned;
    return result;
}

// =====================================================================
// L5 装配注册（project.md §6.5——一次性；所有权移交注册表）
// =====================================================================

void registerModelingCommandHandlers(project::HandlerRegistry& registry,
                                     const HandlerServices& services)
{
    // 五处理器共享同一服务集（无状态子类——共享安全）；重复 token 注册由
    // 注册表边界拒绝（fail-fast——装配错误尽早暴露）。
    registry.registerHandler(std::make_unique<ApplyRobotDesignHandler>(services));
    registry.registerHandler(std::make_unique<ApplyToolDefinitionHandler>(services));
    registry.registerHandler(std::make_unique<ApplySceneObjectsHandler>(services));
    registry.registerHandler(std::make_unique<ApplyNamedPosesHandler>(services));
    registry.registerHandler(std::make_unique<ApplyDrivetrainDesignHandler>(services));
}

}  // namespace sdurws::ird::modeling
