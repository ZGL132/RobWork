# GOV-T21 验收返工实施记录

此记录为实施自检，不能代替冻结提交的独立验收。首轮验收 fail，不回写或覆盖首轮实现/测试记录。

首轮对象：64ef0fd07755479ce01b3f936320fe91396cfce2。独立证据：acc/GOV-T21/1，提交 b889110329709ca340c89cb8ac811bc7cd32dfdf，机器记录 traceability/acceptance/GOV-T21-20261022.json。

| 阻断 | 修正与边界 | 回归证据 |
| --- | --- | --- |
| F-957：干净检出夹具无变更可提交，测试驱动提前退出 | 夹具 commit --allow-empty，仍冻结真实Git树；新增重复冻结正例；JSON必须记录expected/executed/notRun/driverFailure/complete，JUnit在驱动中断时产生error | ps7-final/ps51-final各45项（41负例、4正例）；干净检出需新提交CI及独立验收重跑 |
| F-958：.c/.ps1未声明源码绕过，.mjs未入库 | validate-reuse同一谓词覆盖C/C++模板、脚本、含代码页面和构建文件；固定b12基点，原1370项不删除，补31项为1401；非业务单元文件归foundation-governance | 12种后缀真实子进程负例（含.inc/.opaque未知类型），均须REUSE-UNDECLARED；未知类型新产品文件默认拒绝，先登记发现类型；仍不宣称关键词可以证明语义无重复 |
| 驱动错误不能被部分通过掩盖 | 向真实脚本传不存在的RepoRoot，主动触发clone失败；无生产故障注入开关 | driver-failure-final：退出1、executed0、notRun45、complete=false、JUnit errors1；这是预期拒绝，不计入45项通过 |

双模式重新配置并执行ird_gates，命令沿用上一轮REPORT；实际命令/输出在integration-unknown-final.log、smoke-unknown-final.log。integration.log/smoke.log保留首次CMD路径分隔符错误（未启动构建），不当成配置结果。上一轮全量双模式构建实件仍有日志，本次返工未改CMake或任何产品C++/上游源码。verify-task、文档深检、范围与commit检查另存。产品算法/GUI/性能测试未执行；治理前完整CI失败F-956仍保持阻断，不放宽现有测试。

本轮独立验收需新SHA、新上下文和acc/GOV-T21/2证据。F-957/F-958在独立验收确认前不自签关闭。REUSE-T01至T06继续planned；不编译适配层或核心替换执行任务，不合入redesign-main。

unknown-red保留补强前未知后缀绕过的红测试，原ps7/ps51是补强前已知类型45项通过记录；最终采用ps7-final/ps51-final并由新冻结提交独立复跑。不覆盖中间失败历史。
