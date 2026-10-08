# =====================================================================
# trajectory 单元（业务域：轨迹规划——PTP/笛卡尔直线/避障规划、简化平滑、
# 时间参数化、诊断定位）构建脚本
#
# 设计依据：
#   - units/trajectory.md §3.2（依赖形态与红线自查——计算库 L2 零 Qt；
#     接口依赖 core/evidence/runtime/policy/execution/project/diagnostics/
#     io/testkit(仅测试侧) 公共头；R-1 业务域互链禁止——requirements/
#     kinematics 等 L4 同层单元零编译期依赖，跨域计算经"运行时注入端口
#     ＋③评估器端口语义"；R-5 零 sdurw_proximity——碰撞唯一经 policy；
#     L1 基线链接集 sdurw_math/kinematics/models/pathplanners）、§3.3
#     （端口协作总图——注入端口形态）、§4.2（目标布局 CMake 行——
#     WP-16-T03：INTERFACE→STATIC＋L1 基线＋_test/_contract_test 注册
#     ＋配置期红线守卫；_plugin/plugin/ 目录——T03 即落位最小可注册）、
#     §4.3（不创建 trajectory 专属 worker——明文承诺）、§14.4（TRJ-*
#     12 码登记表——装配期注册）、§16.1/§16.2（插件零计算逻辑/零修订
#     红线——界面面随 WP-16-T12）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 接入定稿）、§4.6（L1 基
#     线表——sdurw_pathplanners 登记给 trajectory〔WP-16-T06 消费；
#     本任务仅链接不消费——P-RPT-9 先例〕）
#   - ARCHITECTURE.md §3.3（业务域单元二分结构——零 Qt 计算库＋Qt Widgets
#     插件）、§3.5（依赖表——各业务域单元→L2/L3 公共接口的许可方向；
#     七边为其实例化，登记于 cmake/ird_gates_whitelist.cmake 的
#     "trajectory->…" 七行）
#   - 需求 NFR-MNT-01（计算内核零 Qt——trajectory 为 L2 等效业务计算库）
#   - 任务契约 tasks/foundation/WP-16-T03.json（本文件即其 acceptance
#     1/2/3 的落地面：INTERFACE 占位→STATIC＋_plugin/_test/_contract_
#     test 同批创建＋TRJ-* 域诊断码装配期注册＋二分结构扫描）
#
# 本文件随 WP-16-T03 落地（trajectory 由上级 INTERFACE 占位升级为真实
# 库）：
#   1. sdurws_ird_trajectory STATIC（目标名与别名 RWS::ird::trajectory
#      不变，DTB §5.1）；T03 源码集＝src/DiagCodes.cpp——TRJ-* 12 码
#      描述符清单＋装配注册函数（§14.4 登记表物化；本单元依赖白名单含
#      diagnostics 编译边，kinematics WP-15-T02 注册函数形态适用——
#      dynamics WP-17-T02 同因同款，与 selection/drivetrain 的"纯登记
#      表、注册归 L5 装配"形态不同，差异来自各自卡 §3.2 边表有无
#      diagnostics 编译边，诚实登记非形态漂移）。T04+ 源码集（TrjTypes/
#      Errors/PlanConfig/Sequence/Ptp/CartesianLine/Continuity/Planner/
#      Smooth/Recheck/TimeParam/Quality/Evidence/Evaluators/
#      KinematicsPort/Commands/Export——卡 §4.2 布局表）随各自任务增列，
#      本任务不预建占位（NFR-MNT-04）；
#   2. 插件界面目标 sdurws_ird_trajectory_plugin（T03 即落位——契约
#      acceptance 1 明文创建）：最小可注册实现＝assembly/ 自持描述符
#      门面（pluginId/titleKey 两字段均有 ui.md §11.1/§3.5 登记出处），
#      零业务计算逻辑（卡 §16.1"UI 线程只做投影、插值查表、命令提交"
#      ——契约测试词表扫描钉住）。界面面（轨迹工作流页/曲线视图入口/
#      动画衔接与 §14.5.1 命令族注册）随 WP-16-T12；
#   3. 测试目标 sdurws_ird_trajectory_test 与
#      sdurws_ird_trajectory_contract_test（自持 ird_add_gtest 宏：
#      add_test＋<target>_report XML——core/io/kinematics/selection/
#      dynamics 同款；ctest LABELS "ird"）；
#   4. 配置期依赖红线守卫（R-1/R-3 的 trajectory 侧断言——卡 §3.2；全
#      仓正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随本任务首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::
# gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
# 独立冒烟模式必须带 vcpkg toolchain 参数方可解析（CORE-T01 G-2 口径）。
find_package(GTest CONFIG REQUIRED)

