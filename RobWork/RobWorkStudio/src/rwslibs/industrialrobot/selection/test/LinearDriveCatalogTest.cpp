/**
 * @file   LinearDriveCatalogTest.cpp
 * @brief  直线传动目录模板导入用例组（SelLinearCatalog）——四类直线传动
 *         器件（滚珠丝杠/齿条/同步带/直线电机）v2 目录包经既有导入校验
 *         通道（SEL-01/02 同一校验器——引用/单位/必填/唯一性/范围）的
 *         正例装配黄金对照、误例逐 issue 黄金对照、v1 输入面零变化与
 *         版本分派（WP-19-T12——SEL-09-S1；AT-36 选型侧）。
 *
 * 设计依据：
 *   - units/selection.md §5.1（固定交接流程——validate→assemble）、§5.2
 *     （v2 第六表 linear_drives.csv）、§5.3（业务校验八码族）、§6.3（曲线
 *     校验四码）、§17.2（SEL-09-S1 目录模板：能力曲线横坐标速度/载荷
 *     〔m/s、N〕纵坐标力/功率〔N、W〕——量纲词表扩展；v1 面零变化）、
 *     §18（SEL-09-S1 验证位置＝V9 AT-36 选型侧测试）
 *   - 需求 SEL-09-S1（目录模板导入校验通过——AT-36 选型侧验收标准）、
 *     SEL-01/SEL-02（同一校验通道）、NFR-COR-03（非有限/非法单位不静默）
 *   - 任务契约 tasks/foundation/WP-19-T12.json acceptance 1（四类模板经
 *     既有通道通过）＋acceptance 3（不改变旋转链行为——v1 分派面零变化）
 *
 * 测试策略（SelGoldenDatasetTest 同款形态）：期望值全部来自数据集
 * sel-linear-golden@1.0.0（generate/ 脚本独立推导——与产品实现零共享），
 * 本文件只做"装载→驱动产品入口→黄金对照"；驱动入口＝ICatalogValidator
 * （CatalogImporter validate/assemble——经公共类消费钉扎交付面）。
 *
 * 线程约束：gtest 用例天然串行；被测入口全部纯函数（卡 §14.10）。
 */

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sel = sdurws::ird::selection;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;

// =====================================================================
// 黄金 CSV/JSON 读取脚手架（SelGoldenDatasetTest 同款——数据集装载/完整性
// 已由 GoldenFixture 校验；此处只做字段提取）
// =====================================================================

