/**
 * @file   SelGoldenDatasetTest.cpp
 * @brief  selection 黄金数据集消费套件（SelGolden*）——WP-19-T11 主交付面：
 *         testdata/golden/sel-* 四数据集经 testkit 黄金设施（GoldenFixture＋
 *         DatasetManifest 完整性＋ToleranceProfile 档案容差）消费。期望值来自
 *         数据文件（各 generate/*.mjs 独立参考实现产物——按单元卡冻结语义
 *         封闭推导，与产品实现零共享代码），覆盖：
 *   - SelGoldenCatalog：目录包正/误样例（AT-08 导入面——validate 报告黄金＋
 *     assemble 装配黄金＋InMemoryCatalogProvider 版本锁定语义）；
 *   - SelGoldenScreening：可行/不可行型号黄金表（AT-08 硬筛选面——每个淘汰
 *     项含实际值/阈值/单位/阈值来源＋SEL-09 范围外面＋SF 恰好满足边界）；
 *   - SelGoldenCombo：组合校核黄金算例（SEL-05——映射事实黄金联动 c=1/n＋
 *     格聚合＋质量核算＋惯量比未判定态）与可行集组装闭环（AT-08"可行组合"）；
 *   - SelGoldenBackfill：回填事务样例（AT-30——§12.4 合成黄金经容差档案＋
 *     复算提示＋组装拒绝分类）。
 *
 * 设计依据：
 *   - units/selection.md §15.1（黄金数据集 sel-*——目录包正/误样例、可行/
 *     不可行黄金表、组合校核黄金算例、回填事务样例）、§15.3（核心筛选和
 *     组合校核优先使用模型测试＋黄金数据集——计算库零 Qt 直调）、§18（AT-08/
 *     AT-30 验证位置＝选型黄金数据）
 *   - units/testkit.md §6.1（GoldenFixture 六步生命周期）、§4.2/§4.5（Dataset-
 *     Manifest 完整性）、§4.3（容差档案）
 *   - 需求 SEL-03/SEL-05/SEL-06/SEL-10（任务契约 requirements）、NFR-COR-01/
 *     02/03、ERR-01、AT-08/AT-30
 *   - 任务契约 tasks/foundation/WP-19-T11.json acceptance 1/2
 *
 * 测试策略（黄金数据独立性——analytic-case 的 lint 义务）：期望值全部由数据集
 * generate/ 脚本按单元卡冻结语义独立推导（与产品 C++ 实现零共享代码），本文件
 * 只做"装载→驱动产品入口→黄金对照"三件事（不书写参考判定公式——inputs 内
 * 手算锚点除外，锚点独立于参考实现双路防"两路同错"）。驱动入口＝产品公共
 * 接口（ICatalogValidator/ICatalogProvider/IHardConstraintSelector/
 * checkCombinations/IFeasibleSetBuilder/assembleDeviceBackfill/
 * IDeviceBackfillCommandHandler——经公共类消费钉扎交付面，接口路径全覆盖，
 * WP-20-T03 接口路径零覆盖教训的正面落实）。
 *
 * 线程约束：gtest 用例天然串行；被测入口全部纯函数（卡 §14.10）。
 */

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/selection/Backfill.hpp>
#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>
#include <sdurws/ird/selection/Curve.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/FeasibleSet.hpp>
#include <sdurws/ird/selection/Screening.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sel = sdurws::ird::selection;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;
namespace project = sdurws::ird::project;
// IRD_EXPECT_* 宏展开为无命名空间限定的 checkCloseWithin 等标识符
// （AssertMacros 契约——宏仅在"已开 testkit 可见性"的消费 TU 使用）。
using namespace sdurws::ird::testkit;

// =====================================================================
// 黄金 JSON/CSV 读取脚手架（数据集装载/完整性已由 GoldenFixture 校验；此处
// 只做字段提取——字段缺失记失败并回退安全值，防越界崩溃掩盖首因）
// =====================================================================

namespace {

/// 读数据集文本文件（inputs/expected 侧相对路径经 dataset 解析——路径必须
/// 登记于 manifest，越界抛 TestKitError）。
std::string readDatasetText(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
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

tk::JsonValue loadJson(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    return tk::parseJson(readDatasetText(ds, rel, inputSide));
}

/// 数值字段（缺失记失败并回退 0——调用方继续走安全路径）。
double num(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isNumber()) {
        ADD_FAILURE() << "黄金数据缺数值字段: " << key;
        return 0.0;
    }
    return v->number;
}

/// 字符串字段（缺失记失败并回退空串）。
std::string str(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isString()) {
        ADD_FAILURE() << "黄金数据缺字符串字段: " << key;
        return {};
    }
    return v->text;
}

/// 布尔字段（缺失记失败并回退 false）。
bool boolOf(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || !v->isBool()) {
        ADD_FAILURE() << "黄金数据缺布尔字段: " << key;
        return false;
    }
    return v->boolean;
}

/// 可缺数值字段（nullopt 面——黄金数据 null → nullopt）。
std::optional<double> optNum(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || v->isNull()) {
        return std::nullopt;
    }
    if (!v->isNumber()) {
        ADD_FAILURE() << "黄金数据数值字段类型不符: " << key;
        return std::nullopt;
    }
    return v->number;
}

// ---- CSV 解析（测试侧扮演 io 映射半区——方言标识行＋表头＋数据行）----

