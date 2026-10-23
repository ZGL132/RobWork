# B30 冻结版独立验收：FAIL

冻结 SHA：0900751718849863af1190ece61c8ddb1f1cf547；基点：bb283be1dbb6ae62c59d6563e3c7dac0a4ae5103。仅验 B30 局部审计，不授全 T01/T02 或任何生产变更权。验收从 Temp 独立本地 clone/detached checkout 执行，未改共享树、未联网、未远程操作。

## 阻断项 B30-F01

`reuse/functions/core.json:8808,8913` 的 `hash.ContentVersion` / `hash.ContentIdentity` 两行未将实际执行的 `core/src/Identity.cpp:71-120`（`detail::fnv1a128`）加入 inputs，也没有跨行依赖。`Digest.hpp:146,154` 明确调用这个实现。旧 B28 的独立记录不构成 B30 证据新鲜度的依赖链；改变 FNV 实现并刷新 B28 后，B30 旧报告仍可能保留有效绑定。依据 audit-contract §4/§6，应在新提交补完整依赖并按新冻结 SHA 复验。本次不代为返工、不更改历史红证。

## 真实复现

VS 2022 x64 开发环境下配置和构建 `ird_digest_audit` 均 exit 0；绝对路径单进程运行原 DigestTest 七例及新增六例：13 total、11 passed、2 failed、0 skipped、exit 1。失败恰为 F-965 两个公开 hash：预期 fold `d84cf67ff3e90eee`（15586103439835860718），实际 3981071475404768780。保留等值断言，没有洗绿。独立 Python 任意精度 FNV 得到 hi `8211cde9c1366483`、lo `5a5d3b9632df6a6d`；14 个 XML CNG 属性全部与 Python hashlib 及入库向量一致。

## 通过的核查与边界

- task/docs/reuse（冻结 -Head）/scope/commit-lint 均 exit 0。默认 diff-check exit 2 将原件 CRLF 的 CR 报为尾空白；保留原日志，按 `core.whitespace=cr-at-eol` 检查同范围 exit 0，未改原件。复用门禁通过仅为 governance，不等于算法或语义验收通过。
- 范围 43 路径；新增 30 行、刷新 compressBlock；全 pending。558 个规范化区段、输入、证据摘要一致。旧矩阵仅刷新预期 compressBlock；生产、上游、vcpkg、catalog 和旧证据零差异。
- 原件 manifest 37 项全部与 Git blob 原始字节一致；9 项工作树 LF→CRLF 差异另列 inspection.json，未篡改原日志。
- 重跑 8 项上下游/安装/CNG 检索，退出码和命中行数与原证据一致；5 项上游阅读摘要、4 项外部候选原始摘要一致。补跑修正 SHA2 词边界转义的检索也无命中。StringUtil 数值解析不是 cv-/cid- 强类型摘要服务；未伪造 catalog/API。
- CMake 实际复用父配置、core/testkit/TestMainReport 与原 DigestTest；CNG 仅 Windows 审计参照，无新生产依赖或算法副本。GAP-DIGEST-01 仍未批准，port 存在不等于安装/API 适用。
- ARCH §3.6/职责边界未扩大；canonical 字节编码仍归各消费者 owner。未把值类型散列缺陷误称 SHA-256 错误或持久化损坏。

未执行：全 core64 例、跨平台/性能/超长消息/ULONG 上界、全部消费者兼容性、成熟候选安装与 API 探测、全 T01/T02、远程 CI/推送。它们不能标通过。

验收原件：acceptance.json、inspection.json、构建/运行日志、gtest.xml、ird-test-report.json 与门禁日志；可复现脚本 reproduce.ps1 / gates.ps1 / inspect.py。安全归档只包含本 evidence 目录，manifest 仅排除自身，不收 clone/.git/build/fixture。
