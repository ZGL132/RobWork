# REUSE-T01 B31 独立局部验收

结论：**FAIL**，仅阻断本批检索证据完整性，不否定已复现的数值反例。冻结 `933dc6016c078b7d674fea68af19c038e0388979`，基点 `0900751718849863af1190ece61c8ddb1f1cf547`。在本目录的独立本地 clone2 中 detached checkout；未联网、未安装依赖、未代修共享追踪文件。

**B31-P2-01：真实上游文档未纳入提交的检索范围。** `batch31/search.json:31` 指向不存在的 `RobWork/RobWork/doc`，该命令 exit=2。第3条重搜（路径列表见54行附近）只删除坏路径，未改成真实 `RobWork/doc`；两条资源搜索的 glob 也未含 `*.md`。audit-contract §2 明确要求公共头、实现、测试、示例、文档及 CMake 的定向检索，因此当前记录不能证明文档部分完成。catalog:2181 原文列的是公共头/实现/测试/示例/构建，并未明说“文档”，本项不将其误引为显式文档覆盖承诺。需要补真实文档路径及 `.md/.rst` 检索、保留原 exit2 历史，并更新受影响证据绑定后冻结复验；不能把删除坏路径等同已校正文档范围。

独立补搜证据已保留 `upstream-doc-search.log`：真实 `RobWork/doc/sphinx/tutorials/basic.rst:173` 使用 Deg2Rad，205行声明 radians 而非 degrees；`manual/rotations.rst:91–92` 说明旋转轴/角度含义。命令范围为真实 manual、sphinx 等目录，包含 `.md/.rst`。这些证据说明坏路径对应的上游文档确实存在，不要求据此扩大生产依赖或新增能力。

其余已通过的本批核查：改动29路径，产品源码/上游源码/vcpkg 相对基点零变更；矩阵新增43行、刷新8行，51个来源区段的符号/规范化摘要及所有 inputs/auditEvidence 均匹配，全部 pending。阅读 Units 的隐藏 helper、21-token/14量纲表、Quantity 模板/14显式实例及 Compare/Q/原测试，未将整文件标 closed。UTIL-UNITS 保留 loader 候选并补真实 DOMCoreBasisTypes::getUnit/core目标；绑定源码、CMake目标与外部供给的对应源码一致，相关上游文件为 Apache-2.0。core 原权限仍仅 std/math；审计目标额外链接 core 并不授权产品依赖 DOM。

VS 2022 x64 环境真实配置及构建 exit0，按绝对路径只运行 `ird_units_audit.exe` 一次，无 GUI/Qt 平台需求。目标直接编译原 core、testkit、UnitsTest、CompareTest 与报告器，审计 CMake 显式将 RW::sdurw_math 公开供给原 core，新增测试的 #error 防止缺 Q 时缩减验证面。实际 Release 编译来源/链接依赖及12项已有绝对库文件的 rawSHA256 保存于 `semantic-results.json`；没有修改产品 CMake。

测试：22跑，**19 passed / 3 failed / 0 skipped / 0 notRun**，exit1。原 Units七例及 Compare八例全通过；新增七例4通过3失败，失败恰为 scalar/vector/Q 的合法有限极值 C4 反例。value=DBL_MAX、reference=-DBL_MAX、relative=1.5、absolute=0 均合法；diff 与 limit 同溢出为 Inf 使当前三个入口误返 true。独立 `independent-oracle.py` 用 Fraction 精确证明差值/DBL_MAX=2、阈值/DBL_MAX=3/2，应 false；implementation oracle 另行重跑同结论。Math 常量/DOMcore比例和词表差异、isNaN不拒Inf、Q精确等值与C4差异均真实通过。次正规 mm→mm 得0只记录各一次乘除的本平台观察，不是正确性或已批准修正证明。

门禁：冻结版本脚本真实运行 task/docs/reuse(-Head冻结)/scope29/commit-lint 均 exit0；reuse 仅 governance pass（functions=1011、closedFiles=0、UNRESOLVED=1399），不是算法合规。默认 git diff --check exit2 是原件 CRLF 被视为行尾空白，显式 cr-at-eol 后 exit0，未清洗原日志。findings 的1288469字节历史前缀 rawSHA256=`31b5b222d04a1c71b657571259c2d6585fad1d9d54eeafcb2bddb5d97830065c` 与基点逐字节一致，只追加 F-966（open）。原 artifact-manifest 的23项全部 Git blob 和共享提交现场 raw 匹配。新 clone 继承系统 core.autocrlf=true 导致10项 checkout文本转CRLF，已与原件/提交blob区分，不把该现场转换误判为提交原件损坏。

未执行完整上游套件、全部消费者阈值溯源、跨平台/极长序列、GUI、完整产品集成或算法修正；不授全T01/T02准入。F-966仍 open。起步曾误写共享 `.cache/b31-accept-location.txt` 定位文件，已只清理该自有文件；验收结束独立 clone 的 git status 为空，未改共享追踪文件。交付清单仅含报告、日志、结果与脚本，排除 clone/.git/build/fixture，清单自身不入其摘要清单。
