# MDL-CR-2 DH无损与切换门（实施待独立验收）

C:指定工作树/codex/core-review-fixes；起点b80043238df28bb83617f985824876c5da37a162。Owner批准F01/F03及F14前置，允许将旧表示/参数逐项回收断言改为独立Fixed末段与真实runtime多构型物理等价。黄金原件未修改，既有1e-9 m/rad阈值未放宽。

保留DH权威、JointEntry来源及冻结LM/数值差分/雅可比/消元骨架。删除末段烘入origin、目标轴硬编码Z和modeling零位FK。适配runtime唯一模型owner：末段flangeOffset进入Description/Canonical/codec/身份，由正式编译器挂FixedFrame；runtime公开纯值采样口，内部临时持有WorkCell/State、调用Device::setQ/Kinematics::worldTframe。边界/各轴边界/多组非零内点比较全链帧，有限采样不宣称全连续空间证明。RNE特殊载体同步转换COM和张量坐标，canonical物性仍关于实际COM。正式UiPlugin向模块注入既有HostCompileProbe，UI切换与prepare共用同一接受门，缺服务/验证失败保草稿。DH/Xacro结果迁入dh/xacro命名空间，删过期共头禁令。

公共影响：RobotDesign/Description/RobotChain新增默认恒等flangeOffset；modeling编码1.2兼容读1.0/1.1，canonical缓存精确协议1.4，编译实现1.2拒旧缓存。DH求解器对部分可表达复杂链可能Approximate或被门拒绝，这是C03冻结下的保守可用性限制，不保证任意可表达链均能切换。回滚本批全部载体/codec/消费者/接线，后续依赖批先逆序回滚。

|实际验证|PASS|FAIL|SKIP|
|---|---:|---:|---:|
|modeling unit|485|0|0|
|modeling contract|17|0|0|
|Windows modeling GUI（VS x64、windows平台、单exe）|124|0|0|
|runtime受影响Codec/Compiler/WorkCell套件|85|0|0|
|逆动力学DynRnea（含独立末段重力臂解析钉）|25|0|0|
|正动力学DynForward|13|0|0|
|独立冒烟DH/codec（同一正式runtime实现）|15|0|0|
|14项评审探针（本批3项＋前批3项通过）|6|8|0|

头共存cl语法编译PASS（replay-header.log）。正式sdurws_ird_ui_plugin构建成功。真实DH oracle独立矩阵覆盖1/2/6/7链长、非零a/alpha/theta、多非零q、正负offset、非平凡base/tool；F03非Z链拒绝且原草稿保持，平行Z正路径允许并再以真实runtime非零FK比较。测试报告与命令/源码/rsp随目录保留；GUI系模块生产Widgets路径，并不冒称已执行完整RobWorkStudio交互巡游，宿主接线已实际编译。

首次runtime全组：306 PASS/3 FAIL/1 SKIP；其中两条协议/实现版本断言随本批有意升版后已回归通过。未改动的LoaderRestoresThreadLocale_REUSE_T13在本机中文路径转换抛异常，保留runtime.log/XML，不计为通过、不扩面修复；ChildProcessWorker按既有worker模式SKIP。后续受影响套件85项不含该无关环境失败。首次冒烟配置碰到上游C:\local/vcpkg宏转义，使用现场既有VCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package配置成功；不修改框架/vcpkg。

复用矩阵仅同步变动区段/新端口事实，pending求解核不虚关债务；全局历史凭证阻断沿batch1记录，不用实施测试冒充独验。未改分支/工作树/远程/框架，未自签独立验收。

本批机器门禁：ird_gates静态R/T/SUB/GRAPH/LIB/SA02通过；附带复用与文档检查未通过。先检出本批WorkCellCompiler权限凭证摘要需同步，已仅刷新既有source/build摘要，未扩类别/操作/owner；当前docs-final.log记录后续实际阻断。完整历史证明未重写、未用新摘要冒充迁移独验。

最终复用检查阻断原文：REUSE-FRESHNESS : RobWork/RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp。旧REUSE凭证/生产证据未重写。原始日志尾空格原样保留，生产diff无空白错误。
