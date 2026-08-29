# WP-00 需求基线与追踪实施计划

> 阶段/发布：阶段 A 前提 / R1；需求维护者、验证者和评审者必须是不同执行上下文。

## 1. 目标、非目标与完成定义

**目标：** 建立唯一、可版本化、可机器检查的需求基线，保证 124 项需求、19 个验收场景、26 个 WP、实现/测试/评审任务双向可追踪。

**完成定义：** `requirements.md` 是产品行为唯一权威；CSV 恰好 124 行且只能由生成器产生；生成器和验证器在 PowerShell 5.1/7 字节和结果一致；每个 WP 的稳定 Task ID 均有一张任务卡；门禁错误包含文件、行、字段、ID 和修复动作。

**非目标：** 不实现产品 C++、插件或 GUI；不重新解释算法、单位、容差和 OPT-B；不手工修正 CSV；不读取旧 Excel/工作簿或旧插件计划作为当前语义。

## 2. 权威层次与架构

```text
requirements.md
  ├─ 需求表、AT-01～19、第16章追踪表
  ▼
generate-traceability.ps1
  ▼
requirement-traceability.csv（派生索引，不可手改）
  ├─ work-packages/  ├─ architecture/  ├─ module-design/  └─ agent-tasks/
  ▼
validate-development-docs.ps1
  ▼
本机/CI 门禁日志与证据工件
```

冲突处理顺序固定为：需求正文/修订记录 → 架构契约 → 总纲/WP → 模块方案 → 任务卡 → CSV。智能体遇到冲突必须暂停并报告。

| 组件 | 输入 | 输出 | 所有权 | 禁止 |
| --- | --- | --- | --- | --- |
| requirements.md | 批准的需求变更 | 需求、AT、追踪声明 | 需求维护者 | 接受 CSV 反推语义 |
| generate-traceability.ps1 | 需求表、第16章 | 11列 CSV | WP-00 | 读取 WP 猜映射 |
| requirement-traceability.csv | 生成器输出 | 查询索引 | 生成器 | 手工编辑 |
| validate-development-docs.ps1 | 全部文档/CSV/manifest | 退出码和诊断 | WP-00 | 自动修正文档 |
| WP/架构/模块/任务卡 | 上游权威文档 | 实施上下文 | 各所有者 WP | 改写需求或接口 |

WP-00 只提供文档工具命令，不提供产品 C++ 接口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\generate-traceability.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1
```

成功输出固定为 `124 requirements, 19 acceptance tests, 0 trace gaps`；非零即阻断。

## 3. 代码与文档目录

```text
docs/industrial-robot-design/
├─ requirements.md
├─ development-task-breakdown.md
├─ requirement-traceability.csv
├─ generate-traceability.ps1
├─ validate-development-docs.ps1
├─ benchmark-manifest.json
├─ architecture/
├─ module-design/
├─ work-packages/WP-00-*.md
├─ agent-tasks/WP-00-T*.md
└─ fixtures/wp-00/
   ├─ missing-requirement/
   ├─ duplicate-requirement/
   ├─ empty-acceptance/
   ├─ missing-work-package/
   ├─ stale-csv/
   ├─ invalid-release/
   ├─ orphan-task/
   └─ table-shape/
```

故障夹具是最小副本，不覆盖正式文件，不进入安装包。

## 4. CSV 字段契约

CSV 固定 UTF-8 BOM、CRLF、字段顺序和标准引号；多值字段使用分号，不允许空字段或未转义换行。

| 字段 | 类型/可空 | 生产规则 |
| --- | --- | --- |
| requirement_id | 大写前缀+两位数字/否 | 需求表提取，唯一 |
| priority | P0/P1/否 | 需求表提取 |
| requirement_summary | UTF-8 文本/否 | 去首尾空白 |
| work_package | WP-00～WP-25 分号/否 | Get-WorkPackages，第一项主包 |
| implementation_task | WP-XX-IMP-ID/否 | 按 WP 顺序派生 |
| test_task | WP-XX-TEST-ID/否 | 按 WP 顺序派生 |
| review_task | WP-XX-REV-ID/否 | 按 WP 顺序派生 |
| acceptance_scenario | 方法+场景/否 | 第16章聚合 |
| phase | A～E，可用 / /否 | 第16章聚合 |
| release | R1/R2/R1-R2/否 | Get-Release 派生，禁止手写 |
| status | 固定状态枚举/否 | 初始 Planned |

状态只允许 `Planned、Ready、Implementing、Verifying、Reviewing、Rework、Integratable、Integrated、Deferred`；P1 仅经评审可 Deferred。

## 5. 详细数据流与逻辑

### 5.1 需求和验收读取

1. UTF-8 读取 requirements.md，锚定正则提取需求 ID、优先级和摘要。
2. 校验 124 行、唯一 ID、P0=110、P1=14。
3. 定位 `## 16. 需求—验收追踪` 到 `## 17.`；缺任一锚点立即失败。
4. 拆分四列追踪表，跳过表头/分隔线。
5. `Expand-RequirementCell` 展开 `REQ-01～03`，维护当前前缀，重复 ID 去重。
6. 建立 acceptanceById：Method、Scenario、Phase；每个需求至少一条。

### 5.2 工作包、任务和发布派生

1. 对每个 ID 调用显式 `Get-WorkPackages`；特殊规则优先于前缀规则。
2. 按工作包顺序派生 implementation/test/review 标识，不读取 WP 内容猜测。
3. `Get-Release` 固定：OPT-01～04/06～08=R1/R2，OPT-05/09/10=R2，CON-04=R1/R2，NFR-PERF-04～06=R2，其余按当前规则。
4. 有序对象写临时 CSV，统一 CRLF，以显式 UTF-8 BOM 原子写入 OutputPath；生成失败不覆盖正式 CSV。

