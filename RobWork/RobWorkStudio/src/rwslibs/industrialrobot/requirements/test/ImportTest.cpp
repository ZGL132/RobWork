/**
 * @file   ImportTest.cpp
 * @brief  需求导入器用例组（ReqImport）——CSV 行级部分成功（V-08/AT-02）、
 *         字段字典自动识别与手动改映射（卡 §7.3 冻结表）、单位预览与 SI
 *         归一（V-09/NFR-COR-03）、JSON roundtrip 副本导出（V-10/AT-24）、
 *         公式按数据解析不执行（REQ-05/NFR-SEC-03）、纯函数确定性与
 *         "取消/失败＝无草稿无修订"（V-20 requirements 侧）。
 *
 * 设计依据：units/requirements.md §7.3/§7.4/§7.5/§9.5/§9.6/§10.2（V-08/
 * V-09/V-10 观测点）；任务契约 tasks/foundation/WP-14-T04.json
 * acceptance 1~6——每条 acceptance 至少一个具名用例自证。
 *
 * 用例载体口径（P-REQ-8 处置）：CSV 用例只经 io::RawTable 直构（CSV 方言
 * /编码边界规则归 io——本单元不复制，契约 note 原文）；文件系统面仅副本
 * 导出用例（testing::TempDir 隔离，io CsvTest 同款真实文件替身）。
 */

#include <sdurws/ird/requirements/Import.hpp>

#include <sdurws/ird/core/Digest.hpp>   // ContentDigester——摘要期望值自算
#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/io/AtomicFile.hpp>
#include <sdurws/ird/io/IoError.hpp>
#include <sdurws/ird/requirements/DiagCodes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <gtest/gtest.h>

#include <rw/math/Vector3D.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace sdurws::ird::requirements;

namespace core = sdurws::ird::core;
namespace io = sdurws::ird::io;

namespace {

// =====================================================================
// 夹具助手（确定性构造——无随机/时钟）
// =====================================================================

/// 直构带表头 RawTable（P-REQ-8 处置口径：CSV 用例只经 RawTable 构造）。
/// 首行＝表头，其余＝数据行；report.dataRows 与行数一致。
io::RawTable makeTable(const std::vector<std::vector<std::string>>& rows)
{
    io::RawTable table;
    if (!rows.empty()) {
        table.report.hasHeader = true;
        table.report.header.assign(rows.front().begin(), rows.front().end());
    }
    for (std::size_t i = 1; i < rows.size(); ++i) {
        table.rows.emplace_back(rows[i].begin(), rows[i].end());
    }
    table.report.dataRows = table.rows.size();
    return table;
}

/// canonical 表头全列（冻结表 20 列序——makeRow 的缺省表头）。
std::vector<std::string> canonicalHeader()
{
    std::vector<std::string> header;
    for (int f = 0; f < static_cast<int>(kImportFieldCount); ++f) {
        header.emplace_back(importFieldToken(static_cast<ImportField>(f)));
    }
    return header;
}

/// JSON 文本 → 字节向量（mapJson 输入形态）。
std::vector<std::uint8_t> jsonBytes(const std::string& text)
{
    return std::vector<std::uint8_t>{text.begin(), text.end()};
}

/// 诊断清单中是否含指定码。
bool hasDiagCode(const std::vector<core::DiagnosticRecord>& diags, std::string_view code)
{
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

/// 行级错误清单中是否含指定码。
bool hasRowErrorCode(const ImportOutcome& out, std::string_view code)
{
    return hasDiagCode(out.rowErrors, code);
}

/// 行级错误 context 是否携带 AT-02 定位三要素（row=/column=/raw=）。
bool contextCarriesTriple(const core::DiagnosticRecord& record)
{
    return record.context.find("row=") != std::string::npos
           && record.context.find("column=") != std::string::npos
           && record.context.find("raw=") != std::string::npos;
}

/// 两条导入产出是否等价（确定性断言面：条目/行错/摘要/报告全量——
/// ImportOutcome 无 operator==，此处逐成员等价判定）。
bool outcomesEquivalent(const ImportOutcome& a, const ImportOutcome& b)
{
    if (a.status != b.status || a.sourceDigest != b.sourceDigest
        || a.sourceMarkedDraft != b.sourceMarkedDraft
        || a.entries != b.entries || a.rowErrors != b.rowErrors
        || a.ignoredColumns != b.ignoredColumns
        || a.defaultedFields != b.defaultedFields) {
        return false;
    }
    return true;
}

/// 规范文本形 ObjectId（确定性字面——悬空引用用例的引用原文）。
std::string sampleObjectIdText()
{
    return "obj-000102030405060708090a0b0c0d0e0f";
}

/// 基本可导入行（canonical 列序；x/y/z=1,2,3、其余缺省——各用例在其上
/// 变异）。
std::vector<std::string> basicRow(const std::string& id = "p1",
                                  const std::string& name = "取件点")
{
    return {id, name, "Pick", "Must", "true", "World", "", "1", "2", "3",
            "", "", "", "", "", "", "", "", "", "示例备注"};
}

/// 基本可导入 JSON 文档（与 basicRow 同内容域——roundtrip 用例起点）。
std::string basicJson()
{
    return std::string{R"({
  "schemaVersion": 1,
  "points": [
    {
      "id": "p1",
      "name": "取件点",
      "process_tag": "Pick",
      "level": "Must",
      "enabled": "true",
      "ref_frame": "World",
      "x": 1.0,
      "y": 2.0,
      "z": 3.0,
      "note": "示例备注"
    }
  ]
})"};
}

/// 准备失败型原子写出器（故障注入——io AtomicFile.hpp"契约/故障注入测
/// 试经 fake 适配层替换 IAtomicFileWriter"口径；prepare 失败＝目标零接
/// 触，验证 exportCopy 失败传播与"旧文件完好"）。
class PrepareFailWriter final : public io::IAtomicFileWriter {
public:
    io::IoResult<io::AtomicTarget> prepare(const std::filesystem::path& /*target*/,
                                           io::ReplacePolicy /*policy*/) override
    {
        io::IoResult<io::AtomicTarget> failure;
        failure.error.code = io::IoErrorCode::ResLockConflict;  // IO-RES-LOCK-CONFLICT
        failure.error.detail = "ImportTest 注入：prepare 失败（锁冲突替身）";
        return failure;
    }
    io::IoResult<void> commit(io::AtomicTarget& /*target*/) override
    {
        return io::IoResult<void>{};  // 不可达（prepare 已失败）
    }
    io::IoResult<void> abort(io::AtomicTarget& /*target*/) override
    {
        return io::IoResult<void>{};  // 幂等成功（终态会话）
    }
};

/// 读文件全文（字节精确比对用）。
std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>{in},
                       std::istreambuf_iterator<char>{}};
}

