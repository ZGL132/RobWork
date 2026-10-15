# 首批工程打磨执行证据

基线 01223053a130c7b437c6a32f439bdd1c58ba8d54，实施契约 WP-01-T05，任务分支 wp01-t05；原始运行日期 2026-10-15。本目录为实施自测证据，不是独立验收记录。

- evidence.zip：逐目标 ctest 全日志、gtest XML、实际产生的 ird-test-report.json、初次失败与补跑、宿主截图/工程包、独立参考、诊断探针源与执行脚本。不含 exe/lib/DLL 或构建树。
- evidence-files.json：压缩包每个文件的字节数及 SHA-256；SHA256SUMS.txt：本目录各文件摘要。验证时先对 zip 整体摘要，再对解压文件逐项核对。
- environment.json / *inventory.json / *results.json：原始编排输出；test-results-summary.json 是从真实 XML/结果表导出的汇总，不冒充原生 JSON。
- integration 是基线 45 个目标首轮；integration-perf-retry 是首次 120 秒超时后的 600 秒补跑，原失败保留。初次独立构建因 D 盘不足终止的日志名含 disk-full；重跑独立构建使用 C 盘。
- host-auto 失败；modeling-tour/requirements-tour 通过的具体动作见 stdout。modeling-tour 导出工程已为七轴，不能当作六轴贯穿案例。
- reference-draft-v5.rwdesign 是六轴真实导入映射、编码、草稿保存和只读重开的可移交现场；尚未正式 apply。v1/v2 目录保留探针 schema 填写错误的失败现场；v3/v4 为中间正确结果。重新运行 build-import-probe.ps1 时选一个不存在的新项目路径，createNew 不覆盖现有目录。
- build-probe.ps1/build-import-probe.ps1 从本机真实 vcxproj 取得 include/lib 参数，链接真实 Release 产品库；只能在对应基线与 MSVC x64 环境复用。探针不是新增正式测试目标，也不能替代完整 io/UI/编译闭环。
- run-baseline.ps1 是本轮持久留痕编排，原 gate-all.ps1 未执行；脚本中的绝对路径就是实际执行环境，新机器需按环境表调整。完整报告见 ../../engineering-polish/2026-10-15-first-batch-delivery.md。

部分原始 MSBuild/测试输出含既有编码显示问题，按原始字节保存，不把修复后的文案伪作原输出。测试内部 SKIP、原生 JSON 缺项、环境失败与未审范围均见交付矩阵。