namespace {

/// 读数据集文本文件（路径必须登记于 manifest——越界抛 TestKitError）。
std::string readDsText(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    const auto path = inputSide ? ds.resolveInput(rel) : ds.resolveExpected(rel);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 解析数据集 JSON（JsonLite——黄金面是登记数据资产，解析失败异常传播
/// 即失败，gtest 语义）。
tk::JsonValue loadDsJson(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    return tk::parseJson(readDsText(ds, rel, inputSide));
}

std::string str(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    return (v != nullptr && v->isString()) ? v->text : std::string{};
}

double num(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    return (v != nullptr && v->isNumber()) ? v->number : 0.0;
}

/// 黄金 CSV（方言行 ird-catalog-v1＋表头＋数据；单元格无引号转义——生成
/// 脚本序列化契约一致）。
struct GoldenCsv {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;

    std::vector<std::string> cellsRowMajor() const
    {
        std::vector<std::string> cells;
        for (const auto& r : rows) {
            for (const auto& c : r) {
                cells.push_back(c);
            }
        }
        return cells;
    }
};

GoldenCsv parseGoldenCsv(const std::string& text)
{
    GoldenCsv t;
    std::istringstream in(text);
    std::string line;
    if (!static_cast<bool>(std::getline(in, line)) || line != "ird-catalog-v1") {
        throw std::runtime_error("黄金 CSV 方言标识行不符");
    }
    if (!static_cast<bool>(std::getline(in, line))) {
        throw std::runtime_error("黄金 CSV 缺表头行");
    }
    std::stringstream hs(line);
    std::string colText;
    while (std::getline(hs, colText, ',')) {
        t.header.push_back(colText);
    }
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> row;
        std::stringstream ls(line);
        std::string cell;
        while (std::getline(ls, cell, ',')) {
            row.push_back(cell);
        }
        while (row.size() < t.header.size()) {
            row.push_back("");   // 行尾空列（io PadTrailing 语义）
        }
        t.rows.push_back(std::move(row));
    }
    return t;
}

/// 由黄金 CSV 构造解析表（firstDataRowNo＝3——方言行＋表头行偏移）。
sel::ParsedFileTable toParsedTable(const std::string& fileName, const GoldenCsv& t)
{
    sel::ParsedFileTable p;
    p.fileName = fileName;
    p.header = t.header;
    p.columnCount = t.header.size();
    p.firstDataRowNo = 3;
    p.cells = t.cellsRowMajor();
    return p;
}

/// 由 valid 目录包构造 v2 解析输入（六表全量）。
sel::ParsedCatalogInput makeValidInput(const tk::GoldenDataset& ds)
{
    sel::ParsedCatalogInput in;
    in.files.push_back(toParsedTable(sel::kCatalogFileMotors,
                                     parseGoldenCsv(readDsText(ds, "inputs/valid/motors.csv", true))));
    in.files.push_back(toParsedTable(sel::kCatalogFileGearboxes,
                                     parseGoldenCsv(readDsText(ds, "inputs/valid/gearboxes.csv", true))));
    in.files.push_back(toParsedTable(sel::kCatalogFileCurves,
                                     parseGoldenCsv(readDsText(ds, "inputs/valid/capability_curves.csv", true))));
    in.files.push_back(toParsedTable(sel::kCatalogFileCompatibility,
                                     parseGoldenCsv(readDsText(ds, "inputs/valid/compatibility.csv", true))));
    in.files.push_back(toParsedTable(sel::kCatalogFileLinearDrives,
                                     parseGoldenCsv(readDsText(ds, "inputs/valid/linear_drives.csv", true))));
    return in;
}

/// v1 兼容曲线表（owner_kind 仅为 motor|gearbox 的行——v1 词表无
/// linear-drive；v1 包本不含直线曲线行，测试构造 v1 输入面时过滤，
/// 保证"v1 输入面零变化"断言的输入是真 v1 内容）。
sel::ParsedFileTable makeV1CurveTable(const tk::GoldenDataset& ds)
{
    GoldenCsv curves = parseGoldenCsv(readDsText(ds, "inputs/valid/capability_curves.csv", true));
    const std::size_t ownerCol = std::find(curves.header.begin(), curves.header.end(),
                                           "owner_kind")
                                 - curves.header.begin();
    GoldenCsv v1;
    v1.header = curves.header;
    for (std::vector<std::string>& row : curves.rows) {
        if (row[ownerCol] != sel::kCurveOwnerLinearDrive) {
            v1.rows.push_back(row);
        }
    }
    return toParsedTable(sel::kCatalogFileCurves, v1);
}

/// 由 valid 包 manifest.json 构造 CatalogManifest（业务模型直载——contentIdentity
/// 输入侧全零占位，装配入口回填为唯一权威，卡 §4.2）。
sel::CatalogManifest makeValidManifest(const tk::GoldenDataset& ds)
{
    const tk::JsonValue m = loadDsJson(ds, "inputs/valid/manifest.json", true);
    sel::CatalogManifest manifest;
    manifest.formatVersion = str(m, "formatVersion");
    const tk::JsonValue& id = *m.find("identity");
    manifest.identity.catalogId = str(id, "catalogId");
    manifest.identity.version = str(id, "version");
    manifest.identity.source = str(id, "source");
    for (const tk::JsonValue& f : m.find("files")->items) {
        sel::ManifestEntry e;
        e.fileName = str(f, "fileName");
        e.role = str(f, "role");
        // required 为 JSON boolean（JsonValue::boolean——number 成员对 Bool
        // 态无效，误读恒 0 会使字典全部 optional 化、契约比对全表失配）。
        const tk::JsonValue* req = f.find("required");
        e.required = req != nullptr && req->isBool() && req->boolean;
        manifest.files.push_back(std::move(e));
    }
    for (const tk::JsonValue& d : m.find("fieldDictionary")->items) {
        sel::FieldDictionary dict;
        dict.targetFile = str(d, "targetFile");
        for (const tk::JsonValue& fs : d.find("fields")->items) {
            sel::FieldSpec spec;
            spec.column = str(fs, "column");
            spec.semantic = str(fs, "semantic");
            spec.unit = str(fs, "unit");
            const tk::JsonValue* req = fs.find("required");
            spec.required = req != nullptr && req->isBool() && req->boolean;
            dict.fields.push_back(std::move(spec));
        }
        manifest.fieldDictionary.push_back(std::move(dict));
    }
    return manifest;
}

}  // namespace

