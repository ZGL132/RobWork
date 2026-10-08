/**
 * @file   CatalogDiffTest.cpp
 * @brief  目录差异比较用例组（SelCatalogDiff）——SEL-08：型号三态差异
 *         （新增/移除/修改）、字段级增量（数值/文本/可缺失 (absent) 哨兵）、
 *         曲线点集增量（同 x 变 y 差分）、曲线头部字段增量、兼容关系增量、
 *         纯函数确定性与 from/to 身份、契约违约 fail-fast，以及"目录更新
 *         不静默改变历史结果"的锁定×比较组合用例（AT-08 版本锁定×更新）。
 *
 * 设计依据：
 *   - units/selection.md §13.6（CatalogDiff 纯函数会话工具——结构化差异
 *     四类增量；目录更新不静默改变历史结果——历史结果保持原目录版本
 *     切片身份）、§4.2（写入即锁定只增；内容身份失效链——变更任何业务
 *     字段→新内容身份→依赖切片失效）、D-SEL-14、§14.0（错误两分法）
 *   - 需求 SEL-08（支持目录差异比较和项目锁定版本，目录更新不应静默
 *     改变历史结果）、CON-05（diff 非空 ⇔ 包内容身份不同）、CON-02
 *     （历史证据不被改写）、NFR-COR-01/02（纯函数确定性）、AT-08（目录
 *     导入/版本锁定/更新测试）
 *   - 任务契约 tasks/foundation/WP-19-T07.json acceptance 2
 *
 * 数值口径：黄金值为解析期望（G-50 额定输出转矩 50→60 N·m、backlash
 * 缺失→0.5 rad、curve-tq 点 (150, 4.5→4.2)——基线数据见
 * CatalogTestSupport；差异值为精确文本比较——canonical 定点格式化无
 * 浮点误差面，不做容差比较）。
 *
 * 线程约束：纯函数单线程调用（CatalogDiffer 可重入）；锁定组合用例
 * 单线程（InMemoryCatalogProvider 非线程安全——头注登记）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogDiff.hpp>
#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO（需求/AT 追溯字段）

#include "CatalogTestSupport.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;

namespace {

/// 解析输入中按文件名取可变表（ParsedCatalogInput::find 返回 const 指针
/// ——v2 编辑场景需要可变面；未命中即测试构造错误，直接终止）。
ParsedFileTable& mutableFile(ParsedCatalogInput& in, const char* name)
{
    for (ParsedFileTable& t : in.files) {
        if (t.fileName == name) {
            return t;
        }
    }
    throw std::logic_error("test bug: file table not found");
}

/// 基线 v1 快照（两电机＋两减速器＋两曲线＋两兼容行——合法装配产物）。
CatalogPackageSnapshot makeV1()
{
    const CatalogImporter importer;
    return importer.assemble(makeBaselineInput(), makeBaselineManifest("cat-demo", "v1"));
}

/// v2 快照（编辑基线：G-50 额定输出转矩 50→60 N·m——单点业务变更，
/// 其余四表不变；manifest 版本号升为 v2——版本演进面）。
CatalogPackageSnapshot makeV2TorqueChange()
{
    ParsedCatalogInput in = makeBaselineInput();
    ParsedFileTable& gb = mutableFile(in, kCatalogFileGearboxes);
    EXPECT_TRUE(editRow(gb, "model_id", "G-50", "rated_output_torque_nm", "60"));
    const CatalogImporter importer;
    return importer.assemble(in, makeBaselineManifest("cat-demo", "v2"));
}

/// 在型号差异表中按 (isMotor, modelId) 定位条目（未命中返回 nullptr）。
const ModelDiff* findModelDiff(const CatalogDiff& diff, bool isMotor, const std::string& id)
{
    for (const ModelDiff& m : diff.models) {
        if (m.isMotor == isMotor && m.modelId == id) {
            return &m;
        }
    }
    return nullptr;
}

/// 在字段增量表中按字段名定位（未命中返回 nullptr）。
const FieldChange* findField(const ModelDiff& m, const std::string& field)
{
    for (const FieldChange& f : m.fieldChanges) {
        if (f.field == field) {
            return &f;
        }
    }
    return nullptr;
}

/// 在曲线差异表中按 curveId 定位（未命中返回 nullptr）。
const CurveDiff* findCurveDiff(const CatalogDiff& diff, const std::string& id)
{
    for (const CurveDiff& c : diff.curves) {
        if (c.curveId == id) {
            return &c;
        }
    }
    return nullptr;
}

}  // namespace

// =====================================================================
// 基础三态与字段级增量
// =====================================================================

/** 同快照比较＝空差异（三表皆空＋empty()==true——无变更不产零信息条目）。 */
TEST(SelCatalogDiff, IdenticalSnapshotsYieldEmptyDiff)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot v1 = makeV1();
    const CatalogDiffer differ;
    const CatalogDiff diff = differ.compare(v1, v1);
    EXPECT_TRUE(diff.empty());
    EXPECT_TRUE(diff.models.empty());
    EXPECT_TRUE(diff.curves.empty());
    EXPECT_TRUE(diff.compatibility.empty());
    EXPECT_EQ(diff.fromIdentity, v1.manifest.identity);
    EXPECT_EQ(diff.toIdentity, v1.manifest.identity);
}

