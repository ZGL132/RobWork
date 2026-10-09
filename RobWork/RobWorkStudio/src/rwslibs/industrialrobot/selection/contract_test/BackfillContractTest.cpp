/**
 * @file   BackfillContractTest.cpp
 * @brief  器件回填契约用例组（SelBackfillContract——WP-19-T09）——跨域
 *         契约面的机器自证：P-SEL-3 命令 token 无点词形（project §4.4.4
 *         冻结语法）、T09 批 SEL-BACKFILL-* 码登记表表尾追加与句法权威
 *         校验、处理器契约面（project::ICommandHandler 派生＋token/版本
 *         唯一性）、记录对象编解码契约（确定性/严格解码/版本演进面）、
 *         AT-30 复算提示的记录面契约。
 *
 * 设计依据：
 *   - units/selection.md §12.3（命令 token 建议值 apply-device-backfill
 *     无点形态——P-SEL-3 登记的裁决前保守口径）、§12.2（纪律九条——
 *     纪律 7"旧结果不能被覆盖；新修订不复用旧正式通过结论"的记录面）、
 *     §14.8（处理器契约——project::ICommandHandler 实现；@throws 无）、
 *     §12.4（合成断言——记录面不重复计入契约）
 *   - units/project.md §4.4.4（commandType 冻结语法 ^[a-z0-9-]{3,64}
 *     ——注册表与磁盘留痕共用该面）、§5.3（ICommandHandler 三态契约）
 *   - 需求 SEL-10/MDL-16（回填与合成不重复计入）、AT-30（新修订提示
 *     复算；复核前不沿用原通过结论）
 *   - 任务契约 tasks/foundation/WP-19-T09.json acceptance 1/3（命令端口
 *     契约面；token 无点词形＋ird_gates 零命中的单元内复核面）
 *
 * 契约测试与单元测试的分界（单元内 T-1 纪律——测试目标仅链接同单元
 * 产品目标＋testkit）：本组只消费 selection 公共头（零 project 库符号
 * ——project 公共头的纯接口/纯值类型消费与产品面同一纪律），不链接
 * sdurws_ird_project（ird_gates 2a 形态一规则③无本单元测试边登记——
 * 白名单不可私扩；端到端命令编排的契约验证随 L5 装配集成面）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/Backfill.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>

#include <sdurws/ird/core/DiagData.hpp>           // DiagnosticRecord::make（句法权威）
#include <sdurws/ird/project/CommandService.hpp>  // ICommandHandler（派生关系契约面）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <string>
#include <type_traits>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird;  // 使限定符 core::/project:: 可见

namespace {

/// 黄金单轴条目（与单元测试面同值口径——本组只关心协议/契约面）。
AxisBackfillEntry makeContractEntry()
{
    AxisBackfillEntry a;
    a.jointId = core::ObjectId::generate();
    a.catalogId = "cat-gold";
    a.catalogVersion = "v1";
    a.motorModelId = "M-GOLD";
    a.gearboxModelId = "G-GOLD";
    a.mountKind = "flangeA";
    a.catalogLockObject = core::ObjectId::generate();
    a.catalogLockVersion = core::ContentVersion{};
    a.appliedRatio = 0.1;
    a.linkMassKg = 2.0;
    a.linkComM = BackfillVec3{0.1, 0.0, 0.05};
    a.linkInertia = BackfillInertiaTensor{0.02, 0.03, 0.04, 0.0, 0.0, 0.0};
    a.motorComAnchorM = BackfillVec3{0.0, 0.0, 0.1};
    a.motorHousingMassKg = 6.0;
    a.motorHousingInertia = BackfillInertiaTensor{0.05, 0.05, 0.05, 0.0, 0.0, 0.0};
    a.gearboxComAnchorM = BackfillVec3{0.2, 0.0, 0.0};
    a.gearboxHousingMassKg = 3.0;
    a.gearboxHousingInertia = BackfillInertiaTensor{0.02, 0.02, 0.02, 0.0, 0.0, 0.0};
    a.rotorInertiaKgM2 = 0.01;
    return a;
}

}  // namespace

// =====================================================================
// P-SEL-3：命令 token 无点词形（acceptance 3 的单元内机器面）
// =====================================================================

/**
 * token 冻结语法契约（project §4.4.4 ^[a-z0-9-]{3,64}——注册表与磁盘
 * 留痕共用该面；P-PR-9 裁决前 selection 以无点保守形态占位——P-SEL-3，
 * 与 modeling D-MDL-6/O-35 同案）：处理器返回的 token 必须逐字符满足
 * 该语法且不含点。
 */
