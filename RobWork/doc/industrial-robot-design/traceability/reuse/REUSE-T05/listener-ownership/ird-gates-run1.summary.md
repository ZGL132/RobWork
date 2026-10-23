# ird_gates 第一次真实运行——结果摘要（原始日志：ird-gates-run1.log）

- 命令：`cmake --build build-int-lsn --config Release --target ird_gates`（2026-10-23，HEAD `fbd51e765`，工作树干净）
- 退出码：**1（失败）**。原始日志为控制台混合编码（中文段 GBK 输出被重定向俘获），ASCII 标识符与命中码完全可辨；本摘要为人工解码，原始字节以 ird-gates-run1.log 为准。

## 第一段命令：红线引擎（cmake/ird_gates.cmake）——已执行，失败

引擎自测（干净夹具＋植入违例检出）全部按设计通过：
`pass_clean`、`pass_perf_targets`、`pass_perf_registered_edge`、`pass_openbench_worker`、`pass_r4comment`、`pass_r4cliff`（通过类）；`fail_perf_unregistered→IRD-GATE-SUB`、`fail_openbench_testkit→IRD-GATE-T1`、`fail_perf_lookalike→IRD-GATE-T1`、`fail_r4cliff→IRD-GATE-R4`、`fail_r1→IRD-GATE-R1`、`fail_t1→IRD-GATE-T1`、`fail_r5→IRD-GATE-R5`、`fail_sub→IRD-GATE-SUB`、`fail_r3→IRD-GATE-R3`、`fail_r4→IRD-GATE-R4`、`fail_t2→IRD-GATE-T2`（植入违例均按预期检出）。

真实扫描命中 **2 处**（本包新增子进程辅助目标 `sdurws_ird_testkit_lsnchild` 的链接边未登记）：

1. `IRD-GATE-SUB`：表外依赖边 `sdurws_ird_testkit_lsnchild → sdurws_ird_testkit` 不在 ARCH §3.5 白名单。
2. `IRD-GATE-T1`：该目标链接 testkit（testkit.md §2.4 T-1：产品目标不得链接 testkit——对非测试目标形态的同一禁链检查）。

引擎按 ARCH §3.2 SA-10 判构建失败（"存在 2 处命中"），目标终止。

## 第二段命令：validate-reuse.ps1——**本次未执行**

ird_gates 目标的第一段命令失败后，第二段（复用校验）未运行；其已知首个阻断（REUSE-UNDECLARED，凭证未整合）来自上一轮单独运行（validate-reuse.log），不代表本次完整复用校验结果。

## 处置路径（testdata_lint 先例，非放宽门禁）

`sdurws_ird_testkit_lsnchild` 是 F-958 定向测试的隔离子进程载体（test-support 工具，不入产品分发面，与 testkit 同生命周期），与 TK-T03 `sdurws_ird_testdata_lint` 同型。按登记机制（白名单注释："边白名单与例外登记数据见 cmake/ird_gates_whitelist.cmake（数据面；人工登记册＝DTB §4.5，两处必须一致）"）需协调会话登记三处：

1. `IRD_TOOL_TARGET_EDGES` 增列 `"sdurws_ird_testkit_lsnchild->sdurws_ird_testkit"`；
2. `IRD_T1_EXEMPT_TARGETS` 增列 `"sdurws_ird_testkit_lsnchild"`（豁免"产品目标"形态误判——开发工具非产品交付物）；
3. development-task-breakdown.md §4.5 人工登记册同步行。

凭证/功能/库存片段见 governance-suggestions.md。登记落地后复跑 `ird_gates` 一次取正式结果。
