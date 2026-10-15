# QP-00 历史缺陷与修复队列

日期：2026-10-15；只读账面快照 820 条，open 502（blocking 1/major 21/minor 8/suggestion 472）。此表是 findings.json 的追溯视图，不是第二份权威缺陷账；没有自行改 fixed。全部 22 个开放高等级条目列入索引，未复现者明确未执行。

| 编号 | 等级 | 账面问题 | 契约文本引用（不等于已修） | 本轮证据/边界 |
| --- | --- | --- | --- | --- |
| F-495 | blocking | 需求三维区域采样结果状态着色未闭环（宿主审核 P1）：当前仅有包围盒＋采样格线骨架，缺采样点通过/失败/未采样三态、达标颜色区分、覆盖率颜色映射、校验项点击定位采样点、颜色图例；颜色必须由统一 UI 词表提供（建议语义：绿=通过/红=未通过/灰=未采样/黄=警告/蓝框=当前选中），禁止三维后端各自硬编码 | FIX-T1, FIX-T2, UI-T64, UI-T65, WP-13-T19, WP-15-T15, WP-15-T16, WP-21-T02, WP-21-T03, WP-21-T04, WP-21-T06, WP-21-T07, WP-23-T02, WP-23-T03, WP-23-T04, WP-23-T05, WP-23-T06, WP-23-T07, WP-23-T08, WP-23-T09 | 已有 UI-T64/T65、FIX-T1 交付记录；本轮 tour 未触真实评估点着色，owner 宿主闭环未验 |
| F-554 | major | 宿主需求就绪门控/就绪绑定的五处计算点全部以空 CheckContext（closureRefs 空）调用域检查器——首应用前根引用表四槽未回填（R0 闭包核对天然跳过）缺陷潜伏；首应用后四槽被 allocateNew 回填（槽有值），R0 以空闭包判定恒「根引用表槽指向的对象不在修订闭包中（悬空）」→Blocking→applyDraftDisablement 恒禁用＝应用草稿恒灰（首应用成功后不论做什么）。五处＝applyDraftDisablement 门控/wireRequirementsSession 首刷/setPostEditAction 编辑后重估/refreshRequirementsFromSession 重导线/空根初始化路径；面板校验页同源同误（呈现与门控一致地错误——自洽但均失真）。域检查器/域逻辑零缺陷（测试面传入正确闭包全绿） | FIX-T1 | 主线含 398d39a7 F-id 回归；本轮 ui_test 运行证据见矩阵，未把旧账自动翻牌 |
| F-557 | major | 命令被处理器拒绝后的拒绝反馈以空禁用键调 resolveText——WorkbenchContent submitCommand 拒绝分支 resolveText(a.disableReasonKey) 在处理器拒绝（非可用性禁用，禁用键为空）时按 UiText fail-fast 设计抛 invalid_argument，异常穿透 Qt 事件循环直达 StudioMain 顶层 catch＝红叉对话框＋进程退出（return -1）。同面第二处＝refreshCommandEnablement 的按钮 tooltip。抛本身是 UiText 缺键 fail-fast 的正确设计——缺陷在调用面未处理「处理器拒绝无禁用键」的合法空态 | FIX-T1 | 主线含 398d39a7 F-id 回归；本轮 ui_test 运行证据见矩阵，未把旧账自动翻牌 |
| F-558 | major | requirements.pick-feature 的会话闭包引用缝在产品装配序下恒为空——assembleView3DGateway（initialize 装配段，进程启动拍，产品全文件唯一 bindView3DSeams 调用点）执行时任何项目均未打开、m_lastStoreAdapter 恒空，seams.sessionClosureRefs 保持缺省空 function 且此后无任何再绑定点（wireRequirementsSession 重导线的是面板就绪上下文、不触三维缝）——RequirementsCommandFlows 拾取流 if(view3d->sessionClosureRefs) 恒 false→CheckContext.closureRefs 空→域闭包核对恒 miss→拾取写回 100% 被误判「悬空」拒绝。F-554 空闭包家族第六处（前三层修复的 currentRequirementsCheckContext 不覆盖此缝）；F-550③ 骨架呈现让场景出现可拾取 drawable 后，该流从结构性不可达变为可达即踩 | FIX-T1 | 主线含 398d39a7 F-id 回归；本轮 ui_test 运行证据见矩阵，未把旧账自动翻牌 |
| F-559 | major | 宿主三维投影三层缓存（UiPlugin m_reqMarkers/m_reqBox/m_reqGrid）不在任何项目关闭拍清理——teardownSharedSurfacesForClose 清了选择/高亮/树/检查器/发布桥/NameMap/编译端口八类，同族（会话呈现面缓存）的三预览缓存漏清；ird 项目关闭经 UiSessionController Draining→syncDomainModulesToContext 不经过宿主框架 WorkCell 关闭拍→网关 onSceneCleared→removePreview 不触发。跨项目串数据：A 项目呈现区域框/采样格/标记→关闭→打开 B→首个 markers sink 合并投递时以 A 残值（m_reqBox/m_reqGrid）重挂——B 项目三维出现 A 的框/格/着色（与 F-550② 修复「不残留旧呈现」纪律同族漏拍） | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-560 | major | 删除最后一个区域后三维框/格/着色层永不清除——RequirementsPanelWidget renderRegionPage 区域集合为空分支只清面板标签文本、不向 m_regionPreview 投递空投影；UiPlugin 侧 m_reqBox/m_reqGrid 原值保留，且后续任一 markers sink 合并投递都以旧值重新挂回（update.boxOutline=m_reqBox 原子替换语义在「空集合」态无生产者）。触发：唯一区域被删除或撤销到无区域→三维残留直到关项目（叠加 F-559 则跨项目残留） | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-561 | major | 区域参考系不可解析分支只清框层不清格层——refResolved=false 时 box 置 nullopt（框清除）而 m_reqGrid 保持上一区域旧值并合并整组投递，logDev 文本自称「框层清除」实际格层（骨架线＋着色采样点）跨区域/跨参考系漂浮——「半新半旧」形态在原子替换语义的自家实现分支内复活。触发：选中区域切到参考系不可解析的区域（引用对象被删/未发布），或区域 refFrame 从 World 改指对象系且宿主无发布 WC | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-562 | major | 区域框 12 棱边表与八角点二进制角序不符——生产者角序契约（PanelRegionModel「索引 i 的 bit0→+sx、bit1→+sy、bit2→+sz」）下 index1=(+x,−y,−z)、index2=(−x,+y,−z) 相差两位＝面对角线，而渲染边表底/顶面按「0→1→2→3 循环」假设（格雷/循环序）连边：实际画出 4 条 x 向棱＋4 条面对角线（上下两面呈蝴蝶结交叉）＋4 条立柱，y 向四条棱（0-2/1-3/4-6/5-7）完全缺失——线框非方盒，任何区域预览必现。边表注释「角序与 requirements 侧同款零重排直投」与实现相悖 | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-563 | major | HostCompilePort digest256 十六进制解析只防长度不防字母表——hex.size()==64 守卫内逐字符 std::stoi(...,16)，64 字符含非 hex 字符时抛 std::invalid_argument 且调用链（draft.apply 命令处理器→compileWorkCellAndDwc）无 try/catch→异常穿透 Qt 事件循环直达 StudioMain 顶层＝红叉对话框＋进程退出。注释自称「解析失败＝清单数据违约……以全零摘要如实投递、由编译器 S2 摘要复核拒绝」但实现无任何 catch 支撑该口径——防御姿态自相矛盾（若上游无条件可信则长度守卫也多余） | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-714 | major | 框架示例插件 RWSimulatorPlugin 在 fresh 树全量构建下编译失败——windows.h 的 min/max 宏污染 rw/math Math.hpp（warning C4003→error C2059），主树存量构建树同样无该产物（既有缺位非本批引入）；任何 fresh 树/CI 全量构建都会在此中断后续目标构建 | FIX-T1 | 框架无目标全构建未执行；本轮仅 industrialrobot 目标＋studio，不覆盖此示例 |
| F-795 | major | drivetrain `DriveTrainCodec.cpp:161-169` getString 流末空串形式 UB：require(len) 对 len==0 恒过（:103-112 仅查 m_pos+n>size），pos==size 时 &m_bytes[m_pos] 为 vector 越界取址 | FIX-T1 | 70f08cfcca8f30237d8d923f0d0b3d1d3f8ffffc 已修且合入；独立验收 E2-DT-20261015；本轮未重演 Debug 必红 |
| F-798 | major | trajectory `Sequence.cpp:334-348` 游标失效分支产一个 Transfer 占位（ptpGeometry=nullopt）后 continue 丢站内容——该站任务点/驻留（:429-441）与 retract（:443-453）均不产出，failures 为空仍 Ok，任务点静默跳出最终轨迹与 §7.6 失败定位语义相悖 | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-801 | major | io `Csv.cpp:261-270` next() 回退槽先于种子区出队：无标识文件首行孤立 CR 被 :1178-1186 peek 读入回退槽、:1225 seed 后该字节先于首行种子出队——字节序错乱（静默数据损坏/行界重组，2ba52bb4 重写未触及该序） | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-802 | major | io `Csv.cpp:622-628/588-594/1457-1462` 行首非法 UTF-8 被吞：parseField 首字节 -2 置 encodingError 后返回 Eof，nextRow 误判「干净结束」（m_rowBytes==0），交付遍编码致命检查位于 nextRow 返回 true 之后不可达——坏文件以 ok 收场并静默丢行，违反「编码错误恒致命稳定拒绝」 | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-803 | major | io `Json.cpp:884` 数值溢出边界差一：`if (magnitude > 308)` 应为 >= 308——mag==308 且 from_chars result_out_of_range 时必为溢出（1e308 等可表示值不会进入此分支），当前静默置 0.0（:889，数据损坏面） | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-806 | major | runtime `Codec.cpp:1267-1289` parseNameMap 映射级复核仅相邻比对（prev.fullName==curr.fullName），(objectId,scope) 唯一性完全未检（同 objectId 异 localName 条目不相邻）——错误解析不报错＋恢复路径 emplace 静默保留首条 | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-811 | major | selection `CombinationCheck.cpp:1713-1727` 效率降级匹配条件（combinationId/jointId/!efficiencyApplied）漏 caseId 过滤（对照同函数 :1622-1625 mfact 查表带 f.caseId==caseId，复制漏改形态）——任一工况的效率降级污染该组合全部工况的格 | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-812 | major | selection `CombinationCheck.cpp:1312-1338` aggregateCaseCell 格聚合只消费传入轴记录，rec.gaps 三类缺口（workpoint-missing :1630-1635/candidate-missing/efficiency :1719-1725）不进格判定，格级回落仅 inertia-ratio（:1744-1748）与移动轴范围外（:1737-1741）——资格矩阵格误标 Feasible/"pass" 而记录躺着数据缺口，与卡 §9.4 矛盾 | 尚未匹配专属后继 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-813 | major | selection `CatalogValidation.cpp:539/577/672/711/745` 行级 colIdx.at 硬查契约列名，唯一门控 checkTableSchema（:470-505）只比对表头 vs 字典——字典漏/改契约列时 .at 抛未文档化 std::out_of_range（既非报告返回也非 schema 级异常），schema 门与行级键面不一致（:527 注释自称「schema 已通过」） | 尚未匹配专属后继 | 本轮未定向复现；基线测试通过也不代表本条已修 |
| F-814 | major | selection `Screening.cpp:104-108` 硬筛选阈值 exceedsLimit 为纯带符号比较（actual > limit + tol，无幅值/符号校验）——违反 FeasibleSet.hpp:126-127 四象限幅值比较契约，负工作点静默通过全部能力判据，筛选层判通过、指标层出负裕量，输出自相矛盾 | FIX-T1 | 源码 exceedsLimit 仍为 actual>limit+tol；仅静态核实，未执行四象限反例 |
| F-815 | major | reporting `Builder.cpp:671` 墙钟 computedAtUtc（system_clock::now）入 CurrentnessSnapshot，ReportCodec.cpp:840-845 canonical 编码首字段即 writeTime(w, c.computedAtUtc)、:1366 contentIdentity＝Full canonical 字节 SHA-256——墙钟直接入身份，同一数据集两次构建身份必不同，破坏「同内容重建身份一致」幂等判定基础（§4.4 排除列/D-04） | FIX-T1 | 源码 computedAtUtc 进入 canonical/contentIdentity；仅静态核实，未执行两次构建反例 |
| F-816 | major | reporting `Builder.cpp:1000-1070` 章节冻结映射（renderHint/order/selected/sourceObjects/sourceResults/currentness/eligibilityNote）漏 diagnostics 写点——section.diagnostics（ReportModel.hpp:826「章节诊断（可空集）」）全文件零写点、仅并入报告级 diagMerger（:1067→:1166）：章节诊断恒空、不进 section 身份（仅诊断不同的投影得相同身份），HTML/JSON 章节诊断表永不出现 | FIX-T1 | 本轮未定向复现；基线测试通过也不代表本条已修 |

## 已复现但历史等级为 minor 的数值问题

F-784：真实 runtime Release 库对小尺度非对称惯量误放、大尺度容差内惯量误拒，双向探针见 qp-02-model-truth.md。后继 RT-T16（DTB WP-06-T16），仅 ready 未实施，不重复新建 finding。

## 首批处理顺序

1. WP-01-T05：22 门禁误分类，已实施/自测，待独立验收；不给性能测试通用跨域许可。
2. RT-T16：F-784 相对惯量判据，两入口一致、对称化后严格 SPD；待独立实施上下文。
3. QP-07.01 服务接线缺口：先查已有宿主装配后继，再立 optimization/ui 接线契约，不能因面板可见就认为能计算。
4. QP-00 宿主 auto 断言老化与原生 JSON 监听器缺口：后继分别界定 UI/测试留痕允许面；不修改本轮唯一产品契约以外源码。
5. F-814/F-815：下一轮先建立可运行反例，不凭静态行号消账；F-795 只对账，不再重复修复已合入守卫。

上表部分历史条目可能已被更早修复覆盖；缺修复 SHA/独立验收/当前复测的条目保持待核对。现有高等级开放数不能直接等同当前真实缺陷数，也不能仅因套件通过批量关闭。