/// 测试隔离路径（gtest 每用例独立 TempDir——返回 path 形态）。
std::filesystem::path tempPath(const char* name)
{
    return std::filesystem::path{testing::TempDir()} / name;
}

}  // namespace

// =====================================================================
// acceptance 1——V-08 CSV 行级部分成功（AT-02：错误行保留正确行、错误
// 定位到列与原文；重复 id/name；结构级拒绝；缺省补全；忽略项清单）
// =====================================================================

/**
 * V-08 主线：含错行文件导入→条目数＝正确行数（正确行保留），错误行逐
 * 条携带定位三要素＋原因（AT-02）。行 2 level 非法、行 3 x 非数值——两
 * 行均被丢弃且各自独立成条诊断；行 1/4 保留。
 */
TEST(ReqImport, CsvPartialSuccess_CorrectRowsRetained_RowErrorsLocated_V08_AT02)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    auto table = makeTable({canonicalHeader(),
                            basicRow("p1", "取件点"),                 // 行 1（保留）
                            basicRow("p2", "错误等级"),               // 行 2（level 非法——下方变异）
                            basicRow("p3", "错误数值"),               // 行 3（x 非数值——下方变异）
                            basicRow("p4", "放件点")});               // 行 4（保留）
    table.rows[1][3] = "Info";     // 行 2 level 词表外（合法值 Must|Should）
    table.rows[2][7] = "abc,def";  // 行 3 x 列非数值（带分隔符样例——原文回显）
    // 注意：上式 "abc,def" 为单元格内文本（RawTable 层已无分隔语义）。

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    const auto out = importer.mapCsv(table, detect.mapping,
                                     ImportUnitOptions::defaults(), diags);

    // 条目数＝正确行数（2）——部分成功，错误行不阻断正确行。
    ASSERT_EQ(out.status, ImportOutcome::Status::Partial);
    ASSERT_EQ(out.entries.size(), 2U);
    EXPECT_EQ(out.entries[0].name, "取件点");
    EXPECT_EQ(out.entries[1].name, "放件点");
    // 错误行逐条（2 条行级错误）＋定位三要素＋原因非空。
    ASSERT_EQ(out.rowErrors.size(), 2U);
    for (const auto& e : out.rowErrors) {
        EXPECT_EQ(e.code, kReqImportRowError);
        EXPECT_TRUE(contextCarriesTriple(e)) << e.context;
        EXPECT_FALSE(e.cause.empty());
        EXPECT_FALSE(e.recommendedAction.empty());
    }
    // 逐条定位可溯（行 2 level、行 3 x——context 含行号与列名）。
    EXPECT_NE(out.rowErrors[0].context.find("row=2; column=level"), std::string::npos);
    EXPECT_NE(out.rowErrors[0].context.find("raw=Info"), std::string::npos);
    EXPECT_NE(out.rowErrors[1].context.find("row=3; column=x"), std::string::npos);
    EXPECT_NE(out.rowErrors[1].context.find("raw=abc,def"), std::string::npos);
}

/**
 * 重复 id/name→REQ-IMPORT-DUPLICATE-ID（acceptance 1）：重复行丢弃、
 * 首行保留；诊断携重复值与首现行号。
 */
TEST(ReqImport, CsvDuplicateIdAndName_ReqImportDuplicateId_V08)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    auto table = makeTable({canonicalHeader(),
                            basicRow("p1", "同名点"),   // 行 1（保留）
                            basicRow("p1", "另一名"),   // 行 2（id 重复——丢弃）
                            basicRow("p3", "同名点")}); // 行 3（name 重复——丢弃）

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    const auto out = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags);

    ASSERT_EQ(out.status, ImportOutcome::Status::Partial);
    ASSERT_EQ(out.entries.size(), 1U);  // 仅首行保留
    EXPECT_EQ(out.entries[0].name, "同名点");
    // id 重复与 name 重复各一条（两键共用一码——§9.6 行语义）。
    ASSERT_EQ(out.rowErrors.size(), 2U);
    EXPECT_EQ(out.rowErrors[0].code, kReqImportDuplicateId);
    EXPECT_NE(out.rowErrors[0].context.find("column=id; value=p1; first-row=1"),
              std::string::npos);
    EXPECT_EQ(out.rowErrors[1].code, kReqImportDuplicateId);
    EXPECT_NE(out.rowErrors[1].context.find("column=name; value=同名点; first-row=1"),
              std::string::npos);
}

