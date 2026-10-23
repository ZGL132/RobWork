# GAP-DIGEST-01：以成熟摘要实现替代自持SHA-256的候选提案

状态：**未批准**。仅审计和候选适配方案，不授权安装依赖、产品替换、新核心或上游修改。

- 缺口证据：B30 search.json含RW/RWSim源码、公共头、测试、示例、文档及构建目录检索；未找到SHA256服务。现有Digest.cpp自持64轮/调度/填充；原七例和新增四例均通过，问题是复用债务，不能声称已证实SHA数值缺陷。F-965为另一个FNV缺陷，两个公开摘要类型的hash反例在B30真实失败。
- 候选与替代：先核同渠道vcpkg boost-hash2（本地port 1.92.0，BSL-1.0，固定来源摘要）；再比较openssl（本地port，许可证以其vcpkg.json为准）及是否已有实际共享消费者，避免引入庞大依赖。二者仅见port，未核安装/API/跨平台，不编造函数签名。Windows SDK BCryptHash/CreateHash/HashData/DuplicateHash/FinishHash已在B30实测；受Windows平台及现有依赖约束，仅为参照，不选作唯一产品后端。保留自持算法不是默认批准的替代。
- 最小扩展点：不需要先改RobWork。批准后只在core/src/Digest.cpp和Digest.hpp私有状态适配成熟实现；core/CMakeLists.txt经既有vcpkg配置目标链接；core/test/DigestTest.cpp补边界/复制/错误翻译。现有core依赖限定及units/core.md §3.2、D-05实现理由须同步修订，不能先改代码再补许可。
- 适配边界：保持Digest256、cv-/cid-规范文本、ContentVersion/ContentIdentity强类型、update(nullptr,0)、分块无关、finalize后禁止复用/CoreError前缀；完整保留现有可复制中间状态/移动的可观察行为。使用成熟API处理完整压缩/填充，不再保留旧核心兜底，不建通用crypto服务或运行时后端框架。大数据需处理size_t与库长度类型差异，错误必须归类，不吞错。
- 影响：公开函数源兼容为目标；替换私有状态可能改变sizeof/ABI，须全量重编译main/worker/插件消费者并说明包兼容。内容SHA字节应完全不变，需跨进程/平台黄金数据和缓存回归；FNV不随本提案改。线程仍每任务实例，不共享可变句柄；RAII和复制失败语义须设计验证。性能以真实批次与原基线比较，不预设收益；许可证/分发及依赖体积按真实选择核实。
- 上游影响：拟零框架改动、零patch。若后续查出已有RobWork服务，优先撤回新增依赖候选改直接复用；如确需上游扩展须另立最小patch提案，不能借此文件批准。
- 验收/回滚：原七例、B30边界/复制/规范文本及跨平台独立oracle、真实消费者/worker内容身份一致；生产正向证据与断开成熟API调用的失效测试、性能/打包/许可证检查和独立验收。任何数值差异阻断合入。按完整能力提交/旧制品回退，不在生产保留双实现fallback。

待所有者逐项确认：成熟依赖选择与安装验证范围、core依赖/私有状态变更、ABI重编译及验收面。自动继续不视为这些批准；可继续完成其他审计。
