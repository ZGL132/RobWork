# =====================================================================
# ird_gates.cmake — 工业机械臂设计软件 依赖红线与补丁门禁引擎（WP-01-T01）
#
# 设计依据（契约 tasks/WP-01-T01.json designRefs）：
#   - ARCHITECTURE.md §3.2（四条构建红线 R-1~R-4 与 SA-10"红线即构建门禁"）
#   - ARCHITECTURE.md §3.5（单元级依赖表——边白名单唯一数据源）
#   - ARCHITECTURE.md §5.2（SA-02 框架零修改＋补丁登记核对）
#   - ARCHITECTURE.md §7.2（六类稳定端口——跨单元只经公共头/端口协作）
#   - development-task-breakdown.md §2.2（WP-01-T01 任务卡）§4.5（例外登记册）
#   - units/policy.md §3.4（R-5 proximity 直链禁止——红线扩展）
#   - units/testkit.md §2.4（T-1/T-2 testkit 分发与依赖红线）
#
# 实现约束（DTB §2.2）：cmake -P 脚本模式＋git，零 Python；对骨架与已落位
# 目标现状零命中（INTERFACE 占位目标无链接边，不产生表外边误报）；未登记
# 例外被门禁检出即失败（含引擎自测用例验证）。
#
# 调用方式：
#   cmake -DIRD_ROOT=<industrialrobot 源码根> -P ird_gates.cmake
# 可选入参：
#   IRD_GRAPH_FILE   dependency-graph.json 路径（缺省按 git 仓库根推导）
#   IRD_ENABLE_GIT   SA-02 工作区核对开关（默认 ON；引擎自测的夹具树关闭）
#   IRD_SELFTEST_DIR 自测夹具输出目录（缺省用系统 TEMP；构建目标传入 build 树）
#   IRD_GATE_CHILD   自测子运行标记（内部使用，防止递归）
#   IRD_SELFTEST_CASE 只跑指定自测用例（内部调试用；缺省全跑）
#
# 退出语义：全部通过 → 正常退出（0）；任一命中 → message(FATAL_ERROR)，
# 构建目标即失败（退出码非 0）。每条命中以稳定码 IRD-GATE-<检查项> 前缀输出。
#
# 检查项与命中码：
#   IRD-GATE-R1   业务域单元目标互链禁止（ARC-02；AGENTS §5 红线 2）
#   IRD-GATE-R2   跨单元私有头禁止——include 路径与私有头可达性（ARC-02）
#   IRD-GATE-R3   L2 产品库零 Qt——链接边＋头包含（NFR-MNT-01/02）
#   IRD-GATE-R4   RobWork 名称前缀拼接/剥离静态扫描＋例外登记（ARC-04/NFR-MNT-07）
#   IRD-GATE-R5   proximity 直链/直含禁止（ARC-05/NFR-MNT-07；policy.md §3.4）
#   IRD-GATE-T1   产品目标禁链/禁含 testkit（testkit.md §2.4）
#   IRD-GATE-T2   testkit 仅依赖 core（testkit.md §2.4）
#   IRD-GATE-SUB  边不在 §3.5 白名单（表外边，含反向边与新边——ARCH §3.5 门禁实施）
#   IRD-GATE-GRAPH 白名单与 dependency-graph.json 数据源不一致（DTB §2.2 验收 3）
#   IRD-GATE-LIB  框架基线库登记约束（sdurw_loaders 须登记）／未知目标族
#   IRD-GATE-SA02 框架工作区出现登记外改动／补丁登记册损坏（ARCH §5.2）
#   IRD-GATE-SELF 引擎自测失败（检出能力或误报，DTB §2.2 验收 5）
# =====================================================================

cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------
# 第 0 步：装载白名单数据（与引擎同目录——数据/引擎分离，增补数据不改代码）
# ---------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/ird_gates_whitelist.cmake")

# 20 单元清单（ARCH §3.1 单元总表）：用于目标名解析与未知单元检出
set(IRD_KNOWN_UNITS
    core evidence policy runtime diagnostics project execution io
    reporting ui workflow modeling requirements kinematics trajectory
    dynamics drivetrain selection optimization testkit)

# 全部命中累积于此；任何非空即最终失败
set(IRD_HITS "")

# ---------------------------------------------------------------------
# 第 1 步：解析 industrialrobot 全部 CMakeLists.txt，提取
#   ① 目标清单（add_library，跳过 ALIAS/IMPORTED/变量名目标）
#   ② 链接边（target_link_libraries）
#   ③ include 路径（target_include_directories）
# 文本级解析的确定性前提：本仓库 CMake 风格统一（无嵌套括号的命令参数、
# 无块注释），正则 [^)]* 即可完整取参；变量名目标（骨架 foreach 的
# sdurws_ird_${ird_m}）无法静态展开，按"跳过＋注释声明"处理——这正是
# 验收第 2 条"INTERFACE 占位目标无链接边、不产生表外边误报"的要求面。
# ---------------------------------------------------------------------

# 收集待扫描的 CMakeLists.txt；跳过路径片段（构建树/自测夹具/.git）
set(IRD_SKIP_PATTERNS "/build" "-smoke" "/.git" "/_gate_fixture")

file(GLOB_RECURSE IRD_CMAKELISTS
    "${IRD_ROOT}/CMakeLists.txt"
    "${IRD_ROOT}/*/CMakeLists.txt")