struct CsvTable {
    std::vector<std::string> header;                 ///< 表头（第 2 物理行）
    std::vector<std::vector<std::string>> rows;      ///< 数据行（自第 3 物理行）
    std::vector<std::string> cellsRowMajor() const   ///< row-major 展开（ParsedFileTable 契约）
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

/// 解析黄金 CSV（首行＝方言标识 ird-catalog-v1、次行＝表头、数据自物理行 3
/// 起——与 sel-catalog-golden 生成脚本的序列化契约一致；单元格无引号转义）。
CsvTable parseGoldenCsv(const std::string& text)
{
    CsvTable t;
    std::istringstream in(text);
    std::string line;
    // 黄金 CSV 是登记过的数据资产——结构违约按数据缺陷抛（非 void 辅助函数
    // 禁用 gtest 断言宏；异常在测试体内传播即失败，gtest 语义）。
    if (!static_cast<bool>(std::getline(in, line))) {
        throw std::runtime_error("黄金 CSV 空文件");
    }
    if (line != "ird-catalog-v1") {
        throw std::runtime_error("黄金 CSV 方言标识行不符");
    }
    if (!static_cast<bool>(std::getline(in, line))) {
        throw std::runtime_error("黄金 CSV 缺表头行");
    }
    std::stringstream hs(line);
    std::string col;
    while (std::getline(hs, col, ',')) {
        t.header.push_back(col);
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
        // 行尾空列（io PadTrailing 语义——空单元格在行尾时 split 不产出）。
        while (row.size() < t.header.size()) {
            row.push_back("");
        }
        if (row.size() != t.header.size()) {
            throw std::runtime_error("黄金 CSV 行列数不符: " + line);
        }
        t.rows.push_back(std::move(row));
    }
    return t;
}

/// 由黄金 CSV 构造解析表（firstDataRowNo＝3——含方言标识行＋表头行偏移）。
sel::ParsedFileTable toParsedTable(const std::string& fileName, const CsvTable& t)
{
    sel::ParsedFileTable p;
    p.fileName = fileName;
    p.header = t.header;
    p.columnCount = t.header.size();
    p.firstDataRowNo = 3;
    p.cells = t.cellsRowMajor();
    return p;
}

// ---- 黄金目录 → 业务模型（screening/combo/backfill 的 inputs JSON 承载）----

/// 电机目录条目（sel-screening inputs 黄金业务模型——字段直取）。
sel::MotorCatalogEntry motorFromJson(const tk::JsonValue& o, const sel::CatalogIdentity& catalog)
{
    sel::MotorCatalogEntry m;
    m.modelId = str(o, "modelId");
    m.catalog = catalog;
    m.ratedTorque = num(o, "ratedTorque");
    m.peakTorque = num(o, "peakTorque");
    m.ratedSpeed = num(o, "ratedSpeed");
    m.maxSpeed = num(o, "maxSpeed");
    m.ratedPower = num(o, "ratedPower");
    if (const tk::JsonValue* ov = o.find("overload"); ov != nullptr && !ov->isNull()) {
        m.overload = sel::OverloadSpec{num(*ov, "torque"), num(*ov, "duration")};
    }
    // dutyClass（可缺——combo inputs 不携工作制面且其 criteria 不消费该维度；
    // 缺失留空＝组合校核快照该维度不触达）。
    if (const tk::JsonValue* d = o.find("dutyClass"); d != nullptr && d->isString()) {
        m.dutyClass = d->text;
    }
    m.ratedVoltage = optNum(o, "ratedVoltage");
    if (const tk::JsonValue* th = o.find("thermal"); th != nullptr && !th->isNull()) {
        m.thermal = sel::ThermalDerating{num(*th, "refTemp"), num(*th, "factorPerRef")};
    }
    m.brakeTorque = optNum(o, "brakeTorque");
    m.holdingTorque = optNum(o, "holdingTorque");
    m.rotorInertia = num(o, "rotorInertia");
    m.mass = num(o, "mass");
    // mounting 词表值由 screening inputs 承载（combo inputs 不含安装面——组合
    // 校核 criteria 全不启用不消费安装维度，条目留空即维度缺数据面、不触达）。
    if (const tk::JsonValue* fl = o.find("flangeKind"); fl != nullptr && fl->isString()) {
        m.mounting.flangeKind = fl->text;
    }
    if (const tk::JsonValue* sh = o.find("shaftKind"); sh != nullptr && sh->isString()) {
        m.mounting.shaftKind = sh->text;
    }
    // 曲线引用（powerCurveId 非空＝条目声明 speed→power 曲线——功率维度曲线
    // 口径消费的前提；组合校核 snapshot 与筛选快照同函数装配，单一来源）。
    if (const tk::JsonValue* pc = o.find("powerCurveId");
        pc != nullptr && pc->isString() && !pc->text.empty()) {
        m.curves.push_back(sel::CurveRef{pc->text, "speed", "power"});
    }
    m.status = sel::ValidationStatus::Valid;
    return m;
}

sel::GearboxCatalogEntry gearboxFromJson(const tk::JsonValue& o, const sel::CatalogIdentity& catalog)
{
    sel::GearboxCatalogEntry g;
    g.modelId = str(o, "modelId");
    g.catalog = catalog;
    g.ratedOutputTorque = num(o, "ratedOutputTorque");
    g.peakOutputTorque = num(o, "peakOutputTorque");
    g.maxInputSpeed = num(o, "maxInputSpeed");
    g.ratio = num(o, "ratio");
    g.efficiency = num(o, "efficiency");
    g.backlash = optNum(o, "backlash");
    g.ratedLife = optNum(o, "ratedLife");
    if (const tk::JsonValue* mo = o.find("mountingOrientation"); mo != nullptr && mo->isString()) {
        g.mountingOrientation = mo->text;
    }
    if (const tk::JsonValue* ex = o.find("extLoad"); ex != nullptr && !ex->isNull()) {
        g.extLoad = sel::ExternalLoadSpec{num(*ex, "radial"), num(*ex, "axial"),
                                          num(*ex, "distance")};
    }
    g.mass = num(o, "mass");
    if (const tk::JsonValue* hi = o.find("housingInertia"); hi != nullptr && !hi->isNull()) {
        g.housingInertia = hi->number;
    }
    if (const tk::JsonValue* fl = o.find("flangeKind"); fl != nullptr && fl->isString()) {
        g.mounting.flangeKind = fl->text;
    }
    if (const tk::JsonValue* sh = o.find("shaftKind"); sh != nullptr && sh->isString()) {
        g.mounting.shaftKind = sh->text;
    }
    g.status = sel::ValidationStatus::Valid;
    return g;
}

/// 筛选条件（黄金 criteria JSON——nullopt/空串＝维度不适用）。
sel::ScreeningCriteria criteriaFromJson(const tk::JsonValue& o)
{
    sel::ScreeningCriteria c;
    c.safetyFactor = num(o, "safetyFactor");
    if (const tk::JsonValue* d = o.find("requiredDutyClass"); d != nullptr && d->isString()) {
        c.requiredDutyClass = d->text;
    }
    c.requiredVoltage = optNum(o, "requiredVoltage");
    if (const tk::JsonValue* vt = o.find("voltageRelativeTolerance"); vt != nullptr && vt->isNumber()) {
        c.voltageRelativeTolerance = vt->number;
    }
    c.ambientTemp = optNum(o, "ambientTemp");
    c.maxBacklash = optNum(o, "maxBacklash");
    c.requiredLife = optNum(o, "requiredLife");
    c.minEfficiency = optNum(o, "minEfficiency");
    if (const tk::JsonValue* rr = o.find("ratioRange"); rr != nullptr && !rr->isNull()) {
        c.ratioRange = sel::RatioRange{num(*rr, "min"), num(*rr, "max")};
    }
    return c;
}

/// 单轴工作点事实（黄金 facts JSON——nullopt＝未供给；关节侧/电机侧分组）。
sel::AxisWorkpointFacts factsFromJson(const tk::JsonValue& o, const core::ObjectId& axis)
{
    sel::AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = str(o, "caseId");
    f.jointKind = str(o, "jointKind") == "prismatic" ? sel::JointKind::Prismatic
                                                     : sel::JointKind::Revolute;
    f.jointTorqueRms = optNum(o, "jointTorqueRms");
    f.jointTorquePeak = optNum(o, "jointTorquePeak");
    f.jointSpeedPeak = optNum(o, "jointSpeedPeak");
    f.motorTorqueRms = optNum(o, "motorTorqueRms");
    f.motorTorquePeak = optNum(o, "motorTorquePeak");
    f.motorSpeedPeak = optNum(o, "motorSpeedPeak");
    f.motorSpeedRms = optNum(o, "motorSpeedRms");
    f.motorPowerPeak = optNum(o, "motorPowerPeak");
    f.motorPowerRms = optNum(o, "motorPowerRms");
    f.peakDuration = optNum(o, "peakDuration");
    f.requiredHoldingTorque = optNum(o, "requiredHoldingTorque");
    if (const tk::JsonValue* ex = o.find("externalLoad"); ex != nullptr && !ex->isNull()) {
        f.externalLoad = sel::ExternalLoadFacts{num(*ex, "radial"), num(*ex, "axial"),
                                                num(*ex, "distance")};
    }
    if (const tk::JsonValue* mr = o.find("mountRequirement"); mr != nullptr && !mr->isNull()) {
        sel::JointMountRequirement req;
        req.flangeKind = str(*mr, "flangeKind");
        req.shaftKind = str(*mr, "shaftKind");
        req.orientation = str(*mr, "orientation");
        f.mountRequirement = req;
    }
    f.atTime = num(o, "atTime");
    f.segmentId = str(o, "segmentId");
    return f;
}

// ---- 黄金对照辅助（离散面精确断言——集中书写防漏字段）----

/// 逐条淘汰原因对照（token/caseId/atTime/actual/required/unit/thresholdSource
/// ——ERR-01 四要素全字段；actual/required 为工作点/能力值直取＝精确相等）。
void expectReasonGolden(const char* ctx, const sel::RejectionReason& r,
                        const tk::JsonValue& e)
{
    SCOPED_TRACE(ctx);
    EXPECT_EQ(r.token, static_cast<sel::ReasonToken>(static_cast<int>(num(e, "tokenOrdinal"))))
        << "原因 token 序不符";
    EXPECT_EQ(r.caseId, str(e, "caseId"));
    EXPECT_DOUBLE_EQ(r.atTime, num(e, "atTime"));
    EXPECT_DOUBLE_EQ(r.actual, num(e, "actual")) << "实际值不符（ERR-01）";
    EXPECT_DOUBLE_EQ(r.required, num(e, "required")) << "阈值不符（ERR-01）";
    EXPECT_EQ(r.unit, str(e, "unit"));
    EXPECT_EQ(r.thresholdSource, str(e, "thresholdSource")) << "阈值来源词面不符";
    // 文本类比较（工作制/安装方向——actualText/requiredText 承载）。
    if (const tk::JsonValue* at = e.find("actualText"); at != nullptr && at->isString()) {
        EXPECT_EQ(r.actualText, at->text);
        EXPECT_EQ(r.requiredText, str(e, "requiredText"));
    }
}

/// 逐条数据缺口对照（dimension/caseId/diagCode）。
void expectGapGolden(const char* ctx, const sel::DataGap& g, const tk::JsonValue& e)
{
    SCOPED_TRACE(ctx);
    EXPECT_EQ(g.dimension, str(e, "dimension"));
    EXPECT_EQ(g.caseId, str(e, "caseId"));
    std::string diagCode;
    if (const tk::JsonValue* dc = e.find("diagCode"); dc != nullptr && dc->isString()) {
        diagCode = dc->text;
    }
    EXPECT_EQ(g.diagCode, diagCode);
}

/// 回填组装源构造（拒绝算例/确定性算例共用——黄金源 JSON → AxisBackfillSource；
/// 匿名命名空间自由函数——非夹具成员，供 SelGoldenBackfill 各用例直调）。
sel::AxisBackfillSource makeSource(const tk::JsonValue& src, const core::ObjectId& axis)
{
    sel::AxisBackfillSource s;
    s.jointId = axis;
    s.motorModelId = str(src, "motorModelId");
    s.gearboxModelId = str(src, "gearboxModelId");
    s.mountKind = str(src, "mountKind");
    s.linkMassKg = num(src, "linkMassKg");
    s.linkComM = {num(*src.find("linkComM"), "x"), num(*src.find("linkComM"), "y"),
                  num(*src.find("linkComM"), "z")};
    const tk::JsonValue& li = *src.find("linkInertia");
    s.linkInertia = {num(li, "ixx"), num(li, "iyy"), num(li, "izz"),
                     num(li, "ixy"), num(li, "ixz"), num(li, "iyz")};
    s.motorComAnchorM = {num(*src.find("motorComAnchorM"), "x"),
                         num(*src.find("motorComAnchorM"), "y"),
                         num(*src.find("motorComAnchorM"), "z")};
    if (const tk::JsonValue* mi = src.find("motorHousingInertiaSupplement");
        mi != nullptr && !mi->isNull()) {
        s.motorHousingInertiaSupplement =
            sel::BackfillInertiaTensor{num(*mi, "ixx"), num(*mi, "iyy"), num(*mi, "izz"),
                                       num(*mi, "ixy"), num(*mi, "ixz"), num(*mi, "iyz")};
    }
    s.gearboxComAnchorM = {num(*src.find("gearboxComAnchorM"), "x"),
                           num(*src.find("gearboxComAnchorM"), "y"),
                           num(*src.find("gearboxComAnchorM"), "z")};
    s.appliedRatio = num(src, "appliedRatio");
    return s;
}

}  // namespace

// =====================================================================
// SelGoldenCatalog——目录包正/误样例（AT-08 导入面）
// =====================================================================

/// 数据集引用（四数据集统一 1.0.0 版本基线）。
class SelGoldenCatalog : public tk::GoldenFixture {
protected:
    tk::DatasetRef datasetRef() const override { return {"sel-catalog-golden", "1.0.0"}; }

    /// 正例五表 → 解析输入（消费测试扮演 io 映射半区——L5 装配前的直构形态）。
    sel::ParsedCatalogInput validInput()
    {
        sel::ParsedCatalogInput in;
        in.files = {
            toParsedTable("motors.csv", parseGoldenCsv(
                readDatasetText(*dataset, "inputs/valid/motors.csv", true))),
            toParsedTable("gearboxes.csv", parseGoldenCsv(
                readDatasetText(*dataset, "inputs/valid/gearboxes.csv", true))),
            toParsedTable("capability_curves.csv", parseGoldenCsv(
                readDatasetText(*dataset, "inputs/valid/capability_curves.csv", true))),
            toParsedTable("compatibility.csv", parseGoldenCsv(
                readDatasetText(*dataset, "inputs/valid/compatibility.csv", true))),
        };
        return in;
    }

    /// 目录包 manifest（io JSON 通道投影——解析回 CatalogManifest）。
    sel::CatalogManifest packageManifest()
    {
        const tk::JsonValue o = loadJson(*dataset, "inputs/valid/manifest.json", true);
        sel::CatalogManifest m;
        m.formatVersion = str(o, "formatVersion");
        const tk::JsonValue& id = *o.find("identity");
        m.identity.catalogId = str(id, "catalogId");
        m.identity.version = str(id, "version");
        m.identity.source = str(id, "source");
        // contentIdentity：装配入口按快照规范序列化计算回填（§4.2）——输入侧
        // 全零占位（黄金数据不伪造；测试断言装配回填非零）。
        const tk::JsonValue& files = *o.find("files");
        for (const tk::JsonValue& f : files.items) {
            sel::ManifestEntry e;
            e.fileName = str(f, "fileName");
            e.role = str(f, "role");
            e.required = boolOf(f, "required");
            e.sha256Hex = str(f, "sha256Hex");
            m.files.push_back(e);
        }
        const tk::JsonValue& dicts = *o.find("fieldDictionary");
        for (const tk::JsonValue& d : dicts.items) {
            sel::FieldDictionary fd;
            fd.targetFile = str(d, "targetFile");
            for (const tk::JsonValue& fs : d.find("fields")->items) {
                sel::FieldSpec s;
                s.column = str(fs, "column");
                s.semantic = str(fs, "semantic");
                s.unit = str(fs, "unit");
                s.required = boolOf(fs, "required");
                fd.fields.push_back(s);
            }
            m.fieldDictionary.push_back(fd);
        }
        return m;
    }
};