/**
 * 型号三态黄金用例：v2 相对 v1——新增电机 M-300、移除电机 M-100（连同
 * 其曲线 curve-tq 与兼容行一起退场——v2 仍是合法可装配包）、修改减速器
 * G-50（额定输出转矩 50→60 N·m，fieldChanges 恰一条黄金行）。
 */
TEST(SelCatalogDiff, ModelAddedRemovedModifiedKinds)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot v1 = makeV1();

    // v2：motors 删 M-100 加 M-300；curves 删 curve-tq（M-100 的 owner
    // 曲线——REF-DANGLING 前置：owner 必须在主表内）；compat 删 M-100 行。
    // 基线表先落位再编辑（避免悬空引用——makeBaselineInput 为临时对象）。
    ParsedCatalogInput base = makeBaselineInput();
    ParsedCatalogInput in;
    std::vector<std::string> m300Row = {"M-300", "Sinotech", "ST-300", "13", "30", "150",
                                        "300", "4000", "9", "10", "S1", "220", "25",
                                        "0.95", "18", "20", "0.04", "12", "flangeA",
                                        "shaftB", ""};
    in.files.push_back(makeTable(kCatalogFileMotors, motorHeader(), {m300Row}));
    ParsedFileTable gb = *base.find(kCatalogFileGearboxes);
    EXPECT_TRUE(editRow(gb, "model_id", "G-50", "rated_output_torque_nm", "60"));
    in.files.push_back(gb);  // 减速器表单点变更（G-50 Modified 黄金面）
    in.files.push_back(makeTable(kCatalogFileCurves, curveHeader(),
                                 {{"curve-eff", "gearbox", "G-120", "speed", "rad/s",
                                   "power", "W", "100", "3000", "0"},
                                  {"curve-eff", "gearbox", "G-120", "speed", "rad/s",
                                   "power", "W", "300", "3600", "1"}}));
    in.files.push_back(makeTable(kCatalogFileCompatibility, compatHeader(),
                                 {{"M-300", "G-120", "flange-mount"}}));
    const CatalogImporter importer;
    const CatalogPackageSnapshot v2 = importer.assemble(in, makeBaselineManifest("cat-demo", "v2"));

    const CatalogDiffer differ;
    const CatalogDiff diff = differ.compare(v1, v2);

    // 身份承载：from=v1、to=v2（版本演进由身份面对照展示——manifest 级
    // 字段不进差异条目，否则 diff 恒非空失去变更检测语义）。
    EXPECT_EQ(diff.fromIdentity.version, "v1");
    EXPECT_EQ(diff.toIdentity.version, "v2");
    EXPECT_FALSE(diff.empty());

    // 新增：M-300（电机侧，fieldChanges 空——实体按 modelId 可得）。
    const ModelDiff* added = findModelDiff(diff, true, "M-300");
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->kind, ModelDiffKind::Added);
    EXPECT_TRUE(added->fieldChanges.empty());
    // 移除：M-100（连同其曲线/兼容行——曲线与兼容增量另有条目）。
    const ModelDiff* removed = findModelDiff(diff, true, "M-100");
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->kind, ModelDiffKind::Removed);
    EXPECT_TRUE(removed->fieldChanges.empty());
    // 修改：G-50 额定输出转矩——黄金字段行（old="50" new="60"，SI 域
    // canonical 定点文本；无其他字段差异）。
    const ModelDiff* modified = findModelDiff(diff, false, "G-50");
    ASSERT_NE(modified, nullptr);
    EXPECT_EQ(modified->kind, ModelDiffKind::Modified);
    ASSERT_EQ(modified->fieldChanges.size(), 1u);
    EXPECT_EQ(modified->fieldChanges[0].field, "ratedOutputTorque");
    EXPECT_EQ(modified->fieldChanges[0].oldText, "50");   // 单位 N·m
    EXPECT_EQ(modified->fieldChanges[0].newText, "60");   // 单位 N·m
    // 曲线与兼容随 M-100 退场（交叉面黄金断言）：兼容差异三条——v2 兼容
    // 表只声明 M-300|G-120，v1 的两条旧关系全部移除＋新关系加入（按
    // motorId 升序：M-100 < M-200 < M-300）。
    const CurveDiff* removedCurve = findCurveDiff(diff, "curve-tq");
    ASSERT_NE(removedCurve, nullptr);
    EXPECT_EQ(removedCurve->kind, CurveDiff::Kind::Removed);
    ASSERT_EQ(diff.compatibility.size(), 3u);
    EXPECT_EQ(diff.compatibility[0].kind, CompatibilityDiff::Kind::Removed);
    EXPECT_EQ(diff.compatibility[0].record.motorId, "M-100");
    EXPECT_EQ(diff.compatibility[1].kind, CompatibilityDiff::Kind::Removed);
    EXPECT_EQ(diff.compatibility[1].record.motorId, "M-200");
    EXPECT_EQ(diff.compatibility[2].kind, CompatibilityDiff::Kind::Added);
    EXPECT_EQ(diff.compatibility[2].record.motorId, "M-300");
}

