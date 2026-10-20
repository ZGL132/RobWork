# WP-13-T24 B4c-3d 检查点（WIP）

基线7fcd8545，分支wp13-t24。整体建模验收仍不通过；本批封闭干净编辑器外部修订完整刷新缺口。

- PASS：十套件1520通过、0失败、0跳过；逐套XML与ird报告核对见checkpoint-summary.json。Windows真实GUI233通过（建模84、UI94、独立建模55），重复专项不累计。
- PASS：正式宿主modeling-tour退出0，361项检查通过、0失败；外部场景真实创建和修改命令后，不关闭项目即核对当前编辑器base、完整payload逐字节、pending=false，与固定修订真实读取结果完全相同。原冲突/原稿保存/取消/完整重建/手工重填/再次应用/两次重开与继续编辑强断言全部保留。
- PASS：模块干净事件或已锚不同tip绑定，必须先完整读取根/活动部件并验证module/base/branch/干净完整帧后一次采纳；失败不换锚、不改值、基线或历史，事件不抛Qt且旧预览诚实缺席。无根明确nullopt是合法空工作集。脏稿仍保留原expectedRevision接受Stale，其他分支及重复同修订零读取。3项新增模块用例先红后绿，原红证在clean-refresh-red。
- PASS：真实GUI新用例验证完整新根/工具立即入树、工具表单显示新值、旧未确认输入被清、无伪造retainedConflictDraft、无跨修订局部undo；已查看clean-revision-live-tree-and-form.png及正式重开窗口。后者只呈现代理图元和坐标架，不能外推真实mesh正确。
- PASS：正式红轮证明revision事件实际到达，但actual仍旧锚、payload不等且pending=0。同步事件在project事务writer锁内回查QueryPortImpl再取同锁。私有桥现只复制值排入宿主UI队列，提交返回后检查来源adapter实例/关闭/project身份再调用域完整读取；无公共签名/存储/project实现改动。previous-modeling-tour-*保留两轮失败日志及截图，最终绿轮目录全新。
- PASS：现行装配清单只同步实际行锚：modeling 2696→2700、requirements 2770→2774，UI契约63通过，未改冻结证据或测试断言。
- FAIL（执行方式，已纠正）：收尾误将full-build和会再次构建同树的verify-task并行，MSBuild因UiPlugin.obj被占用报MSB6003；原日志verify-overlap-build-failure.log保留。全量构建自身退出0；随后串行重跑verify-task三条全部通过（含再次完整构建、门禁零命中和文档校验），不把争用失败计通过。
- NOT RUN：47项全功能最终验收、真实mesh与全部安装/TCP/导航/透明线框/遮挡高亮；F01/F14剩余入口及正式R2完整装配；完整部件CRUD/应用/重开矩阵；全新上下文独立验收。显式冲突重建后尚未重新编辑即收到又一外部修订时，ApplyRetained磁盘素材的延续保护仍需专项实证，未声明闭合。

集成全量Release、独立建模三目标及最终verify-task三条均PASS。构建与门禁最终结果见full-build.log、standalone-build.log、verify-task.log。运行环境VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、同桌面单GUI进程；RWS_BUILD_INDUSTRIALROBOT=ON。仅本地WIP，不推送、不合入、不用子代理；F06/F07未修改或集成。
