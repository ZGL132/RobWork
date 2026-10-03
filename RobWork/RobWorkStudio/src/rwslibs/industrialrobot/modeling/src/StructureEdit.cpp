/**
 * @file   StructureEdit.cpp
 * @brief  关节链结构编辑原语实现（契约头 StructureEdit.hpp——§5.2 v0.28
 *         四操作词表；本文件零 UI 语义——呈现归 PanelEditFlow 层）。
 *
 * 实现纪律（对齐 applyJointFieldEdit 的 T07 内核先例）：
 *   - **先校验后变更**：全部拒绝面在触碰工作集前判完——拒绝路径工作集
 *     字节不变（UX-03 域内强保证）；
 *   - **确定性**：新对象身份一律 deriveDraftObjectId（稳定键含批次命名
 *     空间"ird/modeling/structure-draft/"防与模板路径撞句柄）；命名消歧
 *     为确定性扫描（非随机）；
 *   - **守卫**：每次变更后跑 I-MDL-1 计数关系自检（links==joints+1），
 *     违约＝实现缺陷 fail-fast（正常路径结构上不可达——防御纵深）。
 */

#include <sdurws/ird/modeling/StructureEdit.hpp>

#include <stdexcept>
#include <utility>

#include "DraftIdentity.hpp"  // deriveDraftObjectId/makeSeedLink（单元内共享——UI-T47 提升）

