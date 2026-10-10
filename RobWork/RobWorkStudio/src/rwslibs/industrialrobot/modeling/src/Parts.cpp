/**
 * @file   Parts.cpp
 * @brief  四部件对象值模型的实现——值相等、耦合阶段 token、部件级
 *         不变量核查（工具 I-MDL-5＋I-MDL-13；传动 I-MDL-11/I-MDL-12）、
 *         命名位姿合并编辑流（T10——保留键保留/关节序一一对应）、
 *         部件位姿编辑流（UI-T55——工具安装接口/场景世界位姿）与
 *         耦合矩阵数值校验/编辑流（WP-13-T18——§4.7 coupling 一等字段
 *         R2 编辑与 I-MDL-11 重算复核）。
 *
 * 设计依据：units/modeling.md §4.4/§4.6/§4.7/§4.10、§8.1（WP-13-T18——
 * 病态/非常矩阵经比较型诊断阻止成模，M-12）、§9.5（MDL-21 码行——T18/R2
 * 注册，本文件只产出值面违例与校验事实）；任务契约 tasks/foundation/
 * WP-13-T03.json acceptance 1/3、tasks/foundation/WP-13-T10.json
 * acceptance 1/4、tasks/foundation/WP-13-T18.json acceptance 1/2。
 * 全部纯函数（并发安全；确定性 NFR-COR-02；编辑流除工作集写入外纯）。
 */

#include <sdurws/ird/modeling/Parts.hpp>

#include <sdurws/ird/modeling/Template.hpp>  // ModelingWorkingSet（部件编辑流的写入目标——Parts.hpp 前置声明的完整型）

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

#include "InertiaMath.hpp"  // 单元私有：惯量 SPD＋三角不等式单一实现（I-MDL-5——与连杆同源）
#include "RpyMath.hpp"      // 单元私有：RPY 正解唯一实现（UI-T55——部件位姿编辑的组合数学落点）
#include "DraftIdentity.hpp"  // 单元私有：草稿确定性句柄（UI-T60——位姿集/传动缺席创建；§5.2 临时句柄纪律）
#include "CouplingMath.hpp"  // 单元私有：耦合矩阵 SVD/条件数＋阈值单点（WP-13-T18——I-MDL-11 重算复核；P-MDL-7）

