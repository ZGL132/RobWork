/**
 * @file   Sampling.cpp
 * @brief  ISamplingPlanBuilder 的产品实现——计划定义构建（规范化计数）、
 *         计划摘要（分母面＋canonical 字节 SHA-256）。
 *
 * 设计依据：units/requirements.md §5.2（采样计划定义/规范化/冻结语义图）、
 * §9.5（接口契约）、§4.1（req-plan-set 条目）；任务契约
 * tasks/foundation/WP-14-T03.json acceptance 3。
 *
 * 线程安全：无共享可变状态（builder 与复用服务均为无状态），可重入。
 * 确定性：floor 规范化与 SHA-256 摘要为纯确定运算（NFR-COR-01/02）。
 */

#include <sdurws/ird/requirements/Sampling.hpp>

#include <sdurws/ird/core/Digest.hpp>  // ContentDigester——SHA-256 唯一实现（core §4.2）
#include <sdurws/ird/requirements/Codec.hpp>  // RequirementCodec——计划 canonical 字节（摘要输入）

#include <cmath>
#include <string>
#include <vector>

namespace sdurws::ird::requirements {

namespace {

/// 诊断预留位约束断言（同 Services.cpp——本单元无已登记诊断码，恒不写）。
void assertDiagsUntouched(const std::vector<core::DiagnosticRecord>& diags)
{
    (void)diags;
}

}  // namespace

PlanOutcome SamplingPlanBuilder::buildPlan(const WorkRegion& region,
                                           const SamplingPlanSpec& spec,
                                           std::vector<core::DiagnosticRecord>& diags) const
{
    assertDiagsUntouched(diags);
    PlanOutcome out;
    // ①区域盒非退化（I-REQ-6——DegenerateRegion；m，三分量>0 有限）。
    if (auto e = validateBoundingBox(region.box)) {
        out.error = std::move(*e);
        return out;
    }
    // ②区域形态核对（R1 仅 Box——§5.2"其他几何走需求变更，P-REQ-7 不
    // 预留桩"；WorkRegion 值模型即 Box 形态——恒过；该分支为未来扩展
    // 的拒绝面锚点，@错误 行 RegionNotBox 的承载点）。
    // 注：WorkRegion 不携带几何种类字段（Box 是唯一登记形态），本核对
    // 以"值模型形态"表达——一旦需求变更引入其他几何种类，此处即以
    // RegionNotBox 拒绝（卡 §14.3 P-REQ-7 处置路径）。
    // ③采样定义合法性（间距正有限——IllegalTolerance；姿态计数 ≥1——
    // NegativeCount；由规范化入口与条目校验共同承载）。
    // ④规范化（D-REQ-2——WorkRegionService::normalizeSampling 复用，
    // 语义单源）。
    const WorkRegionService regionService;
    auto norm = regionService.normalizeSampling(spec.positionSampling, region.box.size, diags);
    if (!norm.ok()) {
        out.error = norm.error();
        return out;
    }
    // 姿态计数 ≥1（§5.2——零样本仅位置侧表达；NegativeCount 承载）。
    if (spec.orientationSampling.directionSamples < 1
        || spec.orientationSampling.rollSamples < 1) {
        RequirementError e;
        e.code = RequirementErrorCode::NegativeCount;
        e.params.emplace_back("field", "orientationSampling");
        e.detail = "requirements/sampling: 姿态采样计数须 ≥1（§5.2——零样本仅"
                   "位置侧 counts 乘积=0 表达，V-02）";
        out.error = std::move(e);
        return out;
    }
    // 装配计划条目（位置采样恒规范化 Grid 形态——validateSamplingPlan
    // 的字节面前提；objectId＝spec 注入或临时句柄）。
    out.ok = true;
    out.plan.objectId = spec.planObjectId.has_value() ? *spec.planObjectId
                                                      : core::ObjectId::generate();
    out.plan.regionRef = region.objectId;
    out.plan.positionSampling = norm.get().normalized;
    out.plan.orientationSampling = spec.orientationSampling;
    out.plan.note = spec.note;
    return out;
}

Expected<PlanDigest> SamplingPlanBuilder::digest(const SamplingPlan& plan,
                                                 std::vector<core::DiagnosticRecord>& diags) const
{
    assertDiagsUntouched(diags);
    // 计划形态复核（规范化 Grid＋姿态 ≥1＋区域引用有效——条目级校验单
    // 源复用；违约即非法形态）。
    if (auto e = validateSamplingPlan(plan)) {
        return Expected<PlanDigest>::err(std::move(*e));
    }
    PlanDigest d;
    // 分母面（KIN-04 判定的分母来源——evidence SamplingPlanRef 核对）：
    // 位置＝counts 乘积（0 合法——V-02 零样本由评估判定）；姿态＝方向×
    // 滚转（≥1）。uint64 乘积（uint32 三因子最大约 2^96——超出即饱和到
    // UINT64_MAX 并如实承载；黄金数据集内不触）。
    const std::uint64_t cx = plan.positionSampling.counts[0];
    const std::uint64_t cy = plan.positionSampling.counts[1];
    const std::uint64_t cz = plan.positionSampling.counts[2];
    const std::uint64_t maxU64 = 0xFFFFFFFFFFFFFFFFull;
    auto satMul = [maxU64](std::uint64_t a, std::uint64_t b) {
        if (a != 0 && b > maxU64 / a) { return maxU64; }
        return a * b;
    };
    d.positionSamples = satMul(satMul(cx, cy), cz);
    d.orientationSamples = static_cast<std::uint64_t>(plan.orientationSampling.directionSamples)
                         * static_cast<std::uint64_t>(plan.orientationSampling.rollSamples);
    // canonical 摘要：计划条目编码字节（RequirementCodec——确定性字节）
    // 的 SHA-256（同计划同摘要；计划变更即换摘要——§5.2 计划变更与当前
    // 性行的本单元承载点）。
    const RequirementCodec codec;
    auto bytes = codec.encode(RequirementObjectVariant{PlanSet{
                                  kReqPlanSetSchemaVersion, {plan}}},
                              kCurrentRequirementFormatVersion);
    if (!bytes.ok()) {
        return Expected<PlanDigest>::err(bytes.error());
    }
    core::ContentDigester digester;
    digester.update(bytes.get().data(), bytes.get().size());
    d.planDigest = digester.finalize();
    return Expected<PlanDigest>::ok(std::move(d));
}

}  // namespace sdurws::ird::requirements
