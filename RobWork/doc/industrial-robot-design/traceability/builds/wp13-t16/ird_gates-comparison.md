# WP-13-T16 ird_gates base..head 引擎直跑归一化比对附件

- 任务：WP-13-T16（modeling 契约测试套件与黄金数据集落位）
- 分支：wp13-t16；base＝a4c8c4166b49261aa71c2ada661a91bf5d8512e1
- 比对口径：cmake -P 引擎直跑（ird_gates.cmake），IRD_ROOT＝两侧各自的
  industrialrobot 源码根；head 侧＝主检出工作树（未提交态实况），base 侧＝
  临时 worktree（@a4c8c416，比对后已删除）。命中清单取"[ird_gates] 命中 N 项"
  裁决段（排除自测段的自检输出），路径前缀按两侧根归一化后排序比对。
- 结论：**base..head 命中多重集完全一致（58 = 58，diff 为空）——本任务
  零新增命中**。58 处存量命中为既有状态（ui 插件链接面/测试目标 testkit
  边/R3 ui Qt 面/R4 project 字面量/R5/LIB pugixml·libzip 词表/SUB 表外边
  等，治理侧 DTB §4.5 登记册回填待办；VS 目标 ird_gates 退出码 1 同源）。

## 附件文件

| 文件 | 内容 |
| --- | --- |
| ird_gates_head_raw.txt | head 侧引擎直跑完整输出（工作树实况） |
| ird_gates_base_raw.txt | base 侧引擎直跑完整输出（临时 worktree） |
| ird_gates_head_hist.txt / ird_gates_base_hist.txt | 归一化直方图（早期版；裁决段精确清单见 head_hits/base_hits） |
| head_hits.txt / base_hits.txt | 归一化裁决段命中清单（sort 后逐行；diff 为空） |

## 比对命令（可复现）

```bash
# head 侧
cmake -DIRD_ROOT="D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot" \
  -P "D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake"
# base 侧（临时 worktree @a4c8c416，已删）
cmake -DIRD_ROOT="<worktree>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot" \
  -P "<worktree>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake"
```

差异裁决：新增/消失命中清单为空。若验收者对存量命中中涉及 modeling 面
的条目有裁决需要（ird_gates_whitelist.cmake 回填），属治理面待办——
本任务 allowedFiles 不含 cmake/ird_gates_whitelist.cmake 与 DTB。
