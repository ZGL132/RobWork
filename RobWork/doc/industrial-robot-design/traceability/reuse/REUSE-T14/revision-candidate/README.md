# REUSE-T14 修订候选 R2：fma 残差 + TwoSum 精确折叠（隔离实验）

基线 `6fb26b31ddcd514ba2d3dab8ff548b977b54c9c6`（分支 codex/reuse-foundation）。
本目录是 GAP-CORE-C4-01 修订提案的隔离实验现场；旧失败原件（`../oracle-result.json`、
`../rejected-candidate.patch` 等）原样保留、未被覆盖。候选代码只存在于本目录，
不进入产品目标；生产补丁形态见 [candidate.patch](candidate.patch)。

## 候选定义

仅在 closeWithin 的"diff 与 limit 同时为 +Inf"分支内，把比较化为
`D = A/2 − rel·(|r|/2) − a/2 ≤ 0`（A=|v|+|r|；两侧同除 2），用
scalbn 精确减半＋`std::fma` 2Product 残差（rel·y = p+ep）＋Knuth TwoSum
叶子折叠（irdExactSign，合并最大两项、保守上界判定）求 D 的精确符号。
非分支路径逐位保持原实现；非有限 v/r 仍为 false。被拒候选（scalbn 缩放后
各自舍入相撞，6 组误绿）的死因被消除：折叠全程保持"叶子精确和＝D"不变式。

## 语料（29,072 组，seed 20261023）

| 语料 | 组数 | 说明 |
| --- | --- | --- |
| grid | 3,168 | 与 `../oracle.py` 逐位同源（11×8×9×4 确定性网格，含 DBL_MAX 邻域、阈值 nextafter 邻点、次正规、±0） |
| rand-cpython-mt | 10,000 | CPython MT19937 `init_by_array(20261023)` + `random()` 逐位复刻（本机无 Python，复刻算法见 candidate-probe.cpp） |
| adv-extreme | 12,960 | 6 个极值基点 × 1–2 ulp 邻点 × 符号 × 12 个阈值邻点 rel × 5 个绝对项 |
| adv-subnormal | 2,880 | 最小正规数/次正规数邻域 × 符号 × 阈值 |
| named | 44 | `../oracle-result.json` 全部 24 组失配原件（前 6 组 bothOverflow）＋GAP 三红例及镜像 |
| special | 17 | 非有限 v/r（qnan/snan/±Inf）与 ±0 组合 |

精确期望由 boost::multiprecision::cpp_int 按输入 binary64 的整数分解独立计算
（实验现场独立 oracle；生产核心白名单仍为标准库+sdurw_math，Boost 不进产品）。

## 命令与结果

构建/运行（VS 2022 x64，见 build.log 全文）：

```
cl /std:c++17 /O2 /EHsc /utf-8 /I <core>/include /I <vcpkg>/installed/x64-windows/include ^
   candidate-probe.cpp compare-candidate.cpp /Fe:c4-rev-probe.exe
c4-rev-probe.exe
```

三次运行（含复跑两次）退出码 0，计数完全一致（[run.log](run.log)、[result.json](result.json)）：

```
cases=29072 branchHits=10708 branchFixed=2524 overflowMismatch=0
invarianceViolation=0 normalOracleDiff=74 sentinel=0 maxIter=5
VERDICT: PASS
```

- **overflowMismatch=0**：溢出分支内候选与精确 oracle 在 10,708 组全一致；
  被拒候选的 6 组误绿及全部 named-overflow 原件被纠正（branchFixed=2,524 含
  grid 红族与对抗镜像）。
- **invarianceViolation=0**：非分支路径与非有限域候选与原实现逐位一致——
  正常路径不变的硬约束成立。
- **normalOracleDiff=74**：非分支既定舍入口径差异（原实现同样存在，属批准
  范围外的既有语义；T14 小语料对应量为 18）。
- **sentinel=0, maxIter=5**：折叠循环全语料 ≤5 轮收敛，256 轮上限含充分余量。

## 实验过程记录（含失败迭代）

1. 首版候选把绝对项误为 `−a`（未随两侧同除 2 减半），大绝对项分支 1,147 组
   失配——算法错误，已修正为 `−a/2`；修正前的中间运行未保留通过样本，仅此记录。
2. 探针首版 ulp 取 `nextafter(m,+inf)−m = Inf`，对抗语料意外生成 ±Inf 输入，
   且 oracle 被调用于非有限域（250 组假失配）——探针错误，已改为有限方向 ulp、
   非有限域契约核对（两实现一致为 false）、oracle 限缩到有限输入。
3. 最终形态即上述 PASS 结果；两次复跑逐位一致。

## 边界与不宣称

- 有限样本对照，不宣称全 double 输入正确性；精确性主张由提案引理1–4支撑
  （引理证明义务列入独验范围）。
- 唯一已知的候选与精确实数语义的偏差：**a 为次正规数**时 `a/2` halving 舍入
  偏差 ≤2^-1075；触达该偏差需要次正规绝对项与 ~2^-1000 量级的有理对齐同时
  构造成功，记录在案的失败边界（2^918–2^970 量级裕度）均不受影响。
- Q 重载无 rw 头不编（`IRD_CORE_HAS_RW_Q` 门控）；vector 重载已验证消费同一
  标量入口；Q 路径与 vector 同委托标量，随生产实施一并回归。
- 独立验收 NOT RUN，不自签。