/**
 * 必填列缺失（id/name/位置三分量任一未映射）→结构级拒绝：该文件不可
 * 导入（status=Rejected、entries 空）＋REQ-IMPORT-UNIT-ILLEGAL（列缺失
 * 分支——§9.6 行语义）。
 */
TEST(ReqImport, CsvMissingRequiredColumn_StructuralRejection_V08)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    // 表头缺 x/y/z 位置三分量（含 id/name 齐全的反面——仅位置缺失即可拒绝）。
    auto table = makeTable({{"id", "name", "note"},
                            {"p1", "取件点", "备注"}});

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    const auto out = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags);

    EXPECT_EQ(out.status, ImportOutcome::Status::Rejected);
    EXPECT_TRUE(out.entries.empty());   // 结构级拒绝——零条目（无半成品）
    EXPECT_TRUE(out.rowErrors.empty()); // 非行级——结构级诊断走 diags 面
    ASSERT_TRUE(hasDiagCode(diags, kReqImportUnitIllegal));
    EXPECT_NE(diags[0].cause.find("必备列缺失"), std::string::npos);
    EXPECT_NE(diags[0].cause.find("x"), std::string::npos);
}

/**
 * 可选列缺失→默认值＋报告（acceptance 1）：defaultedFields 列出缺省补
 * 全列；条目取字典缺省（level=Must/enabled=true/process_tag=Generic/
 * 容差 1e-3 m 与 π/180 rad——RequirementTypes 设计默认）。
 */
TEST(ReqImport, CsvMissingOptionalColumn_DefaultValueAndReport_V08)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    // 只给必备列＋note（level/enabled/process_tag/容差等全缺）。
    auto table = makeTable({{"id", "name", "x", "y", "z", "note"},
                            {"p1", "取件点", "1", "2", "3", "备注"}});

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    const auto out = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags);

    ASSERT_EQ(out.status, ImportOutcome::Status::Completed);
    ASSERT_EQ(out.entries.size(), 1U);
    // 缺省补全报告（字典序——冻结表原文序的可选列）。
    EXPECT_FALSE(out.defaultedFields.empty());
    EXPECT_NE(std::find(out.defaultedFields.begin(), out.defaultedFields.end(), "level"),
              out.defaultedFields.end());
    EXPECT_NE(std::find(out.defaultedFields.begin(), out.defaultedFields.end(), "enabled"),
              out.defaultedFields.end());
    EXPECT_NE(std::find(out.defaultedFields.begin(), out.defaultedFields.end(), "pos_tol"),
              out.defaultedFields.end());
    // 条目缺省值（RequirementTypes 设计默认——黄金锁定）。
    const auto& point = out.entries[0];
    EXPECT_EQ(point.level, RequirementLevel::Must);
    EXPECT_TRUE(point.enabled);
    EXPECT_EQ(point.processTag, ProcessTag::Generic);
    EXPECT_DOUBLE_EQ(point.tolerance.positionTolerance, 1.0e-3);            // m
    EXPECT_DOUBLE_EQ(point.tolerance.orientationTolerance, 3.14159265358979323846 / 180.0);  // rad
    EXPECT_FALSE(point.approach.enabled);  // 段距离缺列＝段关闭
    EXPECT_FALSE(point.retract.enabled);
}

/**
 * 未映射/多余列→忽略项清单（不静默丢弃——acceptance 1）：多余列名进
 * ignoredColumns。
 */
TEST(ReqImport, CsvUnmappedColumn_ReportedAsIgnored_V08)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    auto table = makeTable({{"id", "name", "x", "y", "z", "遗留列", "note"},
                            {"p1", "取件点", "1", "2", "3", "垃圾数据", "备注"}});

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    const auto out = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags);

    ASSERT_EQ(out.status, ImportOutcome::Status::Completed);
    ASSERT_EQ(out.entries.size(), 1U);  // 多余列不阻断导入
    ASSERT_EQ(out.ignoredColumns.size(), 1U);
    EXPECT_EQ(out.ignoredColumns[0], "遗留列");  // 不静默丢弃——列名入清单
}

// =====================================================================
// acceptance 2——字段字典自动识别（卡 §7.3 冻结表）＋手动改映射
// =====================================================================

/**
 * 字段字典冻结表经 fieldDictionary() 暴露（acceptance 2——WP-14-T01 交
 * 付物零改动承接）：20 字段、canonical 名逐字等于 §7.3 冻结表、必备集
 * ＝{id,name,x,y,z}、长度/角度列种类与单位缺省语义对应。
 */
TEST(ReqImport, FieldDictionary_FrozenTableExposed_Acc2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05", "REQ-12"}, std::vector<std::string>{});

    RequirementImporter importer;
    const auto& dict = importer.fieldDictionary();
    ASSERT_EQ(dict.fields().size(), 20U);  // §7.3 冻结表 20 列

    // canonical 名逐字核对（冻结表原文——卡 §7.3 字段清单）。
    const char* expected[] = {"id", "name", "process_tag", "level", "enabled",
                              "ref_frame", "tcp", "x", "y", "z", "roll", "pitch",
                              "yaw", "pos_tol", "ori_tol", "approach_axis",
                              "approach_dist", "retract_dist", "min_joint_margin", "note"};
    for (std::size_t i = 0; i < dict.fields().size(); ++i) {
        EXPECT_EQ(importFieldToken(dict.fields()[i].field), expected[i]) << i;
    }
    // 必备集＝{id,name,x,y,z}（缺列→结构级拒绝的四加二）。
    EXPECT_TRUE(dict.spec(ImportField::Id).required);
    EXPECT_TRUE(dict.spec(ImportField::Name).required);
    EXPECT_TRUE(dict.spec(ImportField::X).required);
    EXPECT_TRUE(dict.spec(ImportField::Y).required);
    EXPECT_TRUE(dict.spec(ImportField::Z).required);
    EXPECT_FALSE(dict.spec(ImportField::Note).required);
    // 数量种类（长度/角度/无单位/文本——单位缺省语义的依据）。
    EXPECT_EQ(dict.spec(ImportField::X).kind, ImportColumnKind::Length);
    EXPECT_EQ(dict.spec(ImportField::Roll).kind, ImportColumnKind::Angle);
    EXPECT_EQ(dict.spec(ImportField::OriTol).kind, ImportColumnKind::Angle);
    EXPECT_EQ(dict.spec(ImportField::MinJointMargin).kind, ImportColumnKind::Number);
    EXPECT_EQ(dict.spec(ImportField::Name).kind, ImportColumnKind::Text);
}