# ---- Qt6 接入（WP-16-T03——插件目标的 Qt 载体面，R-3 唯一例外面）----
# 仅插件目标消费 Qt（卡 §3.2 二分结构"零 Qt 计算库＋Qt Widgets 插件"；
# dynamics/selection/workflow 落位批同款先例）。组件＝Core/Gui/Widgets
# （Widgets 是 WP-16-T12 轨迹工作流页/曲线视图的本体、Gui/Core 为 Qt 基
# 面；不取 Test——本单元无 GUI 测试目标，GUI 手动验证通道随 WP-16-T12
# ——卡 §17.4 明文本批不启动 GUI）。REQUIRED 同 gtest：找不到即配置
# 失败（不静默跳过插件目标）。独立冒烟模式必须带 CMAKE_PREFIX_PATH 指向
# Qt 安装（编排口径：Qt6 位于 D:/software/Qt/6.11.1/msvc2022_64）。计算
# 库 sdurws_ird_trajectory 保持零 Qt——R-3/NFR-MNT-01：产品面（include/
# ＋src/）源码零 Qt 包含、链接面零 Qt 目标（BuildRedLineTest 与配置期
# 守卫双面钉住）。
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T03 源码集＝src/DiagCodes.cpp（TRJ-* 12 码登记表——CodeDescriptor 描
# 述符清单＋装配注册函数的唯一实现翻译单元；逐码落值依据见该文件逐码
# 注释）。T04 批（WP-16-T04）按卡 §4.2 布局表增列四个实现翻译单元：
#   src/TrjTypes.cpp   §6 数据模型 T04 面＋段结构守卫（validateTrajectory-
#                      Segment——§6.2 不变量执行点；只读校验零 Q 对象
#                      构造——冒烟模式可编译）
#   src/PlanConfig.cpp §5.5 求解配置五件套（validate/encode/decode/digest/
#                      ConfigEntry 组装——canonical magic IRDCFGTR1；纯
#                      std＋core 头——冒烟模式可编译）
#   src/Ptp.cpp        §7 PTP 规划（插值/构型选择三键/planPtpSegment——
#                      TRJ-01 点到点路径）
#   src/Sequence.cpp   §5.4 任务序列展开（I-REQ-7 校验/Kahn 拓扑/段链——
#                      TRJ-01 有序作业序列）
# 其余翻译单元（CartesianLine/Continuity/Planner/…）随 WP-16-T05~T10
# 增列（本批不预建占位实现，NFR-MNT-04 不建无边界价值包装器）。
#
# Ptp.cpp/Sequence.cpp 为集成模式条件源（kinematics src/Collision.cpp
# 同款 gating 先例——WP-15-T07）：两 TU 构造/析构 rw::math::Q 对象，而
# Q 的虚析构符号（rw::math::Q::~Q）定义在框架库 sdurw_math 内（非
# header-only）——冒烟模式无框架库可链，链接面必然未定义符号；冒烟
# 口径＝"目标注册＋include 路径"（DTB §5.1），Q 构造型算法面的真实验证
# 归集成模式（唯一交付口径）。注意与 TrjTypes.cpp 的差异：后者只做
# const 引用遍历校验，不实例化任何 Q 构造/析构点，两模式均可编译链接。
add_library(sdurws_ird_trajectory STATIC
    src/DiagCodes.cpp
    src/TrjTypes.cpp
    src/PlanConfig.cpp)

