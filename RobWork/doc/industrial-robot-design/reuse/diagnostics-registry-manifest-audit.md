# B34：诊断注册表、码词法与清单布局

审读DiagCodes.cpp剩余辅助/词表/内置规格/查询/seal/manifest/deprecate/装配、Errors.cpp及公共inline。B33的注册期模式链继续有效；本批新增区段不代表整诊断目录已关闭。所有行pending，不改产品。

## 复用与重复点

core/src/DiagData.cpp::codeSyntaxOk与diagnostics::isValidDiagCodeSyntax实现相同的冻结ASCII码词法；新增测试经公开DiagnosticRecord::make与registry判定，对空串、64/65字符和全部单/双字节ASCII共16515输入观察一致。不是两种业务语义，可在后续按core承载句法、diagnostics拥有合法码及文案的边界，最小暴露并复用既有core判定；禁止跨单元包含私有头、第三份正则实现或把已注册权威移到core。当前测例不宣称任意长输入等价的形式证明。

DiagCodes::derivedTextKey与同单元EntryDetail.hpp::codeLower重复码小写派生；后者固定ASCII、不依赖locale，优先薄复用已有函数再拼suffix，不建新文本服务。上游StringUtil::toLower源码用tolower，不自动保证全部宿主locale下冻结ASCII键形；不为一个小写投影打破diagnostics仅core依赖。注册表的map查询/装配/前缀归属/稳定码及异常token属于既有标准容器和产品词表，不另造注册框架。

feedU32le/长度前缀/布尔只是IRDDCM2布局编码：上游BINArchive的归档协议和bool转int与此字节契约不同，不能直接替换清单格式。已有成熟endian候选见前批UTIL-BINARCHIVE及同渠道Boost.Endian；后续若集中整数原语，先适配既有core身份/摘要边界和允许依赖，不新增通用codec facade或让diagnostics反向链接evidence。ContentDigester已经直接复用core唯一入口；GAP-DIGEST未批准，不能在这里加入第二SHA实现。

## 独立字节证据

oracle.py仅用标准struct.pack('<I')与hashlib，对空magic、固定RT-AUDIT描述符、tombstone到RT-NEXT三组布局分别冻结7/135/146字节及SHA。版本0x01020304覆盖四字节序，C++只调用真实registry和ContentIdentity解析，未重新实现编码器。三个摘要一致；原稳定顺序/内置88码/词表/级别与注册错误例继续直接编译，seal后的查询与拒写追加探测通过。该证据不覆盖超4GiB字段长度、所有宿主locale、并发装配、全进程握手或最终产品包。

## 实际执行范围

组合目标沿B33真实库/原测试，只追加本批四例。VS x64顶层cmake --build最初无法寻址EXCLUDE_FROM_ALL子目录目标，保留失败；随后直接MSBuild生成的schema-probe/ird_diagnostic_schema_audit.vcxproj成功，没改产品CMake。绝对exe以CodeManifestAudit/DiagCodes/DiagnosticsError过滤，23跑23passed0failed0skipped，其中原19+新4；组合47项其余24 NOT RUN（含B33既有F968反例），不得用本批绿覆盖原F968或声称47项全过。沿原TestMainReport/testkit，不建第二报告器。F965/F966/F967/F968不因本批通过关闭；没有新增产品缺陷或新核心批准。
