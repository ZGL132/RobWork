/**
 * @file   View3DPreviewMarkerPositionTest.cpp
 * @brief  预览协议工位标记 position 追加纪律测试（UI-T77——F-555 修复的
 *         契约值面钉住；集成树专属——rw/math 头依赖面）。
 *
 * 设计依据：
 *   - findings F-555（工位三维标记恒渲染于参考系原点——投影契约不携带
 *     位置数据）：View3DFrameMarker 表尾追加 optional position（世界系
 *     m；nullopt＝挂帧指示器旧语义）；
 *   - 追加纪律先例：UI-T65 tint（View3DBoxOutline 表尾追加 optional——
 *     既有消费面缺省构造语义不变、二进制面零破坏）。
 *
 * 为什么独立 TU 且集成树专属：View3DPreviewContract.hpp 携带 rw/math
 * 头依赖（Vector3D/Transform3D），而 ui_test 主列表是模型层测试（冒烟
 * 模式无框架目标——rw include 面不存在，core 冒烟分支不链 sdurw_math
 * ——双模式既有设计）。本 TU 编入 sdurws_ird_ui_test 的集成树专属门控
 * 块（HostCompilePipelineTest 同款形态——传递链接面供给 rw 头）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/ui/View3DPreviewContract.hpp>

#include <rw/math/Vector3D.hpp>

namespace ui = sdurws::ird::ui;

/**
 * position 字段（UI-T77——F-555）表尾追加的三重保证：缺省 nullopt＝挂帧
 * 形态（UI-T33 旧语义零破坏——既有消费面缺省构造二进制兼容）；两字段
 * 聚合初始化仍合法（既有构造点零改）；operator== 涵盖 position（预览
 * 更新的原子整组替换语义依赖完整相等——漏比＝点值形态刷新失明）。
 */
TEST(View3DPreviewMarkerPosition, FrameMarkerPositionAppendOnly_UI_T77)
{
    IRD_TEST_INFO("REQ-01", {}, std::nullopt);

    // ①两字段聚合初始化仍合法＋缺省＝挂帧形态（nullopt）。
    const ui::View3DFrameMarker bare{"工位 A", "WORLD"};
    EXPECT_FALSE(bare.position.has_value())
        << "缺省 position 应为 nullopt（挂帧指示器——UI-T33 旧语义零破坏）";

    // ②点值形态：世界系位置随构造冻结（F-555——工位作为空间点可辨）。
    const ui::View3DFrameMarker positioned{
        "工位 A", "WORLD", rw::math::Vector3D<double>(0.5, 0.5, 0.0)};
    ASSERT_TRUE(positioned.position.has_value()) << "点值形态 position 应有值";
    EXPECT_DOUBLE_EQ((*positioned.position)[0], 0.5) << "位置 X 失实";
    EXPECT_DOUBLE_EQ((*positioned.position)[1], 0.5) << "位置 Y 失实";
    EXPECT_DOUBLE_EQ((*positioned.position)[2], 0.0) << "位置 Z 失实";

    // ③相等语义涵盖 position：同 label/frameName 不同 position 不等。
    EXPECT_NE(bare, positioned) << "operator== 漏比 position（原子替换失明）";
    EXPECT_EQ(positioned,
              (ui::View3DFrameMarker{"工位 A", "WORLD",
                                     rw::math::Vector3D<double>(0.5, 0.5, 0.0)}))
        << "同值 marker 应相等";

    // ④整组聚合相等语义同面（View3DPreviewUpdate 原子替换的比较输入）。
    ui::View3DPreviewUpdate withBare;
    withBare.frameMarkers.push_back(bare);
    ui::View3DPreviewUpdate withPositioned;
    withPositioned.frameMarkers.push_back(positioned);
    EXPECT_NE(withBare, withPositioned) << "整组相等语义应传导 position 差异";
}
