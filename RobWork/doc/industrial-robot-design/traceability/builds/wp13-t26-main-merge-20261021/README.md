# WP-13-T26 主线合入现场验证

日期：2026-10-21。所有者指令：合入主分支；唯一开发主线为redesign-main，冻结main不动。

产品合入：7bc8eacd5fc0，送验冻结对象c0a19e7399748268ff1bb41d2db208be29b33473。
合并暂存树与冻结对象一致，未新增产品行为；源wp13-t24/wp13-t25/wp13-t26保留。
独立验收：acc/WP-13-T26/3@0fb2ffe2，verdict=pass，记录位于traceability/acceptance/WP-13-T26-20261020-r3.md。

现场真实执行：VS2022 x64，GUI平台windows，绝对exe，一次一个GUI进程。
- modeling：462通过，0失败，0跳过。
- ui_contract：63通过，0失败，0跳过。
- modeling_gui：91通过，0失败，0跳过。

命令：pwsh -NoProfile -File build/modeling-integration-20261020/run.ps1 -Mode test -Name <套件名>。
三套件XML/log/ird报告随本目录保存；脚本路径供本树复跑，其他检出可按验收记录的绝对exe命令重现。

提交信息检查：90e9c041..7bc8eacd范围74条，71通过、3条历史格式债（a143dbab52ce、844d8d174b55、96010e0f062b缺验证段）；不改写历史，新合并提交自身通过，沿F-012历史豁免口径如实登记。

本轮仅完成集成合入；47项完整终验、真实mesh与三维覆盖层、F07持久来源等开放项继续追踪，未标为通过。
远程交付以实际push成功为准，本记录不预先声明同步成功。
