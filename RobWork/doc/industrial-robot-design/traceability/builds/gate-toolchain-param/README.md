# F-628 批次验证留痕（gate-toolchain-param，2026-10-10）

## 批次与任务

- findings：F-628（gate-all 对 worktree 的 vcpkg 供给依赖 junction——两次清理
  穿透事故的系统性消除）。
- 改动：`RobWork/scripts/industrialrobot/gate-all.ps1` 增 `-Toolchain` 可选参数
  （有参用参、无参回落 `<仓库根>/vcpkg/scripts/buildsystems/vcpkg.cmake` 推定
  路径）；DTB v0.56（§5.1「fresh 树运行时前置」行补 ⑥vcpkg toolchain 供给
  口径＋头部变更记录行）；findings.json F-628 → fixed。
- 提交：8cdc504c（脚本＋DTB＋findings）；本目录留痕随留痕提交登记。

## 验证环境（刻意裸装）

- 工作树 `C:/Users/zgl18/AppData/Local/Temp/gate-tc-work`（branch
  `gate-toolchain-param`，基于 redesign-main@ebf86026）：**无 junction、
  无 vcpkg/ 目录、无预配置**（build/ 与 smoke/ 均由门禁现场生成）——
  toolchain 供给只可能来自 `-Toolchain` 参数，这是本批验收性证据的前提。
- 调用命令（run 1/2/3 同一命令，经驱动脚本 run-gate-all.ps1 落盘时间戳与耗时）：

```
pwsh -File <工作树>/RobWork/scripts/industrialrobot/gate-all.ps1 \
  -BuildDir  C:/Users/zgl18/AppData/Local/Temp/gate-tc-work/build \
  -SmokeDir  C:/Users/zgl18/AppData/Local/Temp/gate-tc-work/smoke \
  -QtPrefix  D:/software/Qt/6.11.1/msvc2022_64 \
  -Toolchain D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake
```

## 实测过程（三轮，全程无 junction）

| 轮次 | 结果 | 说明 |
| --- | --- | --- |
| run 1（gate-all.log） | 集成 39/40＋1 失败，中止于冒烟段 | 全新树从零配置＋构建＋逐目标测试：集成配置吃参直接证据（配置告警调用栈含主仓 vcpkg 路径）；唯一失败＝`sdurws_ird_ui_test` 退出码 0xc0000135（DLL 未找到）＝**DTB §5.1① 登记的 fresh 树前置**（框架 DLL 仅 `sdurws_ird_studio` POST_BUILD 复制，gate-all 不构建该目标），非本批改动引入 |
| 前置补齐（studio-build.log） | EXIT=0 | 按 DTB §5.1①/F-622 口径显式 `cmake --build build --config Release --target sdurws_ird_studio`——46 个框架 DLL 就位于 build/RobWorkStudio/bin/Release |
| run 2（gate-all-run2.log） | 中止 | 集成 39/40 绿后 `sdurws_ird_ui_test` 挂起（进程 0 CPU）——手工探针定位＝`python313.dll` 不在调用方 PATH（**DTB §5.1③ 登记的 ui_test 依赖链**），主动停止 |
| 前置补齐 | —— | 驱动脚本把 `D:/software/miniconda3` 前置进**本进程** PATH（DTB §5.1③ 口径，不改机器环境） |
| run 3（gate-all-run3.log） | **EXIT=0，85/85 通过，40.7 分钟** | 集成缓存确认＋ird_gates 零命中＋集成 40 目标全绿（含 `sdurws_ird_ui_test` 2.42s）＋冒烟配置＋冒烟 40 目标全绿；汇总清单见 gate-all-run3-summary.txt（85 PASS/0 FAIL） |

- 剪贴板型用例按 F-620 预探 SKIPPED（modeling_gui/ui_gui 各 2 例），ctest 层
  计通过——预期口径，非吞失败。
- 框架侧排除面插件（RWSimulatorPlugin/bt_plugin）gate-all 不构建，未触及
  （DTB §5.1「合并后复验与排除面口径」行口径）。

## -Toolchain 消费证据

见 toolchain-evidence.txt（四组：集成树 CMakeCache 记录值、run 1 配置告警栈
中的参数路径、run 3 构建期主仓 vcpkg installed 树 DLL 复制行 29 处、冒烟配置
命令复现探针 smoke-config-evidence.log 的命令行＋CMakeCache 记录）。gate-all
脚本内三处消费点（环境自检 Test-Path／集成配置／冒烟配置）与提交 8cdc504c
说明一致。

## 留给验收者的注意点

1. `gate-tc-work/smoke/workflow/` 为空目录残留：run 3 全绿后 gate-all 清理
   冒烟目录时的删除竞态（workflow 冒烟 exe 句柄未及时释放，空目录删除被
   SilentlyContinue 吞掉）——非门禁判定问题（汇总 85/85 在清理判定之后），
   目录清理归编排者，本批按纪律不做删除。
2. 工作树外另有探针目录 `C:/Users/zgl18/AppData/Local/Temp/gate-tc-smoke-probe`
   （冒烟配置行证据的复现现场），留证后可由编排者清理。
3. run 1/2 的中途中止均属 fresh 树操作员前置（DTB §5.1 ①/③）与外部任务终止，
   逐轮日志如实保留；run 3 为完整全绿证据。
4. run 3 集成阶段未重新配置（缓存确认在位）——集成配置对参数的"从零吃参"
   证据在 run 1（同一命令、同一无 junction 前提）。
