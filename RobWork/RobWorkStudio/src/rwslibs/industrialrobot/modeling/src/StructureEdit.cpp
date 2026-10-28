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
#include <algorithm>
#include <map>
#include <set>
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

void rememberWorkingSetObjectIds(ModelingWorkingSet& ws)
{
    std::map<std::string,core::ObjectId> occupied;
    const auto add=[&](const core::ObjectId& id){if(id.isValid()){occupied.emplace(id.toCanonical(),id);}};
    for(const auto& id:ws.allocatedObjectIds){add(id);}
    if(ws.rootObjectId){add(*ws.rootObjectId);}
    for(const auto& joint:ws.design.joints){add(joint.objectId);}
    for(const auto& link:ws.design.links){add(link.objectId);}
    for(const auto& id:ws.design.toolRefs){add(id);}
    for(const auto& id:ws.design.sceneRefs){add(id);}
    if(ws.design.poseSetRef){add(*ws.design.poseSetRef);}
    if(ws.design.drivetrainRef){add(*ws.design.drivetrainRef);}
    if(ws.design.defaultTcp){add(ws.design.defaultTcp->toolOid);}
    for(const auto& tool:ws.toolObjects){add(tool.objectId);}
    for(const auto& scene:ws.sceneObjects){add(scene.objectId);}
    if(ws.poseSetObject){add(ws.poseSetObject->objectId);}
    if(ws.drivetrainObject){add(ws.drivetrainObject->objectId);}
    for(const auto& resource:ws.design.resourceManifest){if(resource.solidifiedObject){add(resource.solidifiedObject->objectId);}}
    ws.allocatedObjectIds.clear();
    for(const auto& pair:occupied){ws.allocatedObjectIds.push_back(pair.second);}
}

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
    case StructureEditErrorCode::AssociatedDataInvalid: return "associated-data-invalid";
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
    rememberWorkingSetObjectIds(ws);
    const auto occupied=[&](const core::ObjectId& id){return std::find(ws.allocatedObjectIds.begin(),ws.allocatedObjectIds.end(),id)!=ws.allocatedObjectIds.end();};
    // F10：全闭包和历史共同排除，joint/link成对试探；同一输入历史得相同身份。
    std::size_t ordinal = n + 1;
    while(occupied(deriveDraftObjectId(std::string(kStructureDraftKey)+"joint/"+std::to_string(ordinal)))
        ||occupied(deriveDraftObjectId(std::string(kStructureDraftKey)+"link/"+std::to_string(ordinal)))){++ordinal;}
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

    rememberWorkingSetObjectIds(ws);
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
    rememberWorkingSetObjectIds(ws); // 删除前保留占用；删除不释放身份。
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

    // F06：候选以稳定ObjectId建立新序→旧序映射，关联数据与对象同拍提交。
    auto candidate=ws;
    std::swap(candidate.design.joints[index],candidate.design.joints[other]);
    std::swap(candidate.design.links[index+1],candidate.design.links[other+1]);
    std::map<std::string,std::size_t> oldIndex;
    for(std::size_t i=0;i<n;++i){
        if(!oldIndex.emplace(ws.design.joints[i].objectId.toCanonical(),i).second){
            return StructureEditError{StructureEditErrorCode::AssociatedDataInvalid,"关节身份重复，无法无损置换。"};
        }
    }
    std::vector<std::size_t> permutation;
    for(const auto& joint:candidate.design.joints){permutation.push_back(oldIndex.at(joint.objectId.toCanonical()));}
    const auto permute=[&](auto& values,bool allowEmpty){
        if(allowEmpty&&values.empty()){return true;}
        if(values.size()!=n){return false;}
        const auto original=values;
        for(std::size_t i=0;i<n;++i){values[i]=original[permutation[i]];}
        return true;
    };
    if(candidate.poseSetObject){
        for(auto& pose:candidate.poseSetObject->entries){
            if(!permute(pose.jointConfiguration,false)){return StructureEditError{StructureEditErrorCode::AssociatedDataInvalid,"命名姿态维度与关节序不一致。"};}
        }
    }
    if(candidate.drivetrainObject){
        auto& drive=*candidate.drivetrainObject;
        if(!permute(drive.ratioPerJoint,true)||!permute(drive.frictionPerJoint,true)||!permute(drive.torqueLimitsPerJoint,true)){
            return StructureEditError{StructureEditErrorCode::AssociatedDataInvalid,"逐关节传动数组维度不一致。"};
        }
        if(drive.coupling){
            auto& coupling=*drive.coupling;
            const auto original=coupling;
            if(original.jointRangeLast>=n||original.jointRangeFirst>original.jointRangeLast
                ||original.rows!=original.jointRangeLast-original.jointRangeFirst+1
                ||original.cols==0||original.c.size()!=std::size_t(original.rows)*original.cols){
                return StructureEditError{StructureEditErrorCode::AssociatedDataInvalid,"耦合窗口或矩阵维度不一致。"};
            }
            std::vector<std::size_t> positions;
            for(std::size_t i=0;i<n;++i){if(permutation[i]>=original.jointRangeFirst&&permutation[i]<=original.jointRangeLast){positions.push_back(i);}}
            if(positions.back()-positions.front()+1!=positions.size()){
                return StructureEditError{StructureEditErrorCode::AssociatedDataInvalid,"重排将耦合窗口分散，当前连续窗口格式无法无损表达。"};
            }
            coupling.jointRangeFirst=static_cast<std::uint32_t>(positions.front());
            coupling.jointRangeLast=static_cast<std::uint32_t>(positions.back());
            // 只换关节行；电机列和单位不变，不在modeling重算传动算法。
            for(std::size_t row=0;row<positions.size();++row){
                const auto oldRow=permutation[positions[row]]-original.jointRangeFirst;
                std::copy_n(original.c.begin()+oldRow*original.cols,original.cols,coupling.c.begin()+row*original.cols);
            }
        }
    }
    ws=std::move(candidate);

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
    rememberWorkingSetObjectIds(ws);
    std::set<std::string> occupied;
    for(const auto& id:ws.allocatedObjectIds){occupied.insert(id.toCanonical());}
    std::string baseKey;
    for(std::size_t generation=0;;++generation){
        baseKey=std::string(kStructureDraftKey)+"reset-6r/"+std::to_string(generation);
        bool taken=false;
        for(std::size_t i=0;i<=specs.size();++i){
            taken=taken||occupied.count(deriveDraftObjectId(baseKey+"/link/"+std::to_string(i)).toCanonical())!=0;
            if(i<specs.size()){taken=taken||occupied.count(deriveDraftObjectId(baseKey+"/joint/"+std::to_string(i)).toCanonical())!=0;}
        }
        if(!taken){break;}
    }

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
    if (judgeChainCapability(chainTypes, kHybridChainStage).kind
        != ChainCapabilityKind::FullTemplateRange) {
        throw std::logic_error("modeling/structure/reset-6r: 六轴重建未通过"
                               "维度二判定（T-MDL-1 表与 §6.4 判定失一致——"
                               "实现缺陷）");
    }

    const std::size_t oldJoints = ws.design.joints.size();
    ws.design.joints = std::move(rebuilt.joints);
    ws.design.links = std::move(rebuilt.links);
    // 链引用面随链重建清空（旧链的工具/场景/位姿/默认 TCP 引用与重建后
    // 六轴无语义对应——引用对象本体（toolObjects 等）保留在部件视图，重挂
    // 归用户经既有编辑流；根引用表清空＝诚实形态，不静默保留悬挂引用）。
    ws.design.toolRefs.clear();
    ws.design.sceneRefs.clear();
    ws.design.poseSetRef.reset();
    // defaultTcp 同拍清除（F-800——缺陷修复批A FIX-T1）：默认 TCP 是
    // "工具 ObjectId＋该工具 tcpList 键"的引用（I-MDL-9 defaultTcp∈toolRefs），
    // toolRefs 清空后残留即指向不在引用表中的工具＝悬挂引用可随草稿入库，
    // 与上方"不静默保留悬挂引用"的承诺直接矛盾。默认 TCP 本是链的派生
    // 选择（工具重挂后须用户重设），清空＝诚实形态（与 poseSetRef 同语义）。
    ws.design.defaultTcp.reset();
    // drivetrainRef 保留（MDL-16 传动设计挂在根对象、与链长无必然绑定——
    // 摩擦/力矩行数由 prepare 断言按新链长校验，失配走域拒绝面呈现）。

    rememberWorkingSetObjectIds(ws);
    guardChainInvariants(ws, "reset");
    recordChange(ws,
                 "joints[0..]",
                 "六轴重置：整链重建为 T-MDL-1 模板参数（原链 " + std::to_string(oldJoints)
                     + " 轴→6 轴——有损操作；工具/场景/默认 TCP 引用已清空待重挂，"
                       "传动设计保留）；运动学及下游结果需重算（MDL-09）");
    return std::nullopt;
}

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws