/**
 * @file   PropertyEstimation.cpp
 * @brief  物性估算唯一公式表实现——段元闭式公式、平行轴合成、输出自检、
 *         材料密度默认表与质心修改确认流（PropertyEstimation.hpp 契约面）。
 *
 * 设计依据：units/modeling.md §5.3（唯一公式表 mdl-property-formula/1）、
 * §9.4.6（IPropertyEstimator 契约）、§14.4 第 3/5 项；需求 MDL-05/16、
 * DYN-06；任务契约 tasks/foundation/WP-13-T04.json acceptance 1~4。
 *
 * 确定性纪律（NFR-COR-02）：本文件全部浮点运算按固定次序执行（段元按
 * 输入序遍历、矩阵元素按固定行列序累加），无归约重排/无并行/无迭代
 * 收敛判据——同输入逐字节同输出。
 */

#include <sdurws/ird/modeling/PropertyEstimation.hpp>

#include "../src/InertiaMath.hpp"  // 单元私有头（R-2：不入 include/）——合成自检单一实现（卡 §9.4.6 @post）

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace sdurws::ird::modeling {

// =====================================================================
// 稳定 token 表（switch 全枚举、无 default——漏登记编译器告警暴露；
// Errors.cpp 同款防线）
// =====================================================================

std::string_view estimateErrorCodeToken(EstimateErrorCode code) noexcept
{
    switch (code) {
    case EstimateErrorCode::IllegalDimension:       return "IllegalDimension";
    case EstimateErrorCode::MaterialDensityMissing: return "MaterialDensityMissing";
    case EstimateErrorCode::SynthesisFailed:        return "SynthesisFailed";
    }
    // 全枚举已覆盖，此处不可达（返回空串仅为满足无 default 的编译器路径）。
    return "";
}

std::string_view centroidEditResolutionToken(CentroidEditResolution resolution) noexcept
{
    switch (resolution) {
    case CentroidEditResolution::MigrateInertia:  return "migrate-inertia";
    case CentroidEditResolution::OverwriteInertia: return "overwrite-inertia";
    }
    return "";
}

// =====================================================================
// 材料密度默认表（§5.3 输入节——设计默认值；黄金数据集锁定随 WP-13-T16）
// =====================================================================

std::optional<double> defaultMaterialDensity(std::string_view materialId)
{
    // 固定表线性扫描（5 项常数开销）：键＝卡面材料名的稳定英文 token；
    // 值＝kg/m³（表行序遍历——确定性序，NFR-COR-02）。表是设计默认值
    // 而非上游冻结需求值（§14.4 第 3 项/D-MDL-7）：黄金数据集锁定若调整
    // 数值，只改本表并同步单元卡，键词表不变。
    static constexpr std::array<std::pair<std::string_view, double>, 5> kTable{{
        {"steel",               7850.0},  // 钢
        {"aluminum",            2700.0},  // 铝
        {"cast-iron",           7200.0},  // 铸铁
        {"titanium-alloy",      4430.0},  // 钛合金
        {"engineering-plastic", 1200.0},  // 工程塑料
    }};
    for (const auto& [key, density] : kTable) {
        if (materialId == key) {
            return density;
        }
    }
    // 词表外（含空串/大小写变体）＝未命中——不猜测（ARC-04"不猜测"纪律）。
    return std::nullopt;
}

// =====================================================================
// 合成数学辅助（文件内局部——只服务 estimateLink，不跨单元暴露）
// =====================================================================

namespace {

/// 合成累加缓冲：全 3×3 矩阵（kg·m²）——提取六分量前须过自检
/// （inertiamath::synthesisSelfCheck，见 InertiaMath.hpp 函数注）。
using Matrix3 = std::array<std::array<double, 3>, 3>;

/// 把"段质心系主轴惯量旋转到连杆系姿态"累加进合成缓冲：
/// M += R·diag(ixx,iyy,izz)·Rᵀ。D 为对角阵时的闭式展开
/// (R·D·Rᵀ)(i,j) = Σ_k R(i,k)·d_k·R(j,k)——比通用三矩阵乘少一次遍历，
/// 且浮点运算次序固定（确定性）。
void accumulateRotatedInertia(Matrix3& m, const rw::math::Rotation3D<double>& r,
                              double ixx, double iyy, double izz)
{
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            m[row][col] += r(row, 0) * ixx * r(col, 0)
                         + r(row, 1) * iyy * r(col, 1)
                         + r(row, 2) * izz * r(col, 2);
        }
    }
}

/// 把平行轴项 m((d·d)E − d·dᵀ) 累加进合成缓冲（d＝段质心相对连杆合成
/// 质心的位移，单位 m）：对角元 +m(d·d−d_i²)、非对角元 −m·d_i·d_j。
void accumulateParallelAxis(Matrix3& m, double massKg,
                            const rw::math::Vector3D<double>& d)
{
    const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];  // d·d（m²）
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            // (d·d)E 的对角元减去外积项——标准平行轴定理展开。
            m[row][col] += massKg * ((row == col ? dd : 0.0) - d[row] * d[col]);
        }
    }
}

}  // namespace

// =====================================================================
// PropertyEstimator——估算主流程（四步，逐步注释见各段）
// =====================================================================

EstimateOutcome PropertyEstimator::estimateLink(
    const std::vector<SegmentSpec>& segments,
    std::vector<core::DiagnosticRecord>& diags) const
{
    // 纪律锚（§9.4.6 diags 参数契约）：当前 §9.5 无 T04 行码（分批注册、
    // 不预建），估算失败一律经返回值面（EstimateError.detail 携段序定位）
    // ——本函数不追加任何诊断记录；物性缺失的 Warning 登记与
    // DataInsufficient 降级预告的登记面＝T08 就绪校验（卡 §5.3 规则 3）。
    // 参数按卡面签名保留，供后续任务行注册码后表尾接入。
    (void)diags;

    // ---- @pre：段元列表非空（空连杆没有物性估算意义——§9.4.6 @pre 原文）。
    // 前置违约走值面（IllegalDimension）而非异常：本接口契约即两态结果，
    // 调用方按 err() 分支处理（§9.4.6 @错误 行把 @pre 尺寸违约也归
    // IllegalDimension——值面语义）。
    if (segments.empty()) {
        return EstimateOutcome::err(EstimateError{
            EstimateErrorCode::IllegalDimension, "segments 为空（@pre 违约）"});
    }

    // ---- 步①②：逐段尺寸/密度解析与段级闭式估算（公式表逐行落位）。
    // 段质量（kg）与段质心在连杆系位置（m）随段序收集；段惯量在步③
    // 内联旋转累加（不存中间矩阵——省一次遍历，运算次序仍固定）。
    double totalMass = 0.0;                              // Σmᵢ（kg）
    double weightedCx = 0.0, weightedCy = 0.0, weightedCz = 0.0;  // Σmᵢcᵢ（kg·m）
    struct SegmentInertia {                              // 段质心系主轴惯量（kg·m²）
        double ixx = 0.0, iyy = 0.0, izz = 0.0;
        rw::math::Rotation3D<double> rotation;           // Rᵢ＝T_link_seg 的旋转半部
        rw::math::Vector3D<double> comInLink;            // cᵢ（m，连杆系）
        double massKg = 0.0;                             // mᵢ（kg）
    };
    std::vector<SegmentInertia> parts;
    parts.reserve(segments.size());

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const SegmentSpec& segment = segments[i];

        // ---- 步①：图元尺寸解析与 @pre 校验（>0 且有限；空心须壁厚为正）。
        // 校验失败即整次估算失败（部分结果不输出——NFR-COR-03 不静默跳段）。
        double ixx = 0.0, iyy = 0.0, izz = 0.0;  // 段质心系主轴惯量（kg·m²，公式表填入）
        double segmentMass = 0.0;                 // mᵢ（kg，公式表填入）
        const std::string where = "segments[" + std::to_string(i) + "]";  // 失败定位前缀

        // 密度解析序（§9.4.6 @pre"材料表查询失败→密度 NotProvided 报告"）：
        // ①MaterialRef.density 已提供→用已提供值（允许用户/导入侧显式覆盖
        //   默认表——须 >0 且有限，I-MDL-3 同口径）；
        // ②否则查默认表 defaultMaterialDensity；
        // ③两路皆不命中→MaterialDensityMissing 失败（报告即此失败值面；
        //   不猜测第二材料、不回退密度 1——NFR-COR-03）。
        double density = 0.0;  // ρ（kg/m³）
        if (const auto provided = segment.material.density.tryValue()) {
            if (!std::isfinite(*provided) || *provided <= 0.0) {
                return EstimateOutcome::err(EstimateError{
                    EstimateErrorCode::IllegalDimension,
                    where + ".material.density 须 >0 且有限（kg/m³）"});
            }
            density = *provided;
        } else if (const auto fromTable =
                       defaultMaterialDensity(segment.material.materialId)) {
            density = *fromTable;
        } else {
            return EstimateOutcome::err(EstimateError{
                EstimateErrorCode::MaterialDensityMissing,
                where + " 材料密度不可解析（density 未提供且默认表未命中键 \""
                    + segment.material.materialId + "\"——DataInsufficient 降级预告归 T08）"});
        }

        if (const auto* solid = std::get_if<SolidCylinderSpec>(&segment.primitive)) {
            // 公式表第 1 行（实心圆柱 r,L）：m=ρπr²L；Ixx=Iyy=m(3r²+L²)/12；Izz=mr²/2。
            if (!std::isfinite(solid->radius) || solid->radius <= 0.0
                || !std::isfinite(solid->length) || solid->length <= 0.0) {
                return EstimateOutcome::err(EstimateError{
                    EstimateErrorCode::IllegalDimension,
                    where + ".solid-cylinder 半径/长度须 >0 且有限（m）"});
            }
            segmentMass = density * 3.14159265358979323846 * solid->radius * solid->radius
                        * solid->length;                       // ρπr²L
            ixx = segmentMass * (3.0 * solid->radius * solid->radius + solid->length * solid->length) / 12.0;
            iyy = ixx;      // 轴对称：Ixx=Iyy（公式表合并列头的含义）
            izz = segmentMass * solid->radius * solid->radius / 2.0;  // mr²/2
        } else if (const auto* hollow = std::get_if<HollowCylinderSpec>(&segment.primitive)) {
            // 公式表第 2 行（空心圆柱 rOut,rIn,L）：m=ρπ(rOut²−rIn²)L；
            // Ixx=Iyy=m(3(rOut²+rIn²)+L²)/12；Izz=m(rOut²+rIn²)/2。
            // 壁厚为正（rOut>rIn>0）是质量为正的数学前提；rOut≤rIn 属调用方错误。
            if (!std::isfinite(hollow->radiusOuter) || !std::isfinite(hollow->radiusInner)
                || !std::isfinite(hollow->length) || hollow->length <= 0.0
                || !(hollow->radiusOuter > hollow->radiusInner)
                || hollow->radiusInner <= 0.0) {
                return EstimateOutcome::err(EstimateError{
                    EstimateErrorCode::IllegalDimension,
                    where + ".hollow-cylinder 须 rOut>rIn>0 且长度 >0（m，全部有限）"});
            }
            const double rOut2 = hollow->radiusOuter * hollow->radiusOuter;
            const double rIn2 = hollow->radiusInner * hollow->radiusInner;
            segmentMass = density * 3.14159265358979323846 * (rOut2 - rIn2) * hollow->length;
            ixx = segmentMass * (3.0 * (rOut2 + rIn2) + hollow->length * hollow->length) / 12.0;
            iyy = ixx;      // 轴对称：Ixx=Iyy
            izz = segmentMass * (rOut2 + rIn2) / 2.0;
        } else if (const auto* box = std::get_if<BoxSpec>(&segment.primitive)) {
            // 公式表第 3 行（长方体 a,b,L；z 沿 L）：m=ρabL；Izz=m(a²+b²)/12；
            // Ixx=m(b²+L²)/12（表值）。★ Iyy 的落位说明：表列头"Ixx=Iyy"
            // 为轴对称两行（圆柱）承载的合并列——长方体 a≠b 时 Ixx≠Iyy 是
            // 解析事实（绕 x 的回转半径含 y 向边长 b 与 z 向边长 L；绕 y 含
            // a 与 L），按同式取 Iyy=m(a²+L²)/12。该澄清随单元卡 §15 增量
            // 登记（WP-13-T04——DTB §5.4 口径），公式表行数值本身不变。
            if (!std::isfinite(box->a) || box->a <= 0.0
                || !std::isfinite(box->b) || box->b <= 0.0
                || !std::isfinite(box->length) || box->length <= 0.0) {
                return EstimateOutcome::err(EstimateError{
                    EstimateErrorCode::IllegalDimension,
                    where + ".box 三边长须 >0 且有限（m）"});
            }
            segmentMass = density * box->a * box->b * box->length;    // ρabL
            ixx = segmentMass * (box->b * box->b + box->length * box->length) / 12.0;
            iyy = segmentMass * (box->a * box->a + box->length * box->length) / 12.0;
            izz = segmentMass * (box->a * box->a + box->b * box->b) / 12.0;
        } else {
            // variant 新增备择（表尾追加纪律允许扩表）未接入公式→显性失败
            // （不静默按零质量处理——NFR-COR-03；新段元落位时本分支被替换）。
            return EstimateOutcome::err(EstimateError{
                EstimateErrorCode::IllegalDimension,
                where + " 段元图元未接入唯一公式表（实现表尾未同步）"});
        }

        // ---- 步②尾：段级结果收集（段质心 cᵢ＝T_link_seg 平移部分——段系
        // 原点在段几何中心＝均质段质心，§5.3 输入节约定）。
        SegmentInertia part;
        part.massKg = segmentMass;
        part.ixx = ixx;
        part.iyy = iyy;
        part.izz = izz;
        part.rotation = segment.linkFromSegment.R();
        part.comInLink = segment.linkFromSegment.P();
        parts.push_back(std::move(part));

        // Σmᵢ 与 Σmᵢcᵢ 同步累加（输入序固定——合成质心的确定性）。
        totalMass += segmentMass;
        weightedCx += segmentMass * part.comInLink[0];
        weightedCy += segmentMass * part.comInLink[1];
        weightedCz += segmentMass * part.comInLink[2];
    }

    // ---- 步③：合成（§5.3 合成节公式）。
    // C = Σmᵢcᵢ/m_link（m_link>0——各段质量>0、段数≥1，分母无零风险；
    // 溢出情形由步④自检兜底）。
    const rw::math::Vector3D<double> centroid(weightedCx / totalMass,
                                              weightedCy / totalMass,
                                              weightedCz / totalMass);
    Matrix3 composite{};  // 零初始化后按段序累加（I_C=Σ[...]）
    for (const SegmentInertia& part : parts) {
        // Rᵢ·Iᵢ·Rᵢᵀ：段质心系主轴惯量转连杆系姿态（参考姿态＝连杆坐标系——M-2）。
        accumulateRotatedInertia(composite, part.rotation, part.ixx, part.iyy, part.izz);
        // dᵢ＝cᵢ−C（m）——平行轴项 mᵢ((dᵢ·dᵢ)E−dᵢdᵢᵀ)。
        const rw::math::Vector3D<double> delta = part.comInLink - centroid;
        accumulateParallelAxis(composite, part.massKg, delta);
    }

    // ---- 步④：输出自检（§9.4.6 @post——失败输出内部错误码而非非法张量）。
    // 张量三条件（对称 1×10⁻¹²/SPD/三角不等式）单一实现在私有头
    // inertiamath::synthesisSelfCheck（反例拒收语义见其函数注）；质量与
    // 质心同批核查有限性（合成张量合法而标量溢出同样是非法输出）。
    if (const std::string failure = inertiamath::synthesisSelfCheck(composite); !failure.empty()) {
        return EstimateOutcome::err(EstimateError{
            EstimateErrorCode::SynthesisFailed,
            "合成自检失败：" + failure + "（物理合法输入的合成恒 SPD——"
            "触发即内部实现错误，不输出非法张量）"});
    }
    if (!std::isfinite(totalMass) || totalMass <= 0.0) {
        return EstimateOutcome::err(EstimateError{
            EstimateErrorCode::SynthesisFailed,
            "合成质量非有限或非正（内部错误——不输出非法结果）"});
    }
    if (!std::isfinite(centroid[0]) || !std::isfinite(centroid[1])
        || !std::isfinite(centroid[2])) {
        return EstimateOutcome::err(EstimateError{
            EstimateErrorCode::SynthesisFailed,
            "合成质心非有限（内部错误——不输出非法结果）"});
    }

    // ---- 步⑤：组装成功载荷（六分量投影已在自检内完成容差内对称化；
    // 来源标记＝§5.3 规则 1——GeometricEstimate＋methodTag 公式表版本）。
    EstimatedLinkProperties properties;
    properties.massKg = totalMass;
    properties.centerOfMass = centroid;
    properties.inertia.ixx = composite[0][0];
    properties.inertia.iyy = composite[1][1];
    properties.inertia.izz = composite[2][2];
    properties.inertia.ixy = 0.5 * (composite[0][1] + composite[1][0]);
    properties.inertia.ixz = 0.5 * (composite[0][2] + composite[2][0]);
    properties.inertia.iyz = 0.5 * (composite[1][2] + composite[2][1]);
    properties.provenance = core::ValueProvenance::make(
        core::ProvenanceKind::GeometricEstimate,
        std::nullopt,                                  // 无 sourceObject——来源是公式表
        std::nullopt,                                  // P-1：无 sourceObject 则无 sourceVersion
        std::string(kPropertyFormulaVersion));         // methodTag＝mdl-property-formula/1
    return EstimateOutcome::ok(std::move(properties));
}

InertiaTensor PropertyEstimator::migrateByParallelAxis(
    const InertiaTensor& atCom, double massKg,
    const rw::math::Vector3D<double>& deltaM) const noexcept
{
    // §5.3 规则 2 原文式 I'=I+m((d·d)E−ddᵀ) 的分量展开（头注逐条对应）。
    // 位移 d 取自 deltaM（m，新参考点−原质心）；质量 kg。纯算术无失败路径
    // （noexcept 契约）——量纲/有限性前置由调用方（applyCentroidEdit/估算
    // 流程）把关。
    const double dx = deltaM[0], dy = deltaM[1], dz = deltaM[2];
    InertiaTensor out;
    out.ixx = atCom.ixx + massKg * (dy * dy + dz * dz);
    out.iyy = atCom.iyy + massKg * (dx * dx + dz * dz);
    out.izz = atCom.izz + massKg * (dx * dx + dy * dy);
    out.ixy = atCom.ixy - massKg * dx * dy;
    out.ixz = atCom.ixz - massKg * dx * dz;
    out.iyz = atCom.iyz - massKg * dy * dz;
    return out;
}

std::string_view PropertyEstimator::formulaVersion() const noexcept
{
    return kPropertyFormulaVersion;  // 与来源标记 methodTag 同一常量（单一来源）
}

