# WP-24 安装与发布实施计划

**目标：** 交付 Windows x64 离线安装包、版本并存、插件白名单和依赖完整性验证。

**阶段/发布：** 阶段 E，R1/R2；R1 和 R2 分别生成可回滚包。

**需求与契约：** NFR-MNT、NFR-DEP、NFR-SEC-04～06；引用 `architecture/persistence-schema.md`、`testing-contract.md`。

**拥有目录：** `RobWork/scripts/industrial-robot/`、`RobWork/installer/industrial-robot/`、依赖基线和 CI 配置；脚本从仓库根目录解析绝对路径。

**输入/输出：** 输入为构建产物、依赖基线、白名单和发布切片；输出为离线安装包、哈希/许可证清单、安装日志和回滚证据。

## 任务

1. 创建配置、构建、测试、边界扫描和打包脚本，并声明 PowerShell 版本前置。
2. 构建 R1/R2 安装包，包含 RobWork、RobWorkStudio、RobWorkSim、Qt、运行库和依赖清单。
3. 验证离线安装、卸载、版本并存、升级、回滚和无开发机绝对路径。
4. 启用插件显式白名单，记录组件版本、许可证、哈希和来源。
5. 生成发布检查表；签名能力按企业部署策略单独验收。

## 验证

前置：WP-01、WP-22、WP-23；

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release -Release R1
```

必须提交安装日志、包清单、白名单报告、哈希校验和回滚演练记录。

## 迁移与删除

旧安装目标和旧格式只允许在拒绝测试中出现；R1/R2 包验收后从安装清单和 CI 中删除。

## 独立评审

由发布工程师和安全负责人独立复核离线安装、白名单、哈希、许可证和回滚证据。

## 退出条件

NFR-DEP-01～05、NFR-SEC-04～06、NFR-MNT-06 通过；R1/R2 可在干净 Windows x64 机器离线安装并回滚。

## 任务卡索引

- [WP-24-T01 构建测试与边界脚本](../agent-tasks/WP-24-T01-build-scripts.md)
- [WP-24-T02 R1/R2 安装包](../agent-tasks/WP-24-T02-packages.md)
- [WP-24-T03 安装生命周期](../agent-tasks/WP-24-T03-install-lifecycle.md)
- [WP-24-T04 插件白名单与来源](../agent-tasks/WP-24-T04-whitelist.md)
- [WP-24-T05 发布检查表与签名](../agent-tasks/WP-24-T05-release-checklist.md)
