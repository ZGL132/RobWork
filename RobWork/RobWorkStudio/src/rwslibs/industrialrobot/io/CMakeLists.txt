# =====================================================================
# io 单元（项目 I/O：路径安全/CSV/JSON/资源读取/.rwpack 包）构建脚本
#
# 设计依据：
#   - units/io.md §3.3（命名空间/目标/CMake 集成：骨架 INTERFACE → IO-T01
#     升级 STATIC；别名 RWS::ird::io；测试目标随 IO-T06 登记，不预建；
#     链接 sdurws_ird_core＋sdurws_ird_diagnostics＋L1 文件/XML 目标——
#     选型 P-IO-3 冻结后登记；零 Qt、零 Widgets）、§3.4（源码目录布局）、
#     §2.1（拥有/不拥有总表）、§12 IO-T01 行（产物与验收）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项：L2 计算内核零 Qt——R-3）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界——无私有
#     头出 include）；红线 R-1（业务单元互链禁止）/R-2（跨单元私有头
#     禁止）/T-1（产品目标不链 testkit）
#
# 本文件随 IO-T01（≙WP-11-T02，任务契约 tasks/foundation/IO-T01.json）
# 落地：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_io 不变，
#      别名 RWS::ird::io 不变，公共 include 根不变）；
#   2. 公共头就位 IoFwd.hpp/IoError.hpp（§3.1 表前两行——类型契约头，
#      inline/纯值定义自含，无对应 .cpp）；src/ 空起步＝锚点翻译单元
#      Io.cpp（core/policy/diagnostics/project 同先例：STATIC 库至少一个
#      翻译单元纪律，零任何接口实现预建——NFR-MNT-04，契约 acceptance 1）；
#   3. P-IO-3 处置（契约 acceptance 4）：ZIP/XML 候选库（libzip/miniz；
#      expat/pugixml 或 RobWork 自带）仅做 vcpkg 可用性验证并留痕
#      （traceability/io-pio3-dependency-probe.md），**选型冻结权在
#      WP-11 评审**——本脚本冻结前不登记任何 L1/第三方链接目标
#      （NFR-DEP-03 离线约束：不私裁引入依赖）；
#   4. 不注册测试目标：sdurws_ird_io_test / sdurws_ird_io_contract_test
#      随 IO-T06/T07 登记（§3.3"随 IO-T06 登记，不预建"＋§12 IO-T01
#      禁止项"预建空测试目标"——契约 acceptance 3）；因此本文件无
#      find_package(GTest)——gtest 接入随上述任务落位时按 DTB §5.5
#      引入（diagnostics/project CMakeLists 同款机制，不在无测试目标的
#      单元里提前配置）。
# =====================================================================

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# src/Io.cpp＝落位期锚点翻译单元（src/ 空起步——core/src/Core.cpp、
# policy/src/Policy.cpp、diagnostics/src/Diagnostics.cpp、
# project/src/Project.cpp 同先例；零源码 STATIC 在 CMake 生成期即报
# "No SOURCES given to target"，实测）。IO-T02 起按 §3.4 布局增列：
# SafePath.cpp/Budget.cpp（IO-T02）、Csv.cpp（IO-T03）、Json.cpp/
# ZipChannel.cpp（IO-T04）、ResourceIo.cpp（IO-T05）、Package.cpp/
# TempArea.cpp/AtomicFile.cpp（IO-T06）——同名任务文件按 §12 行增列。
add_library(sdurws_ird_io STATIC
    src/Io.cpp)