// =====================================================================
// 质心修改确认流（§5.3 规则 2；V-17 三分支——MDL-05 硬性交互约束）
// =====================================================================

std::optional<ModelingError> applyCentroidEdit(
    BodyData& body,
    const rw::math::Vector3D<double>& newCenterOfMass,
    std::optional<CentroidEditResolution> resolution,
    const std::optional<InertiaTensor>& replacementTensor)
{
    // ---- 调用方契约前置（fail-fast 轨——AGENTS 错误语义：调用方错误走
    // 异常，正常业务拒绝才走值面）：迁移/覆盖的语义前提是"已估算连杆"
    // （V-17 前置）——质量/质心/惯量三者任一缺失说明调用方把确认流用在
    // 了没有物性基准的连杆上（此时应先走估算或直接以 UserProvided 提供
    // 全套物性，而不是部分确认）。
    if (body.mass.state() != core::FieldState::Provided
        || body.centerOfMass.state() != core::FieldState::Provided
        || body.inertia.state() != core::FieldState::Provided) {
        throw std::invalid_argument(
            "applyCentroidEdit 前置违约：质心确认流要求 mass/centerOfMass/"
            "inertia 均为 Provided（V-17 前置\"已估算连杆\"）——缺失基准时"
            "应先估算或整体提供物性");
    }
    // 分支 (b) 的契约前提：覆盖必须有新张量输入（"强制覆盖完整张量"
    // ——InertiaTensor 六分量即完整形态，缺 optional 值＝调用方未给输入）。
    if (resolution == CentroidEditResolution::OverwriteInertia && !replacementTensor.has_value()) {
        throw std::invalid_argument(
            "applyCentroidEdit 前置违约：OverwriteInertia 分支必须提供完整"
            "六分量 replacementTensor");
    }

    // ---- 分支 (c)：既不迁移也不覆盖→调用方错误拒绝（值面 ModelingError
    // ——CentroidEditUnresolved，Errors.hpp §9.4.1 族已登记值）。body 不动
    // （先判后改的序保证了拒绝路径零副作用——质心与惯量基准不脱钩）。
    if (!resolution.has_value()) {
        ModelingError error;
        error.code = ModelingErrorCode::CentroidEditUnresolved;
        error.params = {
            {"field", "body.centerOfMass"},
            {"resolution", "none"},  // 用户既未选迁移也未选覆盖（V-17 分支 c）
        };
        error.detail =
            "质心修改未决议：须显式二选一（migrate-inertia 平行轴迁移/"
            "overwrite-inertia 覆盖完整张量）——不允许质心与惯量基准静默脱钩"
            "（MDL-05/M-2，卡 §5.3 规则 2）";
        return error;
    }

    // ---- 接受分支：先算全部新值、后一次性提交（强保证——中途无失败路径）。
    const rw::math::Vector3D<double> oldCom = body.centerOfMass.value();
    const rw::math::Vector3D<double> delta = newCenterOfMass - oldCom;  // d（m）

    // 新 com：用户直接修改→UserProvided 覆盖（§5.3 规则 1——估算值可被
    // 权威值覆盖，来源标记如实切换不伪装）。
    BodyData updated = body;
    updated.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        newCenterOfMass,
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));

    if (*resolution == CentroidEditResolution::MigrateInertia) {
        // 分支 (a)：惯量按平行轴迁移（d＝质心位移）。经估算器实例调用——
        // 平行轴公式的单一实现点（估算合成与确认流共用同一份算式，两处
        // 各写一份会漂移）。inertia 保留原 provenance：迁移是基准点移动，
        // 张量数值的来源事实不变（估算衍生物仍是估算衍生物——头注）。
        const PropertyEstimator estimator;
        const InertiaTensor migrated = estimator.migrateByParallelAxis(
            body.inertia.value(), body.mass.value(), delta);
        updated.inertia = core::SourcedValue<InertiaTensor>::provided(
            migrated, body.inertia.provenance());
    } else {
        // 分支 (b)：强制覆盖完整张量（用户输入六分量，质心基准/连杆系姿态
        // 不变——M-2 基准语义由本函数契约定死，调用方无需再声明）。来源
        // ＝UserProvided（用户输入的新张量）。
        updated.inertia = core::SourcedValue<InertiaTensor>::provided(
            *replacementTensor,
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    }

    body = std::move(updated);
    return std::nullopt;  // 接受——编辑差值/命令摘要的留痕由编辑器与命令层承载
}

}  // namespace sdurws::ird::modeling
