/**
 * @file   ObjectTypesTest.cpp
 * @brief  requirements 对象类型登记用例组（ReqObjTypes）——五对象 token、
 *         ProcessTag/TemplateKind/ArrayKind 三词表与 schema 版本常量
 *         （任务契约 WP-14-T02 acceptance 3 的 ObjectTypes.hpp 行具名
 *         自证）。
 *
 * 设计依据：
 *   - units/requirements.md §4.1（五对象 token 权威表）、§4.3（processTag
 *     词表 11 值）、§7.1（TemplateKind 词表 6 值）、§7.2（ArrayKind 四值）、
 *     §4.2（schemaVersion ≥1——版本常量）、§14.4（token/词表为 requirements
 *     所有权登记）
 *   - 先例：modeling/test/ObjectTypesTest.cpp（token 字面断言＋词表往返
 *     ＋try 轨拒绝的同款形态）
 *   - 任务契约 tasks/foundation/WP-14-T02.json acceptance 3
 */

#include <sdurws/ird/requirements/ObjectTypes.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

using namespace std::string_view_literals;
using sdurws::ird::requirements::ArrayKind;
using sdurws::ird::requirements::ProcessTag;
using sdurws::ird::requirements::TemplateKind;

namespace {

/// 五对象 token 的卡面权威值清单（§4.1 表"objectTypeToken"列原文——测试
/// 内自持字面清单，与头文件常量机械比对：任一侧拼写漂移即失败）。
const std::pair<std::string_view, std::string_view> kObjectTypeTokenTable[] = {
    {"kReqSetObjectType"sv, "req-set"sv},
    {"kReqPointSetObjectType"sv, "req-point-set"sv},
    {"kReqRegionSetObjectType"sv, "req-region-set"sv},
    {"kReqConditionSetObjectType"sv, "req-condition-set"sv},
    {"kReqPlanSetObjectType"sv, "req-plan-set"sv},
};

}  // namespace

/**
 * 五对象 token 字面与卡面 §4.1 权威表逐字一致（acceptance 3——token 是
 * 修订闭包/命令路由键，拼写漂移＝路由失联）：头文件常量与测试内自持
 * 清单（卡面原文）双向机械比对。
 */
TEST(ReqObjTypes, FiveObjectTypeTokensMatchCardSection41_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04", "CON-01"},
                  std::vector<std::string>{});

    // 逐行核对：常量名→卡面 token 字面（登记簿两份副本必须同值）。
    EXPECT_EQ(sdurws::ird::requirements::kReqSetObjectType, "req-set"sv);
    EXPECT_EQ(sdurws::ird::requirements::kReqPointSetObjectType, "req-point-set"sv);
    EXPECT_EQ(sdurws::ird::requirements::kReqRegionSetObjectType, "req-region-set"sv);
    EXPECT_EQ(sdurws::ird::requirements::kReqConditionSetObjectType, "req-condition-set"sv);
    EXPECT_EQ(sdurws::ird::requirements::kReqPlanSetObjectType, "req-plan-set"sv);

    // 清单完整性哨兵：表行数必须恰为 5（§4.1 权威表行数）——防测试清单
    // 被静默裁剪造成假绿。
    EXPECT_EQ(std::size(kObjectTypeTokenTable), 5U);
}

/**
 * 五对象 schema 版本常量＝1 且类型正确（acceptance 3——§4.2 schemaVersion
 * 行"≥1；NFR-DEP-04 主版本不识别→稳定拒绝"）：常量是 schema 字段的唯一
 * 取值点（结构体初始化禁止写字面量——登记簿纪律），值序/类型在此锁定。
 */
TEST(ReqObjTypes, SchemaVersionConstantsAreOne_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-DEP-04"},
                  std::vector<std::string>{});

    // 值断言（全部对象 schema 主版本初版＝1——§4.2 根对象字段表＋四集合
    // 对象结构对称同版演进）。
    EXPECT_EQ(sdurws::ird::requirements::kReqSetSchemaVersion, 1U);
    EXPECT_EQ(sdurws::ird::requirements::kReqPointSetSchemaVersion, 1U);
    EXPECT_EQ(sdurws::ird::requirements::kReqRegionSetSchemaVersion, 1U);
    EXPECT_EQ(sdurws::ird::requirements::kReqConditionSetSchemaVersion, 1U);
    EXPECT_EQ(sdurws::ird::requirements::kReqPlanSetSchemaVersion, 1U);
}

/**
 * ProcessTag 词表 11 值全表往返（acceptance 3——§4.3 词表；§14.4 登记制）：
 * processTagToken 产出词表原文串、tryProcessTag 全表可逆；两向逐值机械
 * 比对——登记簿失同步即刻暴露。
 */
