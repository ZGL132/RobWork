/**
 * @file   CatalogImportTest.cpp
 * @brief  目录包导入校验用例组（SelCatalogImport）——v1 合法包导入/装配、
 *         错误字段逐码定位、唯一性/范围/引用/兼容/曲线校验族（AT-08
 *         "目录导入/错误字段"用例面；"版本锁定"面见 CatalogLockTest）。
 *
 * 设计依据：
 *   - units/selection.md §5.3（业务校验清单八码——逐用例对应表行）、
 *     §6.3（曲线校验四码）、§6.4（同 quantity 歧义/固定额定值口径）、
 *     §15.2 V1 组（目录导入与数据安全故障注入——本组覆盖其 selection
 *     业务层子集；io 文件层〔公式字符串/路径穿越/超预算〕用例归 io 卡
 *     V29 已落位面——分工不越界，卡 §5.1）、§14.0（错误二分——schema
 *     级抛出 vs 行级报告）
 *   - 需求 SEL-01/SEL-02、AT-08、ERR-01（比较型三要素）、NFR-COR-01/02/03
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 1/3
 *
 * 断言纪律：逐码用例先断言报告含目标码，再断言定位三要素（file/rowNo/
 * column）与比较型字段（actual/expected/unit）——ERR-01 全字段验证；
 * 合法基线来自 CatalogTestSupport.hpp（与 v1 列契约同步的模板形态）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>

#include "CatalogTestSupport.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;

namespace {

/// 逐码断言：报告含该码，且定位三要素匹配（无定位期望传空串/0）。
void expectIssue(const CatalogValidationReport& rep, const std::string& code,
                 const std::string& file, std::uint64_t rowNo, const std::string& column)
{
    const std::optional<CatalogIssue> iss = findByCode(rep, code);
    ASSERT_TRUE(iss.has_value()) << "报告缺少码 " << code << "（issues=" << rep.issues.size()
                                 << "）";
    EXPECT_EQ(iss->file, file);
    if (rowNo != 0) {
        EXPECT_EQ(iss->rowNo, rowNo);
    }
    if (!column.empty()) {
        EXPECT_EQ(iss->column, column);
    }
}

}  // namespace

// =====================================================================
// 合法包导入与装配（SEL-01 正例）
// =====================================================================

/**
 * 合法 v1 包全链（SEL-01/SEL-02 正例＋AT-08 导入面）：校验通过→装配出
 * 四表快照→字段值/缺失清单/条目状态逐项抽查——目录包模板（清单/主表/
 * 曲线表/兼容表＋字段字典/版本/来源）的可执行证明。
 */
