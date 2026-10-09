/**
 * @file   EditorTest.cpp
 * @brief  需求编辑器用例组（ReqEditor）——基线载入/编辑/局部撤销重做/
 *         删除引用保护/草稿状态（任务契约 WP-14-T03 acceptance 4 的编辑
 *         边界面＋acceptance 5 的 O-36 锚面）。
 *
 * 设计依据：units/requirements.md §9.3（IRequirementEditor 契约）、§4.6
 * （编辑态三态——局部撤销零修订）、§5.1（删除引用保护——编辑边界拒绝
 * ＋定位）、§4.7（I-REQ-2/3 集合唯一性）、§3.4（编辑器仅 UI 线程）。
 */

#include <sdurws/ird/requirements/Editor.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D<double>——位姿/盒/采样字段的框架值类型

#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace sdurws::ird::requirements;

/// core 命名空间别名（测试内 ObjectId/SourcedValue/ValueProvenance 直写面）。
namespace core = sdurws::ird::core;

namespace {

/// 测试用闭包字节源：按 token/id 注册的内存映射（确定性取回）。
class MapClosure final : public RequirementObjectClosureView {
public:
    void put(const std::string& token, const RequirementObjectVariant& object)
    {
        const RequirementCodec codec;
        auto bytes = codec.encode(object, kCurrentRequirementFormatVersion);
        // 测试夹具：编码必过（对象由夹具构造为合法形态）。
        if (bytes.ok()) {
            byToken_[token] = RequirementClosureObject{token, bytes.get()};
        }
    }

    void putById(const core::ObjectId& id, const std::string& token,
                 const RequirementObjectVariant& object)
    {
        const RequirementCodec codec;
        auto bytes = codec.encode(object, kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byId_[id] = RequirementClosureObject{token, bytes.get()};
        }
    }

    std::optional<RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        const auto it = byToken_.find(std::string{objectTypeToken});
        if (it == byToken_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<RequirementClosureObject> tryObject(const core::ObjectId& objectId) const override
    {
        const auto it = byId_.find(objectId);
        if (it == byId_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    std::map<std::string, RequirementClosureObject> byToken_;
    std::map<core::ObjectId, RequirementClosureObject> byId_;
};

/// 合法任务点。
TaskPoint makePoint(const std::string& name)
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 装配"根＋四集合"基线闭包（集合 id 固定——可复现）。
void fillBaseline(MapClosure& closure, PointSet points, RegionSet regions,
                  ConditionSet conditions, PlanSet plans)
{
    RequirementSet root;
    const core::ObjectId pointSetId =
        core::ObjectId::fromCanonical("obj-10000000000000000000000000000001");
    const core::ObjectId regionSetId =
        core::ObjectId::fromCanonical("obj-20000000000000000000000000000002");
    const core::ObjectId condSetId =
        core::ObjectId::fromCanonical("obj-30000000000000000000000000000003");
    const core::ObjectId planSetId =
        core::ObjectId::fromCanonical("obj-40000000000000000000000000000004");
    root.name = "基线需求集";
    root.pointSetRef = pointSetId;
    root.regionSetRef = regionSetId;
    root.conditionSetRef = condSetId;
    root.planSetRef = planSetId;
    closure.put(std::string{kReqSetObjectType}, RequirementObjectVariant{root});
    closure.putById(pointSetId, std::string{kReqPointSetObjectType},
                    RequirementObjectVariant{points});
    closure.putById(regionSetId, std::string{kReqRegionSetObjectType},
                    RequirementObjectVariant{regions});
    closure.putById(condSetId, std::string{kReqConditionSetObjectType},
                    RequirementObjectVariant{conditions});
    closure.putById(planSetId, std::string{kReqPlanSetObjectType},
                    RequirementObjectVariant{plans});
}

}  // namespace

/**
 * 载入基线（§9.3 loadBaseline）：根路由＋四集合解引用解码——工作集五对
 * 象齐全＋dirty=false；闭包缺根对象拒绝（@pre 行）。
 */
TEST(ReqEditor, LoadBaselineFromClosure_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-01"},
                  std::vector<std::string>{"ACC4-loadBaseline"});

    MapClosure closure;
    PointSet points;
    points.entries.push_back(makePoint("P1"));
    fillBaseline(closure, points, RegionSet{}, ConditionSet{}, PlanSet{});

    RequirementEditor editor;
    auto out = editor.loadBaseline(closure);
    ASSERT_TRUE(out.ok) << out.error.detail;
    EXPECT_EQ(editor.workingSet().root.name, "基线需求集");
    ASSERT_EQ(editor.workingSet().points.entries.size(), 1U);
    EXPECT_EQ(editor.workingSet().points.entries[0].name, "P1");
    EXPECT_FALSE(editor.draftStatus().dirty);

    // 反例：闭包缺 req-set 根对象——@pre 拒绝。
    MapClosure emptyClosure;
    RequirementEditor editor2;
    auto out2 = editor2.loadBaseline(emptyClosure);
    ASSERT_FALSE(out2.ok);
}

/**
 * 编辑/局部撤销/重做（§9.3 applyEdit/undoLocal/redoLocal）：接受→工作集
 * 推进＋撤销入栈；撤销→回退一步；重做→重放；重做栈在新编辑后清空
 * （@post）；局部撤销零修订（编辑器面——项目级撤销归 project）。
 */
TEST(ReqEditor, ApplyEditUndoRedoFlow_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"ACC4-undo-redo"});

