# WP-24-T05 发布检查表与签名

- **Task ID / 需求 ID / ADR / 阶段：** WP-24-T05；NFR-MNT-06（旧安装目标审计）、NFR-SEC-06（签名按企业部署策略双分支）、§13.4（安装包审计，每阶段提交）；ADR-002（回滚必测项以 `.rwdesign` 用户数据可打开为准）；阶段 E / R1＋R2。契约：`architecture/persistence-schema.md` §1；模块详设 `module-design/installation-release.md` v0.3 §5（六步协议签署为检查表输入）、§6（旧安装目标审计：命中即发布阻断）、§8（发布工程师与安全负责人独立复核）。验证方式＝审计记录＋检查表复核（testing-contract §4），不以 CTest 目标替代。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（installation-release.md v0.3）
- **前置任务及必需工件：** WP-24-T02（R1/R2 包与 `release-manifest.json`）、WP-24-T03（六步演练记录与签署）、WP-24-T04（白名单与依赖/许可证清单齐备）；外部：独立质量负责人签署的 WP-23-T05 门禁清单（作为检查表引用项）。
- **允许创建/修改/删除的文件：** 创建 `RobWork/installer/industrial-robot/release-checklist-r1.md`、`release-checklist-r2.md`、`RobWork/installer/industrial-robot/evidence/t05-release-checklist/`（旧目标审计结果、签名例外记录）。不创建/修改/删除其他文件（审计只读包清单，不改包）。
- **禁止修改的文件和公共接口：** 安装器工程、三清单与三脚本（只读）；业务代码、测试门禁；`requirements.md`、CSV、`architecture/`、`module-design/`；不得为通过审计而从包中移除文件后不改 `release-manifest.json` 重出包。
- **修改前接口：** 无发布检查表；旧安装目标审计未执行；签名处置无双分支记录。
- **修改后接口：** 发布检查表 R1/R2 各一份，覆盖：①§5 六步协议签署记录逐项核对（前置/安装/冒烟/卸载/并存/升级回滚，任一失败＝发布阻断）；②回滚必测项（旧版本打开既有 `.rwdesign`，persistence-schema §1）；③三清单与哈希双向校验结论、白名单报告、离线安装证明；④WP-23 门禁清单引用（R1 对应阶段 B/C 子集门禁、R2 对应全量）；⑤旧安装目标审计结论——对包清单与 `sdurws_robotmodelbuilder`、`sdurws_engineeringrequirements`、`sdurws_kinematicanalysis`、`sdurws_structureoptimizer*`（含通配匹配）名单比对，命中即发布阻断，每阶段提交审计结果；⑥签名双分支（NFR-SEC-06）：签名能力可用时验证安装与升级包完整性，未启用时记录例外并保持哈希校验。
- **实施步骤：**
  1. 编制 R1/R2 检查表模板：逐项映射 §5/§6 断言与前置工件引用。
  2. 执行旧目标审计：读取 `release-manifest.json` 包清单，与四旧目标名单（含 `sdurws_structureoptimizer*` 通配）比对，记录命中数（要求 0）。
  3. 逐项核对 T02～T04 与 T03 演练签署记录，回填检查表。
  4. 按企业部署策略处理签名双分支并记录（例外须保留哈希校验链）。
  5. 提交发布工程师与安全负责人独立复核签署。
- **RED 测试：** 不适用（审计与复核任务）；以检查表首项（四旧目标名单比对命中＝0）作为阻断断言——任何命中即任务失败。
- **最小实现：** 两份检查表＋审计结果＋签名处置记录；不重打包、不改清单内容（清单问题回 T02/T04 整改后复验）。
- **正常/边界/失败测试：**
  - 正常：Given R2 包清单与签署齐备的 T02～T04 记录，When 复核，Then 检查表全项通过、旧目标审计命中 0、双方签署完成。
  - 边界：Given 企业未启用代码签名，When 处置 NFR-SEC-06，Then 记录例外（含理由与负责人）且哈希校验链完整，检查表标注"签名例外"不视为阻断。
  - 失败：Given 包清单命中任一旧目标（含通配变体），When 审计，Then 判发布阻断并上报；Given 六步协议任一签署缺失，Then 检查表拒绝放行并指向缺口步骤。
- **精确验证方式：**（审计记录＋检查表复核，无自动化测试命令）
  - 审计记录：包清单 × 四旧目标名单比对输出（`sdurws_robotmodelbuilder`/`sdurws_engineeringrequirements`/`sdurws_kinematicanalysis`/`sdurws_structureoptimizer*`，命中数必须为 0），随每阶段提交。
  - 复核检查表（首项不成立即任务失败）：①R1/R2 各一份且覆盖上述六类断言；②T03 六步签署记录齐备且回滚必测项在列；③三清单双向校验与白名单报告引用有效；④签名双分支处置记录在文（含例外情形）；⑤发布工程师与安全负责人签署栏非空（module-design/installation-release.md §8）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含两份检查表与 evidence 目录；零代码/清单/脚本改动；审计结果含包清单来源（manifest SHA-256）与比对命令/工具记录；文件 UTF-8 无 BOM、LF。
- **证据工件：** `RobWork/installer/industrial-robot/release-checklist-r1.md`、`release-checklist-r2.md`、`evidence/t05-release-checklist/`（旧目标审计结果、签名例外记录、双方签署页）、commit。
- **提交格式：** `WP-24-T05: 发布检查表与签名`
- **停止与升级条件：** 旧目标审计命中非 0、或 T02～T04 任一签署/证据缺失无法闭合时，停止并升级发布工程师与安全负责人（阻断发布，不自行整改包内容）；签名策略与 NFR-SEC-06 双分支口径冲突时上报，不自行裁剪检查表条目。
