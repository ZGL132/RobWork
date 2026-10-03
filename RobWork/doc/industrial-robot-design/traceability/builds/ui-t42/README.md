# UI-T42 建模面板发布闭环与联动准备批次 实施留痕

- 任务：UI-T42（UI-T41 批次 D 承接改立＋审核修正 R1＋F-465/F-466 消账）
- 分支：`ui-t42`（基点 redesign-main@d00795f9＝UI-T41 合入收尾提交）
- 任务契约：`tasks/foundation/UI-T42.json`（validate-task PASS）
- 需求语义：SA-15/UX-07（确认流）、MDL-20（规范包回读）、ARC-04（根身份稳定）、ERR-01（诚实呈现）

## 交付内容

1. **审核修正 R1（恰一根不变量）**：导入 URDF/Xacro/规范包与模板重种子四路径保留已回填 rootObjectId——requirements 域 F-461 同型缺陷的建模侧预防。
2. **D1 清单化确认**：export/import-package 确认对话框清单化（对象计数/替换明示/原子替换语义/未应用编辑警示/应用语义如实区分）＋默认钮 No（用户未明示即不执行）。
3. **D2 发布即所见（最小承接）**：导出后包端口回读校验（manifest/SHA-256 逐条目/解码全链），失败拒绝成功话术；运行时加载校验（编译链）依赖 runtime 绑定面——摘要诚实注明归后续任务链。
4. **D3 联动准备**：SelectionSource 词表＋PanelSelectionState 选中来源会话态；视口零触碰（UI-T33 前置）。
5. **F-465 消账**：四处过期尾缀『（入口随后续批次装配）』删除。
6. **F-466 消账**：isAssembledModelingCommand 判定迁 flows 层（ModelingCommandFlows 同词表）经装配门面头出线（UiPlugin 零第二词表只转发——R-2 合规）＋gtest 双向对账。

## 证据清单

| 文件 | 内容 |
| --- | --- |
| `build-integrated.log` | 10 受影响目标零错误（build exit=0） |
| `tests/t1.xml` | sdurws_ird_modeling_test **294/294**（新增：批次D 三用例＋F-466 一致性用例） |
| `tests/t2.xml` | 契约 17/17 |
| `tests/t3.xml` | gui 9/9 |
| `tests/t4.xml` | ui 239/239 |
| `tests/t5.xml` | ui 契约 32/32 |
| `tests/t6.xml` | ui gui 67/67 |
| `tests/ird-test-report.json` | testkit 汇总 |

## 诚实边界（验收者注意）

- D2 运行时加载校验（编译链 FK 对照级）依赖 runtime 绑定面——本批以包端口回读校验兑现最小承诺，摘要文案显式注明"运行时加载校验归后续任务链"，不虚构加载成功。
- D3 只落状态模型（词表＋lastSource），**零触碰视口**——UI-T33（View3D 阶段 B）前置；D4 WorkCell 反向导入对齐登记 WP-13-T17（R2/D）零实施。
- 规范包 ObjectId canonical 形态＝`obj-<32 小写 hex>`（无 # 前缀，core Identity.hpp §5.1）——测试夹具曾误用 `#` 前缀（tryFromCanonical 恒 nullopt），已修正并留此备注供后任参考。
- ui_gui_test 负载敏感用例见 F-464（复现失败先查负载）。
