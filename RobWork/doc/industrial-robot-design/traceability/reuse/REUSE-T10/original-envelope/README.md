# EAA恢复与统一1e-6表示转换阈值

所有者先要求撤销补丁，随后明确“可以改为1e-6，不要因为这个卡住，加快进度”。
本目录是该最新决议证据；前面的补丁批准/独验FAIL均保留历史，不再有效授权。

- EAA.cpp、EAATest.cpp与原上游基线blob完全相同，见restoration.json。
- 0004有效补丁和仅为它增加的CheckApprovedUpstreamPatches许可代码已撤销；上游src/gtest均严格只读。
- REQUIREMENTS附录D第13项统一旋转表示转换的最大元素绝对误差1e-6；不批量替换其他容差。
- Envelope.cpp仅在测试中使用独立sin/cos解析参考；不接入生产，不形成产品算法或fallback。
- 每入口3156个固定输入：矩阵→EAA最大误差1.0734242583231835e-9；EAA→矩阵9.9999899999983327e-7，往返9.4406087628578315e-7。2项通过、0失败/跳过。
- 原版上游EAA/RPY/Quaternion：12通过、0失败/跳过。
- 原库制品业务回归：modeling474通过、契约17通过；runtime301通过+1既有worker占位跳过，0失败。
- 治理正负夹具152通过、0失败/跳过。恢复构建及原始XML/JSON/log留存。

复现：使用本目录CMakeLists/Envelope.cpp，VS2022 x64 Release，RobWork_DIR指本工作树RobWork/RobWork/cmake，GTest经原vcpkg供给；EIGEN_DONT_ALIGN_STATICALLY=1与上游一致。实际运行构建树.cache/eaa-original-envelope-build；诊断日志确认载入本工作树重建的math DLL，摘要见restoration.json。

这完成上游恢复和容差决议，不宣称产品旋转已替换，也不关闭任何迁移债务。尚需完整适配包核查矩阵合法性/Custom预设/实际FK语义，不能把表示阈值用于放过实体模型偏差。
未触碰drivetrain/trajectory专属代码和功能矩阵；无远程操作。文档/当前复用门禁在本次提交后执行并另存日志。完整冻结深查未完成，不冒称PASS。
