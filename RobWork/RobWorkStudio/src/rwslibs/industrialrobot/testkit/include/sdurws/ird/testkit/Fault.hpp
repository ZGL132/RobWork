/**
 * @file   Fault.hpp
 * @brief  故障注入原语——计划/命中观测（FaultPlan/FaultLog）＋拦截器适配模板。
 *
 * 设计依据：
 *   - units/testkit.md §6.4（进程内故障注入：接缝原则、结构体签名、故障点标识、
 *     文件系统故障覆盖、构建边界）、§8 TK-FAULT、§9 TK-T09 行
 *   - 需求 NFR-REL-01/02（事务回滚/崩溃恢复的测试载体）、PM-08
 *   - 任务契约 tasks/foundation/TK-T09.json（≙WP-02-T09）
 *
 * 接缝原则（§6.4 依赖方向安全——本头存在的意义）：生产代码**不包含** testkit 头、
 * 不加 #ifdef TEST；故障接缝＝生产代码本就要求的窄接口（端口/写入口/通道接口）。
 * fake 实现编译进测试目标，实现这些消费者自有接口；FaultInterceptor<I> 把"第 N 次
 * 命中触发"的计划接到该接口的方法流。
 *
 * 故障点标识约定：`<unit>/<接口>/<动作>`（如 io/file-writer/write-chunk）——
 * 接缝最小集与各单元义务登记于 testkit.md §10.2。
 *
 * 文件系统故障场景（打开失败/写中途失败/磁盘满/write 后 rename 失败）不在本头
 * 实现——它们是计划的**用法**：fake 的 write 方法按 shouldFire 的裁决抛出被测
 * 接口声明的错误类型（驱动 AT-13 保存事务类测试，见单元测试示例）。
 *
 * 线程安全：非线程安全（测试目标单线程使用——测试串行纪律，testkit.md §6.2 同源）。
 *
 * 构建边界（T-1 红线不变）：本头与 fake 只进 `_test`/`_contract_test` 目标；
 * 生产库无感知。
 */

#ifndef SDURWS_IRD_TESTKIT_FAULT_HPP
#define SDURWS_IRD_TESTKIT_FAULT_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/testkit/TestPaths.hpp>   // TestKitError（构造期 real 判空抛 Usage）

namespace sdurws::ird::testkit {

/// 单条触发规则：故障点 faultPointId 第 occurrence 次被命中时注入一次失败。
struct FaultTrigger {
    std::string faultPointId;                 ///< 故障点标识（<unit>/<接口>/<动作>）
    std::uint64_t occurrence = 1;             ///< 第几次命中触发（≥1；0 无意义——计划构造方责任）
};

/// 故障计划：若干触发规则的集合（同一故障点可有多条规则——任一命中即注入）。
struct FaultPlan {
    std::vector<FaultTrigger> triggers;
};

/// 命中日志：按实际注入顺序记录（故障点, 触发时的命中序号）——观测面，供断言。
struct FaultLog {
    std::vector<std::pair<std::string, std::uint64_t>> hits;
};

/**
 * @brief 计划裁决＋命中记录的非模板核心（实现在 Fault.cpp）。
 *
 * @param plan        [in] 故障计划
 * @param faultPointId [in] 本次命中探测的故障点标识
 * @param occurrence  [in] 该故障点本次是第几次命中（自 1 计）
 * @param log         [out] 命中时追加记录；可为 nullptr（仅探测不记录）
 * @return true＝命中计划（调用方注入失败）；false＝放行
 *
 * 确定性：纯函数式裁决（无随机、无时钟）——同计划同命中序列同结果（NFR-COR-02）。
 */
bool faultShouldFire(const FaultPlan& plan, const std::string& faultPointId,
                     std::uint64_t occurrence, FaultLog* log);

/**
 * @brief 故障拦截器：包住被测单元的"真实实现"，把计划接到接口方法流。
 *
 * 用法（§6.4 原文形态）——测试目标内实现被测接口 I，方法内先 shouldFire 决定
 * 注入失败（抛被测接口声明的错误类型），未命中则转调 real()：
 * @code
 * struct FakeWriter : IFileWriter {                 // IFileWriter＝被测单元自有接口
 *     FaultInterceptor<IFileWriter> fault;
 *     void writeChunk(std::span<const std::byte> data) override {
 *         if (fault.shouldFire("io/file-writer/write-chunk", ++calls_))
 *             throw IoError("disk-full-simulated");  // 磁盘满模拟：按计划在指定次失败
 *         fault.real()->writeChunk(data);            // 未命中→真实实现
 *     }
 * };
 * @endcode
 *
 * @tparam I 被测单元声明的接口类型（如 IFileWriter、IChannel）
 *
 * 生命周期：持有 real 的 shared_ptr（调用方 shared 所有权语义）；本对象按值持有
 * 计划与日志。非线程安全（测试串行）。
 */
template <class I>
class FaultInterceptor {
public:
    /// 绑定真实实现与故障计划；real 不得为空（测试代码契约违约——直接抛）。
    explicit FaultInterceptor(std::shared_ptr<I> real, FaultPlan plan)
        : real_(std::move(real)), plan_(std::move(plan))
    {
        if (!real_) {
            throw TestKitError(TestKitErrorKind::Usage,
                               "fault: FaultInterceptor 的 real 实现不得为空");
        }
    }

    /// 计划裁决＋命中记录（见 faultShouldFire；模板壳只做转发——逻辑单点在 .cpp）。
    bool shouldFire(const std::string& faultPointId, std::uint64_t occurrence)
    {
        return faultShouldFire(plan_, faultPointId, occurrence, &log_);
    }

    /// 命中日志快照（按注入顺序）。
    FaultLog log() const { return log_; }

    /// 真实实现访问器（未命中路径转调用；shared 所有权不转移）。
    const std::shared_ptr<I>& real() const noexcept { return real_; }

private:
    std::shared_ptr<I> real_;   ///< 被测单元真实实现（共享持有）
    FaultPlan plan_;            ///< 故障计划（构造时固化）
    FaultLog log_;              ///< 命中观测（shouldFire 累积）
};

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_FAULT_HPP
