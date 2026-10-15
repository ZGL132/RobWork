# QP-07.01 优化真实接线核查

基线 `01223053a130c7b437c6a32f439bdd1c58ba8d54`；静态调用链＋正式宿主装配日志，未执行真实候选搜索。QP-07.01 仅完成部分核查，未勾整项。

已有 WP-20-T02～T11 done；WP-21-T01～T04/T06/T07 done；T05/T08 planned，受 P-04 正式判定阈值前置。内核、预检、Pareto、并行、暂停能力继续按已交付子集复核，不将全部 OPT-D 判为未实现。

宿主 ui/plugin/ExtraDomainAssembly.cpp 创建 optimization assembly，只绑定 textResolver 并 registerWithHostRegistrar。仓内定向搜索未找到该宿主随后调用 bindRunStart/bindRunCancel/setServices 的位置。optimization/plugin/OptimizationPluginAssembly.cpp 提供这些 API；OptPanelModel.cpp requestRunStart 在 services.runStart 为空时返回 not-assembled。注册成功/面板可见并不能满足真实开始前置。

正式宿主日志确认 optimization registered=1、panel=1、commands=0；commands=0 是隔离域描述符的设计口径，不能单独当缺陷。缺口依据是 run 服务未绑定。当前尚未建立真实运行→候选→比较→采用闭环证据。

后继先对照 SA-16 与既有宿主接线契约，明确 ui 命令所有权与评估端口；覆盖变量授权/锁定、20 项预检、候选补丁编译、基线、取消和项目切换，再用小空间穷举复核真实候选。不得用人工构造候选或替身端口测试替代宿主执行。