/**
 * 可缺失字段的 (absent) 哨兵黄金用例：G-50 回程间隙缺失→0.5 rad、
 * M-100 制动能力缺失→18 N·m——一侧缺失以 kDiffAbsent 承载（缺失是
 * 事实陈述），数值侧为 canonical 定点文本。
 */
TEST(SelCatalogDiff, OptionalFieldTransitionsCarryAbsentMarker)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08"},
                  std::vector<std::string>{"AT-08"});
    // v2：G-50 backlash 空→"0.5"；M-100 brake_torque_nm 空→"18"。
    ParsedCatalogInput in = makeBaselineInput();
    ParsedFileTable& gb = mutableFile(in, kCatalogFileGearboxes);
    EXPECT_TRUE(editRow(gb, "model_id", "G-50", "backlash", "0.5"));
    ParsedFileTable& mo = mutableFile(in, kCatalogFileMotors);
    EXPECT_TRUE(editRow(mo, "model_id", "M-100", "brake_torque_nm", "18"));
    const CatalogImporter importer;
    const CatalogPackageSnapshot v2 =
        importer.assemble(in, makeBaselineManifest("cat-demo", "v2"));

    const CatalogDiff diff = CatalogDiffer{}.compare(makeV1(), v2);
    // 减速器 G-50：backlash 一条增量（旧缺失→新 0.5）。
    const ModelDiff* g50 = findModelDiff(diff, false, "G-50");
    ASSERT_NE(g50, nullptr);
    ASSERT_EQ(g50->fieldChanges.size(), 1u);
    EXPECT_EQ(g50->fieldChanges[0].field, "backlash");
    EXPECT_EQ(g50->fieldChanges[0].oldText, kDiffAbsent);  // 基线缺失
    EXPECT_EQ(g50->fieldChanges[0].newText, "0.5");        // 单位 rad（v1 冻结）
    // 电机 M-100：brakeTorque 一条增量。
    const ModelDiff* m100 = findModelDiff(diff, true, "M-100");
    ASSERT_NE(m100, nullptr);
    ASSERT_EQ(m100->fieldChanges.size(), 1u);
    EXPECT_EQ(m100->fieldChanges[0].field, "brakeTorque");
    EXPECT_EQ(m100->fieldChanges[0].oldText, kDiffAbsent);
    EXPECT_EQ(m100->fieldChanges[0].newText, "18");        // 单位 N·m
}

