# CI 门禁模板（WP-01-T02 落位，2026-09-10）

本目录承载 **CI 常驻门禁的契约模板**（ARCHITECTURE §11.2 第 2 条），两份文件与既有探索分支结论 **逐字节一致**（对齐基准＝commit `eb777e2`〔codex/wp-01-t04，2026-08-31 所有者裁决双文件口径〕，`cmp` 校验通过——契约 acceptance 第 4 条"对齐"成立，无差异改写）：

| 文件 | 角色 | 出处（eb777e2 原路径） |
| --- | --- | --- |
| `industrial-robot-windows.gitlab-ci.yml` | GitLab 门禁契约定义（Runner 接入前豁免实际执行） | `RobWork/gitlab-ci/industrial-robot-windows.yml` |
| `industrial-robot-windows.github-workflow.yml` | GitHub Actions 执行通道（步骤顺序/job 划分/缓存白名单/脚本行与 GitLab 侧逐字符一致） | `.github/workflows/industrial-robot-windows.yml` |

## 已固化结论（缓存回退键与模板供给——契约 acceptance 4 的对齐内容）

1. **双 ini 模板供给**：CI 配置期缺失的两个 gitignored 模板（`RobWorkStudio.ini.template.static`／`ini.shared.in`）由基建步内嵌同文生成（与主树模板逐字节一致，eb777e2 验证）——同时消解 findings **F-007** 的"全新树配置卡死"半边（ini 模板项；toolchain/Qt 前缀项由 workflow 的 `CMAKE_PREFIX_PATH` env＋vcpkg 安装步承载）。
2. **缓存回退键**：`restore-keys` 前缀回退（workflow 变更导致 cache key 变化时命中前缀即跳过依赖安装；保存步 `cache-hit != 'true'` 才写）。
3. **缓存白名单**：仅 `.cache/industrial-robot/{dependencies,packages}/`，不缓存测试结果/结果库/项目快照；失败工件 `when: always`。
4. **六条门禁行**：configure → build → 模型测试 → GUI 规则通道预检（`QT_QPA_PLATFORM=windows`）→ check-boundaries → package。

## 与现状的差异说明（激活前置，非本模板缺陷）

| # | 差异 | 激活处置 |
| --- | --- | --- |
| 1 | 两文件的**落位目标路径在框架树/仓库根**（`RobWork/gitlab-ci/`、`.github/workflows/`），超出本契约 allowedFiles——SA-02：框架侧文件先登记 `industrialrobot/patches/PATCHES.md` 再改动 | 所有者扩契约＋补丁登记后，将本目录两文件复制至原路径（内容零改动） |
| 2 | 模板引用的入口脚本为 `RobWork\scripts\industrial-robot\{configure,build,run-tests,check-boundaries,package}.ps1`（连字符命名，eb777e2 世界），现行治理脚本目录为 `scripts/industrialrobot/`（无连字符）且入口脚本未建 | 激活时由所有者裁决：或按模板口径补建入口脚本（推荐薄封装 `gate-all.ps1`），或统一改路径——二选一，登记于补丁说明 |
| 3 | 集成默认分支保护（main 仅接受六步全绿合并）依赖仓库设置，非文件可承载 | 仓库设置项，激活后操作员配置 |

## 本地强制门槛（CI 建成前）

`pwsh -File RobWork/scripts/industrialrobot/gate-all.ps1`（ird_gates＋双模式构建＋全部 `_test`/`_contract_test` 目标一键执行；零 Python）。CI 常驻激活前，该脚本为每次提交的强制门槛（契约 acceptance 1）。