TEST(ReqObjTypes, ProcessTagVocabularyElevenValuesRoundTrip_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-07"},
                  std::vector<std::string>{});

    // 词表串＝§4.3 括注原文序（登记簿纪律：值序只允许表尾追加）。
    const std::pair<ProcessTag, std::string_view> kTable[] = {
        {ProcessTag::Generic, "Generic"sv},
        {ProcessTag::Pick, "Pick"sv},
        {ProcessTag::Place, "Place"sv},
        {ProcessTag::MachineLoad, "MachineLoad"sv},
        {ProcessTag::MachineUnload, "MachineUnload"sv},
        {ProcessTag::Inspect, "Inspect"sv},
        {ProcessTag::WeldStart, "WeldStart"sv},
        {ProcessTag::WeldEnd, "WeldEnd"sv},
        {ProcessTag::ToolChange, "ToolChange"sv},
        {ProcessTag::SafeStandby, "SafeStandby"sv},
        {ProcessTag::Handover, "Handover"sv},
    };
    ASSERT_EQ(std::size(kTable), 11U) << "ProcessTag 词表应恰 11 值（§4.3）";

    for (const auto& [tag, token] : kTable) {
        // 正向：同标签同串（NFR-COR-02——确定性码面子集）。
        EXPECT_EQ(sdurws::ird::requirements::processTagToken(tag), token);
        // 反向：全表可逆（往返一致）。
        const auto back = sdurws::ird::requirements::tryProcessTag(token);
        ASSERT_TRUE(back.has_value()) << "词表内串必须可解析: " << token;
        EXPECT_EQ(*back, tag);
    }
}

/**
 * ProcessTag try 轨拒绝词表外串（ARC-04"不猜测"）：未知串/大小写变体/
 * 空串一律 nullopt——精确等值比较无折叠。
 */
TEST(ReqObjTypes, ProcessTagUnknownTokenRejected_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"},
                  std::vector<std::string>{});

    EXPECT_FALSE(sdurws::ird::requirements::tryProcessTag("pick").has_value())
        << "小写变体属词表外（无大小写折叠）";
    EXPECT_FALSE(sdurws::ird::requirements::tryProcessTag(" Pick").has_value())
        << "前导空白不剥离（ARC-04 不猜测）";
    EXPECT_FALSE(sdurws::ird::requirements::tryProcessTag("Weld").has_value())
        << "部分匹配不是命中";
    EXPECT_FALSE(sdurws::ird::requirements::tryProcessTag("").has_value())
        << "空串词表外";
}

/**
 * TemplateKind 词表 6 值全表往返（acceptance 3——§7.1 词表；REQ-07 承接
 * 旧六类模板）。
 */
TEST(ReqObjTypes, TemplateKindVocabularySixValuesRoundTrip_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-07"},
                  std::vector<std::string>{});

    // 词表串＝§7.1 原文序。
    const std::pair<TemplateKind, std::string_view> kTable[] = {
        {TemplateKind::BinPicking, "BinPicking"sv},
        {TemplateKind::MachineTending, "MachineTending"sv},
        {TemplateKind::Palletizing, "Palletizing"sv},
        {TemplateKind::Inspection, "Inspection"sv},
        {TemplateKind::ToolChange, "ToolChange"sv},
        {TemplateKind::Handover, "Handover"sv},
    };
    ASSERT_EQ(std::size(kTable), 6U) << "TemplateKind 词表应恰 6 值（§7.1）";

    for (const auto& [kind, token] : kTable) {
        EXPECT_EQ(sdurws::ird::requirements::templateKindToken(kind), token);
        const auto back = sdurws::ird::requirements::tryTemplateKind(token);
        ASSERT_TRUE(back.has_value()) << "词表内串必须可解析: " << token;
        EXPECT_EQ(*back, kind);
    }
    // 词表外拒绝（小写变体——同 ProcessTag 不猜测口径）。
    EXPECT_FALSE(sdurws::ird::requirements::tryTemplateKind("palletizing").has_value());
}

/**
 * ArrayKind 词表 4 值全表往返（acceptance 3——§7.2 阵列四构型；REQ-11）。
 */
TEST(ReqObjTypes, ArrayKindVocabularyFourValuesRoundTrip_WP14T02_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{});

    // 词表串＝§7.2 原文序（Linear→Rectangular→Circular→Polyline）。
    const std::pair<ArrayKind, std::string_view> kTable[] = {
        {ArrayKind::Linear, "Linear"sv},
        {ArrayKind::Rectangular, "Rectangular"sv},
        {ArrayKind::Circular, "Circular"sv},
        {ArrayKind::Polyline, "Polyline"sv},
    };
    ASSERT_EQ(std::size(kTable), 4U) << "ArrayKind 词表应恰 4 值（§7.2）";

    for (const auto& [kind, token] : kTable) {
        EXPECT_EQ(sdurws::ird::requirements::arrayKindToken(kind), token);
        const auto back = sdurws::ird::requirements::tryArrayKind(token);
        ASSERT_TRUE(back.has_value()) << "词表内串必须可解析: " << token;
        EXPECT_EQ(*back, kind);
    }
    // 词表外拒绝（未知构型——区域几何扩展走需求变更，P-REQ-7：不预留）。
    EXPECT_FALSE(sdurws::ird::requirements::tryArrayKind("Grid").has_value());
}