// =====================================================================
// 曲线与兼容关系增量
// =====================================================================

/**
 * 曲线点集增量黄金用例（§13.6"曲线点集增量"）：curve-tq 三点
 * (50,5.0)/(150,4.5)/(300,3.5) → v2 改为 (150,4.2)/(300,3.5)/(350,3.0)：
 * 移除首点 (50,5.0)、同 x 变 y＝旧值 (150,4.5) 移除＋新值 (150,4.2) 加入、
 * 追加尾点 (350,3.0)——双指针归并的三类点级差异各得一条黄金断言。
 */
TEST(SelCatalogDiff, CurvePointLevelDelta)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot v1 = makeV1();

    // v2：curves 表重写 curve-tq 四行——删 (50,5.0)、改 (150,4.2)、
    // 留 (300,3.5)、加 (350,3.0)（x 严格升序——构造入口校验通过）。
    ParsedCatalogInput in = makeBaselineInput();
    ParsedFileTable& cv = mutableFile(in, kCatalogFileCurves);
    // 原基线曲线行：0~2 为 curve-tq（(50,5.0),(150,4.5),(300,3.5)），
    // 3~4 为 curve-eff。重构 cells：curve-tq 新三点＋curve-eff 原两行。
    std::vector<std::string> cells;
    auto curveRow = [&](const char* x, const char* y, const char* idx) {
        for (const char* c : {"curve-tq", "motor", "M-100", "speed", "rad/s", "torque",
                              "N*m", x, y, idx}) {
            cells.push_back(c);
        }
    };
    curveRow("150", "4.2", "0");
    curveRow("300", "3.5", "1");
    curveRow("350", "3.0", "2");
    for (std::size_t i = 3 * cv.columnCount; i < cv.cells.size(); ++i) {
        cells.push_back(cv.cells[i]);  // curve-eff 原样保留
    }
    cv.cells = cells;
    const CatalogImporter importer;
    const CatalogPackageSnapshot v2 =
        importer.assemble(in, makeBaselineManifest("cat-demo", "v2"));

    const CatalogDiff diff = CatalogDiffer{}.compare(v1, v2);
    const CurveDiff* tq = findCurveDiff(diff, "curve-tq");
    ASSERT_NE(tq, nullptr);
    EXPECT_EQ(tq->kind, CurveDiff::Kind::Modified);
    EXPECT_TRUE(tq->fieldChanges.empty());  // 头部字段未变——纯点集差异
    // 点级黄金（按 x 升序）：
    ASSERT_EQ(tq->removedPoints.size(), 2u);
    EXPECT_TRUE((tq->removedPoints[0] == CapabilityPoint{50.0, 5.0}));   // 删首点
    EXPECT_TRUE((tq->removedPoints[1] == CapabilityPoint{150.0, 4.5}));  // 同 x 旧值
    ASSERT_EQ(tq->addedPoints.size(), 2u);
    EXPECT_TRUE((tq->addedPoints[0] == CapabilityPoint{150.0, 4.2}));    // 同 x 新值
    EXPECT_TRUE((tq->addedPoints[1] == CapabilityPoint{350.0, 3.0}));    // 追加尾点
    // curve-eff 未变——零条目（不产零信息差异）。
    EXPECT_EQ(findCurveDiff(diff, "curve-eff"), nullptr);
}

