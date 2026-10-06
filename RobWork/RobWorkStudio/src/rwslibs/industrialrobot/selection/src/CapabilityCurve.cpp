/**
 * @file   CapabilityCurve.cpp
 * @brief  能力曲线分段线性插值实现（selection 单元）——LinearCurveEvaluator
 *         （SEL-02 冻结语义；默认禁止外推）。
 *
 * 设计依据：
 *   - units/selection.md §6.2（插值规则四条：闭区间内插值/默认禁止外推/
 *     边界点闭区间含端点/结果携带区间与版本与单位）、§14.3（接口契约——
 *     不抛异常的拒绝轨＋非有限查询 fail-fast）、§6.3（曲线构造入口已保证
 *     点集形态——本实现只做防御复核）
 *   - 需求 SEL-02（能力曲线采用分段线性插值，默认禁止外推）、NFR-COR-03
 *     （非有限拒绝）、NFR-COR-04（结果可定位——携带输入点/区间/版本/单位）
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 2
 *
 * 确定性：纯算术实现（无容差参与区间判定——卡 §6.2 注：容差口径归筛选
 * 层阈值比较，插值/拒绝是精确代数判定）；同输入恒同输出（NFR-COR-02）。
 */

#include <sdurws/ird/selection/Curve.hpp>

#include <sdurws/ird/selection/DiagCodes.hpp>  // SEL-CURVE-* 码值常量（唯一书写点）

#include <cmath>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::selection {

namespace {

/// 拒绝结果工厂（收拢 Rejected 态的字段填装——两处拒绝路径共用，防遗漏
/// 单位/版本携带；码值一律取 DiagCodes.hpp 常量，禁第二处字面量）。
CurveQueryResult makeRejected(const PerformanceCurve& curve, double x,
                              std::string code, std::string message)
{
    CurveQueryResult r;
    r.outcome = CurveQueryResult::Outcome::Rejected;
    r.value = 0.0;                      // 拒绝态无值——固定零填充（调用方以 ok() 判别）
    r.x = x;
    r.xLow = 0.0;
    r.xHigh = 0.0;
    r.catalog = curve.catalog;
    r.xUnit = curve.xUnit;
    r.yUnit = curve.yUnit;
    r.rejectCode = std::move(code);
    r.rejectMessage = std::move(message);
    return r;
}

}  // namespace

