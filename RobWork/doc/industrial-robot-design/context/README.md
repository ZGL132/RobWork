# 上下文工具（按需使用）

执行入口以根 AGENTS.md §1 为准：当前任务契约＋相关条款和源码。本文、asset-inventory.json、reading-paths.md、rule-enforcement.md 都是导航/维护工具，不是每次任务的必读集合。

## 1. 定位

先用 rg 定位任务 ID、需求 ID、符号或文件，再读附近章节。不默认加载整册 REQUIREMENTS/ARCHITECTURE/DTB、完整 reuse 库存、历史证据或 findings；同一会话已读且未变的规则不重读。输出限制在必要片段，日志落盘。

## 2. 资产职责

- authority：需求/架构/单元设计；按相关条款取用。
- task-source：DTB 任务行与 tasks/ 契约；按任务 ID 取用。
- workflow：ACC/CCP/PIPE；独立验收、契约编制或自动编排时才读。
- generated：context pack、证据索引；可选缓存，脚本生成，禁止手工伪造。
- evidence：原始构建/测试/验收证据；按当前任务路径核对，不默认读取全库。
- decision：长期决策叙事；确有新长期决策时记录，不要求每个修正写 Note。
- archive：历史资料，不进入默认上下文。

## 3. context pack

仅在交接/复杂任务确有需要时运行 build-context-pack.ps1 -TaskId <id>。使用前以 validate-context-pack.ps1 -TaskId <id> 检查该包；无需 -All 或刷新未使用的其他任务包。包里指纹变化不等于任务语义变化，回查相关源章节即可。

显式指定任务但包不存在（包括整个 packs 目录不存在）时退出 1；全量模式无包可查仍退出 0。生成器与校验器共用 `context-pack-files.ps1` 的原有源文件清单：契约、designRefs 所属文件及既有规则输入的指纹不得遗漏。结构错退出 1，输入变化/状态漂移退出 2；缓存校验不代替任务契约校验、复用审计或原件验收。

## 4. 唯一事实源

自动流水线状态只读写 traceability/pipeline/state.json；跨任务缺陷在 traceability/findings.json，不复制到新状态台账。普通人工任务不读写 PIPE 状态。

## 5. 证据

当前任务记录命令、结果、日志/报告路径；保留真实原始证据且不改写历史。全库 evidence-index.json 是检索快照，允许滞后；需要审计时重跑 index-evidence.ps1，再运行 validate-docs.ps1 -DeepEvidence。普通提交不要求索引联动更新。

## 6. 资产登记

设计树新增根级资产时更新 asset-inventory.json；编辑已有文件无需刷新。保留既有 schema 与 canonicalLinks，生成物由脚本重建。七类资产定义见 §2；无需为每条文字规则再维护一行执法审计表。
