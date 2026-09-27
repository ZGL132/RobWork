# WP-15-T13 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-24
- 任务：WP-15-T13（契约测试与运动学黄金数据集）
- 分支：wp15-t13；base＝e837f1ab7bd9732a62c90006a00e8ecf8de2dc3d
- 现场：head 侧＝主检出（分支 wp15-t13 工作区）；base 侧临时 worktree
  （D:/10_Source_Repos/21_robot/rw15t13_base_wt，基 e837f1ab）用毕即删。
- 口径：引擎直跑（`cmake -DIRD_ROOT=<industrialrobot 源码根> -P
  cmake/ird_gates.cmake`，T02~T12 确立口径），命中行归一化（两处树根前缀
  `D:/10_Source_Repos/21_robot/rw15t13_base_wt/...` 与
  `D:/10_Source_Repos/21_robot/RobWork/...` 统一归一为 `<IR>`；引擎双行输出
  的 `[ird_gates] ` 前缀行与裸行同 detail 归并后 sort -u）后逐行比对；
  存量例外未清零（DTB §4.5），本任务不做清零。
- 计数口径声明（F-398——三套计数分别列明，不混标；F-402/F-405/F-407——
  注明去重计数与引擎自计的组成关系）：
  ①直方图＝命中码 × 日志出现行数（`grep -o "IRD-GATE-[A-Z0-9]*"` 按码计
  数；含引擎双行输出——base/head 均 151 行）；
  ②去重条目＝归一化命中行 sort -u 条目数（base_hits.dedup 79 条 /
  head_hits.dedup 79 条）；
  ③引擎自计数＝日志末尾 `[ird_gates] 依赖红线/补丁门禁存在 N 处命中`
  的引擎自报数（base 72 / head 72）。
  ②与③的组成关系：79（去重条目）＝72（引擎自计的源码命中）＋7（引擎自
  测段 token——fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_t2
  七行"按预期检出"自测输出，非源码命中；两侧对称出现）。①与②的关系：
  151＝2×72（引擎对每命中打印带前缀与裸两行）＋7（自测 token 单行）。

## 直方图（命中码 × 日志出现行数）

| 码 | base（e837f1ab） | head（wp15-t13） | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 12 | 12 | 0 |
| IRD-GATE-R1 | 11 | 11 | 0 |
| IRD-GATE-R3 | 41 | 41 | 0 |
| IRD-GATE-R4 | 3 | 3 | 0 |
| IRD-GATE-R5 | 1 | 1 | 0 |
| IRD-GATE-SUB | 77 | 77 | 0 |
| IRD-GATE-T1 | 5 | 5 | 0 |
| IRD-GATE-T2 | 1 | 1 | 0 |
| 合计 | 151 | 151 | 0 |

## 归一化比对结论

head 相对 base **零新增、零消除**：归一化去重条目逐行 diff 完全一致
（79＝79，`diff` 空；直方图逐码相等；引擎自计数相等）。本任务全部改动
落在允许面——

1. `kinematics/test/GoldenKinTest.cpp`（新增测试 TU——测试目标源码不在
   引擎扫描域，T-1 允许形态：`sdurws_ird_kinematics_test` 链接
   `sdurws_ird_testkit` 的既有登记边未变化）；
2. `kinematics/CMakeLists.txt`（仅测试目标源列表增列一行——产品目标链接
   面零变化，T-1 断言面不动）；
3. `testdata/golden/kin-*`＋`testdata/tolerance/kin-*`（数据资产——不在
   构建图内）；
4. `units/kinematics.md`（文档面）。

（T-1 断言的具体执行证据：IRD-GATE-T1 五处命中 base/head 逐条一致，均为
存量登记面——产品目标零 testkit 链接由引擎与契约测试 BuildGraphContractTest
双防线钉住。）

## 附件清单

- `base_gates_raw.log` / `head_gates_raw.log`——引擎完整输出
- `base_hits.norm` / `head_hits.norm`——剥前缀后的命中行（未归一根）
- `base_hits.dedup` / `head_hits.dedup`——归一化树根后 sort -u 的比对基
- `base_hist.txt` / `head_hist.txt`——命中码直方图
