# WP-16-T17 轨迹审核修复批闭合报告

日期：2026-10-22  
分支：`wp16-t17`  
验证平台：VS2022 x64、Release、`QT_QPA_PLATFORM=windows`；所有 GUI 程序串行运行。

## 本批已修复

- 正式宿主装配 trajectory Dock、视图菜单开关、中文页面/命令标题和无归档空态。
- 拒绝命令不再进入宿主提交管线；点击时重新检查可用性，提交异常在面板中反馈并跨刷新保留。
- 曲线在面板创建和会话刷新时首刷；源移除会清理旧关节和曲线。
- 回放支持连续播放、暂停、回到起点、滑杆/秒数定位；会话纪元改变会停止旧播放并复位，坏帧/异常不写三维姿态。
- 失败段定位缺缝禁用；定位回调异常就地显示失败原因，不越出 Qt 槽。
- PTP 逐轴量纲透传，移动轴使用 m；非法量纲入口拒绝。
- CartesianLine 拒绝行列式为负的反射矩阵。
- Sequence 接近/撤离必须消费完整编排结果；缺服务、坏结果、维度/身份/限位错误均产生可定位失败；撤离首构型跨分支时以 `TRJ-BRANCH-JUMP` 拒绝。
- 补充宿主装配目标级门禁登记和临时目录隔离，避免旧崩溃报告污染 UI 回归。

## 验证结果

| 面 | 结果 | 证据 |
|---|---:|---|
| 轨迹算法/序列 | 190 通过，0 失败，0 跳过 | `trajectory-final.xml`、`trajectory-final-report.json` |
| 轨迹契约 | 69 通过，0 失败，0 跳过 | `contract-validated.xml`、`contract-validated-report.json` |
| 轨迹真实 Widgets | 10 通过，0 失败，0 跳过 | `gui-post-final.xml`、`gui-post-final-report.json`、`gui-post-final-shots/` |
| 独立轨迹单元 | 34 通过，0 失败，0 跳过 | `standalone-unit-final.xml`、`standalone-unit-final-report.json` |
| 独立轨迹契约 | 51 通过，0 失败，0 跳过 | `standalone-contract-final.xml`、`standalone-contract-final-report.json` |
| UI 文案/宿主契约 | 344 通过，0 失败，0 跳过 | `ui-final-r2.xml`、`ui-contract-r2.xml` |
| 正式宿主布局探针 | 通过：Dock、菜单、中文标题、空态、中央视图；截图已保存 | `host-final.log`、`host-final-shots/` |
| 依赖/源码门禁 | PASS，R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 均 0 命中 | `gates-r2-build.log` |
| 任务契约 | PASS | `validate-task-r2.log` |

## 尚未闭合的正式产品问题

正式宿主目前只绑定了命令、可用性和姿态出口；尚未提供已发布轨迹归档到 `durationSource`、`playbackSource`、`curvesSource` 的真实数据桥，也没有把失败段身份映射到正式路径对象。因此正式窗口的回放在无归档时正确禁用，不能据此宣称真实轨迹播放、TCP/路径图层和失败段高亮已经通过。接近/撤离的单元测试使用端口替身验证结果消费，不能替代正式 CartesianLine→IK→时间参数化执行链证据。

全仓 `validate-docs.ps1` 本批未通过，原因是本工作树尚未将新增证据文件加入 Git，且已有 `acceptance/UI-T37-20261002.md` 索引大小与文件不一致；这不是轨迹算法或 GUI 回归失败。提交前需按仓库验收流程统一 `git add` 证据并重跑索引，再处理 UI-T37 历史索引漂移。