/**
 * 中英别名自动识别＋手动改映射（acceptance 2）：canonical/中文别名/大写
 * 英文别名均识别；手动 assign 覆盖自动识别结果；未识别列入清单。
 */
TEST(ReqImport, AutoDetect_AliasesAndManualOverride_Acc2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    RequirementImporter importer;
    const auto& dict = importer.fieldDictionary();

    // 单点头：canonical、中文别名、大写别名各识别一列。
    EXPECT_EQ(dict.tryRecognize("id"), ImportField::Id);
    EXPECT_EQ(dict.tryRecognize("名称"), ImportField::Name);
    EXPECT_EQ(dict.tryRecognize("ID"), ImportField::Id);
    EXPECT_EQ(dict.tryRecognize("坐标X"), ImportField::X);
    EXPECT_EQ(dict.tryRecognize("位置容差"), ImportField::PosTol);
    EXPECT_EQ(dict.tryRecognize("备注"), ImportField::Note);
    // 词表外（未登记写法不猜测——精确等值；空白污染不改写）。
    EXPECT_FALSE(dict.tryRecognize("label").has_value());
    EXPECT_FALSE(dict.tryRecognize(" id").has_value());  // 前导空白不改写

    // 表级自动识别：混合 canonical/别名表头＋一未知列。
    auto table = makeTable({{"编号", "名称", "X", "Y", "Z", "神秘列"},
                            {"p1", "取件点", "1", "2", "3", "?"}});
    auto detect = autoDetectMapping(table, dict);
    EXPECT_EQ(detect.mapping.columnFor(ImportField::Id), 0U);    // "编号"→id
    EXPECT_EQ(detect.mapping.columnFor(ImportField::Name), 1U);  // "名称"→name
    EXPECT_EQ(detect.mapping.columnFor(ImportField::X), 2U);
    ASSERT_EQ(detect.unrecognizedColumns.size(), 1U);
    EXPECT_EQ(detect.unrecognizedColumns[0], 5U);  // "神秘列"

    // 手动改映射：把 name 改指第 0 列（覆盖自动识别——向导下拉框语义）。
    detect.mapping.assign(ImportField::Name, 0);
    EXPECT_EQ(detect.mapping.columnFor(ImportField::Name), 0U);

    // 同字段双列：首列胜出（先到先得——确定性），后列入未识别清单。
    auto dup = makeTable({{"id", "id", "name", "x", "y", "z"},
                          {"a", "b", "n", "1", "2", "3"}});
    const auto detectDup = autoDetectMapping(dup, dict);
    EXPECT_EQ(detectDup.mapping.columnFor(ImportField::Id), 0U);
    ASSERT_EQ(detectDup.unrecognizedColumns.size(), 1U);
    EXPECT_EQ(detectDup.unrecognizedColumns[0], 1U);
}

// =====================================================================
// acceptance 3——单位预览与 SI 归一（V-09/NFR-COR-03）
// =====================================================================

/**
 * mm/deg 声明列换算预览正确（V-09 前半）：previewUnitConversion 与
 * mapCsv 用同一套声明校验/换算入口（语义单源）——预览值＝落库值。
 */
TEST(ReqImport, UnitPreview_MmDegConversionCorrect_V09)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    auto table = makeTable({{"id", "name", "x", "y", "z", "roll"},
                            {"p1", "取件点", "1000", "2", "3", "90"}});
    RequirementImporter importer;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    ImportUnitOptions units = ImportUnitOptions::defaults();
    units.setUnit(ImportField::X, "mm");
    units.setUnit(ImportField::Roll, "deg");

    const auto preview = previewUnitConversion(table, detect.mapping, units,
                                               importer.fieldDictionary());
    // 预览覆盖该表全部已映射长度/角度列（x/y/z/roll 四列——语义口径）。
    ASSERT_EQ(preview.size(), 4U);
    // 声明列预览（x=mm、roll=deg）：声明补全＋首样本 SI 换算。
    const auto& xEntry = preview[0];   // 字典序：x 为首长度列
    EXPECT_EQ(xEntry.field, ImportField::X);
    EXPECT_EQ(xEntry.declaredUnit, "mm");
    EXPECT_EQ(xEntry.rawSample, "1000");
    ASSERT_TRUE(xEntry.siSample.has_value());
    EXPECT_DOUBLE_EQ(*xEntry.siSample, 1.0);  // 1000 mm→1 m（SI 断言）
    const auto& rollEntry = preview[3];        // 字典序：roll 为末角度列
    EXPECT_EQ(rollEntry.field, ImportField::Roll);
    EXPECT_EQ(rollEntry.declaredUnit, "deg");
    EXPECT_EQ(rollEntry.rawSample, "90");
    ASSERT_TRUE(rollEntry.siSample.has_value());
    EXPECT_DOUBLE_EQ(*rollEntry.siSample, 90.0 * 3.14159265358979323846 / 180.0);  // 1.5707… rad
    // 未声明列取缺省单位（y/z——长度列缺省 m）。
    EXPECT_EQ(preview[1].declaredUnit, "m");
    EXPECT_EQ(preview[2].declaredUnit, "m");
}