TEST(SelCatalogImport, ValidPackageValidatesAndAssembles_AT08_SEL01)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-01", "SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    const ParsedCatalogInput input = makeBaselineInput();
    const CatalogManifest manifest = makeBaselineManifest();

    const CatalogValidationReport rep = importer.validate(input, manifest);
    // 合法基线包零发现（逐条打印辅助定位——失败时的诊断输出面）。
    for (const CatalogIssue& iss : rep.issues) {
        ADD_FAILURE() << "基线包意外校验发现: " << iss.code << " @ " << iss.file << ":"
                      << iss.rowNo << " col=" << iss.column << " — " << iss.message;
    }
    EXPECT_TRUE(rep.ok());

    // 装配：四表条目数与确定性排序（modelId 升序——"G-120"<"G-50" 字典序）。
    const CatalogPackageSnapshot snap = importer.assemble(input, manifest);
    ASSERT_EQ(snap.motors.size(), 2U);
    ASSERT_EQ(snap.gearboxes.size(), 2U);
    ASSERT_EQ(snap.curves.size(), 2U);
    ASSERT_EQ(snap.compatibility.size(), 2U);
    EXPECT_LT(snap.motors[0].modelId, snap.motors[1].modelId);
    EXPECT_LT(snap.gearboxes[0].modelId, snap.gearboxes[1].modelId);
    EXPECT_LT(snap.curves[0].curveId, snap.curves[1].curveId);
    // 按稳定 ID 取条目（显示名/表序不作为定位键——卡 §4.3 纪律）。
    const auto findMotor = [&](const std::string& id) -> const MotorCatalogEntry& {
        for (const MotorCatalogEntry& m : snap.motors) {
            if (m.modelId == id) { return m; }
        }
        ADD_FAILURE() << "电机缺失: " << id;
        static const MotorCatalogEntry kEmpty;
        return kEmpty;
    };
    const auto findGearbox = [&](const std::string& id) -> const GearboxCatalogEntry& {
        for (const GearboxCatalogEntry& g : snap.gearboxes) {
            if (g.modelId == id) { return g; }
        }
        ADD_FAILURE() << "减速器缺失: " << id;
        static const GearboxCatalogEntry kEmpty;
        return kEmpty;
    };

    // 电机字段抽查（SI 域数值——卡 §4.4 单位表；N·m/rad/s/W/kg·m²/kg）。
    const MotorCatalogEntry& m100 = findMotor("M-100");
    EXPECT_EQ(m100.modelId, "M-100");
    EXPECT_DOUBLE_EQ(m100.ratedTorque, 4.5);   // N·m
    EXPECT_DOUBLE_EQ(m100.peakTorque, 11.0);   // N·m
    EXPECT_DOUBLE_EQ(m100.ratedSpeed, 150.0);  // rad/s
    EXPECT_DOUBLE_EQ(m100.rotorInertia, 0.012); // kg·m²
    EXPECT_EQ(m100.dutyClass, "S1");
    // M-100 的可缺失列留空→missing 清单显式标记（ERR-01 不伪造）＋Partial。
    EXPECT_EQ(m100.status, ValidationStatus::Partial);
    ASSERT_EQ(m100.missing.size(), 7U);  // overload 两列＋voltage＋thermal 两列＋brake＋holding
    EXPECT_EQ(m100.missing.front().reason, "cell-empty");
    // 曲线引用随 curve_ref 列映射（owner/量纲来自曲线组声明）。
    ASSERT_EQ(m100.curves.size(), 1U);
    EXPECT_EQ(m100.curves[0].curveId, "curve-tq");
    EXPECT_EQ(m100.curves[0].xQuantity, "speed");
    EXPECT_EQ(m100.curves[0].yQuantity, "torque");

    // 全填条目＝Valid（无缺失）。
    EXPECT_EQ(findMotor("M-200").status, ValidationStatus::Valid);
    // 减速器抽查（效率∈(0,1]、速比>0——卡 §4.1）。
    const GearboxCatalogEntry& g120 = findGearbox("G-120");
    EXPECT_DOUBLE_EQ(g120.efficiency, 0.94);   // 无量纲
    EXPECT_DOUBLE_EQ(g120.ratio, 100.0);       // 无量纲
    ASSERT_TRUE(g120.extLoad.has_value());
    EXPECT_DOUBLE_EQ(g120.extLoad->radial, 1200.0);  // N
    EXPECT_EQ(g120.status, ValidationStatus::Valid);

    // 版本与来源进入快照身份（SEL-01——目录版本和来源必须进入输入身份）。
    EXPECT_EQ(snap.manifest.identity.catalogId, "cat-demo");
    EXPECT_EQ(snap.manifest.identity.version, "v1");
    EXPECT_TRUE(snap.manifest.identity.contentIdentity.isValid());
    EXPECT_EQ(snap.manifest.identity.contentIdentity, snap.contentIdentity);
}

/**
 * 装配确定性（NFR-COR-01/02）：同输入两次 assemble 产出全等快照（含
 * 内容身份）——稳定序列化与确定性排序的执行证明。
 */
TEST(SelCatalogImport, AssembleIsDeterministicSameInputSameSnapshot)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    const CatalogPackageSnapshot a = importer.assemble(makeBaselineInput(),
                                                       makeBaselineManifest());
    const CatalogPackageSnapshot b = importer.assemble(makeBaselineInput(),
                                                       makeBaselineManifest());
    EXPECT_EQ(a, b);
    EXPECT_EQ(canonicalPackageText(a), canonicalPackageText(b));
}

/**
 * 内容寻址（CON-05/卡 §4.2）：快照任一业务字节变更→内容身份变更
 * （依赖该目录的切片失效判据）。
 */
