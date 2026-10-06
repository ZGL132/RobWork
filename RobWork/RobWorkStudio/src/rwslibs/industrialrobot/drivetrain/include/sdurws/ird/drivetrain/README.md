# drivetrain

drivetrain 单元公共头目录（命名空间 `sdurws::ird::drivetrain`）——**已随
WP-18-T02 落位**（2026-10-06，构建落位任务；此前本目录仅为占位保留位，
其文案指向"卡 §9"与本卡章节编号不符，随落位一并修正——units/drivetrain.md
§18.3 登记项闭环）。

## 单元定位

L2 共享计算服务（非 L4 业务插件）：dynamics 与 selection 之间的唯一共享
传动计算服务——`DriveTrainMappingEvaluator`（评估键 `dt.mapping`）为
DYN-04 传动映射的唯一实现（虚功映射/交叉耦合/工作点/效率/反射惯量/能量
分项）。零 Qt（R-3/NFR-MNT-01）；依赖白名单仅 core＋evidence（ARCH §3.5）；
无 plugin/worker 目标（卡 §2.3/§4.2 明文默认禁止）。

## 当前公共头（随任务增列）

| 头文件 | 内容 | 落位任务 |
| --- | --- | --- |
| `DiagCodes.hpp` | DT-* 稳定诊断码登记表（19 码常量＋登记行清单——L5 装配期注册进 diagnostics StableCodeRegistry 的数据源；P-DT-7） | WP-18-T02 |

## 后续落位（见 units/drivetrain.md §4.2/§15）

- `MappingTypes.hpp`/`MappingCore.hpp`/`Series.hpp`/`Facts.hpp`/`Evaluator.hpp`
  ＋映射核心实现——随 WP-18-T03（映射实现）；
- 黄金数据集与契约测试业务用例——随 WP-18-T04；
- R2 耦合矩阵全链消费——随 WP-18-T05。

## 关键口径

- 传动比全文唯一口径：**c ＝ Δq_joint / Δθ_motor**（减速器 n:1 对应
  c＝1/n；虚功对偶 τ_motor＝Cᵀ·τ_joint）——D-DT-4/P-DT-10，禁用第二种
  未声明约定。
- 错误语义：调用方错误（空输入/维度不匹配/非法矩阵/非有限值）fail-fast；
  环境错误/数据类情形（效率缺失/样本缺失）返回诊断＋完整性降级素材。
