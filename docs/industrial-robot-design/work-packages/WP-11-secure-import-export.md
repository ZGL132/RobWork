# WP-11 安全导入导出实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 为 CSV、JSON、URDF、网格和项目资源提供统一安全边界，防止路径逃逸、资源耗尽、公式注入和业务插件重复解析。

**Architecture:** 通用 I/O 层负责字节预算、路径、编码、CSV 语法和安全导出；业务适配器负责把已验证记录转换为领域对象。解析器不执行宏、公式、命令或外部资源加载。

**Tech Stack:** C++、Qt Core、标准库、CTest；不新增工作簿解析依赖。

---

## 文件与目标

**创建目标：** `sdurws_ird_io`、`sdurws_ird_io_test`。

**创建：**

- `industrialrobot/io/include/.../ImportBudget.hpp`
- `industrialrobot/io/include/.../SafeProjectPath.hpp`
- `industrialrobot/io/include/.../CsvReader.hpp`
- `industrialrobot/io/include/.../CsvWriter.hpp`
- `industrialrobot/io/include/.../JsonDocumentReader.hpp`
- `industrialrobot/io/include/.../ResourceImportService.hpp`
- `industrialrobot/io/include/.../CatalogPackageReader.hpp`
- `industrialrobot/io/src/`
- `industrialrobot/io/test/`

**覆盖需求：** REQ-05，SEL-01、02，NFR-REL-04，NFR-SEC-01～03，AT-02、08。

## CSV 契约

- 输入接受 UTF-8 和 UTF-8 BOM；其他编码明确拒绝并给出转换建议。
- 支持 RFC 4180 引号、逗号和换行；行号和字段名进入诊断。
- 字段映射、单位和采用值在提交前预览；业务层决定正确行是否可保留。
- CSV 字段永远按文本/数值数据解析，不执行公式。
- 导出文本若以 `=、+、-、@` 开头，使用统一安全转义；结构化 JSON 证据保存未转义原值。

## 目录包契约

```text
catalog_manifest.json
motors.csv
reducers.csv
motor_curves.csv
reducer_curves.csv
compatibility.csv
```

manifest 固定记录 Schema、目录 ID、版本、来源、文件名和 SHA-256。缺文件、多余未声明文件、哈希错误和跨表悬空引用均阻止形成 CatalogVersion。

## 任务

### Task 1：路径和资源预算

- [ ] 先写 `..`、绝对路径、UNC 逃逸、符号链接逃逸、大小写变体和超长路径失败测试。
- [ ] 定义单文件大小、总导入量、XML 深度、记录数和几何复杂度预算。
- [ ] 超限立即停止并返回稳定诊断，不产生部分项目修订。

### Task 2：CSV 读取

- [ ] 覆盖 BOM、引号、内嵌换行、空字段、重复表头、非有限数和非法单位。
- [ ] 逐行结果保存源行号、原值、规范值和诊断；不自动把非法值转零。
- [ ] 验证看似公式的字符串只作为文本，不触发任何执行路径。

### Task 3：CSV 安全写出

- [ ] 对 `=1+1`、`+cmd`、`-2+3`、`@SUM` 和普通负数建立区分测试。
- [ ] 统一转义危险文本，数值类型仍按数值输出；不得破坏往返原值。
- [ ] 所有业务 CSV 导出只调用 CsvWriter。

### Task 4：目录包与跨表引用

- [ ] 读取 manifest 后逐文件校验哈希、Schema、列、单位和行数预算。
- [ ] 校验型号唯一性、曲线点顺序、曲线覆盖和 compatibility 引用。
- [ ] 形成不可变 CatalogVersion 输入；业务筛选规则仍归 WP-19。

### Task 5：URDF、网格和 JSON 边界

- [ ] 通用层只提供安全字节/路径/资源读取，语义解析归 WP-13。
- [ ] 禁止外部实体、网络 URL、命令、宏和资源区外引用。
- [ ] 损坏文件和资源缺失给出可定位诊断并保留项目原状态。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_io_test$'
```

## 退出条件

- 恶意路径、超预算、损坏编码和公式样式字段全部被安全处理并可诊断。
- 业务插件没有第二套 CSV/JSON/路径读取实现。
- CSV 任务导入和目录包导入满足 AT-02、08 的输入侧验收。