/**
 * AT-08 主链路面①（目录导入）：正例包 validate 零报告 → assemble 黄金装配
 * ——逐条目字段值/missing 显式清单/status 定级/装配确定性序/包内容身份回填
 * （§4.1/§5.1/§4.2 冻结语义；expected 由独立参考实现封闭推导）。
 */
TEST_F(SelGoldenCatalog, CatalogImportAssemblesGolden_WP19T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-01", "SEL-02", "NFR-COR-01"}),
                  std::vector<std::string>{"AT-08"});

    const sel::CatalogManifest manifest = packageManifest();
    const sel::ParsedCatalogInput input = validInput();

    // ---- 校验段：正例零报告（§14.2"空报告＝通过"）。
    const sel::CatalogImporter importer;
    sel::CatalogValidationReport report;
    EXPECT_NO_THROW(report = importer.validate(input, manifest));
    EXPECT_TRUE(report.ok()) << "正例包不应有校验发现，首条: "
                             << (report.issues.empty() ? ""
                                 : report.issues.front().code + "@" + report.issues.front().file);

    // ---- 装配段：黄金对照（条目值/missing/status/序——expected.valid）。
    const sel::CatalogPackageSnapshot snap = importer.assemble(input, manifest);
    const tk::JsonValue exp = loadJson(*dataset, "expected/catalog-expected.json", false);
    const tk::JsonValue& valid = *exp.find("valid");

    // 电机条目（装配序＝modelId 升序——黄金逐位对照）。
    const tk::JsonValue& expMotors = *valid.find("motors");
    ASSERT_EQ(snap.motors.size(), expMotors.items.size());
    for (std::size_t i = 0; i < expMotors.items.size(); ++i) {
        const tk::JsonValue& e = expMotors.items[i];
        const sel::MotorCatalogEntry& m = snap.motors[i];
        SCOPED_TRACE(std::string("motor[") + std::to_string(i) + "]");
        EXPECT_EQ(m.modelId, str(e, "modelId"));
        EXPECT_DOUBLE_EQ(m.ratedTorque, num(e, "ratedTorque"));
        EXPECT_DOUBLE_EQ(m.peakTorque, num(e, "peakTorque"));
        EXPECT_DOUBLE_EQ(m.ratedSpeed, num(e, "ratedSpeed"));
        EXPECT_DOUBLE_EQ(m.maxSpeed, num(e, "maxSpeed"));
        EXPECT_DOUBLE_EQ(m.ratedPower, num(e, "ratedPower"));
        EXPECT_EQ(m.dutyClass, str(e, "dutyClass"));
        EXPECT_DOUBLE_EQ(m.rotorInertia, num(e, "rotorInertia"));
        EXPECT_DOUBLE_EQ(m.mass, num(e, "mass"));
        // 安装接口词表值（装配面——与黄金 mounting 对象对照）。
        EXPECT_EQ(m.mounting.flangeKind, str(*e.find("mounting"), "flangeKind"));
        EXPECT_EQ(m.mounting.shaftKind, str(*e.find("mounting"), "shaftKind"));
        // 过载/电压/温度/制动/保持（optional 面——nullopt 对齐黄金 null）。
        if (const tk::JsonValue* ov = e.find("overload"); ov != nullptr && !ov->isNull()) {
            ASSERT_TRUE(m.overload.has_value());
            EXPECT_DOUBLE_EQ(m.overload->torque, num(*ov, "torque"));
            EXPECT_DOUBLE_EQ(m.overload->duration, num(*ov, "duration"));
        } else {
            EXPECT_FALSE(m.overload.has_value());
        }
        EXPECT_EQ(m.ratedVoltage, optNum(e, "ratedVoltage"));
        if (const tk::JsonValue* th = e.find("thermal"); th != nullptr && !th->isNull()) {
            ASSERT_TRUE(m.thermal.has_value());
            EXPECT_DOUBLE_EQ(m.thermal->refTemp, num(*th, "refTemp"));
            EXPECT_DOUBLE_EQ(m.thermal->factorPerRef, num(*th, "factorPerRef"));
        } else {
            EXPECT_FALSE(m.thermal.has_value());
        }
        EXPECT_EQ(m.brakeTorque, optNum(e, "brakeTorque"));
        EXPECT_EQ(m.holdingTorque, optNum(e, "holdingTorque"));
        // 曲线引用（curve_ref 分号分隔——落位细化 T03 ⑦）。
        ASSERT_EQ(m.curves.size(), e.find("curveRefs")->items.size());
        // missing 清单（cell-empty 显式标记——ERR-01 不伪造数值）与 status。
        const tk::JsonValue& expMissing = *e.find("missing");
        ASSERT_EQ(m.missing.size(), expMissing.items.size());
        for (std::size_t k = 0; k < expMissing.items.size(); ++k) {
            EXPECT_EQ(m.missing[k].column, str(expMissing.items[k], "column"));
            EXPECT_EQ(m.missing[k].reason, str(expMissing.items[k], "reason"));
        }
        EXPECT_EQ(m.status == sel::ValidationStatus::Valid ? "Valid" : "Partial",
                  str(e, "status"));
    }

    // 减速器条目（关键字段＋optional 面——外载荷/回隙/寿命/壳体惯量）。
    const tk::JsonValue& expGearboxes = *valid.find("gearboxes");
    ASSERT_EQ(snap.gearboxes.size(), expGearboxes.items.size());
    for (std::size_t i = 0; i < expGearboxes.items.size(); ++i) {
        const tk::JsonValue& e = expGearboxes.items[i];
        const sel::GearboxCatalogEntry& g = snap.gearboxes[i];
        SCOPED_TRACE(std::string("gearbox[") + std::to_string(i) + "]");
        EXPECT_EQ(g.modelId, str(e, "modelId"));
        EXPECT_DOUBLE_EQ(g.ratedOutputTorque, num(e, "ratedOutputTorque"));
        EXPECT_DOUBLE_EQ(g.peakOutputTorque, num(e, "peakOutputTorque"));
        EXPECT_DOUBLE_EQ(g.maxInputSpeed, num(e, "maxInputSpeed"));
        EXPECT_DOUBLE_EQ(g.ratio, num(e, "ratio"));
        EXPECT_DOUBLE_EQ(g.efficiency, num(e, "efficiency"));
        EXPECT_EQ(g.backlash, optNum(e, "backlash"));
        EXPECT_EQ(g.ratedLife, optNum(e, "ratedLife"));
        EXPECT_EQ(g.mountingOrientation, str(e, "mountingOrientation"));
        EXPECT_DOUBLE_EQ(g.mass, num(e, "mass"));
        if (const tk::JsonValue* ex = e.find("extLoad"); ex != nullptr && !ex->isNull()) {
            ASSERT_TRUE(g.extLoad.has_value());
            EXPECT_DOUBLE_EQ(g.extLoad->radial, num(*ex, "radial"));
            EXPECT_DOUBLE_EQ(g.extLoad->axial, num(*ex, "axial"));
            EXPECT_DOUBLE_EQ(g.extLoad->distance, num(*ex, "distance"));
        } else {
            EXPECT_FALSE(g.extLoad.has_value());
        }
        const tk::JsonValue& expMissing = *e.find("missing");
        ASSERT_EQ(g.missing.size(), expMissing.items.size());
        EXPECT_EQ(g.status == sel::ValidationStatus::Valid ? "Valid" : "Partial",
                  str(e, "status"));
    }

    // 曲线点集（curveId 升序；点集 x/y 逐点——§6.1 模型）。
    const tk::JsonValue& expCurves = *valid.find("curves");
    ASSERT_EQ(snap.curves.size(), expCurves.items.size());
    for (std::size_t i = 0; i < expCurves.items.size(); ++i) {
        const tk::JsonValue& e = expCurves.items[i];
        const sel::PerformanceCurve& c = snap.curves[i];
        EXPECT_EQ(c.curveId, str(e, "curveId"));
        ASSERT_EQ(c.points.size(), e.find("points")->items.size());
        for (std::size_t k = 0; k < e.find("points")->items.size(); ++k) {
            EXPECT_DOUBLE_EQ(c.points[k].x, num(e.find("points")->items[k], "x"));
            EXPECT_DOUBLE_EQ(c.points[k].y, num(e.find("points")->items[k], "y"));
        }
    }

    // 兼容表（motorId→gearboxId→mountKind 升序）。
    const tk::JsonValue& expCompat = *valid.find("compatibility");
    ASSERT_EQ(snap.compatibility.size(), expCompat.items.size());
    for (std::size_t i = 0; i < expCompat.items.size(); ++i) {
        EXPECT_EQ(snap.compatibility[i].motorId, str(expCompat.items[i], "motorId"));
        EXPECT_EQ(snap.compatibility[i].gearboxId, str(expCompat.items[i], "gearboxId"));
        EXPECT_EQ(snap.compatibility[i].mountKind, str(expCompat.items[i], "mountKind"));
    }

    // ---- 包内容身份（§4.2——装配入口计算回填：非零＋同输入恒同值＋与字段
    //      面一致〔任何业务字段变更必得新身份——CON-05 内容寻址〕）。
    EXPECT_TRUE(snap.contentIdentity.isValid());
    const sel::CatalogPackageSnapshot snap2 = importer.assemble(input, manifest);
    EXPECT_EQ(snap.contentIdentity, snap2.contentIdentity) << "同输入恒同包内容身份（NFR-COR-02）";
    EXPECT_EQ(snap.manifest.identity.contentIdentity, snap.contentIdentity)
        << "manifest 身份回填＝快照身份（§4.2 装配契约）";
}

/**
 * AT-08 主链路面②（错误字段）：六误例逐个 validate——期望校验报告逐 issue
 * 对照（码/文件/物理行/列/型号/比较字段——§5.3/§6.3 逐项定位黄金）。
 */
