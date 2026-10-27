# ④附带发现：foundation-tasks.json（legacy 数组）不进入 -TaskFile 套件的依据

首跑（76 个 .json 全传入，含 foundation-tasks.json）validate-docs exit=1：
  ready task has empty or missing field allowedFiles: CORE-T01

定性（与送验范围无关的基线既有潜伏态）：
- CORE-T01 等 78 份契约内嵌于 legacy 数组文件 foundation-tasks.json，其中 71 个
  status=ready 且 allowedFiles 为空（BASE 44c8d59e4 同态 71 个；两提交间该文件
  `git diff --exit-code 44c8d59e4 HEAD -- .../tasks/foundation-tasks.json` 零差异）。
- 范围内 tasks/ 仅触碰 REUSE-T40.json（git diff --name-only 44c8d59e4..15449f55d）。
- 门禁自身口径即排除该文件：validate-docs.ps1 的 $taskFiles 扫描
  `Where-Object { $_.Name -ne "foundation-tasks.json" }`，validate-task.ps1 的
  dependsOn 索引同样跳过。故④正式口径＝75 份独立契约全传入（数组形式），
  legacy 数组文件不入套件；其内嵌契约的 allowedFiles 缺失另属治理存量，
  不由本验收修复（只验收不修复），提请所有者知悉。