foreach(_cl ${IRD_CMAKELISTS})
    # 跳过模式匹配【相对 IRD_ROOT 的路径】而非绝对路径——绝对路径匹配会把
    # 恰好位于 build/ 目录下的自测夹具（或任何路径含 build 字样的部署）整树
    # 误跳过，造成"零目标零命中"的假阴性（集成模式自测实录，2026-09-10）
    file(RELATIVE_PATH _rel_cl "${IRD_ROOT}" "${_cl}")
    set(_skip FALSE)
    foreach(_pat ${IRD_SKIP_PATTERNS})
        if(_rel_cl MATCHES "${_pat}")
            set(_skip TRUE)
        endif()
    endforeach()
    if(_skip)
        list(REMOVE_ITEM IRD_CMAKELISTS "${_cl}")
    endif()
endforeach()

# 读取并规整单个 CMakeLists：去行注释 → 压平空白（多行命令变单行，正则取参）
function(ird_read_flat path out_var)
    file(READ "${path}" _raw)
    string(REGEX REPLACE "#[^\n]*" "" _nocomment "${_raw}")
    string(REGEX REPLACE "[\r\n\t]+" " " _flat "${_nocomment}")
    string(REGEX REPLACE "  +" " " _flat "${_flat}")
    set(${out_var} "${_flat}" PARENT_SCOPE)
endfunction()

# 从压平文本中提取某命令的全部括号块（不含嵌套括号——仓库风格约束）
function(ird_extract_blocks flat_text command out_var)
    string(REGEX MATCHALL "${command}[ \t]*\\([^)]*\\)" _blocks "${flat_text}")
    set(${out_var} "${_blocks}" PARENT_SCOPE)
endfunction()

# 括号块 → 参数列表（去掉命令名与括号；CMake 列表以分号分隔，故空格→分号
# 后才能用 list()/foreach 逐参处理。前提：参数内不含空格——仓库 CMake 风格
# 中路径均无空格，见第 1 步"文本级解析确定性前提"）
function(ird_block_args block out_var)
    string(REGEX REPLACE "^[A-Za-z_][A-Za-z0-9_]*[ \t]*\\(" "" _inner "${block}")
    string(REGEX REPLACE "\\)$" "" _inner "${_inner}")
    string(STRIP "${_inner}" _inner)
    string(REPLACE " " ";" _args "${_inner}")
    set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()

set(IRD_TARGETS "")        # 全部显式目标名（不含变量名/ALIAS）
foreach(_cl ${IRD_CMAKELISTS})
    ird_read_flat("${_cl}" _flat)
    ird_extract_blocks("${_flat}" "add_library" _blocks)
    foreach(_b ${_blocks})
        ird_block_args("${_b}" _args)
        list(GET _args 0 _tgt)
        # 变量名目标（含 ${...}）静态不可展开——跳过（见第 1 步注释）
        if(_tgt MATCHES "\\$\\{")
            continue()
        endif()
        # ALIAS / IMPORTED 不是可链接检查的独立目标
        set(_is_alias FALSE)
        foreach(_a ${_args})
            if(_a STREQUAL "ALIAS" OR _a STREQUAL "IMPORTED")
                set(_is_alias TRUE)
            endif()
        endforeach()
        if(NOT _is_alias)
            list(APPEND IRD_TARGETS "${_tgt}")
        endif()
    endforeach()
endforeach()

# 目标名 → 单元 与 目标名 → 形态（bare/lib、plugin、worker、test、contract_test）
# 解析规则：sdurws_ird_<unit>[_<suffix>]；未知单元目标在边检查时报 SUB。
function(ird_classify_target name out_unit out_face)
    set(_unit "")
    set(_face "other")
    if(name MATCHES "^sdurws_ird_([a-z]+)$")
        set(_unit "${CMAKE_MATCH_1}")
        set(_face "product")
    elseif(name MATCHES "^sdurws_ird_([a-z]+)_(plugin|worker|app)$")
        set(_unit "${CMAKE_MATCH_1}")
        set(_face "${CMAKE_MATCH_2}")
    elseif(name MATCHES "^sdurws_ird_([a-z]+)_(test|contract_test)$")
        set(_unit "${CMAKE_MATCH_1}")
        set(_face "test")
    endif()
    set(${out_unit} "${_unit}" PARENT_SCOPE)
    set(${out_face} "${_face}" PARENT_SCOPE)
endfunction()

# 提取链接边：每条边记录 "目标|链接库"（规范化 RWS::ird::X → sdurws_ird_X）
set(IRD_EDGES "")
foreach(_cl ${IRD_CMAKELISTS})
    ird_read_flat("${_cl}" _flat)
    ird_extract_blocks("${_flat}" "target_link_libraries" _blocks)
    foreach(_b ${_blocks})
        ird_block_args("${_b}" _args)
        list(LENGTH _args _n)
        if(_n LESS 2)
            continue()
        endif()
        list(GET _args 0 _tgt)
        if(_tgt MATCHES "\\$\\{")
            continue()
        endif()
        # 第 2 个起：跳过可见性/链接限定关键字，其余为链接库
        set(_libs "")
        set(_rest "${_args}")
        list(REMOVE_AT _rest 0)
        foreach(_lib ${_rest})
            set(_is_kw FALSE)
            foreach(_kw PUBLIC PRIVATE INTERFACE LINK_PUBLIC LINK_PRIVATE LINK_INTERFACE_LIBRARIES debug optimized general)
                if(_lib STREQUAL _kw)
                    set(_is_kw TRUE)
                endif()
            endforeach()
            if(NOT _is_kw)
                set(_norm "${_lib}")
                # 别名规范化：RWS::ird::X 即 sdurws_ird_X（ARCH §1.4 目标别名）
                if(_norm MATCHES "^RWS::ird::([a-z_]+)$")
                    set(_norm "sdurws_ird_${CMAKE_MATCH_1}")
                endif()
                list(APPEND _libs "${_norm}")
            endif()
        endforeach()
        foreach(_lib ${_libs})
            list(APPEND IRD_EDGES "${_tgt}|${_lib}")
        endforeach()
    endforeach()
