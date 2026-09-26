/**
 * @file   Bounds.cpp
 * @brief  解析工作半径界限实现——TCP 解析、逐段平移贡献累加、推导输入
 *         确定性格式化与解析界限证明素材组装。
 *
 * 设计依据（契约面见 Bounds.hpp 文件头）：units/kinematics.md §5.4 结局 5
 * （"连杆长度和，静态可验证、覆盖全部可能解"）、§8.2（解析界限素材＝
 * 工作半径推导输入＋目标距离）、evidence §6.3（proof 字段面与两规范
 * 覆盖声明常量）；任务契约 WP-15-T04 acceptance 4。
 *
 * 实现纪律：rw::math 值类型仅用头内 inline 面（元素访问/构造）——两
 * 模式可链接（Fk.cpp 同款登记）；浮点定序固定（逐关节单遍累加）；文本
 * 格式化定点 "%.17g"（C 默认 locale，确定性来源——文件头注）。
 */

#include <sdurws/ird/kinematics/Bounds.hpp>

#include <sdurws/ird/kinematics/Ik.hpp>  // kTaskPointIkEvaluationKey/kIkSolverContractVersion——producer 绑定（唯一书写点常量）

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// TCP 解析（KinTypes.hpp 解析规则的 Bounds 侧执行面——Fk.cpp 同规则；
// 单一语义两处以服务解耦为界，黄金算例两侧互证）
// =====================================================================

/// 错误值构造：无可用设备（§9.6 KIN-NO-DEVICE——结构化错误素材）。
KinematicsError makeNoDevice()
{
    KinematicsError e;
    e.code = KinematicsErrorCode::NoDevice;
    e.detail = "快照模型无可用设备链（KIN-NO-DEVICE 素材——§9.6）";
    return e;
}

/// 错误值构造：TCP 未配置/悬空（§9.6 KIN-NO-TCP 两分语义）。
KinematicsError makeNoTcp(const char* variantDetail)
{
    KinematicsError e;
    e.code = KinematicsErrorCode::NoTcp;
    e.detail = variantDetail;
    return e;
}

/// 错误值构造：TCP 帧未解析（§9.2 @错误 行 FrameUnresolved）。
KinematicsError makeFrameUnresolved(const std::string& tcpKey, const std::string& toolName)
{
    KinematicsError e;
    e.code = KinematicsErrorCode::FrameUnresolved;
    e.params.emplace_back("tcp-key", tcpKey);
    e.params.emplace_back("tool-local-name", toolName);
    e.detail = "tcpKey 不命中 canonical TCP 身份（帧未解析——R-4 禁拼串定位）";
    return e;
}

/**
 * @brief 解析 TCP 引用到工具（KinTypes.hpp 解析规则——空键＝canonical
 *        TCP、非空须与工具 localName 精确相等）。
 *
 * @return 成功＝工具引用；失败＝NoTcp（未配置/悬空两分支）或
 *         FrameUnresolved（键不命中）
 */
Expected<const runtime::CanonicalTool*> resolveTcp(const IKinRuntimeView& view,
                                                   const TcpRef& tcp)
{
    const runtime::CanonicalModel& model = view.model();
    const std::vector<runtime::CanonicalTool>& tools = model.tools();
    if (tools.empty()) {
        // 未配置：快照模型无任何工具（KIN-NO-TCP"TCP 未配置"分支）。
        return Expected<const runtime::CanonicalTool*>::err(
            makeNoTcp("TCP 未配置：快照模型无工具（KIN-NO-TCP 素材——§9.6）"));
    }
    const auto loc = model.findObject(tcp.toolObject);
    if (!loc.has_value() || loc->kind != runtime::CanonicalModel::ObjectKind::Tool) {
        // 悬空：指名对象不在快照内、或不是工具定义（"引用悬空"分支）。
        return Expected<const runtime::CanonicalTool*>::err(
            makeNoTcp("TCP 引用悬空：toolObject 未解析到快照工具（KIN-NO-TCP 素材——§9.6）"));
    }
    const runtime::CanonicalTool& tool = tools.at(loc->index);
    if (!tcp.tcpKey.empty() && tcp.tcpKey != tool.localName) {
        return Expected<const runtime::CanonicalTool*>::err(
            makeFrameUnresolved(tcp.tcpKey, tool.localName));
    }
    return Expected<const runtime::CanonicalTool*>::ok(&tool);
}

// =====================================================================
// 数值格式化（确定文本——proof.boundExpression 的推导输入面）
// =====================================================================

/// "%.17g" 定点格式化（C 默认 locale——进程不调 setlocale；确定性来源
/// 登记于 Bounds.hpp 文件头注；缓冲区 64 字节对 double 十七位有效数字
/// 充裕）。
std::string formatG17(double v)
{
    char buf[64] = {0};
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

}  // namespace

// =====================================================================
// computeAnalyticReachBound——逐段平移贡献累加（类注释列纲）
// =====================================================================