    MapClosure closure;
    fillBaseline(closure, PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);

    // 编辑 1：新增任务点。
    const TaskPoint p1 = makePoint("P1");
    auto e1 = editor.applyEdit(RequirementEdit{p1});
    ASSERT_TRUE(e1.accepted) << e1.error.detail;
    EXPECT_EQ(editor.workingSet().points.entries.size(), 1U);
    EXPECT_TRUE(editor.draftStatus().dirty);
    EXPECT_EQ(editor.draftStatus().edits, 1U);
    EXPECT_NE(e1.changeSummary.find("P1"), std::string::npos) << "人读中文摘要";

    // 编辑 2：新增区域。
    WorkRegion r1;
    r1.objectId = core::ObjectId::generate();
    r1.name = "R1";
    r1.box = BoundingBox{rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                         rw::math::Vector3D<double>(1.0, 1.0, 1.0)};
    auto e2 = editor.applyEdit(RequirementEdit{r1});
    ASSERT_TRUE(e2.accepted);
    EXPECT_EQ(editor.draftStatus().edits, 2U);

    // 撤销一步：区域回退（局部撤销——零修订）。
    EXPECT_TRUE(editor.undoLocal());
    EXPECT_EQ(editor.workingSet().regions.entries.size(), 0U);
    EXPECT_EQ(editor.workingSet().points.entries.size(), 1U) << "只回退一步";

    // 重做一步：区域重放。
    EXPECT_TRUE(editor.redoLocal());
    EXPECT_EQ(editor.workingSet().regions.entries.size(), 1U);

    // 撤销到底再新编辑——重做栈清空（@post）。
    EXPECT_TRUE(editor.undoLocal());
    EXPECT_TRUE(editor.undoLocal());
    EXPECT_FALSE(editor.undoLocal()) << "栈空＝无可撤销（工作集不变）";
    TaskPoint p2 = makePoint("P2");
    auto e3 = editor.applyEdit(RequirementEdit{p2});
    ASSERT_TRUE(e3.accepted);
    EXPECT_FALSE(editor.redoLocal()) << "新编辑后重做栈清空";
    EXPECT_EQ(editor.draftStatus().edits, 3U) << "编辑计数差值推进（撤销不减）";
    EXPECT_NE(editor.buildChangeSummary().find("P2"), std::string::npos);
}