// =====================================================================
// 黄金消费夹具（数据集 sel-linear-golden@1.0.0——装载即完整性校验）
// =====================================================================

class SelLinearCatalog : public tk::GoldenFixture {
protected:
    tk::DatasetRef datasetRef() const override { return {"sel-linear-golden", "1.0.0"}; }
};

// ---------------------------------------------------------------------
// acceptance 1：四类直线传动目录模板经既有导入校验通道通过（SEL-01/02
// 同一校验器——SEL-CATALOG 校验清单全表＋曲线 v2 量纲词表通道）
// ---------------------------------------------------------------------

/**
 * v2 目录包正例黄金对照（AT-36 选型侧）：validate 空报告→assemble→逐条目
 * 业务模型字段值/missing 显式清单（ERR-01——不伪造数值）/status/曲线引用
 * 逐项对照黄金（generate/ 脚本独立推导）；曲线组含 v2 量纲通道
 * （linear-speed→force、load→power——§17.2 词表扩展的导入面）；旋转面
 * 简表（v2 包既有四表照常装配——旋转链行为零变化的黄金证据）；包内容
 * 身份回填非零且同输入恒同（CON-05/NFR-COR-01）。
 */
TEST_F(SelLinearCatalog, LinearCatalogAssemblesGolden_WP19T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "SEL-01", "SEL-02", "NFR-COR-03"},
                  std::vector<std::string>{"AT-36"});

    const sel::CatalogImporter importer;
    const sel::ParsedCatalogInput input = makeValidInput((*dataset));
    const sel::CatalogManifest manifest = makeValidManifest((*dataset));

    // ① 既有校验通道：v2 包经同一 validate 入口——空报告＝通过（§14.2）。
    const sel::CatalogValidationReport rep = importer.validate(input, manifest);
    ASSERT_TRUE(rep.ok())
        << "v2 正例包应零发现（既有八码族校验清单）——全部发现："
        << [&] {
               std::string all;
               for (const sel::CatalogIssue& i : rep.issues) {
                   all += i.code + "@" + i.file + ":" + std::to_string(i.rowNo)
                       + ":" + i.column + "; ";
               }
               return all;
           }();

    // ② 装配不可变快照（§5.1 流程段）。
    const sel::CatalogPackageSnapshot snap = importer.assemble(input, manifest);
    ASSERT_EQ(snap.manifest.formatVersion, std::string{"2"});
    ASSERT_TRUE(snap.contentIdentity.isValid()) << "包内容身份应回填非零（CON-05）";

    // ③ 直线器件条目逐字段黄金对照（四类器件各一——词表覆盖证明）。
    const tk::JsonValue exp = loadDsJson((*dataset), "expected/linear-catalog-expected.json", false);
    const tk::JsonValue& expDrives = *exp.find("valid")->find("linearDrives");
    ASSERT_EQ(snap.linearDrives.size(), expDrives.items.size());
    ASSERT_EQ(snap.linearDrives.size(), std::size_t{4})
        << "四类直线传动器件模板（滚珠丝杠/齿条/同步带/直线电机）各一";
    for (std::size_t i = 0; i < snap.linearDrives.size(); ++i) {
        const sel::LinearDriveCatalogEntry& d = snap.linearDrives[i];
        const tk::JsonValue& e = expDrives.items[i];
        EXPECT_EQ(d.modelId, str(e, "modelId"));
        EXPECT_EQ(d.vendor, str(e, "vendor"));
        EXPECT_EQ(d.displayName, str(e, "displayName"));
        // 器件类别：词表文本反查枚举（四类封闭词表——§17.2）。
        const std::string kindText = str(e, "kind");
        sel::LinearDriveKind kind = sel::LinearDriveKind::BallScrew;
        for (int k = 0; k < sel::kLinearDriveKindCount; ++k) {
            if (sel::linearDriveKindText(static_cast<sel::LinearDriveKind>(k)) == kindText) {
                kind = static_cast<sel::LinearDriveKind>(k);
            }
        }
        EXPECT_EQ(sel::linearDriveKindText(d.kind), kindText)
            << "器件类别词表（第 " << i << " 条目）";
        EXPECT_EQ(d.ratedForce, num(e, "ratedForce")) << d.modelId << " 额定推力（N）";
        EXPECT_EQ(d.peakForce, num(e, "peakForce")) << d.modelId << " 峰值推力（N）";
        EXPECT_EQ(d.maxLinearSpeed, num(e, "maxLinearSpeed")) << d.modelId << " 最高速度（m/s）";
        EXPECT_EQ(d.ratedPower, num(e, "ratedPower")) << d.modelId << " 额定功率（W）";
        // 可缺失字段：null＝nullopt（missing 显式清单同步断言——ERR-01）。
        const tk::JsonValue* eff = e.find("efficiency");
        ASSERT_EQ(d.efficiency.has_value(), eff != nullptr && !eff->isNull())
            << d.modelId << " efficiency 缺失面";
        if (d.efficiency) { EXPECT_EQ(*d.efficiency, eff->number); }
        const tk::JsonValue* st = e.find("stroke");
        ASSERT_EQ(d.stroke.has_value(), st != nullptr && !st->isNull())
            << d.modelId << " stroke 缺失面";
        if (d.stroke) { EXPECT_EQ(*d.stroke, st->number); }
        EXPECT_EQ(d.mass, num(e, "mass")) << d.modelId << " 质量（kg）";
        EXPECT_EQ(d.mounting.flangeKind, str(*e.find("mounting"), "flangeKind"));
        EXPECT_EQ(d.mounting.shaftKind, str(*e.find("mounting"), "shaftKind"));
        // 曲线引用（分号分词后的引用序与量纲声明）。
        const tk::JsonValue& expRefs = *e.find("curveRefs");
        ASSERT_EQ(d.curves.size(), expRefs.items.size()) << d.modelId << " 曲线引用数";
        for (std::size_t k = 0; k < d.curves.size(); ++k) {
            EXPECT_EQ(d.curves[k].curveId, expRefs.items[k].text);
        }
        // missing 显式清单（cell-empty——不伪造数值）。
        const tk::JsonValue& expMissing = *e.find("missing");
        ASSERT_EQ(d.missing.size(), expMissing.items.size()) << d.modelId << " missing 数";
        for (std::size_t k = 0; k < d.missing.size(); ++k) {
            EXPECT_EQ(d.missing[k].column, str(expMissing.items[k], "column"));
            EXPECT_EQ(d.missing[k].reason, str(expMissing.items[k], "reason"));
        }
        // status 定级（missing 非空＝Partial）。
        const std::string expStatus = str(e, "status");
        EXPECT_EQ(d.status == sel::ValidationStatus::Valid ? "Valid" : "Partial", expStatus)
            << d.modelId << " 校验状态";
        EXPECT_EQ(d.catalog.catalogId, manifest.identity.catalogId)
            << "条目目录身份回填＝manifest.identity（装配契约）";
    }

    // ④ 曲线组黄金对照（v2 量纲通道：linear-speed/load/force——§17.2）。
    const tk::JsonValue& expCurves = *exp.find("valid")->find("curves");
    ASSERT_EQ(snap.curves.size(), expCurves.items.size());
    for (std::size_t i = 0; i < snap.curves.size(); ++i) {
        const sel::PerformanceCurve& c = snap.curves[i];
        const tk::JsonValue& e = expCurves.items[i];
        EXPECT_EQ(c.curveId, str(e, "curveId"));
        EXPECT_EQ(c.xQuantity, str(e, "xQuantity"));
        EXPECT_EQ(c.xUnit, str(e, "xUnit"));
        EXPECT_EQ(c.yQuantity, str(e, "yQuantity"));
        EXPECT_EQ(c.yUnit, str(e, "yUnit"));
        ASSERT_EQ(c.points.size(), e.find("points")->items.size());
        for (std::size_t k = 0; k < c.points.size(); ++k) {
            EXPECT_EQ(c.points[k].x, e.find("points")->items[k].find("x")->number);
            EXPECT_EQ(c.points[k].y, e.find("points")->items[k].find("y")->number);
        }
        EXPECT_TRUE(c.contentIdentity.isValid()) << "曲线内容身份回填（§6.1）";
    }

    // ⑤ 旋转面简表（v2 包既有四表照常装配——旋转链零变化的结构证据）。
    const tk::JsonValue& expMotors = *exp.find("valid")->find("motors");
    ASSERT_EQ(snap.motors.size(), expMotors.items.size());
    for (std::size_t i = 0; i < snap.motors.size(); ++i) {
        EXPECT_EQ(snap.motors[i].modelId, str(expMotors.items[i], "modelId"));
        EXPECT_EQ(snap.motors[i].ratedTorque, num(expMotors.items[i], "ratedTorque"));
        EXPECT_EQ(snap.motors[i].peakTorque, num(expMotors.items[i], "peakTorque"));
        EXPECT_EQ(snap.motors[i].mass, num(expMotors.items[i], "mass"));
    }
    const tk::JsonValue& expGearboxes = *exp.find("valid")->find("gearboxes");
    ASSERT_EQ(snap.gearboxes.size(), expGearboxes.items.size());
    for (std::size_t i = 0; i < snap.gearboxes.size(); ++i) {
        EXPECT_EQ(snap.gearboxes[i].modelId, str(expGearboxes.items[i], "modelId"));
        EXPECT_EQ(snap.gearboxes[i].ratio, num(expGearboxes.items[i], "ratio"));
        EXPECT_EQ(snap.gearboxes[i].efficiency, num(expGearboxes.items[i], "efficiency"));
    }

    // ⑥ 包身份确定性（同输入恒同——NFR-COR-01）＋兼容面照常。
    const sel::CatalogPackageSnapshot snap2 = importer.assemble(input, manifest);
    EXPECT_EQ(snap.contentIdentity, snap2.contentIdentity)
        << "同输入两次装配恒同身份（NFR-COR-01）";
    ASSERT_EQ(snap.compatibility.size(), std::size_t{1})
        << "兼容表照常装配（M-201→G-201）";
}