TEST(SelCatalogImport, ContentIdentityChangesWithBusinessByte)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    const CatalogPackageSnapshot base = importer.assemble(makeBaselineInput(),
                                                          makeBaselineManifest());

    // 变异：电机额定转矩 4.5→4.6 N·m（单字节级业务变更）。
    ParsedCatalogInput mutated = makeBaselineInput();
    ASSERT_TRUE(editRow(*mutated.files.begin(), "model_id", "M-100", "rated_torque_nm",
                        "4.6"));
    const CatalogPackageSnapshot changed = importer.assemble(mutated, makeBaselineManifest());

    EXPECT_NE(base.contentIdentity, changed.contentIdentity);
    EXPECT_NE(canonicalPackageText(base), canonicalPackageText(changed));
}

// =====================================================================
// schema 级拒绝（§14.2 note——致命结构错误 fail-fast）
// =====================================================================

/** 未知 formatVersion：拒绝导入＋升级指引（不自动升级——卡 §5.2/PM-06）。 */
TEST(SelCatalogImport, UnknownFormatVersionRejectedWithUpgradeGuidance)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-01", "SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    CatalogManifest manifest = makeBaselineManifest();
    manifest.formatVersion = "999";

    EXPECT_THROW(static_cast<void>(importer.validate(makeBaselineInput(), manifest)),
                 std::invalid_argument);
    // 升级指引随异常消息（V1"未知格式"的拒绝面——不进入行级报告）。
    try {
        static_cast<void>(importer.validate(makeBaselineInput(), manifest));
        FAIL() << "应抛出 schema 级拒绝";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("999"), std::string::npos);
    }
}

/** 身份不完整（catalogId/version 缺失）：schema 级拒绝（卡 §4.3 三元组前提）。 */
TEST(SelCatalogImport, MissingManifestIdentityRejected_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-01"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    CatalogManifest noId = makeBaselineManifest();
    noId.identity.catalogId.clear();
    noId.identity.version.clear();
    EXPECT_THROW(static_cast<void>(importer.validate(makeBaselineInput(), noId)),
                 std::invalid_argument);
}

/** 来源缺失：清单级可定位拒绝（SEL-01 来源信息——V1 注入项定位面）。 */
TEST(SelCatalogImport, MissingSourceLocatedAtManifest_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-01"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    CatalogManifest noSource = makeBaselineManifest();
    noSource.identity.source.clear();
    const CatalogValidationReport rep = importer.validate(makeBaselineInput(), noSource);
    EXPECT_FALSE(rep.ok());
    expectIssue(rep, std::string{kSelCatalogFieldMissing}, kCatalogFileManifest, 0, "source");
}

/** 必备解析表缺失：调用方装配违约（io 文件层前置——selection 不越界重复
 *  文件层校验，仅对输入完整性前置断言）。 */
TEST(SelCatalogImport, MissingParsedTableIsCallerContractBreach)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput incomplete = makeBaselineInput();
    incomplete.files.pop_back();   // 去掉 compatibility 表（io 应已拦）
    EXPECT_THROW(static_cast<void>(importer.validate(incomplete, makeBaselineManifest())),
                 std::invalid_argument);
}

/** 未校验即装配：调用方契约违约（先 validate 后 assemble——卡 §5.1 流程）。 */
TEST(SelCatalogImport, AssembleRequiresValidatedInput)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput bad = makeBaselineInput();
    ASSERT_TRUE(editRow(*bad.files.begin(), "model_id", "M-100", "rated_torque_nm", "-1"));
    EXPECT_THROW(static_cast<void>(importer.assemble(bad, makeBaselineManifest())),
                 std::invalid_argument);
}

// =====================================================================
// schema 列比对（§5.3 行 1——SEL-CATALOG-SCHEMA-MISMATCH）
// =====================================================================

