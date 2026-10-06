/**
 * @file   Curve.hpp
 * @brief  能力曲线分段线性插值契约（selection 单元）——CurveQueryResult
 *         与 IPerformanceCurveEvaluator（卡 §14.3 设计基线签名）＋v1
 *         线性插值器实现。
 *
 * 设计依据：
 *   - units/selection.md §6.2（插值规则——SEL-02 冻结语义：横坐标升序
 *     排序后分段线性插值；查询点落在 [x_min, x_max] 闭区间内才允许；
 *     默认禁止外推——不自动使用最近点、不静默外推；边界点＝闭区间含
 *     端点；插值结果携带实际输入点/插值区间/曲线版本/单位——NFR-COR-04；
 *     插值失败≠候选能力不足——数据不足类分轨，§10.3）、§14.3（接口签名）
 *   - 需求 SEL-02（能力曲线采用分段线性插值，默认禁止外推）、NFR-COR-01
 *     （解析算例对照）、NFR-COR-04（结果可定位）
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 2（分段线性
 *     插值、默认禁止外推 SEL-CURVE-EXTRAPOLATION-DENIED 用例通过）
 *
 * 线程安全：LinearCurveEvaluator 无状态（可重入——卡 §14.10"筛选/曲线/
 * 组合构造（纯函数）可重入"）。
 */

#ifndef IRD_SELECTION_CURVE_HPP
#define IRD_SELECTION_CURVE_HPP

#include <string>

#include <sdurws/ird/selection/CatalogTypes.hpp>

namespace sdurws::ird::selection {

// 稳定码唯一书写点引用（DiagCodes.hpp 常量——禁字符串拼码/第二处字面量）。
/// 曲线查询拒绝码家族的判别经 outcome+code 承载；EXTRAPOLATION-DENIED 码
/// 值常量＝kSelCurveExtrapolationDenied（selection/DiagCodes.hpp）。

/**
 * @brief 曲线查询结果（卡 §6.2"插值结果携带"四要素＋拒绝轨）。
 *
 * 双轨语义（卡 §6.2"两者分轨"）：
 *   - ok==true：插值成功——value 为 SI 域插值 y；携带实际输入点 x、插值
 *     区间 [xLow, xHigh]（边界查询时 xLow==xHigh==端点）、曲线版本与单位
 *     （供淘汰原因与追溯——NFR-COR-04）；
 *   - ok==false：查询被拒绝——区间外（EXTRAPOLATION-DENIED）或曲线数据
 *     不足（空/坏曲线）→ **数据不足类标记**，不是候选能力不足（候选能力
 *     不足＝插值成功但值不达标，走 §10.3 能力不足类淘汰原因；两条轨在
 *     筛选层〔WP-19-T04〕分轨消费）。
 *
 * 不抛异常（卡 §14.3 @return 注——"区间外/空曲线/单点曲线按数据不足标记，
 * 不抛异常"）；查询横坐标非有限属调用方契约违约（NFR-COR-03），由
 * evaluate 以 fail-fast 异常拒绝——见其 @throws。
 */
struct CurveQueryResult {
    /// 查询结果类别（值轨/拒绝轨——卡 §6.2 分轨语义）。
    enum class Outcome {
        Value,     ///< 插值成功（value 有效）
        Rejected,  ///< 查询拒绝（rejectCode 携带 SEL-* 码；数据不足类）
    };

    Outcome outcome = Outcome::Rejected; ///< 结果类别（默认拒绝——显式判别纪律）
    double value = 0.0;                  ///< 插值结果 y（SI 域；仅 Value 态有效）
    double x = 0.0;                      ///< 实际输入点（SI 域；两态均回填——追溯面）
    double xLow = 0.0;                   ///< 插值区间下端 x_i（SI 域；仅 Value 态有效；
                                         ///<   端点查询时 == xHigh == 端点）
    double xHigh = 0.0;                  ///< 插值区间上端 x_i+1（SI 域；仅 Value 态有效）
    CatalogIdentity catalog;             ///< 曲线版本（所属目录版本——卡 §6.2 携带项）
    std::string xUnit;                   ///< 横坐标 SI 单位 token（比较单位——ERR-01）
    std::string yUnit;                   ///< 纵坐标 SI 单位 token
    std::string rejectCode;              ///< 拒绝码（仅 Rejected 态非空；SEL-* 常量值）
    std::string rejectMessage;           ///< 拒绝语义（中文上下文素材——含有效区间，
                                         ///<   供比较型诊断的 expected 侧构造）

    /// 值轨判别便利（调用方显式分轨的最低形态）。
    bool ok() const noexcept { return outcome == Outcome::Value; }
};

/**
 * @brief 能力曲线插值接口（卡 §14.3 设计基线——签名逐注承载）。
 */
class IPerformanceCurveEvaluator {
public:
    virtual ~IPerformanceCurveEvaluator() = default;

    /**
     * @brief 分段线性插值（卡 §6.2 冻结语义）。
     *
     * @param curve [in] 曲线（构造入口 tryMakePerformanceCurve 已校验/
     *              排序——x 严格升序、点数 >= 2、全有限）
     * @param x     [in] 查询横坐标（SI 单位；须与曲线 xUnit 同单位域）
     * @return 插值结果或拒绝（CurveQueryResult 双轨——不抛异常）
     *
     * @throws std::invalid_argument 查询横坐标 x 非有限（NaN/±Inf——调用方
     *         契约违约 fail-fast；NFR-COR-03。曲线自身数据坏〔构造入口应已
     *         拦〕在此按数据不足拒绝轨返回，不抛——防御分支，卡 §14.3 注）。
     *
     * @note 纯函数；ConcurrentReadOnly；比较经 core 统一规则（卡 §6.2 注
     *       ——附录 D 容差不在插值判定内使用：插值/拒绝的区间判定是精确
     *       代数问题，容差口径归筛选层阈值比较〔WP-19-T04〕）。
     */
    virtual CurveQueryResult evaluate(const PerformanceCurve& curve, double x) const = 0;
};

/**
 * @brief 分段线性插值器（IPerformanceCurveEvaluator 的唯一产品实现——
 *        SEL-02 冻结语义；默认禁止外推，无"允许外推"开关——需求语义
 *        不由参数开关化，卡 §6.2"默认禁止"即唯一行为）。
 */
class LinearCurveEvaluator final : public IPerformanceCurveEvaluator {
public:
    CurveQueryResult evaluate(const PerformanceCurve& curve, double x) const override;
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_CURVE_HPP
