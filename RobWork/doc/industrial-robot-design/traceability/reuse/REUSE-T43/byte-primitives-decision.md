# 字节原语族处置决策记录（REUSE-T43，2026-10-25）

**裁决**：产品 canonical 字节序列化原语（显式小端定宽＋IEEE754 位模式）**按字节身份契约保留**（business-specific）；Boost.Endian（load_/store_）替换候选**需所有者明确批准**，未批前不实施。本记录为跨包决策引用点（评审缺口 8：约束 WP-13/WP-18 后续同类处置）。

## 1. 家族范围（沿矩阵反查的完整清单）

| 单元 | 文件 | 行族 |
| --- | --- | --- |
| selection | Backfill.cpp | ByteWriter/ByteReader 类（u32/f64/str/u32At 等） |
| selection | CombinationCheck.cpp | combinationCodec ByteWriter/ByteReader（u32/f64/str/u32At，7 行） |
| evidence | Snapshot.cpp / Slice.cpp | appendU32/appendLenStr 等大端原语（各 10 行） |
| evidence | Evidence.cpp / Evaluator.cpp | 复制 appendU32/appendLenStr（各 2 行——评审缺口 3 补登的反查漏项） |
| reporting | ReportCodec.cpp | 大端 writer/reader（64 行） |
| drivetrain | DriveTrainCodec.cpp | ByteWriter/ByteReader 原语（8 行） |

## 2. 候选否决证据（BINArchive 不等价）

上游 `rw::common::BINArchive`（UTIL-BINARCHIVE 登记面）为主机序容器、字段带格式化长度（doWrite/doRead）；产品协议为**显式字节移位定宽写＋位模式 memcpy**，携带 F-777 修复（主机序 raw 写跨端会静默改变 canonical 身份）。二者编码产物不可互换，替换即破坏既有 project/canonical 数据身份——非等价候选（检索 SB/SG，见 search-batch.txt）。

## 3. Boost.Endian 候选状态（所有者保留裁决）

- 判定性先例：T20/T21 两次引入 Boost 消费均经所有者明确批准（migration §13.1:252/256），`find_package(Boost)` 现仅 core:27、modeling:838、testkit:43 三处——reporting/evidence/selection/drivetrain 四单元均无 Boost 接线，引入需 4 处 CMakeLists 变更并有 IRD-GATE-LIB 拒绝新增局部 CMake 目标变量的先例（migration §13.1:275 kinematics 三变量）。
- 结论：Boost.Endian 属**新依赖消费渠道**，按 H1 授权口径需所有者明确批准；未批前默认分支＝按字节身份契约保留并登记本记录（语义等价性不能补偿渠道引入的程序性授权缺失）。**渠道批准已于 2026-10-26 获得——见第 6 节。**
- 批准后落地前置（顺序敏感）：先在旧实现上捕获字节基线（reporting 无既有字节 fixture——testdata/golden 29 家族无 rpt-*，评审缺口 2），逐位对照后再动码。

## 4. 「复制而非共享」的族内结构

Backfill.cpp:109-110 明文「本文件复制而非共享：单元内不设第二实现头，协议面各自冻结」——同单元双实现为**文件级协议冻结决策**（CombinationCheck 与 Backfill 的 canonical 格式各自独立演进），非无意识重复；族级统一头部（共享 include）属未来重构候选，同样受第 3 节授权约束，本包不动。

## 5. 对后续包的约束

- WP-13（policy）/WP-18（execution）及任何触及本族文件的后续包：字节原语按本记录保持 business-specific，不重复评估、不得在无批准下引入 Boost.Endian。
- 任何单元要替换本族原语：先取得 Boost 渠道批准（**已于 2026-10-26 获得，见第 6 节**）＋完成该单元字节基线捕获＋断开实验红转绿，三件齐备方可实施。

## 6. 所有者渠道批准登记（2026-10-26，A4 裁决）

所有者 2026-10-26 裁决（指令原文「A1~A4 全按推荐」之 A4）：**Boost.Endian／Boost.JSON 消费渠道已批准——经 vcpkg 公共依赖渠道引入，不新增本仓 vendored 依赖**。本记录第 3 节「未批前不实施」的渠道授权缺口就此关闭：

- 渠道批准≠逐单元落地指令：各单元是否替换字节原语族仍由后续任务按自身边界决定；实施时本记录其余约束不变——先在旧实现上捕获该单元字节基线（顺序敏感，reporting 无既有字节 fixture＝评审缺口 2）、逐位对照后动码、断开实验红转绿（第 3/5 节三件套的其余两件）。
- 引入路径沿用既有 vcpkg 接线规范（`find_package(Boost)`，参照 core:27/modeling:838/testkit:43 在案先例），CMake 目标变量口径遵守 IRD-GATE-LIB 既有先例。
- 本节为登记性补记，不改写第 1—5 节的历史裁决与证据。