### 5.3 验证器顺序

```text
路径 → benchmark JSON → 需求/AT计数 → CSV字段/行/ID/release/phase
→ 总纲WP → WP文件和章节 → architecture/module-design
→ agent-tasks字段和反向Task ID → 表格/占位扫描
→ 临时生成CSV逐字节比较正式CSV
```

错误累积后统一非零退出；错误状态不得打印成功行。每个 WP 声明的 Task ID 和每张任务卡必须双向存在。

## 6. 故障夹具

| 夹具 | 注入 | 预期诊断 |
| --- | --- | --- |
| missing-requirement | 删除需求行 | 数量错误/CSV缺失 |
| duplicate-requirement | 复制 ID | IDs not unique |
| empty-acceptance | 删除第16章追踪 | no acceptance trace |
| missing-work-package | 删除 WP 文件 | unique detailed plan 缺失 |
| stale-csv | 改摘要不重生成 | CSV stale |
| invalid-release | 修改 release | invalid release |
| orphan-task | 删除任务卡 | Task ID without card |
| table-shape | 改表列数 | table separators |

每个夹具必须断言非零退出、诊断关键词和正式目录哈希不变。

## 7. 维护者操作界面

WP-00 无产品 Widget。命令行和 CI 工件是唯一操作界面：

1. 在仓库根目录打开 PowerShell。
2. 运行生成器，检查行数、输出路径和日志。
3. 运行门禁，检查固定成功行或文件/行/ID诊断。
4. 失败只修改权威文档或脚本，重新生成 CSV。
5. CI 上传门禁日志、CSV、夹具结果和抽样追踪表。

脚本不得依赖交互输入、当前目录、用户语言或本地编码。

## 任务

WP-00 的四个独立任务按下列顺序执行：先冻结需求和范围，再生成矩阵，再建立门禁与故障夹具，最后由独立上下文抽样验证。

## 8. 详细实施步骤

1. **冻结需求输入**：复核 v0.7、单机械臂边界、OPT-B、历史关键词和关键安全/选型语义；提交基线复核记录。
2. **实现范围展开器**：覆盖单 ID、连续范围、前缀续接、重复、反向和非法范围测试。
3. **实现工作包映射**：特殊规则先测；124 个 ID 均有映射，未知 ID 必须失败。
4. **实现验收聚合**：构建一对多索引，验证每个需求至少一条方法/场景/阶段。
5. **实现 CSV 生成**：固定 11 列、排序、BOM/CRLF；5.1 与 7 逐字节比较。
6. **实现基础门禁**：路径、JSON、需求/AT计数、CSV、WP唯一性和章节检查。
7. **接入架构/模块/任务卡**：检查文件存在、契约引用、WP引用、Task ID 反向覆盖和孤立卡。
8. **实现故障夹具**：副本执行，断言非零、诊断和正式目录不变。
9. **建立双环境证据**：VS x64 环境运行 powershell.exe 和 pwsh.exe；GUI 规则由 WP-01 验证。
10. **独立抽样评审**：每个需求前缀至少两项，沿需求→CSV→WP→模块方案→任务卡→测试/评审回溯。

## 9. 任务卡与验收

- [WP-00-T01 冻结需求版本](../agent-tasks/WP-00-T01-freeze-requirements.md)
- [WP-00-T02 生成追踪矩阵](../agent-tasks/WP-00-T02-generate-trace.md)
- [WP-00-T03 建立文档门禁](../agent-tasks/WP-00-T03-validation-gate.md)
- [WP-00-T04 独立验证与评审](../agent-tasks/WP-00-T04-independent-review.md)

### Given/When/Then

- Given 正式 v0.7 文档和全部 WP/契约/模块方案/任务卡，When 运行门禁，Then 退出码 0 并输出固定成功行。
- Given 需求删除、重复、缺验收、缺 WP、stale CSV、非法 release 或孤立卡，When 运行门禁，Then 非零、给出稳定诊断且不自动修复。
- Given 正式需求，When 运行生成器，Then 输出 124 行、11 列、UTF-8 BOM/CRLF。
- Given 5.1 和 7 相同输入，When 逐字节比较，Then 长度和全部字节一致。
- Given 生成器写入中断，When 再次执行，Then 旧 CSV 可读且临时文件被清理。

## 验证

正式验证必须在仓库根目录、无交互输入的 Visual Studio x64 PowerShell 环境执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\generate-traceability.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1
pwsh.exe -NoProfile -File .\docs\industrial-robot-design\validate-development-docs.ps1
```

预期三条命令均成功；生成器输出 124 行，两个验证器输出固定成功行。故障夹具必须逐个返回非零并保留正式目录哈希。

## 10. 迁移、证据与退出

旧 CSV 少于 11 列必须重新生成；旧 Excel 仅历史保留；PowerShell 脚本保持 5.1/7 双兼容。必须提交需求复核记录、生成日志、双版本字节比较、全部夹具日志、门禁成功日志、抽样表和独立评审记录。

退出条件：

- 124 需求、19 AT、26 WP 和任务卡双向完整追踪；
- P0 100%，P1 阶段和验收方法完整；
- 架构契约、模块方案、WP 和任务卡门禁通过；
- 正式 CSV 只能由生成器产生，5.1/7 字节一致；
- 全部故障夹具按预期非零；
- 无占位符、表格错误、替换字符或未关闭冲突。

## 退出条件

以上第 10 节列出的全部退出条件必须满足，并由独立评审者签署证据清单。
