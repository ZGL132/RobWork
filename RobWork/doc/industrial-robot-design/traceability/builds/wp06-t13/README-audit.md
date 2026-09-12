# RT-T13 README 指向核对与零偏差登记证据（acceptance 1）

- 日期：2026-09-12
- 对象：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/include/sdurws/ird/runtime/README.md`
- 方法：include/ 目录实测清单 ↔ README 模块清单逐头比对；指向核对（任务卡章节引用）；实现状态段与 units/runtime.md §12/§15.4、traceability/builds/wp06-t01~t12 现状比对。

## §1 指向核对（§9→§12）

- README 第 4 行任务卡指向＝`doc/industrial-robot-design/units/runtime.md` **§12**（阶段 A 实现任务拆分）——2026-09-10（runtime.md v0.1 变更记录行）由"§9"修正为"§12"的修正**复审维持**，无回退、无第二指向。核对结果：**零偏差**。

## §2 include/ 目录实测清单（2026-09-12 RT-T13 实施段执行 `ls` 实录）

```
Adapter.hpp
BaseWorldTransform.hpp
CacheKey.hpp
CanonicalModel.hpp
Codec.hpp
Compiler.hpp
Description.hpp
Errors.hpp
NameMap.hpp
README.md
Resource.hpp
Snapshot.hpp
Sources.hpp
```

公共头（.hpp）计 12 个；README.md 为本说明文件自身，不计入模块清单。

## §3 README 模块清单 ↔ 实测目录逐头比对

| # | README 列出模块 | 磁盘实存 | 结论 |
| --- | --- | --- | --- |
| 1 | Errors.hpp | 存在 | 一致 |
| 2 | Sources.hpp | 存在 | 一致 |
| 3 | Description.hpp | 存在 | 一致 |
| 4 | Resource.hpp | 存在 | 一致 |
| 5 | CanonicalModel.hpp | 存在 | 一致 |
| 6 | Codec.hpp | 存在 | 一致 |
| 7 | NameMap.hpp | 存在 | 一致 |
| 8 | BaseWorldTransform.hpp | 存在 | 一致 |
| 9 | Adapter.hpp | 存在 | 一致 |
| 10 | Snapshot.hpp | 存在 | 一致 |
| 11 | Compiler.hpp | 存在 | 一致 |
| 12 | CacheKey.hpp | 存在 | 一致 |

反向核对：磁盘 12 头全部被 README 列出，无遗漏、无多列——**零偏差**。
（修正内容：RT-T13 前的 README 只列 9 模块且声明"RT-T09 起任务未落地"，与 §15.4 v0.10~v0.14（RT-T09~T12 已落位）存在三处过期偏差——本任务已同步，偏差归零。）

## §4 实现状态段比对基准

| README 表述 | 对照源 | 结论 |
| --- | --- | --- |
| 落位登记 §15.4 v0.4~v0.14＝RT-T03~T12 | units/runtime.md §15.4 变更记录行 | 一致 |
| 验证留痕 builds/wp06-t01~t12 | traceability/builds/ 目录实测（wp06-t01…wp06-t12 齐全） | 一致 |
| 集成 261 例＝260 过＋1 例 worker 占位按设计跳过 | builds/wp06-t13/sdurws_ird_runtime_test-integration.xml（本任务实跑：261 ran / 260 passed / 1 skipped＝ContractSuite.ChildProcessWorker_RT_ID_1） | 一致 |
| 冒烟 176/176 | builds/wp06-t13/sdurws_ird_runtime_test-smoke.xml（本任务实跑：176 ran / 176 passed） | 一致 |
| src/ 编译器链仅集成模式（TARGET sdurw_kinematics 条件增列） | runtime/CMakeLists.txt 条件增列＋冒烟 176/176 零框架链接回归 | 一致 |

## §5 joinScopeLocal 产品面唯一性复核（R-4 例外清单佐证，提交件 §1.6 引用）

```
$ grep -rln "joinScopeLocal" --include="*.hpp" --include="*.cpp" runtime/include runtime/src
runtime/include/sdurws/ird/runtime/NameMap.hpp   ← 注释声明侧（文件头 R-4 唯一例外声明）
runtime/src/NameMap.cpp                          ← 定义＋全部调用（detail::joinScopeLocal 第 94 行）
```

产品面（include/＋src/）出现该名字的文件恰为提交件 §1.3 清单所列两文件——R-4 例外清单与实现事实一致。唯一性另由 `runtime/test/NameMapTest.cpp` 源码扫描用例在测试层钉住（集成 261 例内通过）。
