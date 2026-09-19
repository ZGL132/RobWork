/**
 * @file   Ui.cpp
 * @brief  ui 单元锚点翻译单元（落位期"空起步"形态，无任何接口预建）。
 *
 * 设计依据：
 *   - units/ui.md §13 UI-T02 行（构建落位：sdurws_ird_ui INTERFACE 占位升级
 *     STATIC，链 core＋diagnostics＋Qt——R-3 唯一例外登记目标）；
 *   - development-task-breakdown.md §5.1（INTERFACE→真实库升级时目标名不变）、
 *     §5.3（全局禁止项）；
 *   - 需求 NFR-MNT-01（全产品唯一允许 Qt Widgets 的平台单元——例外范围仅
 *     sdurws_ird_ui 与 ui.md §3.3 所列 ui 测试目标，登记于 DTB §4.5 红线例外
 *     登记册"预登记（WP-10-T02 生效）"行）。
 *
 * 背景说明（为什么存在一个"空"翻译单元）：CMake 对零源码的 STATIC 库在
 * 生成期即报 "No SOURCES given to target"（CORE-T01/PRJ-T01/EX-T01 等
 * 落位任务实测），锚点翻译单元是"库可生成、无接口预建"（NFR-MNT-04：
 * 不在骨架预建占位实现）的最小载体。ui 的真实源码（WorkbenchShell、五区
 * Dock、CommandRegistry 等）随 UI-T03~T13 按单元卡 §13 落地，届时本文件
 * 或承载共享工具实现或拆除（EX-T01 同款先例登记）。
 *
 * 落位期形态约束（本文件刻意为零 include 状态）：
 *   1. 零 Qt 头包含——ird_gates 对"拥有产品目标的单元"做产品面（include/、
 *      src/）Qt 头扫描（ird_gates.cmake 第 4b 步）；ui 的 R-3 例外以链接边
 *      形态在本任务落位（CMakeLists.txt 中 PUBLIC 链 Qt6::Core/Gui/Widgets），
 *      头包含面随 UI-T03 界面实现引入，届时门禁侧例外登记同步扩展（登记
 *      提交件见 traceability/wp10-t02-gate-registrations.md）。
 *   2. 零跨单元头包含——O-31 处置边界（任务契约 acceptance 5）：本任务
 *      交付面仅含 ARCH §3.5 表内边（ui→core、ui→diagnostics）与 R-3 例外
 *      登记，不创建任何指向 project/evidence/execution/policy/runtime 的
 *      链接或 include；该边界由 sdurws_ird_ui_test 的
 *      UiBuild.NoCrossUnitInclude_O31_UI_BUILD 用例常驻自证。
 *
 * 线程模型：本翻译单元当前无可执行语义（编译锚点），不涉及线程约束；
 * ui 单元的线程模型（UI 线程/Marshal 纪律 M-1）见 units/ui.md §3.4，
 * 随 UI-T03+ 落地时在各实现文件头注明归属线程。
 */
namespace sdurws {
namespace ird {
namespace ui {

// 锚点命名空间：仅声明单元命名空间存在（sdurws::ird::ui，units/ui.md §3.3），
// 不引入任何类型/函数——接口面随 UI-T03+ 的公共头（UiTypes.hpp 等十三个
// §3.3 组成行）落地。前置空声明是防止本 TU 被优化掉导致链接器视角下
// 目标为空的最小形态（各单元锚点先例同款写法）。

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
