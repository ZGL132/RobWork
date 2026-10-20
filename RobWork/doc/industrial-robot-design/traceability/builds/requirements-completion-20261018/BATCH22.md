# WP-14-T12 第二十二批：完整修订对象集与三维操作后的安全关闭

状态：WIP，原120项尚未全部终验。未使用子代理。

## 修改与失败依据

- A-13按所有者批准实施：CommandService以实际父修订为底本，保留未被增量覆盖的业务对象；TxEngine私有CommitPlan增加默认空retainedObjects，清单按元数据、原有保留项、本次新项装配。保留字节不重写、不列入introducedObjects，历史不改；非法引用、重复OID或摘要失配在序号分配和磁盘副作用之前拒绝。
- batch22-project-red的已有断言真实失败；batch22-expanded-red新增四项全部真实失败。后续覆盖同OID替换、纯元数据、重开读字节、历史原样、非活动分支/建支底本及八种非法计划零写入，定向转绿。
- batch22-integrated-project初次完整模型回归282跑：281通过、1失败。旧UndoRedo测试误把空objectWrites解释成删除所有对象；所有者精确批准后，仅修正该测试的注释/断言，核对纯元数据保留原引用、重放追加新OID及两份负载相同。没有修改撤销产品代码或共享输入件，不把机制测试当业务删除验证。
- batch22-host和batch22-host-close-diagnostic在已有真实会话姿态时第二次关闭发生AV。batch22-debug原生调用栈和batch22-host-debug日志确定关闭刷新经refreshPosePane、resolveSessionTcp访问已解绑模型。修复仅在UiPlugin.cpp无项目路径先清KinSessionPose再解绑快照；没有修改运动学公共接口。原失败和调试事件原样保留。

## 最终真实验证

- [PASS] batch22-build/integration-verified.log与standalone-verified.log：两个完整构建依次执行并明确退出0；集成缓存启用RWS_BUILD_INDUSTRIALROBOT。
- [PASS] batch22-final-integrated：9个exe，1160跑，1159通过、1既有占位跳过、0失败。
- [PASS] batch22-final-standalone-project、requirements及ui：8个exe，1022跑，1021通过、1既有占位跳过、0失败。
- 每个exe保留原XML、控制台和JSON。core没有自行输出JSON，其报告明确由原XML转录；其余JSON由exe生成。跳过按实际testcase/skipped元素统计，完整计数见batch22-verification-summary.json。
- [PASS] batch22-final-host-modeling：正式宿主235条断言通过、0失败，进程退出0。单独更新夹具后关闭重开，HEAD及完整objectRefs不变、创建新的WorkCell、机器人仍在、夹具平移(1.5,0.3,0.2)m及Rz90°与独立输入一致，场景实际挂接；关闭前确有会话姿态，关闭后已清空。
- [PASS] batch22-final-host-requirements：正式需求宿主141条断言通过、0失败，进程退出0。
- batch22-manifests保留真实项目HEAD和四代manifest副本：seq4仍含robot-design和scene-object，未修改真实项目历史文件。
- Windows GUI均VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、一次一个进程。mtour-8-scene-update-reopened.png已目视检查：夹具标记盒及轴存在，近景相机及空建模树不能证明完整机器人mesh保真。
- [PASS] batch22-build/gates.log、validate-task.log及verify-task-final.log：红线零命中，契约校验通过，三条机器验证命令全部真实执行通过，含完整集成构建及文档校验。源码/文档diff --check通过；原始MSBuild空白不清洗。首轮verify-task的构建和门禁通过，但文档校验因新证据尚未git add拒绝；已保存verify-task-first-staging-failed.log，暂存证据、刷新索引后完整重跑通过。

## 仍需继续

原120项逐行当前版本终验、建模工作集/共享树重开恢复、每点与多TCP真实消费、三种方向规则及部分约束求解、样本结果着色与选择、真实mesh/法向和轨迹动力学等消费仍需验证或实现。组件绿与标记盒拾取均不能覆盖这些项目；不声明WP-14-T12验收完成，不自行合入redesign-main。