/** 表头缺列：逐列定位（期望列名随 issue 携带）。 */
TEST(SelCatalogImport, HeaderMissingColumnLocated)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& motors = *input.files.begin();
    // 从表头去掉 rated_speed 列（列位 5）——行数据保持 row-major 等长破坏
    // 由测试构造端同步处理：这里直接重建缺列表（数据行同删该列）。
    std::vector<std::string> header = motorHeader();
    const std::size_t col = 5;
    const std::string dropped = header[col];
    header.erase(header.begin() + static_cast<std::ptrdiff_t>(col));
    std::vector<std::vector<std::string>> rows;
    for (std::size_t r = 0; r < rowCount(motors); ++r) {
        std::vector<std::string> row;
        for (std::size_t c = 0; c < motors.columnCount; ++c) {
            if (c != col) {
                row.push_back(cell(motors, r, motors.header[c]));
            }
        }
        rows.push_back(std::move(row));
    }
    motors = makeTable(kCatalogFileMotors, header, rows);

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    EXPECT_FALSE(rep.ok());
    // 缺列定位形态：首个错位处 issue.column＝表头错位列名（"max_speed"），
    // message 携带字典期望列名（被删列 "rated_speed"）——断言二者同时
    // 出现（卡 §5.3"多列/缺列逐列定位"）。
    bool firstMismatchLocated = false;
    bool droppedNamed = false;
    for (const CatalogIssue& iss : rep.issues) {
        if (iss.code != std::string{kSelCatalogSchemaMismatch}
            || iss.file != std::string{kCatalogFileMotors}) {
            continue;
        }
        if (iss.column == "max_speed") {
            firstMismatchLocated = true;   // 首个错位（删除位 5 的后继列）
        }
        if (iss.message.find(dropped) != std::string::npos) {
            droppedNamed = true;           // 期望列名随消息定位
        }
    }
    EXPECT_TRUE(firstMismatchLocated) << "首个错位未定位到 max_speed";
    EXPECT_TRUE(droppedNamed) << "被删列 " << dropped << " 未随消息定位";
}

/** 表头多列：定位多余列名（字典声明集外）。 */
TEST(SelCatalogImport, HeaderExtraColumnLocated)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& compat = input.files.back();
    compat.header.push_back("extra_col");
    compat.columnCount += 1;
    for (std::size_t r = 0; r < rowCount(compat); ++r) {
        compat.cells.push_back("");   // 多列单元格补空（row-major 等长）
    }

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogSchemaMismatch});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileCompatibility);
    EXPECT_EQ(iss->column, "extra_col");
}

/** 字典与 v1 契约不符（单位声明错）：manifest 定位（字典权威＝selection
 *  v1 契约——业务 schema 归 selection）。 */
TEST(SelCatalogImport, DictionaryDeviationFromV1ContractLocated)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-01", "SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    CatalogManifest manifest = makeBaselineManifest();
    manifest.fieldDictionary[0].fields[3].unit = "kg";   // rated_torque_nm 声明成 kg

    const CatalogValidationReport rep = importer.validate(makeBaselineInput(), manifest);
    EXPECT_FALSE(rep.ok());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogSchemaMismatch});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileManifest);
}

// =====================================================================
// 单位/必填/唯一性（§5.3 行 2~4）
// =====================================================================

/** 未知单位拒绝（曲线表行级 x_unit＝rpm——core 词表外，拒绝不猜测）。 */
TEST(SelCatalogImport, UnknownUnitRejectedNotGuessed)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02", "NFR-COR-03"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    ASSERT_TRUE(editRow(curves, "curve_id", "curve-tq", "x_unit", "rpm"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogUnitInvalid});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileCurves);
    EXPECT_EQ(iss->actualText, "rpm");   // 比较型：实际单位（期望侧＝量纲 SI 本位）
}

/** 曲线单位与量纲不符（speed 配 N*m）：UNIT-INVALID（期望＝rad/s）。 */
TEST(SelCatalogImport, CurveUnitDimensionMismatchLocated)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    ASSERT_TRUE(editRow(curves, "curve_id", "curve-tq", "x_unit", "N*m"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogUnitInvalid});
    ASSERT_TRUE(iss.has_value());
    ASSERT_TRUE(iss->expectedText.has_value());
    EXPECT_EQ(*iss->expectedText, "rad/s");
}

