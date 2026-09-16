# P-IO-3 候选依赖 vcpkg 可用性验证留痕（IO-T01）

| 字段 | 值 |
| --- | --- |
| 验证任务 | IO-T01（≙WP-11-T02，构建落位：io 占位转真实库） |
| 验证依据 | units/io.md §15.2 R-IO-1（"vcpkg 可用性验证前置到 IO-T01"）、§15.3 P-IO-3（候选清单与建议）、§3.3（L1 文件/XML 目标"选型 P-IO-3 冻结后登记"）；任务契约 tasks/foundation/IO-T01.json acceptance 4 |
| 验证日期 | 2026-09-17 |
| 验证人 | IO-T01 实施会话（wp11-t01 分支） |
| 结论性质 | **可用性结论供 WP-11 评审输入，不构成选型冻结**（P-IO-3 裁决者＝WP-11 评审——io.md §15.3；本任务不私裁引入任何依赖，NFR-DEP-03） |

## 1. 验证目的与边界

P-IO-3（ZIP 与 XML 解析库选型）未冻结，阻塞 IO-T04（JSON 读写器＋ZIP 通道）。
R-IO-1 处置：候选库的 vcpkg 可用性验证**前置到 IO-T01** 完成，使 WP-11 评审
冻结选型时不再有"候选不可用"的返工风险。

边界声明（三条，均为契约约束）：

1. 本验证**只读** vcpkg 状态：不执行 `vcpkg install`（安装即引入动作的前半，
   冻结权在评审）；下文"已安装"条目均为**环境既有状态**（gtest/xerces-c/
   libzip 等随既往任务或框架集成配置就位），非本任务动作。
2. 本任务产品构建（io/CMakeLists.txt）**零 L1/第三方链接登记**——io.md §3.3
   "选型 P-IO-3 冻结后登记"，冻结评审通过后由 IO-T04 增量登记（契约
   acceptance 1"本任务不引入"）。
3. NFR-DEP-03（企业离线安装）与 NFR-SEC-05（依赖清单入安装包）为选型硬
   约束，本验证给出每条候选的离线可得性证据，供评审对照。

## 2. 验证环境

| 项 | 值 |
| --- | --- |
| vcpkg 形态 | 仓库内 vendored（`RobWork/vcpkg`，经典模式、无 manifest、只读约定——AGENTS.md §1） |
| 仓库基线 | 2cad0cc59b844db8f13ea402ab243418a7083875（分支 wp11-t01 起点） |
| 目标三元组 | x64-windows（与 gtest 接入及集成构建树一致——DTB §5.5） |
| 工具链 | MSVC 2022 x64（VS 17 2022 生成器） |

## 3. 验证方法（可复现命令）

均在仓库根执行（`./vcpkg` 指代 `RobWork/vcpkg/vcpkg.exe`）：

```text
# ① 端口注册与版本（本地 ports 数据，离线可查）
./vcpkg/vcpkg.exe search libzip | ./vcpkg/vcpkg.exe search miniz
./vcpkg/vcpkg.exe search expat  | ./vcpkg/vcpkg.exe search pugixml
grep '"version' RobWork/vcpkg/ports/{libzip,miniz,expat,pugixml}/vcpkg.json

# ② 本机安装状态与特性（经典模式安装树）
./vcpkg/vcpkg.exe list | grep -iE 'libzip|miniz|expat|pugixml|xerces|zlib|bzip2'

# ③ 离线可构建证据（downloads 源码缓存）
ls RobWork/vcpkg/downloads/ | grep -iE 'libzip|miniz|expat|pugixml'

# ④ CMake 接入形态（安装树 config 与导入目标）
ls RobWork/vcpkg/installed/x64-windows/share/libzip/
grep 'add_library' RobWork/vcpkg/installed/x64-windows/share/libzip/libzip-targets.cmake

# ⑤ RobWork 自带 XML 设施与后端（L1 候选实测）
find RobWork/RobWork/RobWork/src/rw -iname '*dom*' -o -iname '*xml*'
grep XERCESC build/CMakeCache.txt
grep 'sdurw_loaders_LIB_DEPENDS' build/CMakeCache.txt
```

## 4. 候选可用性矩阵（实测 2026-09-17）

