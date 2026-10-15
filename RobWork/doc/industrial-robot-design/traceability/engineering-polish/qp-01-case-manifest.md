# QP-01 六轴输入草案与移交清单

日期 2026-10-15；资产版本 six-axis-v1，状态为合成解析输入草案，未冻结为正式研发工程基线。原始资产见 [inputs.json](assets/six-axis-v1/inputs.json)、[URDF](assets/six-axis-v1/synthetic-6r.urdf)、[独立参考程序](assets/six-axis-v1/reference.py)。input SHA-256 `45d67b5f7050d669ede68f0cb67f4304c52d68aa34769b299591bb06494a2750`。

## 来源与可复用性

[29 数据集来源清单](golden-source-inventory.md)及 golden-integrity.json 记录 118 文件的实际哈希/size，全匹配。mdl-template-6r 与 mdl-urdf-import 用于模板/导入契约；kin-fk-analytic 提供六轴局部变换与独立位姿；dyn-two-link-analytic 仅二连杆解析物理参考；sel-catalog-golden 是合成目录测试，不是可直接采购的厂商目录。这些数据不能简单拼接后宣称真实六轴整机已验收。

本轮六轴几何来自仓内 six-axis FK 案，质量/质心/惯量由明确尺寸的合成长方体生成，未引入外部许可资产。URDF effort=100 只是字段测试值，不是驱动能力。无真实厂商参数、效率曲线或实测误差承诺。

## 输入约定

长度 m、角度 rad、质量 kg、惯量 kg·m²、时间 s。关节 origin 为父连杆局部平移；axis 为关节局部单位轴。六轴 Z/Y/Y/X/Y/X；TCP 在法兰系平移 [0.15,0,0.1]，绕 Z 旋转 π/2。惯量参考点为质心，主轴平行所属连杆系；移轴只输出独立参考，不冒充产品已消费。

安装世界位移 [0.2,-0.1,0.05]，地装 I、墙装 Ry(π/2)、倒挂 Rx(π)；重力世界系 [0,0,-9.80665] m/s²。合成 payload 0/2 kg 与工具参数分别列明。可缺/估算字段不以 0 替代，真实目录仍 missing。

## 实际产品路径

import-probe 调用真实 ModelImportMapper.mapUrdf：axes=6、errors=0、submittable=1；checkInvariants=0。RobotDesignCodec 编码后经 ProjectStoreFactory.createNew→DraftService.save→关闭→只读 open→tryLoad，六轴草稿载荷 5236 个十六进制字符逐字节一致，projectId 不变。有效工程在证据包 reference-draft-v5.rwdesign/。

探针自行装配资源快照（真实计算 SHA-256），不代替完整 io 向导；尚未执行该草稿的正式 apply/编译/运动学评估，也未创建基线只读＋方案分支结构。两次早期失败均因探针版本字段使用 1 而非 kSchemaVersionCurrent；首版还缺时间戳，原日志保留，修正契约输入后通过；不作为产品缺陷登记。

宿主 modeling-tour 导出的 mtour-export.irdbundle 另存证据包，走查模型已编辑为七轴，因此不是本六轴资产的正式导出。只读重开探针成功不等同所有 GUI 新建/导入路径通过。

## S1～S8 状态

| 场景 | 本轮已有 | 剩余验证 |
| --- | --- | --- |
| S1 基线重建 | 六轴 URDF/输入、真实导入与草稿重开、6 组独立 FK 对照（含 2 组六轴） | 正式 apply/编译、模型三维、基线分支资格 |
| S2 范围改型 | 合成输入和场景变更说明 | Must 区域/任务、真评估/覆盖/结果着色 |
| S3 工具负载 | 非零 TCP、合成 0/2kg、三种安装参考 | 正式动力学与编译链逐值对照 |
| S4 节拍 | 输入草案 | 完整任务循环、轨迹/驻留/时间参考 |
| S5 器件替换 | 既有合成目录来源盘点 | 真实目录、四象限与回填复算 |
| S6 方案比较 | 既有 OPT-B/D 契约盘点 | 宿主 run 服务、至少两个真实候选/采用 |
| S7 不足与修复 | 惯量误判复现、来源缺口标记 | 正式失败 UI 与修复后恢复 |
| S8 保存/恢复 | 草稿保存、只读重开、宿主撤销/重做走查 | 取消/切换迟到回调、分支/包跨机器 |

## 独立参考复现

在仓库根：`python RobWork/doc/industrial-robot-design/traceability/engineering-polish/assets/six-axis-v1/reference.py --repo . --out <输出目录>`。仅 Python 标准库，无产品库调用。6 组 FK（4 个平面二轴、2 个六轴）与仓内 expected 最大平移误差 0 m、旋转元素误差 0；另产出 9 组六轴安装姿态参考与 8 个部件物性。

移交只需仓库黄金数据＋本目录 3 个输入/程序文件；工程草稿载荷内含设计，URDF 无外部 mesh。真正跨机器导入/正式资源固化/基线分支仍未执行。QP-01.03/.04 未整体完成，不以合成资产替代实机资格。
