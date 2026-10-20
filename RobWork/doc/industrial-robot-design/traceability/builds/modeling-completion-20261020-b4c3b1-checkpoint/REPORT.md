# WP-13-T24 B4c-3b-1 WIP检查点

**整体不通过。本批完成模块按固定修订重新读取及保留旧完整草稿；生产宿主冲突流程与全部47项三维仍开放。**

- [PASS] 十套件1499通过、0失败、0跳过：集成modeling444、contract17、ui331、ui_contract60、core65；独立modeling348、contract8；Windows GUI三套81+90+55。新增3项UI模型用例及1项窗口用例。checkpoint-summary.py从最终XML与ird报告逐套重算；core仅由原生XML派生。
- [PASS] reanchor-red真实证明无读取器时旧实现仍把旧草稿重标锚。修复以指定branch/revision完整读取、校验并原子建立干净基线，成功前保留旧状态；成功后保存原完整payload及原base作待迁移素材。无reader、坏响应、读错等拒绝，明确无根为空态；项目零修订。
- [PASS] 真实ProjectStore链在HEAD多次前进后仍精确读取首个旧修订，核对全部部件和原草稿保留，随后恢复最终权威回执继续磁盘重开验证。
- [PASS] VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe逐个执行，runner全程跨树互斥。已查看gui-modeling_gui/conflict-rebuilt-current-tool.png：新模型名和工具树正确、旧未确认表单取消、局部undo禁用；显式选工具后管理页值正确。226项窗口测试不替代真实mesh/导航/安装/遮挡终验。
- [PASS] 集成全量Release、独立三目标构建、verify-task机器三条通过，既有编译警告保留。最后仅同步rebuildOnRevision契约注释，无语义修改；未因此重复全量运行。
- [PASS] PackageTest进程内counter产生相同临时目录导致双模式并行互删，原退出3见standalone-overlap-abort。改唯一ObjectId目录后双模式专项各7通过，最终两套全量串行执行444/348通过；专项不重复累计。
- [NOT RUN] 生产reader绑定、冲突草稿落盘、三选项窗口和可见迁移入口；正式tour空快照失败出口。未重复执行上批已知FAIL；原证据保留于../modeling-completion-20261020-b4c2-checkpoint。模块层通过不代表正式宿主闭环。
- [NOT RUN] 全47项真实三维与全新上下文独立验收。仅本地WIP、不推送、不合入；未使用子代理，未修改/集成F06/F07。

前置错误如实分类：reader-fixture-invalid-old-root为夹具原有故意坏根；reader-build-missing-include和gui-fixture-build-missing-private-symbols为编译缺供；gui-tool-selection-test-assumption误要求自动选首工具，按既有选择契约改为显式focusObject。均不是产品红证。gui-old-executable-no-match两轮实际零用例，不计通过；本地runner新增零匹配拒绝。原失败和修复后证据均保留。

run.ps1记录本机绝对路径，其他检出须适配。previous-checkpoint-postchecks保存上批封存后检查日志。冻结后索引/文档/范围/commit-lint日志留运行目录，下一批只增保存。
