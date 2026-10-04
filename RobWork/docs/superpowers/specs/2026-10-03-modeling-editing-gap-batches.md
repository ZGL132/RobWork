# 建模编辑面差距清单与四批立项（UI-T47～T50）

> 状态：已按所有者指令立项（2026-10-03，"从这三处出发做差距清单并拆四批立项（任务号 UI-T47 起）"承接）。
> 立项配套契约＝`tasks/foundation/UI-T47.json`（ready）＋`UI-T48.json`／`UI-T49.json`／`UI-T50.json`（planned，前置落位后转 ready）。
> 红线前置：`old/` 历史实现零拷贝（仅交互能力对照）；R-1/R-2 单元边界；碰撞判定唯一归 policy（ARC-05）；D-MDL-10（草稿不编译）；PA-1/PA-2（权威唯一/修订只增）。

---

## 0. 差距来源（三处）

| # | 出处 | 原文登记 | 内容 |
| --- | --- | --- | --- |
| ① | `tasks/foundation/UI-T41.json` note（批次C 收尾登记） | "C4（结构变更参数保留）/C5（几何资源选择器）登记不实施——依赖结构编辑面与引用字段编辑流（域命令目录无此二面），待后续任务卡" | 两项登记不实施项：**C4** 结构变更时参数保留；**C5** 几何资源选择器 |
| ② | `tasks/foundation/UI-T43.json` note（外审报告接线类 P1 承接） | "外审报告其余 P1（TCP 捕获/三维拾取/**建模对象编辑扩展**）属功能批次，不在本批范围" | 外审 P1 三项中前两项已随 UI-T45＋UI-T33＋UI-T46 收口；**建模对象编辑扩展**未实施。注：外审报告原文不在库（UI-T43 实施段仅逐条核实了接线类建议），实施前置＝报告原文由用户补充或自外部审计记录取得 |
| ③ | `units/modeling.md` §2.5 旧代码功能对照表三行（承接状态标 ✅，新版面板无编辑面） | 行 3"关节表编辑（增删/上移下移/六轴重置）"、行 6"外部几何文件导入（stl/obj/dae/wrl/iv 选择）"、行 7"独立碰撞模型表＋从 Drawables 生成碰撞模型" | 设计语义已落 §5.1/§5.2/§6.7/§4.3-B 并标等价承接；**新版建模面板尚无对应编辑面**——实现层差距而非设计缺口 |

## 1. 现状盘点（2026-10-03，UI-T46 落位后；代码实证）

已就位的编辑与投影面：

1. **域命令面**：五个 project commandType（`apply-robot-design`/`apply-tool-definition`/`apply-scene-objects`/`apply-named-poses`/`apply-drivetrain-design`）——整对象应用语义；草稿差值经 `ModelingUiModule::buildDraftCommand` 组装 `apply-robot-design` 信封。**结构编辑与引用编辑无需新增 project token**——缺的是域编辑原语与面板编辑面（UI-T41 note"域命令目录无此二面"的准确含义）。
2. **面板编辑流**（`modeling/plugin/PanelEditFlow.hpp`）：仅 `submitJointFieldEdit`（D5 关节常用字段——零位/限位）＋`submitCentroidEdit`（L-8 平行轴确认）＋D6 两页（`dh-parameters`/`properties`）。**无结构操作、无几何引用编辑**。
3. **几何行投影**：UI-T41 批次C 已落形状徽标（Mesh/Primitive）＋悬空检测（⚠"未入资源清单"）——只读投影，编辑面空缺。
4. **身份与载荷机制**：`PayloadObjectSlot.allocateNew`＋prepare 期 `ctx.objectId()` 取号（PA-1）已冻结；关节/连杆为模型内标识（O-36），结构变更随根槽替换落库——机制在位，缺消费。
5. **io 端口挂账**：UI-T41 note⑧"导入文件读取标准库（io 端口接线随后续任务）"——URDF/Xacro 导入现以标准库直读替身运行，io 受管路径（SafePath/BudgetGuard/IResourceReader/ResourceSnapshot）未接入面板导入流。

## 2. 差距清单（逐项）

| 编号 | 差距 | 来源 | 去向 |
| --- | --- | --- | --- |
| G1 | 关节链结构编辑：新增（选中位插入）/删除（引用保护＋链连续性）/上移下移重排/六轴重置（§5.1 T-MDL-1 模板参数） | ③行3＋② | **UI-T47** |
| G2 | 结构变更参数保留（C4）：结构操作时用户权威参数（axis/origin/zeroOffset/bounds/workingRange/物性/几何引用）随对象身份保留；DH 权威态派生值重算不冒充权威（D-MDL-5） | ①C4 | **UI-T47** |
| G3 | 几何资源选择器（C5）：文件对话框（stl/obj/dae/wrl/iv 格式族）→io 读取/识别/预算→ExternalResourceRecord（Recorded）→GeometryRef 挂接 | ①C5＋③行6 | **UI-T48** |
| G4 | visual/collision 引用字段编辑流：挂接/替换/摘除＋localTransform（m/rad）编辑＋资源状态呈现（Missing/Changed——§8.2 L6） | ①（引用字段编辑流）＋③行6 | **UI-T48** |
| G5 | io 端口接线：标准库直读替身退役，导入流接 io 受管路径（SafePath 逃逸检查＋BudgetGuard 预算） | UI-T41 note⑧ | **UI-T48** |
| G6 | 独立碰撞模型表：逐连杆 collision 引用独立编辑（与 visual 分离） | ③行7 | **UI-T49** |
| G7 | 视觉→碰撞引用复制辅助：§5.2 几何生成辅助②（同资源引用复制——GeometricEstimate 来源标记；零网格重画/凸包简化） | ③行7＋② | **UI-T49** |
| G8 | 五对象属性页扩展：工具（MDL-13）/场景（MDL-15）/位姿集（MDL-17）/传动（MDL-16）字段页供给——§9.7.5 D5 注"根/基座/工具/场景/位姿集/传动 v1 无页面供给（诚实二态）"的增量边界兑现 | ②（建模对象编辑扩展的主体可考面） | **UI-T50** |
| G9 | 外审 P1「建模对象编辑扩展」收口对账：报告原文取得后逐项对账，残项如实登记 | ② | **UI-T50** |