/**
 * 落库前经 core 唯一换算归一 SI（V-09 后半/NFR-COR-03）：mm/deg 声明下
 * 导入条目的位置/姿态字段为 SI 值断言（m/rad）。
 */
TEST(ReqImport, CsvUnitNormalization_SiValuesAsserted_V09_NFR_COR03)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    auto table = makeTable({{"id", "name", "x", "y", "z", "roll", "pitch", "yaw"},
                            {"p1", "取件点", "2000", "500", "3", "90", "45", "0"}});
    RequirementImporter importer;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    ImportUnitOptions units = ImportUnitOptions::defaults();
    units.setUnit(ImportField::X, "mm");
    units.setUnit(ImportField::Y, "mm");
    units.setUnit(ImportField::Roll, "deg");
    units.setUnit(ImportField::Pitch, "deg");

    std::vector<core::DiagnosticRecord> diags;
    const auto out = importer.mapCsv(table, detect.mapping, units, diags);
    ASSERT_EQ(out.status, ImportOutcome::Status::Completed);
    ASSERT_EQ(out.entries.size(), 1U);
    // SI 值断言（core 唯一换算入口——NFR-COR-03 不静默转 0）。
    ASSERT_TRUE(out.entries[0].pose.position.tryValue().has_value());
    const auto& pos = *out.entries[0].pose.position.tryValue();
    EXPECT_DOUBLE_EQ(pos[0], 2.0);   // 2000 mm→2 m
    EXPECT_DOUBLE_EQ(pos[1], 0.5);   // 500 mm→0.5 m
    EXPECT_DOUBLE_EQ(pos[2], 3.0);   // 缺省 m 直读
    const auto& rpy = out.entries[0].pose.orientation.fixedRpy;
    EXPECT_DOUBLE_EQ(rpy[0], 90.0 * 3.14159265358979323846 / 180.0);  // deg→rad
    EXPECT_DOUBLE_EQ(rpy[1], 45.0 * 3.14159265358979323846 / 180.0);
    EXPECT_DOUBLE_EQ(rpy[2], 0.0);                                    // yaw 未声明＝"0"直读
}

/**
 * 单位声明无法换算→REQ-IMPORT-UNIT-ILLEGAL（acceptance 3）：量纲不匹配
 * （x 声明 deg）与词表外 token（x 声明 furlong）各产一列级诊断；受累行
 * 以行级错误暴露（不静默丢弃列值）。
 */
TEST(ReqImport, CsvUnitDeclarationIllegal_ReqImportUnitIllegal_V09)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    // 反例 1：长度列声明角度单位（量纲不匹配）。
    auto table = makeTable({{"id", "name", "x", "y", "z"},
                            {"p1", "取件点", "1", "2", "3"}});
    RequirementImporter importer;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    ImportUnitOptions units = ImportUnitOptions::defaults();
    units.setUnit(ImportField::X, "deg");
    std::vector<core::DiagnosticRecord> diags;
    const auto out = importer.mapCsv(table, detect.mapping, units, diags);

    ASSERT_TRUE(hasDiagCode(diags, kReqImportUnitIllegal));
    EXPECT_NE(diags[0].context.find("column=x"), std::string::npos);
    // 受累行：x 无法换算→行级错误（部分成功——行不静默丢弃）。
    EXPECT_EQ(out.status, ImportOutcome::Status::Partial);
    EXPECT_TRUE(out.entries.empty());
    ASSERT_FALSE(out.rowErrors.empty());
    EXPECT_EQ(out.rowErrors[0].code, kReqImportRowError);

    // 反例 2：词表外单位 token。
    std::vector<core::DiagnosticRecord> diags2;
    ImportUnitOptions units2 = ImportUnitOptions::defaults();
    units2.setUnit(ImportField::X, "furlong");
    const auto out2 = importer.mapCsv(table, detect.mapping, units2, diags2);
    ASSERT_TRUE(hasDiagCode(diags2, kReqImportUnitIllegal));
    EXPECT_NE(diags2[0].cause.find("unregistered-token"), std::string::npos);
    EXPECT_TRUE(out2.entries.empty());
}

/**
 * Frame 引用悬空→REQ-IMPORT-FRAME-UNKNOWN（warning，可保留待解析——
 * acceptance 3/V-09）：裸 ObjectId 引用的条目保留，诊断级别为警告面。
 */
TEST(ReqImport, CsvFrameDangling_WarningAndRetained_V09)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    const auto oidText = sampleObjectIdText();
    auto table = makeTable({{"id", "name", "x", "y", "z", "ref_frame"},
                            {"p1", "取件点", "1", "2", "3", oidText}});

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    const auto out = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags);

    // 条目保留（warning 不阻断——"可保留待解析"）。
    ASSERT_EQ(out.status, ImportOutcome::Status::Completed);
    ASSERT_EQ(out.entries.size(), 1U);
    // 引用按 ModelFrame 形态保留（跨闭包半区核对归 T05 就绪层 R2）。
    EXPECT_EQ(out.entries[0].refFrame.kind, RequirementRefKind::ModelFrame);
    ASSERT_TRUE(out.entries[0].refFrame.objectId.has_value());
    EXPECT_EQ(out.entries[0].refFrame.objectId->toCanonical(), oidText);
    // 警告诊断（REQ-IMPORT-FRAME-UNKNOWN；context 携原文回查键）。
    ASSERT_TRUE(hasDiagCode(diags, kReqImportFrameUnknown));
    const auto& warn = diags[0];
    EXPECT_NE(warn.context.find(oidText), std::string::npos);
    EXPECT_NE(warn.recommendedAction.find("World"), std::string::npos);
}

