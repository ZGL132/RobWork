#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTROLLER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTROLLER_HPP

#include "StructureOptimizationStrategy.hpp"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace rws {

/**
 * @brief 结构优化异步线程控制器类。
 * 
 * 继承自 Qt 的 QObject，负责管理后台优化算法的异步线程执行、跨线程状态控制 (暂停/恢复/取消) 
 * 以及与 UI 前端界面的信号驱动通信。
 * 
 * 设计哲学：
 * 1. 界面响应保证：将密集型计算放在后台 Worker 线程，彻底避免 Qt UI 主线程卡死；
 * 2. 跨线程状态同步：通过共享的 OptimizationControlState (含条件变量与原子标志) 实现线程安全的暂停和取消；
 * 3. 解耦与可测试性：通过 RunFunction 函数对象支持算法策略注入，便于单元测试或算法替换。
 */
class StructureOptimizationController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 优化算法执行函数签名包装定义。
     * 接收优化问题和回调接口组合，返回包含评估数据与诊断报告的优化结果对象。
     */
    using RunFunction = std::function<StructureOptimizationResult(
        const StructureOptimizationProblem&,
        const StructureOptimizationCallbacks&)>;

    /**
     * @brief 默认构造函数（使用默认的 HybridStructureOptimizer 混合优化算法）。
     * @param parent 可选的 Qt 父对象指针，用于 QObject 内存管理树
     */
    explicit StructureOptimizationController(QObject* parent = nullptr);

    /**
     * @brief 显式注入自定义算法执行函数的构造函数（主要用于单元测试或自定义算子注入）。
     * @param runFunction 自定义的算法执行入口函数
     * @param parent 可选的 Qt 父对象指针
     */
    explicit StructureOptimizationController(RunFunction runFunction,
                                              QObject* parent = nullptr);

    /**
     * @brief 析构函数。
     * 内部会自动安全触发 cancel() 并等待后台线程退出，防止悬挂线程。
     */
    ~StructureOptimizationController() override;

    /**
     * @brief 启动后台异步优化计算。
     * 
     * 内部流程：
     *  1. 检查当前是否已经在运行 (若正在运行则直接返回 false)；
     *  2. 初始化 OptimizationControlState 内部控制状态；
     *  3. 将 _runFunction 投递至 QtConcurrent 线程池中异步运行；
     *  4. 绑定 _watcher 监听后台线程的完成状态；
     *  5. 发射 runningChanged(true) 信号并返回 true。
     * 
     * @param problem 结构优化问题定义对象
     * @return true  成功启动后台异步线程；
     * @return false 当前已处于运行状态，拒绝重复启动
     */
    bool start(const StructureOptimizationProblem& problem);
    bool startBaselineEvaluation(const StructureOptimizationProblem& problem);

    /**
     * @brief 请求暂停当前的后台优化计算。
     * 内部会将控制状态中的 paused 标志置为 true，后台算法在下一次评估前触发 waitIfPaused() 挂起。
     */
    void pause();

    /**
     * @brief 请求恢复已暂停的后台优化计算。
     * 内部将 paused 标志置为 false，并触发条件变量唤醒挂起的后台 Worker 线程继续执行。
     */
    void resume();

    /**
     * @brief 请求取消当前的后台优化计算。
     * 内部将 cancelRequested 标志置为 true，并唤醒可能正在挂起的线程，后台算法在下一次迭代时安全退出。
     */
    void cancel();

    /**
     * @brief 查询当前优化算法是否正在后台异步运行。
     * @return true 正在运行中；false 处于空闲状态
     */
    bool isRunning() const;

    /**
     * @brief 查询当前后台优化算法是否处于挂起暂停状态。
     * @return true 已暂停；false 未暂停
     */
    bool isPaused() const;
    bool isBaselineRunning() const;

Q_SIGNALS:
    /**
     * @brief 优化计算进度更新信号。
     * 当后台算法完成一个评估阶段或若干个候选解评估时发射，通知 UI 更新进度条及当前最高分。
     * @param progress 包含当前阶段名称、已完成数量、计划总数及最高得分的进度结构体
     */
    void progressChanged(const rws::StructureProgress& progress);

    /**
     * @brief 优化算法全盘计算成功完成信号。
     * @param result 包含所有候选解评估数据、最佳解索引及诊断报告的终态结果对象
     */
    void completed(const rws::StructureOptimizationResult& result);

    /**
     * @brief 优化过程发生严重异常或失败时发射的信号。
     * @param message 详细的错误描述字符串
     */
    void failed(const QString& message);

    /**
     * @brief 运行状态改变信号 (用于 UI 控制“开始/取消”按钮的使能与禁用切换)。
     * @param running 当前是否处于运行状态
     */
    void runningChanged(bool running);

    /**
     * @brief 暂停状态改变信号 (用于 UI 控制“暂停/恢复”按钮图标与文本切换)。
     * @param paused 当前是否处于暂停状态
     */
    void pausedChanged(bool paused);
    void baselineCompleted(const rws::StructureOptimizationResult& result);
    void baselineFailed(const QString& message);
    void baselineRunningChanged(bool running);

private:
    struct OptimizationControlState; //!< 内部使用的线程同步控制状态结构体 (包含互斥锁、条件变量及原子标志)

    /**
     * @brief 系统默认的优化入口：内部实例化 HybridStructureOptimizer 和 KinematicEngineeringEvaluator 执行计算。
     */
    static StructureOptimizationResult runDefaultOptimization(
        const StructureOptimizationProblem& problem,
        const StructureOptimizationCallbacks& callbacks);
    static StructureOptimizationResult runDefaultBaselineEvaluation(
        const StructureOptimizationProblem& problem,
        const StructureOptimizationCallbacks& callbacks);

    /**
     * @brief 后台计算结束时的清理与状态复位私有函数。
     */
    void finishCurrentRun();

    /**
     * @brief 设置 running 状态并安全发射 runningChanged 信号。
     */
    void setRunning(bool running);

    /**
     * @brief 设置 paused 状态并安全发射 pausedChanged 信号。
     */
    void setPaused(bool paused);
    void finishBaselineRun();
    void setBaselineRunning(bool running);

    QFutureWatcher<StructureOptimizationResult> _watcher;
    QFutureWatcher<StructureOptimizationResult> _baselineWatcher;
    RunFunction _runFunction;                             //!< 绑定的算法执行函数对象
    std::shared_ptr<OptimizationControlState> _control;  //!< 跨线程共享的同步控制状态对象智能指针
    bool _running = false;                                //!< 当前控制器运行状态标志
    bool _paused = false;
    std::shared_ptr<OptimizationControlState> _baselineControl;
    bool _baselineRunning = false;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTROLLER_HPP
