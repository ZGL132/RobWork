# F-540 修复验证实录（2026-10-07，分支 ui-t70）
# 改动＝ird_gates_whitelist.cmake R-3 登记注释短语「ui 单元界面目标」并回单行（WP-22-T02 返工 3fdafa36 拆行的更正——内容零改动仅换行重排）＋更正注两行

== 集成 ui_test（build 树现役二进制，元测试运行时直读源树白名单）==
[==========] 254 tests from 25 test suites ran.
[  PASSED  ] 254 tests.

== 冒烟 ui_test（build-smoke/ui-t68 树）==
[==========] 239 tests from 22 test suites ran.
[  PASSED  ] 239 tests.

== ird_gates（cmake --build build --config Release --target ird_gates）==
exit=0——R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中＋引擎自测 9 项符合预期