// ---------------------------------------------------------------------
// acceptance 1：误例逐 issue 黄金对照（校验清单同一口径——逐项可定位）
// ---------------------------------------------------------------------

/**
 * 七个单点变异表的期望校验报告对照（AT-36/SEL-02）：范围违约（峰值推力
 * ＜额定/质量负值）、词表违约（drive_kind）、重复型号、必填缺失、引用
 * 悬空（curve_ref）、单位量纲不符（linear-speed 曲线用 rad/s）、点序乱
 * （§6.3 拒绝不代排序）——逐 issue 码/文件/行/列/型号/比较字段对照黄金
 * （generate/ 脚本按 §5.3/§6.3 文字规则独立推导）。
 */
TEST_F(SelLinearCatalog, LinearCatalogInvalidCasesReportGolden_WP19T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "SEL-02", "NFR-COR-03"},
                  std::vector<std::string>{"AT-36"});

    const sel::CatalogImporter importer;
    const tk::JsonValue exp = loadDsJson((*dataset), "expected/linear-catalog-expected.json", false);
    const tk::JsonValue& cases = *exp.find("invalidCases");
    ASSERT_EQ(cases.items.size(), std::size_t{7}) << "七个单点变异表全检";

    for (const tk::JsonValue& c : cases.items) {
        const std::string name = str(c, "name");
        // 组装变异包：变异表替换目标表，其余取 valid 同款（生成脚本同款
        // 组包语义——单点变异隔离）。
        sel::ParsedCatalogInput input = makeValidInput((*dataset));
        const bool mutatesLinear = name.find("curves") == std::string::npos;
        const std::string mutatedRel = "inputs/invalid/" + name;
        sel::ParsedFileTable mutated = toParsedTable(
            mutatesLinear ? sel::kCatalogFileLinearDrives : sel::kCatalogFileCurves,
            parseGoldenCsv(readDsText((*dataset), mutatedRel.c_str(), true)));
        for (sel::ParsedFileTable& t : input.files) {
            if (t.fileName == mutated.fileName) {
                t = std::move(mutated);
            }
        }
        const sel::CatalogManifest manifest = makeValidManifest((*dataset));

        // 校验报告逐 issue 对照黄金（码/文件/行/列/型号/比较字段）。
        const sel::CatalogValidationReport rep = importer.validate(input, manifest);
        ASSERT_FALSE(rep.ok()) << name << " 应非空报告（误例必拒绝）";
        const tk::JsonValue& expIssues = *c.find("issues");
        ASSERT_EQ(rep.issues.size(), expIssues.items.size())
            << name << " issue 数（黄金逐条对照）";
        for (std::size_t i = 0; i < rep.issues.size(); ++i) {
            const sel::CatalogIssue& got = rep.issues[i];
            const tk::JsonValue& e = expIssues.items[i];
            EXPECT_EQ(got.code, str(e, "code"))
                << name << " issue[" << i << "] 码";
            EXPECT_EQ(got.file, str(e, "file")) << name << " issue[" << i << "] 文件";
            EXPECT_EQ(got.rowNo,
                      static_cast<std::uint64_t>(e.find("rowNo")->number))
                << name << " issue[" << i << "] 行号";
            EXPECT_EQ(got.column, str(e, "column")) << name << " issue[" << i << "] 列";
            EXPECT_EQ(got.modelId, str(e, "modelId")) << name << " issue[" << i << "] 型号";
            // 比较型字段（ERR-01 三要素——按黄金非空面逐项核对）。
            const tk::JsonValue* av = e.find("actualValue");
            if (av != nullptr && av->isNumber()) {
                ASSERT_TRUE(got.actualValue.has_value())
                    << name << " issue[" << i << "] 应携实际值";
                EXPECT_EQ(*got.actualValue, av->number);
            } else {
                EXPECT_FALSE(got.actualValue.has_value())
                    << name << " issue[" << i << "] 不应携实际值";
            }
            const tk::JsonValue* ev = e.find("expectedValue");
            if (ev != nullptr && ev->isNumber()) {
                ASSERT_TRUE(got.expectedValue.has_value())
                    << name << " issue[" << i << "] 应携期望值";
                EXPECT_EQ(*got.expectedValue, ev->number);
            } else {
                EXPECT_FALSE(got.expectedValue.has_value())
                    << name << " issue[" << i << "] 不应携期望值";
            }
            const tk::JsonValue* at = e.find("actualText");
            if (at != nullptr && at->isString()) {
                ASSERT_TRUE(got.actualText.has_value());
                EXPECT_EQ(*got.actualText, at->text) << name << " issue[" << i << "]";
            }
            const tk::JsonValue* et = e.find("expectedText");
            if (et != nullptr && et->isString()) {
                ASSERT_TRUE(got.expectedText.has_value());
                EXPECT_EQ(*got.expectedText, et->text) << name << " issue[" << i << "]";
            }
        }
    }
}

