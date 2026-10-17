---
name: ird-implement-task
description: 工业机械臂任务实施——一次只处理一个任务卡：按契约在 wp 分支编码（中文注释契约）、测试与 CMake 与契约文档同提交、提交前双模式构建与门禁自查、四段式 commit 并推送、发出验收请求。禁止跨任务重构与自行合入。
---

# ird-implement-task：任务实施（单任务卡）

**时机**：discover＋design 完成后。
**前置**：分支＝契约 `branch`（`wp<nn>-t<kk>`）；画像中的"必须读"已读。

## 流程

1. **编码**：C++17；命名空间 `sdurws::ird::<unit>`；**详细中文注释是验收的一部分**（根 AGENTS §2 全量：文件头设计依据、类/函数 Doxygen 中文描述、语义段落、高危信息六表——物理单位/坐标系/角度制式/所有权/线程约束/错误语义/确定性来源）。
2. **测试同批**：测试用例名带需求/AT 追溯；替身只打端口边界；跨单元真实接线写进 `contract_test`。
3. **CMake 同批**：目标注册/依赖边变化与代码同一提交（白名单外新边先走 design 门）。
4. **提交前自查**（全过才提交）：
   - 集成模式构建零错误（先 `grep RWS_BUILD_INDUSTRIALROBOT build/CMakeCache.txt` 确认 `:BOOL=ON`）；
   - 独立冒烟模式通过（必带 vcpkg toolchain＋Qt 前缀两参数）；
   - `ird_gates` 零命中（白名单登记件除外，命中集与登记基线 diff 一致）；
   - 测试真实执行并留痕（gtest XML＋per-exe ird-test-report 双 report 口径，DTB §5.5）——**未执行不得标通过**。
5. **提交**：**先核查当前分支**（`git branch --show-current` 必须等于契约 `branch`——多会话共享同一工作树时曾发生提交误落任务分支/主线的实测事故）→ commit 四段式正文（改动内容/改动原因/验证结果/影响面，根 §6.3）；代码＋测试＋CMake＋契约文档**同一提交**；提交后立即 `git push origin <任务分支>`。
6. **验收请求**：固定四元组——任务 ID、分支名、commit 范围（base..head）、证据位置。

## 禁止项

- 同时处理多个任务卡（跨任务重构一律另立任务）；
- 吞错、带病提交（确需保存半成品：commit 正文显式标注"WIP：尚未验证，缺 xxx"）；
- **自行合入 `redesign-main`**（三段式红线——合入归所有者）；
- 修改 `forbiddenFiles` 列出的路径；触碰框架源码（SA-02）。
