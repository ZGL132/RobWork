# REUSE-T01 第五批只读独立验收

- 冻结 SHA：`9fd7256cf61a7768622810ed547617a66013949b`。
- 比较基点：`8a7174945a8f280f674a40ec46a512b5b5f1b414`，已核为冻结提交祖先。
- 独立现场：本目录 `repo/` shared clone，detached HEAD；checkout exit 0，验收前后工作树干净。
- 结论：**FAIL（1 项 P2）**，仅限第五批新增登记。不能据此宣告 REUSE-T01 全功能审计退出或生产算法合规。

## B5-P2-01：采样格绘制绑定了不对应的 UX-07

位置：`RobWork/doc/industrial-robot-design/reuse/functions/ui.json:84-88`，`ui.SampleGridRender.draw` 的 requirements 只有 `UX-07`。

`REQUIREMENTS.md:419` 的 UX-07 是“表单级应用设计修改时确认；仅改变会话显示的操作不弹出保存或冻结提示”。本行 source 则是 `HostView3DPreviewBackend.cpp:268-344`，实现格线、采样点逐点着色、选中点与三正交平面高亮环，既不决定应用事务也不弹出确认提示。因而该 requirements 无法追溯本行实际功能。

本行已有 designRefs 指向 `tasks/foundation/UI-T52.json`，但该真实契约 requirements 为 UX-02、ERR-01，acceptance 第2项明确“采样格线投递…SampleGridRender 增线段渲染”。应按实际呈现功能补正确的需求/设计/验收绑定（必要时追至领域显示需求），不能以 UX-07 单独代表这项绘制；decision/status 仍保持 pending。此为登记语义问题，不要求本批生产迁移。

## 已核查范围

新增共25条、6单元：drivetrain 8、policy 6、selection 6字节原语；kinematics 3独立FK参照；dynamics 1回放插值；ui 1宿主绘制。所有25条 decision/status 均为 pending，累计146条、closedFiles=0；没有以登记关闭库存或新增宿主模型写权限。

- 25条 source 的起止行与规范化SHA全部匹配；逐条读取完整区段。qualifiedSymbol 的域命名空间、匿名命名空间及 ByteWriter/ByteReader/SampleGridRender 类作用域已核。所有 inputs 全文件摘要匹配。新两个 catalog 的公共头、实现、目标CMake及列明测试摘要均匹配；本批引用的4个 auditEvidence 摘要匹配。
- 三域原语差异与源码相符：drivetrain/selection 显式小端，policy u32为大端并经 PolicyCodec canonicalF64；drivetrain保留-0位模式和流末空串守卫；selection读非有限、首错及1MiB串限；policy错误归类与文本范围仍归域。UTIL-BINARCHIVE 的标量直接写主机内存，string长度为格式化流 `<<`/`>>`，不能宣称等价冻结协议；候选登记未授权新codec平台。
- kinematics/test/KinFkFixture.hpp 的 rodrigues:380-397、mul4:408-419、referenceFk:429-518 是独立FK/几何Jacobian参照，NFR-COR-01 有明确独立对照依据。全目录搜索命中仅 test、gui_test、contract_test；真实 CMake 消费者为 kinematics_test/gui_test/contract_test 与 ui_test 中 KinEvaluationChannelTest。登记 targets 仅列已审 kinematics_test，并明确其他测试消费者仍待分别结项，未声称全消费图闭合，也未绑生产计算目标。不应以生产复用为由删除独立oracle。
- DynamicsReplayProjector::sampleAt:230-348 的空/越界nullopt、精确帧原值、双指针对齐和五量混合均实核。上游 LinearInterpolator:70-87 采用 `(end-start)/duration*t+start`、不提供回放越界/缺关节规则，差异登记诚实；std::upper_bound属于定位，不误报为平行数值核心。
- SampleGridRender::draw 的两个真实编译目标均在 ui/CMakeLists.txt 注册：sdurws_ird_ui_plugin（928）与 sdurws_ird_studio（1056）。RenderLines 的真实上游目标是 sdurw_opengl；其顶点转float、draw忽略回调alpha而用成员_alpha、push/pop GL属性均实核。产品使用double顶点、逐点色和24段三平面圈；HOST-LINES只作呈现候选，未扩展四HOST封闭例外或授予WorkCell构造权。
- upstream targets 实核：common/CMakeLists.txt:1/94 为 sdurw_common；rwlibs/opengl/CMakeLists.txt:1/76 为 sdurw_opengl。产品四计算库目标与各源文件注册均实核。除上列 UI 需求误绑，本批其余 requirements 与所登记业务角色无明显语义错配。

## 真实验证

| 验证 | passed | skipped | failed | 证据 |
|---|---:|---:|---:|---|
| task/docs/reuse 脚本 | 3 | 0 | 0 | task.log/docs.log/reuse.log；各exit.txt均0 |
| 冻结batch5-search检索重放 | 4 | 0 | 0 | search-results.json；search-1..4.stdout/stderr.txt |
| 新增source/input绑定 | 全部匹配 | 0 | 0 | bindings.json；source-excerpts.txt；check.log |
| 新catalog绑定 | 5源/测试记录全部匹配 | 0 | 0 | catalog-bindings.json |
| auditEvidence绑定 | 4 | 0 | 0 | audit-bindings.json |

四搜索退出码均0，stdout经路径分隔符归一、逐行排序后与冻结记录集合完全一致，stderr均空。重放脚本为 replay-search.ps1。区段/全文件摘要脚本为 check.ps1，规范化为UTF8去BOM、CRLF转LF、仅保留一个末尾LF。

NOT RUN：C++构建与公共API探测（本代理明确分工外，由另位验收负责）；GUI执行（本批只改治理登记，无生产交互改动）；全库DeepEvidence与全T01退出审计。治理脚本PASS只证明其机器检查通过，不代表semantic/function compliance。

本代理只写此TEMP报告及日志，未改实施树/上游/vcpkg，未提交、推送、合入或派发子代理。