/**
 * 编辑拒绝面（§9.3 @post"拒绝：字节不变＋逐项诊断"）：重名/非法条目/
 * 跨集合 id 冲突——拒绝后工作集与撤销栈不变。
 */
TEST(ReqEditor, ApplyEditRejectionsKeepBytesUntouched_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03", "I-REQ-2"},
                  std::vector<std::string>{"ACC4-edit-reject"});

    MapClosure closure;
    PointSet points;
    points.entries.push_back(makePoint("P1"));
    fillBaseline(closure, points, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);
    const auto before = editor.workingSet();

    // 反例 1：重名（I-REQ-3——DuplicateName，不静默加后缀）。
    TaskPoint dup = makePoint("P1");
    auto r1 = editor.applyEdit(RequirementEdit{dup});
    ASSERT_FALSE(r1.accepted);
    EXPECT_EQ(r1.error.code, RequirementErrorCode::DuplicateName);

    // 反例 2：非法条目（AllDofFree）。
    TaskPoint free = makePoint("P2");
    free.pose.constrainedDof = ConstrainedDof{};
    auto r2 = editor.applyEdit(RequirementEdit{free});
    ASSERT_FALSE(r2.accepted);
    EXPECT_EQ(r2.error.code, RequirementErrorCode::AllDofFree);

    // 反例 3：跨集合 id 冲突（I-REQ-2——区域复用任务点 id）。
    WorkRegion r;
    r.objectId = editor.workingSet().points.entries[0].objectId;
    r.name = "R-ok";
    r.box = BoundingBox{rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                        rw::math::Vector3D<double>(1.0, 1.0, 1.0)};
    auto r3 = editor.applyEdit(RequirementEdit{r});
    ASSERT_FALSE(r3.accepted);
    EXPECT_EQ(r3.error.code, RequirementErrorCode::MalformedPayload);

    // 拒绝后字节不变：工作集全等＋撤销栈空＋dirty=false。
    EXPECT_EQ(editor.workingSet(), before);
    EXPECT_FALSE(editor.draftStatus().dirty);
}

/**
 * 删除引用保护（§5.1——编辑边界拒绝＋定位）：删除被 appliesTo/
 * events.stationRef/顺序键引用的任务点、被计划 regionRef 引用的区域——
 * 拒绝；解除引用后删除成功。
 */
