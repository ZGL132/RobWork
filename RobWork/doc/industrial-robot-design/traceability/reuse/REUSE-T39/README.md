# WP-15批次C／REUSE-T39：摘要供给核验（audit-only）——GAP-DIGEST-01 同渠道 vcpkg port 在案核验

实施日期：2026-10-25；分支 redesign-main；起始 SHA `616823ccfb35…`（WP-15-T38 收口提交，完整 40 位见 git log）。
任务契约：[REUSE-T39](../../../tasks/REUSE-T39.json)。状态＝实施完成待独验（不自签）。

## 性质与边界

本批为 **audit-only**：零产品源码、零 CMake、零依赖安装、零上游改动。依据 GAP-DIGEST-01.md:3"未批准"与 :13"可继续完成其他审计"降级执行（migration.md §13.4"某提案受阻只停止相关改动"）。**GAP-DIGEST-01.md 本体零改动**（其未批准状态、B30 既有证据、oracle.py 冻结向量与"待所有者逐项确认"清单原样保留）。

## 供给核验结论（本次实读，原始摘录见附件）

| 项 | boost-hash2 | openssl |
| --- | --- | --- |
| port 在案 | `vcpkg/ports/boost-hash2/` | `vcpkg/ports/openssl/` |
| 版本 | 1.92.0（自动生成 port，`scripts/boost/generate-ports.ps1`） | 3.6.5（port-version 1） |
| 许可证 | BSL-1.0 | Apache-2.0 |
| 来源固定 | portfile `vcpkg_from_github`（boostorg/hash2，SHA512 钉死：`1555de1c8c0e…da17ef`） | vcpkg-cmake 族构建（host 依赖 vcpkg-cmake/vcpkg-cmake-config/vcpkg-cmake-get-vars） |
| 依赖面 | boost-assert/cmake/config/container-hash/describe/headers/mp11（全 Boost 家族，头文件型模块为主） | 无运行期依赖（features 含 fips: !static） |
| 安装状态 | **未安装**（`vcpkg/installed/x64-windows/share|include` 无 hash2） | **未安装**（share 无 openssl） |
| 产品共享消费者 | 零（20 单元 CMakeLists 无 find_package 引用） | 零 |

与 GAP-DIGEST-01.md:6 的一致性：本核验证实"先核同渠道 vcpkg boost-hash2（本地 port 1.92.0，BSL-1.0，固定来源摘要）；再比较 openssl（许可证以其 vcpkg.json 为准＝Apache-2.0）；二者仅见 port，未核安装/API/跨平台"——安装状态本次已核（未装），**API/跨平台/函数签名仍未核**（未安装即无法编译探测；不编造签名，维持 GAP 原文纪律）。

## 待所有者裁决（不因本核验而视为已批）

1. 成熟依赖选择与安装验证范围（boost-hash2 / openssl / 维持自持）。
2. core 依赖面变更与 Digest.cpp/.hpp 私有状态替换（units/core.md §3.2、D-05 修订同步）。
3. ABI 重编译与消费者兼容面、验收面（含性能以真实批次与原基线比较）。

获批后实施路径的 oracle 约定（GAP-DIGEST-01.md:8-11 原文条款引用）：内容 SHA 字节完全不变（B30 oracle.py＋跨平台黄金数据＋分块无关＋finalize 禁复用）；不保留双实现 fallback；FNV/Identity（T15）与 F-965 公开 hash 不随本提案改。

## 附件

- `supply-boost-hash2.txt`：port vcpkg.json＋portfile.cmake 原文摘录（含治理口径 sha256）。
- `supply-openssl.txt`：port vcpkg.json 关键段摘录（含治理口径 sha256）。
- `gap-digest-01-attachment.md`：核验记录与 GAP 的衔接说明（作为 GAP-DIGEST-01 的证据附件，不改其状态）。

## 矩阵与凭证

audit-only：逐功能矩阵零触碰（core.json Digest 23 行维持 pending/现状）；`reuse/changes/REUSE-T39.json` 凭证 files 为空数组（零产品文件改动的治理表达）。
