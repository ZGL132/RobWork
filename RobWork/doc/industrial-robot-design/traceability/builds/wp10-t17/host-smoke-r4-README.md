# wp10-t17 宿主 GUI 冒烟取证登记（-r4，2026-09-24）

## 1. 执行方式与时序（如实登记）

- **执行者＝所有者本人（人工操作）**。实施侧闭环驱动（host-smoke-r3-driver.ps1 / host-smoke-r4-actions.ps1）实测合成键鼠对 Qt 弹出菜单不可靠（菜单点击漂移，attempt 1 五版脚本同族——F-325 取证管线脆弱性再次实证，驱动尝试帧 01~03 在案），所有者当场接手人工操作（"我来操作验收"）。
- **时序**（据文件系统时间戳与 dev 日志时刻重建）：
  - 14:03 实施侧增量重建三目标零错误（integration-build-r4-prebuild.log——冒烟二进制＝送验对象 e917734a）；
  - 14:05 宿主经 `--rwsplugin` 直载启动（pid 10956，帧 01：标题 `RobWorkStudio v26.9.24-wp10-t17`＋五区工作台＋菜单栏 File/Tools/View3D/视图/Plugins）；
  - 14:32:35 项目「冒烟项目r4」创建并打开（目标目录＝预建空目录 `…\wp10-t17-smoke-r3\projtarget-r4`）；
  - ~14:33-14:34 关闭编排进入 Draining（dev 日志第 2 行 `draining entered: in-flight references=0`）；其后所有者继续人工验证（重开项目等）；
  - ~14:42 再次发起打开同一项目被 S2 切换流候选验证拒绝（dev 日志第 4 行 `switch rejected (candidate open failed): code=lock-held-by-other`——本项目锁被本会话持有，"候选验证成功才切"保守语义）；
  - 14:44:44 所有者裁决不重走补拍（帧 04 为本会话输入框键入"不用"的时点帧）；实施侧优雅退出宿主（WM_CLOSE，QUIT graceful），锁心跳止于 14:44:44Z。
- **所有者验证结论（原话）**："我已经手动验证了，都正确可以通过验收"（2026-09-24 会话消息）。

## 2. 证据清单（本目录）

| 件 | 内容 |
| --- | --- |
| host-smoke-r4-01-startup.png | 装载呈现：`--rwsplugin` 直载成功，标题 wp10-t17，IRD 五区工作台＋融合菜单栏 |
| host-smoke-r4-02-file-menu.png / -02b-crop.png | File 菜单展开：『工业机器人项目』子菜单项位于 Reload 与 Preferences 之间（菜单位置半区在 e917734a 二进制上刷新实证；02b 为原生分辨率裁剪） |
| host-smoke-r4-03-automation-submenu-click-missed.png | 实施侧合成鼠标点击子菜单项未命中的现场（F-325 同族取证管线缺陷实证） |
| host-smoke-r4-04-owner-attestation-typed.png | 所有者键入"不用"（裁决不重走补拍）时点帧 |
| host-smoke-r4-dev-diagnostics-lastrun.log | 插件 dev 日志全程（UTF-8）：①诊断栈就绪 ②`draining entered: in-flight references=0` ③④退出后 `[sampled-tail]` 采样尾段（含 switch rejected 留痕） |
| host-smoke-r4-store-metadata.txt | 项目库落盘事实汇编：project.json（projectId prj-4a387672…，createdAtUtc 2026-09-24T06:32:35.376Z，formatId rwdesign/10000）＋HEAD（rev-577cee08，revisionSeq 1，branch M0/main，manifestDigest）＋project-init 修订 command/manifest＋ProjectMetadata 内容寻址对象 |
| host-smoke-r4-store-listing.txt | 项目库目录清单（文件与字节量） |
| host-smoke-r4-store-lock.txt | 锁文件原件：pid=0000010956（宿主进程）＋hb 心跳至 2026-09-24T06:44:44Z（宿主退出时刻） |
| integration-build-r4-prebuild.log | 冒烟前 e917734a 增量重建三目标零错误（EXIT=0） |
| host-smoke-r4-actions.ps1 / -captureloop.ps1 | 实施侧取证工具（原子动作＋连拍循环；与已入库 host-smoke-r3-driver.ps1 同族，迭代先例同 acc-attempt1 五版脚本） |

## 3. 诚实缺口（不粉饰）

- **关闭确认对话框、保存草稿状态行（"草稿已保存（0 个模块）"）、Draining 状态行（"正在关闭项目……"→"项目已关闭。"）的过程视觉帧未捕获**：第一轮 12 分钟连拍先于操作过期，第二轮连拍期间所有者已裁决不重走（"不用"）。
- B-1 三行为（关闭确认流／Draining 收口／保存草稿链路）的宿主执行事实，以上述**磁盘落盘＋dev 日志＋锁文件 pid/心跳＋所有者人工验证确证**为登记事实；视觉缺口与全部证据一并如实交验收段 attempt 2 独立裁定（新记录 -r2 命名＋evidence 分支 acc/UI-T17/2）。
- dev 日志第 4 行 `switch rejected … lock-held-by-other` 的产品语义属性（同一项目重开的正确保守拒绝 vs 语义面待完善）实施侧不作裁定，登记事实供验收段与所有者裁量。

## 4. 关联

- 任务契约：`tasks/foundation/UI-T17.json`（verify 第 4 条宿主 GUI 冒烟）
- 验收 attempt 1 记录：`traceability/acceptance/UI-T17-20260924.md` @ acc/UI-T17/1（B-1 返工输入六条）
- 移交手册：HOST-SMOKE-HANDOVER.md（本目录）；v1.13-r4 登记注：units/ui.md §13