/**
 * 曲线头部字段增量黄金用例：curve-eff 量纲/单位迁移（power/W →
 * torque/N*m）——kind=Modified、fieldChanges 两条（yQuantity/yUnit）、
 * 点集增量空（点值不变）。v1 词表单单位（W 为 power 唯一注册单位）无法
 * 经真实单位换算表达——本用例以量纲迁移表达"头部字段变化"路径（与点集
 * 增量路径解耦；目录包的实际单位口径约束仍由导入校验把守）。
 */
TEST(SelCatalogDiff, CurveHeaderFieldChange)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08"},
                  std::vector<std::string>{"AT-08"});
    ParsedCatalogInput in = makeBaselineInput();
    ParsedFileTable& cv = mutableFile(in, kCatalogFileCurves);
    // curve-eff 两行（行 3~4）的 y_quantity/y_unit 列改 torque/N*m。
    for (std::size_t r = 3; r < 5; ++r) {
        for (std::size_t i = 0; i < cv.header.size(); ++i) {
            if (cv.header[i] == "y_quantity") { cv.cells[r * cv.columnCount + i] = "torque"; }
            if (cv.header[i] == "y_unit") { cv.cells[r * cv.columnCount + i] = "N*m"; }
        }
    }
    const CatalogImporter importer;
    const CatalogPackageSnapshot v2 =
        importer.assemble(in, makeBaselineManifest("cat-demo", "v2"));

    const CatalogDiff diff = CatalogDiffer{}.compare(makeV1(), v2);
    const CurveDiff* eff = findCurveDiff(diff, "curve-eff");
    ASSERT_NE(eff, nullptr);
    EXPECT_EQ(eff->kind, CurveDiff::Kind::Modified);
    ASSERT_EQ(eff->fieldChanges.size(), 2u);
    EXPECT_EQ(eff->fieldChanges[0].field, "yQuantity");
    EXPECT_EQ(eff->fieldChanges[0].oldText, "power");
    EXPECT_EQ(eff->fieldChanges[0].newText, "torque");
    EXPECT_EQ(eff->fieldChanges[1].field, "yUnit");
    EXPECT_EQ(eff->fieldChanges[1].oldText, "W");
    EXPECT_EQ(eff->fieldChanges[1].newText, "N*m");
    EXPECT_TRUE(eff->addedPoints.empty());
    EXPECT_TRUE(eff->removedPoints.empty());
}

/**
 * 兼容关系增量黄金用例（全键三列）：M-200|G-120 的 mount_kind
 * flange-mount → shaft-mount＝旧键移除＋新键新增（全键语义——安装方式
 * 演进不被误判为无差异）；M-100|G-50 未变零条目。
 */
TEST(SelCatalogDiff, CompatibilityDeltaAddedRemoved)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08"},
                  std::vector<std::string>{"AT-08"});
    ParsedCatalogInput in = makeBaselineInput();
    ParsedFileTable& cp = mutableFile(in, kCatalogFileCompatibility);
    EXPECT_TRUE(editRow(cp, "motor_model_id", "M-200", "mount_kind", "shaft-mount"));
    const CatalogImporter importer;
    const CatalogPackageSnapshot v2 =
        importer.assemble(in, makeBaselineManifest("cat-demo", "v2"));

    const CatalogDiff diff = CatalogDiffer{}.compare(makeV1(), v2);
    ASSERT_EQ(diff.compatibility.size(), 2u);
    // 排序契约：motorId→gearboxId→mountKind 升序（M-100|G-50 未变——零
    // 条目；差异两条同为 M-200|G-120 全键对：同键下 Removed<Added）。
    EXPECT_EQ(diff.compatibility[0].kind, CompatibilityDiff::Kind::Removed);
    EXPECT_EQ(diff.compatibility[0].record.motorId, "M-200");
    EXPECT_EQ(diff.compatibility[0].record.gearboxId, "G-120");
    EXPECT_EQ(diff.compatibility[0].record.mountKind, "flange-mount");  // 旧安装方式移除
    EXPECT_EQ(diff.compatibility[1].kind, CompatibilityDiff::Kind::Added);
    EXPECT_EQ(diff.compatibility[1].record.motorId, "M-200");
    EXPECT_EQ(diff.compatibility[1].record.gearboxId, "G-120");
    EXPECT_EQ(diff.compatibility[1].record.mountKind, "shaft-mount");   // 新安装方式加入
}