// =====================================================================
// acceptance 4——JSON roundtrip 副本语义（V-10/AT-24）
// =====================================================================

/**
 * JSON 导入→exportCopy 副本导出→重导入逐字段一致（V-10/AT-24 主线）：
 * 同一字段字典；条目域字段全等（objectId 均为待分配全零——O-36）。
 */
TEST(ReqImport, JsonRoundtrip_ExportCopy_FieldByFieldEqual_V10_AT24)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-12"}, std::vector<std::string>{"AT-24"});

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto first = importer.mapJson(jsonBytes(basicJson()), diags);
    ASSERT_EQ(first.status, ImportOutcome::Status::Completed) << "首导须全成功";
    ASSERT_EQ(first.entries.size(), 1U);

    // 副本导出（正式形态——draft=false）。
    const auto targetPath = tempPath("roundtrip_copy.json");
    RequirementWorkingSet ws;
    ws.points.entries = first.entries;
    const auto exported = importer.exportCopy(ws, ExportFormat::Json,
                                              ExportTarget{std::filesystem::path{targetPath}, false});
    ASSERT_TRUE(exported.ok) << exported.error;
    EXPECT_GT(exported.bytesWritten, 0U);

    // 重导入→逐字段一致（条目值语义全等；importProvenance 为源侧事实——
    // 首导/重导摘要各指其源，按设计不参与条目域等价，单独断言见溯源用例）。
    const auto bytes = readFile(targetPath);
    std::vector<core::DiagnosticRecord> diags2;
    const auto second = importer.mapJson(jsonBytes(bytes), diags2);
    ASSERT_EQ(second.status, ImportOutcome::Status::Completed);
    ASSERT_EQ(second.entries.size(), 1U);

    auto lhs = first.entries[0];
    auto rhs = second.entries[0];
    lhs.importProvenance.reset();  // 源侧溯源按设计各指其源——归零后比域字段
    rhs.importProvenance.reset();
    EXPECT_EQ(lhs, rhs) << "roundtrip 条目域字段逐字段一致（AT-24）";
    EXPECT_FALSE(lhs.objectId.isValid());  // 待命令分配（O-36）——两轮一致
}

/**
 * exportCopy 经 io AtomicFile 原子写出、失败时旧文件完好（V-10）：注入
 * prepare 失败（fake IAtomicFileWriter——io AtomicFile.hpp 故障注入口径）
 * →ok=false 且目标旧字节逐字保留；工作集（项目侧替身）状态不变。
 */
TEST(ReqImport, ExportCopy_AtomicFailure_OldFileIntact_ProjectUntouched_V10)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-12"}, std::vector<std::string>{"AT-24"});

    RequirementImporter importer(std::make_shared<PrepareFailWriter>());
    const auto targetPath = tempPath("atomic_target.json");
    const std::string oldContent = "{\"previous\":\"完整旧版本\"}";
    {
        std::ofstream out(targetPath, std::ios::binary);
        out << oldContent;
    }

    RequirementWorkingSet ws;
    ws.points.entries.push_back(TaskPoint{});
    ws.points.entries[0].name = "草稿点";
    const auto before = ws;  // 值快照——导出后比对（零修订/项目状态不变）
    const auto result = importer.exportCopy(ws, ExportFormat::Json,
                                            ExportTarget{targetPath, false});

    // 失败如实失败（值面——非异常路径）。
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    // 旧文件完好（字节级——commit 未发生，目标零接触）。
    EXPECT_EQ(readFile(targetPath), oldContent);
    // 工作集不变（exportCopy 无项目句柄——结构上不可能产生修订；此处以
    // 值等价复核"零副作用"）。
    EXPECT_EQ(ws, before);
}

/**
 * 导出成功路径：OverwriteAtomic 原子替换（旧内容被完整新内容顶替）＋
 * draft:true 标记（acceptance 4——防误当正式数据）；重导入观测
 * sourceMarkedDraft。
 */
TEST(ReqImport, ExportCopy_OverwriteAtomic_DraftMarker_V10)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-12"}, std::vector<std::string>{"AT-24"});

    RequirementImporter importer;
    const auto targetPath = tempPath("overwrite_draft.json");
    {
        std::ofstream out(targetPath, std::ios::binary);
        out << "OLD";
    }

    // 导出条目＝真实导入产物（含位置三分量——坐标表字典的必备列语义）。
    std::vector<core::DiagnosticRecord> seedDiags;
    const auto seeded = importer.mapJson(jsonBytes(basicJson()), seedDiags);
    ASSERT_EQ(seeded.status, ImportOutcome::Status::Completed);

    RequirementWorkingSet ws;
    ws.points.entries = seeded.entries;
    const auto result = importer.exportCopy(ws, ExportFormat::Json,
                                            ExportTarget{targetPath, /*draft=*/true});
    ASSERT_TRUE(result.ok) << result.error;

    // 草稿标记入文档（"draft": true——canonical 键序内可定位）＋旧内容被顶替。
    const auto bytes = readFile(targetPath);
    EXPECT_NE(bytes.find("\"draft\": true"), std::string::npos);
    EXPECT_EQ(bytes.find("OLD"), std::string::npos);

    // 重导入：标记透传为观测面（不改变导入语义）。
    std::vector<core::DiagnosticRecord> diags;
    const auto out = importer.mapJson(jsonBytes(bytes), diags);
    ASSERT_EQ(out.status, ImportOutcome::Status::Completed);
    EXPECT_TRUE(out.sourceMarkedDraft);
}

