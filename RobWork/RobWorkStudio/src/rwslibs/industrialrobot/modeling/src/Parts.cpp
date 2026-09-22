/**
 * @file   Parts.cpp
 * @brief  四部件对象值模型的实现——值相等、耦合阶段 token 与部件级
 *         不变量核查（工具 I-MDL-5；传动 I-MDL-11/I-MDL-12）。
 *
 * 设计依据：units/modeling.md §4.4/§4.7/§4.10、§9.5（MDL-21 码行——T18/R2
 * 注册，本文件只产出值面违例）；任务契约 tasks/foundation/WP-13-T03.json
 * acceptance 1/3。全部纯函数（并发安全；确定性 NFR-COR-02）。
 */

#include <sdurws/ird/modeling/Parts.hpp>

#include <cmath>

#include "InertiaMath.hpp"  // 单元私有：惯量 SPD＋三角不等式单一实现（I-MDL-5——与连杆同源）

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

}  // namespace

std::vector<InvariantViolation> checkInvariants(const ToolDefinition& tool)
{
    std::vector<InvariantViolation> out;

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
        // 条件数自洽：正实数且 ≤1×10⁸（P-RT-7 设计默认——阈值来源 P-MDL-7
        // 登记，modeling 不私设第二常量；1e8 字面与 runtime 设计默认同值，
        // 该值冻结后经单源常量替换——登记于单元卡 §15 增量）。条件数与
        // 可逆性的重算复核（SVD）随 T18/R2 经 runtime 单源实现（§8.1），
        // 本处只核查已登记字段的自洽范围。
        if (!(std::isfinite(cp.conditionNumber) && cp.conditionNumber > 0.0
              && cp.conditionNumber <= 1e8)) {
            addViolation(out, InvariantId::IMdl11, "coupling.conditionNumber");
        }
        // 窗口自洽：闭区间 [i,j] 不得倒序（适用关节窗口语义——§4.7 jointRange 行）
        if (cp.jointRangeFirst > cp.jointRangeLast) {
            addViolation(out, InvariantId::IMdl11, "coupling.jointRange");
        }
    }

    return out;
}

}  // namespace sdurws::ird::modeling