TEST_F(SelGoldenCatalog, CatalogInvalidCasesReportGolden_WP19T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-02", "NFR-COR-03"}),
                  std::vector<std::string>{"AT-08"});

    const sel::CatalogManifest manifest = packageManifest();
    // 误例表 → 被替换的目标文件名（文件名以 CSV 内容契约为准——误例文件名
    // 仅是落盘名，校验报告的 file 字段＝表内契约文件名）。
    const std::vector<std::pair<const char*, const char*>> invalidTables = {
        {"inputs/invalid/range-motors.csv", "motors.csv"},
        {"inputs/invalid/duplicate-motors.csv", "motors.csv"},
        {"inputs/invalid/dangling-curves.csv", "capability_curves.csv"},
        {"inputs/invalid/unordered-curves.csv", "capability_curves.csv"},
        {"inputs/invalid/missing-motors.csv", "motors.csv"},
        {"inputs/invalid/unit-curves.csv", "capability_curves.csv"},
    };
    const tk::JsonValue exp = loadJson(*dataset, "expected/catalog-expected.json", false);
    const tk::JsonValue& expCases = *exp.find("invalidCases");
    ASSERT_EQ(invalidTables.size(), expCases.items.size());

    const sel::CatalogImporter importer;
    for (std::size_t c = 0; c < invalidTables.size(); ++c) {
        const tk::JsonValue& expCase = expCases.items[c];
        SCOPED_TRACE(str(expCase, "id"));
        // 组装误例输入：正例四表＋被替换的变异表。
        sel::ParsedCatalogInput in = validInput();
        const bool replaced = std::any_of(in.files.begin(), in.files.end(),
            [&](const sel::ParsedFileTable& t) {
                return t.fileName == invalidTables[c].second;
            });
        ASSERT_TRUE(replaced) << "误例目标表不在正例输入内: " << invalidTables[c].second;
        for (sel::ParsedFileTable& t : in.files) {
            if (t.fileName == invalidTables[c].second) {
                t = toParsedTable(t.fileName, parseGoldenCsv(
                    readDatasetText(*dataset, invalidTables[c].first, true)));
            }
        }
        // 校验：数据类错误入报告不抛（§14.0 错误二分）。
        sel::CatalogValidationReport report;
        EXPECT_NO_THROW(report = importer.validate(in, manifest));
        EXPECT_FALSE(report.ok()) << "误例必须被拒绝";
        // 逐 issue 黄金对照（报告稳定序＝(file,rowNo,column,code)——§14.2）。
        const tk::JsonValue& expIssues = *expCase.find("issues");
        ASSERT_EQ(report.issues.size(), expIssues.items.size())
            << "误例 issue 数不符（golden id=" << str(expCase, "id") << "）";
        for (std::size_t i = 0; i < expIssues.items.size(); ++i) {
            const tk::JsonValue& e = expIssues.items[i];
            const sel::CatalogIssue& iss = report.issues[i];
            SCOPED_TRACE(std::string("issue[") + std::to_string(i) + "]");
            EXPECT_EQ(iss.code, str(e, "code"));
            EXPECT_EQ(iss.file, str(e, "file"));
            EXPECT_EQ(iss.rowNo, static_cast<std::uint64_t>(num(e, "rowNo")));
            EXPECT_EQ(iss.column, str(e, "column"));
            EXPECT_EQ(iss.modelId, str(e, "modelId"));
            // 比较型字段（ERR-01 三要素——数值/文本侧按黄金携带面断言）。
            if (const tk::JsonValue* av = e.find("actualValue"); av != nullptr && av->isNumber()) {
                ASSERT_TRUE(iss.actualValue.has_value());
                EXPECT_DOUBLE_EQ(*iss.actualValue, av->number);
                ASSERT_TRUE(iss.expectedValue.has_value());
                EXPECT_DOUBLE_EQ(*iss.expectedValue, num(e, "expectedValue"));
                EXPECT_EQ(iss.unit, str(e, "unit"));
            }
            if (const tk::JsonValue* atx = e.find("actualText"); atx != nullptr && atx->isString()) {
                ASSERT_TRUE(iss.actualText.has_value());
                EXPECT_EQ(*iss.actualText, atx->text);
            }
        }
    }
}

/**
 * AT-08 主链路面③（版本锁定）：InMemoryCatalogProvider 写入即锁定只增
 * （§4.2/§14.1——重复锁定拒绝、load 幂等、摘要不符 fail-fast）。
 */
TEST_F(SelGoldenCatalog, CatalogLockSemantics_WP19T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-01", "CON-05"}),
                  std::vector<std::string>{"AT-08"});

    const sel::CatalogImporter importer;
    const sel::CatalogPackageSnapshot snap = importer.assemble(validInput(), packageManifest());
    const core::ObjectId lockId = core::ObjectId::fromCanonical(
        "obj-00000000000000000000000000000c01");

    sel::InMemoryCatalogProvider provider;
    const sel::CatalogVersion lock = provider.lockVersion(snap, lockId);
    EXPECT_EQ(lock.identity.catalogId, "cat-sel-golden");
    EXPECT_EQ(lock.identity.version, "1.0.0");
    // 重复锁定＝只增纪律拒绝（PA-2 不可变历史——fail-fast）。
    EXPECT_THROW(provider.lockVersion(snap, lockId), std::invalid_argument);
    // load 幂等：同锁同快照（锁定后不可变）。
    const sel::CatalogPackageSnapshot loaded = provider.load(lock);
    EXPECT_EQ(loaded, snap);
    EXPECT_EQ(loaded.motors.size(), snap.motors.size());
    // 引用完整性：摘要不符（或锁不存在）＝fail-fast（§14.1 load 契约）。
    sel::CatalogVersion forged = lock;
    forged.identity.version = "9.9.9";
    EXPECT_THROW((void)provider.load(forged), std::invalid_argument);
    // listLocked 确定性清单（catalogId→version 升序）。
    EXPECT_EQ(provider.listLocked().size(), 1u);
}

// =====================================================================
// SelGoldenScreening——可行/不可行型号黄金表（AT-08 硬筛选面）
// =====================================================================

class SelGoldenScreening : public tk::GoldenFixture {
protected:
    tk::DatasetRef datasetRef() const override { return {"sel-screening-golden", "1.0.0"}; }
};

/**
 * AT-08 主链路面④（硬筛选黄金表）：四算例逐候选×逐轴驱动产品
 * IHardConstraintSelector（screenMotors/screenGearboxes——接口路径消费），
 * 逐记录黄金对照（verdict/逐原因 ERR-01 全字段/逐缺口）；另以 inputs 手算
 * 锚点独立钉扎（第三路——防参考实现与产品实现两路同错）。
 */
TEST_F(SelGoldenScreening, ScreeningGoldenTables_WP19T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-03", "SEL-04", "SEL-06", "SEL-09",
                                            "NFR-COR-01"}),
                  std::vector<std::string>{"AT-08"});

    // ---- 目录快照（黄金业务模型直构——identity/axes/motors/gearboxes）。
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/screening-inputs.json", true);
    sel::CatalogIdentity catalog;
    catalog.catalogId = str(*inputs.find("identity"), "catalogId");
    catalog.version = str(*inputs.find("identity"), "version");
    catalog.source = str(*inputs.find("identity"), "source");
    sel::CatalogPackageSnapshot snap;
    snap.manifest.formatVersion = sel::kCatalogFormatVersion;
    snap.manifest.identity = catalog;
    for (const tk::JsonValue& m : inputs.find("motors")->items) {
        snap.motors.push_back(motorFromJson(m, catalog));
    }
    for (const tk::JsonValue& g : inputs.find("gearboxes")->items) {
        snap.gearboxes.push_back(gearboxFromJson(g, catalog));
    }

    // ---- 曲线（电机功率维度曲线口径消费——tryMakePerformanceCurve 统一入口）。
    std::optional<sel::PerformanceCurve> powerCurve;
    {
        const tk::JsonValue& cj = *inputs.find("curves");
        ASSERT_EQ(cj.items.size(), 1u);
        sel::CatalogIssue reject;
        std::vector<sel::CapabilityPoint> pts;
        for (const tk::JsonValue& p : cj.items[0].find("points")->items) {
            pts.push_back({num(p, "x"), num(p, "y")});
        }
        powerCurve = sel::tryMakePerformanceCurve(
            str(cj.items[0], "curveId"), "speed", "power", "rad/s", "W", pts, catalog, reject);
        ASSERT_TRUE(powerCurve.has_value()) << "黄金曲线构造被拒绝: " << reject.code;
        snap.curves.push_back(*powerCurve);
    }

    const core::ObjectId j1 = core::ObjectId::fromCanonical(
        str(*inputs.find("axes"), "j1"));
    const core::ObjectId j2 = core::ObjectId::fromCanonical(
        str(*inputs.find("axes"), "j2"));

    const sel::HardConstraintSelector selector;
    const tk::JsonValue exp = loadJson(*dataset, "expected/screening-expected.json", false);
    const tk::JsonValue& expCases = *exp.find("cases");

    std::size_t checkedRecords = 0;
    for (const tk::JsonValue& expCase : expCases.items) {
        const std::string caseId = str(expCase, "id");
        SCOPED_TRACE("golden case " + caseId);
        // ---- 逐算例驱动（criteria＋facts 全由黄金数据承载）。
        const sel::ScreeningCriteria criteria = criteriaFromJson(*expCase.find("criteria"));
        std::vector<sel::AxisWorkpointFacts> facts;
        for (const tk::JsonValue& f : expCase.find("facts")->items) {
            facts.push_back(factsFromJson(f,
                str(f, "axisRef") == "j1" ? j1 : j2));
        }
        ASSERT_EQ(facts.size(), 1u) << "黄金算例单轴事实约定";

        // ---- 产品入口（IHardConstraintSelector 接口引用消费）。
        const std::vector<sel::FeasibilityRecord> motorRecords =
            selector.screenMotors(snap, facts, criteria, nullptr);
        const std::vector<sel::FeasibilityRecord> gearboxRecords =
            selector.screenGearboxes(snap, facts, criteria, nullptr);

        // ---- 黄金对照（期望记录序＝电机候选×轴→减速器候选×轴——与产品
        //      遍历框架一致；逐字段精确对照）。
        const tk::JsonValue& expRecords = *expCase.find("records");
        ASSERT_EQ(motorRecords.size() + gearboxRecords.size(), expRecords.items.size());
        std::size_t golden = 0;
        for (const sel::FeasibilityRecord& r : motorRecords) {
            const tk::JsonValue& e = expRecords.items[golden++];
            SCOPED_TRACE(r.id);
            EXPECT_EQ(r.id, str(e, "id")) << "记录键（候选|轴 canonical）不符";
            EXPECT_EQ("Motor", str(e, "deviceKind"));
            EXPECT_EQ(r.verdict == sel::VerdictKind::Feasible ? "Feasible"
                      : r.verdict == sel::VerdictKind::Rejected ? "Rejected"
                                                                : "DataInsufficient",
                      str(e, "verdict"));
            const tk::JsonValue& expReasons = *e.find("reasons");
            ASSERT_EQ(r.reasons.size(), expReasons.items.size())
                << "原因数不符（" << r.id << "）——稳定排序或维度判定漂移";
            for (std::size_t i = 0; i < r.reasons.size(); ++i) {
                expectReasonGolden(r.id.c_str(), r.reasons[i], expReasons.items[i]);
            }
            const tk::JsonValue& expGaps = *e.find("gaps");
            ASSERT_EQ(r.gaps.size(), expGaps.items.size());
            for (std::size_t i = 0; i < r.gaps.size(); ++i) {
                expectGapGolden("gap", r.gaps[i], expGaps.items[i]);
            }
            ++checkedRecords;
        }
        for (const sel::FeasibilityRecord& r : gearboxRecords) {
            const tk::JsonValue& e = expRecords.items[golden++];
            SCOPED_TRACE(r.id);
            EXPECT_EQ(r.id, str(e, "id"));
            EXPECT_EQ("Gearbox", str(e, "deviceKind"));
            EXPECT_EQ(r.verdict == sel::VerdictKind::Feasible ? "Feasible"
                      : r.verdict == sel::VerdictKind::Rejected ? "Rejected"
                                                                : "DataInsufficient",
                      str(e, "verdict"));
            const tk::JsonValue& expReasons = *e.find("reasons");
            ASSERT_EQ(r.reasons.size(), expReasons.items.size());
            for (std::size_t i = 0; i < r.reasons.size(); ++i) {
                expectReasonGolden(r.id.c_str(), r.reasons[i], expReasons.items[i]);
            }
            const tk::JsonValue& expGaps = *e.find("gaps");
            ASSERT_EQ(r.gaps.size(), expGaps.items.size());
            for (std::size_t i = 0; i < r.gaps.size(); ++i) {
                expectGapGolden("gap", r.gaps[i], expGaps.items[i]);
            }
            ++checkedRecords;
        }
    }
    // 覆盖自检：24 记录全对照（防黄金/输入漂移导致静默缩表）。
    EXPECT_EQ(checkedRecords, 24u);

    // ---- 手算锚点（第三路独立断言——不经参考实现通道）。
    const tk::JsonValue& heavy = expCases.items[0];
    // 曲线插值解析：@130 rad/s → 800+8×30＝1040 W（M-101 P_rms 的能力值——
    // 该值不淘汰，以插值器直接断言解析值）。
    sel::LinearCurveEvaluator evaluator;
    const sel::CurveQueryResult qr = evaluator.evaluate(*powerCurve, 130.0);
    ASSERT_TRUE(qr.ok());
    EXPECT_DOUBLE_EQ(qr.value, num(*heavy.find("analyticAnchors"), "powerCurveAt130"));
    // 力臂核算解析：F_allow＝500×0.05/0.10＝250 N（G-202 径向允许力阈值——
    // 已由该记录 ExternalLoadExceeded.required 黄金对照承载，此处双路互证）。
    EXPECT_DOUBLE_EQ(num(*heavy.find("analyticAnchors"), "radialAllowG202"), 250.0);
    // SF 恰好边界解析：240×1.25＝300.0（与 M-101 maxSpeed 相等——C7 容差内
    // 不误淘汰，已由 sf-edge M-101 Feasible 黄金承载，锚点数值独立复核）。
    EXPECT_DOUBLE_EQ(num(*heavy.find("analyticAnchors"), "sfEdgeSpeedPeak"), 300.0);
}

