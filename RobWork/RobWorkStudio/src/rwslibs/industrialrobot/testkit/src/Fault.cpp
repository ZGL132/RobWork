/**
 * @file   Fault.cpp
 * @brief  故障注入原语的非模板核心——计划裁决与命中记录（Fault.hpp 模板壳转发至此）。
 *
 * 设计依据：
 *   - units/testkit.md §6.4（"第 occurrence 次命中触发"语义、命中序列观测）
 *   - 任务契约 tasks/foundation/TK-T09.json（TK-FAULT 用例载体）
 *
 * 实现说明：裁决逻辑单点在本文件（模板成员只转发）——命中规则与日志格式若需
 * 调整只有一个改动点；同计划同命中序列必同结果（无随机/时钟，NFR-COR-02）。
 */

#include <sdurws/ird/testkit/Fault.hpp>

namespace sdurws::ird::testkit {

bool faultShouldFire(const FaultPlan& plan, const std::string& faultPointId,
                     std::uint64_t occurrence, FaultLog* log)
{
    // 线性扫描计划（测试规模下计划条目极少；语义清晰优先于容器选择）。
    for (const FaultTrigger& t : plan.triggers) {
        if (t.faultPointId != faultPointId) {
            continue;                                 // 故障点不同——不相关规则
        }
        if (t.occurrence == occurrence) {             // 恰好第 occurrence 次命中——注入
            if (log != nullptr) {
                log->hits.emplace_back(faultPointId, occurrence);
            }
            return true;
        }
        // occurrence 大于/小于规则值的命中：既不注入也不记录（只记录真实注入）。
    }
    return false;
}

}  // namespace sdurws::ird::testkit
