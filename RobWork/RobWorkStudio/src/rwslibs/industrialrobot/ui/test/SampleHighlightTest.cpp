/**
 * @file   SampleHighlightTest.cpp
 * @brief  采样点高亮键解析与契约追加面测试（FIX-T2——F-495 样本级定位）。
 *
 * 测试锚（对齐契约 tasks/foundation/FIX-T2.json acceptance）：
 *   - 功能闭环面：resolveHighlightedPosition 命中路径——高亮键经
 *     sampleIndices 对齐表取回正确世界系位置（P-FIX-4 对齐语义）；
 *   - 诚实降级面：键未命中/空对齐表/无键三路径全部 nullopt（零虚构
 *     位置——ERR-01 不伪造）；
 *   - 契约面：View3DSampleGrid 追加字段（sampleIndices/highlightedSampleIndex）
 *     进等值比较（缺省值语义零破坏——UI-T65 ①追加纪律同款验收）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/ui/View3DPreviewContract.hpp>

#include "plugin/SampleHighlight.hpp"  // 被测解析纯函数（同源编入 TU）

#include <cstdint>
#include <vector>

using namespace sdurws::ird::ui;

namespace {

/// 夹具：两区域切片后的合并格（P-FIX-4 场景——区域过滤后数组下标与域
/// 样本号错位：本格 samples[0..2] 的域样本号是 4/5/6——第二区域切片）。
View3DSampleGrid makeSlicedGrid()
{
    View3DSampleGrid grid;
    grid.samples = {
        rw::math::Vector3D<double>(0.10, 0.0, 0.0),
        rw::math::Vector3D<double>(0.20, 0.0, 0.0),
        rw::math::Vector3D<double>(0.30, 0.0, 0.0),
    };
    grid.cellStates = {
        View3DCellState::Good,
        View3DCellState::Failed,
        View3DCellState::NotSampled,
    };
    grid.sampleIndices = {4, 5, 6};  // 域样本号≠数组下标（切片偏移实证面）
    return grid;
}

}  // namespace

/// 命中路径：键 5 → 数组下标 1 的位置（对齐表翻译——P-FIX-4 核心语义；
/// 若按下标直取会错拿 samples[5] 越界或他点）。
TEST(SampleHighlight, ResolveHitsViaAlignmentTable_FIX_T2)
{
    View3DSampleGrid grid = makeSlicedGrid();
    grid.highlightedSampleIndex = std::uint64_t{5};

    const std::optional<rw::math::Vector3D<double>> pos =
        resolveHighlightedPosition(grid);
    ASSERT_TRUE(pos.has_value());
    EXPECT_DOUBLE_EQ((*pos)[0], 0.20);  // samples[1]（键 5 的对齐点）
    EXPECT_DOUBLE_EQ((*pos)[1], 0.0);
    EXPECT_DOUBLE_EQ((*pos)[2], 0.0);
}

/// 键未命中路径：对齐表非空但无此键（区域切片与高亮请求不同步的诚实
/// 不亮——零虚构位置）。
TEST(SampleHighlight, ResolveMissYieldsNullopt_FIX_T2)
{
    View3DSampleGrid grid = makeSlicedGrid();
    grid.highlightedSampleIndex = std::uint64_t{99};

    EXPECT_FALSE(resolveHighlightedPosition(grid).has_value());
}

/// 空对齐表路径：旧投影方未透传对齐键——高亮功能诚实降级（FIX-T2
/// 追加字段缺省语义零破坏：无键时不亮，渲染照旧）。
TEST(SampleHighlight, EmptyAlignmentTableDegradesHonestly_FIX_T2)
{
    View3DSampleGrid grid = makeSlicedGrid();
    grid.sampleIndices.clear();  // 无对齐键
    grid.highlightedSampleIndex = std::uint64_t{4};

    EXPECT_FALSE(resolveHighlightedPosition(grid).has_value());
}

/// 无键路径：常态未选中（nullopt 高亮键）——零产出不画占位。
TEST(SampleHighlight, NoKeyYieldsNullopt_FIX_T2)
{
    View3DSampleGrid grid = makeSlicedGrid();
    grid.highlightedSampleIndex = std::nullopt;

    EXPECT_FALSE(resolveHighlightedPosition(grid).has_value());
}

/// 契约等值面：追加字段进 operator==（任一新字段差异即不等——投影
/// 断言/合并投递的等值依赖此语义；两字段全等＝相等）。
TEST(SampleHighlight, SampleGridEqualityIncludesNewFields_FIX_T2)
{
    View3DSampleGrid a = makeSlicedGrid();
    a.highlightedSampleIndex = std::uint64_t{5};
    View3DSampleGrid b = a;  // 全等副本
    EXPECT_TRUE(a == b);

    b.highlightedSampleIndex = std::uint64_t{4};  // 仅高亮键异
    EXPECT_FALSE(a == b);

    View3DSampleGrid c = makeSlicedGrid();
    c.highlightedSampleIndex = std::uint64_t{5};
    c.sampleIndices[0] = 40;  // 仅对齐表异
    EXPECT_FALSE(a == c);
}

/// 缺省构造零破坏面：新字段缺省（空表＋nullopt）的两格相等——既有
/// View3D 预览用例与投影方零回归的契约底座（UI-T65 ①追加纪律同款）。
TEST(SampleHighlight, DefaultFieldsPreserveLegacyEquality_FIX_T2)
{
    View3DSampleGrid legacy;
    legacy.samples = {rw::math::Vector3D<double>(1.0, 2.0, 3.0)};
    legacy.cellStates = {View3DCellState::Good};
    View3DSampleGrid withNew = legacy;  // 新字段均缺省
    EXPECT_TRUE(legacy == withNew);
}