CurveQueryResult LinearCurveEvaluator::evaluate(const PerformanceCurve& curve,
                                                double x) const
{
    // 第零步：调用方契约自检——查询横坐标必须有限（NFR-COR-03）。
    // 非有限查询不属于"数据不足"（目录数据没有问题），而是查询方把坏值
    // 传进了计算链——按卡 §14.0 错误二分走 fail-fast，不静默转成拒绝轨
    // （静默会把上游计算链的污染伪装成目录数据缺口，破坏分轨语义）。
    if (!std::isfinite(x)) {
        throw std::invalid_argument(
            "SEL-CURVE: 查询横坐标非有限（NaN/±Inf 拒绝——NFR-COR-03；"
            "曲线 [" + curve.curveId + "]）");
    }

    // 第一步：曲线形态防御复核（构造入口 tryMakePerformanceCurve 应已拦；
    // 此处为防御分支——形态坏的曲线按"数据不足"拒绝轨返回，不抛异常，
    // 卡 §14.3 @return 注"空曲线/单点曲线按数据不足标记"）。
    // 注意防御检查先做全点有限复核：构造入口拦 NONFINITE，若运行期收到
    // 含非有限点的曲线（内部一致性破坏），按数据坏拒绝而非产出污染值。
    if (curve.points.size() < 2) {
        // 点数 < 2（含空/单点）＝区间不合法（单点能力值应走"固定额定值"
        // 口径——卡 §6.4），数据不足类拒绝。
        return makeRejected(curve, x, std::string{kSelCurveIntervalInvalid},
                            "曲线点数不足 2（空/单点曲线不可插值——卡 §6.4 固定额定值口径）");
    }
    for (const CapabilityPoint& p : curve.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return makeRejected(curve, x, std::string{kSelCurveNonfinite},
                                "曲线含非有限点（构造入口应已拒绝——防御复核命中）");
        }
    }

    const double xMin = curve.points.front().x;   // 有效区间下端（SI，单位 xUnit）
    const double xMax = curve.points.back().x;    // 有效区间上端（SI，单位 xUnit）

    // 第二步：外推判定——默认禁止外推（SEL-CURVE-EXTRAPOLATION-DENIED）。
    // 闭区间 [x_min, x_max] 之外一律拒绝：不自动使用最近点、不静默外推
    // （卡 §6.2 原文——外推会产出无目录依据的"能力值"，违反 NFR-COR-03
    // 不静默纪律）。拒绝消息携带有效区间与单位，供比较型诊断的 expected
    // 侧构造（ERR-01：实际输入点/有效区间/单位）。
    if (x < xMin || x > xMax) {
        CurveQueryResult r = makeRejected(
            curve, x, std::string{kSelCurveExtrapolationDenied},
            "查询点落在曲线有效区间 [x_min, x_max] 之外（默认禁止外推——卡 §6.2；"
            "有效区间按 SI 单位 " + curve.xUnit + " 计）");
        r.xLow = xMin;   // 有效区间随拒绝结果携带（比较型 expected 侧素材）
        r.xHigh = xMax;
        return r;
    }

    // 第三步：定位插值区间 [x_i, x_i+1]——线性扫描（点数为目录数据规模，
    // 通常 < 数十点；扫描即确定性序，无需二分的规模理由）。循环不变量：
    // xMin <= x <= xMax 已由第二步保证，故必有 i 使 points[i].x <= x。
    // 边界点（x == xMin / xMax）落入首/末段端点——闭区间含端点（卡 §6.2），
    // 由 t==0/t==1 的端点值自然承载。
    for (std::size_t i = 0; i + 1 < curve.points.size(); ++i) {
        const CapabilityPoint& p0 = curve.points[i];
        const CapabilityPoint& p1 = curve.points[i + 1];

        // 段定位：x 在本段闭区间 [p0.x, p1.x] 内才处理。因 x 已确认整体
        // 在 [xMin, xMax] 内且点集严格升序（构造入口保证），本条件必在
        // 某段命中；未命中即内部一致性破坏，循环走完后防御拒绝。
        if (x < p0.x || x > p1.x) { continue; }

        // 第四步：分段线性插值 y = y0 + t·(y1 − y0)，t ∈ [0,1] 为段内
        // 线性参数（无量纲）。分母 p1.x − p0.x 因点集严格升序恒 > 0，
        // 无除零风险（构造入口 DUP-X 已排除相等横坐标）。
        const double t = (x - p0.x) / (p1.x - p0.x);   // 段内位置参数，无量纲 [0,1]
        const double y = p0.y + t * (p1.y - p0.y);     // 插值结果（SI，单位 yUnit）

        CurveQueryResult r;
        r.outcome = CurveQueryResult::Outcome::Value;
        r.value = y;
        r.x = x;
        r.xLow = p0.x;   // 插值区间随结果携带（NFR-COR-04——淘汰原因可定位到段）
        r.xHigh = p1.x;
        r.catalog = curve.catalog;
        r.xUnit = curve.xUnit;
        r.yUnit = curve.yUnit;
        return r;
    }

    // 不可达防御：点集严格升序且 x∈[xMin,xMax] 时上方必命中段；
    // 走到此处＝曲线内部序被破坏（应被构造入口拦下）——按数据坏拒绝。
    return makeRejected(curve, x, std::string{kSelCurveUnordered},
                        "曲线点集序内部一致性破坏（应被构造入口拒绝——防御复核命中）");
}

}  // namespace sdurws::ird::selection