/** 必填字段缺失：逐字段定位（电机行 rated_torque_nm 空——行 3）。 */
TEST(SelCatalogImport, RequiredFieldMissingLocatedByRowAndColumn_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& motors = *input.files.begin();
    ASSERT_TRUE(editRow(motors, "model_id", "M-100", "rated_torque_nm", "  "));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    expectIssue(rep, std::string{kSelCatalogFieldMissing}, kCatalogFileMotors, 3,
                "rated_torque_nm");
}

/** 可缺失字段缺失：条目 missing 清单显式标记（不伪造数值——ERR-01）；
 *  与必填缺失分轨（本基线 M-100 已验证——此处断言清单列名逐项对位）。 */
TEST(SelCatalogImport, OptionalMissingGoesToEntryMissingList)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02", "ERR-01"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    const CatalogPackageSnapshot snap = importer.assemble(makeBaselineInput(),
                                                          makeBaselineManifest());
    const MotorCatalogEntry& m100 = snap.motors[0];
    std::vector<std::string> missingCols;
    for (const MissingField& mf : m100.missing) {
        missingCols.push_back(mf.column);
    }
    // 基线 M-100 缺 7 可选列：overload 两列＋voltage＋thermal 两列＋brake
    // ＋holding（mounting 两列非空、curve_ref 有引用——不入清单）。
    for (const char* col : {"overload_torque_nm", "overload_duration_s", "rated_voltage_v",
                            "thermal_ref_temp", "thermal_factor_per_ref", "brake_torque_nm",
                            "holding_torque_nm"}) {
        EXPECT_NE(std::find(missingCols.begin(), missingCols.end(), std::string(col)),
                  missingCols.end())
            << "可缺失列未入 missing 清单: " << col;
    }
    EXPECT_EQ(m100.missing.size(), 7U);
}

/** 稳定 ID 重复（同 ID 不同内容）：SEL-CATALOG-DUPLICATE-ID（主键冲突）。 */
TEST(SelCatalogImport, DuplicateIdConflictDetected_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& motors = *input.files.begin();
    std::vector<std::string> row = cellRowOf(motors, 0);
    row[0] = "M-100";   // 与首行同 ID
    row[2] = "ST-100B"; // 显示名不同→内容不同
    appendRow(motors, row);

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogDuplicateId});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->modelId, "M-100");
    EXPECT_EQ(iss->rowNo, 5U);   // 追加行＝数据行 2（物理行 3 起＋2）
    EXPECT_FALSE(findByCode(rep, std::string{kSelCatalogDuplicateModel}).has_value());
}

/** 重复行（同 ID 同内容）：SEL-CATALOG-DUPLICATE-MODEL（登记 1 两码分配）。 */
TEST(SelCatalogImport, DuplicateIdenticalRowDetected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& motors = *input.files.begin();
    appendRow(motors, cellRowOf(motors, 0));   // 整行复制 M-100

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogDuplicateModel});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->modelId, "M-100");
    EXPECT_FALSE(findByCode(rep, std::string{kSelCatalogDuplicateId}).has_value());
}

/** 显示名重复但 ID 不同＝合法（卡 §5.3 验证矩阵登记行——不发码）。 */
TEST(SelCatalogImport, SameDisplayNameDifferentIdIsLegal)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& motors = *input.files.begin();
    ASSERT_TRUE(editRow(motors, "model_id", "M-200", "display_name", "ST-100")); // 与 M-100 同名

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    EXPECT_TRUE(rep.ok()) << "显示名重复不构成校验发现（卡 §5.3）";
}

// =====================================================================
// 数值范围（§5.3 行 5——SEL-CATALOG-RANGE-INVALID 比较型三要素）
// =====================================================================

/** 效率越上界（1.5∉(0,1]）：比较型 actual/expected/unit 齐备（ERR-01）。 */
TEST(SelCatalogImport, EfficiencyAboveOneReportedWithComparison)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02", "ERR-01"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ASSERT_TRUE(editRow(input.files[1], "model_id", "G-120", "efficiency", "1.5"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRangeInvalid});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileGearboxes);
    EXPECT_EQ(iss->column, "efficiency");
    ASSERT_TRUE(iss->actualValue.has_value());
    EXPECT_DOUBLE_EQ(*iss->actualValue, 1.5);
    ASSERT_TRUE(iss->expectedValue.has_value());
    EXPECT_DOUBLE_EQ(*iss->expectedValue, 1.0);
    EXPECT_EQ(iss->unit, "1");
}

