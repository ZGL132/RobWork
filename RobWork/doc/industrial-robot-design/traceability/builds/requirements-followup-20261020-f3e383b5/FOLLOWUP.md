# WP-14-T12 需求封闭补充核查（2026-10-20）

产品基线：`f3e383b58c2d`（本轮产品修复已推送origin/wp14-t12；完整SHA以Git解析为准）。本目录只追加验证与审计，不修改已冻结的requirements-close-20261019证据，不宣称需求模块全部完成。

## 新执行的验证

`mixed_probe.cpp`复用既有运行时模型构造、编辑器端口夹具与生产KinEvaluationExecutor，真实调用采样、FK、IK、计数和回投，不注入脚本求解器。VS2022 x64环境下运行，1项执行、1通过、0失败、0跳过；XML、per-exe JSON、构建及运行日志均同目录留痕。该探针为QCoreApplication模型测试，无需GUI平台插件。

- 真实单移动轴X∈[0,0.6] m；Grid位置100点，X格心0.05..0.95 m，Y格心0..0.45 m，Z=0 m。
- 实际位置结局：6 Reached、47 Unreachable、47 DataInsufficient；分母100，双口径样本共200。位置可达数可由独立几何条件Y=Z=0且X在轴区间内直接复核。
- 存在数据不足时downgraded=true；所有样本运行完成时incomplete=false。两者不得混作同一种“完成”。
- 同一冻结输入重复两次，逐样本坐标、状态与计数一致；换绑另一快照后结果清空，旧请求拒绝。

该结果补充OPEN-07/J-02的真实100点混合分母子项，但不关闭整行J-02：真实项目必验工况漏验阻断及正式证据还未验证。换绑测试也只证明执行器锚守卫，不能关闭J-01的真实跨项目隔离。没有新增窗口或正式三维全谱终验证据。

## 真实几何缺口必须单列

追加为OPEN-08，防止原七组汇总遗漏：runtime/src/WorkCellCompiler.cpp:191的attachResourceMeshGeometry仍为空实现，参数仅void化，不读取真实mesh。第9步挂基本盒/圆柱标记；ui/plugin/UiPlugin.cpp:4360的生产拾取调用view->pickFrame并反解ObjectId，需求当前法向语义为FramePlaneNormal。

因此，已通过的标记盒/Frame raycast与Frame局部Z正反法向，不能证明真实资源几何显示、命中曲面及所点表面法向。REQ-10的Frame引用拾取与任意三角面法向不是同一个契约，不能擅自把后者塞入FramePlaneNormal或改变旧token含义。应先按REQ-09/10、MDL-04/15和runtime资源契约完成精确变更设计，再由所有者审阅需要扩展的资源/几何纯值接口；真实加载具有非零局部变换的非对称mesh，核对世界顶点、真实视图命中对象及可支持的特征法向、取反、取消、过期拒绝和撤销。当前不将该项标成已修复。

## 剪贴板追加定位

`native-diagnostic.ps1`不调用Qt剪贴板、不修改安全设置，不终止外部进程。实际输出仍为Session2、WinSta0/Default、InputDesktop=Default可打开，但OpenClipboard=False，Win32Error=5。进程在job中；QueryInformationJobObject当前可查询job的BasicUIRestrictions=0，读写剪贴板限制位均未置位。

这排除了“该次查询返回的job UI限制位直接拒绝”的假设；不能据此排除所有祖先job或其他访问限制，也不能归因锁屏。系统访问尚未恢复，原两模式12项失败与正式宿主1项失败保留，禁止改为fake或跳过来宣布通过。

## 剩余封闭顺序

1. 原计划A-21公共设计仍待所有者答复：精确有向Box与区域独立TCP。未新增接口、未越过设计门实施。
2. OPEN-02实际工程策略生命周期/统一编辑/下游策略消费；OPEN-03轨迹、动力学及选型的真实供数装配，分别完成公开装配缝设计，不能include跨域私有头或只挂空面板。
3. OPEN-04完整有效条目计算预览与正式证据失效链；OPEN-05算法到真实窗口颜色/碰撞高亮；OPEN-07余下真实项目隔离、漏工况及全属性裁切；新增OPEN-08真实几何链。
4. 系统剪贴板访问恢复后原样复跑OPEN-06。全部问题与120项完整终验关闭之前，任务保持未完成，不申请合入redesign-main。

原账本与问题清单见同级requirements-close-20261019目录。主快照2256次执行的计数不回写；本目录另计1项通过。没有对未执行功能标记通过。
