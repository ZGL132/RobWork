/**
 * @file   SampleHighlight.hpp
 * @brief  采样点高亮键解析（FIX-T2——F-495 样本级定位的渲染侧纯值面）。
 *
 * 设计依据：
 *   - 需求 F-495（校验项点击定位采样点——样本级半区；建议语义
 *     "蓝框=当前选中"）
 *   - 契约 tasks/foundation/FIX-T2.json outputs（解析纯函数抽 ui_test
 *     可编译面——四路径：命中/未命中/空对齐表/无键）
 *
 * 背景说明：View3DSampleGrid.samples 在投影合并段按区域过滤多计划结果
 * 后，数组下标与域样本号（SampleRecord::sampleIndex）错位（P-FIX-4）——
 * 高亮请求以域样本号跨缝寻址，渲染层经本函数把键翻译回数组下标取位置。
 * 纯函数零依赖（不触渲染/GUI/会话态），确定性输出（NFR-COR-02 同族）。
 */

#ifndef RWS_IRD_UI_PLUGIN_SAMPLE_HIGHLIGHT_HPP
#define RWS_IRD_UI_PLUGIN_SAMPLE_HIGHLIGHT_HPP

#include <optional>

#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/ui/View3DPreviewContract.hpp>

namespace sdurws {
namespace ird {
namespace ui {

/**
 * @brief 解析高亮样本的世界系位置（渲染层取位纯函数）。
 *
 * 四条路径（FIX-T2 契约 acceptance"诚实降级面"的载体）：
 *   1. 命中——对齐表含键：返回对应下标的 samples 位置；
 *   2. 键未命中——对齐表非空但不含键（如高亮请求与评估结果的区域
 *      切片不同步）：nullopt＝诚实不亮，零虚构位置；
 *   3. 空对齐表——sampleIndices 空（投影方未透传对齐键）：nullopt
 *      ＝高亮功能诚实降级（旧投影方零破坏——FIX-T2 追加字段缺省
 *      语义）；
 *   4. 无键——highlighted 为 nullopt（常态无选中）：nullopt。
 *
 * 对齐表与 samples 不等长属投影方装配缺陷（契约注 fail-fast 面）——
 * 本函数只读 min(两表长) 范围内的点（防御性遍历上界），不抛不补齐：
 * 装配缺陷的暴露归投影断言用例，渲染路径 fail-fast 会崩绘制线程
 * （比"少一个高亮点"更伤——纵深选择诚实不亮）。
 *
 * @param grid        [in] 采样格投影值（世界系位置＋对齐表＋高亮键）
 * @return 命中时＝该样本世界系坐标（m）；否则 nullopt
 *
 * 复杂度：O(n)，n＝对齐表长（线性查键——样本规模千级内足够；排序表
 * 属投影方优化空间，本函数不假设有序）。
 */
inline std::optional<rw::math::Vector3D<double>> resolveHighlightedPosition(
    const View3DSampleGrid& grid)
{
    // 无键＝常态（未点击/已清位）——最便宜的早退放最前。
    if (!grid.highlightedSampleIndex.has_value()) {
        return std::nullopt;
    }
    // 空对齐表＝旧投影方或未透传——高亮功能诚实降级（路径 3）。
    if (grid.sampleIndices.empty()) {
        return std::nullopt;
    }
    // 防御性遍历上界（两表长度取小——装配缺陷不在此抛，见函数注）。
    const std::size_t bound = std::min(grid.sampleIndices.size(),
                                       grid.samples.size());
    const std::uint64_t key = *grid.highlightedSampleIndex;
    for (std::size_t i = 0; i < bound; ++i) {
        if (grid.sampleIndices[i] == key) {
            return grid.samples[i];  // 路径 1：命中——世界系坐标（m）
        }
    }
    return std::nullopt;  // 路径 2：键未命中——诚实不亮
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // RWS_IRD_UI_PLUGIN_SAMPLE_HIGHLIGHT_HPP