endforeach()

# 提取 include 路径声明：记录 "目标|原始路径|所在CMakeLists目录"
set(IRD_INCDIRS "")
foreach(_cl ${IRD_CMAKELISTS})
    ird_read_flat("${_cl}" _flat)
    ird_extract_blocks("${_flat}" "target_include_directories" _blocks)
    foreach(_b ${_blocks})
        ird_block_args("${_b}" _args)
        list(GET _args 0 _tgt)
        if(_tgt MATCHES "\\$\\{")
            continue()
        endif()
        get_filename_component(_cl_dir "${_cl}" DIRECTORY)
        foreach(_a ${_args})
            if(_a STREQUAL _tgt)
                continue()
            endif()
            set(_is_kw FALSE)
            foreach(_kw PUBLIC PRIVATE INTERFACE SYSTEM BEFORE AFTER)
                if(_a STREQUAL _kw)
                    set(_is_kw TRUE)
                endif()
            endforeach()
            if(NOT _is_kw)
                list(APPEND IRD_INCDIRS "${_tgt}|${_a}|${_cl_dir}")
            endif()
        endforeach()
    endforeach()
endforeach()

# 命中登记辅助
function(ird_hit code detail)
    set(IRD_HITS ${IRD_HITS} "IRD-GATE-${code}: ${detail}" PARENT_SCOPE)
    # 同时打印，便于构建日志直接定位（PARENT_SCOPE 传递不动当前列表副本）
    message("IRD-GATE-${code}: ${detail}")
endfunction()

# ---------------------------------------------------------------------
# 第 2 步：链接边检查（R-1/R-5/T-1/T-2/R-3 链接面/SUB/LIB）
# ---------------------------------------------------------------------

# 判断目标名是否在豁免清单
function(ird_in_list value lst out_var)
    list(FIND lst "${value}" _idx)
    if(_idx GREATER -1)
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

foreach(_edge ${IRD_EDGES})
    string(REPLACE "|" ";" _pair "${_edge}")
    list(GET _pair 0 _tgt)
    list(GET _pair 1 _lib)

    ird_classify_target("${_tgt}" _t_unit _t_face)
    ird_classify_target("${_lib}" _l_unit _l_face)

    # ---- 2a. 链接目标是 ird 单元目标：按形态走 T 规则或白名单 ----
    if(_l_face MATCHES "^(product|plugin|worker|app)$")
        # 形态一：测试目标链接产品目标——仅允许【同单元】产品目标（testkit.md §2.4
        # 允许形态"被测产品目标"；跨单元契约语义走 _contract_test＋testkit 替身，
        # 若确需直链他单元产品目标，属未登记例外 → SUB）
        if(_t_face STREQUAL "test")
            if(NOT _l_unit STREQUAL _t_unit)
                ird_hit("SUB" "测试目标 ${_tgt} 直链他单元产品目标 ${_lib}（允许形态仅同单元被测目标；跨单元需求经 testkit 替身或 DTB §4.5 登记）")
            endif()
        else()
            # 形态二：产品目标之间——边必须落在 §3.5 白名单（ARCH §3.5：表外边＝构建失败）
            list(FIND IRD_ALLOWED_UNIT_EDGES "${_t_unit}->${_l_unit}" _idx)
            if(_idx EQUAL -1)
                # 业务域互链额外以 R-1 命中码标出（更醒目；R-1 无例外，SA-10）
                list(FIND IRD_BUSINESS_UNITS "${_t_unit}" _bi)
                list(FIND IRD_BUSINESS_UNITS "${_l_unit}" _bj)
                if(_bi GREATER -1 AND _bj GREATER -1)
                    ird_hit("R1" "业务域目标互链：${_tgt} → ${_lib}（ARC-02；R-1 无例外）")
                else()
                    ird_hit("SUB" "表外依赖边 ${_t_unit}->${_l_unit}（${_tgt} → ${_lib}）不在 ARCH §3.5 白名单")
                endif()
            endif()
        endif()

        # 形态三：产品目标链接 testkit＝T-1（testkit 不随产品分发）
        if(_lib STREQUAL "sdurws_ird_testkit")
            if(NOT _t_face STREQUAL "test")
                ird_hit("T1" "产品目标 ${_tgt} 链接 testkit（testkit.md §2.4 T-1：产品目标不得链接/包含 testkit）")
            endif()
        endif()

        # 形态四：testkit 自己的依赖＝T-2（只允许 core）
        if(_tgt STREQUAL "sdurws_ird_testkit")
            if(NOT _lib STREQUAL "sdurws_ird_core")
                ird_hit("T2" "testkit 目标链接 ${_lib}（testkit.md §2.4 T-2：testkit 只依赖 core＋标准库）")
            endif()
        endif()
    endif()

    # ---- 2b. Qt 链接（R-3 链接面）：L2 产品库（bare）与 worker 禁止 ----
    # Qt 目标名两种惯例：命名空间式 Qt5::Widgets／Qt::Core；旧式 Qt5Widgets／Qt5
    if(_lib MATCHES "^(Qt[0-9]?::|Qt[0-9]?$|Qt[0-9]?[A-Za-z]+$)")
        if(_t_face MATCHES "^(product|worker)$")
            ird_in_list("${_tgt}" "${IRD_R3_EXCEPTION_TARGETS}" _ex)
            if(NOT _ex)
                ird_hit("R3" "L2 目标 ${_tgt} 链接 Qt 库 ${_lib}（NFR-MNT-01：计算核心零 Qt；例外仅 ui 界面目标形态）")
            endif()
        endif()
    endif()

    # ---- 2c. proximity 链接（R-5 链接面）：产品/测试目标一律禁止，policy 豁免 ----
    if(_lib STREQUAL "sdurw_proximity")
        ird_in_list("${_tgt}" "${IRD_R5_EXEMPT_TARGETS}" _ex)
        if(NOT _ex)
            ird_hit("R5" "目标 ${_tgt} 直链 sdurw_proximity（policy.md §3.4 R-5：碰撞唯一实现归 policy；例外须 DTB §4.5 登记）")
        endif()
    endif()

    # ---- 2d. 框架库登记约束（DTB §4.6：sdurw_loaders 原则不使用，启用须登记）----
    set(_lib_hit_reg FALSE)
    foreach(_req ${IRD_FRAMEWORK_REGISTER_REQUIRED})
        if(_lib MATCHES "^${_req}($|::)")
            ird_in_list("${_lib}" "${IRD_FRAMEWORK_REGISTER_ALLOWED}" _allowed)
            if(NOT _allowed)
                ird_hit("LIB" "目标 ${_tgt} 使用须登记框架库 ${_lib}（DTB §4.6；启用须经例外登记）")
            endif()
        endif()
    endforeach()

    # ---- 2e. 未知目标族：非 ird、非框架基线、非 gtest、非 Qt —— 需登记后放行 ----
    set(_known_family FALSE)
    if(_lib MATCHES "^sdurws_ird_" OR _lib MATCHES "^sdurw" OR _lib MATCHES "^sdurwsim"
       OR _lib MATCHES "^GTest::" OR _lib MATCHES "^(gtest|gmock)$" OR _lib MATCHES "^Threads::")
        set(_known_family TRUE)
    endif()
    if(NOT _known_family)
        # Qt 链接已由 R-3/形态豁免逻辑处置（plugin 允许），此处不再重复命中
        if(NOT _lib MATCHES "^(Qt[0-9]?::|Qt[0-9]?$|Qt[0-9]?[A-Za-z]+$)")
            ird_hit("LIB" "目标 ${_tgt} 链接未知目标族 ${_lib}（不在 ird/框架基线/gtest 词表；如属第三方新增依赖，先登记 DTB 再扩词表）")
        endif()
    endif()
