# REUSE-T17 独立验收记录（2026-10-23）

验收会话：所有者授权的独立子智能体（与实施会话隔离的新上下文）；被验收冻结
提交 `e63cfaf8f3b3a3f3348014067f6315760e48f8c1`（父 `bf45f69a5`），分支
codex/reuse-foundation。验收全程只读，复跑产物在其 `.cache/acc-t17/`（构建/
门禁/四套件 gtest XML＋log＋report 原件）。

## 判定：PASS

七项核对全部通过：
1. SHA/父/分支一致；28 文件范围与契约 allowedFiles 一致，forbiddenFiles
   （runtime/**、kinematics/src/**、core/**、上游、vcpkg）零触碰。
2. runtimeModelView() 表尾追加/缺省空/虚表序与契约 /2 登记实证
   （KinTypes.hpp:163-180、units/kinematics.md v0.37）；实现者派生类全库清点
   10 个均一致覆盖或缺省空；FrozenRuntimeView 全库零残留。
3. 门禁复跑（-Head 冻结核）：validate-reuse -TaskFile T17 PASS exit 0；
   validate-docs 四契约批次 PASS exit 0。
4. 四套件复跑：kinematics 288+1skip/0、contract 57/0、GUI 48/0、
   ui 344/345——增量构建零错误。
5. F-560 预存归因实锤：UiPlugin.cpp 与 DefectPinningTest.cpp 在两提交间
   零变化；钉扎串 applyPreview 与现实现 applyCombinedPreview 字符漂移。
6. 证据一致性：四组 XML 计数与 README 表逐格一致；changes/REUSE-T17.json
   六文件 SHA256 按门禁规范化重算 6/6 MATCH。
7. 语义红线：纯转发桥零值复制回潮、无 no-op deleter 伪装、无隐藏 fallback；
   weak_ptr 过期实证锚＋宿主释放后快照可析构。

## 低危备注（不阻塞）

- A：提交含 3 个 validation-batch 批次验证原件，不落 allowedFiles 通配内——
  跨任务批次证据的结构性归属，沿用本分支前序治理提交惯例；建议后续契约
  增补 `traceability/reuse/validation-batch/**` 授权模式。
- B：README/单元卡"8 实现者"为登记口径，直接派生类实测 10（缺省空替身
  实为 6）；语义零风险，计数登记待下次单元卡增量订正。

独立验收通过即本包可进入集成排程；F-966（C4）与 T13/T15/T16 独验状态
不受本验收影响，仍按各自轨道执行。
