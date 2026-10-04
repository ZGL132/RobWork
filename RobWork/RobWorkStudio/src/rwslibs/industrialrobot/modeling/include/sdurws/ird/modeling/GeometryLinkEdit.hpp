/**
 * @file   GeometryLinkEdit.hpp
 * @brief  几何资源引用编辑原语（UI-T48——差距清单 G3 资源选择器＋G4 引用
 *         挂换摘；units/modeling.md §6.7 资源三段边界＋§4.10 I-MDL-9/10
 *         ＋§5.2 编辑轨道的 T07 纯函数形态）。
 *
 * 设计依据：
 *   - units/modeling.md §6.7（资源三段边界——Recorded 登记面：外部引用
 *     ＋摘要；对象字节只存引用 CON-03；"复制入项目资源区"提前固化不
 *     做——Recorded 单态）；§4.10（I-MDL-9 引用保护/I-MDL-10 状态机
 *     Recorded→Solidified 不可逆——本卡零固化语义）；
 *   - §4.3 resourceManifest 行（ResourceRef 值面：resourceId＋
 *     contentDigest 身份要素＋externalRecord Recorded 必填）；
 *   - io 公共契约 ResourceIo.hpp（IResourceReader::snapshot——受管路径
 *     SafePath＋预算 BudgetGuard＋格式识别 ResourceKind；P-MDL-8——本
 *     原语零文件系统直读，全部经注入的 reader 端口）；
 *   - 任务契约 tasks/foundation/UI-T48.json acceptance 1~3（G3＋G4；
 *     C5 消账——UI-T41 批次C 登记不实施项随本卡兑现）。
 *
 * 背景说明（第一读者须知——为什么不直接在 UI 层拼装）：
 *   资源登记是模型语义操作（清单键生成/摘要身份/引用保护），归域；UI
 *   只做文件对话框与呈现。本头两原语与 StructureEdit 四原语同一形态：
 *   **域纯函数裁决，工作集就地变更，拒绝路径工作集字节不变**；io 交互
 *   （读文件/识别/预算）经 Deps 注入的 reader 函数缝——原语本体零 I/O
 *   （确定性可测；P-MDL-8 的结构执行）。
 *
 * 线程安全：非线程安全（编辑态仅 UI 线程——ModelingWorkingSet 注）。
 * 确定性：reader 缝返回的快照由 io 侧确定性保证（同文件同摘要）；清单
 * 键生成为确定性扫描。
 */

#ifndef SDURWS_IRD_MODELING_GEOMETRYLINKEDIT_HPP
#define SDURWS_IRD_MODELING_GEOMETRYLINKEDIT_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <sdurws/ird/core/Digest.hpp>       // Digest256（资源身份要素）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // ModelingWorkingSet 载体域值面（GeometryRef/ResourceRef/LinkEntry）
#include <sdurws/ird/modeling/Template.hpp>     // ModelingWorkingSet（编辑态载体——§9.4.1）

namespace sdurws::ird::modeling {

// =====================================================================
// 错误面（StructureEditError 同款局部枚举先例——不进域级错误轨道）
// =====================================================================

/**
 * @brief 几何引用编辑拒绝面（§6.7 三段边界＋io 公共面错误的域侧汇集；
 *        token 见 geometryLinkErrorCodeToken）。
 */
enum class GeometryLinkErrorCode {
    /// "io-rejected"——io 端口拒绝（路径逃逸/预算超限/格式不识别/读取
    /// 失败——detail 携带 io 侧定位文本，ERR-01 诚实呈现的载体）
    IoRejected,
    /// "unsupported-kind"——格式识别成功但不在几何承载族（stl/obj/dae
    /// 之外——如 URDF XML/纹理；§2.5 行 6 格式族边界）
    UnsupportedKind,
    /// "duplicate-resource"——同 resourceId 已登记（清单键冲突；确定性
    /// 键生成下实际不可达，防御面）
    DuplicateResource,
    /// "no-such-resource"——替换/摘除时引用键不在清单（悬空引用上操作
    /// ——先摘除旧引用或先修复清单）
    NoSuchResource,
    /// "index-out-of-range"——连杆下标越界
    IndexOutOfRange,
};

/// 局部错误码稳定 token（枚举成员名连字符串——UT 判别与 UI 呈现映射）。
std::string_view geometryLinkErrorCodeToken(GeometryLinkErrorCode code) noexcept;

/**
 * @brief 几何引用编辑拒绝值（码＋定位细节——detail 面向诊断链/日志；
 *        io 拒绝时 detail 携带 io 侧错误 token＋定位，UI 直投呈现）。
 */
struct GeometryLinkError {
    GeometryLinkErrorCode code = GeometryLinkErrorCode::IoRejected;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8）

