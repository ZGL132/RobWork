# UI-T24 ird_gates 命中集增量登记（DTB §4.5 机器消费面）

- 任务：UI-T24（宿主默认布局收敛与域命令文案语义化），分支 ui-t24，base＝67e1d45d443ce920e6431ad224603a795c506964。
- 取证口径：ird_gates 引擎直跑（`cmake -DIRD_ROOT=<源码根> -P ird_gates.cmake`，WP-15-T18 先例同款）——base 于 detached worktree @67e1d45d、head 于本任务工作树，双侧同通道输出；归一化＝提取 IRD-GATE- 行→剥离 `[ird_gates]` 前缀→剥离 IRD_ROOT 路径前缀（消除 base/head 检出路径差异）→sort -u。
- 结果：**base 96 条 → head 97 条，恰增 1 条，零意外命中**。机器面＝本目录 `ird-gates-base-norm.txt`（96 行）与 `ird-gates-head-norm.txt`（97 行）；`diff` 增量恰为下表一行。

## 增量条目（1 条）

```text
IRD-GATE-R3: ui 产品面疑似包含 Qt 类头（Q+大写约定）：ui/include/sdurws/ird/ui/FlowLayout.hpp
```

## 登记

| 红线 | 例外范围 | 依据 | 登记落点 |
| --- | --- | --- | --- |
| R-3（Qt 禁入计算核心，命中集增量） | `ui/include/sdurws/ird/ui/FlowLayout.hpp`（ui 单元公共呈现构件——流式栅格布局，本任务 P2 新增；`#include <QLayout>/<QRect>/<QSize>/<QWidget>` 命中 Q+大写类头扫描） | NFR-MNT-01 ui 例外类既定形态（UI-T20/T21/T22 各自"恰增 1 条 R3＋登记件"同族先例；本头是 ui 界面构件的公共契约面——Qt 类型为构件本体而非计算逻辑渗入） | DTB §4.5 登记册 UI-T24 行（随本任务文档同步提交）；责任方 WP-01-T03 抽查 |

## 附注

- 本任务零新增目标、零新增链接边（FlowLayout.cpp 编入既有 `sdurws_ird_ui` 源集）；依赖图不变式保持，白名单数据文件零改动。
- 集成构建树 `ird_gates` 目标（契约 verify 2）执行留痕＝本目录 `ird-gates-head.log`（经 MSBuild 通道，命中行 67 行含前缀变体；与引擎直跑归一口径的差异仅为通道回显格式，命中集合一致——F-207/F-231/F-309 留痕乱码家族口径下以引擎直跑 UTF-8 输出为归一基准）。
- 门禁目标的退出码语义（"任一命中即失败"）在库内既有登记态命中基线上本就非零（WP-24-T09 head 同为 exit 1＋89 处命中）——验收判据按既定方法论消费**归一命中集增量**（本任务恰增 1 条、已登记），不消费退出码绝对值。
