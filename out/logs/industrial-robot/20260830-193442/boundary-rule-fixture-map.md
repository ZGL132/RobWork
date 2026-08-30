# WP-01-T01 规则-夹具映射表

| 规则 | 夹具 | 关键词 | 定位示例 |
| --- | --- | --- | --- |
| R2 旧插件目标依赖 | old-plugin-dependency | 旧插件目标依赖 | CMakeLists.txt:6 sdurws_robotmodelbuilder |
| R1 QWidget/QApplication 头包含 | widget-header | QWidget/QApplication 头包含 | include/sdurws/ird/core/Sample.hpp:4 |
| R3 未登记 target | unregistered-library | 未登记 target | CMakeLists.txt:6 some_unregistered_lib |
| R4 运行时名称拼接 | name-concatenation | 运行时名称拼接 | plugins/sample/NameConcat.cpp:8 |
| R0 IO | （失败测试） | 扫描根不存在或不可读 | -ScanRoot ./nonexistent-dir-xyz |
| R5/R6 | （无夹具；正式树暂无 CMake 内容，随 WP-01-T02 起生效） | 碰撞默认值/安全距离；安装规则 | check-boundaries.ps1 规则头注释 |
| R7 | （无夹具；令牌等价断言＋扫描器自扫描零命中验证） | 虚拟平台设置/GUI 并行 | boundary-clean-tree.log R7 token assertion |

- 退出码：io-failure=1, name-concatenation=1, old-plugin-dependency=1, unregistered-library=1, widget-header=1; clean=0
- R7 令牌等价=True；扫描器源码禁词字面量计数=0
- 生成时间：2026-08-30 19:34:44