## 3. 四批映射与堆叠序

| 批次 | 卡 | 覆盖差距 | 核心增量 | 依赖 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 批一·结构编辑 | UI-T47（分支 `ui-t47`） | G1＋G2 | 域侧结构编辑原语（§5.2 词表边界）＋面板关节表编辑面＋就绪联动＋C4 消账 | UI-T46（排序前置——文件面零交叠，合入序 T46→T47） | **done**（已合入 redesign-main） |
| 批二·几何引用 | UI-T48（分支 `ui-t48`） | G3＋G4＋G5 | 资源选择器＋io 端口真装＋引用挂换摘＋C5 消账 | UI-T47（串行堆叠——PanelEditFlow/属性区同文件交叠） | **done**（已合入 redesign-main） |
| 批三·碰撞模型 | UI-T49（分支 `ui-t49`） | G6＋G7 | 独立碰撞模型表＋视觉→碰撞复制辅助（消费批二引用流设施） | UI-T48 | **落位待验收**（分支 ui-t49 已推送） |
| 批四·对象扩展收口 | UI-T50（分支 `ui-t50`） | G8＋G9 | 五对象属性页（UI-T22 D5/D6 页协议）＋外审 P1 对账收口＋三处差距整合回归 | UI-T47＋UI-T48＋UI-T49 | planned |

- 堆叠纪律：分支基点＝UI-T46 合入后的 `redesign-main`；串行堆叠（UI-T36→T37→T38 先例同款），合入序 T47→T48→T49→T50。
- 每批独立走三段式（实施→对抗验收 acc/ui-t4x→owner 合入）；域语义词表缺口一律先 `units/modeling.md` 增量修订再实现（DTB §5.4），禁代码先行。

## 4. 各批边界（禁止项摘要）

1. 全批：`old/src/rwslibs/robotmodelbuilder` 零拷贝（仅能力对照）；单元间互不直链（R-1）；跨单元只消费公共头（R-2）；io/**、runtime/** 等他单元源码禁改（公共面缺口停原地登记）。
2. UI-T47：结构编辑零权威模式切换语义（整链一个 authority）；新关节身份 PA-1 唯一取号点（禁第二取号路径）；空变化零快照（幻影脏化纪律）。
3. UI-T48：modeling 只持引用不持资源字节（CON-03）；固化半区（solidifyToStaging→Solidified）不实施——归 io/project 既有轨；格式族外选择＝诚实拒绝。
4. UI-T49：零碰撞判定语义（ARC-05）；SelfCollisionHints 不动（P-MDL-3 导入报告→策略草稿输入既有轨）；复制辅助产物与手编同权同校验。
5. UI-T50：页协议走 UI-T22 冻结面（共享检查器零建模类型知识——域判定在域）；D-MDL-10（位姿集/传动页零即时编译语义）；外审报告原文缺席时按可考面收口并登记残留（禁凭记忆推断报告内容）。

## 5. 旁注（已知相关项，不在本四批内）

1. **UI-T33 三项呈现收口**（工位标记/区域边界框/采样格着色——RegionPreviewSink 宿主场景注入）：在其卡承载，UI-T46 合入后回到该卡完成（acc 随收口合并复验）——本四批不涉及。
2. **需求域坐标输入差距**（"空间与公差"卡位置框只读——`RequirementsPanelWidget.cpp` ~1808 行 `setReadOnly(true)`，UI-T37 返工③登记为后续批次；域侧 `applyStationEditSet` 词表已在位）：可另立卡或并入 UI-T33 后批次，与本四批无关。
3. **F-462**（建议级——WritableSwitchRefreshesAll 变异不敏感，UI-T40 序列候选）：维持 open，不在本四批。
4. **登记漂移回填**：UI-T43/T44/T45 三批在 `units/ui.md` §13 与 DTB §2.11 均无登记行（实施留痕此前仅在提交与 builds/ 目录；同族先例＝F-467 版本谱系跳号）——已由四批立项的同日治理批次回填（ui.md v1.53：§13 表尾 T42 与 T46 之间增三行＋DTB §2.11 同步；三行均注明 acc/ui-t43、acc/ui-t44、acc/ui-t45 待独立验收会话补录）。
