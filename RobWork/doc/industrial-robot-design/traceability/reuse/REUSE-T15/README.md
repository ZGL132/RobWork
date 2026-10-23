# REUSE-T15：固定 FNV128 的成熟算术薄适配

基线 `f378f01c5199032e08c2cce7ba0251a2f783d505`；任务分支 codex/reuse-foundation。GAP-CORE-FNV-01 候选 A 的明确批准见对应提案授权节。所有者另明确答复：**“接受本次 FNV128 开销，保留候选并交独验”**（2026-10-23，针对 performance.json 的逐项提问）。不扩大到 SHA、上游或新算术核心。

保留六类 Id、AttemptId/TaskIdentity、规范解析/格式/字节、随机生成纪律、ContentVersion/ContentIdentity 的 SHA 与 hi/lo 折叠；删除 add/addShifted/mulHi64/addMulSmall。原 fnv1a128 逐字节异或/乘固定素数委托现有 vcpkg Boost.Multiprecision uint128_t；没有新公开包装层。core 仍只拥有值契约，不获取模型/状态/诊断或证据权威。

上游检索直接引用 B28 的 search.json/core-identity-events-audit.md 与 B30：RW/RWSim 未找到等价冻结 FNV128。上游 Random 的全局状态不满足既有私有随机纪律；StateStructure 局部编号不能替代持久身份。其余解析、保留值和精确比较是产品契约，并非新通用解析框架。CMake 经 find_package(Boost CONFIG) 显式取得 Boost::headers 的头目录，作为 Identity.cpp 私有编译输入，不产生链接依赖，来源为既有 vcpkg；不改第三方、不 vendor、不加运行库。catalog 只允许 RW/RWSim 源码，不伪造 Boost 为上游能力；本包以已批准的产品 FNV 协议薄适配登记，具体外部供给见下文。

适用性：已安装 cpp_int.hpp/fwd.hpp 的 uint128_t = number<cpp_int_backend<128,128,unsigned_magnitude,unchecked,void>>；固定宽度、模回绕、无分配器、无共享状态。Boost Software License 1.0 已随现有依赖提供。公开 operator*= 与 operator^= 没有 noexcept 声明，首次编译断言失败保存在 boost-build.log；不谎称类型系统保证。已审读 number.hpp 对应调用与 cpp_int 固定 unchecked/void 非抛后端路径，既定输入不触发checked异常/变精度分配；探测观察运算窗口零 new。禁止换成动态 cpp_int 或 checked 后端。boost-build-r2.log、boost-oracle.json 为候选原语；production-probe-build.log、production-oracle.json 为真实生产入口，各1,017组精确整数期望，无失败。不是全输入形式证明。

消费者定向核实：公开 hash 特化在 Identity.hpp/Digest.hpp；project/src/ObjectStore.hpp 的对象版本键、RevisionIndex.hpp 的修订表，execution/include/.../RunRegistry.hpp 的 RunId 表均用于内存查找；持久化仍以规范身份/SHA文本。未发现持久化size_t分桶值；不声称审计所有未来消费者。修复会改变旧错误hash数值及桶遍历次序，禁止消费者依赖该顺序；精确身份及公共排序不变。project282项、execution登记表27项及runtime305项已回归，不接管业务算法。

实际验证：build-r2.log 集成受影响目标零错误；core.xml **69 passed/0 skipped/0 failed**（六类ID＋两类摘要公开hash及原容器）；runtime.xml **305/0/0**；execution.xml **27/0/0**；project.xml **282/0/0**；smoke-build.log＋smoke.xml **67/0/0**（冒烟无Q/旋转两项，按原条件编列，非skip）。各有对应原生JSON。oracle.py 固定种子20261023，用Python整数模2^128独立生成期望；Boost与真实产品探测各1017通过，不算成2034个不同输入。

性能：performance.json 为 /O2 未显式 NDEBUG 的三轮数据，所有者已接受其中最大范围；随后 performance-release.json 以 /O2 /DNDEBUG 对齐实际Release：16字节0.296–0.306µs、32字节0.546–0.573µs/次，旧错误实现约0.026–0.030与0.058–0.061µs。两者每轮100万次、同工具链/机器，观察分配均0；不以错误算法作为准确性oracle，不将内核时间推为端到端预算。没有进一步为追快重写乘法。

公共头仅纠正旧“手写乘法”注释，签名/ABI不变。依据根AGENTS需要一次独验，由所有者另开会话；本会话只自查、不closed、不合入、不远程。全量门禁继承 drivetrain/CMakeLists.txt 覆盖缺口；该项由协调主会话安排，不能称全局准入PASS。回滚边界为本包完整提交，不保留双实现。

最终私有头供给改为取 Boost::headers 的 INTERFACE_INCLUDE_DIRECTORIES 后，增量集成及独立冒烟目标构建均退出0；原件见 REUSE-T15/build-final.log 与 smoke-build-final.log。此后仅补中文注释，不改变二进制语义，直接引用上述测试证据。

收口核查：原函数本包范围诊断 PASS（REUSE-T16/package-diagnostic.ps1、package-diagnostic.log），不是全量准入替代；架构引擎 PASS（REUSE-T16/ird-gates-r4.log）。全量 validate-reuse、validate-docs 与两任务 validate-task 均 FAIL，唯一阻断仍为集成基线继承的 drivetrain/CMakeLists.txt REUSE-COVERAGE，由协调会话安排，不修改业务文件/门禁迁就。实际退出码与分列测试计数见 verification.json；独验仍 NOT RUN。

本轮两个有界能力包及失败候选记录作为一个本地交付提交；整体回滚须回退该提交，避免只回退代码而留下不匹配矩阵/凭证。未承诺两包可分别用单次 git revert 回滚。共享 execution/ui 行仅更新受影响输入绑定，不修改其 pending 状态或历史结论。

## 独验状态核对（2026-10-23 复核会话追加；不回写上文历史结论）

- 实施版本 `091e53353`（与 T16 同一本地交付提交）；实施后至 `6fb26b31d` core 产品源码零变化，上文证据输入仍适用。
- 上文"全量门禁唯一阻断＝drivetrain/CMakeLists.txt REUSE-COVERAGE"已由 `6fb26b31d` 合入清除：冻结核实测 validate-reuse PASS（exit 0；余 10 条 REUSE-AD-PENDING 为分支既有状态）。
- **独立验收仍 NOT RUN**：由所有者另开独立会话执行；最小核对清单＝verification.json 分列计数（core 69/runtime 305/execution 27/project 282/冒烟 67）＋oracle.py 固定种子对照＋performance-release.json（所有者已接受范围）＋execution/ui 共享行 pending 未改核对。本会话不自签。