endforeach()

# ---------------------------------------------------------------------
# 第 3 步：include 路径检查（R-2 之配置面）
# 语义：任何目标的 include 路径不得指向【其他单元】的私有面（src/、test/）。
# 归属单元按声明所在 CMakeLists 的目录判定；生成器表达式剥壳后静态可判的
# 才检查（变量路径无法展开——与第 1 步同一诚实边界，随单元落地显式化消除）。
# ---------------------------------------------------------------------
foreach(_entry ${IRD_INCDIRS})
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _tgt)
    list(GET _parts 1 _dir_raw)
    list(GET _parts 2 _cl_dir)

    # 生成器表达式剥壳：$<BUILD_INTERFACE:x> / $<INSTALL_INTERFACE:x> → x
    set(_dir "${_dir_raw}")
    string(REGEX REPLACE "\\$<BUILD_INTERFACE:([^>]*)>" "\\1" _dir "${_dir}")
    string(REGEX REPLACE "\\$<INSTALL_INTERFACE:([^>]*)>" "\\1" _dir "${_dir}")

    # 相对化：把声明处的 CMAKE_CURRENT_SOURCE_DIR 替换为实际目录再解析
    string(REPLACE "\${CMAKE_CURRENT_SOURCE_DIR}" "${_cl_dir}" _dir "${_dir}")
    get_filename_component(_dir_abs "${_dir}" ABSOLUTE)

    # 该目标归属单元＝声明文件所在单元目录
    string(REPLACE "${IRD_ROOT}/" "" _rel_cl "${_cl_dir}")
    string(REGEX REPLACE "^([a-z]+)/.*" "\\1" _owner_unit "${_rel_cl}")

    # 是否落在某单元的私有面（src/ 或 test/ 子目录）
    string(REPLACE "${IRD_ROOT}/" "" _rel_dir "${_dir_abs}")
    if(_rel_dir MATCHES "^([a-z]+)/(src|test)/")
        set(_path_unit "${CMAKE_MATCH_1}")
        if(NOT _path_unit STREQUAL _owner_unit)
            ird_hit("R2" "目标 ${_tgt} 的 include 路径指向他单元私有面：${_rel_dir}（ARC-02：跨单元只经 include/ 公共头）")
        endif()
    endif()
endforeach()