namespace sdurws {
namespace ird {
namespace modeling {

namespace {

/// 结构编辑批次命名空间（临时句柄稳定键前缀——防与模板路径撞句柄）。
constexpr char kStructureDraftKey[] = "ird/modeling/structure-draft/";

/// 变更记录追加（四操作共用——MDL-09 摘要载体）。
void recordChange(ModelingWorkingSet& ws, std::string subject, std::string summary)
{
    ModelingChangeRecord record;
    record.subject = std::move(subject);
    record.summary = std::move(summary);
    ws.changes.push_back(std::move(record));
}

/// I-MDL-1 计数关系自检（变更后守卫——违约＝实现缺陷 fail-fast）。
void guardChainInvariants(const ModelingWorkingSet& ws, const char* op)
{
    if (ws.design.joints.empty() || ws.design.links.size() != ws.design.joints.size() + 1) {
        throw std::logic_error(std::string("modeling/structure/") + op
                               + ": 链计数关系破坏（links==joints+1/I-MDL-1 ≥1——"
                                 "实现缺陷，正常路径不可达）");
    }
}

/// 链序自动命名：base 前缀＋从 1 起扫描的第一个未占用序号（确定性消歧——
/// I-MDL-2 作用域唯一；非随机、非全局计数——同工作集态同命名）。
/// 关节名消歧（"j<序>"——序从 1 起扫描第一个未占用值；确定性）。
std::string disambiguatedJointName(const ModelingWorkingSet& ws)
{
    for (std::size_t n = 1;; ++n) {
        const std::string candidate = "j" + std::to_string(n);
        bool taken = false;
        for (const JointEntry& j : ws.design.joints) {
            if (j.localName == candidate) { taken = true; break; }
        }
        if (!taken) { return candidate; }
    }
}

/// 连杆名消歧（"l<序>"——基座"base"名占 0 位，种子从 1 起同款扫描）。
std::string disambiguatedLinkName(const ModelingWorkingSet& ws)
{
    for (std::size_t n = 1;; ++n) {
        const std::string candidate = "l" + std::to_string(n);
        bool taken = false;
        for (const LinkEntry& l : ws.design.links) {
            if (l.localName == candidate) { taken = true; break; }
        }
        if (!taken) { return candidate; }
    }
}

/// 新关节种子（§5.2 v0.28 ①——T-MDL-1 J1 行同族＋origin 恒位姿＋
/// 确定性临时句柄；与 §5.1 custom-chain 种子同构）。
JointEntry makeSeedJoint(std::size_t ordinal, const std::string& localName)
{
    // T-MDL-1 J1 行表值（sixAxisTemplateDefaults().front()——单一权威，
    // 禁抄字面量；§5.1 表为注册源）。
    const SixAxisJointSpec seed = sixAxisTemplateDefaults().front();
    const core::ValueProvenance provenance = core::ValueProvenance::make(
        core::ProvenanceKind::UserProvided);

    JointEntry joint;
    joint.objectId = deriveDraftObjectId(std::string(kStructureDraftKey)
                                         + "joint/" + std::to_string(ordinal));
    joint.localName = localName;
    joint.type = seed.type;
    joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        seed.axis, provenance);
    joint.origin = core::SourcedValue<JointPose>::provided(JointPose{}, provenance);
    joint.zeroOffset = seed.zeroOffset;
    joint.bounds = core::SourcedValue<JointLimits>::provided(seed.bounds, provenance);
    // workingRange＝NotApplicable（Revolute 不适用——I-MDL-4，模板同款）
    joint.workingRange = core::SourcedValue<JointLimits>::notApplicable();
    return joint;
}

}  // namespace

// =====================================================================
// 错误码 token
// =====================================================================

std::string_view structureEditErrorCodeToken(StructureEditErrorCode code) noexcept
{
    switch (code) {
    case StructureEditErrorCode::IndexOutOfRange: return "index-out-of-range";
    case StructureEditErrorCode::WouldDeleteLastJoint: return "would-delete-last-joint";
    case StructureEditErrorCode::ReorderAtBoundary: return "reorder-at-boundary";
    case StructureEditErrorCode::ReferencedByTcp: return "referenced-by-tcp";
    }
    return "unknown";
}

// =====================================================================
// ① 新增（选中位插入）
// =====================================================================

std::optional<StructureEditError> addJointAt(ModelingWorkingSet& ws,
                                             std::size_t index)
{
    const std::size_t n = ws.design.joints.size();
    if (index > n) {
        return StructureEditError{
            StructureEditErrorCode::IndexOutOfRange,
            "插入位 " + std::to_string(index) + " 越界（合法域 0.." + std::to_string(n)
                + "；n＝链尾追加）"};
    }

    // 先算名与身份（不触碰工作集——拒绝面已过，此为变更准备）。
    const std::string jointName = disambiguatedJointName(ws);
    const std::string linkName = disambiguatedLinkName(ws);
    const std::size_t ordinal = n + 1;  // 批次内序（句柄键稳定——重建态同键）
    JointEntry joint = makeSeedJoint(ordinal, jointName);
    LinkEntry link = makeSeedLink(std::string(kStructureDraftKey) + "link/"
                                      + std::to_string(ordinal),
                                  linkName,
                                  core::ValueProvenance::make(
                                      core::ProvenanceKind::UserProvided));

    // 插入：关节在 joints[index] **之后**（下标 index+1——§5.2 v0.28 ①
    // "选中位插入"语义）；index==n（链尾追加形态）时插在下标 n（向量尾）
    // ——统一公式 min(index+1, n)。连杆在 links[index+1]（下游位——新
    // 关节连接 links[index] 与新连杆；原 links[index+1] 后移保持链序；
    // 尾追加时 links[index+1] 同为向量尾）。
    const std::size_t jointInsertAt = index + 1 > n ? n : index + 1;
    ws.design.joints.insert(ws.design.joints.begin()
                                + static_cast<std::ptrdiff_t>(jointInsertAt),
                            std::move(joint));
    ws.design.links.insert(ws.design.links.begin()
                               + static_cast<std::ptrdiff_t>(index + 1),
                           std::move(link));

    guardChainInvariants(ws, "add");
    recordChange(ws,
                 "joints[" + std::to_string(index) + "]",
                 "新增关节 " + jointName + "（设计默认种子，选中位插入）"
                 "——链长 " + std::to_string(n + 1)
                 + "；结构变更后运动学及下游结果需重算（MDL-09）");
    return std::nullopt;
}

// =====================================================================
// ② 删除（连带下游连杆）
// =====================================================================

std::optional<StructureEditError> removeJointAt(ModelingWorkingSet& ws,
                                                std::size_t index)
{
    const std::size_t n = ws.design.joints.size();
    if (index >= n) {
        return StructureEditError{
            StructureEditErrorCode::IndexOutOfRange,
            "删除下标 " + std::to_string(index) + " 越界（合法域 0.."
                + std::to_string(n - 1) + "）"};
    }
    if (n == 1) {
        return StructureEditError{
            StructureEditErrorCode::WouldDeleteLastJoint,
            "删除后将剩零关节（I-MDL-1：至少一根关节）——如需清空链请"
            "重建工作集而非逐根删除"};
    }
    // 引用保护词表保留面（§5.2 v0.28 ②——常规链 TCP 挂工具不挂关节/
    // 连杆，此面为将来引用扩展防静默丢失；当前扫描 defaultTcp 的 TcpRef
    // 不指向关节/连杆，结构性不触发——保留实现位）。
    const std::string removedName = ws.design.joints[index].localName;
    const std::string removedLinkName = ws.design.links[index + 1].localName;

    // 变更：删 joints[index]＋links[index+1]（尾关节 index==n−1 时
    // links[index+1] 即 links[n]——同一下标语义覆盖，无需分支）。
    ws.design.joints.erase(ws.design.joints.begin()
                           + static_cast<std::ptrdiff_t>(index));
    ws.design.links.erase(ws.design.links.begin()
                          + static_cast<std::ptrdiff_t>(index + 1));

    guardChainInvariants(ws, "remove");
    recordChange(ws,
                 "joints[" + std::to_string(index) + "]",
                 "删除关节 " + removedName + "（连带连杆 " + removedLinkName
                     + "）——链长 " + std::to_string(n - 1)
                     + "；结构变更后运动学及下游结果需重算（MDL-09）");
    return std::nullopt;
}

// =====================================================================
// ③ 重排（上移/下移——相邻交换＋连杆随动）
// =====================================================================

std::optional<StructureEditError> reorderJoint(ModelingWorkingSet& ws,
                                               std::size_t index, bool down)
{
    const std::size_t n = ws.design.joints.size();
    if (index >= n) {
        return StructureEditError{
            StructureEditErrorCode::IndexOutOfRange,
            "重排下标 " + std::to_string(index) + " 越界（合法域 0.."
                + std::to_string(n - 1) + "）"};
    }
    if (!down && index == 0) {
        return StructureEditError{
            StructureEditErrorCode::ReorderAtBoundary,
            "首关节无上移位（已是链头）"};
    }
    if (down && index == n - 1) {
        return StructureEditError{
            StructureEditErrorCode::ReorderAtBoundary,
            "尾关节无下移位（已是链尾）"};
    }
    const std::size_t other = down ? index + 1 : index - 1;
    const std::string movedName = ws.design.joints[index].localName;

    // 关节对交换＋连杆随动（连杆身份不变——C4 参数保留：axis/origin/
    // zeroOffset/bounds/物性/几何引用随 ObjectId 自然随动；dhDerived 在
    // Explicit 态不参与编码身份，重算不冒充权威——D-MDL-5）。
    std::swap(ws.design.joints[index], ws.design.joints[other]);
    std::swap(ws.design.links[index + 1], ws.design.links[other + 1]);

    guardChainInvariants(ws, "reorder");
    recordChange(ws,
                 "joints[" + std::to_string(index) + "]",
                 std::string("关节 ") + movedName
                     + (down ? " 下移" : " 上移") + "一位（参数随对象保留——"
                     "C4）；结构变更后运动学及下游结果需重算（MDL-09）");
    return std::nullopt;
}

// =====================================================================
// ④ 六轴重置（T-MDL-1 整链重建——有损，确认归 UI）
// =====================================================================

std::optional<StructureEditError> resetToSixAxis(ModelingWorkingSet& ws)
{
    // 与模板创建同构的整链重建（§5.2 v0.28 ④）：关节/连杆全部重建为
    // T-MDL-1 种子——身份为新的确定性临时句柄（旧身份随旧草稿态废弃，
    // 无"保留用户参数"混合态——有损语义的显式形态）。
    const auto specs = sixAxisTemplateDefaults();
    const core::ValueProvenance provenance =
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
    const std::string baseKey = std::string(kStructureDraftKey) + "reset-6r";

    RobotDesign rebuilt;  // 缺省＝schemaVersion 单一权威/Explicit/空引用表
    rebuilt.displayName = ws.design.displayName;  // 呈现名保留（非链参数）
    rebuilt.authority = ws.design.authority;      // 权威模式整链一个（零切换语义）
    rebuilt.basePlacement = ws.design.basePlacement;  // 基座布置保留（MDL-22 非链面）

    for (std::size_t i = 0; i < specs.size(); ++i) {
        const SixAxisJointSpec& spec = specs[i];
        JointEntry joint;
        joint.objectId = deriveDraftObjectId(baseKey + "/joint/" + std::to_string(i));
        joint.localName = "j" + std::to_string(i + 1);
        joint.type = spec.type;
        joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            spec.axis, provenance);
        joint.origin = core::SourcedValue<JointPose>::provided(JointPose{}, provenance);
        joint.zeroOffset = spec.zeroOffset;
        joint.bounds = core::SourcedValue<JointLimits>::provided(spec.bounds, provenance);
        joint.workingRange = core::SourcedValue<JointLimits>::notApplicable();
        rebuilt.joints.push_back(std::move(joint));
    }
    rebuilt.links.push_back(
        makeSeedLink(baseKey + "/link/0", "base", provenance));
    for (std::size_t i = 0; i < specs.size(); ++i) {
        rebuilt.links.push_back(
            makeSeedLink(baseKey + "/link/" + std::to_string(i + 1),
                         "l" + std::to_string(i + 1), provenance));
    }

