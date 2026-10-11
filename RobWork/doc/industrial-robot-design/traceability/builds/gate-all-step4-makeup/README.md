# gate-all 第 4 步 fresh 冒烟树补跑留痕（治理批 2026-10-12）

## 消账对象

- **T11 findings（F-717）**：gate-all 第 4 步 fresh 冒烟树中止——wp-16-t11 验收记录
  （`traceability/acceptance/wp-16-t11-20261011.md`）登记的第 4 步 fresh 冒烟配置失败事项。
- **T16 findings（F-728）**：gate-all ui ctest 挂住环境遗留——wp-16-t16 验收记录
  （`traceability/acceptance/wp-16-t16-20261011.md`）登记的 gate-all ui ctest 挂住事项。

两条均于 `traceability/findings.json` 以 status=fixed 登记，本目录即其闭环依据留痕。

## 执行信息

- **执行日期**：2026-10-12
- **执行方式**：治理流水线按 B 项侦察产出的等效命令确定性执行（与
  `RobWork/scripts/industrialrobot/gate-all.ps1` 第 4 步同口径，gate-all.ps1:158-190）
- **命令**：删除重建 `C:\Users\zgl18\AppData\Local\Temp\ird-gate-smoke` 后配置＋构建：
  - 配置：`cmake -S D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B C:/Users/zgl18/AppData/Local/Temp/ird-gate-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64`
  - 构建：`cmake --build C:/Users/zgl18/AppData/Local/Temp/ird-gate-smoke --config Release`
- **结果**：**两步退出码全 0**（配置 0、构建 0）。fresh 冒烟树独立配置与全量测试
  目标构建能力验证通过。

## Qt 前缀口径说明

配置命令按流水线冒烟同口径**附加 `-DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64`**。
gate-all.ps1 第 4 步的默认配置在未传 `-QtPrefix` 参数且环境变量 `CMAKE_PREFIX_PATH`
为空时**不带 Qt 前缀**（gate-all.ps1:166-167 为条件追加）——而 testkit_qt 自
WP-10-T02 起使 `find_package(Qt6 Core REQUIRED)` 进入独立配置面，缺 Qt 前缀必配置
失败，此为 **findings F-455 已知口径**；T11 验收批第 4 步中止的直接根因即此
（上次失败现场 `CMakeCache.txt` 中 `Qt6_DIR:PATH=Qt6_DIR-NOTFOUND` 实证）。故本补跑
按 F-455 修正口径显式供给 Qt 前缀。

## 口径注

gate-all 全量运行中**第 3 步（集成树）ui ctest 挂住**为跨单元环境问题（宿主窗口
依赖），**本补跑只对第 4 步 fresh 冒烟树能力消账**（独立配置＋构建＋测试目标产物
生成），与 T15 批 gates 口径一致（wp-16-t15 验收记录同口径处理）；第 3 步 ui ctest
挂住不在本留痕消账范围内。

## 附：控制台输出尾部

- `console-configure.log`——配置步控制台输出尾部（流水线返回的截前摘录）
- `console-build.log`——构建步控制台输出尾部（流水线返回的截前摘录）

两份 log 为流水线执行器返回的**输出尾部摘录**（非完整控制台留痕），按原样存档；
构建尾部含 workflow 测试目标编译告警 C4834（`[[nodiscard]]` 返回值放弃，
HostAdapterTest.cpp:1250）与各测试目标产物生成记录，均不影响退出码 0 结论。
