# GAP-CORE-FNV-01：冻结 FNV128 契约的成熟整数适配候选

状态：所有者已明确批准下述有界范围（2026-10-23）；实施后仍须独立验收。

- **缺陷与既有证据**：[core-identity-events-audit](../core-identity-events-audit.md) 的 B28 任意精度 oracle、[摘要关联提案](GAP-DIGEST-01.md) 的 B30公开类型反例。Identity.cpp::fnv1a128 按项原地累加导致后项读到已改状态，非冻结 FNV-1a128。影响六类 Id 及 ContentVersion/ContentIdentity 的 std::hash；不是内容 SHA-256 或规范持久化字节错误。现有矩阵输入已核一致，不重复运行全套探测。
- **复用检索**：B28/B30 已检索 RW/RWSim 公共头、源码、测试、示例、构建及文档；未定位冻结 FNV128 入口。当前定向重查只出现无关 OpenGL FNVPROC 名称，不能当哈希服务。现有 vcpkg 安装的 Boost.Multiprecision cpp_int.hpp 可读；本任务没有安装、修改或调用生产依赖。
- **首选候选 A，申请先验证及适配**：核已安装 Boost.Multiprecision 的固定无符号 `uint128_t` 模2^128运算；若数值/异常/许可证及构建适用，原单一fnv1a128入口按冻结逐字节异或/乘素数直接消费成熟整数运算，删除addShifted/addMulSmall/mulHi64等自持算术。保留原hi/lo返回、size_t折叠及所有公开类型，不新建整数类或更换哈希协议。
- **依赖边界**：core现规范只允许标准库＋sdurw_math；不能因Boost已在机器上就隐式使用。若采用，需所有者明确批准core私有Boost头依赖、独立冒烟供给及文档/门禁精确登记，来源仍为现有vcpkg，不加二渠道，不改上游。固定精度后端不得分配共享状态；必须验证noexcept边界及编译器支持，未验证不得承诺。
- **替代**：继续寻找已安装成熟FNV128实现；不得把只见本地port的boost-hash2认定已安装可用。重写64×64乘法是新核心候选，当前不申请；更换std::hash算法会改冻结契约，当前不申请。
- **影响核查前置**：定向确认实际容器消费者、是否有人持久化size_t hash、排序/确定性/缓存影响；只知道接口被std::hash调用，尚不能宣称所有消费者安全。保持规范ID文本/字节和SHA身份完全不变，跨进程使用序列化身份而非分桶值。
- **拟变更文件**：Identity.cpp、原IdentityTest.cpp/DigestTest.cpp、core/CMakeLists.txt必要私有依赖、units/core.md依赖说明和对应矩阵/凭证。Digest.cpp自持SHA替换仍归GAP-DIGEST-01，不夹带。
- **验收/回滚**：原core64项证据可按有效输入引用，追加全部公开hash类型、空/非空/边界长度的Python整数oracle对照；真实容器与受影响消费链、双模式/性能/一次独验。若API不适用，保留缺口不自写核心；按完整能力提交回退，生产不留双实现。

## 所有者授权

- 原提案规范内容 SHA-256：`5ad4738ecd16087b0179ee705140251c9579493bf3f16c3f46a9be07085cb719`。
- 所有者答复原文：**“批准候选A范围”**（本会话异步批准项，2026-10-23）。
- 已确认范围：候选 A：先验证已安装 Boost 固定 uint128；适用才在原 fnv1a128 薄适配、删除手写乘法，并登记 core 必要私有头依赖。不批准上游修改或新乘法核心。
- 不含未申请的新核心、上游修改或其他提案；实施者不得将自查冒充独立验收。
