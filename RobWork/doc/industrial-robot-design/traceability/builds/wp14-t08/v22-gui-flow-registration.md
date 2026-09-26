# WP-14-T08 · V-22 GUI 主流程——设计登记（本次不启动 GUI）

> 依据：tasks/foundation/WP-14-T08.json acceptance 5（"GUI 用例仅登记执行流程
> （V-22——本次不启动 GUI，按 AGENTS Windows 规程 QT_QPA_PLATFORM=windows 逐个
> 绝对路径启动的规程写入留痕，envUnavailable 不得记绿灯）"）；units/requirements.md
> §10.2 V-22 行（"设计登记……面板截图＋诊断呈现（本次不启动）"）。
>
> **状态声明：本文件是流程设计登记，不是执行记录——以下用例全部为
> "已设计、未执行"，不得记载为通过（AGENTS §4.2；卡 §10.1 总则）。**
> 本任务实际执行的验证面为 QCoreApplication 级模型测试（test/PluginPanelTest.cpp，
> 14 用例——gtest XML＋ird-test-report.json 留痕于本目录）。

## 1. 执行环境规程（承接 AGENTS.md Windows Qt GUI 规程）

1. 在 **Visual Studio x64 Developer Environment**（vcvars64 初始化的 shell）中运行；
2. 设置 `$env:QT_QPA_PLATFORM='windows'`（不使用 offscreen）；
3. **每次只启动一个** GUI 测试可执行文件，且使用**绝对路径**调用（集成模式路径形如
   `<仓库根>/build/RobWorkStudio/src/rwslibs/industrialrobot/requirements/Release/
   sdurws_ird_requirements_test.exe`——本任务测试目标为模型级，GUI 化呈现验证随
   WP-24-T03 装配任务在宿主通道执行）；
4. 不把 Widget 测试与模型测试可执行文件合并到同一命令；
5. 若 Qt 报平台插件初始化失败：先停止受影响进程→清除冲突的 `QT_*`/`QML_*`
   环境变量→按上述设置重启；
6. 本任务执行环境事实：执行机处于锁屏/无交互桌面会话（O-42 ③同款环境约束），
   GUI 启动无法获得可视桌面——如实登记为**环境不可用（envUnavailable）**，
   不记绿灯、不补假证据。

## 2. 登记的 GUI 主流程用例（V-22，全部"已设计、未执行"）

| # | 场景（卡 §10.2 V-22 行） | 操作序列 | 预期结果 | 观测点 |
| --- | --- | --- | --- | --- |
| V-22-1 | 工位表批量粘贴 | 选中任务点→检查器表单粘贴多行"键: 值"→确认应用 | ui FormEditCommon 批量影响明细逐行接受/拒绝；接受行入草稿（applyEdit） | 就地错误保留原值（UX-03/05） |
| V-22-2 | 姿态规则切换 | 检查器"姿态规则"切换 kind（Fixed→PointAtTarget→ToolRollFree） | 参数表单按 orientationRuleParamKeys 显隐联动（五规则联动表单） | 参数行集合随 kind 变化（L-R8） |
| V-22-3 | 几何特征拾取确认 | 工位面板"拾取"→View3D 拾取态→命中场景对象特征→确认对话（三要素）→写回 | applyPickToOrientation 走确认门→AlignGeometryNormal 更新目标任务点；取消＝零变更 | 确认对话三要素；写回前确认（REQ-08/AT-23） |
| V-22-4 | TCP 捕获确认 | 工位面板"捕获当前 TCP"→确认对话→写回 | captureTcpAsFixedPoint 构造 Fixed 任务点入草稿（methodTag=captured-tcp）；会话过期→REQ-CAPTURE-STATE-STALE 警告呈现 | 来源徽标"捕获"；STALE 警告非阻断（L-R7） |
| V-22-5 | 导入向导 | requirements.import-csv→文件选择（io 预检）→表头映射＋单位预览→逐行错误清单→确认→草稿 | ImportWizardFlow 五步推进；正确行入草稿、错误行定位到列与原文（AT-02） | 部分成功明细；draft.apply 触发 prepare 重估（L-R10/L-R3） |
| V-22-6 | 区域三维预览 | 选中区域→预览出口注入 View3D | 区域轮廓＋采样格线投递（RegionPreviewGeometry）；零结果着色（KIN-07 不实现） | 预览零修订零正式证据（AT-04/V-20） |
| V-22-7 | 校验面板定位跳转 | 校验面板逐项行点击 | 树滚动＋三维高亮（LocateTarget）；无锚行不可点击 | 逐项定位跳转（UX-06/L-R1） |
| V-22-8 | 只读模式呈现 | 打开只读项目 | 编辑行灰显、写命令禁用（readOnlyAllowed=false）；浏览/预览/副本导出可用 | L-R12 三分支呈现（PM-07） |

## 3. 本次实际执行面与登记边界

- **已执行**：模型层 14 用例（PluginPanelTest.cpp）——四面板投影/编辑流/命令
  目录/快捷键注册表/向导流/导出提示/只读门控/草稿源往返的**数据面与行为面**
  （QCoreApplication 级，无窗口系统消费）；
- **未执行（envUnavailable，如实登记）**：上表 V-22-1~8 的 GUI 呈现流程——
  需交互桌面会话，本次环境不可用，**不记通过**；呈现层验证随 WP-24-T03
  正式装配（或解锁交互桌面的后续批次）按本登记规程执行后回填证据。
