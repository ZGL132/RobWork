# REUSE-T01 B31-r2 独立局部验收

结论：**ACCEPT**。仅针对 B31-r2 修复与局部证据，不授全 REUSE-T01/T02 准入。

冻结 `a02785a10eada414533d9789eedac3f5af654a71`，范围基点 `52285db6f0d276742531ecb7e7bcf73db283e563`，原 B31 `933dc6016c078b7d674fea68af19c038e0388979`。独立本地 `clone --no-hardlinks --no-checkout` 后 detached checkout；长路径启用 `core.longpaths=true`。只写本独立 Temp，未修改共享树、安装依赖或执行网络/远程 Git 操作。

B31-P2-01：实际重跑提交的八目录 `rg` 命令，排除 `industrial-robot-design`，覆盖 `.md/.rst/.hpp/.cpp/.txt`，exit0、stderr空、11条命中与提交逐条一致（仅统一路径分隔符和行尾）。真实阅读 `RobWork/doc/sphinx/tutorials/basic.rst` 161–207行，173行为 Deg2Rad 示例，205行明示 radians。规范化摘要为 `f1042b3b2af4c0ac555cb1cd34e25500d6c44858ae3135e09e5f8992994e437a`，与 search.read 及51行 inputs一致。

51行仅刷新 search、inputs、auditEvidence；所有来源符号/区段、输入及证据规范化摘要均匹配，保持 pending。catalog 只刷新 UTIL-UNITS.resourceSearch，review仍 candidate，原 docs、上游与权限保持不变。冻结校验器只允许 `^RobWork/(RobWork|RobWorkSim)/` 资源路径已核实；真实共享doc保持 search.read/inputs绑定，未伪造catalog docs路径或升级verified。

产品/上游源码/vcpkg、探测 C++、两级审计 CMake 与 oracle 相对原 B31 零变化，原 batch31 全部文件 Git blob 原字节保持。原件23项、归档失败45项、r2原件54项全部 bytes/rawSHA256 与冻结Git blob及共享提交现场匹配；三个manifest自身原字节摘要分别保留在 raw-blob-verification.json，均无自引用。原独验 FAIL 与原 exit2 搜索未改写，原提交与基点均为冻结提交祖先。

新跑 task/docs/reuse(-Head冻结)/scope(58路径)/commit-lint 均 exit0；附加 cr-at-eol diff-check exit2，仅指出归档原件 make-safe-manifest.py:17 末尾空行；该原件已逐字节核对，遵循不改写历史证据要求，保留提示不洗绿。本次验收不把原件格式提示误判为功能证据不完整；复用结果只为 governance pass，functions=1011、closedFiles=0，不是算法合规。执行脚本的规范化内容均与冻结blob匹配。checks共30 passed / 0 failed。最初证据汇总早于reuse完成，将未生成exit文件记为false；原汇总与日志保留，最终以完成后的真实exit文件复核收口。

C++本轮 NOT RUN（无相关变更），引用原独立执行 XML：22跑，19 passed / 3 failed / 0 skipped / 0 notRun，失败仍为 scalar/vector/Q 三个有限极值C4反例。独立 Fraction 复核差值/M=2、阈值/M=3/2，必须false；原实现两项皆Inf。F-966仍open，不将失败洗绿。

未运行跨平台、极长序列、全部消费者、上游完整套件、GUI或完整产品集成；不修改数值算法。源工作树已有未跟踪内容保留；独立clone最终status为空。交付清单只含本Temp顶层报告、日志、结果与脚本，排除clone/.git/build/fixture及清单自身。
