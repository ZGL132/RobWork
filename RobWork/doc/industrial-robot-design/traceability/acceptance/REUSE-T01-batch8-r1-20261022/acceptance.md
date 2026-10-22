# REUSE-T01 第八批只读独立验收

**FAIL：1项P2，B8-P2-01。** 冻结 `bb997f1cfb89c8d8eb24b457d2c82d4c0711af44`，比较基点 `7af7e86a18e34384677c2599a4f8812ad262908f`（已核为祖先）。结论只覆盖第八批20条JSON/Unicode/错误映射登记、UTIL-TEXT catalog及io/testkit文档补正，不判全REUSE-T01通过。

## B8-P2-01：readHex4区段绑定调用分支，遗漏真正定义

`RobWork/doc/industrial-robot-design/reuse/functions/io.json:306-318` 的 `io.Json.Parser.readHex4` 将 `sources.startLine/endLine` 登记为 `io/src/Json.cpp:644-649`。此段是 **Parser::parseString 内部的 `if (!readHex4(...))` 调用及失败返回分支**，不是所声明 qualifiedSymbol `sdurws::ird::io::(anonymous namespace)::Parser::readHex4` 的定义。真正定义从723行签名开始，到747行闭括号，包括4位循环、截断/非十六进制拒绝和结果写入。

当前摘要与错误区段一致，因此机器hash检查通过仍不足以成立语义绑定。应将本行改为真正定义723-747并重新计算规范化SHA，保留pending。实际定义原文和正确区段SHA分别落在 readHex4-actual-definition.txt/readHex4-expected-sha.txt。该调用区段已包含于 parseString:604-720 登记，不能用它替代readHex4函数审计。

## 本轮其余核查

- 新增20条，io/testkit各10；20条decision/status全pending。累计220关系，其中217 pending、3 proposal，closedFiles=0。未发现将本批登记称为全量闭合、批准保留手写解析核心或扩大owner权限。
- 所有20条登记区段摘要和全部inputs摘要匹配；逐条人工读取起止签名、完整函数体和作用域，除B8-P2-01外其余19条均是完整定义，没有将调用后的if/switch大括号当成函数体。逐项结论见 semantic-boundaries.json，原文见 source-excerpts.txt。纯hash结论为0 mismatch，不消除上述语义FAIL。
- io 的 Parser/DecodedStream 位于 `sdurws::ird::io`匿名命名空间；testkit Parser及dump函数位于 `sdurws::ird::testkit`匿名命名空间。双mapSystemError在各自独立翻译单元，qualifiedSymbol属同名私有映射，不将std::errc比较误认新底层错误核心。注册目标真实：io/CMakeLists.txt:98-104的sdurws_ird_io包含Csv.cpp/Json.cpp；testkit/CMakeLists.txt:46-48的sdurws_ird_testkit包含JsonLite.cpp。
- 需求字段使用确切契约IO-T04、IO-T03、TK-T02。真实契约分别覆盖JSON解析/重复键/预算/定位、CSV编码/BOM/逐行错误、受限测试JSON往返拒绝与行列路径；designRefs所列io §5.9.1、CSV §5与testkit §4.1/任务可支撑当前功能。未发现本批明显需求语义错绑。
- JSON object/array/string/number的SourceSpan、重复键、字符串预算、int64超范围原文透传、数值拒绝逻辑已读；CSV UTF8逐组放行/残缺=-2、UTF16代理对合成/BOM序保留已读；testkit独立值模型、转义、parseUnicodeEscape/parseHex4/utf8Encode、strtod与dumpNumber的to_chars/插入键序均实核。本批只登记既有实现与差异，不对既有实现全部正确性背书，也未批准重写核心。
- UTIL-TEXT三个上游资源与目标CMake摘要匹配，真实目标为sdurw_core。DOMParser::make明确BoostXMLParser；Factory还会查插件扩展，因此“默认XML”成立，不可推成整个插件生态仅有XML。StringUtil公共头是大小写、quote等字符串工具，未提供本登记所需严格UTF8/代理区/超长编码契约。
- 资源检索的命令只覆盖列明RobWork/RobWorkSim源码与gtest、hpp/cpp/CMake文件及具体小写模式；exit1空命中只限该范围。catalog和文档均保留此限制，没有由此宣称全生态无JSON/Unicode。
- 同渠道Boost.PropertyTree/Endian的环境记录11个依赖原始摘要、探测2个source摘要及本批5个auditEvidence摘要全部匹配（共18项，evidence-bindings.json）。探测CMake确实find_package boost_endian/boost_property_tree并链接Boost::endian/Boost::property_tree；列明vcpkg ports中有成熟JSON/Unicode候选，installed/share未见本次列明的独立JSON/Unicode包。ports可用不等于已安装，安装内容摘要也不等于可复现来源，环境记录已保留该界限。
- 已阅读DependencySuitabilityTest：公开ptree把数值/布尔/null及字符串混同、空数组/对象/空串混同，reader可接受重复键/BOM；detail回调仅源码检索，不被声称稳定公共API。两份文档移除了“test-only/未安装包即自写合理”的旧豁免，要求继续同渠道成熟API审查、薄profile适配、禁止新DOM平台，且不将testkit与io产品库反向互链。此处是待迁移限制登记，不是排除所有候选或准许新核心。

## 真实验证与限制

| 验证 | passed | skipped | failed | 证据 |
|---|---:|---:|---:|---|
| task/docs/reuse | 3 | 0 | 0 | task.log/docs.log/reuse.log，独立exit.txt均0 |
| batch8检索重放 | 8 | 0 | 0 | search-results.json及search-1..8.stdout/stderr.txt |
| source/input摘要 | 全部匹配 | 0 | 0 | bindings.json/check.log |
| catalog资源/目标摘要 | 全部匹配 | 0 | 0 | catalog-bindings.json |
| audit/依赖/probe源摘要 | 18 | 0 | 0 | evidence-bindings.json |

重放8命令的退出码与冻结记录一致（第一条rg exit1，其余exit0），stdout经CRLF/路径分隔归一并逐行排序后集合一致，stderr均一致为空。相关脚本在本TEMP根。规范区段摘要为UTF8去BOM、CRLF→LF、保留一个末尾LF。

NOT RUN：C++/公开API实际构建测试（按独立验收分工由另一验收者负责，本代理只核静态证据，不把既有3项PASS称自己运行）；GUI；全库DeepEvidence；其他批次重新验收；全T01退出。治理脚本PASS仅证明机器治理检查通过，未覆盖B8-P2-01定义语义。

本目录repo为独立shared clone、detached冻结HEAD，clone/checkout exit0，前后status干净。未改父实施树、上游、vcpkg，未提交推送合入，未派子代理。报告与日志仅写本TEMP根；不得递归归档repo。
