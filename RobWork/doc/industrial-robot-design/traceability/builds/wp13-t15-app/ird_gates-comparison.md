# ird_gates 引擎直跑比对（base 1b3a7371 .. branch wp13-t15-app 工作树）
- 比对口径：引擎直跑（cmake -DIRD_ROOT=... -DIRD_SELFTEST_DIR=... -P cmake/ird_gates.cmake），IRD_ROOT 路径剥离归一化后排序 diff；base 侧经临时 worktree，用毕即删。
- 结果：base 127 行 / branch 129 行（引擎逐命中双次发射，逻辑命中数 63/64）；净新增恰 1 处逻辑命中：
  IRD-GATE-R1: 业务域目标互链：sdurws_ird_modeling_app → sdurws_ird_modeling_plugin（同单元引用；与 T15 插件链接面既有两处命中同性质，登记册回填归 WP-01-T03）
- 其余差集均为路径形态噪声（base 侧 worktree 绝对路径 vs branch 侧仓库相对路径，同一命中集）。