if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_trajectory PRIVATE
        src/Ptp.cpp           # §7 PTP 规划（Q 构造面——集成条件，见上）
        src/Sequence.cpp      # §5.4 序列展开（Q 构造面——集成条件，见上）
        src/CartesianLine.cpp # §8 笛卡尔直线段＋IK ③端口逐采样连续性
                              #   （Q 构造面——WP-16-T05；集成条件，见上）
        src/Continuity.cpp    # §9 段边界几何连续＋C2 适用双路（Q 构造面
                              #   ——WP-16-T05；集成条件，见上）
        src/Planner.cpp)      # §10 规划器适配＋避障重规划编排（消费
                              #   sdurw_pathplanners 非模板符号 RRTPlanner/
                              #   QConstraint 与 policy 会话——WP-16-T06；
                              #   集成条件，见上；Planner.hpp 公共头对 rw/
                              #   policy/runtime 仅前向声明，两模式皆可编译）
endif()

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：trajectory 公共头使用 C++17 设施（inline constexpr 变量
# 等），消费者编译 trajectory 头也必须至少 C++17。
target_compile_features(sdurws_ird_trajectory PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_trajectory PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 trajectory
# 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_trajectory PUBLIC /utf-8)
endif()

# 公共头根：仅 trajectory/include（内部含 sdurws/ird/trajectory/）。T03
# 公共面＝DiagCodes.hpp（TRJ-* 12 码登记表——契约 acceptance 3）；卡
# §4.2 布局表十七头（TrjTypes/Errors/PlanConfig/Sequence/…）随 T04+
# 任务增列。R-2 红线：私有实现头不得置于 include/——本目录是
# trajectory 唯一对外的头暴露位，实现细节头（若有）只能放 src/ 并以
# 相对路径被实现文件包含。
target_include_directories(sdurws_ird_trajectory PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋evidence＋runtime＋policy＋execution＋diagnostics＋io
# （卡 §4.2 CMake 行边表七条登记边——ARCH §3.5"各业务域单元→L2/L3 公共
# 接口"许可方向的实例化，ird_gates_whitelist.cmake 的 "trajectory->…"
# 七行随本任务登记）。
# PUBLIC 形态（与卡面 "PRIVATE 链" 字样的差异——诚实登记，非形态漂移）：
# DiagCodes.hpp 内嵌 diagnostics 承载的 CodeDescriptor/IDiagnosticRegistry
# 契约类型（其头又内嵌 core::DiagCode——消费方必须随链到达），PRIVATE
# 链会使测试目标/下游单元编译 trajectory 头时 core/diagnostics 头不可
# 达，而测试目标直链他单元产品目标即 ird_gates SUB——已落位各单元同以
# PUBLIC 建边（P-RPT-9 同款先例：边随落位登记，消费随 T04+ 任务回填：
# runtime IRuntimeModelView/RuntimeSnapshot→T04 输入组装面、policy ④端
# 口 IPolicyProvider/CollisionEvaluationSession→T06/T07 碰撞与复检面、
# evidence IEngineeringEvaluator/EvaluationRequest→T10 评估器面、
# execution 任务提交/取消令牌/检查点→T04 批量通道、io CSV/原子写通道→
# T10 导出面；七边统一 PUBLIC 保持形态一致——契约 acceptance 1 依赖面
# 全建立）。
# project 边本任务不落（卡 §3.2 接口依赖清单提及 project 公共头，但
# §4.2 CMake 行边表未列＋T03 零 project 公共值类型引用——requirements/
# dynamics 卡落位批"runtime/project 边暂不登记"同款处理：实际引用时按
# 卡 §3.2 增登；project 只读查询经装配面组装、结果归档经执行通道——
# 卡 §14.3）。
# 用统一真名 sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名
# RWS::ird::* 仅集成模式存在——kinematics/dynamics/selection 落位时同款
# 取舍）。
# 零 Qt（R-3/NFR-MNT-01——_plugin 目标为唯一 Qt 例外载体）、零其他业务
# 域单元（R-1——requirements 工况经快照 objectClosure、kinematics IK/
# 位姿指标经注入端口＋③端口语义——卡 §3.2 消费表，零编译期依赖）、零
# testkit（T-1——testkit 仅随测试目标消费）、零 sdurw_proximity（R-5
# ——碰撞唯一经 policy，卡 §3.2）。
target_link_libraries(sdurws_ird_trajectory PUBLIC
    sdurws_ird_core
    sdurws_ird_evidence
    sdurws_ird_runtime
    sdurws_ird_policy
    sdurws_ird_execution
    sdurws_ird_diagnostics
    sdurws_ird_io)

# ---- L1 基线库链接（卡 §3.2 L1 基线集——规划算法面）----
# 集成/冒烟判定＝TARGET sdurw_kinematics（框架库目标只在集成配置树存在
# ——runtime P-RT-3 同款探测）。集成模式：sdurw_math/sdurw_kinematics/
# sdurw_models/sdurw_pathplanners 直接 TARGET 链接——DTB §4.6 L1 基线表
# 明文登记 trajectory 为 sdurw_pathplanners 消费方（规划算法面——
# P-TRJ-2 口径：DTB §4.6/runtime §8.1 登记归 trajectory，碰撞约束经
# policy 会话适配、规划器内部零 proximity 触碰〔D-TRJ-3〕；R-5 红线
# 不动——proximity 零直链）。T03 源码零框架符号消费（仅链接不消费——
# P-RPT-9 先例），sdurw_pathplanners 的真实消费随 WP-16-T06。冒烟模式：
# T03 源码集（DiagCodes.cpp）零 rw 消费，零框架链接（L1 清单空）——
# T04 引入 rw 头消费时照 runtime RT-T03 先例补冒烟侧 rw 源码树头路径。
if(TARGET sdurw_kinematics)
    target_link_libraries(sdurws_ird_trajectory PUBLIC
        sdurw_math
        sdurw_kinematics
        sdurw_models
        sdurw_pathplanners)
else()
    # ---- 冒烟分支：rw 源码树头路径（WP-16-T04 起 T04 源码消费 rw 数学
    # 模板头——rw::math::Q/Transform3D 均 header-only；runtime RT-T03 冒烟
    # header-only 机制同款：只加 include 路径＋Eigen 头，不链接框架库——
    # 冒烟口径＝"目标注册＋include 路径"，库目标在冒烟树不存在即无链接
    # 面）。约束（实现纪律，与 runtime 同款）：T04 产品代码只消费 rw 的
    # header-only 面（模板/内联）——不调用框架库 .cpp 导出符号（如
    # Rotation3D::identity() 的库内定义形态），否则冒烟链接未定义符号。
    set(IRT_RW_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../RobWork/src")
    if(NOT EXISTS "${IRT_RW_SRC_DIR}/rw/math/Q.hpp")
        message(FATAL_ERROR
            "trajectory 冒烟模式：未找到 rw 模板头（期望 ${IRT_RW_SRC_DIR}/"
            "rw/math/Q.hpp）——T04 起产品源码消费 rw::math 头（header-only），"
            "请勿移动仓库目录结构")
    endif()
    target_include_directories(sdurws_ird_trajectory PUBLIC "${IRT_RW_SRC_DIR}")
    find_package(Eigen3 CONFIG QUIET)
    if(NOT TARGET Eigen3::Eigen)
        message(FATAL_ERROR
            "trajectory 冒烟模式：未找到 Eigen3（vcpkg eigen3）——rw 数学头"
            "依赖 <Eigen/Core>（RT-T03 冒烟 header-only 机制同款）")
    endif()
    get_target_property(IRT_EIGEN_INC Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT IRT_EIGEN_INC)
        message(FATAL_ERROR
            "trajectory 冒烟模式：Eigen3::Eigen 无 INTERFACE_INCLUDE_DIRECTORIES"
            "——vcpkg eigen3 配置包异常（RT-T03 冒烟 header-only 机制同款）")
    endif()
    # Eigen 仅取 include 目录不入链接清单（header-only 语义＝"头可达"；
    # ird_gates LIB 门禁按链接清单核对，框架外目标入清单即命中）。
    target_include_directories(sdurws_ird_trajectory PUBLIC ${IRT_EIGEN_INC})

    # boost 头路径：rw/math/Q.hpp 无条件 include <boost/serialization/...>
    # （T04 关节向量 rw::math::Q——§6.4"不包装不重定义"——必然携带此头
    # 依赖）。boost 头随 vcpkg 经典模式安装（installed/x64-windows/include
    # ——gtest 同款安装根，DTB §5.5）；toolchain 已在独立冒烟命令行给定，
    # vcpkg 根由 toolchain 路径上溯三级推导（<vcpkg>/scripts/buildsystems/
    # vcpkg.cmake）。同样只取 include 目录不入链接清单——Q.hpp 的序列化
    # 模板在冒烟编译只实例化声明级内联，不引用 boost 编译库。
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR
            "trajectory 冒烟模式：必须带 vcpkg toolchain 参数（DTB §5.1/"
            "CORE-T01 G-2 口径）——rw/math/Q.hpp 依赖 boost 头")
    endif()
    get_filename_component(IRT_VCPKG_ROOT "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(IRT_VCPKG_ROOT "${IRT_VCPKG_ROOT}" DIRECTORY)
    get_filename_component(IRT_VCPKG_ROOT "${IRT_VCPKG_ROOT}" DIRECTORY)
    set(IRT_BOOST_INC "${IRT_VCPKG_ROOT}/installed/x64-windows/include")
    if(NOT EXISTS "${IRT_BOOST_INC}/boost/serialization/split_free.hpp")
        message(FATAL_ERROR
            "trajectory 冒烟模式：未找到 boost 头（期望 ${IRT_BOOST_INC}/"
            "boost/serialization/split_free.hpp）——rw/math/Q.hpp 序列化头"
            "依赖（vcpkg 经典模式经典布局）")
    endif()
    target_include_directories(sdurws_ird_trajectory PUBLIC "${IRT_BOOST_INC}")
endif()

# 别名（集成模式）：供其余单元以 RWS::ird::trajectory 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::trajectory ALIAS sdurws_ird_trajectory)
endif()

# ---- 插件界面目标 sdurws_ird_trajectory_plugin（WP-16-T03 落位——契约
#      acceptance 1 明文创建；卡 §16.1 目标形态）----
# 最小可注册实现（零业务计算逻辑——卡 §16.1/§16.2）：TrajectoryPlugin-
# Assembly＝自持描述符装配门面（assembly/ 公共头——pluginId/titleKey 均
# 有 ui.md §11.1/§3.5 登记出处；dynamics WP-17-T02 同款先例）。源码布局
# ＝plugin/ 目录（门禁 R-3 产品面扫描域为 include/**＋src/**，plugin/
# 不在扫描域——Qt 红线的文件域隔离，dynamics/selection 同款先例）；装配
# 契约头＝assembly/（plugin 目标的 PUBLIC include 面——宿主装配层只见
# 门面头，零插件私有头依赖——R-2；workflow O-45 教训前移，落位即建）。
# 本批次零 Q_OBJECT 类（界面控件随 WP-16-T12 落位时引入），故不开
# AUTOMOC；首个 Q_OBJECT 类落位任务按 modeling/requirements 先例置
# AUTOMOC ON（DTB §5.1 v0.17 行口径）。
add_library(sdurws_ird_trajectory_plugin STATIC
    plugin/TrajectoryPluginAssembly.cpp)  # 装配门面实现（assembly/
                                          #   公共头的私有实现）
set_target_properties(sdurws_ird_trajectory_plugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_trajectory_plugin PRIVATE /utf-8)
endif()

# 插件私有头解析（plugin/*.hpp——PRIVATE，不外溢；dynamics/selection 同
# 款先例；T03 仅单实现 TU，仍按先例登记以稳定后续批次形态）。
target_include_directories(sdurws_ird_trajectory_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
# 装配层（宿主插件）消费 <sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp>
# ——PUBLIC include 面（非 include/ 产品扫描域——零 Qt 红线文件域隔离同
# plugin/ 口径）。
target_include_directories(sdurws_ird_trajectory_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/assembly>)

# 链接面（卡 §16.1 二分结构——"Qt Widgets 插件"例外载体面；dynamics/
# selection 同款两行形态）：
#   - sdurws_ird_trajectory＝本计算库（门面/后续面板消费域设施的唯一来
#     源；自边：ird_gates 引擎按"插件目标链接本单元产品目标"装配自边
#     豁免放行〔ird_gates.cmake 2a 形态二 _self_edge 分支〕，与 dynamics/
#     selection/modeling/requirements/kinematics/workflow 六 plugin 同款
#     形态）；
#   - Qt 三件套＝plugin 分类面（ird_gates R-3 仅查 product/worker 形态；
#     T03 源码零 Qt 类消费，链接面先行建立——WP-16-T12 面板批次直接
#     增列源文件即可）；
#   - 零 sdurws_ird_ui（与 modeling/requirements/kinematics 插件的
#     "→本计算库＋sdurws_ird_ui"三边形态不同：selection/dynamics 落位
#     批已立"自持描述符、零 ui 编译边"先例——trajectory 插件界面随
#     WP-16-T12 以真实 ui 类型落位，届时若需 ui 编译边，先走单元卡增
#     量修订＋白名单增登）。
target_link_libraries(sdurws_ird_trajectory_plugin PUBLIC
    sdurws_ird_trajectory
    Qt6::Core Qt6::Gui Qt6::Widgets)

# 别名（集成模式）：供其余单元以 RWS::ird::trajectory_plugin 消费；冒烟
# 模式无别名（DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::trajectory_plugin ALIAS sdurws_ird_trajectory_plugin)
endif()

# ---- 测试目标 sdurws_ird_trajectory_test（随 WP-16-T03 登记）----
# 用例面（任务契约 acceptance 逐条具名自证）：
#   test/DiagCodesTest.cpp        TRJ-* 12 码登记表全表清单＋逐字段登记
#                                 值＋真实 StableCodeRegistry 注册行为＋
#                                 重复注册拒绝（acceptance 3——DiagCodes.
#                                 hpp 行；kinematics P-KIN-7 同款真实注
#                                 册表验证——TRJ 前缀已在册无前缀阻塞）
#   test/BuildRedLineTest.cpp     产品面源码红线运行期扫描（零 Qt＋七边
#                                 include 面＋R-4 字面量＋文件头注释——
#                                 acceptance 1/2，与文件末尾配置期守卫
#                                 互为两道防线；dynamics/selection 同款
#                                 四用例）
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15/selection/dynamics 同款先例；IRD_TEST_INFO 携带需求/AT 追溯
# 字段）。零 QCoreApplication 构造（dynamics T02 同款诚实登记）：_test
# 目标零 Qt 消费（不链 _plugin），测试面无 Qt 类型。
enable_testing()

add_executable(sdurws_ird_trajectory_test
    test/TestMainReport.cpp
    test/DiagCodesTest.cpp
    test/BuildRedLineTest.cpp
    test/PlanConfigTest.cpp)         # WP-16-T04：§5.5 求解配置（校验/canonical
                                     #   往返/确定性/digest 身份面）

# PtpSequenceTest 为集成模式条件源（同产品面 Ptp.cpp/Sequence.cpp 的 gating
# 口径——用例构造 rw::math::Q 对象，冒烟模式无框架库符号可链；V-01/02/03
# 解析算例的真实执行归集成模式测试运行）。
# CartesianLineContinuityTest 同为条件源（WP-16-T05——V-04~V-07/V-09 与
# C2 双路用例：构造 Transform3D/Q 对象＋端口假实现，真实执行归集成模式）。
# PlannerAvoidanceTest 同为条件源（WP-16-T06——acceptance 1/2 全真实链路
# 用例：真实 WorkCell/SerialDevice/RRTPlanner/QConstraint＋真实 policy 会
# 话〔内置 ProximityStrategyRW〕；消费 rw 非模板符号，集成条件同因）。
if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_trajectory_test PRIVATE
        test/PtpSequenceTest.cpp)    # WP-16-T04：TRJ-01（PTP 插值/构型选择/
                                     #   限位守卫/序列展开——V-01/02/03 面）
    target_sources(sdurws_ird_trajectory_test PRIVATE
        test/CartesianLineContinuityTest.cpp)
    target_sources(sdurws_ird_trajectory_test PRIVATE
        test/PlannerAvoidanceTest.cpp)  # WP-16-T06：TRJ-03（真实规划器基准/
                                        #   C8 重规划/搜索未果素材轨/取消/
                                        #   选型 fail-fast/④端口直穿）
endif()

target_compile_features(sdurws_ird_trajectory_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_trajectory_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_trajectory_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_trajectory 同时验证"公共头可被正常 include
# ＋库可链接"（七登记边 PUBLIC 传染链随目标自动到达——core/evidence/
# runtime/policy/execution/diagnostics/io 的链接语义经本链路在链接器层
# 面自证）。sdurws_ird_testkit＝报告/追溯设施消费面（RecordListener/
# IRD_TEST_INFO——T-1 允许形态：仅测试目标链 testkit，产品目标不链）。
# 单行链接语句＝契约测试 T-1 扫描的登记形态约束（testkit 与测试目标名
# 同行可见——kinematics/selection/dynamics 同款）。本目标零 Qt（红线扫
# 描用例面——不链 _plugin）。
target_link_libraries(sdurws_ird_trajectory_test PRIVATE sdurws_ird_trajectory sdurws_ird_testkit GTest::gtest)

# 源码树位置注入：红线扫描用例在运行期扫描 trajectory 源码核对红线
# （零 Qt/零表外 include——卡 §3.2/§4.2）。经编译定义传 industrialrobot
# 根（kinematics/io/selection/dynamics 同款——值内以单元名作子目录前
# 缀，扫描面随单元自包含）。
target_compile_definitions(sdurws_ird_trajectory_test PRIVATE
    IRD_TRAJECTORY_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_trajectory_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_trajectory_test>
            $<TARGET_FILE_DIR:sdurws_ird_trajectory_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io/kinematics/selection/
# dynamics 同款形态，不做跨单元共享——trajectory 是业务域单元，不提供
# 构建工具设施，NFR-MNT-04）。报告输出于构建树本目录 gtest_reports/
# <target>.xml；任务留痕时拷贝入 doc/industrial-robot-design/
# traceability/（AGENTS.md §4.2 验证留痕）。报告目标不进默认构建
# （EXCLUDE_FROM_ALL）：跑测试是显式动作，与框架"不随 ordinary build
# 执行测试"的约定一致。
macro(ird_add_gtest target)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    add_custom_target(
        ${target}_report
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_CURRENT_BINARY_DIR}/gtest_reports
        COMMAND $<TARGET_FILE:${target}>
                --gtest_output=xml:${CMAKE_CURRENT_BINARY_DIR}/gtest_reports/${target}.xml
        DEPENDS ${target}
        COMMENT "ird: run ${target} and write gtest XML report")
    set_target_properties(${target}_report PROPERTIES
        EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)
endmacro()

ird_add_gtest(sdurws_ird_trajectory_test)

# ---- 契约测试目标 sdurws_ird_trajectory_contract_test（随 WP-16-T03
#      登记——契约 acceptance 1 明文创建）----
# 落位期契约面＝构建图边界契约（dynamics/selection/drivetrain
# BuildGraphContractTest 同款扫描形态——七条登记边的存在性/封闭性/无
# 业务域互链/无表外平台边/testkit 仅测试目标/_worker 形态不存在/include
# 面同界；与 drivetrain 相反，_plugin 目标落位为正向断言）＋插件可注册
# 契约（TrajectoryPluginAssemblyContractTest——描述符登记出处自证＋卡
# §16.1 插件零计算红线词表扫描〔规划/碰撞/时间化/统计符号＋直接文件/
# 项目访问零命中〕——acceptance 1"最小可注册实现"的执行证明）；业务
# 对端面契约（runtime 视图/evidence 评估器注册/policy 会话/execution
# 任务通道 fake 套件）随 WP-16-T04+ 落地（T03 登记面不触对端契约面，
# 不预建占位用例）。
# 自有 main 同 _test 目标（报告设施接入）。
add_executable(sdurws_ird_trajectory_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/TrajectoryPluginAssemblyContractTest.cpp
    contract_test/PlanConfigContractTest.cpp
    contract_test/PathPlannerContractTest.cpp)  # WP-16-T06：R-5/R-POL-2 产品面
                                                #   零 proximity 静态扫描＋§15.5
                                                #   接口面钉扎（纯文本扫描——非
                                                #   条件源，两模式皆跑）

# KinematicsPortContractTest 为集成模式条件源（WP-16-T05——③端口消费契
# 约：R-1 源码扫描零 kinematics include／请求字段透传钉扎／确定性解序／
# 端口错误传播；用例构造 CartesianLineRequest（rw::math::Q/Transform3D
# 对象），冒烟模式无框架库符号可链——同 PtpSequenceTest gating 口径）。
if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_trajectory_contract_test PRIVATE
        contract_test/KinematicsPortContractTest.cpp)
endif()

target_compile_features(sdurws_ird_trajectory_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_trajectory_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_trajectory_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
# sdurws_ird_trajectory_plugin＝同单元插件目标（装配门面——可注册契约
# 用例的被测面；"测试目标仅允许同单元产品目标"纪律下 plugin face 亦属
# 同单元许可集——dynamics/selection/workflow 先例同款；其 PUBLIC 面携带
# Qt 传染，供后续批次注册端口符号解析预备）。
target_link_libraries(sdurws_ird_trajectory_contract_test PRIVATE sdurws_ird_trajectory sdurws_ird_trajectory_plugin sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（构建图/插件面扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_trajectory_contract_test PRIVATE
    IRD_TRAJECTORY_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库＋插件目标 Qt 传染面——同 _test
# 目标的 POST_BUILD 口径）。
add_custom_command(TARGET sdurws_ird_trajectory_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_trajectory_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_trajectory_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_trajectory_contract_test)

# ---- ctest LABELS "ird"（modeling/ui/requirements/kinematics/selection/
#      dynamics 落位同款）----
# 验收过滤口径：ctest -L "^ird$"——两测试（模型层、进程内、确定性）同
# 标签；_gui_test 形态本单元无（GUI 手动验证通道随 WP-16-T12——卡
# §17.4，本批次无 GUI 载体用例）。
set_tests_properties(sdurws_ird_trajectory_test sdurws_ird_trajectory_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，dynamics/selection/drivetrain
#      foreach 同款）----
# trajectory 的 industrialrobot 单元依赖仅限七条登记边：core＋evidence＋
# runtime＋policy＋execution＋diagnostics＋io（卡 §4.2 CMake 行边表——
# ARCH §3.5 业务域→L2/L3 公共接口许可方向的实例化，白名单
# "trajectory->…" 七行同源）；链接到任何其他 sdurws_ird_* 目标（业务域
# 单元＝R-1 命中面；表外平台单元＝SUB 命中面——含卡 §3.2 提及但 §4.2
# CMake 行未列的 project〔T03 零公共值类型引用不落边，实际引用时按卡
# §3.2 增登〕）即配置失败。零 Qt（R-3/NFR-MNT-01——Qt 例外载体仅为
# _plugin 目标，不在本遍历范围）。产品库本体零 gtest/testkit（D-06/T-1
# ——断言宏只在消费方测试目标展开；本守卫遍历的是产品目标
# sdurws_ird_trajectory 的链接面，测试目标的 gtest/testkit 边不在其内）。
# 两面读取（LINK_LIBRARIES 含 PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含
# PUBLIC 边）防漏。属性未设置时 get_target_property 返回
# "<var>-NOTFOUND" 字面串，不匹配任何模式，不会误报。testkit 不在第一
# 条模式中逐字列出（dynamics/selection 同款先例）：sdurws_ird_testkit
# 落在通配 "^sdurws_ird_" 的拦截范围（≠七条登记边即 FATAL），避免守卫
# 正则字样干扰契约测试的 T-1 链接语句扫描（BuildGraphContractTest 的
# NoTestkitEdge 用例以"逐行只认测试目标链接语句"钉住产品目标零
# testkit）。正式全仓门禁（依赖图白名单）归 ird_gates（WP-01-T01），
# 本守卫不替代它。
get_target_property(_ird_trajectory_link_libs sdurws_ird_trajectory LINK_LIBRARIES)
get_target_property(_ird_trajectory_iface_libs sdurws_ird_trajectory INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_trajectory_link_libs _ird_trajectory_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_evidence"
       AND NOT _lib STREQUAL "sdurws_ird_runtime"
       AND NOT _lib STREQUAL "sdurws_ird_policy"
       AND NOT _lib STREQUAL "sdurws_ird_execution"
       AND NOT _lib STREQUAL "sdurws_ird_diagnostics"
       AND NOT _lib STREQUAL "sdurws_ird_io")
        message(FATAL_ERROR
            "trajectory 依赖红线（R-1/卡 §4.2）：sdurws_ird_trajectory "
            "仅可链接七条登记边 core/evidence/runtime/policy/execution/"
            "diagnostics/io（units/trajectory.md §4.2、ARCH §3.5——"
            "project T03 零公共值引用不落边，含 testkit——T-1 产品目标"
            "禁链），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "trajectory 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §3.2 "
            "二分结构，Qt 例外载体仅 _plugin 目标）：发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "trajectory 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