    bool operator==(const GeometryLinkError& o) const noexcept
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const GeometryLinkError& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// reader 函数缝（P-MDL-8——原语零 I/O，io 交互全部经注入）
// =====================================================================

/**
 * @brief io 读取缝的域侧值载体（io ResourceSnapshot 的最小投影——ui/域
 *        面零 io 类型知识，R-2 值面解耦；三字段即登记所需全部事实）。
 */
struct ResourceProbeResult {
    core::Digest256 contentDigest{};  ///< 内容摘要（SHA-256——ResourceRef 身份要素）
    std::string absPath;              ///< 实体绝对路径（weakly_canonical——ExternalResourceRecord）
    bool isMeshFamily = false;        ///< 格式识别∈几何承载族（stl/obj/dae——§2.5 行 6）
};

/**
 * @brief 资源探测缝（装配注入 io::IResourceReader::snapshot＋identify 的
 *        组合适配——生产装配在 plugin 层；测试绑替身）。
 *
 * 契约：ok=false 时 errText 携带 io 侧失败定位（预算超限/逃逸路径/读取
 * 失败——io 错误 token 原文，ERR-01 诚实呈现素材）；ok=true 时 isMeshFamily
 * 反映格式识别结果（几何承载族之外由原语按 UnsupportedKind 拒绝——io
 * 识别成功≠几何可用，两族边界归 modeling）。
 */
using ResourceProbeFn =
    std::function<std::optional<ResourceProbeResult>(const std::string& absPath,
                                                     std::string& errText)>;

// =====================================================================
// 原语 ①：资源选择器（登记＋挂接——acceptance 1）
// =====================================================================

/**
 * @brief 选择几何槽位（挂接目标——LinkEntry 的两槽词表）。
 */
enum class GeometrySlot {
    Visual,     ///< 视觉几何（LinkEntry.visual）
    Collision,  ///< 碰撞几何（LinkEntry.collision——判定唯一归 policy，本卡零判定）
};

/**
 * @brief 把外部几何文件登记进资源清单（Recorded）并挂接到指定连杆槽位。
 *
 * 流程（§6.7 Recorded 登记面）：probe 缝读文件（SafePath＋预算＋识别——
 * io 治理全在端口内）→格式族校验（非几何族＝UnsupportedKind 诚实拒绝）
 * →清单键确定性生成（"res-<序>"顺序扫描未占用值）→ResourceRef{Recorded
 * ＋externalRecord{absPath, digest}} 登记→GeometryRef{resourceRefId,
 * 恒位姿 localTransform, Mesh} 挂接目标槽→变更摘要一条（MDL-09）。
 *
 * @param ws         [in,out] 编辑态工作集
 * @param linkIndex  [in] 目标连杆下标（越界拒绝）
 * @param slot       [in] 挂接槽（visual/collision——**替换语义**：槽已
 *                   有引用时旧引用被新引用覆盖，旧清单条目若失引用保留
 *                   在清单〔其他槽/对象可能仍引用——不级联删除，悬空由
 *                   L6 检测呈现〕）
 * @param absPath    [in] 用户选择的文件路径（对话框产物——域侧不经手
 *                   UI 类型）
 * @param probe      [in] 资源探测缝（非空——装配违约 fail-fast）
 * @return nullopt＝接受；错误值＝拒绝（工作集未动——含 probe 失败/
 *         格式族外/越界）
 *
 * @throws std::invalid_argument probe 为空（装配契约违约）
 */
std::optional<GeometryLinkError>
    attachExternalGeometry(ModelingWorkingSet& ws, std::size_t linkIndex,
                           GeometrySlot slot, const std::string& absPath,
                           const ResourceProbeFn& probe);

/**
 * @brief 摘除指定连杆槽位的几何引用（acceptance 2 三态之"摘除"）。
 *
 * 语义：GeometryRef 置空＝引用移除；清单条目保留（可能被其他槽/对象
 * 引用——不级联删除，防误伤共享资源；失引用条目的悬空由 §8.2 L6 检测
 * 呈现——"让悬空可见"的既有纪律）。槽本无引用＝恒等动作（幂等——不出
 * 错不出摘要）。
 *
 * @param ws        [in,out] 编辑态工作集
 * @param linkIndex [in] 目标连杆下标
 * @param slot      [in] 摘除槽
 * @return nullopt＝接受（含幂等空摘）；错误值＝拒绝（越界）
 */
std::optional<GeometryLinkError>
    detachGeometry(ModelingWorkingSet& ws, std::size_t linkIndex, GeometrySlot slot);

/**
 * @brief 编辑几何引用的局部变换（T_link_geom——m/rad；acceptance 2 的
 *        引用字段编辑半区；值合法性＝有限性校验，单位语义归 UI 解析层）。
 *
 * @param ws        [in,out] 编辑态工作集
 * @param linkIndex [in] 目标连杆下标
 * @param slot      [in] 目标槽（须已挂引用——未设置＝NoSuchResource 拒绝）
 * @param x/y/z     [in] 平移三分量（m；须有限）
 * @param roll/pitch/yaw [in] 姿态欧拉角（rad；须有限）
 * @return nullopt＝接受；错误值＝拒绝（越界/未挂引用/非有限值——工作
 *         集未动）
 */
std::optional<GeometryLinkError>
    editGeometryLocalTransform(ModelingWorkingSet& ws, std::size_t linkIndex,
                               GeometrySlot slot, double x, double y, double z,
                               double roll, double pitch, double yaw);

}  // namespace sdurws::ird::modeling

#endif  // SDURWS_IRD_MODELING_GEOMETRYLINKEDIT_HPP