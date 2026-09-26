/**
 * @file   Recompute.cpp
 * @brief  批量复算请求构造实现（KIN-08/§7.4）——字段携带规则与调用方
 *         错误 fail-fast 轨（契约面见 Recompute.hpp 文件头注）。
 *
 * 设计依据：units/kinematics.md §7.4；REQUIREMENTS KIN-08/CON-02/CON-05；
 * 任务契约 tasks/foundation/WP-15-T09.json acceptance 3（登记随卡
 * §14.6 v0.9）。
 */

#include <sdurws/ird/kinematics/Recompute.hpp>

#include <stdexcept>
#include <utility>

namespace sdurws::ird::kinematics {

KinRecomputeRequest buildKinRecomputeRequest(const IkRequestIdentity& original,
                                             std::string evaluationKey,
                                             const core::ContentIdentity& currentSnapshotId,
                                             std::optional<core::ObjectId> subjectOid)
{
    // ---- 调用方错误 fail-fast（§9.1——调用方契约违约走异常，不进结果
    // 对象、不做静默缺省替换，NFR-COR-03 同轨）----
    // 空评估键：execution 接纳面无法路由到评估器——"宁错带病提交"没有
    // 合法形态；全零快照锚：ContentIdentity 的保留值（core 契约"全零＝
    // 空"），新 run 的切片重推导无从锚定——同样拒绝。
    if (evaluationKey.empty()) {
        throw std::invalid_argument(
            "Recompute：评估键为空（execution 接纳面无法路由——调用方"
            "错误，§7.4 复算请求构造纪律）");
    }
    if (!currentSnapshotId.isValid()) {
        throw std::invalid_argument(
            "Recompute：当前快照绑定为全零保留值（切片重推导无锚——调用方"
            "错误，§7.4 复算请求构造纪律）");
    }

    // ---- 字段携带（§7.4 三要件的构造面——文件头注逐条兑现）----
    // 1) "按原 slice"：sliceId/mode/seed/referenceQ 逐字段取自原结果身份
    //    （复算同一评估定义；referenceQ 显式携带——D-KIN-4，签名里没有
    //    会话形参，会话残留结构性不可入）。
    // 2) "execution 新 run"：只携带任务定义面——旧 run 的 runId/attempt/
    //    结果载荷在类型面上不存在（结构性保证）；新 run 标识归 execution
    //    接纳时分配。
    // 3) "结果当前性重新判定"：snapshotId 取参数直传的当前绑定，本值无
    //    当前性字段——判定归 evidence（CON-02 正交）。
    KinRecomputeRequest request;
    request.sliceId = original.sliceId;
    request.snapshotId = currentSnapshotId;
    request.evaluationKey = std::move(evaluationKey);
    request.subjectOid = std::move(subjectOid);
    request.mode = original.mode;
    request.seed = original.seed;
    request.referenceQ = original.referenceQ;
    return request;
}

}  // namespace sdurws::ird::kinematics
