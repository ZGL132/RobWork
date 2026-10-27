# REUSE-T01 B30-r2 独立验收

结论：**PASS，仅 B30-r2 局部**。

冻结 SHA：`52285db6f0d276742531ecb7e7bcf73db283e563`；范围基点：`933dc6016c078b7d674fea68af19c038e0388979`；原 B30：`0900751718849863af1190ece61c8ddb1f1cf547`。

B30-F01 的两个公开 hash 行现均直接绑定 `core/src/Identity.cpp`，摘要为 `966007ba8d3822f096b9978af59d9dca1fcd9730e3e6c342237240fb6ae3d210`。已读真实 `fnv1a128` 定义与 `Digest.hpp` 两处调用。31 行共 622 个源码段、输入、证据绑定新鲜度全部一致；原有来源及输入保留，其他字段仅预期证据刷新。

原 37 项制品、失败独验归档 57 项、R2 原件 66 项均按冻结 Git blob 原字节核对 bytes/rawSha256，零失配，目录覆盖完整且清单均不自引用。原 37 项及其 manifest 相对原 B30 未变。详见 `inspection.json`、`delta-and-coverage.json`。

五项指定门禁真实运行：task=0，docs=0，reuse=0，scope=0，commit-lint=0；附加 diff-check=0。scope 为 933dc6016..52285db6f0d276742531ecb7e7bcf73db283e563，共 69 路径。完整命令及日志见相应 `.command.txt`、`.log`、`.exit.txt`。

产品/probe/CMake 相对原 B30 零变，本次 C++ **NOT RUN**，引用原独验真实 13 跑：passed=11、failed=2、skipped=0。两项失败名称 `ContentVersionPublicFnv_CORE_T02` / `ContentIdentityPublicFnv_CORE_T02`，继续归 F-965。独立 Python hashlib 再核原两份 XML 各 14 条 CNG 属性全部一致；独立整数 FNV fold 为 `d84cf67ff3e90eee`。原有失败保持原记录，未洗绿。

R2 三条检索真实重跑退出码 1/0/0，分别表示正确 SHA 搜索无命中、Windows SDK BCrypt 符号命中、真实跨文件 FNV 定义/调用命中。

本地无硬链接克隆；第一次检出长路径失败，单次命令 `core.longpaths=true` 重试成功。冻结 reuse 首次 exit1，实因为内部 git show 同样遭遇 Windows 长路径；同一 blob 用长路径选项可读，故仅设置临时 clone 的 `core.longpaths=true` 后重跑，保留首次失败日志及诊断。未修改共享工作树，未执行远程操作或安装。克隆最终状态干净。

不授全 T01/T02、生产修复、算法替换、新核心、上游扩展或依赖安装；GAP-DIGEST-01 仍未批准。未执行跨平台/性能/超长流/全部消费者兼容性验收。

安全归档只包含 evidence 的报告、原始日志、结果、脚本及引用原件，不包含 clone/build/.git；raw 清单 `archive-manifest.json` 排除自身。