# C++17（单元卡 §1.4／core.md D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：io 公共头使用 C++17 设施（std::string_view/inline constexpr
# 变量等——§1.4），消费者编译 io 头也必须至少 C++17。
target_compile_features(sdurws_ird_io PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_io PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 io 头的翻译
# 单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_io PUBLIC /utf-8)
endif()

# 公共头根：仅 io/include（内部含 sdurws/ird/io/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 io 唯一对外的头暴露
# 位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋diagnostics（§3.2 依赖表仅有的两条单元边，与 ARCH §3.5
# 白名单、ird_gates_whitelist.cmake "io->core"/"io->diagnostics" 行一致）。
# PUBLIC 形态：io 公共头（IoFwd/IoError 起步，后续任务扩展）的契约类型面
# 将随任务进展内嵌 core 承载的值类型（Digest256/诊断构造输入等——§3.2
# 消费表）与 diagnostics 承载的 DiagnosticRecord 基类型，消费方必须随链
# 到达。当前落位期两公共头为纯标准库头、零 ird 头消费（与 PRJ-T01 的
# P-PR-1 同款口径：仅建立链接边，消费随首个消费任务按冻结签名进入）。
# 零 L1/第三方目标：ZIP/XML 选型 P-IO-3 未冻结（§3.3"冻结后登记"——
# 契约 acceptance 1/4），冻结评审通过后由 IO-T04 以增量修订登记于此。
# 零 Qt（D-01/NFR-MNT-01）、零 Widgets（WP-11-T02 禁止项）、零其他
# industrialrobot 单元（R-1——io 不直链任何业务域单元，协作走六类端口/
# 注入面，§10）、零 testkit（T-1——testkit 仅随 _test/_contract_test 目标
# 消费，本文件无测试目标）。
# 用统一真名 sdurws_ird_core/sdurws_ird_diagnostics：集成与独立冒烟两种
# 模式下都可解析（别名 RWS::ird::* 仅集成模式存在——core/diagnostics
# 落位时同款取舍）。
target_link_libraries(sdurws_ird_io PUBLIC
    sdurws_ird_core
    sdurws_ird_diagnostics)

# 别名（集成模式）：供消费方单元以 RWS::ird::io 消费；冒烟模式无别名
# （与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::io ALIAS sdurws_ird_io)
endif()

# ---- 测试目标：本任务不注册（契约 acceptance 3 显式登记）----
# sdurws_ird_io_test（单元/黄金）与 sdurws_ird_io_contract_test（契约/故障
# 注入）随 IO-T06/T07 落地（§3.3"随 IO-T06 登记，不预建"；§12 IO-T01
# 禁止项"预建空测试目标"；父 CMakeLists"不在骨架中预建空目标"约定）。
# 届时按 DTB §5.5 接入 gtest（find_package(GTest CONFIG REQUIRED) 失败即
# 停）＋ird_add_gtest 宏（add_test＋XML 报告目标——diagnostics/project
# 单元 CMake 内宏定义先例，本单元随首个测试目标自持定义）。

# ---- 依赖红线守卫（配置期断言——diagnostics/project 同款自律）----
# io 的 industrialrobot 单元依赖仅限 core＋diagnostics（§3.2/§3.3：ARCH
# §3.5 仅有的两条登记出边）；零 Qt（R-3/NFR-MNT-01——比 L3 上限更严，
# D-01 精神：io 无界面职责）；产品库本体零 gtest（D-06——断言宏只在
# 消费方 _test 目标展开；本文件无测试目标，gtest 边即违例）。两面读取
# （LINK_LIBRARIES 含 PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）
# 防漏。属性未设置时 get_target_property 返回 "<var>-NOTFOUND" 字面串，
# 不匹配任何模式，不会误报。正式全仓门禁（依赖图白名单）归 ird_gates
# （WP-01-T01），本守卫不替代它。
get_target_property(_ird_io_link_libs sdurws_ird_io LINK_LIBRARIES)
get_target_property(_ird_io_iface_libs sdurws_ird_io INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_io_link_libs _ird_io_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                       AND NOT _lib STREQUAL "sdurws_ird_diagnostics")
        message(FATAL_ERROR
            "io 依赖红线（R-1/R-2）：sdurws_ird_io 仅可链接 core＋diagnostics"
            "（units/io.md §3.2/§3.3、ARCH §3.5），发现单元目标 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "io 依赖红线（R-3/NFR-MNT-01，零 Qt——io.md §1.4/§3.3）：发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$|^sdurws_ird_testkit$")
        message(FATAL_ERROR
            "io 依赖红线（D-06/T-1）：产品库本体不链接 gtest/testkit，发现 ${_lib}")
    endif()
endforeach()
