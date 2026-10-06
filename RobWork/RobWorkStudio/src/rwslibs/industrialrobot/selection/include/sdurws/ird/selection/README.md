# selection

selection 单元公共头目录（命名空间 `sdurws::ird::selection`）——**已随
WP-19-T02 落位**（2026-10-06，构建落位任务；此前本目录仅为占位保留位，
其文案指向"卡 §9"与本卡章节编号不符〔实现任务＝卡 §16〕，随落位一并
修正——units/selection.md §1.2/§19.3 登记项闭环）。

## 单元定位

L4 业务域单元（二分结构）：从动力学与传动结果形成电机—减速器方案——
目录包/能力曲线/电机减速器硬筛选/组合校核（经③端口消费唯一
`DriveTrainMappingEvaluator`，不自建映射）/可行集与逐项淘汰原因/器件
回填。计算库 `sdurws_ird_selection` 零 Qt（R-3/NFR-MNT-01）；编译依赖
仅 core＋evidence（卡 §3.2——policy/io/project/diagnostics/ui/runtime/
drivetrain 走运行时注入/端口，零编译边）；插件目标
`sdurws_ird_selection_plugin` 为唯一 Qt 例外载体（最小可注册实现——
自持描述符装配门面，界面面随 WP-19-T10）；worker 不默认创建
（D-SEL-13——批量评估经 execution 承载）。

## 当前公共头（随任务增列）

| 头文件 | 内容 | 落位任务 |
| --- | --- | --- |
| `DiagCodes.hpp` | SEL-* 稳定诊断码登记表（17 码常量＋登记行清单——L5 装配期注册进 diagnostics StableCodeRegistry 的数据源；SEL 前缀已在册无补登事项） | WP-19-T02 |

另有装配契约头 `assembly/sdurws/ird/selection/SelectionPluginAssembly.hpp`
（插件目标 PUBLIC include 面——非本目录扫描域，见单元 CMakeLists 注）。

## 后续落位（见 units/selection.md §3.5/§16）

- `CatalogTypes.hpp`/`CatalogProvider.hpp`/`Curve.hpp`/`Screening.hpp`/
  `Combination.hpp`/`FeasibleSet.hpp`/`ResultFacts.hpp`/`Backfill.hpp`
  ＋目录模型/导入校验/插值/硬筛选/组合校核/可行集/回填实现——随
  WP-19-T03～T09（卡 §3.5 布局表任务列权威）；
- selection 插件界面（目录管理页/筛选条件/候选表/淘汰原因视图/回填入口
  ＋IUiTreeNodesProvider/IUiPropertyPagesProvider 域供给——UI-T21/T22
  协议）——随 WP-19-T10；
- 契约测试业务用例与选型黄金数据集（`testdata/golden/sel-*`）——随
  WP-19-T11。

## 关键口径

- 依赖边界：selection **不自建传动映射**——组合校核经③端口消费唯一
  `DriveTrainMappingEvaluator`（键 `dt.mapping`；SEL-05/ARCH §7.10；
  P-SEL-2 推荐端口形态）；目录文件层读取/安全归 io，业务 schema 与语义
  归 selection（双层校验，D-SEL-3）。
- 能力曲线：分段线性、闭区间、**默认禁止外推**（`SEL-CURVE-
  EXTRAPOLATION-DENIED`）；插值失败≠候选能力不足（数据不足与能力不足
  分轨——D-SEL-5）。
- 错误语义：调用方错误（schema/身份/维度/非有限——致命输入）校验边界
  fail-fast 快速拒绝；数据/环境类（字段缺失/插值失败/上游缺口）返回
  诊断＋降级素材（卡 §14.0）；淘汰原因逐项保留实际值/要求值/单位/阈值
  来源（SEL-06/ERR-01）。
