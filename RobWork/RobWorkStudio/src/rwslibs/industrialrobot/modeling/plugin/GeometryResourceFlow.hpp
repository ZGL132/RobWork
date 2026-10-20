/**
 * @file   GeometryResourceFlow.hpp
 * @brief  几何资源选择流（UI-T48——io 端口真装：IResourceReader/
 *         SafePath/BudgetGuard 公共面消费＋格式族过滤＋域原语触发）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T48.json acceptance 1/3（资源选择器＋
 *     拒绝路径）；
 *   - io 公共契约 ResourceIo.hpp（IResourceReader——snapshot＋identify
 *     两入口；P-1 UserSource 角色＝用户对话框自选路径的合法通道，
 *     NFR-SEC-01"路径只在此层出现"）＋makeResourceReader/makeBudgetGuard
 *     装配线；
 *   - units/modeling.md §6.7（Recorded 登记面——域原语
 *     attachExternalGeometry 承接；本流只做 io 探测缝装配与 UI 交互编排，
 *     零模型语义）；
 *   - P-MDL-8（零直读——UI-T41 批次C 的 std::ifstream 替身随本流退役：
 *     几何资源读取全部经 io 受管路径；URDF/Xacro 导入直读属其契约范围
 *     不在本卡——挂账面按契约 note 如实收窄）。
 *
 * 线程模型：全部入口仅 UI 线程（§3.4 M-1——对话框与编辑流同线程）。
 */

#ifndef IRD_MODELING_PLUGIN_GEOMETRYRESOURCEFLOW_HPP
#define IRD_MODELING_PLUGIN_GEOMETRYRESOURCEFLOW_HPP

#include <filesystem>
#include <optional>
#include <string>

#include <QString>
#include <QWidget>

#include <sdurws/ird/modeling/GeometryLinkEdit.hpp>  // ResourceProbeFn/GeometrySlot/域原语（几何引用编辑轨）
#include <sdurws/ird/modeling/Template.hpp>          // ModelingWorkingSet（编辑态载体）

namespace sdurws {
namespace ird {
namespace modeling {

// =====================================================================
// 探测缝生产装配（io 端口真装——域 ResourceProbeFn 的 io 公共面适配）
// =====================================================================

/**
 * @brief 生产探测缝（io::IResourceReader::snapshot＋identify 的组合适配
 *        ——ResourceProbeFn 契约的 io 公共面实现）。
 *
 * 装配形态（makeResourceReader＋makeBudgetGuard 产品实例、P-1 UserSource
 * 角色——用户对话框自选路径的 io 合法通道；SafePath 逃逸检查/预算单文件
 * 预检/格式识别/SHA-256 摘要全部在 reader 端口内完成）。实例无状态——
 * 调用方可持有单例复用；返回闭包捕获 reader/budget 的 unique_ptr 所有权。
 *
 * 格式族判定：identify 产物∈{BinaryStl, AsciiStl, WavefrontObj,
 * ColladaDae}（§2.5 行 6 几何承载族——stl/obj/dae；wrl/iv 老格式族 io
 * 阶段 A 未登记识别＝IO-FORMAT-MESH-UNKNOWN 诚实拒绝，如实呈现不伪造
 * 支持）；URDF/Xacro/纹理等登记族∈isMeshFamily=false（原语按
 * UnsupportedKind 拒绝）。
 */
ResourceProbeFn makeIoResourceProbe();

/** @brief 工具/场景管理的几何来源词表；仅私有UI编排，未改变项目存储。 */
enum class ManagedGeometrySource { None, Existing, External };
/**
 * @brief 根据已有引用或Recorded网格后缀识别几何资源，拒绝来源XML等非几何。
 * @param ws [in] 当前完整草稿，仅读取，不接管任何资源所有权。
 * @param key [in] 清单资源键；不存在或非几何返回nullopt。
 * @return 原几何类别或Mesh；不读取文件，不产生第二套摘要，纯函数可重入。
 */
std::optional<GeometryKind> existingGeometryKind(const ModelingWorkingSet& ws,const std::string& key);
/**
 * @brief 在完整工作集副本登记几何来源，资源键/复用/格式过滤沿B3f既有规则。
 * @param before [in] 只读完整基线；恒不修改。
 * @param geometry [in,out] 调用方持有的候选几何；仅成功更新，替换时保留局部m/rad位姿。
 * @param source [in] None摘除、Existing引用、External沿io安全读取。
 * @param resourceOrPath [in] 资源键或用户明确选择的UTF-8路径。
 * @param probe [in] External资源端口；缺位或失败返回拒绝，不吞错误。
 * @param error [out] 中文纠错信息；成功清空。
 * @return 带资源清单的候选工作集，失败nullopt；只有上层完整事务才写权威草稿。
 * 纯值编排不调用Qt；有副作用的文件读取只经probe，调用期间不得并发修改基线。
 */
std::optional<ModelingWorkingSet> evaluateManagedGeometry(const ModelingWorkingSet& before,
    std::optional<GeometryRef>& geometry,ManagedGeometrySource source,const std::string& resourceOrPath,
    const ResourceProbeFn& probe,std::string& error);

// =====================================================================
// 面板编排（文件对话框＋域原语触发——拒绝呈现回流）
// =====================================================================

/**
 * @brief 资源选择器编排（acceptance 1 的 UI 半区）。
 *
 * 流程：文件对话框（格式族过滤器 stl/obj/dae——用户取消＝静默返回，
 * 非错误）→域原语 attachExternalGeometry（探测缝＝makeIoResourceProbe
 * 产物）→接受＝true（调用方编排树/属性刷新——结构变更同款全量重建）；
 * 拒绝＝false＋经 sink 呈现比较型原因（io 拒绝 detail 直投——ERR-01）。
 *
 * @param parent    [in] 对话框父窗口
 * @param ws        [in,out] 编辑态工作集（现取——面板零副本）
 * @param linkIndex [in] 目标连杆下标
 * @param slot      [in] 挂接槽
 * @param sink      [in] 编辑回流（拒绝呈现——IPanelEditSink 同款回调面）
 * @return true＝挂接落草稿；false＝用户取消或域拒绝（工作集未动）
 */
bool runGeometryResourceSelection(QWidget& parent, ModelingWorkingSet& ws,
                                  std::size_t linkIndex, GeometrySlot slot,
                                  class IPanelEditSink& sink);

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_MODELING_PLUGIN_GEOMETRYRESOURCEFLOW_HPP
