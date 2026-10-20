/**
 * @file HostDraftConflict.hpp
 * @brief PM-04/08、AT-21/29：宿主草稿冲突三选项的私有同步呈现缝。
 * ui只编排既有控制器保存/重建，不解析域payload、不提交或自行合并修订。
 */
#ifndef IRD_UI_HOSTDRAFTCONFLICT_HPP
#define IRD_UI_HOSTDRAFTCONFLICT_HPP
#include <sdurws/ird/ui/IDraftController.hpp>
#include <functional>
class QWidget;
namespace sdurws::ird::ui {
/** @brief 一次冲突处置的可见结局；无共享状态、值返回，不代表项目提交成功。 */
enum class DraftConflictResolution { Cancelled, Rebuilt, Unavailable };
/**
 * @brief 先保留原稿再显示三选项；只有显式重新编辑才请求完整基线重建。
 * @param parent [in] 非拥有Qt父窗口，可空；对话框仅本次UI线程同步存活。
 * @param controller [in,out] 当前会话控制器，借用至函数返回；不得在模态中销毁。
 * @param module [in] 已挂接且有未决Stale的模块token，不读取其他域负载。
 * @param stillCurrent [in] 宿主同步复核同项目/分支/tip/原稿的回调，必须非空；
 *        每次保存/重建前调用，false时禁止重建，不能自动换锚或提交。
 * @return 取消、完整重建成功或会话不可用；保存/读取失败保持原稿并在窗口提示。
 * @throws std::invalid_argument 缺少guard，属装配违约。磁盘仍经控制器后台执行器。
 */
DraftConflictResolution presentDraftConflict(QWidget* parent,IDraftController& controller,
    const ModuleDraftHandle& module,const std::function<bool()>& stillCurrent);
}
#endif
