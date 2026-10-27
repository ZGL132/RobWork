# B33：诊断受限模式解析与去重辅助

本批审读私有ParamSchema.hpp/.cpp、EntryDetail.hpp三项inline、DiagCodes::validateParamSchema与注册期调用、Factory::create的模式校验调用。原registry/factory38例直接编译原文件，沿真实diagnostics库及原TestMainReport/testkit；不复制诊断服务，不越过稳定码/文案权威。该范围不代表Logging/Redaction/Catalog全部已审计。

## 复用来源与边界

先检索并阅读UTIL-TEXT的rw::core::DOMParser默认工厂和StringUtil源码、相关测试/示例/共享文档/CMake。默认DOM走XML，工厂允许插件扩展；范围内未见产品受限参数数组服务，不能推断生态无对应插件。StringUtil大小写工具不自动保证固定ASCII码键、既有依赖仍仅core，不能为一次lower引入新库。codeLower/taskScopeId/deriveDedupKey分别只投影冻结文案键和业务五元组/对象身份，复用原core规范串，不引入新哈希/身份系统。

进一步复用已安装Boost.PropertyTree 1.92公开read_json实测：["1"]和[1]的ptree相同，不能保留产品要求的字符串类型；\u0070id被解码为pid，JSON换行空白也被接受，而当前受限文法拒绝二者。仅做树上参数名检查会放宽已冻结语法。要采用通用解析器仍需保留词法约束和类型证据，可能比现有窄解析更繁；不据此批准第二JSON框架或新核心。catalog维持既有UTIL-TEXT candidate，diagnostics没有上游构造/依赖授权；矩阵NONE表示当前业务权威/候选不适配，全部仍pending，不是已核准business-specific豁免。

## F-968：失败输出契约不一致

ParamSchema.hpp承诺失败时names清空。实际输入["pid","PID"]、["pid","pid"]、["pid",]、["pid"]x均返回false且error非空，但留下已解析pid，四组断言真实失败。未使用空指针或未定义行为。注册期validateParamSchema和Factory::create都检查false并停止，原38例通过；尚未证明部分names进入公共诊断对象，因此缺陷限定私有辅助的失败输出纪律，不夸大为生产参数放行。

后续最小处置应先核这两个既有调用方和冻结契约，保持单一私有解析点及拒绝语义；修正输出提交或收窄错误轨必须独立回归，不在T01重写解析算法。参数词形依赖cctype的locale行为未作跨locale探测，保留未决；不把C locale实测外推到所有宿主环境。

## 实测

VS x64审计配置/构建成功，绝对exe一次运行43跑42passed1failed0skipped；原38例全过，新5例4passed1failed。去重测试实证对象不同和任务attempt不同不会合并、Task scope优先；该最小例不代替全部目录聚合/原因链验收。prefix供给沿B30/B31/B32的既有同渠道安装，不冒充产品集成构建，不安装依赖。F968保持OPEN，未作产品/上游/vcpkg修改，未执行远程同步，不授全T01/T02。