# ---------------------------------------------------------------------
# 第 4 步：源码面扫描（R-2 头可达性／R-3 零 Qt／R-4 前缀／R-5 proximity 包含）
# 扫描域＝各单元【产品面】：include/** 与 src/**；test/** 为测试面不在此列
# （T-1 允许测试目标消费 testkit——文件域与目标域语义一致，testkit.md §3.4）。
# ---------------------------------------------------------------------
set(IRD_PRODUCT_FACE_FILES "")
foreach(_u ${IRD_KNOWN_UNITS})
    set(_u_root "${IRD_ROOT}/${_u}")
    if(IS_DIRECTORY "${_u_root}")
        file(GLOB_RECURSE _inc_files "${_u_root}/include/*.hpp" "${_u_root}/include/*.h"
                                    "${_u_root}/include/*.cpp" "${_u_root}/include/*.ipp"
                                    "${_u_root}/src/*.hpp" "${_u_root}/src/*.h"
                                    "${_u_root}/src/*.cpp" "${_u_root}/src/*.ipp")
        foreach(_f ${_inc_files})
            list(APPEND IRD_PRODUCT_FACE_FILES "${_u}|${_f}")
        endforeach()
    endif()
endforeach()

foreach(_entry ${IRD_PRODUCT_FACE_FILES})
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _unit)
    list(GET _parts 1 _file)
    file(READ "${_file}" _src)

    # ---- 4a. R-2 头可达性：包含他单元命名空间头时，该头必须存在于对方 include/ ----
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"]sdurws/ird/([a-z]+)/([A-Za-z0-9_/.]+)[>\"]" _inc_all "${_src}")
    foreach(_m ${_inc_all})
        string(REGEX REPLACE "#[ \t]*include[ \t]*[<\"]sdurws/ird/([a-z]+)/([A-Za-z0-9_/.]+)[>\"]" "\\1;\\2" _cap "${_m}")
        list(GET _cap 0 _inc_unit)
        list(GET _cap 1 _inc_hdr)
        if(NOT _inc_unit STREQUAL _unit)
            set(_pub "${IRD_ROOT}/${_inc_unit}/include/sdurws/ird/${_inc_unit}/${_inc_hdr}")
            if(NOT EXISTS "${_pub}")
                ird_hit("R2" "${_unit} 产品面文件包含他单元非公共头 sdurws/ird/${_inc_unit}/${_inc_hdr}（公共头未在对方 include/ 下命中）")
            endif()
        endif()
    endforeach()

    # ---- 4b. R-3 头包含：L2 单元（bare/worker 目标所属单元）产品面零 Qt ----
    # 该单元是否存在 bare/worker 目标——存在才做 R-3 文件扫描
    set(_has_l2 FALSE)
    foreach(_t ${IRD_TARGETS})
        ird_classify_target("${_t}" _tu _tf)
        if(_tu STREQUAL _unit AND _tf MATCHES "^(product|worker)$")
            set(_has_l2 TRUE)
        endif()
    endforeach()
    if(_has_l2)
        # 两个模式：Qt 模块路径（<QtWidgets/…>、<QtCore/…>）与 Qt 类头（<QWidget> 等
        # Q+大写开头头文件——Qt 类名头约定）；rw/std 头不含该形态
        if(_src MATCHES "#[ \t]*include[ \t]*[<\"][^>\"]*Qt")
            ird_hit("R3" "${_unit} 产品面包含 Qt 头：${_file}（NFR-MNT-01：L2 计算内核零 Qt）")
        elseif(_src MATCHES "#[ \t]*include[ \t]*<[A-Za-z_]*Q[A-Z][A-Za-z0-9_]*>")
            ird_hit("R3" "${_unit} 产品面疑似包含 Qt 类头（Q+大写约定）：${_file}")
        endif()
    endif()

    # ---- 4c. R-4 前缀拼接/剥离静态扫描（例外＝登记册，当前空册——O-12 未裁决）----
    set(_r4_skip FALSE)
    ird_in_list("${_unit}" "${IRD_R4_EXCEPTION_UNITS}" _u_ex)
    if(_u_ex)
        set(_r4_skip TRUE)
    endif()
    if(NOT _r4_skip)
        file(RELATIVE_PATH _rel_file "${IRD_ROOT}" "${_file}")
        ird_in_list("${_rel_file}" "${IRD_R4_EXCEPTION_FILES}" _f_ex)
        if(_f_ex)
            set(_r4_skip TRUE)
        endif()
    endif()
    if(NOT _r4_skip)
        # 启发式：源码字符串字面量中出现 "RobWork" 即可能在前缀拼接/剥离
        # （ARC-04：名称语义归 runtime；NFR-MNT-07 静态扫描零命中）。
        # runtime 单元整域例外走 IRD_R4_EXCEPTION_UNITS（登记后生效）。
        if(NOT _unit STREQUAL "runtime")
            # 注释剥离（F-011 消账，2026-09-11）：R-4 判定对象是名称拼接/剥离的
            # 【代码行为】，注释中的 RobWork 字样属文档而非行为——扫描前剥离
            # 行注释与块注释，杜绝文档性误报。已知边界：字符串字面量内的
            # "http://" 会被行注释规则截断（当前仓库无此形态，随用例扩充复核）。
            string(REGEX REPLACE "//[^\n]*" "" _r4_src "${_src}")
            string(REGEX REPLACE "/\\*([^*]|\\*[^/])*\\*/" " " _r4_src "${_r4_src}")
            string(REGEX MATCHALL "\"[^\"]*RobWork[^\"]*\"" _rw_all "${_r4_src}")
            if(_rw_all)
                list(LENGTH _rw_all _n)
                ird_hit("R4" "${_unit} 产品面发现 RobWork 字面量 ${_n} 处（疑前缀拼接/剥离）：${_file}（例外须 DTB §4.5 登记）")
            endif()
        endif()
    endif()

    # ---- 4d. R-5 头包含：rw/proximity 只许 policy（与链接面同一豁免表）----
    set(_r5_skip FALSE)
    ird_in_list("sdurws_ird_${_unit}" "${IRD_R5_EXEMPT_TARGETS}" _r5_unit_ex)
    if(_r5_unit_ex)
        set(_r5_skip TRUE)
    endif()
    if(NOT _r5_skip)
        if(_src MATCHES "#[ \t]*include[ \t]*[<\"]rw/proximity")
            ird_hit("R5" "${_unit} 产品面包含 rw/proximity 头：${_file}（policy.md §3.4 R-5；例外须 DTB §4.5 登记）")
        endif()
    endif()