/** 峰值转矩低于额定（交叉范围）：RANGE-INVALID（peak>=rated）。 */
TEST(SelCatalogImport, PeakBelowRatedReported)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ASSERT_TRUE(editRow(*input.files.begin(), "model_id", "M-100", "peak_torque_nm", "3"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRangeInvalid});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->column, "peak_torque_nm");
}

/** 非有限数值（nan 文本）：同路径拒绝＋原文保留（NFR-COR-03——不静默转 0）。 */
TEST(SelCatalogImport, NonFiniteValueRejectedWithRawText)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ASSERT_TRUE(editRow(*input.files.begin(), "model_id", "M-100", "rated_torque_nm", "nan"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRangeInvalid});
    ASSERT_TRUE(iss.has_value());
    ASSERT_TRUE(iss->actualText.has_value());
    EXPECT_EQ(*iss->actualText, "nan");
}

/** 非数字文本（"4,5"）：数值合法性同码族（登记 2——actualText 原文）。 */
TEST(SelCatalogImport, UnparseableNumberRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02", "NFR-COR-03"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ASSERT_TRUE(editRow(*input.files.begin(), "model_id", "M-100", "mass_kg", "5,5"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRangeInvalid});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->column, "mass_kg");
    ASSERT_TRUE(iss->actualText.has_value());
    EXPECT_EQ(*iss->actualText, "5,5");
}

// =====================================================================
// 文件间引用语义与兼容冲突（§5.3 行 6/7）
// =====================================================================

/** curve_ref 悬空（引用不存在的曲线）：REF-DANGLING 定位到引用单元格。 */
TEST(SelCatalogImport, DanglingCurveRefLocated_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ASSERT_TRUE(editRow(*input.files.begin(), "model_id", "M-100", "curve_ref", "ghost"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRefDangling});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileMotors);
    EXPECT_EQ(iss->column, "curve_ref");
    EXPECT_EQ(iss->modelId, "M-100");
    ASSERT_TRUE(iss->actualText.has_value());
    EXPECT_EQ(*iss->actualText, "ghost");
}

/** 曲线 owner 不匹配（M-100 引用减速器曲线）：REF-DANGLING（语义层归
 *  selection——io 只做文件层存在性，卡 §5.3 分工）。 */
TEST(SelCatalogImport, CurveOwnerMismatchDetected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ASSERT_TRUE(editRow(*input.files.begin(), "model_id", "M-100", "curve_ref", "curve-eff"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRefDangling});
    ASSERT_TRUE(iss.has_value());
    ASSERT_TRUE(iss->expectedText.has_value());
    EXPECT_EQ(*iss->expectedText, "motor/M-100");
}

/** 曲线 owner 型号不存在（owner_model_id=ghost）：REF-DANGLING。 */
TEST(SelCatalogImport, CurveOwnerModelDanglingDetected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    ASSERT_TRUE(editRow(curves, "curve_id", "curve-tq", "owner_model_id", "ghost"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRefDangling});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileCurves);
    EXPECT_EQ(iss->column, "owner_model_id");
}

/** owner_kind 词表违约（owner=robot）：REF-DANGLING（登记 3——无法归属）。 */
TEST(SelCatalogImport, CurveOwnerKindVocabularyRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    ASSERT_TRUE(editRow(curves, "curve_id", "curve-tq", "owner_kind", "robot"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRefDangling});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->column, "owner_kind");
    ASSERT_TRUE(iss->expectedText.has_value());
    EXPECT_EQ(*iss->expectedText, "motor|gearbox");
}

/** 量纲 token 词表违约（x_quantity=altitude）：SCHEMA-MISMATCH（词表归
 *  字段字典——卡 §6.1，登记 3）。 */
TEST(SelCatalogImport, CurveQuantityVocabularyRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    ASSERT_TRUE(editRow(curves, "curve_id", "curve-tq", "x_quantity", "altitude"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogSchemaMismatch});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileCurves);
    EXPECT_EQ(iss->column, "x_quantity");
}

