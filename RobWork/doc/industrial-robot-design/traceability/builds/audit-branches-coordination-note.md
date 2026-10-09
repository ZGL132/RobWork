# 审计修复分支与并行工作流的分支协调记录（2026-10-09）

## 背景

audit 系列修复分支（audit-p0-fixes → audit-p1a-fixes → 本分支 audit-p1b-fixes）
与 ui-t77 工作流会话**共享同一工作树**（D:\10_Source_Repos\21_robot\RobWork）
作业期间发生两次交叉：

1. 审查批次 A 门禁首轮 67/85：13 项冒烟构建失败系并行会话在途编辑
   requirements 插件所致（C1083 打不开 obj——文件半写/占用），非代码缺陷。
2. 并行会话在共享树处于 audit-p1a-fixes 检出态时执行了提交，其
   `[UI-T76] F-555/F-556 修复`（1a9b5523）落在审计分支上并随审计推送
   进入 origin/audit-p1a-fixes。

## 已完成的协调动作

1. **共享工作树切回 ui-t77**（与 origin/ui-t77 同步）：并行会话的后续
   提交回到其自身分支；其未提交的 ui-t74 冒烟产物（console-tour.log/
   driver-verdict.log/tour-5-final-state.png）原样保留在其工作面上。
2. **审计修复作业迁移至隔离工作树**：`D:\10_Source_Repos\21_robot\
   RobWork-audit`（分支 audit-p1b-fixes，基于 audit-p1a-fixes@9bb32719），
   独立构建树——此后批次 B/C/D 的修复、构建、测试、门禁全部在隔离树
   进行，与并行会话零共享（除 origin 推送面）。
3. 隔离树按 DTB §5.1 全新树前置配置：vcpkg 工具链/Qt 前缀以绝对路径
   引主仓；RobWorkStudio.ini.template.static 自源码树 bin/ 复制
   （AGENTS.md §4.1）。

## 遗留处置项（所有者裁决）

**1a9b5523（[UI-T76] F-555/F-556 修复）误落 audit-p1a-fixes**：

- 现状：已在 origin/audit-p1a-fixes（随审计批次 A 推送进入远程历史）；
  不在 ui-t77 上。按"禁止改写已推送历史"纪律，审计分支不摘除该提交。
- 该提交内容属 ui-t77 工作流域（F-555/F-556 的后续修复，触碰
  requirements 插件＋ui 预览后端 7 文件）。
- 处置选项（由 ui-t77 所有者选择）：
  - 选项一：`git cherry-pick 1a9b5523` 至 ui-t77（若与该分支已演进的
    文件版本冲突则按语义重放）；
  - 选项二：随审计分支合流 redesign-main 时一并进入（合流说明中已
    标注该提交归属 ui 工作流）。
- 该提交在 audit-p1a-fixes 上的 85/85 门禁已被覆盖验证（run2 于含此
  提交的树状态执行）。

## 审计分支图谱

```
redesign-main@a30f0a94
└─ audit-p0-fixes        （F-570/571/572，P0×3，已推送）
   └─ audit-p1a-fixes    （F-573~F-579 批次 A，已推送；含误落的 1a9b5523）
      └─ audit-p1b-fixes （本隔离工作树——批次 B/C/D 修复线）
```

三段式状态：audit-p0-fixes 与 audit-p1a-fixes 均已发验收请求（证据见
各自 traceability/builds/ 目录），待验收合入 redesign-main。