endforeach()

# ---------------------------------------------------------------------
# 第 5 步：SA-02 框架零修改工作区核对（ARCH §5.2）
# 范围（诚实边界）：①patches/PATCHES.md 登记册完好（每条 patch 文件存在）；
# ②框架源码路径工作区零未提交改动（提交级核对＝已评审 patch 的提交形态，
# 其自动化随 WP-01-T02 CI 承接）。industrialrobot/ 本身不在框架核对范围。
# ---------------------------------------------------------------------
if(NOT DEFINED IRD_ENABLE_GIT)
    set(IRD_ENABLE_GIT ON)
endif()
if(IRD_ENABLE_GIT)
    execute_process(
        COMMAND git -C "${IRD_ROOT}" rev-parse --show-toplevel
        OUTPUT_VARIABLE _repo_top RESULT_VARIABLE _git_rc
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT _git_rc EQUAL 0)
        ird_hit("SA02" "无法定位 git 仓库根（ird_gates 须在仓库工作树内运行）")
    else()
        # 5a. 补丁登记册完好性
        set(_patches_md "${IRD_ROOT}/patches/PATCHES.md")
        if(NOT EXISTS "${_patches_md}")
            ird_hit("SA02" "补丁登记册缺失：patches/PATCHES.md（ARCH §5.2 要求集中登记）")
        else()
            file(READ "${_patches_md}" _pm)
            string(REGEX MATCHALL "[A-Za-z0-9_./-]+\\.patch" _plist "${_pm}")
            foreach(_p ${_plist})
                get_filename_component(_pname "${_p}" NAME)
                if(NOT EXISTS "${IRD_ROOT}/patches/${_pname}")
                    ird_hit("SA02" "登记册引用的补丁文件不存在：${_pname}")
                endif()
            endforeach()
        endif()

        # 5b. 框架路径工作区零未登记改动（porcelain 精确到框架目录并排除 industrialrobot）
        set(_fw_paths
            "RobWork/src"
            "RobWork/RobWorkStudio/src"
            "RobWorkSim/src")
        set(_fw_args "")
        foreach(_p ${_fw_paths})
            if(EXISTS "${_repo_top}/${_p}")
                list(APPEND _fw_args "${_p}")
            endif()
        endforeach()
        if(_fw_args)
            execute_process(
                COMMAND git -C "${_repo_top}" status --porcelain
                        -- ${_fw_args}
                        ":(exclude)RobWork/RobWorkStudio/src/rwslibs/industrialrobot"
                OUTPUT_VARIABLE _dirty RESULT_VARIABLE _rc2
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT _rc2 EQUAL 0)
                ird_hit("SA02" "git status 执行失败（框架工作区核对不可行）")
            elseif(NOT "${_dirty}" STREQUAL "")
                ird_hit("SA02" "框架源码存在登记外工作区改动：\n${_dirty}")
            endif()
        endif()
    endif()
endif()

# ---------------------------------------------------------------------
# 第 6 步：白名单 ↔ dependency-graph.json 一致性核对（DTB §2.2 验收 3）
# ①图文件每条边必须 ⊆ 白名单；②白名单多出的边必须在 IRD_EXTRA_EDGE_REFS
# 登记出处（当前恰为 O-21 补登的 testkit->core）。
# ---------------------------------------------------------------------
if(NOT DEFINED IRD_GRAPH_FILE OR IRD_GRAPH_FILE STREQUAL "")
    if(IRD_ENABLE_GIT AND _repo_top)
        set(IRD_GRAPH_FILE "${_repo_top}/RobWork/doc/industrial-robot-design/traceability/dependency-graph.json")
    endif()
