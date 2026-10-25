# GAP-DIGEST-01 证据附件：同渠道 vcpkg 供给核验记录（REUSE-T39 / WP-15批次C）

> 本文件是 [GAP-DIGEST-01](../../../extensions/GAP-DIGEST-01.md) 的**证据附件**。
> GAP 本体（状态"未批准"、待所有者逐项确认清单、B30 既有证据与 oracle 冻结向量）零改动；
> 本附件不构成任何批准，仅完成 GAP:6"先核同渠道 vcpkg boost-hash2…再比较 openssl…二者仅见
> port，未核安装/API/跨平台"中的**在案性与安装状态核验**。

核验会话：REUSE-T39（2026-10-25，起点 616823ccfb35…，分支 redesign-main）。

## 核验结果

1. **boost-hash2**：port 在案（`vcpkg/ports/boost-hash2/`），version 1.92.0，BSL-1.0；
   portfile 经 `vcpkg_from_github` 钉死 boostorg/hash2 来源（SHA512 `1555de1c8c0e…da17ef`）；
   依赖面全为 Boost 家族模块（assert/cmake/config/container-hash/describe/headers/mp11）。
   **未安装**（`vcpkg/installed/x64-windows/share|include` 无该组件）；产品 20 单元 CMake 零消费。
   原文与治理口径 sha256：`supply-boost-hash2.txt`（vcpkg.json `86cd9457…`、portfile `5e65d260…`）。
2. **openssl**：port 在案（`vcpkg/ports/openssl/`），version 3.6.5 port-version 1，Apache-2.0；
   host 依赖 vcpkg-cmake 族；features 含 fips(!static)。**未安装**；产品零消费。
   原文与治理口径 sha256：`supply-openssl.txt`（vcpkg.json `7d44cd71…`）。
3. 与 GAP:6 的差异更正：GAP 原文"openssl 许可证以其 vcpkg.json 为准"——本次实读为 **Apache-2.0**。
4. 仍未核（维持 GAP 纪律，不编造）：两候选的安装后 API 面/跨平台行为/函数签名/依赖体积实测。

## 对 GAP 待确认项的映射

- "成熟依赖选择与安装验证范围"：本附件提供选择所需的在案性/许可证/来源固定/依赖面事实；
  安装验证范围仍待所有者圈定（安装本身未执行）。
- "core 依赖/私有状态变更""ABI 重编译及验收面"：未动，待批准后子任务（获批后 oracle 约定见
  GAP:8-11 与 REUSE-T39 契约 acceptance 第 3 条）。

## 冻结面声明

Digest.cpp 自持 FIPS SHA-256、Digest.hpp 私有状态、core/CMakeLists.txt、DigestTest.cpp、
units/core.md §3.2/D-05 在本批全部零改动；core.json Digest 23 行与 ContentDigester.compressBlock
维持现状（pending/migrated 现状不变）；T15 FNV/Identity 与 F-965 公开 hash 零触碰。