    // 出口守卫：六轴重建必须通过维度二判定（模板创建同款一致性检查——
    // FullTemplateRange；否则＝表被改坏的实现缺陷 fail-fast）。
    std::vector<JointType> chainTypes;
    chainTypes.reserve(rebuilt.joints.size());
    for (const JointEntry& j : rebuilt.joints) {
        chainTypes.push_back(j.type);
    }
    if (judgeChainCapability(chainTypes).kind != ChainCapabilityKind::FullTemplateRange) {
        throw std::logic_error("modeling/structure/reset-6r: 六轴重建未通过"
                               "维度二判定（T-MDL-1 表与 §6.4 判定失一致——"
                               "实现缺陷）");
    }

    const std::size_t oldJoints = ws.design.joints.size();
    ws.design.joints = std::move(rebuilt.joints);
    ws.design.links = std::move(rebuilt.links);
    // 链引用面随链重建清空（旧链的工具/场景/位姿/传动引用与重建后六轴
    // 无语义对应——引用对象本体（toolObjects 等）保留在部件视图，重挂
    // 归用户经既有编辑流；根引用表清空＝诚实形态，不静默保留悬挂引用）。
    ws.design.toolRefs.clear();
    ws.design.sceneRefs.clear();
    ws.design.poseSetRef.reset();
    // drivetrainRef 保留（MDL-16 传动设计挂在根对象、与链长无必然绑定——
    // 摩擦/力矩行数由 prepare 断言按新链长校验，失配走域拒绝面呈现）。

    guardChainInvariants(ws, "reset");
    recordChange(ws,
                 "joints[0..]",
                 "六轴重置：整链重建为 T-MDL-1 模板参数（原链 " + std::to_string(oldJoints)
                     + " 轴→6 轴——有损操作；工具/场景引用已清空待重挂，"
                       "传动设计保留）；运动学及下游结果需重算（MDL-09）");
    return std::nullopt;
}

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws