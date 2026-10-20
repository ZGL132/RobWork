# WP-13-T29 修复复验及仓库归属核对

本批完成可封闭缺陷的修复和专项复验，**不代表需求、建模全功能通过**。
实施分支为wp13-t29，继承T27@648c9509、T28@059236f2，并整合
redesign-main@402e5992。当前工作区整合、任务分支推送、独立验收、所有者合入
是四个不同事实；本报告不自签独立验收，不自行合入开发主线。冻结main@49a9e819不变。
本轮未使用子代理。

## 修复与封闭边界

| 问题 | 本批处理和实测 | 结论 |
| --- | --- | --- |
| MA-03 覆盖进度、取消、快照隔离 | 继承T27可取消上下文和进度投影；新增真实区域采样/FK/IK专项，观察0与100之间的进度及单调性，解绑后模型端口调用增长小于32、回执中断且不入成功账。另用生产编译快照弱引用真实复现attachView解绑后仍持有旧碰撞场景，修复清理顺序；覆盖/单点/批量后台只消费提交拍捕获的完整快照 | 两个新增专项通过，旧快照残留已修复。应用整体析构压力时序未单独验证，不外推该场景 |
| MA-04 表单尾部空白和可操作性 | 继承T27布局修复；新增32行动态长表单，展开后在顶层QWindow坐标点击末端输入并键入42、点击应用计数，收起后滚动上限为0，重复并缩小窗口。需求tour两尺寸底部原图已有树/卡片 | 通用机制专项通过；截图中卡片仍为折叠/未选中状态，未证明每一个需求/建模页所有展开字段验齐，因此整项不作全面封闭 |
| MA-05 优化旧契约 | 重跑52条优化契约；建支修订保留恰一原RobotDesign引用、原OID/CV和负载，符合A-13完整引用集语义，不回退产品行为以迎合旧断言 | 本项测试契约冲突已封闭，待独立验收/合入 |
| MA-09 原生测试报告 | 真实发现runtime运行300条却指定报告为0，根因是旧RT-T16环境与T27 main重复安装监听器，两个ProgramEnd争取记录；删除旧环境。补扫发现core缺报告，改测试入口统一安装一次testkit监听器。原五目标及core双模式均有非零报告并与XML逐例对应 | 六目标本轮报告入口缺口已封闭；旧零报告/缺失报告保留为失败证据，不回填历史 |
| MA-01/02 网格与碰撞 | 继承并回归T28的STL/OBJ和PQP基础设施，保留其诚实拒绝/降级语义 | 组件回归通过；格式、变换、安全间距、规则及真实宿主全矩阵仍未封闭 |
| MA-06/07/08 | F07持久来源、同项目跨域联合终验、真实三维完整矩阵及目标文字轮廓缺陷 | 按[OPEN-ISSUES.md](OPEN-ISSUES.md)登记尝试和具体封闭条件，不以测试数量关闭 |

红绿证据见red-ma03、red-ma09、red-ma09-core、green-ma04及对应全量XML/JSON。
新增专项名称为DetachViewReleasesCollisionSnapshot_MA03、
CoverageProgressAndDetachStopRealComputation_MA03、DynamicFormTailRemainsClickable_MA04。
MA-03供数端口200微秒延迟仅用于稳定观察，采样/FK/IK仍为产品算法；该延迟不作性能指标。

## 本轮真实执行

| 检查 | 结果 | 直接证据 |
| --- | --- | --- |
| 集成Release全量构建 | PASS，退出0；RWS_BUILD_INDUSTRIALROBOT=ON | full-build.log、baseline.json |
| 独立配置/15个目标构建 | PASS，退出0；框架集成专属测试不在独立模式伪登记 | standalone-configure.log、standalone-build.log |
| 集成19套 | 2668条：2665通过、0失败，XML中3跳过；原生细分为1显式跳过、2环境不可用 | verified-test-summary.json、每目标XML/原生JSON/日志 |
| 独立15套 | 1805条：1804通过、0失败，XML中1跳过；原生为1环境不可用 | verified-test-summary-standalone.json、standalone_*三件 |
| 需求/建模真实GUI | 集成110/91通过，独立110/60通过，均0失败0跳过 | requirements_gui/modeling_gui及standalone同名XML/JSON |
| 共享UI真实GUI | 集成96通过；独立结果见逐目标汇总，含动态长表单专项 | ui_gui及standalone_ui_gui的XML/JSON |
| 正式宿主需求tour | 237个pass标记、31张PNG，DONE failed-assert=0，进程退出0 | requirements-tour.log、requirements-tour/ |
| 正式宿主建模tour | 361个pass标记、31张PNG，执行至step8保存重开后继续编辑、进程退出0；源码未提供DONE标记，如实保留 | modeling-tour.log、modeling-tour/ |
| 正式宿主layout/auto | layout 67个pass标记、9张PNG；auto八步ok；均退出0 | layout.log、layout/、auto.log、tour-summary.json |
| 架构门禁 | PASS，产品扫描零命中，负向自测按预期检出 | gates.log |

逐例对账严格检查非零、唯一用例ID、XML与原生集合/投影状态相等、原生六类计数与decisive一致。
testkit原生envUnavailable在gtest XML表现为skipped，**原生分类仍保留**，不计通过。
两个环境不可用分别是需求V-22与运动学GUI登记占位；runtime显式跳过为子进程身份用例。
独立模式仅有需求V-22环境不可用。登记项不会因其他GUI测试成功而自动改成passed。
因此原生含缺供的套件decisive=false，不把“执行无失败”冒充整个验收可判定。
独立core实际63条、集成65条；独立runtime195条、集成300条，差异按框架集成注册面解释。

验证脚本第一次对账错误地直接比较三值XML与六值原生状态，在V-22处失败；
已按testkit §7.2修正仅用于对照的投影，并保留六类计数，原脚本存harness-error/。
动态表单脚本首轮把点击直接送到viewport，未命中子输入后触发keyClick空指针断言；
改用顶层窗口坐标并先断言焦点后通过。此两项为验证脚本错误，不列作产品缺陷，也不删失败现场。
集成batch首次末尾因runtime空报告失败的日志同样保留，最终以修复后每目标报告及对账汇总为准。

全部Windows GUI在VS2022 x64环境、QT_QPA_PLATFORM=windows下以绝对路径串行运行，
并检查跨工作树GUI进程互斥；未用offscreen。截图自动断言不等同完整人工视觉验收。

## 明确看过的原图与视觉限制

- requirements-tour/tour-9-populated-bottom-0.png和-1.png：两尺寸底部已有树/属性卡，
  原大面积纯空白已改善；卡片未呈现完整展开内容，保留逐页终验缺口。
- modeling-tour/mtour-1-panel.png、mtour-5-applied.png：正式宿主树和面板在位，
  施加后可见小型链图元；不是全部真实机型/网格格式的外形比对。
- modeling-tour/mtour-8-scene-update-reopened.png：重开后树/检查器和坐标架显示；
  不能由此推断所有安装姿态/TCP/遮挡拾取正确。
- modeling-tour/mtour-target-3d-preview.png：临时目标文字明显多重轮廓仍在，本轮未定位根因，
  保留MA-08，不宣称三维显示全部正确。
- modeling-tour/mtour-pick-normal-confirm.png：写回确认界面可见；不能代替各真实曲面法向真值对照。

## 仓库归属与交接

[REPOSITORY-STATE.md](REPOSITORY-STATE.md)记录97个工作树、644个引用的fetch后盘点。
需求含A-23、建模T24/T25/T26及F06原基点都已在开发主线祖先链；
真正待合的是T27/T28和本轮T29。旧UI/轨迹分支已有正式替代，不可重复整支合入。
另外两处旧工作树的已跟踪未提交修改已保留原位。没有删除工作树、分支或重写历史。
盘点JSON为取证时刻快照，当前提交/推送应以Git实际引用为准，不把JSON当实时状态源。

建议后续每个问题先核对“已有修复SHA→主线包含/替代提交→构建缓存源码路径→最小失败回归”，
再安排修复。先完成T29独立验收和所有者合入，再归档确认无独有工作与证据的旧工作树。
本报告及仓库台账不复制流水线状态，流水线仍只读pipeline/state.json。

任务契约WP-13-T29、设计计划2026-10-21-audit-closure；提交前治理验证日志见本目录。
权威findings增量同步：F-950恢复open并写明局部改善和剩余验收范围；F-949/F-951/F-952保留fixed，
更新到本轮有效回归证据，明确任务分支验证不等于主线交付。未修改旧报告或清除失败历史。
verify-task三条机器检查已通过，包含全量构建、门禁和validate-docs；新增留痕/台账后的文档复检另留日志。
本批交付限定为修复与复验证据、待独立验收，开放项不因提交或推送自动关闭。
