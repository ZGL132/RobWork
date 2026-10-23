> 当前状态：所有者已撤销本补丁；EAA.cpp/EAATest.cpp恢复上游原状，0004及其专用许可通路撤销。以下实施/测量/批准为历史记录，不再有效授权。最新指令见withdrawal-owner-decision.json。TestApprovedPatches仅能在历史SHA重放，不是当前verify入口。

# REUSE-T10：已批准的上游 EAA 最小精度修正

所有者于2026-10-23在本会话明确批准GAP-EAA-01方案A；原文和精确范围见approval.json。
先生成0004完整patch并登记PATCHES.md，再仅添加上游测试，确认旧实现失败，最后应用源码补丁。
本包未替换产品旋转实现、未关闭任何债务；完整冻结包仍待独立验收，性能接受未完成。

## 设计与复用边界

| 文件/函数 | 保留与最小修正 | 权威/边界 |
| --- | --- | --- |
| EAA.cpp::angleAxis | 保留原主值、近零与pi分支；一般取角改为atan2(反对称部分长度/2, trace余弦) | 数值修正留在原上游EAA入口，不在产品新建log或小角核心 |
| EAA.cpp::EAA(v1,v2) | 同一双向量构造以cross长度与dot经atan2取角，精确平行/反平行仍沿原确定轴约定 | 修正近乎平行时dot舍入导致有效方向被丢弃；不增接口 |
| EAA.cpp::toRotation3D | 非零向量按实际theta归一化，不消费axis()原有1e-6阈值 | axis()公共旧行为保持，类布局/符号/模板实例化不变 |
| EAATest.cpp | 原用例完整保留，增4例独立单轴sin/cos及双向量/float检查 | 旧实现真实失败，原容差未改；RPY/Quaternion原测试原样运行 |
| validate-reuse.ps1 / reuse-contract.ps1 | 精确批准patch核对，覆盖上游src和gtest，不再无条件拒绝所有已批准变更 | 无registry不授予任何权限；不按目录/扩展名整体豁免 |

后续产品复用原RPY/EAA类型与入口；本包不增加facade、模型、State、缓存、依赖或Qt。
runtime继续是唯一模型owner；移动关节offset反例已定位，但尚未修复，不能由kinematics另补。
禁止用Quaternion::ln当小角替代：本包未批准Quaternion扩展，旧零角非有限候选仍明确排除。

## 门禁与批准证据

新增reuse/upstream-patches.json仅是精确补丁登记，绑定当前批准文档、批准原文、基线blob、当前源摘要、patch摘要。
机器用Git标准full-index diff逐字核对批准文件，不读取或执行patch内容；冻结Head读取同一Git树。
批准身份仍由独立审查核对，引用一段文本不能自动证明作者身份。
TestApprovedPatches.ps1加载真实validator定义和helper；隔离Git夹具覆盖篡改后重新摘要、过期source/patch/proposal、错基线/范围、空批准、缺登记、重复记录、未登记对象与冻结Head。
新增规则本身也必须独验，本地PASS不构成新的阶段门已验收。

## 运行命令与原件

在VS2022 x64环境配置 .cache/t08a → .cache/t08a-build。该诊断CMake从现成集成runtime_test工程沿用include/link，编译原上游测试文件并链接真实sdurw_math；未复制测试框架/main。
EIGEN_DONT_ALIGN_STATICALLY=1与上游RobWorkSetup.cmake一致；测试同时设/utf-8，防中文注释按GBK吞掉后续include。

- cmake --build .cache/t07-integrated --config Release --target sdurw_math
- .cache/t08a-build/Release/eaa_precision_test.exe --gtest_output=xml:.cache/t08a/green.xml
- cmake --build .cache/t07-integrated --config Release --target sdurws_ird_modeling_test sdurws_ird_modeling_contract_test sdurws_ird_runtime_test
- 分别以绝对路径运行modeling_test / modeling_contract_test / runtime_test，传--gtest_output与--ird_report
- pwsh / powershell -File 本目录TestApprovedPatches.ps1，PS5.1使用UTF-8 BOM脚本
- pwsh -File RobWork/scripts/industrialrobot/test-reuse-governance.ps1 -OutputDir .cache/t10-governance
- pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1
- git apply --reverse --check --ignore-space-change industrialrobot/patches/0004-eaa-small-angle-precision.patch（实际以仓库根完整相对路径运行）

## 四维结果与尚未完成项

准确性：旧版上游16跑，12通过、4预期失败；补丁版16通过、0失败/跳过。单轴解析oracle、原多轴/pi回归与float/近乎平行方向均保留。
原T08探针EAA回构6个输入均进入原1e-12容差，小角{1e-6,-2e-6,3e-6}误差由约1e-11降到约2e-17；整个探针仍exit1，因为它也包含未修改的Quaternion::ln候选失败，不能把它标整体验证通过。
产品modeling 474通过；其契约17通过；runtime 302跑=301通过+1原有子进程占位跳过，父子进程身份用例仍真实执行。完整产品旋转迁移未实施，不能以本结果代替迁移生产正向/断开证据。
架构：上游单点修正，不引入第二模型/状态/平台；公共API/ABI形状与axis()旧行为保持，仍需独验核查。
简洁性：在原EAA三个扩展点修正，产品零新增算法/包装；本包不假称删除了产品旧核，删除随后续迁移执行。
运行效果：无共享状态/新增缓存，固定输入确定执行，既有错误入口不变；7组各10万次、100预热，旧0.027129μs/次，新0.043428μs/次，checksum相同。
这是约1.60倍耗时的首次同机微基准，采样有波动且缺本操作应用预算，**性能接受未完成**；不得自判可接受或关闭。

真实中间失败保留：benchmark首次缺/utf-8，中文源文件按GBK解析导致EAA include不可达；补齐编译选项后重编成功。首条产品构建还误列不存在runtime_contract_test目标，前三个真实目标已成功；runtime契约在runtime_test中，未伪造单独目标。
文档门禁先分别因EAATest摘要过期、新patch未登记类型拒绝；新增精确批准通路后重跑。PS5.1首轮新脚本无BOM解析失败，补BOM后13项通过；没有把失败原件覆盖。
全库DeepEvidence、完整独验、性能接受、远程CI均未执行；不合入、不操作远程。

## 回滚

完整回滚本能力提交、0004登记、精确patch机读记录及匹配sdurw_math制品；不留运行时旧/新算法开关。
若后续产品迁移已依赖补丁，按提交依赖逆序整体回滚，不能只替DLL留下源/制品不一致。
原失败/批准/审查证据保留，不改inventory历史分类，closures为空。

最终实施验证：PS7完整治理152通过、0失败/跳过；精确patch回归PS7/PS5.1各13通过。validate-docs/validate-task/集成ird_gates均通过；DeepEvidence未跑。独立CONFIG包消费者从本工作树RobWork_DIR导出目标链接本工作树补丁制品，上游16项全部通过；未使用D盘旧math库冒充补丁验证。

## 后续所有者性能接受（2026-10-23）

所有者在看到0.027→0.043微秒的测量后明确回复“明确接受此项微基准开销”。
精确范围和原始测量绑定见performance-owner-acceptance.json。该接受仅限本EAA补丁，
不延伸到SVD或全业务链，也不覆盖独立验收/冻结检查。前文和implementation.json
保留当时“性能接受未完成”的历史事实，不覆盖原始测量。
