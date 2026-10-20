# WP-15-T20 第二批实施报告：F04私有配置面修复

工作树为3582，分支wp15-t20；起始提交8bd146f6558579ad2d640124004a1ade6b49b9ca，审核基线5fd9a3d192ea1f67484bed7aec089ec10719c51a。本批串行执行，未使用子代理；未修改建模、共享UiPlugin、其他工作树或框架源码。70项方法保持MATRIX.md；本批逐项状态见MATRIX-RESULTS-stage2.md。实施报告不等于独立验收，不授权主线合入。

## 本批根因与修改

原策略下拉只更新m_draftStrategy，没有进入数值表的确认修改集；公共参数表的void保存出口不能反馈失败，确认移交后仍会清空数值草稿。首次基线注入后未刷新表格；确认区展示后再编辑会把新输入借旧确认写出；持久化拒绝/缺通道仍返回ok=true，抛异常还会穿越编排边界。

现数值解析、单位、筛选、粘贴继续复用ui公共参数模型，运动学私有面板负责策略与数值的整体非模态确认。确认冻结修改集与会话基线，草稿/基线不一致即拒绝。仅保存端口返回成功才前移savedConfig与表单基线；拒绝、缺通道、std::exception均返回失败和原因，完整草稿可直接重试；取消恢复策略和数值。初次显示已保存数值，缺保存端口禁用保存并说明原因。不自动重算、不写项目修订、不改canonical或存储格式。

## 实际执行

| 检查 | 结论 | 证据 |
| --- | --- | --- |
| 新增9项修复前反例 | 1通过、8失败；失败均为预期产品行为 | stage2/raw/kinematics_gui-config-red.xml及对应ird报告、test日志 |
| 修复后相同9项 | 9通过，0跳过 | stage2/raw/kinematics_gui-config-green2.xml及报告、test日志 |
| 集成模式目标构建 | PASS；运动学模型/契约/GUI与正式宿主四目标，不是全仓所有目标 | stage2/raw/build-kinematics-stage2-final.log及verify中时间戳构建日志 |
| 独立模式配置/三目标构建 | PASS；vcpkg和Qt参数显式传入 | stage2/raw/standalone-configure-kinematics-stage2-final.log及standalone-build日志 |
| 集成模型/契约/GUI | 287通过＋1跳过；57通过；35通过 | stage2/raw/kinematics-时间戳.xml、kinematics_contract-时间戳.xml、kinematics_gui-时间戳.xml及报告 |
| 独立模型/契约/GUI | 279通过＋1跳过；57通过；35通过 | stage2/raw/standalone_kinematics-时间戳.xml、standalone_kinematics_contract-时间戳.xml、standalone_kinematics_gui-时间戳.xml及报告 |
| ird_gates | PASS，产品扫描零命中；故障夹具按预期检出 | stage2/raw/gates-kinematics-时间戳.log |
| 契约verify | PASS，11条机器命令、0条人工条目，含双模式/六套测试/门禁/文档校验 | stage2/raw/verify-task-stage2.log |
| 正式宿主配置保存后项目切换/重开 | NOT RUN，不将本夹具外推到共享宿主 | 后续F04宿主消费验证 |
| 正式宿主真实网格/TCP坐标架/导航与覆盖层 | NOT RUN | F13保持开放 |
| 标准负载P95与70项终验 | NOT RUN | F14及完整矩阵保持开放 |

唯一模型跳过项均为PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1（GUI注册占位）。真实GUI单独运行，保留模型原始跳过计数。两模式数量分别报告，不能混称完全相同的覆盖。

初次产品构建因误用提示行name/value成员失败，已更正为label/displayText，原失败日志gui-build-kinematics-config-green1.log保留；后续构建与完整verify已通过。原始MSBuild/CMake输出（含既有警告、编码和空格）原样保留，不改写成“零警告”。

## 交互与证据边界

Windows GUI以VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe路径串行执行；runner在启动前及运行中检查其他GUI进程，未用offscreen。控件实际分发Qt鼠标按下/释放事件，未用私有槽替代按钮。窗口截图在stage2/screenshots/，已检查完整策略old→new与保存基线显示；程序化填值不宣称人手或OS级键盘鼠标验收。

合法保存使用真实canonical编解码和临时QSettings ini落盘重开；故障只注入既有configPersist副作用端口，计算/数值解析不打替身。这个临时适配器不能证明正式宿主QSettings装配、异常后的磁盘原子性或项目刷新时的基线同步。F04私有面已修复，宿主消费仍开放：共享UiPlugin保存回调与m_kinConfigBaseline刷新/恢复需后续真实端到端验证，本批避开另一工作树正在修改的同文件。

## 并行冲突控制与接续

本批产品路径均在kinematics私有plugin、同单元GUI测试/CMake；未触碰公共include/assembly、modeling或ui宿主实现。另一工作树486b当时有UiPlugin.cpp和optimization契约测试改动，未覆盖或重置。共享evidence-index.json仅由生成器更新；将来合入若冲突，基于双方新增证据重生成，不选边覆盖。

F01～F03沿用stage1通过证据，F04本批私有面封闭；F05～F12仍须逐项修复，F13/F14仍缺实证，不把本批套件全绿当全功能通过。下一批优先F06完整Jacobian/候选q和F10任务点双击的真实反例，仍避开建模并行面。提交/推送状态以实际Git及build/kinematics-audit-20261021/同步日志为准；不因本报告出现“提交”字样推断远端已同步。
