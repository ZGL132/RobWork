# ird_gates base↔head 归一化比对（WP-15-T18 实施段留痕）

- 任务：WP-15-T18（运动学迁移三接入面＋Jog/Playback 呈现衔接——O-44 接入
  面 v1 诚实边界）；分支 wp15-t18；base 3f7dd0f11d6908099acb8d94f7b581d1a0a396fe
  （O-44 裁决登记提交，tick#414 实施领取冻结值）。
- 本附件口径：登记册回填归 WP-01-T03 治理面（本任务 allowedFiles 不含
  `cmake/ird_gates_whitelist.cmake`），实施段出具命中集增量声明与留痕，
  base 侧同树复现与裁决归验收段（wp14-t10/wp13-t20 先例同型）。

## head（wp15-t18 工作树）门禁结论

- 命令：`cmake --build build --config Release --target ird_gates`（集成构建树）。
- 结果：引擎退出 1——原始命中 82 处（与 wp14-t10 验收时同量级——引擎带
  既有登记态命中退出非零属既有形态，F-434 家族 GBK 乱码不影响 ASCII 骨架
  比对）；去重归一化后 28 类。
- 全文留痕：`ird_gates-head.log`；归一化清单：`ird_gates_head_hits_normalized.txt`。

## base（3f7dd0f1，O-44 裁决登记提交）门禁结论

- 命令：同上（`git stash` 暂存实施改动后同构建树直跑，`git stash pop`
  恢复——实施段增量声明口径；验收段 detached worktree 冷启复现不变）。
- 结果：引擎退出 1——原始命中 82 处；归一化后 28 类。
- 全文留痕：`ird_gates-base.log`；归一化清单：`ird_gates_base_hits_normalized.txt`。

## 增量声明（head 相对 base 3f7dd0f1）

**恰增 0 条**：归一化命中集 `diff`（base↔head）完全一致（28 类＝28 类）。
本任务零新增目标、零新增链接语句、零新增共享面触碰——三接入面全部编入
既有 `sdurws_ird_kinematics_plugin` 目标（源文件增列）、测试编入既有
`sdurws_ird_kinematics_test` 目标、harness 改造既有
`sdurws_ird_kinematics_app` 目标（链接面单行语句不变）——构建图契约测试
登记形态零变化，命中集因此零增量。

本域既有命中（前序任务已登记，本次零变化）：`kinematics_plugin →
kinematics`（R1 自边，WP-15-T12 登记）、`kinematics → ui`（SUB 插件链接面，
WP-15-T12/§3.2 sanction）、`kinematics_app → kinematics_plugin`（R1 自边，
WP-15-T12——modeling_app/requirements_app 先例同型）。
