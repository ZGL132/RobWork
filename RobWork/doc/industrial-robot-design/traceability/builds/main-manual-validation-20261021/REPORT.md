# 最新开发主线手工验收交接（2026-10-21）

## 目标与合入结果

用户明确授权“所有修改合并到主分支，保持主分支为最新版本软件”，用于本轮人工验证。
本次按该所有者决策，将唯一开发主线redesign-main从402e5992快进至 `de7590e75ff06bf17f2c251b55f53f9b78dfbe88`，
继承T27@648c9509、T28@059236f2、T29@de7590e7；T24/T25/T26、需求含A-23和运动学T20此前已进入祖先链。
冻结main@49a9e819不变；未把旧失败/被替代UI或轨迹分支重复合入，未重写历史、未强推。
本轮没有新增产品源码修改，只新增主线构建/回归及手工启动辅助证据。
此后仅追加本报告及索引的治理提交，产品源码树仍由manual-build.json的productTree精确标识。

## 构建与验证

| 检查 | 真实结果 | 证据 |
| --- | --- | --- |
| 合入前集成全量构建/架构门禁 | PASS，退出0 | premerge-full-build.log、premerge-gates.log |
| 合入前专项回归 | 448项：447通过、1显式跳过、0失败 | verified-test-summary-premerge.json及premerge-*三套原始报告 |
| 提交信息/范围/文档检查 | PASS，4个新增提交、346个允许路径 | commit-lint-premerge.log、scope-premerge.log、validate-docs-premerge.log |
| 主线工作树Release全量构建 | PASS，退出0，缓存绑定3582主线源码，工业产品ON | full-build.log、baseline.json |
| 主线19套回归 | 2668项：2665通过、3 XML跳过、0失败 | verified-test-summary.json及逐目标XML/原生JSON/日志 |
| 独立模式15目标构建及15套回归 | 1805项：1804通过、1 XML跳过、0失败 | standalone-build.log、verified-test-summary-standalone.json及standalone_* |
| 主线架构门禁 | PASS，产品扫描零命中，负向自测正常 | gates.log、gates-command.log |
| 主线正式宿主auto流程 | PASS，退出0 | auto.log、tour-command.log |

跳过项及原生环境缺供按summary的nativeTotals保留，未计入通过；GUI全部在VS2022 x64、
QT_QPA_PLATFORM=windows下，以绝对路径串行执行，运行期间无其他GUI进程重叠。
独立树在486b/build/audit-closure-20261021/standalone，源码提交也是de7590e7；
主线集成树在3582/build。没有把独立模式的注册面冒充完整宿主覆盖。
各GUI测试生成的本轮截图保留在主线build/manual-validation-20261021/gui-*本地目录；
本次不宣称完整人工视觉验收，留给用户逐页操作。

## 手工启动入口

直接双击本机文件：
`C:/Users/zgl18/.codex/worktrees/3582/RobWork/build/manual-validation-20261021/Start-Latest.cmd`

对应正式产品程序：
`C:/Users/zgl18/.codex/worktrees/3582/RobWork/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe`

入口先检查主线、构建源码及程序SHA-256，再按VS x64/windows环境启动正常工作台。
本地脚本及manifest也作为证据保存在本目录；本目录中的副本仍默认指向上述本机主线工作树。
旧根目录start_studio.bat硬编码D盘旧构建，本次人工验证请使用这里的新入口，避免测到旧版。
程序SHA-256：`5AE196BCB9E454B00C017F86A905E9D443A7517EC5374C7113F842B0BC0B3DB0`。manual-build.json保存精确源码提交、源码树和二进制摘要。

## 开放边界

用户本次授权的是集成与人工验收版本交付，不构成全新上下文独立验收通过。
T29原报告、失败证据和OPEN-ISSUES.md不回写；开放的网格格式/变换、碰撞策略与安全间距、
F07持久来源、联合终验、三维完整矩阵及目标文字多重轮廓仍按原台账保留。
已知未闭项不因合并自动判为修复完成，其他旧验收树暂存还原/格式现场仍原位保留。

## 交接与下一步

最新源码已在redesign-main，正式程序由该主线工作树重建。
提交和远程同步结果以Git实时引用核对，不在本报告复制流水线状态或伪造未来SHA。
用户通过新入口进行手工验证；若发现问题，请记录操作步骤、项目/修订及截图，再按最小复现修复。
本轮不使用子代理。
