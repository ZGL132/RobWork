# REUSE-T01 IK/FD 草案治理独立验收

冻结 SHA：8a7174945a8f280f674a40ec46a512b5b5f1b414。结论：PASS（仅草案治理）。未发现本范围需返工问题。

两份草案均明确未批准，三条功能关系为 proposal/proposal 且提案规范化摘要一致。IK 保留完整位姿/仅位置原API优先路线，EAA与Euler、返回候选与真实收敛的差异与源码及已归档反例相符；新增任务数学、回调与观测接口仍需逐项批准。ODE先供给现成后端，不申请新FD核心，OFF/NOTFOUND仅为本机事实；Force路径、引擎API及target声明真实。原gtest接线存在既有ODESimpleTest扩展名省略，草案已保留资产/依赖不完整时如实报告的约束；未声称本轮已能构建运行。

7条检索重放退出码及命中集合全部一致；18个上游绑定当前/基点摘要全部一致；3个proposal摘要正确。task/docs/reuse及本审查范围diff均exit 0。整提交diff-check exit 2仅命中随批归档的原始日志/源码摘录尾空格与空行（见diff.log），未作为草案失败或修改历史原件。矩阵121条＝118 pending＋3 proposal，closedFiles=0。

PASS不批准实施框架patch、修改vcpkg或生产迁移，不是全T01退出或T02/T03-B/T04-B准入。ODE实际供给、任务数学/ABI/性能/取消与生产证据仍待后续批准和验证。本轮仅审草案，不重跑C++/GUI/API/ODE实验或远程CI，不重新背书同提交其他证据。父树未改，无提交、推送或合入。

根目录证据：search-replay.json、search-1..7.log、binding-checks.json、proposal-bindings.json、source-review.log、authority-evidence.log、semantic-excerpts.log及各门禁日志。