// =====================================================================
// 纯函数确定性与契约违约
// =====================================================================

/**
 * 纯函数确定性（NFR-COR-01/02）：同输入两次 compare 结果全等；输出序
 * 契约（电机先→modelId 升序）。同时钉住 CON-05 失效链：diff 非空 ⇔
 * 两快照包内容身份不同（变更任何业务字段→新内容身份）。
 */
TEST(SelCatalogDiff, PureFunctionDeterminismAndIdentityChain)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08", "CON-05"},
                  std::vector<std::string>{"NFR-COR-02"});
    const CatalogPackageSnapshot v1 = makeV1();
    const CatalogPackageSnapshot v2 = makeV2TorqueChange();
    const CatalogDiffer differ;

    const CatalogDiff a = differ.compare(v1, v2);
    const CatalogDiff b = differ.compare(v1, v2);
    EXPECT_EQ(a, b);                                   // 纯函数——同输入恒同输出
    EXPECT_FALSE(a.empty());
    EXPECT_FALSE(v1.contentIdentity == v2.contentIdentity);  // 内容身份已变
    // 方向对称性（黄金面）：反向比较的 models 条目镜像（G-50 Modified
    // 对称；新增/移除互换不在此展开——方向语义由 from/to 身份承载）。
    const CatalogDiff reverse = differ.compare(v2, v1);
    ASSERT_EQ(reverse.models.size(), a.models.size());
    const ModelDiff* fwd = findModelDiff(a, false, "G-50");
    const ModelDiff* rev = findModelDiff(reverse, false, "G-50");
    ASSERT_NE(fwd, nullptr);
    ASSERT_NE(rev, nullptr);
    EXPECT_EQ(fwd->fieldChanges[0].oldText, rev->fieldChanges[0].newText);  // 60↔50
    EXPECT_EQ(fwd->fieldChanges[0].newText, rev->fieldChanges[0].oldText);
}

/**
 * 契约违约 fail-fast（调用方错误——卡 §14.0）：①无身份快照不可比较；
 * ②主表 modelId 重复（装配产物唯一性前提——直构快照同责）。
 */
TEST(SelCatalogDiff, ContractViolationsFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08"},
                  std::vector<std::string>{"NFR-COR-03"});
    const CatalogPackageSnapshot v1 = makeV1();
    const CatalogDiffer differ;

    // ①包内容身份无效（全零）——CON-05 无身份不可比较。
    CatalogPackageSnapshot ghost = v1;
    ghost.contentIdentity = sdurws::ird::core::ContentIdentity{};  // 清零——无效身份
    EXPECT_THROW(static_cast<void>(differ.compare(ghost, v1)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(differ.compare(v1, ghost)), std::invalid_argument);

    // ②主表 modelId 重复——直构快照绕过 assemble（唯一性前提破坏，
    // SEL-CATALOG-DUPLICATE-ID 同责）；身份字段填有效值以到达 ID 检查。
    CatalogPackageSnapshot dup;
    dup.manifest = makeBaselineManifest("cat-demo", "v9");
    MotorCatalogEntry m1;
    m1.modelId = "M-DUP";
    m1.ratedTorque = 1.0;   // 单位 N·m（直构字段不参与比较，仅需满足结构）
    MotorCatalogEntry m2 = m1;  // 同 ID 第二行
    dup.motors = {m1, m2};
    dup.contentIdentity = computePackageContentIdentity(dup);  // 身份有效
    EXPECT_THROW(static_cast<void>(differ.compare(v1, dup)), std::invalid_argument);
}

