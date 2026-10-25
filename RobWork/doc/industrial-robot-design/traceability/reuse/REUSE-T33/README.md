# WP-12 / REUSE-T33：modeling 直接复用收口（一）——RPY 反解唯一实现换上游

起点 `2f0ee6dcc581da0e0f18ffacbdfa60080af5b062`，redesign-main 本地串行。所属 migration §13 **WP-12**，DTB **WP-E**。本批**实际落地 1 项直接迁移（rotationToRpy→adapter，MATH-ROT verified 面）＋3 文件全行铺面＋1 组扰动断开**；**子包 A（Import.cpp 惯量旋转）与 D（Template.cpp 占位圆柱）因 UI-T53 closures 冻结面受阻，未落地**（见下），实施自查完成、独立验收 **NOT RUN**，任务 ready。无框架/vcpkg/公共接口/CMake/permissions（grant 登记零新增——三个测试文件经查无受限上游类别）变更，无远程操作。

## CLOSURES 冻结阻断（A/D 未落地的原因——所有者裁决项）

`reuse/closures.json` 的 UI-T53 closure（migrationCommit `a1393ad4a`）将 modeling.rpyToRotation/rpyToEaa 两 closed 行的 acceptedPaths 文件（**Import.cpp、Template.cpp、TemplateTest.cpp**、RpyMath.hpp、证据文件及其绑定）逐字节冻结（reuse-contract.ps1:339-351：与 migrationCommit 状态 Digest 比对，任何内容变化即 CLOSURE FAIL）。A（rotateInertia 换 InertiaMatrix）与 D（占位圆柱换 EAA）的生产改动、乃至其测试文件（TemplateTest.cpp/ImportTest.cpp）的任何修改均被冻结面拦截。候选实现与全套 oracle/断开证据**已完成并留存本目录**（见"候选证据"节），移交 [REUSE-T35](../../../tasks/REUSE-T35.json)（planned，吸收 A/D/C-lib）；解冻须所有者对 closed 行启动验收后推进（超实施者权限，blocked 登记）。

## 本批落地内容（C 插件面）

- `PanelModel.cpp` 逐字同款 `rotationToRpy` 副本删除（评审缺口 **G1**——同单元双轨）：统一消费 `rpyview::rotationToRpy` 唯一实现。
- `RpyPresentation.hpp` 反解改直接消费上游 `rw::math::RPY(R)`（RPY.cpp 外联符号，catalog MATH-ROT verified 面；probe-r6 近奇异/精确±π/2 旋转往返正确在案），输出按上游 (z,y,x)→产品 (roll,pitch,yaw) **反向换序**；**万向锁确定性 roll=0 保留**——pitch 取上游奇异支恰 ±π/2、yaw 用与原实现同式的配角公式 atan2(−R01,R11)（上游两支 x 角取号相反，不宜换算）；上游 1e-5 带内呈现规范化 roll=0 形式，带内重建 O(带宽)（≤2e-6）如实登记。宿主侧消费点（HostMigrationProviders/JointFormSession/ModelingPanelWidget）零改码。
- 失实注释改写（G5 范围内两文件）；"冒烟 header-only 禁外联符号"纪律经 WP-01 冒烟供给修复＋REUSE-T11 RpyMath 生产先例证伪后废止（G6：modeling 库面直接证据；插件层仅集成实证）。
- 插件链接面（G4）：RPY.cpp 外联符号经 STATIC 库 `$<LINK_ONLY>` 传递——集成构建 sdurws_ird_modeling_plugin/app 链接成功实证；独立冒烟树无插件目标（T29 README:24），不外推。

## 验证证据（本目录，全部真实执行于缩减树）

| 项 | 文件 | 结果 |
| --- | --- | --- |
| 全量回归（480 项＝基线 479＋rpy oracle 新增 1） | final.xml / final.log / final-report.json | 480 passed / 0 failed（含受影响 Golden/等价/命令流回归） |
| 契约测试 | contract.xml / contract.log | 17 passed / 0 failed |
| rotationToRpy oracle | rpy-oracle.xml/.log | 1 passed（通支网格往返＋上游 RPY 位级 EXPECT_EQ 对照＋精确 ±π/2 万向锁＋近奇异带） |
| 断开（rotationToRpy） | rpy-disconnected.xml/.log＋rpy-mutated.hpp | failures=1 / exit=1（通支 roll×2） |
| 恢复复绿 | restored-green.xml/.log | 0 failures / exit=0 |
| production 证据 | production-rpy.json | ird-reuse-production/1 四面＋绑定 |

## 候选证据（A/D，未落地——移交 T35/所有者）

import-rotated-*（rotateInertia 上游返回值扰动断开必红＋rotateinertia-oracle 字面期望）、template-oracle/template-disconnected（EAA 双向量构造字面期望＋吞轴带/近极点边界；含一次无效突变 ux×0.9 的废弃重做留痕）、canonicalbridge-fold/dhconvert-order/dhconvert-disconnected（C-lib 值代数突变预演）。这些证明对应候选实现正确性；候选代码文本见各 *-mutated.cpp/hpp 的反向（即 T35 待落地形态）与 T35 契约 note。

## 评审缺口（G1–G8）最终处置

| 缺口 | 处置 |
| --- | --- |
| G1 PanelModel/RpyPresentation 逐字同款 | **本批收口**（副本删除，唯一 rpyview 实现） |
| G2 CanonicalBridge 第三份同款 | 移交 T35（本批两侧原样保留，同规不失配） |
| G3 requirements 第三份同族反解 | 移交 requirements 单元批次（T35 note 登记） |
| G4 插件链接面 | STATIC `$<LINK_ONLY>` 集成实证；冒烟树无插件目标如实登记 |
| G5 失实注释 | RpyPresentation/PanelModel 本批改写；Import/Template/DhConvert/CanonicalBridge 随 T35 |
| G6 证伪证据链 | modeling 库面直接证据＋插件集成实证，范围不外推 |
| G7 allowedFiles 模式 | T27/T28 正列举＋forbiddenFiles 沿 T29 |
| G8 T07 性能前科 | 归 REUSE-T34（已实测：三规模 1.94~2.60× 退化→不落地，交所有者裁决） |
| **新增：closures 冻结（A/D）** | **blocked 登记——所有者对 UI-T53 closed 行启动解冻/验收后，A/D 候选与证据随 T35 落地** |

## 边界与不声明

- 本批不声明 A/D 候选已迁移；候选代码未进生产树。Import.cpp/Template.cpp/TemplateTest.cpp 与 T27/T28/T11 全部证据文件保持基线字节状态（工作区零触碰）。
- 声明行（*.decl）仅承担行覆盖归属，不冒充语义审计。