/**
 * CSV 无草稿标记通道→draft 请求被值面拒绝（fail-visible——不静默省略
 * 标记导出，acceptance 4 的 ExportTarget 语义对偶面）。
 */
TEST(ReqImport, ExportCopy_CsvDraftRejected_FailVisible)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-12"}, std::vector<std::string>{});

    RequirementImporter importer;
    const auto targetPath = tempPath("no_such.csv");
    const auto result = importer.exportCopy(RequirementWorkingSet{}, ExportFormat::Csv,
                                            ExportTarget{targetPath, /*draft=*/true});
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("草稿"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(targetPath));  // 拒绝在写出前——零副作用
}

/**
 * 导入溯源 importProvenance={sourceDigest, recordNumber}、路径不入身份
 * （I-REQ-8）：JSON 通道摘要＝输入字节 SHA-256、记录号＝记录序。
 */
TEST(ReqImport, JsonImport_ProvenanceDigestAndRecordNumber_I_REQ8_V10)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-12"}, std::vector<std::string>{"AT-24"});

    const auto bytes = jsonBytes(basicJson());
    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto out = importer.mapJson(bytes, diags);
    ASSERT_EQ(out.entries.size(), 1U);

    // 摘要期望值＝输入字节原文的 SHA-256（自算对表——SA-12 唯一摘要算法）。
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    EXPECT_EQ(out.entries[0].importProvenance->sourceDigest, digester.finalize());
    EXPECT_EQ(out.entries[0].importProvenance->recordNumber, 1U);  // 记录序 1 起
    // 路径不入身份：ImportProvenance 无路径字段（类型面结构性保证——此处
    // 复核溯源两字段恰为登记口径）。
    EXPECT_FALSE(out.entries[0].importProvenance->sourceDigest
                     == core::Digest256{});
}

// =====================================================================
// acceptance 5——CSV 按数据解析不执行公式（DTB 约束列原文）
// =====================================================================

/**
 * 公式样式单元格作为字面数据处理（REQ-05/NFR-SEC-03）：以 = + - @ 开头
 * 的文本列值按字面导入/按行错误回显，任何路径不求值——note 列导出
 * "=1+1" 后字段值恒为字面 "=1+1"（不是 "2"）。
 */
TEST(ReqImport, CsvFormulaCellsAsData_NoExecution_Req05_NFR_SEC03)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05", "NFR-SEC-03"}, std::vector<std::string>{"AT-02"});

    auto table = makeTable({{"id", "name", "x", "y", "z", "note"},
                            {"=p1", "=1+1&REPT(\"x\",3)", "1", "2", "3",
                             "=cmd|' /C calc'!A0"}});
    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    const auto out = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags);

    // 公式样式文本按字面导入——无任何求值/命令执行路径（io 通道数据-only
    // ＋本单元零解释：字段值＝原文）。
    ASSERT_EQ(out.status, ImportOutcome::Status::Completed);
    ASSERT_EQ(out.entries.size(), 1U);
    EXPECT_EQ(out.entries[0].note, "=cmd|' /C calc'!A0");  // DDE 样式串字面保留
    EXPECT_EQ(out.entries[0].name, "=1+1&REPT(\"x\",3)");   // 文本列原样
    // 数值列公式样式文本＝行级错误（按数据解析——不求值也不猜测）。
    auto table2 = makeTable({{"id", "name", "x", "y", "z"},
                             {"p1", "取件点", "=1+1", "2", "3"}});
    std::vector<core::DiagnosticRecord> diags2;
    const auto detect2 = autoDetectMapping(table2, importer.fieldDictionary());
    const auto out2 = importer.mapCsv(table2, detect2.mapping, ImportUnitOptions::defaults(), diags2);
    EXPECT_EQ(out2.status, ImportOutcome::Status::Partial);
    EXPECT_TRUE(out2.entries.empty());  // "=1+1" 不求值为 2——整行按错误处置
    ASSERT_FALSE(out2.rowErrors.empty());
    EXPECT_NE(out2.rowErrors[0].context.find("raw==1+1"), std::string::npos);  // 原文回显
}

// =====================================================================
// acceptance 6——mapCsv/mapJson 纯函数确定性；取消/失败＝无草稿无修订
// =====================================================================

/**
 * mapCsv 纯函数确定性（同输入同输出——acceptance 6/NFR-COR-01）：两次
 * 调用产出逐成员等价（条目/诊断/摘要/报告）。
 */
TEST(ReqImport, MapCsv_Deterministic_SameInputSameOutput_Acc6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05", "NFR-COR-01"}, std::vector<std::string>{});

    auto table = makeTable({canonicalHeader(),
                            basicRow("p1", "取件点"),
                            basicRow("p2", "坏行"),   // 行级错误面也纳入比对
                            basicRow("p1", "重复")});
    table.rows[1][3] = "Info";  // level 非法（行 2 错误）

    RequirementImporter importer;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    std::vector<core::DiagnosticRecord> diags1;
    std::vector<core::DiagnosticRecord> diags2;
    const auto out1 = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags1);
    const auto out2 = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags2);

    EXPECT_TRUE(outcomesEquivalent(out1, out2));
    EXPECT_EQ(diags1, diags2);
}

/**
 * mapJson 纯函数确定性（acceptance 6）：同字节两次调用产出等价。
 */
TEST(ReqImport, MapJson_Deterministic_SameInputSameOutput_Acc6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-12", "NFR-COR-01"}, std::vector<std::string>{});

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags1;
    std::vector<core::DiagnosticRecord> diags2;
    const auto out1 = importer.mapJson(jsonBytes(basicJson()), diags1);
    const auto out2 = importer.mapJson(jsonBytes(basicJson()), diags2);

    EXPECT_TRUE(outcomesEquivalent(out1, out2));
    EXPECT_EQ(diags1, diags2);
}