Expected<AnalyticReachBound> computeAnalyticReachBound(const IKinRuntimeView& view,
                                                       const TcpRef& tcp)
{
    // ---- 第 1 步：模型获取与 TCP 解析（§5.1 唯一来源——界限含 TCP 偏置
    // 贡献，TCP 必须可解析；错误面与 FkEvaluator 同规则）。----
    const runtime::CanonicalModel& model = view.model();
    const std::vector<runtime::CanonicalJoint>& joints = model.chain().joints;
    if (joints.empty()) {
        return Expected<AnalyticReachBound>::err(makeNoDevice());
    }
    const Expected<const runtime::CanonicalTool*> tool = resolveTcp(view, tcp);
    if (!tool.ok()) {
        return Expected<AnalyticReachBound>::err(tool.error());
    }

    // ---- 第 2 步：逐段平移贡献累加（三角形不等式上界——旋转不改模长，
    // |p_tcp(q)| ≤ Σ 逐段平移模长最大值，与关节角取值无关）。----
    AnalyticReachBound bound;
    bound.jointOriginNorms.reserve(joints.size());
    bound.prismaticStrokes.reserve(joints.size());

    for (const auto& j : joints) {
        // 2a. 关节 origin 平移模长（m——Fixed/可动关节皆有：T_acc ←
        // T_acc·origin·M(q) 复合链中的逐段平移）。
        const double ox = j.origin.P()[0];
        const double oy = j.origin.P()[1];
        const double oz = j.origin.P()[2];
        const double originNorm = std::sqrt(ox * ox + oy * oy + oz * oz);
        bound.jointOriginNorms.push_back(originNorm);

        // 2b. 移动关节行程贡献 max(|qmin|,|qmax|)（m——沿轴平移的模长
        // 在区间端点取最大；转动/连续关节运动为纯旋转、平移贡献 0）。
        if (j.type == runtime::JointType::Prismatic && j.bounds.has_value()) {
            const double lo = j.bounds->lower;
            const double hi = j.bounds->upper;
            bound.prismaticStrokes.push_back(std::max(std::fabs(lo), std::fabs(hi)));
        } else {
            bound.prismaticStrokes.push_back(0.0);
        }
    }

    // 2c. TCP 偏置平移模长（m——法兰→TCP 的 |p|）。
    const runtime::CanonicalTool& t = *tool.get();
    const double tx = t.tcpOffset.P()[0];
    const double ty = t.tcpOffset.P()[1];
    const double tz = t.tcpOffset.P()[2];
    bound.tcpOffsetNorm = std::sqrt(tx * tx + ty * ty + tz * tz);

    // 2d. 总上界（m）＝Σorigin＋Σprismatic＋tcp（逐项累加、定序固定——
    // 确定性；求和顺序即链序）。
    double total = bound.tcpOffsetNorm;
    for (const double r : bound.jointOriginNorms) {
        total += r;
    }
    for (const double s : bound.prismaticStrokes) {
        total += s;
    }
    bound.totalRadius = total;

    return Expected<AnalyticReachBound>::ok(std::move(bound));
}

// =====================================================================
// formatReachDerivation——推导输入确定文本（Bounds.hpp 布局说明）
// =====================================================================

std::string formatReachDerivation(const AnalyticReachBound& bound,
                                  double targetDistance)
{
    // 逐段拼接（定长段——无环境依赖；%.17g 定点见 formatG17 注）。
    std::string out;
    out.reserve(256);
    out += "‖p_target‖(m)=";
    out += formatG17(targetDistance);
    out += " > Σ(m)=";
    out += formatG17(bound.totalRadius);
    out += "；推导：r_i=[";
    for (std::size_t i = 0; i < bound.jointOriginNorms.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        out += formatG17(bound.jointOriginNorms[i]);
    }
    out += "]; s_i=[";
    for (std::size_t i = 0; i < bound.prismaticStrokes.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        out += formatG17(bound.prismaticStrokes[i]);
    }
    out += "]; t=";
    out += formatG17(bound.tcpOffsetNorm);
    out += "（三角形不等式上界——覆盖全部关节角取值；不含姿态可达性）";
    return out;
}

// =====================================================================
// makeAnalyticBoundMaterial——证明素材组装（仅素材不裁定）
// =====================================================================

AnalyticBoundMaterial makeAnalyticBoundMaterial(const AnalyticReachBound& bound,
                                                const rw::math::Transform3D<double>& targetInBase,
                                                const IkRequestIdentity& identity,
                                                const IkTargetRef& targetRef)
{
    // 目标位置模长（m——基座系 {B}；只消费平移，姿态不在界限前提内）。
    const double px = targetInBase.P()[0];
    const double py = targetInBase.P()[1];
    const double pz = targetInBase.P()[2];

    AnalyticBoundMaterial m;
    m.bound = bound;
    m.targetDistance = std::sqrt(px * px + py * py + pz * pz);

    // 证明素材候选（evidence §6.3 字段面；validateProof 五查的通过面——
    // ①category 三类之一；②AnalyticBound 条件字段 boundExpression 非空；
    // ③producer 键＋契约版本与注册值一致（常量唯一书写点——Ik.hpp，
    // descriptor 同源）；④snapshotId/sliceId 非空绑定（expectedSliceId
    // ——取自请求身份；零值照实写入，查证归 evidence）；⑤覆盖声明＝
    // 解析界限类的规范值 kCoverageClaimAllAlternatives）。
    evidence::DeterministicInfeasibilityProof& proof = m.proof;
    proof.category = evidence::ProofCategory::AnalyticBound;
    proof.claimToken = kReachBeyondLinkSumClaim;
    proof.subject = targetRef.pointOid;
    proof.boundExpression = formatReachDerivation(bound, m.targetDistance);
    proof.preconditions =
        "静态连杆长度和上界（三角形不等式，§5.4 结局 5）；仅约束基座系"
        "位置半径，不含姿态可达性；TCP 偏置计入上界（同快照解析）";
    proof.coverageClaim = std::string(evidence::kCoverageClaimAllAlternatives);
    proof.snapshotId = identity.snapshotId;
    proof.sliceId = identity.sliceId;
    proof.producer = kTaskPointIkEvaluationKey;
    proof.producerContractVersion = kIkSolverContractVersion;

    return m;
}

}  // namespace sdurws::ird::kinematics
