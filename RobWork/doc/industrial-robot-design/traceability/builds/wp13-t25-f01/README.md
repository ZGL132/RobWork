# WP-13-T25 F01 批证据——逐关节最大速度/最大加速度 schema 落位

任务：WP-13-T25 第 2 项（F01／审计 M05／MDL-01／AT-01）。
基线：本批工作树＝F06 提交后（598c9b03）＋本批改动。
日期：2026-10-20。执行：ZCode 旁支会话（独立 worktree C:/zcode-wt/wp13-t25）。

## 缺口与实现（单一权威数据路径）

- 缺口（红证锚定）：JointEntry 无 maxVelocity/maxAcceleration 字段——CanonicalBridge.cpp 硬写 NotProvided、URDF `<limit velocity>` 谎报"joint-velocity-limit 无落点"、WorkCell VelLimit/AccLimit 报未保留、面板关节零行、模板参考值不入内核（审计 M05）。
- 权威归属（MDL-01 原文）："逐轴定义关节（类型、轴线、零位、限位、**速度/加速度限制**）"——关节一等权威字段（两权威模式均权威，同 bounds/zeroOffset）；非传动对象（§4.7 无速度字段——旧 PanelModel"属传动对象不投影"划界系缺字段期的过渡口径，随本批废止）。
- 实现：RobotDesign.hpp 表 4.3-A 表尾两行（SourcedValue<double>；转动 rad/s、rad/s²｜移动 m/s、m/s²；Provided 须有限且 ≥0〔零＝真实零速非缺失〕；Fixed＝NotApplicable）→ Codec {1,0}→{1,1}（表尾追加；旧字节兼容读 NotProvided——手构 {1,0} 夹具按线上格式规范逐字节构造并解码验证）→ CanonicalBridge Provided 透传（SI 真值门＋provenance 原样→runtime JointDescription 既有字段）→ 模板种子（sixAxisTemplateDefaults 逐轴值）／URDF velocity／WorkCell VelLimit·AccLimit 三入口映射 → PanelModel 两行＋面板中文标签＋编辑页 12→14 行＋applyJointFieldEdit 域规则（负值/非有限/Fixed 拒绝；拒绝原子性）→ 就绪 L9 注记条件化。

## 红证（先红后绿）

| 文件 | 内容 |
| --- | --- |
| red-standalone-3tests.{xml,log} | 桥/导入报告/面板行 3 例红（NotProvided／谎报不支持／行缺失） |
| red-integration-readiness.{xml,log} | 就绪注记红（无条件登记 drivetrain.velocityAcceleration） |

## 绿证

| 文件 | 口径 | 结果 |
| --- | --- | --- |
| green-standalone-modeling.{xml,log} | 独立 modeling_test 全量 | 343 通过/0 失败/0 跳过 |
| green-standalone-modeling-contract.{xml,log} | 独立 contract | 8/0/0 |
| green-standalone-modeling-gui.{xml,log} | 独立 GUI 全量 | 48/0/0 |
| green-integration-modeling.{xml,log} | 集成 modeling_test 全量 | 420/0/0 |
| green-integration-modeling-contract.{xml,log} | 集成 contract | 17/0/0 |
| green-integration-modeling-gui.{xml,log} | 集成 GUI 全量（含新增速度表单编辑/拒绝链 GUI 例） | 62/0/0 |
| green-integration-ui.{xml,log} | 集成 ui（python313 PATH 前置） | 326/0/0 |

ird_gates 零命中（exit 0）；validate-docs/validate-task PASS。

## 关键新增断言面

- 逐轴不同值：v=[0.8,1.1,1.4,1.7,2.0,2.3] rad/s、a=[1..6] rad/s²（交接探针族——TemplateTest 编辑规则＋CodecTest 值入身份）。
- 旧编码真实解码：手构最小 {1,0} 根对象字节（magic/小端/全 NotProvided SourcedValue/1 关节 2 连杆）→{1,1} 读端解码成功＋追加字段 NotProvided＋再编码升级 {1,1}。
- 身份：逐关节不同 v/a→编码字节不同；同值重编码逐字节全等；仅改 j1 速度→字节变。
- 描述消费：模板种子六轴逐轴值经编码→桥→JointDescription Provided（期望值取 sixAxisTemplateDefaults 单一权威）。
- 拒绝原子性：负值/NaN/Fixed 拒绝后工作集与拒绝前逐字段相等；GUI 拒绝后表格回填真实草稿值（F04 同族纪律）。
- F06 复跑：WorkCell VelLimit 报告由未保留转映射行（流＋GUI 两面断言更新）；URDF velocity 同（流断言＋黄金计数）。

## 黄金数据集演化（非掩盖）

mdl-urdf-import/1.0.0 期望面：unsupported 8→3（joint-velocity-limit×5 移出）、新增 mappedVelocityCount=5 计数锚；manifest sha256/size 更新＋history 登记（契约演化随批提交，测试同步钉住映射面——非对账失败回写）。

## 四层状态

- 数据：PASS（字段/编码/兼容读/身份/描述/导入映射逐项）。
- Qt 交互：PASS（表单输入→两步确认→域接受；负值→拒绝→表格回填真实值；14 行装配）。
- 正式宿主：PASS（命令按钮/面板链路经既有装配；就绪条注记条件化）。
- 真实三维：NOT RUN（本项无三维消费面）。
- 消费者执行说明：runtime JointDescription.maxVelocity/maxAcceleration 为既有字段（CanonicalModel/hasJointVelocityLimits 既有消费链），本批打通 modeling→描述流入；轨迹/动力学入口的端到端数值消费复验归 F07/统一验收批的跨域链（本批以描述层 Provided 为闭合点，如实登记）。

## 环境口径

同 F06 批（wp13-t25-f06/README.md）；框架 RobWorkSim 编译失败＝基线固有（v0.47 已登记先例＋本批 stash 复现，工业链零错误口径）。
