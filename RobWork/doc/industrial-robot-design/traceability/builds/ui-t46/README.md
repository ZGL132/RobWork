# UI-T46 实施段留痕（宿主呈现装配——发布桥接线＋编译链真数据源＋名称映射真值）

- 分支：`ui-t46`（基点 redesign-main@afd52d2f）；实施会话：ZCode（2026-10-03）。
- 任务契约：`tasks/foundation/UI-T46.json`（本批随实施提交一并登记，status=ready 待独立验收）。

## 验证结论（双模式构建＋门禁＋八套件全绿）

1. **集成模式构建零错误**（Release 全量，`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 已 grep 确认）：
   受影响目标含 sdurws_ird_runtime（三处公共面增量）／sdurws_ird_ui_plugin／sdurws_ird_studio
   （新 TU×2 编入）／sdurws_ird_ui_test（测试增列）——全部零 error。
2. **门禁**：`ird_gates` 命中集与基线（stash 前后双跑归一 diff）**唯一新增 1 条**＝
   `IRD-GATE-SUB: 测试目标 sdurws_ird_ui_test 直链他单元产品目标 sdurws_ird_modeling_plugin`
   ——UI-T46 测试增列的测试面边（contract_test 三域 plugin 直链同款先例形态），
   登记于 DTB §2.11 UI-T46 行（随批）。其余 74 条与基线逐行一致（先在债 F-463 口径不变）。
3. **测试真实执行留痕**（本目录 gtest XML＋log；windows 平台——UI-T03 GUI 纪律；
   offscreen 平台下 ui_gui_test 5 例假红为环境形态，windows 平台全绿复验）：

| 套件 | 结果 |
| --- | --- |
| sdurws_ird_ui_test | **246/246**（含新增 HostCompilePipelineTest 6/6＋HostRuntimeNameContextTest 1/1） |
| sdurws_ird_runtime_test | **265/265**（＋1 SKIP＝worker 子进程用例环境常态；含 CM-0 范围对齐 O-36 的测试语义翻转×1） |
| sdurws_ird_modeling_test | **297/297** |
| sdurws_ird_ui_contract_test | **32/32** |
| sdurws_ird_modeling_contract_test | **17/17** |
| sdurws_ird_ui_gui_test | **76/76** |
| sdurws_ird_modeling_gui_test | **12/12** |
| sdurws_ird_requirements_test | **200/200** |
| sdurws_ird_requirements_gui_test | **39/39** |

4. **独立冒烟模式**：本批 ui 单元新增 TU 全部位于 plugin/（集成树专属目标）与
   测试门控块（if(TARGET sdurws)）——冒烟树注册面零变化（UI-T45 同款判别纪律），
   冒烟配置面未触碰（合规：ui/CMakeLists 主结构零改动，仅门控块与两目标源列表增量）。

## 关键事实（对验收有用的实证记录）

1. **编译链端到端首次真实走通**：HostCompilePipelineTest.CompilePort_PublishesSnapshot
   以内存存储替身＋真实 modeling Codec 编码字节驱动 project CompileRequest→合成闭包→
   runtime 十段链 S1~S10→快照发布→名称映射非空——生产编译链在产品装配中的可运行性
   由本用例首证。
2. **CM-0 复核范围对齐 O-36（重要口径修正）**：CanonicalModelBuilder 原实现对全模型
   对象（含关节/连杆）复核引用∈objectRefs，与 O-36 裁决"复核范围＝真实存储对象
   （根/工具/场景）；关节/连杆为模型内标识"不符——生产命令流 plannedWrites 只有根槽，
   首应用即被拒（本批接通生产编译链时实证暴露）。修正后 runtime 两测试语义翻转
   （CompilerTest.JointObjectIdOutsideRefs*、CanonicalModelBuilderTest.RejectsDuplicate
   AndForeignObjectIds——唯一性半区不限缩，引用复核半区限缩至存储对象），
   units/runtime.md §15.4 v0.13 随批登记。
3. **名称映射真值链**：HostRuntimeNameMapPort 绑当前呈现视图（发布观察者单点绑定）→
   SelectionService/三维网关/需求域三维缝三消费面共享同一实例（UI-T45"注入点单一"
   纪律兑现）—— ACC2 用例以真实编译产物验证反解往返。
4. **宿主挂接出口的 const 面口径**：HostPresentationView::hostPresentationWorkCell()
   ＝B1-SPEC v1.1 const 面审计的唯一登记豁免点（runtime 单元内单点 const 解除；
   对象同一性零复制；呈现借用写纪律）——头注三条口径澄清为对抗验收对账面。