TEST(ReqEditor, RemoveReferenceProtection_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-04", "REQ-03"},
                  std::vector<std::string>{"ACC4-remove-protection"});

    MapClosure closure;
    // 点集：Station（被引用）＋Free（无引用）。
    PointSet points;
    const TaskPoint station = makePoint("Station");
    const TaskPoint free = makePoint("Free");
    points.entries.push_back(station);
    points.entries.push_back(free);
    // 区域：被计划引用。
    RegionSet regions;
    WorkRegion region;
    region.objectId = core::ObjectId::generate();
    region.name = "R1";
    region.box = BoundingBox{rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                             rw::math::Vector3D<double>(1.0, 1.0, 1.0)};
    regions.entries.push_back(region);
    // 工况：appliesTo＋event 都绑 Station。
    ConditionSet conditions;
    OperatingCondition cond;
    cond.objectId = core::ObjectId::generate();
    cond.name = "C1";
    cond.appliesTo.scope = AppliesToScope::Stations;
    cond.appliesTo.stations.push_back(station.objectId);
    ConditionEvent ev;
    ev.type = ConditionEventType::Grasp;
    ev.stationRef = station.objectId;
    cond.events.push_back(ev);
    conditions.entries.push_back(cond);
    // 计划：regionRef 指向区域。
    PlanSet plans;
    SamplingPlan plan;
    plan.objectId = core::ObjectId::generate();
    plan.regionRef = region.objectId;
    plan.positionSampling.method = PositionSamplingMethod::Grid;
    plan.orientationSampling = OrientationSampling{};
    plans.entries.push_back(plan);
    fillBaseline(closure, points, regions, conditions, plans);

    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);

    // 反例 1：删除被 appliesTo 引用的 Station——拒绝＋定位。
    auto r1 = editor.applyEdit(removeEdit(station.objectId, WorkingSetMember::Points));
    ASSERT_FALSE(r1.accepted);
    EXPECT_NE(r1.error.detail.find("Station"), std::string::npos) << "定位被引用条目";

    // 反例 2：删除被计划引用的区域——拒绝。
    auto r2 = editor.applyEdit(removeEdit(region.objectId, WorkingSetMember::Regions));
    ASSERT_FALSE(r2.accepted);

    // 反例 3：删除被顺序键引用的点（先解除工况绑定面——Free 被 Station
    // 的顺序键引用场景另行装配；此处以独立编辑链验证顺序键保护）。
    TaskPoint child = makePoint("Child");
    child.sequenceKey = "Free";
    ASSERT_TRUE(editor.applyEdit(RequirementEdit{child}).accepted);
    auto r3 = editor.applyEdit(removeEdit(free.objectId, WorkingSetMember::Points));
    ASSERT_FALSE(r3.accepted);
    EXPECT_NE(r3.error.detail.find("Free"), std::string::npos);

    // 正例：无引用的 Child 删除成功。
    auto r4 = editor.applyEdit(removeEdit(child.objectId, WorkingSetMember::Points));
    ASSERT_TRUE(r4.accepted) << r4.error.detail;
    EXPECT_EQ(editor.workingSet().points.entries.size(), 2U) << "Station＋Free 保留";
}

/**
 * O-36 锚面＋schema 版本透传（acceptance 5＋§9.3 @pre）：编辑器 upsert
 * 不改条目 id（跨修订稳定锚）；载入未来 schema 字节→
 * RequirementError(SchemaVersionUnsupported)（§9.3 @pre 行原文）。
 */
TEST(ReqEditor, O36_AnchorStableAndSchemaRejection_WP14T03_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04", "NFR-DEP-04"},
                  std::vector<std::string>{"ACC5-O36", "ACC4-schema"});

    MapClosure closure;
    PointSet points;
    points.entries.push_back(makePoint("P1"));
    fillBaseline(closure, points, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);

    // O-36：Update（同 id 再提交）不改 ObjectId——诊断 subjectObjectId 锚
    // 跨编辑稳定；名称变更＝内容变更（新字节）而 id 不变。
    TaskPoint renamed = editor.workingSet().points.entries[0];
    renamed.name = "P1-renamed";
    auto e = editor.applyEdit(RequirementEdit{renamed});
    ASSERT_TRUE(e.accepted);
    ASSERT_EQ(editor.workingSet().points.entries.size(), 1U);
    EXPECT_TRUE(editor.workingSet().points.entries[0].objectId == renamed.objectId)
        << "锚跨修订/编辑稳定（O-36 模型内标识口径）";

    // schema 版本透传：载入头 major=99 的根字节→SchemaVersionUnsupported
    // （§9.3 @pre 行"解码失败→RequirementError(SchemaVersionUnsupported)"）。
    MapClosure futureClosure;
    fillBaseline(futureClosure, PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    auto rootBytes = futureClosure.tryObjectByToken(kReqSetObjectType);
    ASSERT_TRUE(rootBytes.has_value());
    RequirementBytes tampered = rootBytes->bytes;
    ASSERT_GE(tampered.size(), 11U);
    tampered[7] = 99;  // 头 major 低字节（小端）
    class TamperedClosure final : public RequirementObjectClosureView {
    public:
        explicit TamperedClosure(RequirementBytes bytes) { root_ = RequirementClosureObject{"req-set", std::move(bytes)}; }
        std::optional<RequirementClosureObject> tryObjectByToken(std::string_view t) const override
        {
            if (t == "req-set") { return root_; }
            return std::nullopt;
        }
        std::optional<RequirementClosureObject> tryObject(const core::ObjectId&) const override { return std::nullopt; }
    private:
        RequirementClosureObject root_{"req-set", {}};
    };
    TamperedClosure tamperedClosure(std::move(tampered));
    RequirementEditor editor2;
    auto out = editor2.loadBaseline(tamperedClosure);
    ASSERT_FALSE(out.ok);
    EXPECT_EQ(out.error.code, RequirementErrorCode::SchemaVersionUnsupported);
}

