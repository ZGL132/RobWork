# WP-06-T13（RT-T13）门禁登记提交件

| 字段 | 值 |
| --- | --- |
| 提交任务 | RT-T13 文档与门禁同步（≙WP-06-T13，runtime 单元） |
| 日期 | 2026-09-12 |
| 提交方 | RT-T13 实施段（分支 `wp06-t13`） |
| 接收方 | §1 → WP-01-T01（ird_gates 门禁数据面回填）＋WP-01-T03（DTB §4.5 例外登记册同步）；§2 → WP-01（CI 门禁所有者）＋WP-24（交付与部署基础） |
| 性质 | **登记提交件**——本文件只提交清单与建议，接收方处置后才生效；提交方不代行 cmake/ 与 scripts/ 侧改动（均不在本任务 allowedFiles） |
| 上游依据 | 需求 NFR-MNT-07；ARC-04/SA-05（名称解析唯一实现者）；units/runtime.md §7（R-4 唯一例外）、§12 RT-T13 任务行、§15.4 v0.15；DTB §4.5 预登记行（2026-09-10）；`ird_gates_whitelist.cmake` R-4 占位注释；CORE-T10 文档与门禁同步先例（traceability/acceptance/CORE-T10-20260911.md） |

---

## §1 R-4 例外登记——runtime 名称解析器实现文件清单（提交 WP-01-T01）

### 1.1 红线与例外语义

R-4 红线：RobWork 名称前缀拼接/剥离禁止（ARC-04"统一名称解析器"、NFR-MNT-07、AGENTS.md §5 红线 5——"不做 RobWork 名称前缀拼接/剥离，名称解析统一归 runtime"）。唯一合法例外＝runtime 单元的名称解析器实现本身：RuntimeNameMap 生成运行时全名时必须执行一次"作用域前缀＋'.'＋局部名"拼装（runtime.md §7——"前缀拼接/剥离唯一合法位置是本单元实现"）。该例外是**架构明文设计**（runtime.md §2.2/§3.4/§7），不是运行期放宽：业务单元经⑥端口整名查询，任何下游自行拼名即违例（RT-NM-7 用例钉住"拼名查询不得命中"）。

### 1.2 登记链（本提交的生效路径）

1. DTB §4.5 例外登记册（WP-01-T03 维护）2026-09-10 预登记行："R-4（前缀拼接/剥离禁止）｜runtime 名称解析器实现文件清单｜ARCH §3.2、§7.4｜**预登记（清单随 WP-06-T13 提交转生效）**"。
2. `industrialrobot/cmake/ird_gates_whitelist.cmake` R-4 占位注释（WP-01-T01 交付）："runtime 名称解析器实现文件清单：DTB §4.5 预登记，清单随 WP-06-T13 提交转生效——届时由 WP-01-T03 回填 IRD_R4_EXCEPTION_FILES"。
3. runtime.md §12 RT-T13 任务行产物："R-4 例外登记（名称解析器实现文件清单）提交 WP-01-T01"；§15.4 v0.6⑧："R-4 例外实现文件＝NameMap 模块（NameMap.hpp 声明＋NameMap.cpp 的 joinScopeLocal 单点）——清单提交 WP-01-T01 门禁随 RT-T13"。

本文件即该"清单提交"的载体。

### 1.3 文件清单（机器消费面）

路径相对 `IRD_ROOT`（＝`RobWorkStudio/src/rwslibs/industrialrobot/`，与 `IRD_R4_EXCEPTION_FILES` 的相对路径语义一致——ird_gates.cmake 第 4c 步 `file(RELATIVE_PATH ... "${IRD_ROOT}" ...)`）：

```cmake
# 建议 WP-01-T01 按下列内容回填 ird_gates_whitelist.cmake：
set(IRD_R4_EXCEPTION_FILES
    "runtime/include/sdurws/ird/runtime/NameMap.hpp"
    "runtime/src/NameMap.cpp"
)
```

| # | 文件 | 例外理由 |
| --- | --- | --- |
| 1 | `runtime/include/sdurws/ird/runtime/NameMap.hpp` | 名称解析器公共契约声明面：文件头明文声明 R-4 唯一例外落点（"只允许出现在 NameMap.cpp 的 joinScopeLocal() 一处——R-4 例外登记的唯一豁免点"）；属同一例外实现的声明侧 |
| 2 | `runtime/src/NameMap.cpp` | 前缀拼装**唯一实现点** `detail::joinScopeLocal()`（第 94 行）：`scopeToken + "." + localName`；生成/消歧/查询全路径的全名产出唯一经此函数 |

### 1.4 例外范围声明（登记边界）

- **文件级、非单元级**：本清单只豁免上述两个名称解析器实现文件；runtime 其余产品面文件（include/ 其余十一头、src/ 其余实现）**仍应接受 R-4 扫描**。
- **不申请整单元豁免**：`IRD_R4_EXCEPTION_UNITS` 不使用（当前引擎 4c 步对 runtime 的处理见 1.5 过渡态说明）。
- 单点不变量由测试钉住：`runtime/test/NameMapTest.cpp` 源码扫描用例断言 "joinScopeLocal" 名字在产品源（include/+src/）中唯一出现——未来任何第二拼装点入产线即测试红。
- O-12（P-POL-8："policy 只读消费完整设备作用域名不构成拼接/剥离"）与本清单**相互独立**：本清单只处理 runtime 侧唯一实现点的登记，不预设、不代替 O-12 的措辞裁决（policy 例外待其确认后另行登记，见 DTB §4.5 第二条 R-4 行）。

