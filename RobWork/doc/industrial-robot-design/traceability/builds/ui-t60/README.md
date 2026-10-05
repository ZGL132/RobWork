# UI-T60 构建与测试留痕（位姿集/传动编辑页批次——F-497 余项收尾）

分支 `ui-t60`（基点 redesign-main@c58af6d6）；任务契约 tasks/foundation/UI-T60.json；
DTB §2.11 WP-10-T60 行＋§5.1 验证口径行；units/modeling.md v0.38；units/ui.md v1.72
§13 UI-T60 行。

## 交付物

- 域原语（Parts.hpp/.cpp）：位姿集两原语（upsert/remove——mergeNamedPoseEntries
  单一合并复用；草稿句柄创建；保留键拒绝；KeyNotFound 表尾追加；用户集拷贝
  构建）＋传动三原语（ratio/摩擦三元/力矩对＋公共前置；SourcedValue
  UserProvided；三向量按关节表对齐）。
- 面板承载：编辑页签堆栈 4→6 页（位姿集/传动）；构型表行随关节表重建（零关
  节守卫）；stagePoseSetConfigEdits 确认值同步（保存条目"净取权威"）；fv 量
  纲缺席诚实直投；refreshEditPages 分派＋无选中路径基线驱动；L-7 两页。
- 随批治理：F-519①（t-world-frame 值断言）②（剪贴板哨兵）③（注释表述）
  ④（缩进）＋F-515（冗余调用消除）＋F-517 S1（setWritable 动态窗口用例）
  ＋DTB §5.1 验证口径行（F-517 S2/S3＋F-518 防复发登记）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响目标） | 零错误 | 会话执行记录 |
| 独立冒烟（gate-all standalone 树） | 构建零错误；modeling_test 273/273（271+2）＋modeling_gui_test 35/35（31+4）全绿 | gate-all 会话执行记录＋冒烟树实测 |
| sdurws_ird_modeling_test（集成） | 332/332（新增 2） | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test（集成） | 38/38（新增 4） | modeling-gui.log／.xml |
| modeling_contract／ui_test／ui_contract／ui_gui／requirements_gui | 17／246／32／78／44 全绿 | 会话执行记录 |
| ird_gates | 引擎汇总 101 命中＝基线存量集口径；归一化多重集与 UI-T59 基线集 **diff=0**；命中集零项涉改动文件 | ird-gates-branch.log／ird-gates-branch-hits-normalized.txt（比对基线＝builds/ui-t59/ 同名文件） |
| validate-docs／validate-task（UI-T60.json） | PASS（20 units/238 task files）／PASS | 会话执行记录 |

## 诚实边界

- 传动 coupling（R2/MDL-21 阶段门禁）与目录回填（SEL-10 命令写入）不在本页
  编辑面——页说明行如实划界。
- 传动 fv 量纲（N·m·s/rad 或 N·s/m）不在 core 词表——F-510 族诚实边界：
  数值面原样直投零换算，标签注明真实单位，不虚构量纲 token。
- 位姿集/传动对象在模板会话缺席——新建经草稿确定性句柄（§5.2 临时句柄纪
  律，提交时回填正式身份），不伪造持久身份。
- 保存条目的构型"未编辑行取权威"：权威侧 NotProvided（空基线）＋用户未编辑
  ＝分量缺失诚实拒绝（不虚构零值）——构型表零位姿种子行为全零有值，正常路
  径不触发。
- gui 套件口径＝原生窗口平台（gate-all 同口径）。