endif()
if(NOT "${IRD_GRAPH_FILE}" STREQUAL "")
    if(NOT EXISTS "${IRD_GRAPH_FILE}")
        ird_hit("GRAPH" "依赖图数据文件缺失：${IRD_GRAPH_FILE}")
    else()
        file(READ "${IRD_GRAPH_FILE}" _graph)
        set(_graph_edges "")
        string(REGEX MATCHALL "\"from\":\"([a-z]+)\",\"to\":\"([a-z]+)\"" _gm_all "${_graph}")
        foreach(_m ${_gm_all})
            string(REGEX REPLACE "\"from\":\"([a-z]+)\",\"to\":\"([a-z]+)\"" "\\1->\\2" _e "${_m}")
            list(APPEND _graph_edges "${_e}")
            list(FIND IRD_ALLOWED_UNIT_EDGES "${_e}" _idx)
            if(_idx EQUAL -1)
                ird_hit("GRAPH" "依赖图边 ${_e} 不在门禁白名单（数据源不一致：${IRD_GRAPH_FILE}）")
            endif()
        endforeach()
        # 反向：白名单多出的边必须有登记出处（成对清单，数量必须相等）
        foreach(_we ${IRD_ALLOWED_UNIT_EDGES})
            list(FIND _graph_edges "${_we}" _idx)
            if(_idx EQUAL -1)
                list(FIND IRD_EXTRA_EDGE_REFS_EDGES "${_we}" _jdx)
                if(_jdx EQUAL -1)
                    ird_hit("GRAPH" "白名单边 ${_we} 不在依赖图且未登记出处（IRD_EXTRA_EDGE_REFS）")
                endif()
            endif()
        endforeach()
        list(LENGTH IRD_EXTRA_EDGE_REFS_EDGES _n_e)
        list(LENGTH IRD_EXTRA_EDGE_REFS_NOTES _n_n)
        if(NOT _n_e EQUAL _n_n)
            ird_hit("GRAPH" "IRD_EXTRA_EDGE_REFS 边/出处数量不配对（${_n_e} vs ${_n_n}）")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------
# 第 7 步：引擎自测（DTB §2.2 验收 5：未登记例外被门禁检出即失败，含用例）
# 子运行以 IRD_GATE_CHILD=ON 调用本脚本扫描夹具树，断言：
#   干净夹具 → 通过；植入违例夹具 → 失败且输出对应命中码。
# 夹具树由本脚本按 case 动态生成（写入 IRD_SELFTEST_DIR，不污染源码树）。
# ---------------------------------------------------------------------
if(NOT DEFINED IRD_GATE_CHILD)
    set(IRD_GATE_CHILD OFF)
endif()
if(NOT IRD_GATE_CHILD)
    if(NOT DEFINED IRD_SELFTEST_DIR OR IRD_SELFTEST_DIR STREQUAL "")
        if(DEFINED ENV{TEMP} AND NOT "$ENV{TEMP}" STREQUAL "")
            set(IRD_SELFTEST_DIR "$ENV{TEMP}/ird_gates_selftest")
        else()
            set(IRD_SELFTEST_DIR "/tmp/ird_gates_selftest")
        endif()
    endif()
    file(REMOVE_RECURSE "${IRD_SELFTEST_DIR}")
    file(MAKE_DIRECTORY "${IRD_SELFTEST_DIR}")

    # 用例骨架生成器说明：每例只写 CMakeLists.txt（＋按需源文件），门禁是
    # 纯静态扫描，不需要真实可编译工程——目标"存在性"由解析器从声明提取。

    set(_out_dummy "")    # —— 用例实现（每例独立子目录，互不干扰）——
    # CASE-PASS：干净骨架（复刻现状结构：core 链框架库、testkit 链 core、
    # core_test 走允许形态、业务域 INTERFACE 无边）→ 必须通过
    set(_dir "${IRD_SELFTEST_DIR}/pass_clean")
    file(MAKE_DIRECTORY "${_dir}/core/src")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core STATIC src/Core.cpp)
target_include_directories(sdurws_ird_core PUBLIC include)
target_link_libraries(sdurws_ird_core PUBLIC sdurw_math)
add_library(sdurws_ird_testkit INTERFACE)
target_link_libraries(sdurws_ird_testkit INTERFACE sdurws_ird_core)
add_executable(sdurws_ird_core_test test_main.cpp)
target_link_libraries(sdurws_ird_core_test PRIVATE sdurws_ird_core GTest::gtest_main)
add_library(sdurws_ird_kinematics INTERFACE)
target_include_directories(sdurws_ird_kinematics INTERFACE include)
")
    file(WRITE "${_dir}/core/src/Core.cpp" "namespace sdurws { namespace ird { namespace core { } } }\n")

    # CASE-R1：业务域互链（kinematics→modeling）→ 必须命中 IRD-GATE-R1
    set(_dir "${IRD_SELFTEST_DIR}/fail_r1")
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core INTERFACE)
add_library(sdurws_ird_kinematics INTERFACE)
target_link_libraries(sdurws_ird_kinematics INTERFACE sdurws_ird_core)
add_library(sdurws_ird_modeling INTERFACE)
target_link_libraries(sdurws_ird_kinematics INTERFACE sdurws_ird_modeling)
")

    # CASE-T1：产品目标链接 testkit → 必须命中 IRD-GATE-T1
    set(_dir "${IRD_SELFTEST_DIR}/fail_t1")
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core INTERFACE)
add_library(sdurws_ird_testkit INTERFACE)
target_link_libraries(sdurws_ird_testkit INTERFACE sdurws_ird_core)
add_library(sdurws_ird_io STATIC src/Io.cpp)
target_link_libraries(sdurws_ird_io PUBLIC sdurws_ird_testkit)
")
    file(MAKE_DIRECTORY "${_dir}/io_src_unused")

    # CASE-R5：evidence 直链 sdurw_proximity → 必须命中 IRD-GATE-R5
    set(_dir "${IRD_SELFTEST_DIR}/fail_r5")
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core INTERFACE)
add_library(sdurws_ird_evidence STATIC src/Evidence.cpp)
target_link_libraries(sdurws_ird_evidence PUBLIC sdurws_ird_core sdurw_proximity)
")

    # CASE-SUB：反向表外边 core→ui → 必须命中 IRD-GATE-SUB
    set(_dir "${IRD_SELFTEST_DIR}/fail_sub")
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core INTERFACE)
add_library(sdurws_ird_ui INTERFACE)
target_link_libraries(sdurws_ird_core INTERFACE sdurws_ird_ui)
")

    # CASE-R3：L2 目标链接 Qt → 必须命中 IRD-GATE-R3
    set(_dir "${IRD_SELFTEST_DIR}/fail_r3")
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core STATIC src/Core.cpp)
target_link_libraries(sdurws_ird_core PUBLIC Qt5::Widgets)
")

    # CASE-R4：产品面源码出现 RobWork 字面量（未登记例外）→ 必须命中 IRD-GATE-R4
    set(_dir "${IRD_SELFTEST_DIR}/fail_r4")
    file(MAKE_DIRECTORY "${_dir}/core/src")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core STATIC src/Core.cpp)