/**
 * 确定性（NFR-COR-02）：同输入同线程两次筛选输出逐记录全等（含原因序/缺口
 * 序）——黄金数据驱动面即其确定性载体。
 */
TEST_F(SelGoldenScreening, ScreeningDeterministic_WP19T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"NFR-COR-02"}),
                  std::vector<std::string>{});

    const tk::JsonValue inputs = loadJson(*dataset, "inputs/screening-inputs.json", true);
    sel::CatalogIdentity catalog;
    catalog.catalogId = str(*inputs.find("identity"), "catalogId");
    catalog.version = str(*inputs.find("identity"), "version");
    catalog.source = str(*inputs.find("identity"), "source");
    sel::CatalogPackageSnapshot snap;
    snap.manifest.formatVersion = sel::kCatalogFormatVersion;
    snap.manifest.identity = catalog;
    for (const tk::JsonValue& m : inputs.find("motors")->items) {
        snap.motors.push_back(motorFromJson(m, catalog));
    }
    for (const tk::JsonValue& g : inputs.find("gearboxes")->items) {
        snap.gearboxes.push_back(gearboxFromJson(g, catalog));
    }
    const core::ObjectId j1 = core::ObjectId::fromCanonical(
        str(*inputs.find("axes"), "j1"));
    const tk::JsonValue& exp = loadJson(*dataset, "expected/screening-expected.json", false);
    const tk::JsonValue& heavy = exp.find("cases")->items[0];
    const sel::ScreeningCriteria criteria = criteriaFromJson(*heavy.find("criteria"));
    std::vector<sel::AxisWorkpointFacts> facts;
    for (const tk::JsonValue& f : heavy.find("facts")->items) {
        facts.push_back(factsFromJson(f, j1));
    }

    const sel::HardConstraintSelector selector;
    const auto first = selector.screenMotors(snap, facts, criteria, nullptr);
    const auto second = selector.screenMotors(snap, facts, criteria, nullptr);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i], second[i]) << "第 " << i << " 条记录两次筛选不一致";
    }
}

// =====================================================================
// SelGoldenCombo——组合校核黄金算例（SEL-05）＋可行集组装闭环（AT-08）
// =====================================================================

class SelGoldenCombo : public tk::GoldenFixture {
protected:
    tk::DatasetRef datasetRef() const override { return {"sel-combo-golden", "1.0.0"}; }

    /// 黄金目录快照（combo inputs 业务模型——combo 侧曲线面不消费〔criteria
    /// 全不启用且 M-102 无曲线〕；M-101 功率维度曲线口径消费——保留曲线装配）。
    sel::CatalogPackageSnapshot snapshot()
    {
        const tk::JsonValue inputs = loadJson(*dataset, "inputs/combo-inputs.json", true);
        sel::CatalogIdentity catalog;
        catalog.catalogId = str(*inputs.find("identity"), "catalogId");
        catalog.version = str(*inputs.find("identity"), "version");
        catalog.source = str(*inputs.find("identity"), "source");
        sel::CatalogPackageSnapshot s;
        s.manifest.formatVersion = sel::kCatalogFormatVersion;
        s.manifest.identity = catalog;
        for (const tk::JsonValue& m : inputs.find("motors")->items) {
            // 曲线引用面由 motorFromJson 统一装配（powerCurveId——单一来源）。
            s.motors.push_back(motorFromJson(m, catalog));
        }
        for (const tk::JsonValue& g : inputs.find("gearboxes")->items) {
            s.gearboxes.push_back(gearboxFromJson(g, catalog));
        }
        for (const tk::JsonValue& p : inputs.find("compatPairs")->items) {
            s.compatibility.push_back({str(p, "motorId"), str(p, "gearboxId"),
                                       "flange-mount"});
        }
        // c101-power 曲线（M-101 功率维度曲线口径——@ω_m 查询；@275→2200）。
        sel::CatalogIssue reject;
        auto curve = sel::tryMakePerformanceCurve("c101-power", "speed", "power",
            "rad/s", "W", {{100.0, 800.0}, {200.0, 1600.0}, {300.0, 2400.0}},
            catalog, reject);
        if (curve.has_value()) {
            s.curves.push_back(*curve);
        }
        return s;
    }
};

/**
 * SEL-05 黄金联动面：四组合经 checkCombinations（纯函数直调——NFR-MNT-01）
 * 逐组合黄金对照（verdict/组合级原因/coverage 逐格 note/totalMass 经容差
 * 档案）；映射批由黄金 mappingFacts 组装（c＝1/n 解析换算——黄金联动单点在
 * 生成脚本，本测试只值传递不重算——§9.1 纪律）。
 */
