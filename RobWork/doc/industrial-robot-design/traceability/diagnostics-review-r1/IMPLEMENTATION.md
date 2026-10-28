# DIAG-T12：diagnostics 专项评审整改

现场为 `C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`、分支 `codex/core-review-fixes`，起始 SHA `af375242c7b632d572c8190ee0d954aa149d80dc`，起始工作区干净。所有者要求“按照评审建议进行修改”，覆盖旧 D: 主线调度。生产、测试及构建产物均来自本 C: 工作树；D: 仅只读依赖供给。未创建工作树/分支/子智能体，未远程操作、修改框架/vcpkg、放宽门禁或推进 ui 全单元评审。

本包为实施与自查，待所有者独立验收；不关闭前序 findings、迁移债务或任务。原评审及隔离探针保留于本树 `build/diagnostics-review-20261028/`，本轮原始构建/测试文件保留于 `build/diagnostics-fix-20261028/`，正式证据副本见本目录 `evidence/`。红例失败是预期的复现记录，不计为最终通过。

## 按根因实施与回滚

| 包 / 评审编号 | 保留、删除、适配及 owner | 接口与回滚边界 |
| --- | --- | --- |
| W1 D04/D05 稳定元数据 | diagnostics 保留唯一 descriptor；注册前拒绝表外 category/severity/retryable；工厂副本的 record.level 依 F-618 由 descriptor 规范化，输入对象不变，其他字段不改写 | 不增业务码表或依赖；明确正式条目 level 权威。原始业务 DiagnosticRecord 尚未经过工厂时不获得此规范化。回滚 Factory/DiagCodes 对应校验及回归 |
| W2 D01/D02/D03 安全出口 | ui 生产装配为 pipeline/factory/catalog 挂接同一服务；既有规则支持引号键和 key；普通 channel 不再授予脱敏旁路，message/channel/code/threadTag 脱敏后编码；固定失败常量使用私有来源位阻断回环 | 不增日志平台、公开旁路或 sink。装配回归编译真实 TU、真实 UI 描述符、真实磁盘。回滚装配/Logging/Redaction 及对应回归；不能只撤一侧防环 |
| W3 D06 确认观测事务 | 状态与 token 仍由 ConfirmableService 唯一管理；通知快照在状态锁外派发，出口异常被隔离；创建通知同样不覆盖已完成结果 | 只改私有 helper 签名，公共确认接口和绑定编码不变；不改变 project 编排。回滚 Confirmable 源/私有声明和回归 |
| W4 D07/D08/D09 目录与聚合 | 新条目的三种链接参与每遍容量保护；输出按 orderKey 排序后限量，容量仍按入目录序；cause-root 组复用既有任务分区字段 | 不改 DedupKey、原因图合法域、工程判定或持久化 owner。回滚 Catalog/Aggregation 对应函数和回归 |
| W5 D10 日志等待与关闭（部分修复） | flush 文件冲刷进入现有日志队列，调用者只按 steady_clock 期限等待共享完成凭据；并发 flush 不互相阻塞；关闭预算到期停止剩余记录/延迟观测并计数，不再逐条写完整 backlog | 公共签名不变、无 detach、clock/ops 借持期由 join 保护。同步在途 I/O 仍不能取消：严格析构硬时限缺口未关闭，见下文。回滚 LogItem/flush/关闭预算整组，不能仅回滚等待方 |
| W6 D11 Windows 路径 | 沿现 filesystem/同实例锁/bool 错误轨，MSVC 使用 _wfopen_s 原生 UTF-16 路径；T16 轮转语义不变 | 不更换文件框架，不承诺跨进程锁或 fsync。回滚 appendOne 与 Unicode 回归 |
| W7 D12 UTF-8 | Factory/Logging/Redaction 复用 EntryDetail 私有 UTF-8 前缀边界函数，各自保留原标记及字节预算 | 不引入公共文本平台，不裁决“预算含标记”冲突。回滚三个调用点及私有 helper |
| W8 D13/D14/D15 收尾与观测 | final 先清除更高序缓存；被选中的采样末条清除旧 tail；丢弃统计使用真实中性文案，并纳入 flush 排空 | 不改队列溢出策略或 IPC 契约，无真实业务 worker 重放接线的扩大声明。回滚对应三个分支和回归 |

## 复用与职责

定向核查 `RobWork/RobWork/src/rw/core/Log.*`、`LogWriter.*`、`LogStreamWriter.*`、`Exception.*`，目标 `sdurw_core`，本树 CMake 版本 `26.10.28`。通用流日志没有产品两级投影、稳定码、原因目录、确认绑定、worker final 或脱敏失败回环语义；不能整替产品管线。沿标准库线程/队列/filesystem 与 Windows CRT 薄适配，不新写上游数值核心、不改供给渠道。Hash 继续使用 core ContentDigester；没有新增摘要或单位转换实现。

删除依据公开 channel 名字判断“可信内部记录”的 helper；UTF-8 切点集中为单元私有原语，复用日志/脱敏既有算法。保留产品必要的稳定码、目录、确认和两级安全投影，零 Qt、只依赖 core；新增回归不依赖生产聚合 helper 生成预期。逐文件凭证和功能片段只同步本包实际变动；重钉摘要不等于独立语义验收，不将 pending/proposal 批量关闭。

## 未完成与裁决

D10 仍有同步在途文件操作不能取消的缺口：调用方 flush 期限已覆盖等待文件冲刷；析构必须 join，若 ILogFileOps 永久阻塞仍不能保证硬退出。具体候选是增加有期限/可取消的文件接缝，或改变接缝所有权并隔离可终止执行边界；两者都涉及公共语义与生命周期，未获精确方案批准前不实施。不能通过 detach 借持线程或降低既定预算伪造完成。

原评审 C01 默认路径策略、C02 去重键与修订可见性、C03 摘要预算含标记、C04 高级别满队列条款冲突保留；没有选择更宽松解释。S01 观察者退订与在途生命周期、S02 finding 终结回收、S03 回调异常/重入、S04 比较值与 locale、S05 轮转失败/定时窗口/正则性能疑点不改为确认缺陷，也不夹带新公共 API。

前序 POL-T13 正式生产码与 T24/F-968 解析失败清空修复保留，T16 已批准的轮转边界不重写。core/testkit/project/evidence/runtime/policy/execution 的独验不被本包取代。project 真实确认后仍须硬断言、重新编译、失败不提交和摘要持久化；本包不把 ConfirmableProjectStubTest 当成真实接线。UI 后续需专项核查诊断订阅的在途退订和线程边界；本次只修其诊断栈安全装配。

## 验证

最终命令、状态、passed/skipped/failed 与证据见 `VALIDATION.md`。本目录证据只说明所列版本和范围，不宣称所有问题已找全。