// ---------------------------------------------------------------------
// acceptance 3：v1 输入面零变化与版本分派（V12-03 收窄——旋转链行为
// 不变；未知版本仍拒绝——不自动升级）
// ---------------------------------------------------------------------

/**
 * v1 分派面零变化（WP-19-T12 acceptance 3）：同一 valid 包去第六表＋
 * formatVersion "1"＋字段字典四份→既有通道照常通过、装配 linearDrives
 * 恒空、包身份确定性保持；v1 包多余 linear_drives 表不在必备面（与既有
 * 对未知文件名的处理一致：不取不校验——文件层归 io 清单核对）；未知
 * 版本 "999" 仍 fail-fast 拒绝＋升级指引（不自动升级——PM-06 纪律不变）。
 */
TEST_F(SelLinearCatalog, V1FaceUnchangedAndVersionDispatch_WP19T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "SEL-01", "NFR-DEP-04"},
                  std::vector<std::string>{"AT-36", "AT-08"});

    const sel::CatalogImporter importer;

    // ① v1 包（五表、formatVersion "1"）照常通过且 linearDrives 恒空。
    // 曲线表取 v1 兼容内容（过滤 linear-drive owner 行——真 v1 输入面）。
    sel::ParsedCatalogInput v1Input = makeValidInput((*dataset));
    // v1 解析面：去掉第六表（v1 包本无该文件）＋曲线表过滤直线 owner 行。
    ASSERT_EQ(v1Input.files.size(), std::size_t{5});
    v1Input.files.pop_back();
    for (sel::ParsedFileTable& t : v1Input.files) {
        if (t.fileName == sel::kCatalogFileCurves) {
            t = makeV1CurveTable((*dataset));
        }
    }
    sel::CatalogManifest v1Manifest = makeValidManifest((*dataset));
    v1Manifest.formatVersion = "1";
    v1Manifest.files.pop_back();   // 文件清单同步去 linear_drives 条目
    ASSERT_EQ(v1Manifest.fieldDictionary.size(), std::size_t{5});
    v1Manifest.fieldDictionary.pop_back();   // 字段字典同步去 linear_drives 份

    const sel::CatalogValidationReport v1Rep = importer.validate(v1Input, v1Manifest);
    ASSERT_TRUE(v1Rep.ok()) << "v1 面应零发现（既有通道零变化）——全部发现："
                            << [&] {
                                   std::string all;
                                   for (const sel::CatalogIssue& i : v1Rep.issues) {
                                       all += i.code + "@" + i.file + ":"
                                           + std::to_string(i.rowNo) + ":" + i.column + "; ";
                                   }
                                   return all;
                               }();
    const sel::CatalogPackageSnapshot v1Snap = importer.assemble(v1Input, v1Manifest);
    EXPECT_TRUE(v1Snap.linearDrives.empty()) << "v1 快照直线表恒空（v1 行为零变化）";
    EXPECT_EQ(v1Snap.motors.size(), std::size_t{1}) << "v1 旋转面照常装配";
    EXPECT_TRUE(v1Snap.contentIdentity.isValid());
    // v1 身份确定性（同输入恒同）。
    EXPECT_EQ(v1Snap.contentIdentity, importer.assemble(v1Input, v1Manifest).contentIdentity);

    // ② v1 包多余 linear_drives 表：不取不校验（selection 对必备面之外
    //    的文件名与既有行为一致——文件层归 io 清单核对，卡 §5.1 分工）。
    sel::ParsedCatalogInput extraInput = makeValidInput((*dataset));   // 六表全量
    for (sel::ParsedFileTable& t : extraInput.files) {
        if (t.fileName == sel::kCatalogFileCurves) {
            t = makeV1CurveTable((*dataset));   // 曲线表同为 v1 兼容内容
        }
    }
    const sel::CatalogValidationReport extraRep = importer.validate(extraInput, v1Manifest);
    EXPECT_TRUE(extraRep.ok()) << "v1 分派不消费第六表（多余表不进校验面）";

    // ③ 未知版本仍拒绝（升级指引随异常消息——PM-06 不自动升级）。
    sel::CatalogManifest badManifest = makeValidManifest((*dataset));
    badManifest.formatVersion = "999";
    bool thrown = false;
    try {
        static_cast<void>(importer.validate(makeValidInput((*dataset)), badManifest));
        FAIL() << "未知版本应 schema 级拒绝";
    } catch (const std::invalid_argument& e) {
        thrown = true;
        EXPECT_NE(std::string(e.what()).find("999"), std::string::npos)
            << "升级指引携带版本号";
    }
    EXPECT_TRUE(thrown);
}