TEST_F(SelGoldenCombo, CombinationCheckGolden_WP19T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-05", "SEL-06", "DYN-04", "NFR-COR-01"}),
                  (std::vector<std::string>{"AT-08", "AT-38"}));

    const sel::CatalogPackageSnapshot snap = snapshot();
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/combo-inputs.json", true);
    const core::ObjectId j3 = core::ObjectId::fromCanonical(str(*inputs.find("axes"), "j3"));
    const core::ObjectId j4 = core::ObjectId::fromCanonical(str(*inputs.find("axes"), "j4"));

    // ---- 轴工作点事实（关节侧——黄金 jointFacts；电机侧以映射批为权威，
    //      bundle 的 motor* 字段不填充＝组装方无须填充的形态自证）。
    std::vector<sel::AxisWorkpointFacts> bundle;
    for (const char* key : {"j3", "j4"}) {
        const tk::JsonValue& jf = *inputs.find("jointFacts")->find(key);
        sel::AxisWorkpointFacts f;
        f.jointId = key == std::string("j3") ? j3 : j4;
        f.caseId = str(jf, "caseId");
        f.jointTorqueRms = optNum(jf, "jointTorqueRms");
        f.jointTorquePeak = optNum(jf, "jointTorquePeak");
        f.jointSpeedPeak = optNum(jf, "jointSpeedPeak");
        f.atTime = num(jf, "atTime");
        f.segmentId = str(jf, "segmentId");
        bundle.push_back(f);
    }

    // ---- 组合指派（黄金 K1~K4——组合键由产品 makeDeviceCombinationId 现算，
    //      黄金数据不书写键哈希〔产品计算面〕；K4 含不兼容对＝校核直调面）。
    const tk::JsonValue exp = loadJson(*dataset, "expected/combo-expected.json", false);
    sel::MappingBatchFacts batch;
    batch.mappingContractVersion = 1;
    batch.mappingAlgorithmVersion = 1;
    batch.completeness = sel::CompletenessKind::Complete;
    std::vector<sel::DeviceCombination> combos;
    for (const tk::JsonValue& c : exp.find("cases")->items) {
        sel::DeviceCombination combo;
        combo.catalog = snap.manifest.identity;
        for (const tk::JsonValue& ax : c.find("mappingFacts")->items) {
            const std::string axisRef = str(ax, "axisRef");
            // 轴指派（motor/gearbox 型号从黄金 mappingFacts 的对位组合面反查
            // ——inputs.combos 为声明源）。
            const tk::JsonValue* decl = nullptr;
            for (const tk::JsonValue& d : inputs.find("combos")->items) {
                if (str(d, "id") == str(c, "id")) {
                    decl = &d;
                }
            }
            ASSERT_NE(decl, nullptr);
            for (const tk::JsonValue& da : decl->find("axes")->items) {
                if (str(da, "axis") == axisRef) {
                    combo.axes.push_back({axisRef == "j3" ? j3 : j4,
                                          str(da, "motor"), str(da, "gearbox")});
                }
            }
        }
        ASSERT_EQ(combo.axes.size(), 2u);
        combo.id = sel::makeDeviceCombinationId(combo.catalog, combo.axes);
        combos.push_back(combo);
        sel::MappingCombinationFact cf;
        cf.combinationId = combo.id;
        cf.catalog = combo.catalog;
        cf.axes = combo.axes;
        batch.combinations.push_back(cf);
        // 逐轴映射事实（黄金数值面——值传递）。
        for (const tk::JsonValue& ax : c.find("mappingFacts")->items) {
            sel::MappingAxisFact f;
            f.combinationId = combo.id;
            f.jointId = str(ax, "axisRef") == "j3" ? j3 : j4;
            f.caseId = str(ax, "caseId");
            f.motorTorqueRms = optNum(ax, "motorTorqueRms");
            f.motorTorquePeak = optNum(ax, "motorTorquePeak");
            f.motorSpeedPeak = optNum(ax, "motorSpeedPeak");
            f.motorSpeedRms = optNum(ax, "motorSpeedRms");
            f.motorPowerPeak = optNum(ax, "motorPowerPeak");
            f.motorPowerRms = optNum(ax, "motorPowerRms");
            f.peakDuration = optNum(ax, "peakDuration");
            f.peakAtTime = num(ax, "peakAtTime");
            f.peakSegmentId = str(ax, "peakSegmentId");
            f.inertiaRatio = optNum(ax, "inertiaRatio");
            f.reflectedInertia = optNum(ax, "reflectedInertia");
            f.efficiencyApplied = boolOf(ax, "efficiencyApplied");
            batch.axes.push_back(f);
        }
    }

    // ---- 核心输入（筛选条件全不启用＋惯量比规则未配置——R1 默认态）。
    sel::CombinationCheckCoreInput in;
    in.snapshot = &snap;
    in.axisFacts = bundle;
    in.mappingBatch = batch;
    const tk::JsonValue& cj = *inputs.find("criteria");
    in.criteria.safetyFactor = num(cj, "safetyFactor");
    in.criteria.requiredDutyClass = str(cj, "requiredDutyClass");

    const std::vector<sel::CombinationCheckOutcome> outcomes = sel::checkCombinations(in, nullptr);
    ASSERT_EQ(outcomes.size(), combos.size());

    // ---- 逐组合黄金对照。
    const tk::JsonValue& expCases = *exp.find("cases");
    for (std::size_t i = 0; i < expCases.items.size(); ++i) {
        const tk::JsonValue& e = expCases.items[i];
        const sel::CombinationCheckOutcome& o = outcomes[i];
        SCOPED_TRACE("golden combo " + str(e, "id"));
        // 组合级身份承载（T05 落位细化——无单候选/单轴）。
        EXPECT_EQ(o.record.id, combos[i].id);
        EXPECT_EQ(o.record.deviceKind, sel::DeviceKind::Combination);
        EXPECT_TRUE(o.record.candidateModelId.empty());
        EXPECT_EQ(o.record.verdict == sel::VerdictKind::Feasible ? "Feasible"
                  : o.record.verdict == sel::VerdictKind::Rejected ? "Rejected"
                                                                   : "DataInsufficient",
                  str(*e.find("check"), "verdict"));
        // 组合级原因（token 序号/定位/ERR-01 字段——含 ComboIncompatible 文本面）。
        const tk::JsonValue& expReasons = *e.find("check")->find("reasons");
        ASSERT_EQ(o.record.reasons.size(), expReasons.items.size());
        for (std::size_t k = 0; k < o.record.reasons.size(); ++k) {
            const sel::RejectionReason& r = o.record.reasons[k];
            const tk::JsonValue& er = expReasons.items[k];
            EXPECT_EQ(static_cast<int>(r.token), static_cast<int>(num(er, "tokenOrdinal")));
            EXPECT_EQ(r.caseId, str(er, "caseId"));
            EXPECT_EQ(r.candidateModelId, str(er, "candidateModelId"));
            if (const tk::JsonValue* at = er.find("actualText"); at != nullptr && at->isString()) {
                EXPECT_EQ(r.actualText, at->text) << "文本比较面（组合兼容词面）";
                EXPECT_EQ(r.requiredText, str(er, "requiredText"));
                EXPECT_EQ(r.thresholdSource, str(er, "thresholdSource"));
            } else {
                EXPECT_DOUBLE_EQ(r.actual, num(er, "actual"));
                EXPECT_DOUBLE_EQ(r.required, num(er, "required"));
                EXPECT_EQ(r.unit, str(er, "unit"));
                EXPECT_EQ(r.thresholdSource, str(er, "thresholdSource"));
            }
        }
        // 缺口（维度/caseId/diagCode——曲线外推拒绝面）。
        const tk::JsonValue& expGaps = *e.find("check")->find("gaps");
        ASSERT_EQ(o.record.gaps.size(), expGaps.items.size());
        for (std::size_t k = 0; k < o.record.gaps.size(); ++k) {
            expectGapGolden("combo gap", o.record.gaps[k], expGaps.items[k]);
        }
        // 资格矩阵（逐格 verdict/note/定位面——EVI-02 素材）。
        const tk::JsonValue& expCoverage = *e.find("check")->find("coverage");
        ASSERT_EQ(o.coverage.size(), expCoverage.items.size());
        for (std::size_t k = 0; k < o.coverage.size(); ++k) {
            const sel::CaseCoverageEntry& cell = o.coverage[k];
            const tk::JsonValue& ec = expCoverage.items[k];
            EXPECT_EQ(cell.combinationId, combos[i].id);
            EXPECT_EQ(cell.caseId, str(ec, "caseId"));
            EXPECT_EQ(cell.verdict == sel::VerdictKind::Feasible ? "Feasible"
                      : cell.verdict == sel::VerdictKind::Rejected ? "Rejected"
                                                                   : "DataInsufficient",
                      str(ec, "verdict"));
            EXPECT_EQ(cell.note, str(ec, "note")) << "格 note（含惯量比未判定标注）";
            if (const tk::JsonValue* ar = ec.find("axisRef"); ar != nullptr && ar->isString()
                                                              && !ar->text.empty()) {
                EXPECT_EQ(cell.axisId, ar->text == "j3" ? j3 : j4) << "格定位轴";
                EXPECT_DOUBLE_EQ(cell.atTime, num(ec, "atTime"));
                EXPECT_EQ(cell.segmentId, str(ec, "segmentId"));
            }
        }
        // 惯量比未判定态（O-11 显式标记）。
        EXPECT_EQ(o.inertiaRatioUnsettled, boolOf(*e.find("check"), "inertiaRatioUnsettled"));
        // 质量核算（容差档案 sel-golden——combos[*].totalMass）。
        IRD_EXPECT_CLOSE(std::string("combos[") + std::to_string(i) + "].totalMass",
                         o.totalMass, num(*e.find("check"), "totalMass"), *profile, "kg");
    }
}

/**
 * AT-08"可行组合"闭环：组合校核黄金产出接可行集组装器
 * IFeasibleSetBuilder.build（接口路径消费）——K1 进可行集＋三态记录齐备＋
 * §10.4 稳定排序（可行居首）＋diagRef 输出回填（reasonTokenDiagCode 唯一
 * 映射——SEL-06 输出面闭环）。
 */