/**
 * 结构级拒绝＝无草稿无修订无半成品（acceptance 6/V-20 requirements 侧；
 * PM-01 口径）：Rejected 时条目恒空、无行级半成品；mapCsv/mapJson 纯内
 * 存计算（输入即 io 已解析产物——签名面无文件/项目句柄，结构上不可能
 * 产生草稿落盘或修订）。
 */
TEST(ReqImport, ImportRejected_NoDraftNoRevisionNoHalfProduct_Acc6_V20)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "REQ-05"}, std::vector<std::string>{"AT-04"});

    auto table = makeTable({{"id", "name"},   // 缺位置三分量——结构级拒绝
                            {"p1", "取件点"}});
    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(table, importer.fieldDictionary());
    const auto out = importer.mapCsv(table, detect.mapping, ImportUnitOptions::defaults(), diags);

    EXPECT_EQ(out.status, ImportOutcome::Status::Rejected);
    EXPECT_TRUE(out.entries.empty());
    EXPECT_TRUE(out.rowErrors.empty());   // 无行级半成品（整文件拒绝）
    EXPECT_TRUE(hasDiagCode(diags, kReqImportUnitIllegal));
}

/**
 * CSV 源摘要口径（Import.hpp 头注实现决策 2 的自证）：同内容 RawTable
 * 两次导入同摘要；内容变更摘要变（内容寻址——CON-05 一致口径）。
 */
TEST(ReqImport, CsvSourceDigest_ContentAddressed_Acc4_I_REQ8)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{"AT-02"});

    auto tableA1 = makeTable({{"id", "name", "x", "y", "z"}, {"p1", "取件点", "1", "2", "3"}});
    auto tableA2 = makeTable({{"id", "name", "x", "y", "z"}, {"p1", "取件点", "1", "2", "3"}});
    auto tableB  = makeTable({{"id", "name", "x", "y", "z"}, {"p1", "取件点", "9", "2", "3"}});

    RequirementImporter importer;
    std::vector<core::DiagnosticRecord> diags;
    const auto detect = autoDetectMapping(tableA1, importer.fieldDictionary());
    const auto outA1 = importer.mapCsv(tableA1, detect.mapping, ImportUnitOptions::defaults(), diags);
    const auto outA2 = importer.mapCsv(tableA2, detect.mapping, ImportUnitOptions::defaults(), diags);
    const auto outB = importer.mapCsv(tableB, detect.mapping, ImportUnitOptions::defaults(), diags);

    // 同内容同摘要（确定性）；内容变更摘要变（内容寻址）。
    EXPECT_EQ(outA1.sourceDigest, outA2.sourceDigest);
    EXPECT_NE(outA1.sourceDigest, outB.sourceDigest);
    // 摘要＝RawTable 规范投影的 SHA-256（期望值自算对表）。
    core::ContentDigester digester;
    for (const auto& name : tableA1.report.header) {
        digester.update(name.data(), name.size());
        digester.update("\n", 1);
    }
    for (const auto& row : tableA1.rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i != 0) {
                digester.update("\x1F", 1);
            }
            digester.update(row[i].data(), row[i].size());
        }
        digester.update("\n", 1);
    }
    EXPECT_EQ(outA1.entries[0].importProvenance->sourceDigest, digester.finalize());
}

// =====================================================================
// 调用方契约违约 fail-fast（错误语义面——AGENTS.md 调用方错误轨）
// =====================================================================

/**
 * 前置违约 fail-fast：无表头自动识别、映射越界、rows 与 dataRows 失配、
 * 空字节 mapJson、空路径 exportCopy——均抛 std::invalid_argument。
 */
TEST(ReqImport, PreconditionViolations_FailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05"}, std::vector<std::string>{});

    RequirementImporter importer;

    // 无表头 → autoDetectMapping 拒绝。
    io::RawTable headerless;
    headerless.rows.emplace_back(std::vector<io::IoString>{"a", "b"});
    EXPECT_THROW(autoDetectMapping(headerless, importer.fieldDictionary()),
                 std::invalid_argument);

    // 映射越界 → mapCsv 拒绝。
    auto table = makeTable({{"id", "name", "x", "y", "z"}, {"p1", "取件点", "1", "2", "3"}});
    FieldMapping bad = FieldMapping::none();
    bad.assign(ImportField::Id, 0);
    bad.assign(ImportField::Name, 1);
    bad.assign(ImportField::X, 42);  // 越界
    bad.assign(ImportField::Y, 3);
    bad.assign(ImportField::Z, 4);
    std::vector<core::DiagnosticRecord> diags;
    EXPECT_THROW(importer.mapCsv(table, bad, ImportUnitOptions::defaults(), diags),
                 std::invalid_argument);

    // rows 与 dataRows 失配（流式丢弃行误用）→ mapCsv 拒绝。
    io::RawTable inconsistent = table;
    inconsistent.rows.clear();
    EXPECT_THROW(importer.mapCsv(inconsistent, autoDetectMapping(table, importer.fieldDictionary()).mapping,
                                 ImportUnitOptions::defaults(), diags),
                 std::invalid_argument);

    // 空字节 → mapJson 拒绝。
    std::vector<core::DiagnosticRecord> diagsJson;
    EXPECT_THROW(importer.mapJson({}, diagsJson), std::invalid_argument);

    // 空目标路径 → exportCopy 拒绝。
    EXPECT_THROW(importer.exportCopy(RequirementWorkingSet{}, ExportFormat::Json,
                                     ExportTarget{{}, false}),
                 std::invalid_argument);
}