/** 同条目引用同量纲用途两条曲线：SCHEMA-MISMATCH（§6.4 歧义，登记 4）。 */
TEST(SelCatalogImport, SameQuantityTwoCurvesAmbiguous)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    // 复制 curve-tq 为 curve-tq2（同 owner/量纲，点值错开——本身合法）。
    ParsedFileTable& curves = input.files[2];
    const std::size_t base = rowCount(curves);
    for (std::size_t r = 0; r < 3; ++r) {
        std::vector<std::string> row;
        for (std::size_t c = 0; c < curves.columnCount; ++c) {
            std::string v = cell(curves, r, curves.header[c]);
            if (c == 0) { v = "curve-tq2"; }
            row.push_back(v);
        }
        appendRow(curves, row);
    }
    // M-100 引用两条同量纲曲线。
    ASSERT_TRUE(editRow(*input.files.begin(), "model_id", "M-100", "curve_ref",
                        "curve-tq;curve-tq2"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogSchemaMismatch});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileMotors);
    EXPECT_EQ(iss->column, "curve_ref");
    EXPECT_NE(iss->message.find("歧义"), std::string::npos);
}

/** 兼容关系引用悬空（引用不存在型号）：REF-DANGLING。 */
TEST(SelCatalogImport, CompatReferenceDanglingDetected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ASSERT_TRUE(editRow(input.files[3], "motor_model_id", "M-100", "motor_model_id",
                        "ghost"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogRefDangling});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileCompatibility);
}

/** 同型号对 mount_kind 矛盾：SEL-CATALOG-COMPAT-CONFLICT（比较型：实际/
 *  首声明）。 */
TEST(SelCatalogImport, CompatMountKindConflictDetected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    appendRow(input.files[3], {"M-100", "G-50", "shaft-mount"});   // 与首行矛盾

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep,
                                                       std::string{kSelCatalogCompatConflict});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileCompatibility);
    ASSERT_TRUE(iss->actualText.has_value());
    EXPECT_EQ(*iss->actualText, "shaft-mount");
    ASSERT_TRUE(iss->expectedText.has_value());
    EXPECT_EQ(*iss->expectedText, "flange-mount");
}

/** 兼容表零行＝无预声明兼容对（合法——卡 §5.2 零行语义）。 */
TEST(SelCatalogImport, EmptyCompatTableIsLegalNoDeclaredPairs)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-01"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    input.files.back() = makeTable(kCatalogFileCompatibility, compatHeader(), {});

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    EXPECT_TRUE(rep.ok());
    const CatalogPackageSnapshot snap = importer.assemble(input, makeBaselineManifest());
    EXPECT_TRUE(snap.compatibility.empty());   // 组合校核按"无记录即不兼容"消费
}

// =====================================================================
// 曲线校验族（§6.3 四码）
// =====================================================================

/** 点序下降：SEL-CURVE-UNORDERED（不代排序——拒绝要求目录修正）。 */
TEST(SelCatalogImport, CurveUnorderedRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    // 把 curve-tq 第 3 点（point_index=2，x=300）改为 10——提交序变为
    // 50→150→10，出现下降（editRow 按 point_index 键定位该组内唯一行）。
    ASSERT_TRUE(editRow(curves, "point_index", "2", "x_value", "10"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep, std::string{kSelCurveUnordered});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileCurves);
}

/** 重复横坐标：SEL-CURVE-DUP-X（插值语义歧义）。 */
TEST(SelCatalogImport, CurveDuplicateXRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    ASSERT_TRUE(editRow(curves, "curve_id", "curve-tq", "x_value", "150")); // 第 2 点 x=150

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep, std::string{kSelCurveDupX});
    ASSERT_TRUE(iss.has_value());
}

/** 非有限点（y=inf）：SEL-CURVE-NONFINITE（NFR-COR-03）。 */
TEST(SelCatalogImport, CurveNonfinitePointRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    ASSERT_TRUE(editRow(curves, "curve_id", "curve-tq", "y_value", "inf"));

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss = findByCode(rep, std::string{kSelCurveNonfinite});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->file, kCatalogFileCurves);
}

