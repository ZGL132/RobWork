# WP-13-T28 实施批证据——MA-01 真实网格加载＋MA-02 真实碰撞设施

审计溯源：main-functional-audit-20261021-5fd9a3d1/REPORT.md MA-01/MA-02 行
（RT-T15 诚实跳过的两个"待所有者裁决"项；所有者 2026-10-21 裁决立项实施）。
分支：wp13-t28（基于 wp13-t27@648c9509）。日期：2026-10-21。

## MA-01 资源 mesh 真实加载（runtime/WorkCellCompiler）

- MeshReferral 收集器（基座/连杆 visual・collision/工具/场景 geometry）→
  attachResourceMeshGeometry 真实实现：provider 字节→ContentDigester 复算
  SHA-256 比对引用摘要（S4 挂接点复核）→hint 扩展名判格式→临时文件解析
  （文件名＝摘要十六进制幂等）→STL（STLFile）/OBJ（LoaderOBJ→Model3D 逐
  对象合并）挂接（名字面〈帧名〉.visual/.collision）；DAE/WRL/IV 依赖
  Assimp 明确跳过；localTransform 属 codec 演化面（恒等挂接）；失败逐引用
  回退图元代理。
- 测试（RT-GEO-7 四例＋RT-GEO-4(c) 语义更新）：STL 端到端（visual+collision
  双挂接断言）/OBJ 端到端/摘要失配拒绝/未支持格式跳过——12/12 全绿
  （green-runtime-geo7-collision.xml）。

## MA-02 真实碰撞设施（runtime/CollisionSession＋宿主装配）

- 新公共头 CollisionSession.hpp：RuntimeCollisionSession::make(snapshot,
  rules)（PQP 窄相 CollisionDetector＋链序连杆帧→ObjectId 映射＋规则面
  〔相邻过滤/ObjectId 对忽略〕）；evaluate 构型级判定（自由度守卫→setQ→
  检测→过滤→明细）；快照缺位/设备缺失→null＝宿主回落 EvidenceMissing
  （KIN-05 保持）；safetyClearance>0 容差判定未实机化**显式拒绝**（不静默
  降级伪间距）；Snapshot::workCellHandle 非持有借持访问器。
- 宿主装配（ui/KinEvaluationChannel）：attachSnapshot 同拍保留快照副本；
  makeRuntimeCollisionSession 单一翻译点（policy CollisionRules→Runtime
  CollisionRules，ScopeTarget.Object 直映）；RuntimeRealCollisionSession
  Adapter（runtime Verdict→IkCollisionVerdict）；覆盖/单点求解/批量三安装
  位＋PolicyAwareIkSolver 内联路径真实设施优先，不可用回落桩。
- 测试（RuntimeCollisionSessionTest 两例）：装配＋自由度守卫；诚实降级两轨
  （null 快照→null；clearance>0→显式拒绝）。
- **登记（红线例外册 DTB §4.5/§4.6＋白名单机器面）**：R-5 例外增列
  sdurws_ird_runtime（判定权威仍归 policy 规则面）；LIB 登记启用
  sdurw_loaders；IRD_R5_EXEMPT_TARGETS/IRD_FRAMEWORK_REGISTER_ALLOWED
  同步；L1 清单扩展（+sdurw_loaders/proximity/proximitystrategies，
  BuildRedLineTest 三库齐断言兼容）。

## 绿证

| 文件 | 内容 |
| --- | --- |
| green-runtime-geo7-collision.xml | RT-GEO-7×4＋诚实跳过更新＋碰撞会话×2＝12/12 |
| green-full-build.log | 集成全量 Release 构建零错误 |
| green-standalone-runtime-build.log/.xml | 独立冒烟 runtime 构建零错误＋195/195 |
| green-host-tour-auto.log / green-host-tour-requirements.log | 正式宿主 tour 全绿（宿主装配面回归） |

## 回归

- 集成套件：runtime/ui/ui_contract/modeling/modeling_gui 全绿（MA-012 批
  亲跑）；宿主 auto/layout/requirements-tour 全绿；ird_gates 全项零命中
  （登记后）；validate-docs PASS。
- 独立冒烟：runtime 195/195（真实 mesh/碰撞 TU 为集成 gated——被测面构造
  真实框架对象，与 RT-T07 系同因）。

## 开放面（如实）

- 带摆位场景对象的碰撞/不碰撞/排除正反例与宿主画面归统一验收批
  （MA-07/08 联合终验同批）；safetyClearance>0 容差判定实机化同批。
- DAE/WRL/IV 格式需 Assimp 依赖（vcpkg＋DTB §3 登记后随批补齐）。
- localTransform（网格帧内变换）需 codec 版本演化（现恒等挂接）。