target_include_directories(sdurws_ird_core PUBLIC include)
")
    file(WRITE "${_dir}/core/src/Core.cpp"
"#include <string>\nstatic const std::string kPrefix = std::string(\"RobWork\") + \"_Device\";\n")

    # CASE-R4-PASS：RobWork 仅出现在注释中 → 必须通过（F-011 回归用例：
    # R-4 判定代码行为，注释属文档；2026-09-11 注释剥离修复的防退化锚点）
    set(_dir "${IRD_SELFTEST_DIR}/pass_r4comment")
    file(MAKE_DIRECTORY "${_dir}/core/src")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core STATIC src/Core.cpp)
target_include_directories(sdurws_ird_core PUBLIC include)
")
    file(WRITE "${_dir}/core/src/Core.cpp"
"// 本单元与 RobWork 框架的协作经 sdurw_math 公共头（注释提及框架名不属拼接行为）\nstatic const int kAnchor = 1;\n")

    # CASE-T2：testkit 链接他单元 → 必须命中 IRD-GATE-T2
    set(_dir "${IRD_SELFTEST_DIR}/fail_t2")
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${_dir}/CMakeLists.txt"
"add_library(sdurws_ird_core INTERFACE)
add_library(sdurws_ird_evidence INTERFACE)
add_library(sdurws_ird_testkit INTERFACE)
target_link_libraries(sdurws_ird_testkit INTERFACE sdurws_ird_core sdurws_ird_evidence)
")

    # 逐例运行子门禁并断言
    set(_selftest_cases
        "pass_clean|0|"
        "pass_r4comment|0|"
        "fail_r1|1|IRD-GATE-R1"
        "fail_t1|1|IRD-GATE-T1"
        "fail_r5|1|IRD-GATE-R5"
        "fail_sub|1|IRD-GATE-SUB"
        "fail_r3|1|IRD-GATE-R3"
        "fail_r4|1|IRD-GATE-R4"
        "fail_t2|1|IRD-GATE-T2")
    foreach(_case ${_selftest_cases})
        string(REPLACE "|" ";" _c_parts "${_case}")
        list(GET _c_parts 0 _c_id)
        list(GET _c_parts 1 _c_expect_fail)
        list(GET _c_parts 2 _c_code)
        if(DEFINED IRD_SELFTEST_CASE AND NOT IRD_SELFTEST_CASE STREQUAL ""
           AND NOT IRD_SELFTEST_CASE STREQUAL _c_id)
            continue()
        endif()
        set(_c_dir "${IRD_SELFTEST_DIR}/${_c_id}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                    "-DIRD_ROOT=${_c_dir}"
                    -DIRD_GATE_CHILD=ON
                    -DIRD_ENABLE_GIT=OFF
                    -DIRD_GRAPH_FILE=
                    -P "${CMAKE_SCRIPT_MODE_FILE}"
            OUTPUT_VARIABLE _c_out ERROR_VARIABLE _c_err
            RESULT_VARIABLE _c_rc)
        set(_c_all_out "${_c_out}${_c_err}")
        if(_c_expect_fail STREQUAL "1")
            if(_c_rc EQUAL 0)
                ird_hit("SELF" "自测 ${_c_id} 期望失败实际通过（门禁检出能力缺失）")
            elseif(NOT _c_all_out MATCHES "${_c_code}")
                ird_hit("SELF" "自测 ${_c_id} 失败但未输出期望命中码 ${_c_code}")
            else()
                message("[ird_gates] 自测 ${_c_id}：按预期检出 ${_c_code}")
            endif()
        else()
            if(NOT _c_rc EQUAL 0)
                ird_hit("SELF" "自测 ${_c_id} 期望通过实际失败（误报）：\n${_c_all_out}")
            else()
                message("[ird_gates] 自测 ${_c_id}：干净夹具通过（无误报）")
            endif()
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------
# 第 8 步：汇总裁决——任一命中即 FATAL_ERROR（构建目标失败）
# ---------------------------------------------------------------------
list(LENGTH IRD_HITS _hit_count)
message("==============================================================")
message("[ird_gates] 扫描根：${IRD_ROOT}")
if(_hit_count GREATER 0)
    message("[ird_gates] 命中 ${_hit_count} 项：")
    foreach(_h ${IRD_HITS})
        message("[ird_gates] ${_h}")
    endforeach()
    message("==============================================================")
    message(FATAL_ERROR "[ird_gates] 依赖红线/补丁门禁存在 ${_hit_count} 处命中——按 ARCH §3.2 SA-10 判构建失败；例外只经 DTB §4.5 登记册")
else()
    message("[ird_gates] 全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中")
    message("==============================================================")
endif()
