# B17：容差、集合匹配模板与黄金数据装载承重逻辑

本批逐定义审读core Compare.cpp/Compare.hpp、testkit SetCheck.hpp与Dataset.cpp/Dataset.hpp，包含inline/template及样板值比较/访问器。精确区段见batch17-definitions.json，实际决策仍在functions/core.json、testkit.json；头内仅DTO声明不是算法，不能因此豁免真正的工厂、比较、匹配与load。

## core比较权威

closeWithin实现需求附录D C4的reference定向absolute+relative，任一原始输入非有限拒绝；vector/Q逐元素且形状错误抛CoreError，Q仅在可包含rw头时编译。Tolerance::make校验非负有限，但聚合公开字段仍可直接构造；C7默认由core按量纲单点转写、未声明量纲nullopt。上游Transform3D::equal只有绝对precision且只承载变换，Q的operator==是精确相等，不能替代该通用业务公式。继续优先使用标准数值原语并保留最薄C4契约，不为每个量纲新建比较器或改变reference含义。

已用原CMake的sdurws_ird_core编译并真实调用：v=M、ref=-M、relative=0.5、absolute=M（M为最大有限double），数学差2M>阈值1.5M，应false。原实现diff和limit同时变inf，两个<=均true，gtest反例真实FAIL（1 failed、0 passed/skipped/notRun），见probe-r17/core-red。独立缩放oracle只比较2与1.5，不依赖MSVC long double精度。此为产品缺陷OPEN，T05需在原owner保持C4语义并修数值边界，不能放宽容差、删反例或重产黄金洗绿。

## 集合最大匹配

SetCheck不是排序后逐项比较：先expected身份重复拒绝/actual重复失败，再身份匹配与数值对照，剩余元素构相容边调用自持Kuhn增广；相容性真实复用ToleranceProfile::resolve与core::closeWithin。含固定100000元素护栏、mismatchedPairs、missing/extra与见证输出。F-594后的歧义实现是交错圈左闭包，不能照抄文件头旧“移除后增广”说明；稳定顺序是另一个逐位契约，不能以集合相等代替。

上游RobWork/Sim及测试的matching/augment命中多为任务augmentation或名字相似，未找到已读公共二分图最大匹配API，但零命中不证明所有候选无能力。同vcpkg已安装Boost1.92公开max_cardinality_matching.hpp提供checked_edmonds_maximum_cardinality_matching、matching_size及验证器；本批沿原审计目标/GTest/testkit追加Boost::headers消费，未新增生产依赖或渠道。两个真实公开API探测passed（failed/skipped/notRun=0）：贪心陷阱得到唯一完美匹配；删已配边后，完整2×2图仍可完美而唯一配对图变小。见probe-r17/candidates，非生产正向证据。

候选仅解决最大匹配核心；身份优先、C4边谓词、错误详情/稳定见证/歧义与性能仍须适配。Boost接收显式图，当前实现按谓词按需求边，不能未经测量在100000护栏内展开稠密图；不承诺内存/复杂度收益。也不能只因Boost存在就替换整个testkit或引入通用图平台。

静态待核：重复键三次时duplicates可能多次列同键；注释O(n)与线性查找/双循环不符；身份失败后元素仍进入第二阶段，但r.equivalent已false；matched不是简单全局expected索引升序；长度不同时firstDivergence与更早数值错位的优先级需对契约验证。未实跑这些场景或性能，不把静态疑点报成已复现故障。

## 黄金装载、版本和路径

Dataset实际先从goldenDataRoot/数据集/版本读取manifest，再以JsonLite解析，校schema/id/版本/kind/单位/frames/来源独立性/producer/edgeCases/完整性/history，真实复用core UnitToken和ContentDigester、标准ifstream/filesystem/容器算法。自持小写hex输出可继续评估既有Boost.Algorithm hex_lower，不能另建摘要平台。单元协议与路径授权归testkit，RobWork通用文件/归档工具不能替代产品manifest schema，JSON底层profile仍承接此前成熟parser候选调查。

必须区别注释与实际执行：load逐一要求inputs/expected在integrity中，逐清单读字节核size和SHA；代码没有额外保证generatorCommitted为true时generatorScript也在integrity，尽管注释如此写。parseSemanticVersion用std::stoi，溢出可能泄露std::out_of_range；producer.seed/sizeBytes/threadCount从double转换，整数/范围检查是否足够待专项验证。路径相对字段与ref.version在filesystem join前的边界需核，不将清单membership等同真实规范化沙箱。以上仅静态待核，本批未跑实际Dataset/load或SetCheck产品测试，未判它们缺陷已复现。accessor和逐字段==/!=只作明确值转发成组定位，不新增封装。

本批只新增审计探测和治理登记，零产品/上游/vcpkg变更。新3项实跑2 passed+1真实反例failed；旧30项NOT RUN，不称全探测套件或产品构建通过。权限仍按ARCH §3.6 owner/adapter，未添grant或新核心批准；待冻结独立验收，全T01尚未退出。
