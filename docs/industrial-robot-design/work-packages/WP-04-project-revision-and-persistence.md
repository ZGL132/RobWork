# WP-04 项目修订与持久化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 实现目录式 `.rwdesign`、原子项目命令、方案分支、草稿、不可变资源和可恢复的多文件事务。

**Architecture:** `ProjectRevision` 是已应用状态唯一聚合根；命令产生新修订，查询只读取指定修订。内容对象与资源不可变，`HEAD` 是最后原子切换的提交指针。结果和检查点由各自仓库追加，不由工作进程直接写项目。

**Tech Stack:** C++、Qt Core 文件/JSON API、SHA-256、CTest、Windows 原子文件操作。

---

## 文件与目标

**创建目标：** `sdurws_ird_project`、`sdurws_ird_project_test`。

**创建：**

- `industrialrobot/project/include/.../IProjectQuery.hpp`
- `industrialrobot/project/include/.../IProjectCommandService.hpp`
- `industrialrobot/project/include/.../ProjectRevision.hpp`
- `industrialrobot/project/include/.../ProjectManifest.hpp`
- `industrialrobot/project/include/.../ProjectStore.hpp`
- `industrialrobot/project/include/.../ProjectUpgradeRegistry.hpp`
- `industrialrobot/project/src/`
- `industrialrobot/project/test/`

**覆盖需求：** ARC-01，CON-01、03，NFR-REL-01、04，NFR-DEP-04，AT-05、10、12、13。

## 项目格式

```text
ProjectName.rwdesign/
  HEAD
  project.json
  revisions/<revision-id>/manifest.json
  revisions/<revision-id>/domain/*.json
  objects/<sha256>
  results/<run-id>/
  checkpoints/<run-id>/<attempt-id>/
  drafts/<draft-id>.json
  reports/<report-id>/
```

`project.json` 保存项目身份和格式 Schema；`HEAD` 只保存当前分支/修订指针；修订目录保存引用清单，不复制内容寻址资源。首版 Schema 为 1；旧 `.rwproj` 返回稳定“不支持旧格式”诊断。Schema 1 发布后的升级必须通过 `ProjectUpgradeRegistry` 逐版本前向执行，禁止跳级猜测。

## 冻结接口

```cpp
class IProjectQuery {
public:
    virtual ProjectRevision getRevision(ProjectId, BranchId, RevisionId) const = 0;
};

class IProjectCommandService {
public:
    virtual ApplyCommandResult apply(ProjectCommand, ExpectedRevision) = 0;
};
```

并发修改使用 expected revision；不匹配时返回冲突诊断，不覆盖新状态。每次用户“应用”产生至多一个原子修订；会话显示变化不调用命令服务。

## 任务

### Task 1：格式与路径安全测试

- [ ] 先写空 HEAD、未知 Schema、旧 `.rwproj`、资源逃逸、哈希不符和丢失修订失败测试。
- [ ] 实现规范路径解析，所有资源必须位于项目目录或内容对象区。
- [ ] 验证 Windows 大小写和分隔符差异不会绕过项目边界。

### Task 2：命令与修订

- [ ] 先写一次应用只生成一个修订、expected revision 冲突和稳定对象 ID 保持测试。
- [ ] 实现 ProjectCommand、验证、原子应用和 RevisionEnvelope。
- [ ] 实现方案分支；优化候选只有“设为当前方案”时创建分支和正式修订。

### Task 3：多文件事务

- [ ] 在版本目录写入、对象写入、清单校验和 HEAD 切换处分别注入失败。
- [ ] 实现 staging 写入、逐文件校验和 HEAD 原子切换。
- [ ] 重启时忽略未提交版本并提供恢复诊断；原版本逐文件哈希保持一致。

### Task 4：资源和草稿

- [ ] Verified/正式报告引用的外部网格、目录和材料表复制为不可变内容对象。
- [ ] 实现引用计数/可达性清理，任何历史快照或报告引用的对象不得删除。
- [ ] 编辑草稿单独保存，不进入 ProjectRevision、输入切片或计算。

### Task 5：升级与重新关联

- [ ] 实现 Schema 1 身份和升级注册表空基线。
- [ ] 对未知未来 Schema 明确拒绝，不尝试降级读取。
- [ ] 外部源缺失/变化时报告并提供重新关联命令；历史不可变副本不受影响。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_project_test$'
```

## 退出条件

- A-GATE-02、03、04 和 AT-05、10、12、13 的项目侧断言通过。
- 任意保存故障后旧项目仍可打开且哈希一致。
- 工作进程没有项目文件写权限接口。
- 会话态和草稿态不会产生项目修订。
