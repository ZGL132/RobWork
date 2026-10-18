# WP-14-T12 第二十一批：参考系目录与正式模型帧消费

状态：未完成。两处私有修复已真实验证，但正式宿主新增持久化断言仍失败；A-13设计待所有者精确放行。没有使用子代理，未改project产品文件。

- [PASS] batch21-directory-green：名称目录用例通过；batch21-directory-red同一回归先实际失败，AlignFrame目标错误列出World。修复只从AlignFrame目标种类移除World，位置参考系仍合法可选World，模型/场景候选保留。
- [FAIL] batch21-host-reference-red及batch21-host-closure-diagnostic：当前HEAD找不到模型根；诊断确认只剩ProjectMetadata和scene-object。batch21-manifest-loss保存真实磁盘manifest：seq2含robot-design，seq3含robot-design＋scene-object，seq4仅scene-object。历史清单未改。
- [FAIL] batch21-host-model-isolated-red：将模型探针移至夹具更新前，模型根确实存在，但生产解析器把Device名交给findFrame，受理被拒；同时夹具更新后根丢失断言失败。
- [PASS] 修复后batch21-host-model-fixed中的真实ModelFrame/SceneObject两轨均受理并完成，各按本次ack.taskRef等待，逐次局部撤销恢复完整工作集；原三维创建/移动、真实拾取及其它宿主断言仍执行。
- [FAIL] batch21-host-model-fixed整体：228条断言中227通过、1失败，exit1。唯一失败为scene-only-commit-preserves-model-root，不能称宿主全通过。两轨因缺少碰撞检测器而DataInsufficient，只证明装配及诚实降级，不证明可达性。
- [NOT RUN] A-13修订完整对象集保留实现：超出当前WP-14-T12精确allowedFiles，已在计划给出八节设计并向所有者请求范围放行；未修改project或扩展契约。

batch21-host-red未执行新增探针：最初只构建sdurws_ird_ui_plugin，而正式studio把UiPlugin.cpp同源直接编入exe；该次旧宿主exit0不作为本批新增回归通过证据。之后先构建sdurws_ird_studio且确认exit0，再实际运行。此运行原样保留。

GUI均在VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe路径、单个GUI进程下执行。batch21-host-model-fixed/mtour-5.7-scene-picked.png已目视检查：正交近景的标记盒/轴与空共享树不构成完整模型mesh保真证据。

构建、逐exe最终回归与门禁的确切结果在本报告后续核验段及batch21-verification-summary.json登记。原120项审计历史不改，尚无当前版本120项全部通过结论。

## 核验段

- [PASS] batch21-build/integrated-full.log：完整集成串行构建exit0，缓存RWS_BUILD_INDUSTRIALROBOT:BOOL=ON；batch21-build/standalone-full.log：已配置vcpkg/Qt的独立完整串行构建exit0。
- [PASS] 集成7个exe共846跑：845通过、1旧占位跳过、0失败；独立6个exe共708跑：707通过、1旧占位跳过、0失败。逐exe权威XML路径和计数见batch21-verification-summary.json，JSON由各exe生成；core未生成JSON，已明确从其原始XML转录。跳过按testcase/skipped实际元素计数。
- [PASS] batch21-build/gates.log：ird_gates零命中；batch21-build/validate-task.log：当前任务契约校验通过。

组件/契约全绿没有消除正式宿主持久化失败。此批以WIP保存，尚缺A-13实施及其回归、原120项当前快照逐行终验、真实多TCP与五规则/部分约束求解、完整mesh/法向及跨域消费等；不得标记WP-14-T12验收完成。

- [PASS] batch21-build/verify-task.log：任务三条机器命令（完整集成构建、ird_gates、validate-docs）全部执行通过，exit0。末次validate-docs识别9099条证据；新增完整日志与报告后再次刷新证据索引并校验。源码/文档git diff --check通过；原始MSBuild日志中的空白原样保留，不改写实测输出。