/** 单点曲线声明为曲线：SEL-CURVE-INTERVAL-INVALID（固定额定值口径——§6.4）。 */
TEST(SelCatalogImport, SinglePointCurveRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ParsedFileTable& curves = input.files[2];
    // 只留 curve-tq 的第一点（单点）。
    std::vector<std::vector<std::string>> kept;
    for (std::size_t r = 0; r < rowCount(curves); ++r) {
        if (cell(curves, r, "curve_id") == "curve-eff"
            || cell(curves, r, "point_index") == "0") {
            std::vector<std::string> row;
            for (std::size_t c = 0; c < curves.columnCount; ++c) {
                row.push_back(cell(curves, r, curves.header[c]));
            }
            kept.push_back(std::move(row));
        }
    }
    curves = makeTable(kCatalogFileCurves, curveHeader(), kept);

    const CatalogValidationReport rep = importer.validate(input, makeBaselineManifest());
    const std::optional<CatalogIssue> iss =
        findByCode(rep, std::string{kSelCurveIntervalInvalid});
    ASSERT_TRUE(iss.has_value());
    EXPECT_EQ(iss->modelId, "curve-tq");
}

// =====================================================================
// 校验纯函数性（§14.2 note——NFR-COR-01/02）
// =====================================================================

/** 同输入恒同报告（内容与顺序全等）——校验器确定性。 */
TEST(SelCatalogImport, ValidateIsPureSameInputSameReport)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput input = makeBaselineInput();
    ASSERT_TRUE(editRow(*input.files.begin(), "model_id", "M-100", "peak_torque_nm", "2"));
    const CatalogManifest manifest = makeBaselineManifest();

    const CatalogValidationReport a = importer.validate(input, manifest);
    const CatalogValidationReport b = importer.validate(input, manifest);
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a.ok());
}

// =====================================================================
// 曲线构造入口直测（tryMakePerformanceCurve——§6.1/§6.3 独立面）
// =====================================================================

/** 构造入口合法路径：成功产出曲线＋点集内容身份有效（CON-05）。 */
TEST(SelCatalogImport, MakeCurveHappyPathProducesIdentity)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05", "SEL-01"},
                  std::vector<std::string>{});

    const CatalogIdentity id{"cat-demo", "v1", {}, "demo"};
    CatalogIssue reject;
    const std::optional<PerformanceCurve> curve = tryMakePerformanceCurve(
        "c1", kQuantityTorque, kQuantitySpeed, "N*m", "rad/s",
        {{10.0, 50.0}, {20.0, 45.0}}, id, reject);
    ASSERT_TRUE(curve.has_value());
    EXPECT_EQ(curve->curveId, "c1");
    EXPECT_TRUE(curve->contentIdentity.isValid());
    EXPECT_TRUE(curve->catalog == id);
}

/** 构造入口单位未注册：UNIT-INVALID（实际单位随 reject 携带）。 */
TEST(SelCatalogImport, MakeCurveRejectsUnregisteredUnit)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogIdentity id{"cat-demo", "v1", {}, "demo"};
    CatalogIssue reject;
    const std::optional<PerformanceCurve> curve = tryMakePerformanceCurve(
        "c1", kQuantitySpeed, kQuantityTorque, "rpm", "N*m", {{10, 1}, {20, 2}}, id, reject);
    EXPECT_FALSE(curve.has_value());
    EXPECT_EQ(reject.code, std::string(kSelCatalogUnitInvalid));
    ASSERT_TRUE(reject.actualText.has_value());
    EXPECT_EQ(*reject.actualText, "rpm");
}

/** 构造入口点序下降：UNORDERED 且不代排序（§6.3 行 1——拒绝而非修正）。 */
TEST(SelCatalogImport, MakeCurveDoesNotReorderUnorderedPoints)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogIdentity id{"cat-demo", "v1", {}, "demo"};
    CatalogIssue reject;
    const std::optional<PerformanceCurve> curve = tryMakePerformanceCurve(
        "c1", kQuantitySpeed, kQuantityTorque, "rad/s", "N*m",
        {{20.0, 1.0}, {10.0, 2.0}}, id, reject);   // 提交序下降
    EXPECT_FALSE(curve.has_value());
    EXPECT_EQ(reject.code, std::string(kSelCurveUnordered));
}
