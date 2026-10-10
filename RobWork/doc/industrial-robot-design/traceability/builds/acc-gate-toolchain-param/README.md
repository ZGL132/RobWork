# acc-gate-toolchain-param 验收复现留痕（独立子会话对抗式验收，2026-10-10）

## 身份与前提

- 送验对象：任务 `gate-toolchain-param`，分支 `gate-toolchain-param`，冻结
  headSha `db3beaaa1a68da02f155c55403396dc298812695`（commit 范围
  `ebf86026..db3beaaa`，2 提交）。
- 验收会话与实施者不共享记忆；本目录全部产物由验收者**亲手复现**生成，
  复现树为独立 detach worktree `C:/Users/zgl18/AppData/Local/Temp/acc-gtp-repro`
  （checkout db3beaaa），**无 junction、无 vcpkg/ 目录、无预配置**——
  驱动脚本启动时自检 `ACC_VCPKG_DIR_PRESENT=False` 逐轮落盘（三轮日志第 2 行）。
- 前置仅两项（DTB §5.1④ ini 模板手工复制＋③进程 PATH 前置 Miniconda，
  后者由驱动脚本 acc-run.ps1 进程内完成，不改机器环境）。

## 复现命令（三轮同一命令）

```
pwsh -File <A树>/RobWork/scripts/industrialrobot/gate-all.ps1
  -BuildDir  <A树>/build
  -SmokeDir  <A树>/smoke
  -QtPrefix  D:/software/Qt/6.11.1/msvc2022_64
  -Toolchain D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake
```

## 三轮实测（引擎计数由验收者独立 awk/grep 复核，非转录）

| 轮次 | 起止（2026-10-10） | 耗时 | EXIT | 引擎汇总 | 说明 |
| --- | --- | --- | --- | --- | --- |
| run 1（acc-gate-all-run1.log） | 08:49:29→09:42:14 | 52.7 min | 1 | 83 PASS / 1 FAIL | **全新树从零配置＋构建**：CMakeCache 记录 `CMAKE_TOOLCHAIN_FILE=主仓vcpkg路径`（参数从零吃参直证）；构建期 DLL 复制行含主仓 vcpkg installed 路径；唯一失败＝`sdurws_ird_testkit_test`（见下「testkit 间歇」） |
| run 2（acc-gate-all-run2.log） | 09:42:52→10:23:23 | 40.5 min | 1 | 84 PASS / 1 FAIL | 集成缓存确认＋冒烟从零配置构建；testkit 集成段再现同一失败，冒烟段 40/40 全绿（含 testkit 冒烟 PASS） |
| run 3（acc-gate-all-run3.log／acc-run3-summary.txt） | 10:24:39→11:08:47 | 44.1 min | **0** | **85 PASS / 0 FAIL（85/85 通过）** | 集成 40＋冒烟 40 全绿，含 `sdurws_ird_ui_test`（集成 3.2s 级）与 `sdurws_ird_testkit_test` 两侧通过 |

- **testkit 间歇观察（F-629 登记依据）**：三轮 gate-all 共 6 次
  testkit 执行（集成＋冒烟各半），集成段 2 次失败（run 1/2）、其余 4 次全过；
  空闲直跑复测 5 次全 PASS（testkit-retest-stability.log 为第 5 次留档）。
  失败时 ctest 仅留尾 4 行无法定位具体用例；结合源码
  ProcessRunnerTest.cpp 树终止用例观察窗 3500ms 对孙进程延迟 3000ms
  仅 500ms 余量（源注释自述余量只计落盘开销）＋本机 Defender 实时防护
  开启（RealTimeProtectionEnabled=True），判定为防病毒扫描/负载延迟吃尽
  余量的计时敏感间歇，**与本批 diff 无关**（该批零触碰产品代码）。

## 负例与变异（4.5，三件）

| 文件 | 操作 | 预期与实测 |
| --- | --- | --- |
| neg1-bad-toolchain.log | 传 `-Toolchain D:/nonexistent/vcpkg.cmake` | 第 0 步环境自检 FAIL `vcpkg toolchain 不存在: D:/nonexistent/vcpkg.cmake`＋同路径 cmake 配置失败，EXIT=1——参数真实被消费 |
| neg2-default-fallback.log | 不带 `-Toolchain`（无 junction） | 第 0 步 FAIL 于推定路径 `<复现树>/vcpkg/scripts/buildsystems/vcpkg.cmake`，EXIT=1——缺省回落与改动前行为一致 |
| neg3-mutation-old-hardcode.log | 临时还原条件赋值为旧硬编码后带**有效**参运行（事后已还原并经 git status 核验） | 第 0 步 FAIL 于旧推定路径（所传有效参数被无视），EXIT=1——参数生效确由新增条件赋值接线 |

（负例日志原为 GBK 控制台编码，入库前已转 UTF-8，内容逐字节对应。）

## 消费点证据（三处全数吃参）

1. 第 0 步环境自检：neg1/neg2/neg3 三件负例（上表）。
2. 集成树（重）配置：run 1 从零配置后 `build/CMakeCache.txt` 记录
   `CMAKE_TOOLCHAIN_FILE:FILEPATH=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake`；
   run 1/2/3 构建期 DLL 复制行均自主仓 vcpkg installed 树。
3. 冒烟树配置：acc-smoke-config-probe.log——以 gate-all 第 4 步**同款命令**
   独立复现配置，PROBE_CONFIG_EXIT=0，探针 CMakeCache 记录参数路径
   （gate-all 全绿后清理冒烟目录，故按同命令探针留证，与实施者 run 3 后探针同口径）。

## 其他核查留痕

- acc-validate-docs.log：验收者独立执行 validate-docs，EXIT=0，
  `PASS (20 units, 12 trace entries, 312 task files)`。
- 驱动脚本 acc-run.ps1：编码 UTF-8＋时间戳/耗时落盘＋退出码原样传播；
  日志中 ird_gates 等子进程输出存在 GBK 混编花字（子进程编码差异），
  判定行（PASS/FAIL/结果:/ACC_*）均为 ASCII 可读，不影响结论判读。