| 候选 | 端口注册 | 版本 | 本机安装 | downloads 离线缓存 | CMake 接入形态 | 关键能力核对 |
| --- | --- | --- | --- | --- | --- | --- |
| libzip（ZIP） | 在册（ports/libzip） | 1.11.4 | **已装** x64-windows（默认特性 bzip2＋default-aes） | **有**（nih-at-libzip-v1.11.4.tar.gz） | `find_package(libzip CONFIG)` → 目标 `libzip::zip`（SHARED IMPORTED；share/libzip/libzip-config.cmake 与 -targets 齐备） | 读写/修改 zip 档案；依赖 zlib 1.3.2#2、bzip2 1.0.8#6 均已装——依赖链闭环 |
| miniz（ZIP） | 在册（ports/miniz） | 3.1.2 | 未装 | 无 | （首装后经 vcpkg config 接入） | 单 C 源文件 zlib 替代（portable/嵌入式风格；zip64 能力以库文档为准，评审时核对） |
| expat（XML） | 在册（ports/expat） | 2.8.3 | 未装 | 无 | （首装后 `find_package(expat CONFIG)`） | C 流式 XML 解析器（P-IO-3 建议：流式/小） |
| pugixml（XML） | 在册（ports/pugixml） | 1.16 | 未装 | 无 | （首装后 `find_package(pugixml CONFIG)`） | 轻量 DOM＋XPath |
| RobWork 自带 XML（L1） | 不适用（框架设施） | 随框架基线 | **框架内已启用**（见 §5） | 不适用 | 链接 RobWork L1 目标（sdurw_core/sdurw_common 等，集成模式已构建） | `rw::core::DOMParser` 抽象＋`BoostXMLParser`（boost 后端）＋`rw::loaders::dom` 工具层（DOMBasisTypes 等）；Xerces 后端实测启用（§5） |

## 5. RobWork 自带 XML 实测细节

- 设施位置：`RobWork/src/rw/core/DOMParser.hpp`（`rw::core::DOMParser` 抽象，
  `rw/common/DOMParser.hpp` 为废弃转发头）、`rw/core/BoostXMLParser.{hpp,cpp}`、
  `rw/loaders/dom/*`（DOMBasisTypes/DOMProximitySetupLoader 等）。
- Xerces 后端实测：`build/CMakeCache.txt` 第 972 行
  `XERCESC_LIBRARY=.../vcpkg/installed/x64-windows/lib/xerces-c_3.lib`；
  xerces-c 3.3.0#1（含 network 特性）已在 vcpkg 安装树；
  `sdurw_loaders_LIB_DEPENDS` 缓存行含该 lib（sdurw_loaders 已实际链接）——
  即框架 XML 通道在本集成构建树为**已构建可用**状态。
- 约束对照：属 io.md §1.4 允许的 L1（`rw::common` 文件与工具设施；L3 允许
  L1——ARCH §2.3）；经既有框架目标消费，无 SA-02 源码修改面。

## 6. 验证结论

1. **四条 vcpkg 候选端口全部在册**，版本见 §4——候选清单（io.md §15.3
   P-IO-3）无一"端口不存在"死项，评审可在全集中裁决。
2. **实机就绪度分两档**：libzip（ZIP 建议）与 RobWork 自带 XML（XML 备选）
   已在本机安装/构建树就绪且离线缓存齐备，冻结后 IO-T04 可离线接入；
   miniz/expat/pugixml 端口在册但**未安装、无 downloads 缓存**——冻结选择
   它们时首次安装需网络（或先行 `vcpkg download` 预热缓存），此为
   NFR-DEP-03 离线约束下的风险输入，须在评审时明示。
3. **本任务零引入**：io/CMakeLists.txt 无任何 L1/第三方链接登记；vcpkg
   安装树在本次验证期间零变更（"已装"条目为环境既有）；ird_gates 扫描
   io 零命中（基线 11 处既有命中均与本任务无关，base 对比一致）。
4. 评审建议输入（不裁决）：ZIP 侧 libzip 的 zip64 能力（io.md §7.1
   IO-D07 包格式要求）与 AES 特性（默认含 default-aes——包加密**拒绝**
   语义 §7.1 只需检测、不需解密）在评审时按官方文档复核；XML 侧按
   §6.2 URDF/Xacro 展开边界需要的流式/DOM 形态裁量 expat/pugixml/
   RobWork 自带三选一。

## 7. 变更记录

| 日期 | 变更 | 依据 |
| --- | --- | --- |
| 2026-09-17 | 建册：IO-T01 实施段完成候选可用性验证并留痕（本文件） | 契约 acceptance 4；io.md §15.2 R-IO-1 |
