# 70项当前结果：第二批快照（非最终验收）

每行计算、窗口、真实三维独立四态。NOT RUN表示该行完整预期尚未执行；已通过子项在说明中列证据，不把子项成功当整行通过。算法/契约套件已真实执行（REPORT-stage2.md），但未逐条审阅其断言与本矩阵每个预期的对应关系，因此不批量转为PASS。源码确认的入口缺口见ISSUES-stage1.md，尚未完成正式宿主可达性复核。

| ID | 功能 | 计算 | 窗口交互 | 正式三维 | 本批证据/缺口 |
| --- | --- | --- | --- | --- | --- |
| K01 | 当前 FK 与双参考系 TCP | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K02 | Jacobian 完整显示 | NOT RUN | NOT RUN | NOT RUN | F06：完整Jacobian呈现缺口；需逐元对照。 |
| K03 | 奇异值、条件数、可操作度 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K04 | 逐关节裕量与最差关节 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K05 | 设备和 TCP 选择 | NOT RUN | NOT RUN | NOT RUN | F05：设备/TCP用户入口与正式宿主可达性待补。 |
| K06 | XYZ＋RPY 单目标输入 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K07 | 目标单位切换 | PASS | NOT RUN | NOT RUN | F02红绿已验证保物理目标、角度/区域往返与溢出；F05用户切换入口仍缺。 |
| K08 | 非法/空/非有限输入 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K09 | 三类多初值策略 | NOT RUN | NOT RUN | NOT RUN | F04：策略草稿未消费，真实保存反例待补。 |
| K10 | 残差与限位硬过滤 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K11 | 碰撞硬过滤 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K12 | 逐轴去重 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K13 | 四键稳定排序 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K14 | 搜索未果与不可达分离 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K15 | 候选检查器与双击 | NOT RUN | NOT RUN | NOT RUN | F06：候选q/J检查器未完整呈现；双击与真三维仍待验。 |
| K16 | 需求点只读消费 | NOT RUN | NOT RUN | NOT RUN | F10：任务点双击接线待补；只读消费全部字段待逐项验。 |
| K17 | 批量三态报告 | NOT RUN | NOT RUN | NOT RUN | F12：正式归档未闭环，三态与内存结果不等同交付。 |
| K18 | 多必验工况覆盖 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K19 | 后台进度/取消 | NOT RUN | NOT RUN | NOT RUN | F07：实际取消入口/响应待补。 |
| K20 | 跨项目与迟到回执 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K21 | 只读与无项目门控 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K22 | 区域参考系与独立TCP | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K23 | 冻结采样与身份 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K24 | 位置与姿态覆盖分母 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K25 | 混合数据不足/零样本 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K26 | 区域操作入口与结果 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K27 | 缺检测器/几何错误 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K28 | 共享策略一致性 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K29 | Home/Zero与命名姿态 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K30 | Jog | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K31 | Playback | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K32 | 失败点三维 | NOT RUN | NOT RUN | NOT RUN | F13：正式宿主真实对象/屏幕叠层尚未执行。 |
| K33 | 薄弱区三维 | NOT RUN | NOT RUN | NOT RUN | F13：正式宿主真实对象/屏幕叠层尚未执行。 |
| K34 | 碰撞对象高亮 | NOT RUN | NOT RUN | NOT RUN | F13：正式宿主真实对象/屏幕叠层尚未执行。 |
| K35 | 最差裕量三维 | NOT RUN | NOT RUN | NOT RUN | F13：正式宿主真实对象/屏幕叠层尚未执行。 |
| K36 | 筛选与最差项排序入口 | NOT RUN | NOT RUN | NOT RUN | F11：最差排序用户入口待补。 |
| K37 | JSON/CSV导出 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K38 | 导出失败原子性 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K39 | 批量复算 | NOT RUN | NOT RUN | NOT RUN | F11：批量复算范围待补。 |
| K40 | 随机均匀采样 | NOT RUN | NOT RUN | NOT RUN | F01默认区域盒按钮已红绿并生成真实采样；随机/碰撞全矩阵待验。 |
| K41 | 关节网格采样 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K42 | 采样后台与进度取消 | NOT RUN | NOT RUN | NOT RUN | F07：探索采样同步阻塞，需大预算窗口响应反例。 |
| K43 | 七种指标着色 | NOT RUN | NOT RUN | NOT RUN | F08：七指标着色入口待补。 |
| K44 | XY/XZ/YZ投影 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K45 | 散点/包络/标签图例 | NOT RUN | NOT RUN | NOT RUN | F08：散点/包络切换入口待补。 |
| K46 | PNG导出 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K47 | 选样本与三维回写 | NOT RUN | NOT RUN | NOT RUN | F08：鼠标样本选择与真三维回写待补。 |
| K48 | 采样碰撞与策略跳转 | NOT RUN | NOT RUN | NOT RUN | F07：采样策略及碰撞真消费待验。 |
| K49 | 外包络方向/默认180 | NOT RUN | NOT RUN | NOT RUN | F09：外包络正式用户入口待补。 |
| K50 | 外包络后台取消与标识 | NOT RUN | NOT RUN | NOT RUN | F09：外包络后台取消/报告待补。 |
| K51 | 位姿计划预览 | NOT RUN | NOT RUN | NOT RUN | F09：位姿计划正式用户入口待补。 |
| K52 | 位姿后台报告 | NOT RUN | NOT RUN | NOT RUN | F09：位姿后台取消/报告待补。 |
| K53 | 显示单位入口 | NOT RUN | NOT RUN | NOT RUN | F02已有文本重投影红绿；F05正式用户入口与全显示/导出仍待验。 |
| K54 | inch/grad/turn子项 | NOT RUN | NOT RUN | NOT RUN | F14：T17当前为R2预留；发布状态待确认，不能隐去。 |
| K55 | 完整配置编辑 | NOT RUN | NOT RUN | NOT RUN | F04私有面9项红绿已通过：策略/数值完整确认、初始基线和非法输入；正式宿主完整编辑入口仍待验。 |
| K56 | 保存取消重开 | NOT RUN | NOT RUN | NOT RUN | F04面板取消/失败后重试及真实临时QSettings重开已通过；正式宿主项目切换/恢复未验，整行不转PASS。 |
| K57 | 运行身份和过期提示 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K58 | 策略权威 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K59 | 设默认TCP | NOT RUN | NOT RUN | NOT RUN | F05：先补非默认TCP选择，再验证真实提交/撤销。 |
| K60 | 设默认设备 | NOT RUN | NOT RUN | NOT RUN | F05：默认设备入口可达性及真实提交待验。 |
| K61 | 空态/缺项/失败/过期 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K62 | 诊断定位及比较字段 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K63 | 键盘/复制粘贴/选择 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K64 | 真网格与TCP坐标架 | NOT RUN | NOT RUN | NOT RUN | F13：尚无正式宿主真实mesh/TCP坐标架证据。 |
| K65 | 三维导航与显示 | NOT RUN | NOT RUN | NOT RUN | F13：导航与遮挡/拾取尚未实际执行。 |
| K66 | DPI/缩放/布局恢复 | NOT RUN | NOT RUN | NOT RUN | F03面板960×720及滚动到底按钮已红绿；宿主布局与高DPI仍未验。 |
| K67 | 响应与耗时隔离 | NOT RUN | NOT RUN | NOT RUN | F14：标准负载及>=15有效样本P95未测。 |
| K68 | 黄金对照与确定性 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K69 | 修改与失效边界 | NOT RUN | NOT RUN | NOT RUN | 原有套件已执行；本行全部预期与正式窗口/三维尚未逐项核验。 |
| K70 | 归档重开与结果追溯 | NOT RUN | NOT RUN | NOT RUN | F12：保存重开和正式快照/结果绑定待补。 |

K07计算PASS仅指本行物理量保持的6类红绿断言，证据为stage1/raw/kinematics_gui-expanded-red.xml与kinematics_gui-green1.xml。用户选择制式仍待补，不是整体功能通过。非三维适用项在后续逐项界定适用性后可记SKIP并说明，不在本批猜测。所有者尚未收到全功能完成的验收请求。

第二批F04只更新K55/K56的已通过子项，不把临时配置适配器当正式宿主；完整范围、红绿及其余缺口见REPORT-stage2.md，stage1证据只读保留。
