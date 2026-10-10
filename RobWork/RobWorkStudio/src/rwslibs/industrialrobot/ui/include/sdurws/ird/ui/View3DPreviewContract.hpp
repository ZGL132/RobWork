/**
 * @file   View3DPreviewContract.hpp
 * @brief  三维会话预览协议（UI-T33 收口——工位标记/区域边界框/采样格
 *         着色的宿主挂接值面）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T33.json acceptance 1~3（工位标记/
 *     区域边界框/采样格着色三维预览）；
 *   - View3D 阶段 B 方案 spec §2.4（呈现应用链——着色判定归域侧采样
 *     纯函数，网关零判定；本协议只承载**已判定**的着色态词表）；
 *   - requirements.md §9.8 区域面板行（"区域轮廓与采样格三维预览——
 *     仅几何预览；结果着色归 KIN-07"）。
 *
 * 背景说明（第一读者须知——为什么是独立协议而不是修订呈现链）：
 *   工位标记/区域预览是**会话编辑面**（PM-11——会话投影随编辑刷新，
 *   零修订零第二状态源），与 RT-T14 修订呈现链（appliedRevision 绑定
 *   的发布快照整场景呈现）语义不同源。本协议是 ui 自有值面（R-2——
 *   ui 对业务域零编译依赖；需求域装配层把域内预览几何投影为本协议值
 *   后投递，域类型不出 requirements 插件边界）。
 *
 * 坐标系约定：全部位置/方向为**世界系**（m/rad——投影方负责参考系
 *   变换；参考系缺失＝投影失败诚实呈现，协议层无参考系语义）。
 *
 * 线程约束：仅 UI 线程（§3.4——宿主三维交互面）。
 */

#ifndef SDURWS_IRD_UI_VIEW3DPREVIEWCONTRACT_HPP
#define SDURWS_IRD_UI_VIEW3DPREVIEWCONTRACT_HPP

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>


namespace sdurws {
namespace ird {
namespace ui {

/**
 * @brief 工位标记值（acceptance 1——非禁用工位的坐标轴＋名称标签）。
 *
 * 双形态语义（UI-T77——F-555 修复增量；表尾追加纪律〔UI-T65 tint 同款〕
 * ——既有消费面缺省构造即"挂帧"旧语义，二进制面零破坏）：
 *   - position 无值（nullopt，缺省）＝**挂帧指示器**：标记经 frameName
 *     挂接宿主 WorkCell 对应帧（坐标轴随帧位姿动——会话/示教移动零重
 *     投；投影方零位姿计算）——UI-T33 既有语义原样保持；
 *   - position 有值＝**空间点位**：坐标轴与标签渲染于该世界系点位（工
 *     位作为带坐标的空间点在三维可辨——F-555：此前工位坐标在投影链全
 *     程丢失，标记恒渲染于参考系原点）。label 承载为场景节点名。
 *
 * 语义约定：enabled=false 的工位**不入**标记集（投影方过滤——协议值
 * 集合即呈现集合，渲染层零过滤逻辑）；帧不可解析（宿主 WorkCell 无
 * 对应帧）＝挂帧形态下该标记由渲染后端跳过并计数（失败可见面——投影
 * 方摘要承载）；position 形态下参考系解析失败由**投影方**降级（世界系
 * 变换不可得＝position 置空回落挂帧形态——诚实降级，协议层无参考系
 * 语义）。
 */
struct View3DFrameMarker {
    /// 名称标签（工程用语——UX-02；承载为场景节点名后缀）。
    std::string label;
    /// 挂接帧名（宿主 WorkCell 帧名——工位参考帧；渲染层 findFrame
    /// 挂接，坐标轴随帧位姿动）。
    std::string frameName;
    /// 工位点位置（UI-T77——F-555 增量；**世界系** m，契约头注坐标系
    /// 约定——投影方负责 refFrame→世界变换〔区域框角点同款纪律〕；
    /// nullopt＝挂帧指示器形态〔缺省——UI-T33 旧语义零破坏〕。表尾
    /// 追加纪律：既有消费面缺省构造语义不变）。
    std::optional<rw::math::Vector3D<double>> position;

    bool operator==(const View3DFrameMarker& o) const
    {
        return label == o.label && frameName == o.frameName
            && position == o.position;
    }
    bool operator!=(const View3DFrameMarker& o) const { return !(*this == o); }
};

/**
 * @brief 区域框覆盖率映射档（UI-T65——F-495"覆盖率颜色映射"的框层承载；
 * 呈现对照档位：分档由消费侧按"计数比×目标下限"对照计算〔呈现对照
 * 非工程判定——判定权威归域/evidence〕，渲染层零判定只分色）。
 */
enum class View3DTint {
    None,   ///< 无映射（未评估/分母零/无对照目标——缺省蓝保持）
    Good,   ///< 达标档（位置口径计数比 ≥ 目标下限——绿）
    Weak,   ///< 未达档（计数比 < 目标下限——黄；零达标也走本档——
            ///  "零达标"强信号呈现归消费侧裁定，映射两档保守）
    Failed, ///< 预留档（当前映射**无生产者**——view3DTintFromCoverage
            ///  零达标实走 Weak；本值保留供后续"零达标→红"强信号呈现
            ///  裁定，渲染层分色已就位〔词表 kSampleFailedGl〕——E1 注）
};

/**
 * @brief 区域边界框线框值（acceptance 2——选中区域的三维轮廓预览）。
 *
 * 八角点为**世界系**盒角（投影方自 refFrame 系变换；参考系缺失＝投影
 * 失败，不产本值——诚实呈现路径在投影方）。渲染为 12 棱线框。
 */
struct View3DBoxOutline {
    /// 盒八角点（世界系，m；序＝requirements 侧 regionPreviewGeometry
    /// 同款角序——投影方零重排直投）。
    std::array<rw::math::Vector3D<double>, 8> corners{};

