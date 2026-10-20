# WP-13-T24 B4c-1 WIP检查点

**建模全功能验收仍未通过；本批只完成过期草稿真实应用前的数据保护修复。**

- [PASS] 完整七套件1194通过、0失败、0跳过；XML与原生ird报告逐项对账，core报告明确从XML派生。重算命令：`python checkpoint-summary.py`。
- [PASS] 集成全量Release、独立modeling三目标构建；verify-task机器三条（全量构建、ird_gates、validate-docs）通过。保留既有编译警告。
- [PASS] 真实门面恢复旧草稿→runDomainApply→真实ProjectStore拒绝stale-revision；项目tip和草稿载荷保持。未重放的外部事件也保留旧锚；跨分支未处置拒绝且后域继续。
- [PASS] 锚/组装失败不再冒称实际提交；状态行产品源码已同步区分失败/成功/无修改。
- [NOT RUN] 集成modeling_gui/ui_gui、独立modeling_gui、正式宿主tour及新局部历史/状态文案真实窗口；按用户指令等待另一工作树GUI结束并通知。
- [NOT RUN] 全新上下文独立验收；完整工作集apply/权威回执/跨域删除保护/真实网格/完整47项验收。

红证见anchor-red：3项模块探针为2失败+1干净正例通过，3项应用编排回归全部失败。修复后最终全套结果见checkpoint-summary.json；专项与最终不重复累加。

重跑环境与命令见run.ps1（保留本树GUI禁启保护）。当前工作目录和runner为本机绝对路径，外部复跑需改为自身工作树；Windows GUI恢复后仍遵守同一桌面一次一个进程。

本地WIP，不推送、不自行合入。F06/F07保持另一工作树职责；未启动/关闭/干预其GUI进程。

后续：先B4c-2完整槽/临时身份映射/不可变修订回执；保留对象的活动闭包投影及逆放、多次删除后重建、C-6权威切换和真正基于新修订重新编辑均要真实反例。用户通知后再补窗口/三维，不把本报告当作B4b/B4c或17类问题全闭合。
