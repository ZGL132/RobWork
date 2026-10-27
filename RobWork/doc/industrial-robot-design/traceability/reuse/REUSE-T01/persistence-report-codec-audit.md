# REUSE-T01：项目持久化与报告编解码审计

本批补覆盖审查COV-08/09，审读project/src/Codec.cpp与reporting/src/ReportCodec.cpp全部定义。矩阵按函数、明确重载、成对字段编解码登记；精确首尾见batch14-definitions.json。不是整个project/reporting单元结项，也不以ObjectStore/Archive已有登记冒充codec已审。所有新关系pending，产品/上游/vcpkg未改。

## 项目格式与重复工具边界

Codec的JsonValue只承载String/Integer/Object/Array；JsonParser自持递归下降与游标、对象/数组/字符串/转义/代理对/整数词法和溢出。深度64，拒空白、重复键、原始非ASCII、bool/null、浮点/指数、前导零、截断与尾随内容；整数跨度为INT64_MIN至UINT64_MAX。dump另有手写UTF8解码、Unicode转义及JSON字符串输出。源码以“testkit不可用于产品、第三方未登记”解释自含JSON，不足以满足H1；禁止依赖testkit不等于获准重写parser。T05须按成熟typed parser/writer的既有或同vcpkg候选调查最小profile，不能把整个持久化格式宣告业务专属。

上游core StringUtil/IOUtil及common archive已检索。既有B8探测probe-r8保留PropertyTree公开DOM丢类型/空容器、接受重复键/BOM的反例：不能用公开ptree直接替代而把数字、字符串、空对象/数组混淆。本批没有重跑B8。内部parser或私有实现不作为稳定API；检索无同名不证明整个框架/插件无能力。安装的Boost JSON encoder适配头不等于Boost.JSON或nlohmann正式库供给，batch14-search.json分别核公开入口存在性。尚未批准新依赖或新解析核；若确需补同渠道依赖，先提交必要性/构建目标/许可/跨平台与profile差异的最小供给提案，不能用临时下载或第二渠道。

project的七类记录dump/parse、版本与字段校验属于持久化owner：静态身份、HEAD、修订清单、项目元数据、命令、草稿、run清单分别有固定字段序/可选省略/字段范围；身份仍调用core toCanonical/tryFromCanonical，contentVersionOf实际委托core ContentDigester。命令payload是hex不透明字节，草稿payload透传，不由project解释域算法。schemaVersion未来/旧主版本拒绝，同主新minor按tolerate容忍未知追加字段；RevisionManifest/CommandRecord/RunManifest无同样preamble，不能泛化成七类都走版本判定。parseRefPair及嵌套unknown策略须保留调用处传参，不能静默放宽。StoreError携机器detail，用户文案由diagnostics，不再建错误平台。

appendHexString/decodeHexString另有自持hex。既有Boost.Algorithm提供hex_lower/unhex；已读公开定义，unhex接受大小写且抛non_hex_input/not_enough_input，产品decode仅收小写。可薄保留小写/偶数长度profile和错误映射后用成熟原语，不复制其实现，不为两个短函数建立通用编码框架。若选择共享输出，须落在已有core/IO职责和真实消费需求内，不把project私有头暴露给其他owner。

静态待核：parse接受斜杠转义与大小写Unicode、字段顺序未校验，因此输出canonical不等于输入仅一种字节形态；应保持现有接受/拒绝契约，不能未经需求核查扩大“严格canonical”。integerInRange对大于INT64_MAX的正数转int64再由requireUint64Field转回存在实现相关边界。本批未实跑，不称已复现缺陷。UTF8与JSON基础能力不能在project、io、testkit继续各扩一套；跨层依赖仍按ARCH，不能直接跨单元私有头串接。

## 报告固定字节与权威边界

Writer/Reader手写u16/u32/u64大端，f64经memcpy搬位模式再委托u64并拒NaN/Inf；bool/presence只收0/1，字符串u32长度、拒NUL，单位symbol写入及core::UnitToken::find读回。BINArchive确实存在，但标量写宿主表示、字符串长度stream文本、版本与budget不等价。不能把IRD报告冻结字节整体换成archive，也不能再写一套大端工具。既有Boost.Endian公开load/store_big_u16/u32/u64可作为现成低层候选；与B13通道的little版同样先用已有供给，不增加后端选择平台。外部头只作为安装候选证据，不伪装成RobWork文件写入catalog。

逐项审读header/time/enum/optional身份/TaskIdentity及来源、四态数量、比较字段、失效原因、基准结果、字段、结果/证据引用、跳转、章节条目、缺项、资格说明、诊断投影、当前性、章节、评论、签署、权衡、变体差异、版本史、评审元数据、结果来源与单位偏好的成对编解码。它们仅搬运既有core/evidence/diagnostics值，不生成envelope、资格、当前性真值、稳定码或文案。不得因“只做codec”复制这些权威。Reader借持输入，Writer持私有vector；不持WorkCell、State或跨worker宿主句柄，不扩大上游对象权限。

encodeFull真实嵌入encodeData返回字节；parseFull却复制parseData字段读取主体。T05最小方案是在当前TU提取已有readDataBody供两入口消费，保留各自header/顶层expectEnd，不增加反射、schema registry或跨层serializer。digestData/digestFull最终经core唯一摘要。digestFull仅对身份副本清章节Currentness.computedAtUtc，encode/parse保留原记录时间；F-815的身份排除规则不能被“统一codec”破坏。

静态待核：Reader各读取有need，但向量计数后reserve(n)并不等于先按剩余字节限制分配；Writer字符串/向量size转u32缺显式超长拒绝。字符串代码仅查NUL，不能据注释声称完整UTF8已验证。readTime注释称MSVC system_clock同纳秒精度，本批未验证，不承诺任意外来纳秒无损。以上均未实跑或分配极大内存，不登记为已复现缺陷，不顺手修改。

## 接线、测试与迁移验收

batch14-search.json保存真实消费者：project的ProjectStoreImpl/TxEngine/DraftServiceImpl/ArchiveServiceImpl/StoreClone/QueryPortImpl等调用codec；reporting的ReportModel/Builder消费ReportCodec。测试定位project/test/PersistenceFormatTest.cpp、reporting/test/ReportModelTest.cpp和test/ReportingContractSuiteTest.cpp，已绑定原CMake目标与测试摘要；RoundtripTest.cpp是报告复算/回读上层测试，不冒充所有codec直接测试。未运行这些产品测试，不把静态grep或绑定当作生产正向证据。

后续T05按功能迁移：先保住原固定字节/身份、拒绝profile、版本策略、截断/边界测试与独立黄金oracle，再最小替换编码原语/成熟JSON工具并去掉旧路径；不用重产旧黄金值掩盖差异。必须补真实生产入口、返回值消费、扰动变红和独立oracle证据。与运动学适配/动力学提案不同的职责不能混为同一批准。当前仅文档与登记验证，本批C++/GUI/性能NOT RUN，不关闭库存，不授T01 PASS或T02准入。
