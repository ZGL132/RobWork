# WP-13-T24 B4c-3c 检查点（WIP）

基线37ddc7ded99cea94ae876a44bff7db8864accefe，分支wp13-t24。整体建模验收仍不通过；本批封闭正式冲突至重新应用/两次重开链和草稿生命周期缺陷。

- PASS：十套件1516通过、0失败、0跳过；逐套XML/ird核对见checkpoint-summary.json。Windows真实GUI232通过（建模83、UI94、独立建模55）；重复专项不累计。
- PASS：正式宿主modeling-tour退出0，359项检查通过、0失败，与组件计数分列。经真实draft.apply确认→Stale原完整稿ApplyRetained落盘→查看对象并取消→显式完整重建→旧稿逐字段对照→真实控件手工重填→再次明确应用。1 rad策略比较拒绝零提交，24 rad恢复后仍先处理新Stale，不自动换锚。
- PASS：成功应用后经既有C-5/后台串行执行器消费本模块磁盘current/bak/new，再清控制器脏态、冲突及历史。失败保持控制器状态，runner保留真实Committed修订并隔离回执异常；其他域继续。真实ProjectStore关闭重开已应用模块Missing、另一模块原载荷保留，HEAD不变。
- PASS：控制器终身单调脏代次避免消费后新编辑与旧autosave ABA；迟到回执既不恢复已消费保存来源，也不清新编辑。保留consume-controller-red、late-save-aba-red与consume-contract-red；consume-contract-fixture-failure为测试构造省略真实branch导致的夹具错误，不作为产品红证。
- PASS：真实局部undo返回应用基线后，域唯一完整规范比较给出clean；宿主经notifySessionClean结算磁盘旧稿，只清dirty/来源/时刻，保留redo及冲突。redo重新置脏。冲突未决期间保留原完整磁盘稿，不能提前消费。新增3项控制器红例保留clean-baseline-controller-red；真实add/undo/redo及无摘要根字段/部件差异由新增GUI验证。
- PASS：正式第一次重开精确匹配保存修订且clean/Missing；中途所有建模编辑撤回后不再autosave无编辑旧稿。其他域更新场景后的第二次重开保留同根、完整场景部件ID/type/原字节，继续改origin-z=0.177 m的命令仍携带完整部件且零项目修订。强断言未放宽。
- PASS：集成全量Release、独立建模三目标及verify-task三条全部通过，含ird_gates零命中与validate-docs。RWS_BUILD_INDUSTRIALROBOT=ON。VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、全程跨树GUI互斥。已查看新冲突/旧稿对照/最终重开窗口截图。
- PASS：全量UI契约首轮62通过、1失败仅为新增代码使现行装配清单行锚2755→2770；manifest-anchor-drift保留失败，更新实际行锚后63通过。未改测试断言。
- FAIL（历史、本批已修复）：旧tour误期待Stale直接成功，以及未消费磁盘稿、undo后无条件脏通知导致覆盖后来场景，分别原样留在本目录红证。中间一次新增clean误消费冲突旧稿的原日志被同名重跑覆盖，只有工具输出和计划登记；不虚称有冻结原日志。最终绿轮PNG均来自完整本轮流程，未用旧图冒充新图。
- NOT RUN：干净会话外部修订的完整实时刷新专项、F01/F14剩余入口及正式R2装配、完整部件CRUD/应用/重开矩阵、47项全部真实mesh/安装/TCP/导航/透明线框/遮挡高亮和全新上下文独立验收。截图中的代理图元不得当成真实网格验收通过。

仅本地WIP、不推送、不合入、不使用子代理；F06/F07未修改/集成。下一批继续尚未闭合项，不能将本批359项或1516项外推为全功能交付。
