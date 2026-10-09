# audit-p1a-fixes 批次（P1·并发面）验证证据（F-573~F-579，2026-10-09）

## 修复内容（7 条登记，6 项代码修复＋1 项改判）

| 编号 | 单元 | 摘要 | 提交 |
| --- | --- | --- | --- |
| F-573 | ui | m_revisionEpoch 跨线程读改写→atomic | 44e7427f |
| F-574 | diagnostics | writeFailures 锁外读→atomic | bd812c85 |
| F-575 | project | 身份查询面无锁读→writer 锁内快照（m_writerMutex 加 mutable） | 77efdaa4 |
| F-576 | execution | 派发闸未接线→构造接线＋析构摘闸＋出队终态防御＋回归测试 | db376476 |
| F-577 | execution | worker 正常终结误回池僵尸记录＋死进程峰值永久计入→终结即销毁＋聚合排除＋契约测试重钉 | d9870b08＋f25d14c9 |
| F-578 | execution | requestCancelAll/abandonAllForced：**改判**（逐调用点核实现网均在主锁串行域内，不构成现行竞争）→锁域契约升格 | 93965cec |
| F-579 | execution | Broken 分支未等进程终结取退出码→有界等待＋超时强杀兜底 | d9870b08 |

## 验证方式与结果

1. **受影响测试目标全量（修复后）**：
   - `sdurws_ird_execution_test` 150/150；`sdurws_ird_execution_contract_test`
     38/38（真进程场景——含 F-577 后重钉的 EX_WKR_1 九步接纳流与
     AggregateJobMemory 存活窗采样两用例）；
   - `sdurws_ird_diagnostics_test` 115/115；`sdurws_ird_project_test`
     254/254（1 项既有登记跳过）；`sdurws_ird_ui_test` 254/254。
2. **红灯验证**：F-576 回归用例 QueuedCancelDoesNotPoisonDispatch_F576
   ——撤 src 修复后抛 ExecutionError（矩阵外转换 DispatchDequeued 于
   canceled）恰好失败，恢复后通过（现场输出：`execution/statemachine:
   矩阵外转换请求（触发 DispatchDequeued 于状态 canceled…）`）。
   F-577 的行为修复由两个契约用例的旧钉失败反向证实（EX_WKR_1 :651
   phase==Idle 断言失败→重钉后全绿）——旧钉即病态行为的测试化存证。
3. **全量门禁**：`audit-p1a-fixes-gate-run2.log`——**85/85 通过（EXIT=0）**，
   双模式构建零错误、ird_gates 零命中、全部 40 个注册测试目标双模式
   全绿（含 P0 批次基线上的两个既有 GUI 失败项——本轮工作树状态下
   亦通过，见下述"并发工作流"说明）。
   - 首轮 `audit-p1a-fixes-gate.log`（67/85）失败构成：13 项冒烟构建
     失败＝并行工作流在途编辑 requirements 插件所致（C1083 打不开其
     obj——文件被外部进程占用/半写），2 项 execution_contract 失败＝
     F-577 契约重钉前的旧钉（本批已重钉），2 项 GUI 为 P0 批次已录
     基线失败；均非本批代码缺陷。
4. **发现改判（F-578）**：原审查 P1#48（cancelAll/abandonAll 绕主锁数
   据竞争）经逐调用点核实为定性过重——abandonAllForced 唯一生产调用
   点在 poll 超阈值兜底（tick 主锁段内）；requestCancelAll 生产代码零
   调用（shutdown 主锁内自带遍历）。处置为锁域契约升格（status=closed）。

## 并发工作流声明（重要）

本批门禁与部分测试运行期间，同一工作树存在**另一会话的在途编辑**
（requirements 插件 4 文件＋ui 预览后端 3 文件＋ui-t74 冒烟产物，未提
交）。85/85 的门禁结果反映"本分支提交＋该在途编辑"的合成状态；其中
requirements/ui 目标的通过可能受益于该在途工作。本批 7 个提交均只显式
暂存自己的文件，未触碰对方任何文件；findings.json 的 F-573~F-579 登记
为本批写入（与对方无冲突）。**建议尽快协调：该会话应在其自己的任务分
支（如 ui-t77 系）上作业，避免与本修复分支互相污染。**

## 结论

批次 A（并发面）7 项发现闭环：6 修＋1 改判，全部具备真实失败能力验证
或调用点级核实；门禁双模式全绿。分支 audit-p1a-fixes（基于 audit-p0-
fixes@cbe7836c，其基于 redesign-main@a30f0a94）待验收合入。
