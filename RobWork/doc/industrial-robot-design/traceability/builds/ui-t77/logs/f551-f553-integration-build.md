# F-551/F-553 修复批——集成模式全量构建留痕（2026-10-12）

- 命令：`cmake --build build --config Release`（全量增量；构建前 `grep RWS_BUILD_INDUSTRIALROBOT build/CMakeCache.txt` 确认 `:BOOL=ON`）
- 结果：退出码 0；输出扫描 `error C|error LNK|error MSB` 命中 0（零错误）
- 受影响目标（先行单测构建均通过）：
  sdurws_ird_requirements_plugin、sdurws_ird_ui、sdurws_ird_ui_plugin、
  sdurws_ird_ui_test、sdurws_ird_ui_contract_test、sdurws_ird_ui_gui_test、
  sdurws_ird_requirements_test
- 既有警告（非本批改动面）：workflow/test/HostAdapterTest.cpp C4834（[[nodiscard]] 放弃返回值——基线既有）