TEST(SelBackfillContract, CommandTokenDotlessFrozenSyntax_PSEL3)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{"P-SEL-3", "P-PR-9"});

    const IDeviceBackfillCommandHandler& handler = DeviceBackfillCommandHandler{};
    const std::string token = handler.commandType();
    // 冻结常量与处理器返回值同源（唯一书写点）。
    EXPECT_EQ(token, std::string(kBackfillCommandToken));
    // 长度半区 [3,64]。
    ASSERT_GE(token.size(), 3u);
    ASSERT_LE(token.size(), 64u);
    // 字符半区 [a-z0-9-]（无点——P-PR-9 裁决前的保守形态）。
    for (const char c : token) {
        EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
            << "token 字符越冻结语法: '" << c << "'";
    }
    EXPECT_EQ(token.find('.'), std::string::npos)
        << "回填 token 含点（P-PR-9 裁决前禁止——P-SEL-3 保守形态）";
}

/**
 * 处理器派生契约（§14.8——IDeviceBackfillCommandHandler : project::
 * ICommandHandler；具体类可经接口引用多态消费）：类型关系编译期自证
 * ＋运行期经接口引用调用（虚分发可达）。
 */
TEST(SelBackfillContract, HandlerDerivesProjectCommandHandler)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-02"},
                  std::vector<std::string>{});

    // 编译期：派生关系（接口与实现两级）。
    static_assert(std::is_base_of_v<project::ICommandHandler, IDeviceBackfillCommandHandler>,
                  "IDeviceBackfillCommandHandler 必须派生 project::ICommandHandler（§14.8）");
    static_assert(std::is_base_of_v<IDeviceBackfillCommandHandler, DeviceBackfillCommandHandler>,
                  "实现必须派生 IDeviceBackfillCommandHandler（L5 注册形态）");

    // 运行期：经 ICommandHandler 基类引用的虚分发（L5 装配注册后的
    // 消费形态——命令服务按基类指针分发）。
    DeviceBackfillCommandHandler impl;
    project::ICommandHandler& base = impl;
    EXPECT_EQ(base.commandType(), std::string(kBackfillCommandToken));
    EXPECT_EQ(base.currentPayloadVersion(), kBackfillPayloadFormatVersion);
}

// =====================================================================
// T09 批码登记契约（DiagCodes 表尾追加——登记纪律的机器面）
// =====================================================================

/**
 * T09 批 9 码登记行契约：全表 54 行、尾部 9 行恰为 SEL-BACKFILL- 族、
 * 行序＝判定序（载荷结构→版本→域输入→目录/安装→数据缺失→范围→锁定
 * 引用→合成断言）、每码经 core::DiagnosticRecord::make 句法权威校验
 * （C-3——码值合法性权威在 core 句法＋diagnostics 注册面）。
 */
TEST(SelBackfillContract, T09BatchCodeEntriesTailAppendedAndSyntaxValid)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{});

    const std::vector<DiagnosticEntry> entries = selectionCodeEntries();
    // 全表 54 行（T02 批 17＋T06 批 28＋T09 批 9——登记纪律：只增不重排）。
    ASSERT_EQ(entries.size(), 54u);

    // 尾部 9 行＝T09 批（表尾追加纪律的机器面）。
    const DiagnosticEntry* t09[] = {
        &entries[45], &entries[46], &entries[47], &entries[48], &entries[49],
        &entries[50], &entries[51], &entries[52], &entries[53],
    };
    const std::string_view expected[] = {
        kSelBackfillPayloadMalformed,
        kSelBackfillPayloadVersionUnsupported,
        kSelBackfillInputInvalid,
        kSelBackfillUnknownDevice,
        kSelBackfillMountMismatch,
        kSelBackfillDataInsufficient,
        kSelBackfillRangeInvalid,
        kSelBackfillLockRefMismatch,
        kSelBackfillSynthesisAssertFailed,
    };
    for (std::size_t i = 0; i < 9; ++i) {
        EXPECT_EQ(t09[i]->code, expected[i])
            << "T09 批第 " << i << " 行码值漂移（登记契约序——禁重排）";
        // 出处列指向单元卡 §12（回填章）。
        EXPECT_NE(t09[i]->sourceClause.find("§12"), std::string_view::npos)
            << "T09 批第 " << i << " 行出处未锚定 §12: " << t09[i]->sourceClause;
        // 语义列非空（登记三列齐备——NFR-MNT-03）。
        EXPECT_FALSE(t09[i]->semantics.empty());
    }
    // 码值句法权威校验（core::DiagnosticRecord::make 的 C-3 句法门——
    // 以最小合法记录构造；抛错即句法违约）。
    for (const std::string_view code : expected) {
        EXPECT_NO_THROW((void)core::DiagnosticRecord::make(
            std::string(code), std::nullopt, std::nullopt, std::nullopt,
            "句法校验", "契约测试", "无"))
            << "SEL-BACKFILL 码句法违约: " << code;
    }
    // 既有行不重排：第 45 行仍为 T06 批尾（user-preference-filtered）。
    EXPECT_EQ(entries[44].code, kSelUserPreferenceFiltered);
}

// =====================================================================
// 记录对象编解码契约（确定性/严格解码/版本演进面——CON-05/CON-04）
// =====================================================================