// =====================================================================
// UI-T35 P1-1 空项目需求集初始化（R2 审核整改——内存空根闭包载入路径
// 的域侧 UT：空根合法态＋生命周期编辑可用＋根槽 allocateNew 语义）。
// =====================================================================

/// 内存空根闭包（宿主 EmptyRequirementsClosure 同语义——测试直构版）。
class EmptyRootClosure final : public RequirementObjectClosureView {
public:
    EmptyRootClosure()
    {
        RequirementSet root;
        root.name = "需求集";
        RequirementCodec codec;
        auto bytes = codec.encode(RequirementObjectVariant{root},
                                  kCurrentRequirementFormatVersion);
        rootBytes_ = bytes.ok() ? bytes.get() : RequirementBytes{};
    }

    std::optional<RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        if (objectTypeToken == kReqSetObjectType) {
            return RequirementClosureObject{std::string(kReqSetObjectType),
                                            rootBytes_};
        }
        return std::nullopt;  // 四集合未挂载（空项目）
    }

    std::optional<RequirementClosureObject> tryObject(
        const core::ObjectId&) const override
    {
        return std::nullopt;
    }

private:
    RequirementBytes rootBytes_;
};

/// 空根载入：成功＋四集合空＋生命周期首编辑（新增工位）可用——
/// R2 审核 P1-1 的域侧证据（wireRequirementsSession 空项目分支的
/// 数据前提）。
TEST(ReqEditor, EmptyRootLoadBaselineAndFirstEdit_UI_T35)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{});

    EmptyRootClosure closure;
    RequirementEditor editor;
    auto out = editor.loadBaseline(closure);
    ASSERT_TRUE(out.ok) << out.error.detail;
    EXPECT_TRUE(editor.workingSet().points.entries.empty());
    EXPECT_TRUE(editor.workingSet().regions.entries.empty());

    // 生命周期首编辑可用：新增工位（upsert）→集合 +1＋dirty。
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = "工位";
    p.pose.constrainedDof.z = true;  // 至少约束一个分量（I-REQ-5）
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    auto r1 = editor.applyEdit(RequirementEdit{p});
    ASSERT_TRUE(r1.accepted) << r1.error.detail;
    EXPECT_TRUE(editor.draftStatus().dirty);
    EXPECT_EQ(editor.draftStatus().edits, 1U);

    // 复制（第二个 upsert——不同 id 不冲突）。
    TaskPoint p2 = p;
    p2.objectId = core::ObjectId::generate();
    p2.name = "工位 2";
    auto r2 = editor.applyEdit(RequirementEdit{p2});
    ASSERT_TRUE(r2.accepted);
    EXPECT_EQ(editor.workingSet().points.entries.size(), 2U);
}

/// 空根载入后删除不存在条目：Rejected 零变更（空集合删除的域拒绝面）。
TEST(ReqEditor, EmptyRootRemoveNonexistentRejected_UI_T35)
{
    IRD_TEST_INFO(std::vector<std::string>{"I-REQ-2"},
                  std::vector<std::string>{});

    EmptyRootClosure closure;
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);
    auto r = editor.applyEdit(removeEdit(core::ObjectId::generate(),
                                         WorkingSetMember::Points));
    EXPECT_FALSE(r.accepted) << "空集合删除不应接受";
}

// =====================================================================
// F-583/F-585 回归（audit/unit-code-review-20261009）
// =====================================================================