TEST_F(SelGoldenCombo, FeasibleSetBuildFromGolden_WP19T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-05", "SEL-06"}),
                  std::vector<std::string>{"AT-08"});

    const sel::CatalogPackageSnapshot snap = snapshot();
    // 组合校核（同黄金用例的输入组装——复用其黄金断言，本用例聚焦组装段）。
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/combo-inputs.json", true);
    const core::ObjectId j3 = core::ObjectId::fromCanonical(str(*inputs.find("axes"), "j3"));
    const core::ObjectId j4 = core::ObjectId::fromCanonical(str(*inputs.find("axes"), "j4"));
    std::vector<sel::AxisWorkpointFacts> bundle;
    for (const char* key : {"j3", "j4"}) {
        const tk::JsonValue& jf = *inputs.find("jointFacts")->find(key);
        sel::AxisWorkpointFacts f;
        f.jointId = key == std::string("j3") ? j3 : j4;
        f.caseId = str(jf, "caseId");
        f.jointTorqueRms = optNum(jf, "jointTorqueRms");
        f.jointTorquePeak = optNum(jf, "jointTorquePeak");
        f.jointSpeedPeak = optNum(jf, "jointSpeedPeak");
        f.atTime = num(jf, "atTime");
        f.segmentId = str(jf, "segmentId");
        bundle.push_back(f);
    }
    const tk::JsonValue exp = loadJson(*dataset, "expected/combo-expected.json", false);
    sel::MappingBatchFacts batch;
    batch.mappingContractVersion = 1;
    batch.mappingAlgorithmVersion = 1;
    batch.completeness = sel::CompletenessKind::Complete;
    std::vector<sel::FeasibleSetEntry> entries;
    std::vector<sel::CaseCoverageEntry> coverage;
    std::vector<sel::FeasibilityRecord> records;
    for (const tk::JsonValue& c : exp.find("cases")->items) {
        sel::DeviceCombination combo;
        combo.catalog = snap.manifest.identity;
        const tk::JsonValue* decl = nullptr;
        for (const tk::JsonValue& d : inputs.find("combos")->items) {
            if (str(d, "id") == str(c, "id")) { decl = &d; }
        }
        ASSERT_NE(decl, nullptr);
        for (const tk::JsonValue& da : decl->find("axes")->items) {
            combo.axes.push_back({str(da, "axis") == "j3" ? j3 : j4,
                                  str(da, "motor"), str(da, "gearbox")});
        }
        combo.id = sel::makeDeviceCombinationId(combo.catalog, combo.axes);
        sel::MappingCombinationFact cf;
        cf.combinationId = combo.id;
        cf.catalog = combo.catalog;
        cf.axes = combo.axes;
        batch.combinations.push_back(cf);
        for (const tk::JsonValue& ax : c.find("mappingFacts")->items) {
            sel::MappingAxisFact f;
            f.combinationId = combo.id;
            f.jointId = str(ax, "axisRef") == "j3" ? j3 : j4;
            f.caseId = str(ax, "caseId");
            f.motorTorqueRms = optNum(ax, "motorTorqueRms");
            f.motorTorquePeak = optNum(ax, "motorTorquePeak");
            f.motorSpeedPeak = optNum(ax, "motorSpeedPeak");
            f.motorSpeedRms = optNum(ax, "motorSpeedRms");
            f.motorPowerPeak = optNum(ax, "motorPowerPeak");
            f.motorPowerRms = optNum(ax, "motorPowerRms");
            f.peakDuration = optNum(ax, "peakDuration");
            f.peakAtTime = num(ax, "peakAtTime");
            f.peakSegmentId = str(ax, "peakSegmentId");
            f.inertiaRatio = optNum(ax, "inertiaRatio");
            f.reflectedInertia = optNum(ax, "reflectedInertia");
            f.efficiencyApplied = boolOf(ax, "efficiencyApplied");
            batch.axes.push_back(f);
        }
        entries.push_back({combo, std::nullopt});
    }
    sel::CombinationCheckCoreInput in;
    in.snapshot = &snap;
    in.axisFacts = bundle;
    in.mappingBatch = batch;
    const tk::JsonValue& cj = *inputs.find("criteria");
    in.criteria.safetyFactor = num(cj, "safetyFactor");
    in.criteria.requiredDutyClass = str(cj, "requiredDutyClass");
    for (const sel::CombinationCheckOutcome& o : sel::checkCombinations(in, nullptr)) {
        records.push_back(o.record);
        coverage.insert(coverage.end(), o.coverage.begin(), o.coverage.end());
    }

    // ---- 可行集组装（IFeasibleSetBuilder 接口引用消费——SEL-06 输出面）。
    sel::IdentityBlock identity;
    identity.catalog = snap.manifest.identity;
    identity.contractVersion = sel::kCombinationCheckContractVersion;
    identity.mode = core::EvaluationMode::Verified;
    const sel::SelectionRunResult run =
        sel::FeasibleSetBuilder().build(records, coverage, identity, entries);

    // K1 全维通过 → 可行集恰一组合；Rejected 居后（§10.4 排序键①可行居首）。
    ASSERT_EQ(run.set.feasible.size(), 1u);
    EXPECT_EQ(run.set.feasible.front().axes.size(), 2u);
    EXPECT_EQ(run.set.feasible.front().axes.front().motorModelId, "M-101");
    ASSERT_EQ(run.set.records.size(), 4u);
    EXPECT_EQ(run.set.records.front().verdict, sel::VerdictKind::Feasible);
    // SEL-06 输出面闭环：逐原因 diagRef 回填非空（词表→稳定码唯一映射）。
    for (const sel::FeasibilityRecord& r : run.set.records) {
        for (const sel::RejectionReason& reason : r.reasons) {
            ASSERT_TRUE(reason.diagRef.has_value()) << "输出原因 diagRef 未回填";
            EXPECT_FALSE(reason.diagRef->empty());
        }
    }
    // EVI-02 覆盖矩阵透传（组合序×工况序）。
    EXPECT_EQ(run.coverage.size(), coverage.size());
}

// =====================================================================
// SelGoldenBackfill——回填事务样例（AT-30）
// =====================================================================

class SelGoldenBackfill : public tk::GoldenFixture {
protected:
    tk::DatasetRef datasetRef() const override { return {"sel-backfill-golden", "1.0.0"}; }

    /// 黄金目录快照（backfill inputs 器件数据——与 catalog 家族同源）。
    sel::CatalogPackageSnapshot snapshot()
    {
        const tk::JsonValue inputs = loadJson(*dataset, "inputs/backfill-inputs.json", true);
        sel::CatalogIdentity catalog;
        catalog.catalogId = str(*inputs.find("identity"), "catalogId");
        catalog.version = str(*inputs.find("identity"), "version");
        catalog.source = str(*inputs.find("identity"), "source");
        sel::CatalogPackageSnapshot s;
        s.manifest.formatVersion = sel::kCatalogFormatVersion;
        s.manifest.identity = catalog;
        sel::MotorCatalogEntry m;
        m.modelId = str(*inputs.find("motor101"), "modelId");
        m.catalog = catalog;
        m.ratedTorque = 8.0;             // 器件能力字段不进回填面——装配取值仅
        m.peakTorque = 20.0;             // mass/rotorInertia；其余字段以合法占位
        m.ratedSpeed = 150.0;
        m.maxSpeed = 300.0;
        m.ratedPower = 2200.0;
        m.dutyClass = "S1";
        m.rotorInertia = num(*inputs.find("motor101"), "rotorInertia");
        m.mass = num(*inputs.find("motor101"), "mass");
        m.status = sel::ValidationStatus::Valid;
        s.motors.push_back(m);
        sel::GearboxCatalogEntry g;
        g.modelId = str(*inputs.find("gearbox201"), "modelId");
        g.catalog = catalog;
        g.ratedOutputTorque = 120.0;
        g.peakOutputTorque = 240.0;
        g.maxInputSpeed = 300.0;
        g.ratio = 50.0;
        g.efficiency = 0.92;
        g.mountingOrientation = "any";
        g.mass = num(*inputs.find("gearbox201"), "mass");
        g.housingInertia = num(*inputs.find("gearbox201"), "housingInertia");
        g.status = sel::ValidationStatus::Valid;
        s.gearboxes.push_back(g);
        s.compatibility.push_back({m.modelId, g.modelId,
                                   inputs.find("mountKind")->text});
        return s;
    }

    /// 锁定版本引用（黄金 lockObject/lockVersion）。
    sel::CatalogVersion lockRef(const sel::CatalogPackageSnapshot& snap)
    {
        const tk::JsonValue inputs = loadJson(*dataset, "inputs/backfill-inputs.json", true);
        sel::CatalogVersion lock;
        lock.identity = snap.manifest.identity;
        lock.lockObjectId =
            core::ObjectId::fromCanonical(str(inputs, "lockObject"));
        return lock;
    }
};

/**
 * AT-30 主链路面①（回填组装＋合成黄金）：assembleDeviceBackfill（接口路径
 * 消费）→ 合成黄金逐分量经容差档案对照（sel-golden——附录 D 第 9 项）＋
 * 转子独立登记＋载荷编码往返＋planFromEnvelope→Planned＋恰一
 * sel-device-backfill 对象写＋复算提示四域全量且不沿用原结论。
 */