/**
 * 记录对象契约：同输入恒同字节（CON-05 内容寻址的前提——记录对象的
 * ContentVersion 由 project 对字节计算，字节漂移＝身份漂移）；严格解码
 * （残余拒绝——半成品/篡改字节不产生值面）；版本演进面（codec 版本
 * 字段翻转 → UnsupportedVersion——NFR-DEP-04 不带静默兼容读）。
 */
TEST(SelBackfillContract, RecordObjectCodecDeterminismAndStrictness)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05", "CON-04", "NFR-DEP-04"},
                  std::vector<std::string>{});

    // 构造最小合法记录（单轴；合成结果由内核产出——与载荷同源）。
    const AxisBackfillEntry axis = makeContractEntry();
    const SynthesisOutcome syn = synthesizeAxisBodyProperties(axis);
    ASSERT_TRUE(syn.ok);
    BackfillRecordObject record;
    record.referenceFrameToken = std::string(kBackfillFrameLink);
    record.recalc = BackfillRecalcNotice{};
    record.synthesis = {syn.synthesis};
    record.axes = {axis};

    // 确定性（两次编码逐字节相等）。
    const std::vector<std::uint8_t> bytes = encodeBackfillRecordObject(record);
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes, encodeBackfillRecordObject(record));

    // 往返保真。
    const DecodedBackfillRecord back = decodeBackfillRecordObject(bytes);
    ASSERT_EQ(back.status, DecodedBackfillRecord::Status::Ok);
    EXPECT_EQ(back.record, record);

    // 严格解码：尾部残余拒绝。
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0x00);
    EXPECT_EQ(decodeBackfillRecordObject(trailing).status,
              DecodedBackfillRecord::Status::Malformed);

    // 版本演进面：codec 版本字节翻转 → UnsupportedVersion（升级指引轨）。
    std::vector<std::uint8_t> foreign = bytes;
    foreign[11] = static_cast<std::uint8_t>(foreign[11] + 1);
    EXPECT_EQ(decodeBackfillRecordObject(foreign).status,
              DecodedBackfillRecord::Status::UnsupportedVersion);
}

/**
 * AT-30 复算提示契约（记录面）：默认提示恒四域全量＋retainPriorConclusion
 * 恒 false（"复核完成前不沿用原通过结论"——记录对象编码/解码保真该
 * 语义；当前性推进权在 evidence，本对象零改写）。
 */
TEST(SelBackfillContract, RecalcNoticeFourDomainsAndNoPriorConclusion_AT30)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{"AT-30"});

    // 默认值契约（组装面/计划面统一取默认——单一来源）。
    const BackfillRecalcNotice notice{};
    for (std::size_t i = 0; i < kRecalcDomainCount; ++i) {
        EXPECT_TRUE(notice.domains[i]) << "复算域缺失 index=" << i;
    }
    EXPECT_FALSE(notice.retainPriorConclusion);

    // 域 token 词表封闭性（四域 token 非空且互异——报告机器判读面）。
    std::vector<std::string_view> tokens;
    for (std::size_t i = 0; i < kRecalcDomainCount; ++i) {
        const std::string_view t =
            recalcDomainToken(static_cast<RecalcDomain>(i));
        ASSERT_FALSE(t.empty());
        tokens.push_back(t);
    }
    EXPECT_EQ(tokens[0], "kinematics");
    EXPECT_EQ(tokens[1], "dynamics");
    EXPECT_EQ(tokens[2], "selection");
    EXPECT_EQ(tokens[3], "optimization");
}

/**
 * 壳体/转子分轨记录契约（SEL-10/MDL-16——不重复计入的记录面证明）：
 * 记录对象的轴段完整携带转子惯量独立字段，而合成结果段与"转子变化
 * 无关"（合成内核黄金面已证——本契约钉记录协议字段的分轨形态：
 * synthesis 段与 axes 段转子字段同值透传，两个物理量不混写一字段）。
 */
TEST(SelBackfillContract, RecordKeepsRotorFieldIndependent_SEL10)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10", "MDL-16"},
                  std::vector<std::string>{});

    AxisBackfillEntry axis = makeContractEntry();
    axis.rotorInertiaKgM2 = 0.0123;  // 任意非默认值
    const SynthesisOutcome syn = synthesizeAxisBodyProperties(axis);
    ASSERT_TRUE(syn.ok);

    BackfillRecordObject record;
    record.referenceFrameToken = std::string(kBackfillFrameLink);
    record.recalc = BackfillRecalcNotice{};
    record.synthesis = {syn.synthesis};
    record.axes = {axis};

    const DecodedBackfillRecord back =
        decodeBackfillRecordObject(encodeBackfillRecordObject(record));
    ASSERT_EQ(back.status, DecodedBackfillRecord::Status::Ok);
    // 轴段独立字段保真（记录面）。
    EXPECT_DOUBLE_EQ(back.record.axes.front().rotorInertiaKgM2, 0.0123);
    // 合成段独立字段同值透传（未参与合成、未丢失）。
    EXPECT_DOUBLE_EQ(back.record.synthesis.front().rotorInertiaKgM2, 0.0123);
}