// =====================================================================
// SEL-08 核心组合：目录更新不静默改变历史结果
// =====================================================================

/**
 * ★ 核心验收（SEL-08"目录更新不静默改变历史结果"）：锁定 v1 → 历史选
 * 型结果引用 v1 身份 → 锁定 v2（同 catalogId 版本并存——只增）→
 * ①load(v1 引用) 仍返回 v1 冻结快照（全等原快照——历史证据零污染）；
 * ②listLocked 两条版本并存（PA-2 只增不删）；③diff(v1,v2) 报告业务差
 * 异（升级影响提示）；④历史结果的目录身份块与 load(v1) 身份逐字节一致
 * ——目录更新后历史结果保持原目录版本切片身份（CON-02）。
 */
TEST(SelCatalogDiff, CatalogUpdateKeepsHistoryIntact)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08", "CON-02"},
                  std::vector<std::string>{"AT-08"});
    const CatalogImporter importer;
    const CatalogPackageSnapshot v1 = makeV1();
    const CatalogPackageSnapshot v2 = makeV2TorqueChange();  // G-50 转矩 50→60

    // 项目锁定版本（写入即锁定、只增——catalog/<id>/<ver>/ 本域执行面；
    // lockObjectId 为 project 对象库 ID 的值语义承载）。
    InMemoryCatalogProvider provider;
    const CatalogVersion lockV1 =
        provider.lockVersion(v1, sdurws::ird::core::ObjectId::fromCanonical("obj-000000000000000000000000000000a1"));
    const CatalogVersion lockV2 =
        provider.lockVersion(v2, sdurws::ird::core::ObjectId::fromCanonical("obj-000000000000000000000000000000a2"));

    // 历史选型结果的目录身份块（导入 v1 时期产出——引用 v1 锁定版本）。
    const CatalogIdentity historicalCatalog = v1.manifest.identity;

    // ①历史引用经 v2 落位后仍解析为 v1 冻结快照（全等——零静默变更）。
    const CatalogPackageSnapshot loaded = provider.load(lockV1);
    EXPECT_EQ(loaded, v1);
    EXPECT_EQ(loaded.manifest.identity, historicalCatalog);
    // ②同 catalogId 两版本并存（只增不删）。
    const std::vector<CatalogVersion> locked = provider.listLocked();
    ASSERT_EQ(locked.size(), 2u);
    EXPECT_EQ(locked[0].identity.version, "v1");  // catalogId→version 升序
    EXPECT_EQ(locked[1].identity.version, "v2");
    // ③差异比较报告业务变更（升级影响提示——G-50 转矩黄金行）。
    const CatalogDiff diff = CatalogDiffer{}.compare(
        provider.load(lockV1), provider.load(lockV2));
    EXPECT_FALSE(diff.empty());
    const ModelDiff* g50 = findModelDiff(diff, false, "G-50");
    ASSERT_NE(g50, nullptr);
    ASSERT_EQ(g50->fieldChanges.size(), 1u);
    EXPECT_EQ(g50->fieldChanges[0].field, "ratedOutputTorque");
    EXPECT_EQ(g50->fieldChanges[0].oldText, "50");  // 单位 N·m
    EXPECT_EQ(g50->fieldChanges[0].newText, "60");  // 单位 N·m
    // ④历史身份与 v1 锁定身份逐字节一致（内容寻址——CON-05/CON-02）：
    // 目录更新后历史结果的当前性按其自身切片身份独立判定（Superseded
    // 语义归 evidence——本域保证的是引用对象不被改写）。
    EXPECT_EQ(historicalCatalog, provider.load(lockV1).manifest.identity);
    EXPECT_EQ(historicalCatalog.contentIdentity, v1.contentIdentity);
}