TEST_F(SelGoldenBackfill, BackfillAssembledGolden_WP19T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-10", "MDL-16", "MDL-05"}),
                  std::vector<std::string>{"AT-30"});

    const sel::CatalogPackageSnapshot snap = snapshot();
    const sel::CatalogVersion lock = lockRef(snap);
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/backfill-inputs.json", true);
    const core::ObjectId j5 = core::ObjectId::fromCanonical(str(*inputs.find("axes"), "j5"));

    // ---- 组装源（黄金 assembled 算例——§12.4 合成输入直取）。
    const tk::JsonValue& src = *inputs.find("cases")->find("assembled");
    sel::AxisBackfillSource s;
    s.jointId = j5;
    s.motorModelId = str(src, "motorModelId");
    s.gearboxModelId = str(src, "gearboxModelId");
    s.mountKind = str(src, "mountKind");
    s.linkMassKg = num(src, "linkMassKg");
    s.linkComM = {num(*src.find("linkComM"), "x"), num(*src.find("linkComM"), "y"),
                  num(*src.find("linkComM"), "z")};
    const tk::JsonValue& li = *src.find("linkInertia");
    s.linkInertia = {num(li, "ixx"), num(li, "iyy"), num(li, "izz"),
                     num(li, "ixy"), num(li, "ixz"), num(li, "iyz")};
    s.motorComAnchorM = {num(*src.find("motorComAnchorM"), "x"),
                         num(*src.find("motorComAnchorM"), "y"),
                         num(*src.find("motorComAnchorM"), "z")};
    const tk::JsonValue* mi = src.find("motorHousingInertiaSupplement");
    if (mi != nullptr && !mi->isNull()) {
        s.motorHousingInertiaSupplement =
            sel::BackfillInertiaTensor{num(*mi, "ixx"), num(*mi, "iyy"), num(*mi, "izz"),
                                       num(*mi, "ixy"), num(*mi, "ixz"), num(*mi, "iyz")};
    }
    s.gearboxComAnchorM = {num(*src.find("gearboxComAnchorM"), "x"),
                           num(*src.find("gearboxComAnchorM"), "y"),
                           num(*src.find("gearboxComAnchorM"), "z")};
    s.appliedRatio = num(src, "appliedRatio");

    // ---- 组装（唯一实现点——组装期已执行 §12.4 合成与断言）。
    const sel::BackfillAssemblyOutcome asmOut =
        sel::assembleDeviceBackfill(snap, lock, {s});
    ASSERT_EQ(asmOut.kind, sel::BackfillAssemblyOutcome::Kind::Assembled) << asmOut.detail;

    // ---- 合成黄金对照（§12.4 五步——容差档案 sel-golden 逐分量；
    //      expected 由独立参考实现按五步规则标量直算）。
    const tk::JsonValue exp = loadJson(*dataset, "expected/backfill-expected.json", false);
    const tk::JsonValue& expSyn = *exp.find("assembled")->find("synthesis");
    ASSERT_EQ(asmOut.synthesis.size(), 1u);
    const sel::AxisSynthesis& syn = asmOut.synthesis.front();
    const std::string pfx = "backfill.axes[0].synthesis";
    IRD_EXPECT_CLOSE(pfx + ".massKg", syn.massKg, num(expSyn, "massKg"), *profile, "kg");
    IRD_EXPECT_CLOSE(pfx + ".comM.x", syn.comM.x, num(*expSyn.find("comM"), "x"),
                     *profile, "m");
    IRD_EXPECT_CLOSE(pfx + ".comM.y", syn.comM.y, num(*expSyn.find("comM"), "y"),
                     *profile, "m");
    IRD_EXPECT_CLOSE(pfx + ".comM.z", syn.comM.z, num(*expSyn.find("comM"), "z"),
                     *profile, "m");
    const tk::JsonValue& expI = *expSyn.find("inertia");
    IRD_EXPECT_CLOSE(pfx + ".inertia.ixx", syn.inertia.ixx, num(expI, "ixx"),
                     *profile, "kg*m^2");
    IRD_EXPECT_CLOSE(pfx + ".inertia.iyy", syn.inertia.iyy, num(expI, "iyy"),
                     *profile, "kg*m^2");
    IRD_EXPECT_CLOSE(pfx + ".inertia.izz", syn.inertia.izz, num(expI, "izz"),
                     *profile, "kg*m^2");
    IRD_EXPECT_CLOSE(pfx + ".inertia.ixy", syn.inertia.ixy, num(expI, "ixy"),
                     *profile, "kg*m^2");
    IRD_EXPECT_CLOSE(pfx + ".inertia.ixz", syn.inertia.ixz, num(expI, "ixz"),
                     *profile, "kg*m^2");
    IRD_EXPECT_CLOSE(pfx + ".inertia.iyz", syn.inertia.iyz, num(expI, "iyz"),
                     *profile, "kg*m^2");
    // 转子独立登记（不重复计入的记录面证明——与 inertia 分轨同值透传）。
    IRD_EXPECT_CLOSE(pfx + ".rotorInertiaKgM2", syn.rotorInertiaKgM2,
                     num(expSyn, "rotorInertiaKgM2"), *profile, "kg*m^2");
    // 手算锚点（第三路——质心 z 分量分数算术独立复核）。
    EXPECT_DOUBLE_EQ(num(*inputs.find("analyticAnchors"), "comZ"),
                     (12.0 * 0.30 + 5.5 * 0.12 + 6.5 * 0.05) / 24.0);

    // ---- 载荷编码往返（encode→decode 严格校验——组装产物结构保真）。
    ASSERT_EQ(asmOut.request.axes.size(), 1u);
    const std::vector<std::uint8_t> payload = sel::encodeBackfillPayload(asmOut.request);
    const sel::DecodedBackfillPayload back = sel::decodeBackfillPayload(payload);
    ASSERT_EQ(back.status, sel::DecodedBackfillPayload::Status::Ok) << back.detail;
    ASSERT_EQ(back.request.axes.size(), 1u);
    EXPECT_EQ(back.request.axes.front().motorModelId, "M-101");
    EXPECT_DOUBLE_EQ(back.request.axes.front().rotorInertiaKgM2,
                     asmOut.request.axes.front().rotorInertiaKgM2);

    // ---- 处理器计划（IDeviceBackfillCommandHandler 接口路径——AT-30 主链路）。
    const project::CommandEnvelope env =
        sel::makeBackfillEnvelope(core::BranchId::generate(), std::nullopt, asmOut.request);
    // 基线闭包：目录锁定引用（判定 3 正向前置——(oid, cv) 半区命中）。
    project::RevisionView baseline;
    baseline.id = core::RevisionId::generate();
    baseline.seq = 1;
    baseline.branch = core::BranchId::generate();
    project::ObjectRef lockRefView;
    lockRefView.objectId = lock.lockObjectId;
    lockRefView.contentVersion = core::ContentVersion{};
    lockRefView.objectTypeToken = "catalog";
    lockRefView.digest256 = std::string(64, '0');
    baseline.objectRefs.push_back(lockRefView);
    baseline.metadataRef.objectTypeToken = "project-metadata";

    sel::DeviceBackfillCommandHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const sel::BackfillPlanOutcome planOut =
        handler.planFromEnvelope(env, baseline, plan, diags);
    ASSERT_EQ(planOut.kind, sel::BackfillPlanOutcome::Kind::Planned) << planOut.detail;
    EXPECT_TRUE(diags.empty());
    // 恰一对象写（多轴整体原子的记录面载体——§12.2 纪律 3）＋token 无点词形。
    ASSERT_EQ(plan.objectWrites.size(), 1u);
    EXPECT_EQ(plan.objectWrites.front().objectTypeToken, "sel-device-backfill");
    // 记录载荷回读：复算提示四域全量＋不沿用原结论（AT-30 记录面恒值）＋
    // 转子独立登记在记录面。
    const sel::DecodedBackfillRecord recordBack =
        sel::decodeBackfillRecordObject(plan.objectWrites.front().payloadCanonical);
    ASSERT_EQ(recordBack.status, sel::DecodedBackfillRecord::Status::Ok) << recordBack.detail;
    ASSERT_EQ(recordBack.record.synthesis.size(), 1u);
    ASSERT_EQ(recordBack.record.axes.size(), 1u);
    for (std::size_t i = 0; i < sel::kRecalcDomainCount; ++i) {
        EXPECT_TRUE(recordBack.record.recalc.domains[i])
            << "复算域缺失: " << sel::recalcDomainToken(static_cast<sel::RecalcDomain>(i));
    }
    EXPECT_FALSE(recordBack.record.recalc.retainPriorConclusion)
        << "复核完成前不沿用原通过结论（AT-30）";
    EXPECT_EQ(recordBack.record.referenceFrameToken, std::string(sel::kBackfillFrameLink));
    EXPECT_DOUBLE_EQ(recordBack.record.synthesis.front().rotorInertiaKgM2,
                     asmOut.request.axes.front().rotorInertiaKgM2);
    // 命令摘要留痕（复算提示随修订持久化）。
    EXPECT_NE(plan.summary.find("复算"), std::string::npos);
    EXPECT_NE(plan.summary.find("不沿用"), std::string::npos);
}

/**
 * AT-30 主链路面②（组装拒绝分类）：三拒绝算例——壳体惯量补充缺席
 * （DataInsufficient）、安装关系不一致（MountIncompatible）、双轴整体原子
 * （第二轴缺失→整体失败零产出）；失败路径零请求产出（§12.2 纪律）。
 */
TEST_F(SelGoldenBackfill, BackfillRejections_WP19T11_ACC1)
{
    IRD_TEST_INFO((std::vector<std::string>{"SEL-10"}),
                  std::vector<std::string>{"AT-30"});

    const sel::CatalogPackageSnapshot snap = snapshot();
    const sel::CatalogVersion lock = lockRef(snap);
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/backfill-inputs.json", true);
    const core::ObjectId j5 = core::ObjectId::fromCanonical(str(*inputs.find("axes"), "j5"));
    const core::ObjectId j6 = core::ObjectId::fromCanonical(str(*inputs.find("axes"), "j6"));
    const tk::JsonValue& casesJ = *inputs.find("cases");
    const tk::JsonValue exp = loadJson(*dataset, "expected/backfill-expected.json", false);
    const tk::JsonValue& expRej = *exp.find("rejections");
    ASSERT_EQ(expRej.items.size(), 3u);

    // ---- 逐拒绝算例（黄金 kind 词面对照；组装失败零请求产出）。
    const std::vector<std::pair<const char*, std::vector<sel::AxisBackfillSource>>> scenarios = {
        {"missing-supplement", {makeSource(*casesJ.find("missingSupplement"), j5)}},
        {"mount-mismatch", {makeSource(*casesJ.find("mountMismatch"), j5)}},
        {"multi-axis-atomic", {makeSource(casesJ.find("multiAxisAtomic")->find("axes")->items[0], j5),
                               makeSource(casesJ.find("multiAxisAtomic")->find("axes")->items[1], j6)}},
    };
    for (std::size_t i = 0; i < scenarios.size(); ++i) {
        SCOPED_TRACE(scenarios[i].first);
        EXPECT_EQ(str(expRej.items[i], "id"), scenarios[i].first) << "黄金拒绝算例序";
        const sel::BackfillAssemblyOutcome o =
            sel::assembleDeviceBackfill(snap, lock, scenarios[i].second);
        const std::string goldenKind = str(expRej.items[i], "kind");
        const std::string actualKind =
            o.kind == sel::BackfillAssemblyOutcome::Kind::Assembled ? "Assembled"
            : o.kind == sel::BackfillAssemblyOutcome::Kind::UnknownDevice ? "UnknownDevice"
            : o.kind == sel::BackfillAssemblyOutcome::Kind::MountIncompatible ? "MountIncompatible"
            : o.kind == sel::BackfillAssemblyOutcome::Kind::DataInsufficient ? "DataInsufficient"
                                                                             : "InvalidInput";
        EXPECT_EQ(actualKind, goldenKind) << o.detail;
        EXPECT_FALSE(o.kind == sel::BackfillAssemblyOutcome::Kind::Assembled);
    }
}

/**
 * AT-30 面③（确定性）：同一组装源两次完整编码逐字节相等（NFR-COR-02——
 * canonical 协议无环境量/时钟/地址依赖；黄金数据驱动面即其载体）。
 */
TEST_F(SelGoldenBackfill, BackfillPayloadDeterministic_WP19T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"NFR-COR-02"}),
                  std::vector<std::string>{});

    const sel::CatalogPackageSnapshot snap = snapshot();
    const sel::CatalogVersion lock = lockRef(snap);
    const tk::JsonValue inputs = loadJson(*dataset, "inputs/backfill-inputs.json", true);
    const core::ObjectId j5 = core::ObjectId::fromCanonical(str(*inputs.find("axes"), "j5"));
    const sel::AxisBackfillSource s =
        makeSource(*inputs.find("cases")->find("assembled"), j5);
    const sel::BackfillAssemblyOutcome o = sel::assembleDeviceBackfill(snap, lock, {s});
    ASSERT_EQ(o.kind, sel::BackfillAssemblyOutcome::Kind::Assembled);

    const std::vector<std::uint8_t> bytes1 = sel::encodeBackfillPayload(o.request);
    const std::vector<std::uint8_t> bytes2 = sel::encodeBackfillPayload(o.request);
    ASSERT_EQ(bytes1.size(), bytes2.size());
    EXPECT_TRUE(std::equal(bytes1.begin(), bytes1.end(), bytes2.begin()))
        << "同请求两次编码逐字节不等（NFR-COR-02 违约）";
    const std::vector<std::uint8_t> rec1 = sel::encodeBackfillRecordObject(
        {std::string(sel::kBackfillFrameLink), sel::BackfillRecalcNotice{}, o.synthesis,
         o.request.axes});
    const std::vector<std::uint8_t> rec2 = sel::encodeBackfillRecordObject(
        {std::string(sel::kBackfillFrameLink), sel::BackfillRecalcNotice{}, o.synthesis,
         o.request.axes});
    ASSERT_EQ(rec1.size(), rec2.size());
    EXPECT_TRUE(std::equal(rec1.begin(), rec1.end(), rec2.begin()));
}