/**
 * 回归（F-583，P1）：编辑器边界拒空名——upsert 与批次两条路径此前只查
 * 重名，空名条目入集破坏"工作集恒为可编码形态"承诺（下一次就绪预检
 * 抛 invalid_argument——值面拒绝退化成 fail-fast）。服务构造/解码门
 * 早已拒空名，本钉补齐第三生产者。
 */
TEST(ReqEditor, EmptyNameRejectedAtEditorBoundary_F583)
{
    IRD_TEST_INFO(std::vector<std::string>{"I-REQ-3"},
                  std::vector<std::string>{});

    MapClosure closure;
    fillBaseline(closure, PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);

    // upsert 面：空名任务点拒绝（DuplicateName 同口径——服务构造一致）。
    TaskPoint unnamed = makePoint("P-TEMP");
    unnamed.name.clear();
    auto e = editor.applyEdit(RequirementEdit{unnamed});
    EXPECT_FALSE(e.accepted) << "空名条目必须被编辑边界拒绝";
    EXPECT_EQ(e.error.code, RequirementErrorCode::DuplicateName);
    EXPECT_EQ(editor.workingSet().points.entries.size(), 0U) << "拒绝面零写入";

    // 批次面：空名新条目整批拒绝（批次原子性——零部分写入）。
    EditBatch batch;
    batch.newPoints.push_back(makePoint("P-OK"));
    TaskPoint unnamedBatch = makePoint("P-TEMP2");
    unnamedBatch.name.clear();
    batch.newPoints.push_back(unnamedBatch);
    auto b = editor.applyEdit(batch);
    EXPECT_FALSE(b.accepted) << "批次含空名条目必须整批拒绝";
    EXPECT_EQ(editor.workingSet().points.entries.size(), 0U) << "批次拒绝零写入";
}

/**
 * 回归（F-585，P1）：删除非末位条目后摘要/诊断解引用已悬垂的 target
 * 指针——删非末位时读到的是后继条目的名字（张冠李戴），末位时读失效
 * 遗留串（UB）。修复：删除前快照名称。
 */
TEST(ReqEditor, RemoveNonLastEntrySummaryUsesDeletedName_F585)
{
    IRD_TEST_INFO(std::vector<std::string>{"I-REQ-3"},
                  std::vector<std::string>{});

    MapClosure closure;
    fillBaseline(closure, PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);

    auto e1 = editor.applyEdit(RequirementEdit{makePoint("P-ALPHA")});
    auto e2 = editor.applyEdit(RequirementEdit{makePoint("P-BETA")});
    ASSERT_TRUE(e1.accepted);
    ASSERT_TRUE(e2.accepted);
    ASSERT_EQ(editor.workingSet().points.entries.size(), 2U);

    // 取 P-ALPHA 的 id（按 objectId 排序后它可能非末位——删除非末位条目
    // 才触发"后继左移覆盖 target 槽位"的悬垂面）。
    core::ObjectId alphaId;
    for (const auto& p : editor.workingSet().points.entries) {
        if (p.name == "P-ALPHA") { alphaId = p.objectId; }
    }
    ASSERT_TRUE(alphaId.isValid());

    auto rm = editor.applyEdit(removeEdit(alphaId, WorkingSetMember::Points));
    ASSERT_TRUE(rm.accepted) << rm.error.detail;
    // 摘要必须引用被删条目自己的名字（修复前非末位删除读到的是后继名
    // "P-BETA"——张冠李戴；本断言即回归钉）。
    EXPECT_NE(rm.changeSummary.find("P-ALPHA"), std::string::npos)
        << "删除摘要应引用被删条目名（实际：" << rm.changeSummary << "）";
    EXPECT_EQ(editor.workingSet().points.entries.size(), 1U);
    EXPECT_EQ(editor.workingSet().points.entries[0].name, "P-BETA");
}
