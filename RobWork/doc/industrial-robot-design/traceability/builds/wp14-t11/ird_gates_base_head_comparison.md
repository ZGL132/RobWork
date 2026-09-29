# ird_gates base↔head 归一化比对（WP-14-T11 实施段留痕）

- 任务：WP-14-T11（需求域装配门面补建——O-45 裁决）；分支 wp14-t11；
  base 2865a67f173c316bcca19e9a248355125013fee8（＝redesign-main HEAD，
  WP-15-T18 合入后主线）。
- 本附件口径：登记册回填归 WP-01-T03 治理面（本任务 allowedFiles 不含
  `cmake/ird_gates_whitelist.cmake`），实施段出具命中集增量声明与留痕，
  base 侧冷启复现与精确双向归一化裁决归验收段（wp14-t10 先例同型）。

## head（wp14-t11 工作树）门禁结论

- 命令：`cmake --build build --config Release --target ird_gates`（集成构建树）。
- 结果：**引擎退出 1**——命中集共 **82 条**（与 base 主线命中集同数；失败
  消息原文口径："依赖红线/补丁门禁存在 82 处命中——按 ARCH §3.2 SA-10 判
  构建失败；例外只经 DTB §4.5 登记册"）。
- 全文留痕：`ird_gates-head.log`；命中行清单：`ird_gates_head_hits_normalized.txt`
  （GBK 控制台转码畸变形态与 wp14-t10 留痕同源——F-207/F-231/F-309/F-336
  家族既录口径，可读性以本文件的语义声明为准）。

## 新增命中声明（head 相对 base 2865a67f）

**零新增**。理由：

1. 计数口径：引擎判定命中集 82 条＝base（WP-15-T18 验收记录的引擎直跑
   base↔head 归一化 82↔82 之主线侧）同数；
2. 形态口径：本任务变更面＝requirements 单元内新增 `assembly/` 门面头
   （非产品 include/ 扫描域、零 Qt、无链接边）＋plugin 目标增列一个实现
   TU＋contract_test 增列一个测试 TU＋测试文件别名修正——**不新建任何
   CMake 目标、不新增任何链接边、不改任何目标链接面**（契约测试
   PluginTargetLinkFaceUnchanged_O45 以单行链接语句逐字钉住）；
3. requirements 域既有命中形态零变化：`requirements_plugin → requirements`
   （R1 自边）与 `requirements_app → requirements_plugin`（R1 自边，
   WP-14-T10 已声明）两轮扫描各 2 处、合计命中集内位置与 wp14-t10 留痕
   一致；`requirements → ui`（SUB，插件链接面卡 §3.2 sanctioned）既有。
   门面出线的宿主消费边（`requirements` 相关 SUB 新边）**不存在**——
   宿主装配层尚未消费（UI-T23 三域集成收口时经三门面消费，其命中集
   增量按 O-45 裁决与 UI-T23 契约 v1.2 归该任务随其命中集增量登记）。

## base 侧说明

base＝2865a67f（WP-15-T18 合入后主线）。其门禁命中集在 WP-15-T18 验收
记录为 82 条（引擎直跑 base↔head 归一化 82↔82 集合零增量）。本任务 head
82 条－新增声明 0 条＝82 条，与 base 相容；精确双向归一化由验收段在
detached worktree 冷启复现（acceptance-protocol §3/§4，WP-15-T18 验收
侧先例同款）。