### 1.5 过渡态事实（供门禁所有者处置，非本提交动作）

当前引擎 `ird_gates.cmake` 第 4c 步对 runtime 单元存在**硬编码整单元跳过**（`if(NOT _unit STREQUAL "runtime")`；引擎内注释自述"runtime 单元整域例外走 IRD_R4_EXCEPTION_UNITS（登记后生效）"）——即本清单回填前，R-4 扫描对 runtime 产品面实际不生效，属 WP-01-T01 交付时点的已知过渡态（O-12 未裁决＋清单未提交）。建议回填本清单时一并把该硬编码转为登记册驱动：文件级豁免按 `IRD_R4_EXCEPTION_FILES`，runtime 其余文件纳入扫描。引擎侧是否调整、如何调整属 WP-01 辖域，本提交不代行、不私裁。

### 1.6 依据与验证锚点

| 锚点 | 内容 |
| --- | --- |
| 单点实锚 | `runtime/src/NameMap.cpp:94` `joinScopeLocal` 定义（文件头第 14 行："拼装点＝本文件 detail::joinScopeLocal()；任何其他文件出现该动作即 R-4"） |
| 唯一性测试 | `runtime/test/NameMapTest.cpp`（源码扫描钉住 joinScopeLocal 名字在产品源唯一出现）＋RT-NM-7（拼名查询不得命中——绕过唯一解析器不可靠） |
| 行为验证 | RT-NM-1～7 双向往返全量通过；AT-18 往返（§7.4）；集成 261 例留痕 traceability/builds/wp06-t12/（RT-T12 已验收） |
| 全仓复核 | RT-T13 实施段 grep 复核：产品面（include/+src/）"joinScopeLocal" 仅 NameMap.hpp（注释声明）/NameMap.cpp（定义＋调用）出现——复核记录 traceability/builds/wp06-t13/README-audit.md §3 |

---

## §2 UT-BUILD 并入 CI 建议（登记 WP-01／WP-24）

### 2.1 对象与现状

runtime 的 UT-BUILD（构建红线用例组）＝`runtime/test/BuildRedLineTest.cpp` 四例，已注册于 `sdurws_ird_runtime_test`：

| 用例 | 红线 |
| --- | --- |
| `RuntimeBuild.SourceTreeReachable_RT_BUILD` | 源码树可达（读失败显性失败，不静默跳过） |
| `RuntimeBuild.NoQtInclude_RT_BUILD_NFR_MNT_01` | L2 产品面零 Qt（NFR-MNT-01，红线 R-3 同源） |
| `RuntimeBuild.NoCrossUnitInclude_RT_BUILD_R1_R2` | 零跨单元 include（R-1/R-2） |
| `RuntimeBuild.BaselineLibsPinned_RT_BUILD_P_RT_3` | L1 基线库链接清单钉住（P-RT-3 消账输入） |

### 2.2 既有承载（沿 CORE-T10 先例口径如实消账——非新裁决）

1. **本地门禁已常驻**：`gate-all.ps1`（WP-01-T02 交付）执行面＝ird_gates＋双模式构建＋**全部 `_test`/`_contract_test` 目标一键执行**——runtime UT-BUILD 四例随 `sdurws_ird_runtime_test` 包含其中。
2. **构建期门禁已常驻**：`ird_gates.cmake`（WP-01-T01 交付）在构建树对产品面执行 R-2/R-3/R-4/R-5 等静态扫描，与 UT-BUILD 的运行期扫描同源双层（core 单元 CORE-T01 验收已登记该"双层防线"口径）。

### 2.3 缺口登记（建议事项的真实落点）

CI 模板（`RobWork/scripts/industrialrobot/ci/industrial-robot-windows.github-workflow.yml` 与 `industrial-robot-windows.gitlab-ci.yml`）测试阶段的 run-tests 正则当前钉 `^sdurws_ird_core_test$`——这是 WP-01-T02 交付时点"唯一已建测试目标"的历史口径；此后各单元 `_test` 目标陆续落地（testkit/evidence/project/runtime/policy 等），**runtime（及其余单元）的测试目标尚未进入 CI 执行面**，与 ARCH §11.2 第 2 条"构建红线门禁随首批源码启用、CI 常驻"的目标存在执行面差距。

### 2.4 登记请求

| 接收方 | 请求 | 辖域说明 |
| --- | --- | --- |
| WP-01（CI 门禁所有者，WP-01-T02 交付物维护方） | CI 测试阶段执行面由单目标正则扩展为全部单元 `_test`/`_contract_test` 目标（或按单元矩阵逐目标），使 runtime UT-BUILD 四例与其余单元测试随 CI 常驻 | `scripts/` 与 `ci/` 不在 RT-T13 allowedFiles，扩展动作归 WP-01 |
| WP-24（交付与部署基础） | WP-24-T01 冻结版本基线首版登记时，复核 CI 执行面对"全部单元测试目标"的覆盖状态（含本条扩展是否已完成） | WP-24-T01 依赖 WP-01-T01，基线登记即自然复核点 |

---

## §3 提交方声明

- 本提交件内容为**事实登记与建议**，不改变任何需求/架构语义，不裁决任何未决项（O-12 维持待裁决；引擎过渡态的处置权在 WP-01）。
- 本提交件随 RT-T13 验收请求送验；接收方处置后请在本文件追加处置记录（或于各自交付物中回链本文件）。