    /// 覆盖率映射档（UI-T65——nullopt＝无映射〔未评估/零分母/无对照
    /// 目标——缺省蓝保持〕；枚举档位语义见 View3DTint 注。表尾追加
    /// 纪律：既有消费面缺省构造即 None 语义，二进制面零破坏）。
    std::optional<View3DTint> tint;

    bool operator==(const View3DBoxOutline& o) const
    {
        return corners == o.corners && tint == o.tint;
    }
    bool operator!=(const View3DBoxOutline& o) const { return !(*this == o); }
};

/// 采样格单元着色态词表（判定已由域侧采样纯函数完成——本协议零判定）。
/// UI-T65 表尾追加 NotSampled：F-495 四态词表（绿=通过/红=未通过/
/// 灰=未采样/黄=警告）的协议承载——未运行/未采样样本的诚实态（不虚构
/// 判定；与"cellStates 空＝中性格线"的整层空态语义正交：四值词表表达
/// 逐点事实，空序列表达"本层无评估结果"）。
enum class View3DCellState {
    Good,        ///< "good"——达标单元
    Weak,        ///< "weak"——临界/数据不足单元（黄）
    Failed,      ///< "failed"——不达标单元（红）
    NotSampled,  ///< "not-sampled"——未采样/未运行单元（灰；UI-T65 追加）
};

/**
 * @brief 采样格线值（acceptance 3——采样格着色预览）。
 *
 * 着色语义：cellStates 与 samples 一一对应（序即渲染着色依据——判定
 * 已在域侧完成，本协议零判定）；**cellStates 空＝无评估结果的中性格
 * 呈现**（诚实空态——不虚构判定；契约 note②"预览零结果语义"）。
 * gridLines＝采样格骨架线段（世界系——UI-T52 字面化：格线与着色点
 * 并存呈现，格线灰/点分色）。
 */
struct View3DSampleGrid {
    /// 采样点世界系位置（m——格单元锚点，与 cellStates 一一对应）。
    std::vector<rw::math::Vector3D<double>> samples;
    /// 逐点着色态（与 samples 等长——不等长属投影方装配缺陷，渲染层
    /// fail-fast；空＝无评估结果的中性格线呈现）。
    std::vector<View3DCellState> cellStates;
    /// 采样格骨架线段（世界系，m——UI-T52：requirements 侧
    /// RegionPreviewGeometry.gridLines 经世界系变换直投；空＝无格）。
    std::vector<std::pair<rw::math::Vector3D<double>, rw::math::Vector3D<double>>>
        gridLines;

    bool operator==(const View3DSampleGrid& o) const
    {
        return samples == o.samples && cellStates == o.cellStates
            && gridLines == o.gridLines;
    }
    bool operator!=(const View3DSampleGrid& o) const { return !(*this == o); }
};

/**
 * @brief 一次预览更新的聚合值（原子整组语义——渲染层整组替换，杜绝
 *        半新半旧呈现）。
 */
struct View3DPreviewUpdate {
    /// 工位标记集（acceptance 1——全量替换语义；空＝清除标记层）。
    std::vector<View3DFrameMarker> frameMarkers;
    /// 区域边界框（acceptance 2——单选区域预览；nullopt＝清除框层）。
    std::optional<View3DBoxOutline> boxOutline;
    /// 采样格（acceptance 3；nullopt＝清除格层）。
    std::optional<View3DSampleGrid> sampleGrid;

    bool operator==(const View3DPreviewUpdate& o) const
    {
        return frameMarkers == o.frameMarkers && boxOutline == o.boxOutline
            && sampleGrid == o.sampleGrid;
    }
    bool operator!=(const View3DPreviewUpdate& o) const { return !(*this == o); }
};

/**
 * @brief 宿主三维预览出口（会话编辑面挂接——UI-T33 收口的宿主半区）。
 *
 * 实现方义务（HostView3DGateway——网关预览半区）：
 *   - applyPreview＝**原子整组替换**（标记/框/格三层全量重挂——杜绝
 *     半新半旧）；false＝挂接失败（渲染后端缺位/场景不可得——调用方
 *     诚实呈现，保留旧呈现"失败保持原状"同款事务语义）；
 *   - removePreview＝整组摘除（幂等——项目关闭/会话拆除拍对称收口，
 *     "不残留旧呈现"纪律）；
 *   - 着色判定零参与（cellStates 已由域侧判定——渲染层只分色）。
 *
 * 线程约束：仅 UI 线程。
 */
class IUiView3DPreviewOutlet {
public:
    virtual ~IUiView3DPreviewOutlet() = default;

    /**
     * @brief 原子应用一次预览更新（整组替换——见类注义务）。
     *
     * @param update [in] 预览值（三层聚合——渲染后端缺位＝false）
     * @return true＝已挂接；false＝渲染后端缺位（诚实降级——调用方
     *         呈现失败原因，不虚构预览）
     */
    virtual bool applyPreview(const View3DPreviewUpdate& update) = 0;

    /// 整组摘除（幂等——项目关闭/会话拆除拍）。
    virtual void removePreview() = 0;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_VIEW3DPREVIEWCONTRACT_HPP