namespace sdurws::ird::modeling {

// =====================================================================
// 值相等（逐字段——rw::math 位姿经元素访问器比较，口径同 RobotDesign.cpp）
// =====================================================================

bool ToolDefinition::operator==(const ToolDefinition& o) const
{
    return schemaVersion == o.schemaVersion
        && objectId == o.objectId
        && localName == o.localName
        && displayName == o.displayName
        && TcpEntry::transformEquals(mountInterface, o.mountInterface)
        && tcpList == o.tcpList
        && geometry == o.geometry
        && body == o.body
        && payloadAttributes == o.payloadAttributes;
}

bool SceneObject::operator==(const SceneObject& o) const
{
    return schemaVersion == o.schemaVersion
        && objectId == o.objectId
        && localName == o.localName
        && TcpEntry::transformEquals(worldPose, o.worldPose)
        && geometry == o.geometry
        && role == o.role
        && collisionProfileHint == o.collisionProfileHint;
}

bool PoseSet::operator==(const PoseSet& o) const
{
    return schemaVersion == o.schemaVersion
        && objectId == o.objectId
        && entries == o.entries;
}

bool DrivetrainDesign::operator==(const DrivetrainDesign& o) const
{
    return schemaVersion == o.schemaVersion
        && objectId == o.objectId
        && ratioPerJoint == o.ratioPerJoint
        && coupling == o.coupling
        && frictionPerJoint == o.frictionPerJoint
        && torqueLimitsPerJoint == o.torqueLimitsPerJoint
        && catalogBackfill == o.catalogBackfill;
}

// =====================================================================
// 耦合阶段 token
// =====================================================================

std::string_view couplingStageToken(CouplingStage stage) noexcept
{
    switch (stage) {
    case CouplingStage::R1Locked: return "R1-locked";
    case CouplingStage::R2Enabled: return "R2-enabled";
    }
    return "unknown";
}

// =====================================================================
// 部件级不变量核查
// =====================================================================

namespace {

/// 追加违例（同 RobotDesign.cpp 口径——输出顺序＝调用序，确定性）。
void addViolation(std::vector<InvariantViolation>& out, InvariantId id, std::string subject)
{
    out.push_back(InvariantViolation{id, std::move(subject)});
}

/// double 有限性。
bool finite(double v) noexcept { return std::isfinite(v); }

/// 数值稳定文本形态（%.17g——位级可往返；耦合校验 detail 的 κ/σ 比值
/// 文案用，与 CommandHandlers formatDouble 同口径——单元内两处小函数，
/// 判定零重复：本函数只做文本化，不做任何比较判定）。
std::string formatSigmaRatio(double v)
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

}  // namespace

std::vector<InvariantViolation> checkInvariants(const ToolDefinition& tool)
{
    std::vector<InvariantViolation> out;

    // ---- I-MDL-13（工具 TCP 完整——§4.4 tcpList 行"≥1"；WP-13-T10 落位）----
    // defaultTcp 经 (toolOid, tcpKey) 引用 tcpList 其一（KIN-14）：空表＝
    // 工具无任何可引用 TCP，引用锚悬空＝引用完整性违例的构造侧根源——
    // 在值模型层就地拒绝（解码门校验链④经本函数自动强制，命令载荷/存储
    // 字节双通道都不可能携带空 TCP 表的工具——单一判定面，NFR-MNT-04）。
    // 键须非空且集合内唯一（键是 defaultTcp 的引用锚——空键/重复键同属
    // 引用锚损坏面；codec 排序规范化在字节边界另有一层，内存对象由本函数
    // 兜底——两处同源谓词，语义单一）。
    if (tool.tcpList.empty()) {
        addViolation(out, InvariantId::IMdl13, "tcpList.empty");
    }
    for (std::size_t i = 0; i < tool.tcpList.size(); ++i) {
        if (tool.tcpList[i].key.empty()) {
            addViolation(out, InvariantId::IMdl13,
                         "tcpList[" + std::to_string(i) + "].key.empty");
        }
        for (std::size_t j = i + 1; j < tool.tcpList.size(); ++j) {
            if (tool.tcpList[i].key == tool.tcpList[j].key) {
                addViolation(out, InvariantId::IMdl13,
                             "tcpList[" + std::to_string(i) + "].key.duplicate");
            }
        }
    }

    // 断言①②③同连杆（§4.4 表行原文）：与根对象连杆共用同一判定实现
    // （InertiaMath.hpp 单元私有头——防两处容差/判定式漂移）。
    // subject＝完整体路径 "tool.body"（与连杆 "links[i].body" 同一定位
    // 规则；诊断组装时由调用方前置对象定位信息）。
    const auto mass = tool.body.mass.tryValue();
    if (mass.has_value() && !(*mass > 0.0)) {
        addViolation(out, InvariantId::IMdl5, "tool.body.mass");
    }
    inertiamath::checkInertiaAssertions(out, tool.body.inertia, "tool.body");
    return out;
}

std::vector<InvariantViolation> checkInvariants(const DrivetrainDesign& drivetrain,
                                                CouplingStage stage)
{
    std::vector<InvariantViolation> out;

    // ---- I-MDL-12（R1 耦合阶段锁）：coupling 存在即违例 ----
    // §4.7 coupling 行"R1＝禁止配置（存在即阻断＋诊断，MDL-12/21 R1 口径）"。
    // 值面违例编号 I-MDL-12（§4.10 I-MDL-11 行括注定义）；稳定诊断码
    // MDL-21-COUPLING-STAGE-LOCKED 随 T18/R2 注册（§9.5 表尾追加纪律——
    // 不预建无消费者条目），本函数不产诊断记录（PA-1 产码唯一经工厂）。
    if (stage == CouplingStage::R1Locked && drivetrain.coupling.has_value()) {
        addViolation(out, InvariantId::IMdl12, "coupling.r1-locked");
    }

    // ---- I-MDL-11（传动合法）：ratio 有限>0；R2 矩阵自洽 ----
    for (std::size_t i = 0; i < drivetrain.ratioPerJoint.size(); ++i) {
        // 传动比无量纲，须有限且 >0（§4.7 ratioPerJoint 行）；缺失
        // （NotProvided）不违例——回填/编辑前允许暂缺（DataInsufficient 降级）
        if (const auto r = drivetrain.ratioPerJoint[i].tryValue();
            r.has_value() && !(std::isfinite(*r) && *r > 0.0)) {
            addViolation(out, InvariantId::IMdl11,
                         "ratioPerJoint[" + std::to_string(i) + "]");
        }
    }

    if (stage == CouplingStage::R2Enabled && drivetrain.coupling.has_value()) {
        const CouplingDesign& cp = *drivetrain.coupling;
        // 方阵：rows==cols 且扁平存储容量一致（行主序 rows*cols 个元素）
        if (cp.rows != cp.cols || static_cast<std::size_t>(cp.rows) * cp.cols != cp.c.size()) {
            addViolation(out, InvariantId::IMdl11, "coupling.notSquare");
        }
        // 条件数申报值自洽：正实数且 ≤1×10⁸（P-RT-7 设计默认——阈值唯一
        // 书写点＝CouplingMath kCouplingConditionNumberLimit，P-MDL-7；
        // WP-13-T18 起历史字面收敛于该单点）。注意本处仅核查**申报值**的
        // 自洽范围；申报值不参与合法性判定（见 CouplingDesign 类型注），
        // 合法性以重算为准——重算复核（SVD）由 checkCouplingMatrix 承载
        // （§8.1"病态/非常矩阵"行；编辑边界/prepare/L9 三处共用）。
        if (!(std::isfinite(cp.conditionNumber) && cp.conditionNumber > 0.0
              && cp.conditionNumber
                     <= couplingmath::kCouplingConditionNumberLimit)) {
            addViolation(out, InvariantId::IMdl11, "coupling.conditionNumber");
        }
        // 窗口自洽：闭区间 [i,j] 不得倒序（适用关节窗口语义——§4.7 jointRange 行）
        if (cp.jointRangeFirst > cp.jointRangeLast) {
            addViolation(out, InvariantId::IMdl11, "coupling.jointRange");
        }
    }

    return out;
}

// =====================================================================
// 耦合矩阵数值校验（WP-13-T18——I-MDL-11 重算复核；§8.1/M-12）
// =====================================================================

CouplingMatrixCheck checkCouplingMatrix(const CouplingDesign& coupling,
                                        std::size_t jointCount)
{
    CouplingMatrixCheck result;

    // ---- ① 非方阵（结构半段）：行数≠列数，或行主序存储容量与维度不符 ----
    // I-MDL-11"方阵"半段；比较要素 actual=rows、expected=cols（行数须
    // 等于列数——ERR-01 三要素的真实可比面；无量纲计数）。
    if (coupling.rows != coupling.cols
        || static_cast<std::size_t>(coupling.rows) * coupling.cols
               != coupling.c.size()) {
        result.violationKind = "not-square";
        result.detail = "非方阵（rows=" + std::to_string(coupling.rows)
                        + "，cols=" + std::to_string(coupling.cols)
                        + "，元素数=" + std::to_string(coupling.c.size())
                        + "——行主序容量 rows×cols 须与元素数一致）";
        return result;
    }
    const std::size_t n = coupling.rows;

    // ---- ② 元素非有限：任一 NaN/±Inf（I-MDL-3——非法值不静默置 0）----
    // 比较要素 actual=非有限元素计数、expected=0（有限元素全集）。
    std::size_t nonFiniteCount = 0;
    for (const double v : coupling.c) {
        if (!std::isfinite(v)) { ++nonFiniteCount; }
    }
    if (nonFiniteCount > 0) {
        result.violationKind = "element-not-finite";
        result.detail = "矩阵含非有限元素（NaN/Inf）计 " + std::to_string(nonFiniteCount)
                        + " 个（共 " + std::to_string(coupling.c.size())
                        + " 个，行主序）——常矩阵 C 的全部元素须有限";
        result.singularSigmaRatio = 0.0;
        return result;
    }

    // ---- ③ 窗口一致性：闭区间计数≠方阵阶（"方阵 n×n〔适用关节窗口与
    // 对应电机轴同序〕"——MDL-21；窗口行序＝关节串联序、列序＝电机轴序，
    // 同序性由有序承载，计数一致性在此强制）----
    const std::uint64_t windowCount =
        coupling.jointRangeLast >= coupling.jointRangeFirst
            ? static_cast<std::uint64_t>(coupling.jointRangeLast)
                  - coupling.jointRangeFirst + 1u
            : 0u;  // 倒序窗口＝计数 0（§4.10 值面已有 jointRange 违例——
                   // 此处按计数面拒绝，不猜测修复方向）
    if (windowCount != n) {
        result.violationKind = "window-mismatch";
        result.detail = "适用关节窗口计数（j−i+1=" + std::to_string(windowCount)
                        + "）与方阵阶 n=" + std::to_string(n)
                        + " 不一致（窗口 [" + std::to_string(coupling.jointRangeFirst)
                        + "," + std::to_string(coupling.jointRangeLast) + "]）";
        return result;
    }

    // ---- ④ 窗口越界：窗口须指向根关节表内存在的关节（编辑流传入
    // jointCount；0 关节表下任何非空窗口均越界——窗口必须指向存在的
    // 关节，不设"无根豁免"特例）----
    if (static_cast<std::size_t>(coupling.jointRangeFirst) + n > jointCount) {
        result.violationKind = "window-out-of-range";
        result.detail = "适用关节窗口 [" + std::to_string(coupling.jointRangeFirst)
                        + "," + std::to_string(coupling.jointRangeLast)
                        + "] 超出根关节表长度 " + std::to_string(jointCount)
                        + "（窗口须指向存在的关节）";
        return result;
    }

    // ---- ⑤⑥ 数值档（奇异/病态）：单侧 Jacobi SVD 重算（I-MDL-11"可逆、
    // 条件数 ≤1×10⁸"以实测为准；申报值不参与判定——防申报失真绕过）。
    // 阈值唯一书写点＝CouplingMath（P-MDL-7——modeling 不私设第二常量；
    // P-RT-7 冻结时同步）。
    const std::vector<double> sigma =
        couplingmath::couplingSingularValues(coupling.c, n);
    const double kappa = couplingmath::couplingConditionNumber(sigma);
    result.recomputedConditionNumber = kappa;
    const double sigmaMax = sigma.empty() ? 0.0 : sigma.front();
    const double sigmaMin = sigma.empty() ? 0.0 : sigma.back();
    const double sigmaRatio =
        (sigmaMax > 0.0 && std::isfinite(kappa)) ? sigmaMin / sigmaMax : 0.0;
    result.singularSigmaRatio = sigmaRatio;

    if (sigmaMin <= sigmaMax * couplingmath::kCouplingSingularSigmaRatio) {
        // 奇异档：σmin 相对 σmax 已到双精度有效零（det≈0 不可逆）——
        // κ 理论上 ≥1×10¹²（浮点下溢时为无穷），比较要素以有限比值承载
        // （actual=σmin/σmax、expected=分界之上——ERR-01 不伪造有限 κ）。
        result.violationKind = "singular";
        result.detail = "数值奇异（σmin/σmax=" + formatSigmaRatio(sigmaRatio)
                        + " ≤ 奇异分界 1×10⁻¹²——det≈0，矩阵不可逆）";
        return result;
    }
    if (!(kappa <= couplingmath::kCouplingConditionNumberLimit)) {
        // 病态档：重算 κ 超上限（比较要素 actual=重算 κ、expected=1×10⁸、
        // 单位 "1"——V-18 反例条件数 1×10⁹ 落本档阻断）。
        result.violationKind = "ill-conditioned";
        result.detail = "病态矩阵（重算条件数 κ=σmax/σmin="
                        + formatSigmaRatio(kappa) + " > 上限 1×10⁸——"
                        "映射数值不稳定，I-MDL-11）";
        return result;
    }

    // ---- ⑦ 通过：实测 κ 作为申报参考回填事实面（不改动输入对象——
    // 申报值是否更新由编辑流决定）。
    return result;
}

// =====================================================================
// 部件编辑流（WP-13-T10——§4.6/D-MDL-3/MDL-17；接口契约见 Parts.hpp）
// =====================================================================

bool isReservedPoseKey(std::string_view key) noexcept
{
    // §4.6 两保留键的字面集合成员测试（编译期常量——键值冻结不改拼）。
    return key == kHomeConfigurationPoseKey || key == kZeroConfigurationPoseKey;
}

PoseEditOutcome mergeNamedPoseEntries(const std::optional<PoseSet>& baseline,
                                      std::vector<PoseSetEntry> userEntries,
                                      std::size_t rootJointCount)
{
    // ---- ① 编辑条目校验（按编辑序首个违例即拒绝——确定性；不产出半成品
    // 合并产物，NFR-COR-03 不静默修复/跳过）----
    for (std::size_t i = 0; i < userEntries.size(); ++i) {
        const PoseSetEntry& e = userEntries[i];
        if (e.key.empty()) {
            // 空键＝引用锚缺失（key 是会话复位/导出消费的编址键）。
            PoseEditOutcome out;
            out.code = PoseEditErrorCode::EmptyKey;
            out.subject = "entries[" + std::to_string(i) + "]";
            return out;
        }
        if (isReservedPoseKey(e.key)) {
            // 保留键越出面：MDL-17"除 Home/Zero 外保存、命名与恢复"——
            // 用户命名位姿管理不得写保留键（其读取归 ui 会话复位，KIN-06）。
            PoseEditOutcome out;
            out.code = PoseEditErrorCode::ReservedKeyInEdit;
            out.subject = e.key;
            return out;
        }
        for (std::size_t j = i + 1; j < userEntries.size(); ++j) {
            if (e.key == userEntries[j].key) {
                // 键重复＝同键二义（I-MDL-2 唯一性的条目面）。
                PoseEditOutcome out;
                out.code = PoseEditErrorCode::DuplicateKey;
                out.subject = e.key;
                return out;
            }
        }
        if (e.jointConfiguration.size() != rootJointCount) {
            // 关节序一一对应（§4.6 字面）：长度＝根关节表长度（含 Fixed
            // 表序位——对照 §4.7"逐可动关节"的有意区分，取卡面字面）。
            PoseEditOutcome out;
            out.code = PoseEditErrorCode::JointOrderMismatch;
            out.subject = e.key;
            return out;
        }
    }

    // ---- ② 保留键保留（V-27 建模侧）：基线保留键条目原样带入——用户
    // 位姿编辑永不破坏 Home/Zero 参考键（复位走会话命令零修订，KIN-06）。
    std::vector<PoseSetEntry> merged;
    if (baseline.has_value()) {
        for (const PoseSetEntry& e : baseline->entries) {
            if (isReservedPoseKey(e.key)) {
                merged.push_back(e);
            }
        }
    }

    // ---- ③ 规范化输出：用户条目全集替换基线非保留条目（编辑提交＝完整
    // 用户位姿清单），合并后按 key 字典序排列（codec canonical 序——
    // putPoseSet 排序域，编解码往返零重排）。 ----
    merged.insert(merged.end(),
                  std::make_move_iterator(userEntries.begin()),
                  std::make_move_iterator(userEntries.end()));
    std::sort(merged.begin(), merged.end(),
              [](const PoseSetEntry& a, const PoseSetEntry& b) { return a.key < b.key; });

    PoseSet result;
    if (baseline.has_value()) {
        result.objectId = baseline->objectId;  // 同一对象的新版本——身份跨修订稳定（ARC-04）
    }
    result.entries = std::move(merged);

    PoseEditOutcome out;
    out.code = PoseEditErrorCode::Ok;
    out.merged = std::move(result);
    return out;
}

// =====================================================================
// 部件位姿编辑流（UI-T55——F-497 兑现③；§4.4/§4.5）
// =====================================================================

std::string_view partPoseEditErrorCodeToken(PartPoseEditErrorCode code) noexcept
{
    switch (code) {
    case PartPoseEditErrorCode::ValueNotFinite: return "value-not-finite";
    }
    return "value-not-finite";  // 全枚举已覆盖，不达此处（无 default——漏项编译器告警）
}

namespace {

/**
 * @brief 六分量有限性检查（两消费面同一判定——I-MDL-3；拒绝面单一实现，
 *        NFR-MNT-04）。
 */
std::optional<PartPoseEditError> partPoseFiniteCheck(const PartPoseEditValue& value,
                                                     const std::string& subject)
{
    const double comps[6] = {value.x, value.y, value.z,
                             value.roll, value.pitch, value.yaw};
    for (const double c : comps) {
        if (!std::isfinite(c)) {
            return PartPoseEditError{PartPoseEditErrorCode::ValueNotFinite,
                                     subject + "：位姿含非有限分量（NaN/Inf）"};
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<PartPoseEditError> applyToolMountEdit(ModelingWorkingSet& ws,
                                                    std::size_t toolIndex,
                                                    const PartPoseEditValue& value)
{
    // ① 越界＝调用方契约违约（fail-fast——与 applyJointFieldEdit ①同款）。
    if (toolIndex >= ws.toolObjects.size()) {
        throw std::invalid_argument(
            "modeling/parts/tool-mount-edit-index: 工具下标越界: tools["
            + std::to_string(toolIndex) + "]");
    }
    const std::string subject = "tools[" + std::to_string(toolIndex) + "]";
    // ② 六分量有限性（I-MDL-3）。
    if (const auto err = partPoseFiniteCheck(value, subject)) { return err; }
    // ③ 提交：mountInterface 直写（Transform3D 值语义——§4.4 表行；旋转
    // 经域内核唯一正解组合 rpymath，ZYX 约定——法兰系 T_flange_tool）。
    auto& tool = ws.toolObjects[toolIndex];
    tool.mountInterface = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(value.x, value.y, value.z),
        rpymath::rpyToRotation(value.roll, value.pitch, value.yaw));
    ModelingChangeRecord record;
    record.subject = subject;
    record.summary = "修改工具安装接口（法兰系 T_flange_tool，m/rad——ZYX 约定）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::optional<PartPoseEditError> applyScenePoseEdit(ModelingWorkingSet& ws,
                                                    std::size_t sceneIndex,
                                                    const PartPoseEditValue& value)
{
    // ①/②/③ 与工具面同构（对象表/字段/subject 差异化——共用有限性判定）。
    if (sceneIndex >= ws.sceneObjects.size()) {
        throw std::invalid_argument(
            "modeling/parts/scene-pose-edit-index: 场景下标越界: scenes["
            + std::to_string(sceneIndex) + "]");
    }
    const std::string subject = "scenes[" + std::to_string(sceneIndex) + "]";
    if (const auto err = partPoseFiniteCheck(value, subject)) { return err; }
    auto& scene = ws.sceneObjects[sceneIndex];
    scene.worldPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(value.x, value.y, value.z),
        rpymath::rpyToRotation(value.roll, value.pitch, value.yaw));
    ModelingChangeRecord record;
    record.subject = subject;
    record.summary = "修改场景世界位姿（世界系固连，m/rad——M-11 不预乘安装旋转）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

// =====================================================================
// TCP 列表结构化编辑流（UI-T57——F-497 兑现④；MDL-13 不变量流；UI-T58
// 增第五原语 applyTcpDisplayNameEdit——displayName 仅呈现字段编辑）
// =====================================================================

std::string_view tcpEditErrorCodeToken(TcpEditErrorCode code) noexcept
{
    switch (code) {
    case TcpEditErrorCode::KeyEmpty: return "key-empty";
    case TcpEditErrorCode::KeyDuplicate: return "key-duplicate";
    case TcpEditErrorCode::KeyNotFound: return "key-not-found";
    case TcpEditErrorCode::LastTcpProtected: return "last-tcp-protected";
    case TcpEditErrorCode::DefaultTcpReferenced: return "default-tcp-referenced";
    case TcpEditErrorCode::ValueNotFinite: return "value-not-finite";
    }
    return "value-not-finite";  // 全枚举已覆盖，不达此处
}

namespace {

/// TCP 键查找（返回条目下标；未命中＝nullopt）。
std::optional<std::size_t> tcpKeyIndex(const ToolDefinition& tool,
                                       const std::string& tcpKey)
{
    for (std::size_t i = 0; i < tool.tcpList.size(); ++i) {
        if (tool.tcpList[i].key == tcpKey) { return i; }
    }
    return std::nullopt;
}

/// TCP offset 六分量有限性检查（与部件位姿面同一判定口径）。
std::optional<TcpEditError> tcpOffsetFiniteCheck(const PartPoseEditValue& value,
                                                 const std::string& subject)
{
    const double comps[6] = {value.x, value.y, value.z,
                             value.roll, value.pitch, value.yaw};
    for (const double c : comps) {
        if (!std::isfinite(c)) {
            return TcpEditError{TcpEditErrorCode::ValueNotFinite,
                                subject + "：offset 含非有限分量（NaN/Inf）"};
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<TcpEditError> applyTcpAddEdit(ModelingWorkingSet& ws,
                                            std::size_t toolIndex,
                                            const std::string& key,
                                            const std::string& displayName,
                                            const PartPoseEditValue& offset)
{
    // ① 越界 fail-fast。
    if (toolIndex >= ws.toolObjects.size()) {
        throw std::invalid_argument(
            "modeling/parts/tcp-add-index: 工具下标越界: tools["
            + std::to_string(toolIndex) + "]");
    }
    const std::string subject = "tools[" + std::to_string(toolIndex) + "]";
    // ② 键非空（I-MDL-13：键是 defaultTcp 引用锚——空键即引用锚损坏面）。
    if (key.empty()) {
        return TcpEditError{TcpEditErrorCode::KeyEmpty,
                            subject + "：TCP 键为空（I-MDL-13——键须非空唯一）"};
    }
    // ③ 键集合内唯一（I-MDL-13）。
    auto& tool = ws.toolObjects[toolIndex];
    if (tcpKeyIndex(tool, key).has_value()) {
        return TcpEditError{TcpEditErrorCode::KeyDuplicate,
                            subject + "：TCP 键重复：" + key};
    }
    // ④ offset 有限性（I-MDL-3）。
    if (const auto err = tcpOffsetFiniteCheck(offset, subject)) { return err; }
    // ⑤ 提交：offset 经 ZYX 正解组合；displayName 原样。
    TcpEntry entry;
    entry.key = key;
    entry.displayName = displayName;
    entry.offset = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(offset.x, offset.y, offset.z),
        rpymath::rpyToRotation(offset.roll, offset.pitch, offset.yaw));
    tool.tcpList.push_back(std::move(entry));
    ModelingChangeRecord record;
    record.subject = subject + ".tcpList";
    record.summary = "新增 TCP 条目（键 " + key + "——tcp 系相对安装接口，m/rad）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::optional<TcpEditError> applyTcpRemoveEdit(ModelingWorkingSet& ws,
                                               std::size_t toolIndex,
                                               const std::string& tcpKey)
{
    // ① 越界 fail-fast。
    if (toolIndex >= ws.toolObjects.size()) {
        throw std::invalid_argument(
            "modeling/parts/tcp-remove-index: 工具下标越界: tools["
            + std::to_string(toolIndex) + "]");
    }
    auto& tool = ws.toolObjects[toolIndex];
    const std::string subject = "tools[" + std::to_string(toolIndex) + "]";
    // ② 键须存在。
    const auto idx = tcpKeyIndex(tool, tcpKey);
    if (!idx.has_value()) {
        return TcpEditError{TcpEditErrorCode::KeyNotFound,
                            subject + "：TCP 键不存在：" + tcpKey};
    }
    // ③ defaultTcp 引用保护（I-MDL-9——RemoveObjectRefEdit 同款语义；
    // 先于最后一条保护——引用保护的指引更明确：先切换默认 TCP）。
    if (ws.design.defaultTcp.has_value()
        && ws.design.defaultTcp->tcpKey == tcpKey
        && ws.design.defaultTcp->toolOid == tool.objectId) {
        return TcpEditError{
            TcpEditErrorCode::DefaultTcpReferenced,
            subject + "：TCP 键被根 defaultTcp 引用：" + tcpKey
                + "——请先切换默认 TCP（引用保护，I-MDL-9）"};
    }
    // ④ 最后一条保护（I-MDL-13：tcpList ≥1——删除即空表违例）。
    if (tool.tcpList.size() == 1) {
        return TcpEditError{TcpEditErrorCode::LastTcpProtected,
                            subject + "：最后一条 TCP 不得删除"
                                      "（I-MDL-13——tcpList ≥1）"};
    }
    // ⑤ 提交＋一条变更记录。
    tool.tcpList.erase(tool.tcpList.begin() + static_cast<std::ptrdiff_t>(*idx));
    ModelingChangeRecord record;
    record.subject = subject + ".tcpList";
    record.summary = "删除 TCP 条目（键 " + tcpKey + "）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::optional<TcpEditError> applyTcpOffsetEdit(ModelingWorkingSet& ws,
                                               std::size_t toolIndex,
                                               const std::string& tcpKey,
                                               const PartPoseEditValue& offset)
{
    // ① 越界 fail-fast。
    if (toolIndex >= ws.toolObjects.size()) {
        throw std::invalid_argument(
            "modeling/parts/tcp-offset-edit-index: 工具下标越界: tools["
            + std::to_string(toolIndex) + "]");
    }
    auto& tool = ws.toolObjects[toolIndex];
    const std::string subject = "tools[" + std::to_string(toolIndex) + "].tcp(" + tcpKey + ")";
    // ② 键须存在。
    const auto idx = tcpKeyIndex(tool, tcpKey);
    if (!idx.has_value()) {
        return TcpEditError{TcpEditErrorCode::KeyNotFound,
                            subject + "：TCP 键不存在"};
    }
    // ③ offset 有限性（I-MDL-3）。
    if (const auto err = tcpOffsetFiniteCheck(offset, subject)) { return err; }
    // ④ 提交：offset 直写（tcp 系相对安装接口——core.md §4.6 T_ab 约定）。
    tool.tcpList[*idx].offset = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(offset.x, offset.y, offset.z),
        rpymath::rpyToRotation(offset.roll, offset.pitch, offset.yaw));
    ModelingChangeRecord record;
    record.subject = subject;
    record.summary = "修改 TCP 安装偏移（键 " + tcpKey + "，m/rad——ZYX 约定）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::optional<TcpEditError> applyDefaultTcpSwitchEdit(ModelingWorkingSet& ws,
                                                      std::size_t toolIndex,
                                                      const std::string& tcpKey)
{
    // ① 越界 fail-fast。
    if (toolIndex >= ws.toolObjects.size()) {
        throw std::invalid_argument(
            "modeling/parts/default-tcp-switch-index: 工具下标越界: tools["
            + std::to_string(toolIndex) + "]");
    }
    auto& tool = ws.toolObjects[toolIndex];
    // ② 键须存在（引用锚不得悬空——I-MDL-9/KIN-14 前置）。
    if (!tcpKeyIndex(tool, tcpKey).has_value()) {
        return TcpEditError{TcpEditErrorCode::KeyNotFound,
                            "tools[" + std::to_string(toolIndex)
                                + "]：TCP 键不存在：" + tcpKey};
    }
    // ③ 提交：根 defaultTcp 整体写入（根字段编辑——与基座安装编辑流同址
    // 口径：本原语落位 Parts.cpp 与其消费面同址）。
    modeling::TcpRef ref;
    ref.toolOid = tool.objectId;
    ref.tcpKey = tcpKey;
    ws.design.defaultTcp = ref;
    ModelingChangeRecord record;
    record.subject = "design.defaultTcp";
    record.summary = "切换默认 TCP（键 " + tcpKey + "）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::optional<TcpEditError> applyTcpDisplayNameEdit(ModelingWorkingSet& ws,
                                                    std::size_t toolIndex,
                                                    const std::string& tcpKey,
                                                    const std::string& displayName)
{
    // ① 越界 fail-fast。
    if (toolIndex >= ws.toolObjects.size()) {
        throw std::invalid_argument(
            "modeling/parts/tcp-displayname-edit-index: 工具下标越界: tools["
            + std::to_string(toolIndex) + "]");
    }
    auto& tool = ws.toolObjects[toolIndex];
    const std::string subject = "tools[" + std::to_string(toolIndex) + "].tcp(" + tcpKey + ")";
    // ② 键须存在（编辑目标锚定既有条目——键是 defaultTcp 引用锚，
    // I-MDL-13；不存在即引用锚失配，拒绝而非静默丢弃）。
    const auto idx = tcpKeyIndex(tool, tcpKey);
    if (!idx.has_value()) {
        return TcpEditError{TcpEditErrorCode::KeyNotFound,
                            subject + "：TCP 键不存在"};
    }
    // ③ 提交：displayName 直写（仅呈现字段——UX-02 口径；空串接受＝
    // 呈现侧回落按 TCP 键呈现。§4.8：displayName 改名产生新修订但
    // Description 不变——编译缓存可复用，不触发运动学失效）。
    tool.tcpList[*idx].displayName = displayName;
    ModelingChangeRecord record;
    record.subject = subject;
    record.summary = "修改 TCP 显示名（键 " + tcpKey + "——仅呈现，不入编译身份）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

// =====================================================================
// 位姿集/传动编辑流（UI-T60——F-497 余项收尾；§4.6/§4.7 编辑页承载面。
// 位姿集经 mergeNamedPoseEntries 单一合并实现——NFR-MNT-04，本段零第二
// 合并判定；传动对象缺席时的创建经 deriveDraftObjectId 草稿确定性句柄
// ——§5.2"内存编辑态先用临时句柄，提交时回填"）
// =====================================================================

std::string_view poseEditErrorCodeToken(PoseEditErrorCode code) noexcept
{
    switch (code) {
    case PoseEditErrorCode::Ok: return "ok";
    case PoseEditErrorCode::ReservedKeyInEdit: return "reserved-key-in-edit";
    case PoseEditErrorCode::EmptyKey: return "empty-key";
    case PoseEditErrorCode::DuplicateKey: return "duplicate-key";
    case PoseEditErrorCode::JointOrderMismatch: return "joint-order-mismatch";
    case PoseEditErrorCode::KeyNotFound: return "key-not-found";
    }
    return "empty-key";  // 全枚举已覆盖，不达此处
}

namespace {

/// 位姿集编辑目标的确保/创建（两原语共用——UI-T60）。
PoseSet& ensurePoseSetObject(ModelingWorkingSet& ws)
{
    if (!ws.poseSetObject.has_value()) {
        // 草稿确定性句柄（§5.2——同键同句柄；提交时命令 prepare 回填
        // 正式身份，PA-1）。批次命名空间与结构编辑同族防跨流撞句柄。
        PoseSet created;
        created.objectId = deriveDraftObjectId("ird/modeling/structure-draft/pose-set/1");
        ws.design.poseSetRef = created.objectId;
        ws.poseSetObject = std::move(created);
    }
    return *ws.poseSetObject;
}

/// 传动编辑目标的确保/创建/向量对齐（三原语共用——UI-T60）。
DrivetrainDesign& ensureDrivetrainObject(ModelingWorkingSet& ws)
{
    if (!ws.drivetrainObject.has_value()) {
        DrivetrainDesign created;
        created.objectId = deriveDraftObjectId("ird/modeling/structure-draft/drivetrain/1");
        ws.design.drivetrainRef = created.objectId;
        ws.drivetrainObject = std::move(created);
    }
    // 三向量与根关节表长度对齐（§4.7"与关节序一一对应"——值模型下标即
    // 关节序；结构编辑增删关节后由本对齐补齐/裁剪，NotProvided 补位保
    // 前缀——既有值零丢失）。
    const std::size_t jointCount = ws.design.joints.size();
    auto& dt = *ws.drivetrainObject;
    if (dt.ratioPerJoint.size() != jointCount) {
        dt.ratioPerJoint.resize(jointCount);
    }
    if (dt.frictionPerJoint.size() != jointCount) {
        dt.frictionPerJoint.resize(jointCount);
    }
    if (dt.torqueLimitsPerJoint.size() != jointCount) {
        dt.torqueLimitsPerJoint.resize(jointCount);
    }
    return dt;
}

}  // namespace

std::optional<PoseEditError> applyPoseSetEntryUpsertEdit(ModelingWorkingSet& ws,
                                                         const PoseSetEntry& entry)
{
    // ① 条目键非空（EmptyKey——引用锚不可为空）。
    if (entry.key.empty()) {
        return PoseEditError{PoseEditErrorCode::EmptyKey,
                             "位姿条目键为空（键是复位/导出消费的编址锚）"};
    }
    // ② 保留键拒绝（ReservedKeyInEdit——home/zero 的写入不属用户命名
    // 位姿管理面，MDL-17 字面；其读取归 ui 会话复位，KIN-06）。
    if (isReservedPoseKey(entry.key)) {
        return PoseEditError{PoseEditErrorCode::ReservedKeyInEdit,
                             "保留键不得经编辑面写入：" + entry.key
                                 + "（home/zero 归会话复位——KIN-06 零修订）"};
    }
    // ③ 经 mergeNamedPoseEntries 单一合并实现校验＋合并（NFR-MNT-04；
    //    同键＝覆盖语义——编辑提交＝完整用户清单的单条目增量形态）。
    //    ★ 用户集构建用拷贝（拒绝路径工作集字节不变纪律——搬移会在合并
    //    校验失败时把工作集条目留成 moved-from 空壳，UI-T60 域测试实测
    //    抓获；条目集为草稿小集合，拷贝成本可忽略）。
    std::vector<PoseSetEntry> userEntries;
    if (ws.poseSetObject.has_value()) {
        for (const PoseSetEntry& e : ws.poseSetObject->entries) {
            if (!isReservedPoseKey(e.key)) {
                userEntries.push_back(e);
            }
        }
    }
    // 同键既有条目先剔除再压入（覆盖语义——C++17 无 vector erase_if，
    // 手写剔除循环）。
    std::vector<PoseSetEntry> merged1;
    merged1.reserve(userEntries.size());
    for (PoseSetEntry& e : userEntries) {
        if (e.key != entry.key) { merged1.push_back(std::move(e)); }
    }
    merged1.push_back(entry);
    const PoseEditOutcome outcome = mergeNamedPoseEntries(
        ws.poseSetObject, std::move(merged1), ws.design.joints.size());
    if (outcome.code != PoseEditErrorCode::Ok) {
        // 校验失败透传（EmptyKey/ReservedKey/Duplicate/OrderMismatch——
        // 合并实现的拒绝面即本原语的拒绝面，零转译失真）。
        return PoseEditError{outcome.code, outcome.subject};
    }
    // ④ 提交：对象缺席→草稿句柄创建＋根引用写入；合并产物整体写入
    // （保留键由合并实现原样带回——V-27 建模侧）＋恰一条变更记录。
    ensurePoseSetObject(ws);
    ws.poseSetObject->entries = outcome.merged->entries;
    ModelingChangeRecord record;
    record.subject = "poseSet.entries";
    record.summary = "保存命名位姿（键 " + entry.key + "——与关节序一一对应，rad/m）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::optional<PoseEditError> applyPoseSetEntryRemoveEdit(ModelingWorkingSet& ws,
                                                         const std::string& key)
{
    // ① 保留键拒绝（ ReservedKeyInEdit——保留键条目原样保留，V-27）。
    if (isReservedPoseKey(key)) {
        return PoseEditError{PoseEditErrorCode::ReservedKeyInEdit,
                             "保留键不得经编辑面删除：" + key
                                 + "（home/zero 归会话复位——KIN-06 零修订）"};
    }
    // ② 键须存在于用户条目集（KeyNotFound——UI-T60 表尾追加值）。
    //    ★ 拷贝构建（拒绝路径字节不变——同 upsert 段注）。
    std::vector<PoseSetEntry> userEntries;
    bool found = false;
    if (ws.poseSetObject.has_value()) {
        for (const PoseSetEntry& e : ws.poseSetObject->entries) {
            if (isReservedPoseKey(e.key)) { continue; }
            if (e.key == key) {
                found = true;
                continue;  // 剔除目标——不入用户集
            }
            userEntries.push_back(e);
        }
    }
    if (!found) {
        return PoseEditError{PoseEditErrorCode::KeyNotFound,
                             "位姿键不存在：" + key};
    }
    // ③ 经合并实现（剩余用户集合法性随剔除断言——合并实现复核）＋写入
    // ＋恰一条变更记录。删空用户集＝位姿集仅剩保留键（合法态）。
    const PoseEditOutcome outcome = mergeNamedPoseEntries(
        ws.poseSetObject, std::move(userEntries), ws.design.joints.size());
    if (outcome.code != PoseEditErrorCode::Ok) {
        return PoseEditError{outcome.code, outcome.subject};
    }
    ensurePoseSetObject(ws);
    ws.poseSetObject->entries = outcome.merged->entries;
    ModelingChangeRecord record;
    record.subject = "poseSet.entries";
    record.summary = "删除命名位姿（键 " + key + "）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::string_view drivetrainEditErrorCodeToken(DrivetrainEditErrorCode code) noexcept
{
    switch (code) {
    case DrivetrainEditErrorCode::ValueNotFinite: return "value-not-finite";
    case DrivetrainEditErrorCode::RatioNotPositive: return "ratio-not-positive";
    case DrivetrainEditErrorCode::KeyNotFound: return "key-not-found";
    }
    return "value-not-finite";  // 全枚举已覆盖，不达此处
}

std::optional<DrivetrainEditError> ensureDrivetrainEditTarget(ModelingWorkingSet& ws,
                                                              std::size_t jointIndex)
{
    // 越界 fail-fast（调用方契约违约——UI-T55 部件位姿编辑同款口径）。
    if (jointIndex >= ws.design.joints.size()) {
        throw std::invalid_argument(
            "modeling/parts/drivetrain-edit-index: 关节下标越界: joints["
            + std::to_string(jointIndex) + "]");
    }
    // 缺席创建/向量对齐不产生错误（纯确保步）——错误面由各原语的值校验
    // 半段产出。
    ensureDrivetrainObject(ws);
    return std::nullopt;
}

std::optional<DrivetrainEditError> applyDrivetrainRatioEdit(ModelingWorkingSet& ws,
                                                            std::size_t jointIndex,
                                                            double ratio)
{
    // ① 确保目标（缺席创建/对齐/越界 fail-fast）。
    if (const auto err = ensureDrivetrainEditTarget(ws, jointIndex)) { return err; }
    // ② 有限性（I-MDL-3）＋正值（I-MDL-11：无量纲比有限且 >0）。
    if (!std::isfinite(ratio)) {
        return DrivetrainEditError{DrivetrainEditErrorCode::ValueNotFinite,
                                   "joints[" + std::to_string(jointIndex)
                                       + "]：传动比含非有限分量（NaN/Inf）"};
    }
    if (ratio <= 0.0) {
        return DrivetrainEditError{DrivetrainEditErrorCode::RatioNotPositive,
                                   "joints[" + std::to_string(jointIndex)
                                       + "]：传动比须 >0（I-MDL-11）"};
    }
    // ③ SourcedValue UserProvided 写入（MDL-05 显式权威一等值）＋恰一条
    // 变更记录（无量纲——SI 系数 1）。
    ws.drivetrainObject->ratioPerJoint[jointIndex] =
        core::SourcedValue<double>::provided(ratio, core::ValueProvenance::make(
                                                        core::ProvenanceKind::UserProvided,
                                                        std::nullopt, std::nullopt,
                                                        std::string("panel-edit")));
    ModelingChangeRecord record;
    record.subject = "drivetrain.ratioPerJoint[" + std::to_string(jointIndex) + "]";
    record.summary = "修改传动比（关节序 " + std::to_string(jointIndex)
                   + "，无量纲——R1 可编辑 OPT StageB 变量）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::optional<DrivetrainEditError> applyDrivetrainFrictionEdit(ModelingWorkingSet& ws,
                                                               std::size_t jointIndex,
                                                               double viscous,
                                                               double coulomb,
                                                               double bias)
{
    // ① 确保目标。
    if (const auto err = ensureDrivetrainEditTarget(ws, jointIndex)) { return err; }
    // ② 三分量逐项有限性（I-MDL-3——Viscous/Coulomb/Bias 任一非有限即
    // 整组拒绝，不产出半成品三元）。
    const double comps[3] = {viscous, coulomb, bias};
    const char* names[3] = {"viscous", "coulomb", "bias"};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(comps[i])) {
            return DrivetrainEditError{
                DrivetrainEditErrorCode::ValueNotFinite,
                "joints[" + std::to_string(jointIndex) + "]：摩擦 " + names[i]
                    + " 含非有限分量（NaN/Inf）"};
        }
    }
    // ③ SourcedValue UserProvided 三元写入＋恰一条变更记录（单位随关节
    // 类型：转动 N·m·s/rad＋N·m；移动 N·s/m＋N——SI 真值直写）。
    auto provenance = core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                                  std::nullopt, std::nullopt,
                                                  std::string("panel-edit"));
    FrictionEntry& entry = ws.drivetrainObject->frictionPerJoint[jointIndex];
    entry.viscous = core::SourcedValue<double>::provided(viscous, provenance);
    entry.coulomb = core::SourcedValue<double>::provided(coulomb, provenance);
    entry.bias = core::SourcedValue<double>::provided(bias, provenance);
    ModelingChangeRecord record;
    record.subject = "drivetrain.frictionPerJoint[" + std::to_string(jointIndex) + "]";
    record.summary = "修改关节摩擦（关节序 " + std::to_string(jointIndex)
                   + "，三元 fv/fc/bias——DYN-06 缺失降级解除）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

std::optional<DrivetrainEditError> applyDrivetrainTorqueLimitEdit(ModelingWorkingSet& ws,
                                                                  std::size_t jointIndex,
                                                                  double rated,
                                                                  double peak)
{
    // ① 确保目标。
    if (const auto err = ensureDrivetrainEditTarget(ws, jointIndex)) { return err; }
    // ② 两分量逐项有限性（I-MDL-3）。
    const double comps[2] = {rated, peak};
    const char* names[2] = {"rated", "peak"};
    for (std::size_t i = 0; i < 2; ++i) {
        if (!std::isfinite(comps[i])) {
            return DrivetrainEditError{
                DrivetrainEditErrorCode::ValueNotFinite,
                "joints[" + std::to_string(jointIndex) + "]：力矩限值 " + names[i]
                    + " 含非有限分量（NaN/Inf）"};
        }
    }
    // ③ SourcedValue UserProvided 对写入＋恰一条变更记录（单位随关节
    // 类型：转动 N·m；移动 N——不进 CanonicalModel，消费方 SEL/DYN）。
    auto provenance = core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                                  std::nullopt, std::nullopt,
                                                  std::string("panel-edit"));
    TorqueLimitEntry& entry = ws.drivetrainObject->torqueLimitsPerJoint[jointIndex];
    entry.rated = core::SourcedValue<double>::provided(rated, provenance);
    entry.peak = core::SourcedValue<double>::provided(peak, provenance);
    ModelingChangeRecord record;
    record.subject = "drivetrain.torqueLimitsPerJoint[" + std::to_string(jointIndex) + "]";
    record.summary = "修改力矩限值（关节序 " + std::to_string(jointIndex)
                   + "，额定/峰值——驱动工作点评估输入）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

// =====================================================================
// 耦合矩阵编辑流（WP-13-T18——§4.7 coupling 一等字段；MDL-21/R2）
// =====================================================================

std::string_view couplingEditErrorCodeToken(CouplingEditErrorCode code) noexcept
{
    switch (code) {
    case CouplingEditErrorCode::StageLocked: return "stage-locked";
    case CouplingEditErrorCode::NotSquare: return "not-square";
    case CouplingEditErrorCode::ElementNotFinite: return "element-not-finite";
    case CouplingEditErrorCode::WindowMismatch: return "window-mismatch";
    case CouplingEditErrorCode::WindowOutOfRange: return "window-out-of-range";
    case CouplingEditErrorCode::Singular: return "singular";
    case CouplingEditErrorCode::IllConditioned: return "ill-conditioned";
    }
    return "stage-locked";  // 全枚举已覆盖，不达此处
}

std::optional<CouplingEditError> applyDrivetrainCouplingEdit(
    ModelingWorkingSet& ws,
    CouplingStage stage,
    std::optional<CouplingDesign> coupling)
{
    // ---- ① 阶段锁（I-MDL-12）：R1 能力位下配置即阻断（"不提前放开 R1
    // 阻断"红线——MDL-12/21 R1 口径；稳定码 MDL-21-COUPLING-STAGE-LOCKED
    // 的诊断面在命令 prepare 断言产出，编辑边界为值面拒绝）。清除面
    // （nullopt）不是"配置"——两态均放行（"阶段 D 启用前移除"的修复
    // 动作即经此面）。
    if (stage == CouplingStage::R1Locked && coupling.has_value()) {
        return CouplingEditError{
            CouplingEditErrorCode::StageLocked,
            "当前程序阶段为 R1：耦合矩阵禁止配置（I-MDL-12 存在即阻断——"
            "MDL-21 于阶段 D 启用；阶段 D 启用前请移除该配置）"};
    }

    // ---- ② 权威编辑守卫语义（C-1 单一判定——见头文件注）：coupling 为
    // 传动对象字段，不在 AuthorityLockedField 受管字段轴（两态均权威族
    // ——同 type/bounds）；本原语不做权威模式拒绝（不发明第二套 C-1）。
    // （无运行时动作——判定语义以测试钉扎：StandardDH 态合法 C 照常入
    // 修订。）

    // ---- ③ 清除面直写（nullopt＝移除配置——合法性无矩阵面可校验）。
    if (!coupling.has_value()) {
        if (ws.drivetrainObject.has_value() && ws.drivetrainObject->coupling.has_value()) {
            ws.drivetrainObject->coupling = std::nullopt;
            ModelingChangeRecord record;
            record.subject = "drivetrain.coupling";
            record.summary = "清除耦合矩阵配置（robot-drivetrain.coupling）";
            ws.changes.push_back(std::move(record));
        }
        // 无配置可清除＝幂等清除（接受，零变更记录——工作集字节本就未变）。
        return std::nullopt;
    }

    // ---- ④ 传动对象确保（缺席创建＋根引用写入——与 UI-T60 三原语同款
    // 前置；ensureDrivetrainObject 私有辅助无下标入参、不抛）。
    ensureDrivetrainObject(ws);

    // ---- ⑤ 数值校验（checkCouplingMatrix——I-MDL-11 重算复核；三处
    // 共用同一判定：编辑边界/prepare/L9——NFR-MNT-04）。jointCount＝根
    // 关节表长度（窗口越界判据）。
    const CouplingMatrixCheck check =
        checkCouplingMatrix(*coupling, ws.design.joints.size());
    if (!check.ok()) {
        // 违例种类→编辑错误码（一一对应——值面拒绝不降级不静默，M-12；
        // detail 携带比较型三要素素材——诊断记录组装归产码点）。
        CouplingEditErrorCode code = CouplingEditErrorCode::IllConditioned;
        if (check.violationKind == "not-square") {
            code = CouplingEditErrorCode::NotSquare;
        } else if (check.violationKind == "element-not-finite") {
            code = CouplingEditErrorCode::ElementNotFinite;
        } else if (check.violationKind == "window-mismatch") {
            code = CouplingEditErrorCode::WindowMismatch;
        } else if (check.violationKind == "window-out-of-range") {
            code = CouplingEditErrorCode::WindowOutOfRange;
        } else if (check.violationKind == "singular") {
            code = CouplingEditErrorCode::Singular;
        } else if (check.violationKind == "ill-conditioned") {
            code = CouplingEditErrorCode::IllConditioned;
        }
        return CouplingEditError{code, check.detail};
    }

    // ---- ⑥ 提交：coupling 整体写入＋恰一条变更记录（append-only）。
    ws.drivetrainObject->coupling = std::move(*coupling);
    ModelingChangeRecord record;
    record.subject = "drivetrain.coupling";
    record.summary = "配置腕部关节窗口线性耦合矩阵（robot-drivetrain.coupling——"
                     "常矩阵 "
                   + std::to_string(ws.drivetrainObject->coupling->rows)
                   + "×"
                   + std::to_string(ws.drivetrainObject->coupling->cols)
                   + "，窗口 ["
                   + std::to_string(ws.drivetrainObject->coupling->jointRangeFirst)
                   + ","
                   + std::to_string(ws.drivetrainObject->coupling->jointRangeLast)
                   + "]，无量纲）";
    ws.changes.push_back(std::move(record));
    return std::nullopt;
}

}  // namespace sdurws::ird::modeling
