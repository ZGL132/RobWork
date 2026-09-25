/**
 * @file   Editor.cpp
 * @brief  IRequirementEditor 的产品实现——基线闭包解码、编辑差值应用
 *         （四段校验链＋删除引用保护）、模板/镜像/阵列批次应用（一次入栈
 *         一次整体回滚）、局部撤销/重做与变更摘要。
 *
 * 设计依据：units/requirements.md §9.3（接口契约）、§4.6（编辑态三态）、
 * §5.1（删除引用保护）、§4.7（I-REQ-2/3 集合唯一性）、§3.4（编辑器仅
 * UI 线程）；任务契约 tasks/foundation/WP-14-T03.json acceptance 4/5；
 * tasks/foundation/WP-14-T07.json acceptance 4/5（批量入口＋删除提示）。
 *
 * 线程约束：本 TU 全部状态操作仅限 UI 线程（§3.4 总约定 2——非线程安全
 * 是契约面而非实现缺陷；不做加锁）。
 */

#include <sdurws/ird/requirements/Editor.hpp>

#include <sdurws/ird/requirements/DiagCodes.hpp>  // REQ-DERIVE-SOURCE-REMOVED 稳定码常量（§9.6 表尾增登——删除提示产码）

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace sdurws::ird::requirements {

namespace {

/// 载入失败错误构造（闭包违约/解码失败——@pre 行语义）。
RequirementError loadFailure(const std::string& what)
{
    RequirementError e;
    e.code = RequirementErrorCode::MalformedPayload;
    e.detail = "requirements/editor: " + what;
    return e;
}

/// 集合成员的通用访问（工作集四集合的 upsert/erase/名称表——模板分发）。
template <class Entry>
const Entry* findEntry(const std::vector<Entry>& entries, const core::ObjectId& id)
{
    for (const auto& e : entries) {
        if (e.objectId == id) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace

// =====================================================================
// 载入基线（§9.3 loadBaseline）
// =====================================================================

RequirementLoadOutcome RequirementEditor::loadBaseline(const RequirementObjectClosureView& closure)
{
    RequirementLoadOutcome out;
    const RequirementCodec codec;

    // ---- ①根对象路由（@pre 行原文："closure 含 req-set（token 路由）"）。
    const auto rootObj = closure.tryObjectByToken(kReqSetObjectType);
    if (!rootObj) {
        out.error = loadFailure("闭包内无 req-set 根对象（§9.3 @pre——token 路由）");
        return out;
    }
    auto rootParsed = codec.decode(rootObj->bytes, kCurrentRequirementFormatVersion);
    if (!rootParsed.ok()) {
        out.error = rootParsed.error();  // 版本面 SchemaVersionUnsupported 原样透传（@pre 行）
        return out;
    }
    if (!std::holds_alternative<RequirementSet>(rootParsed.get())) {
        out.error = loadFailure("req-set token 对象解码为非根对象（闭包 token 与字节不一致）");
        return out;
    }
    RequirementWorkingSet next;
    next.root = std::get<RequirementSet>(std::move(rootParsed.get()));

    // ---- ②四集合解引用（根引用表→闭包取回→token 复核→解码）。
    const auto loadSet = [&](const std::optional<core::ObjectId>& ref,
                             std::string_view expectToken, auto& slot) -> bool {
        if (!ref.has_value()) {
            return true;  // 引用缺席＝空集合对象未建（合法——§4.7 空集合合法可保存）
        }
        const auto obj = closure.tryObject(*ref);
        if (!obj) {
            out.error = loadFailure("闭包内缺失集合对象 " + std::string(expectToken)
                                    + "（根引用表悬空——闭包违约）");
            return false;
        }
        if (obj->objectTypeToken != expectToken) {
            out.error = loadFailure("集合对象 token 失配（期望 "
                                    + std::string(expectToken) + "，实际 "
                                    + obj->objectTypeToken + "）");
            return false;
        }
        auto parsed = codec.decode(obj->bytes, kCurrentRequirementFormatVersion);
        if (!parsed.ok()) {
            out.error = parsed.error();
            return false;
        }
        using SlotT = std::decay_t<decltype(slot)>;
        if (!std::holds_alternative<SlotT>(parsed.get())) {
            out.error = loadFailure("集合对象解码形态与 token 不符（"
                                    + std::string(expectToken) + "）");
            return false;
        }
        slot = std::get<SlotT>(std::move(parsed.get()));
        return true;
    };
    if (!loadSet(next.root.pointSetRef, kReqPointSetObjectType, next.points)
        || !loadSet(next.root.regionSetRef, kReqRegionSetObjectType, next.regions)
        || !loadSet(next.root.conditionSetRef, kReqConditionSetObjectType, next.conditions)
        || !loadSet(next.root.planSetRef, kReqPlanSetObjectType, next.plans)) {
        return out;  // 失败＝保持原状（不半更新——NFR-COR-03）
    }
    // 跨集合 id 唯一复核（I-REQ-2 跨集合半区——解码校验链只查集合内，
    // 跨集合在工作集装配点复核）。
    if (auto e = checkCrossSetIdUniqueness(next.points.entries, next.regions.entries,
                                           next.conditions.entries, next.plans.entries)) {
        out.error = std::move(*e);
        return out;
    }
    // ---- ③重置为基线态（@post——工作集/栈/计数/摘要全部重置）。
    ws_ = std::move(next);
    // baseRevisionId：闭包抽象（对象面取回）不携带修订身份——基线修订 id
    // 由命令提交面（T05，从命令信封）补齐；本实现置空串，draftStatus 以
    // 空串呈现"基线未标注"（最小闭包口径，登记于头注）。
    baseRevisionId_.clear();
    undoStack_.clear();
    redoStack_.clear();
    summary_.clear();
    editCount_ = 0;
    loaded_ = true;
    out.ok = true;
    return out;
}

// =====================================================================
// 编辑应用（§9.3 applyEdit——四段校验链＋删除引用保护）
// =====================================================================

namespace {

/// upsert 通用实现（Add/Update：同 id 替换、否则插入；插入后按 id 排序
/// 维护 I-REQ-1；返回 false＝条目级/集合级校验失败——err 已置）。
template <class Entry, class ValidateFn>
bool upsertEntry(std::vector<Entry>& entries, Entry incoming, const char* subject,
                 RequirementError& err, ValidateFn&& validate)
{
    // ①条目级不变量（服务/解码同源——NFR-MNT-04）。
    if (auto e = validate(incoming)) {
        err = std::move(*e);
        return false;
    }
    // ②集合级唯一性：名称唯一（I-REQ-3——除自身外核对）＋id 唯一（集合内）。
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const bool sameId = entries[i].objectId == incoming.objectId;
        if (!sameId && entries[i].name == incoming.name) {
            err = RequirementError{};
            err.code = RequirementErrorCode::DuplicateName;
            err.params.emplace_back("name", incoming.name);
            err.detail = std::string("requirements/editor: 集合内名称重复（I-REQ-3——")
                       + subject + "；构造边界拒绝，不静默加后缀，NFR-COR-03）";
            return false;
        }
    }
    // 跨集合 id 唯一性在 applyEdit 主体以快照核对（此处只查本集合——
    // 其余三集合的核对需要全集视图）。
    // ③写入（upsert）＋排序（I-REQ-1）。
    bool replaced = false;
    for (auto& e : entries) {
        if (e.objectId == incoming.objectId) {
            e = incoming;  // Update——ObjectId 不变（跨修订稳定，O-36）
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        entries.push_back(std::move(incoming));
    }
    sortEntriesByObjectId(entries);
    return true;
}

/// Remove 编辑的目标集合是否存在该条目＋从集合移除（存在性核对——
/// 删除不存在的条目＝调用方错误拒绝）。
template <class Entry>
bool eraseEntry(std::vector<Entry>& entries, const core::ObjectId& id)
{
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].objectId == id) {
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

}  // namespace

EditOutcome RequirementEditor::applyEdit(const RequirementEdit& edit)
{
    EditOutcome out;
    if (!loaded_) {
        out.error = loadFailure("编辑器未载入基线（loadBaseline 前置违约——调用方错误）");
        return out;
    }
    // 编辑前快照（撤销栈深拷贝——类注取舍；仅在通过全部校验后入栈）。
    RequirementWorkingSet snapshot = ws_;
    std::string line;

    const bool dispatched = std::visit(
        [&](auto&& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TaskPoint>) {
                if (!upsertEntry(ws_.points.entries, value, "任务点", out.error,
                                 validateTaskPoint)) {
                    return false;
                }
                line = "任务点 " + value.name + " 已更新";
                return true;
            } else if constexpr (std::is_same_v<T, WorkRegion>) {
                if (!upsertEntry(ws_.regions.entries, value, "工作区域", out.error,
                                 validateWorkRegion)) {
                    return false;
                }
                line = "工作区域 " + value.name + " 已更新";
                return true;
            } else if constexpr (std::is_same_v<T, OperatingCondition>) {
                if (!upsertEntry(ws_.conditions.entries, value, "工况", out.error,
                                 validateOperatingCondition)) {
                    return false;
                }
                line = "工况 " + value.name + " 已更新";
                return true;
            } else if constexpr (std::is_same_v<T, SamplingPlan>) {
                if (auto e = validateSamplingPlan(value)) {
                    out.error = std::move(*e);
                    return false;
                }
                // 计划条目无 name——集合级核对只有 id；跨集合核对在主体。
                bool replaced = false;
                for (auto& p : ws_.plans.entries) {
                    if (p.objectId == value.objectId) {
                        p = value;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    ws_.plans.entries.push_back(value);
                }
                sortEntriesByObjectId(ws_.plans.entries);
                line = "采样计划已更新（区域 " + value.regionRef.toCanonical() + "）";
                return true;
            } else if constexpr (std::is_same_v<T, std::pair<std::string, std::string>>) {
                // 根头编辑（{name, note}）。
                if (value.first.empty()) {
                    out.error = RequirementError{};
                    out.error.code = RequirementErrorCode::DuplicateName;
                    out.error.detail = "requirements/editor: 需求集名称为空（进报告的"
                                       "语义名——空名无定位意义，§4.2）";
                    return false;
                }
                ws_.root.name = value.first;
                ws_.root.note = value.second;
                line = "需求集头已更新（" + value.first + "）";
                return true;
            } else {
                // Remove：{条目 id, 集合种类}。
                const auto& [id, memberInt] = value;
                const auto member = static_cast<WorkingSetMember>(memberInt);
                bool removed = false;
                switch (member) {
                case WorkingSetMember::Points: {
                    // 删除引用保护（§5.1——编辑边界拒绝＋定位）：
                    //   a. 被工况 appliesTo.stations 引用；
                    //   b. 被工况 events.stationRef 引用；
                    //   c. 被其他任务点 sequenceKey（前驱名引用）引用。
                    const auto* target = findEntry(ws_.points.entries, id);
                    if (!target) {
                        out.error = loadFailure("删除目标不存在（任务点 "
                                                + id.toCanonical() + "）");
                        return false;
                    }
                    for (const auto& c : ws_.conditions.entries) {
                        for (const auto& s : c.appliesTo.stations) {
                            if (s == id) {
                                out.error = loadFailure("任务点 " + target->name
                                                        + " 被工况 " + c.name
                                                        + " 的 appliesTo 引用（§5.1 删除"
                                                          "引用保护——先解除绑定）");
                                return false;
                            }
                        }
                        for (const auto& ev : c.events) {
                            if (ev.stationRef == id) {
                                out.error = loadFailure("任务点 " + target->name
                                                        + " 被工况 " + c.name
                                                        + " 的 event.stationRef 引用"
                                                          "（§5.1 删除引用保护）");
                                return false;
                            }
                        }
                    }
                    for (const auto& p : ws_.points.entries) {
                        if (!(p.objectId == id) && p.sequenceKey.has_value()
                            && *p.sequenceKey == target->name) {
                            out.error = loadFailure("任务点 " + target->name
                                                    + " 被 " + p.name
                                                    + " 的顺序键引用（§5.1 删除引用保护）");
                            return false;
                        }
                    }
                    removed = eraseEntry(ws_.points.entries, id);
                    if (removed) {
                        line = "任务点 " + target->name + " 已删除";
                        // 删除保护提示（§7.2 删除保护行——源删除**允许**，
                        // 但若仍有 linked 派生条目引用该源（参数快照
                        // "source-id"），产 REQ-DERIVE-SOURCE-REMOVED
                        // warning 知情登记：派生条目独立性不受影响，仅
                        // 提示重生成将不可再解析该源）。
                        std::size_t linkedDerived = 0;
                        for (const auto& p : ws_.points.entries) {
                            if (p.generation.has_value() && p.generation->linked) {
                                for (const auto& kv : p.generation->parameters) {
                                    if (kv.first == kGenParamSourceId
                                        && kv.second == id.toCanonical()) {
                                        ++linkedDerived;
                                        break;  // 每条派生条目计一次
                                    }
                                }
                            }
                        }
                        if (linkedDerived > 0) {
                            out.diagnostics.push_back(core::DiagnosticRecord::make(
                                std::string{kReqDeriveSourceRemoved},
                                std::nullopt, std::nullopt, std::nullopt,
                                "entry=" + target->name + "; linked-derived="
                                    + std::to_string(linkedDerived),
                                "该源仍有 " + std::to_string(linkedDerived)
                                    + " 条 linked 派生（派生条目独立性不受影响；"
                                      "源删除后其批次重生成将不可再解析该源）",
                                "如需保持批次可再生成为，请先恢复该源或解除相关"
                                "条目的关联（unlinkGenerator）"));
                        }
                    }
                    break;
                }
                case WorkingSetMember::Regions: {
                    const auto* target = findEntry(ws_.regions.entries, id);
                    if (!target) {
                        out.error = loadFailure("删除目标不存在（区域 "
                                                + id.toCanonical() + "）");
                        return false;
                    }
                    // 被采样计划 regionRef 引用（§9.7 关系图——计划→区域
                    // 子条目引用；删除前先删/改计划）。
                    for (const auto& p : ws_.plans.entries) {
                        if (p.regionRef == id) {
                            out.error = loadFailure("区域 " + target->name
                                                    + " 被采样计划引用（§9.7——先解除"
                                                      "计划绑定）");
                            return false;
                        }
                    }
                    removed = eraseEntry(ws_.regions.entries, id);
                    if (removed) {
                        line = "工作区域 " + target->name + " 已删除";
                    }
                    break;
                }
                case WorkingSetMember::Conditions:
                    removed = eraseEntry(ws_.conditions.entries, id);
                    if (removed) {
                        line = "工况已删除（" + id.toCanonical() + "）";
                    } else {
                        out.error = loadFailure("删除目标不存在（工况 "
                                                + id.toCanonical() + "）");
                        return false;
                    }
                    break;
                case WorkingSetMember::Plans:
                    removed = eraseEntry(ws_.plans.entries, id);
                    if (removed) {
                        line = "采样计划已删除（" + id.toCanonical() + "）";
                    } else {
                        out.error = loadFailure("删除目标不存在（计划 "
                                                + id.toCanonical() + "）");
                        return false;
                    }
                    break;
                }
                return removed;
            }
        },
        edit);
    if (!dispatched) {
        return out;  // 拒绝——工作集/栈不变（@post"拒绝：字节不变"）
    }
    // 跨集合 id 唯一复核（I-REQ-2 跨集合半区——全集视图核对）。
    if (auto e = checkCrossSetIdUniqueness(ws_.points.entries, ws_.regions.entries,
                                           ws_.conditions.entries, ws_.plans.entries)) {
        ws_ = std::move(snapshot);  // 违例→回滚快照（拒绝面）
        out.error = std::move(*e);
        out.error.detail = "requirements/editor: " + out.error.detail + "（I-REQ-2 跨集合半区）";
        out.diagnostics.clear();  // 拒绝＝零产出（删除提示等警告面随编辑作废）
        return out;
    }
    // 接受——@post：撤销入栈＋重做栈清空＋计数/摘要推进。
    undoStack_.push_back(UndoStep{std::move(snapshot), line});
    redoStack_.clear();
    if (!summary_.empty()) {
        summary_ += "\n";
    }
    summary_ += line;
    ++editCount_;
    out.accepted = true;
    out.changeSummary = line;
    return out;
}

// =====================================================================
// 批次应用（§9.3 applyEdit 行"批量变体/模板镜像阵列批次"——T07 增列；
// 一次 applyEdit＝一次入栈＝一次整体回滚，V-06 批次面）
// =====================================================================

EditOutcome RequirementEditor::applyEdit(const EditBatch& batch)
{
    EditOutcome out;
    if (!loaded_) {
        out.error = loadFailure("编辑器未载入基线（loadBaseline 前置违约——调用方错误）");
        return out;
    }
    // ok=false 的批次传入＝调用方契约违约（服务产出错误批次未检查即
    // 应用——fail-fast 拒绝，不代为解析 error 字段）。
    if (!batch.ok) {
        out.error = batch.error;
        out.error.detail = "requirements/editor: 批次非 ok 态（服务产出错误未"
                           "检查即应用——调用方契约违约）: " + out.error.detail;
        return out;
    }
    // 空批次（无新条目且无替换）＝无事可做的调用方错误（避免制造
    // "空撤销步"污染撤销栈语义）。
    if (batch.newPoints.empty() && batch.replaceNames.empty()) {
        out.error = loadFailure("批次为空（无新条目且无替换名——无编辑语义）");
        return out;
    }

    // 编辑前快照（批量原子性的回滚底座——任一校验失败即整体丢弃）。
    RequirementWorkingSet snapshot = ws_;

    // ---- ①替换名核验（重生成批次——先移除后写入的"替换"语义）。
    //      被工况 appliesTo/events 引用的条目一旦被替换（新 ObjectId）即
    //      产生悬空引用——§5.1 删除保护同源拒绝（顺序键按名引用且替换
    //      保名，不受影响，不作保护面）。
    for (const auto& name : batch.replaceNames) {
        const auto it = std::find_if(ws_.points.entries.begin(), ws_.points.entries.end(),
                                     [&name](const TaskPoint& p) { return p.name == name; });
        if (it == ws_.points.entries.end()) {
            out.error = loadFailure("替换目标不存在（任务点 " + name
                                    + "——批次相对工作集已过期）");
            return out;
        }
        const core::ObjectId id = it->objectId;
        for (const auto& c : ws_.conditions.entries) {
            for (const auto& s : c.appliesTo.stations) {
                if (s == id) {
                    out.error = loadFailure("替换目标 " + name + " 被工况 " + c.name
                                            + " 的 appliesTo 引用（§5.1 保护——"
                                              "替换产生新 ObjectId，先解除绑定）");
                    return out;
                }
            }
            for (const auto& ev : c.events) {
                if (ev.stationRef == id) {
                    out.error = loadFailure("替换目标 " + name + " 被工况 " + c.name
                                            + " 的 event.stationRef 引用（§5.1 保护）");
                    return out;
                }
            }
        }
    }
    // ---- ②新条目逐条校验＋批内互斥名预检（任一失败＝整批拒绝，不落半批）。
    std::vector<std::string> batchNames;  // 批内新条目名（互斥核对）
    for (const auto& p : batch.newPoints) {
        if (auto e = validateTaskPoint(p)) {
            out.error = std::move(*e);
            out.error.detail = "requirements/editor: 批次条目 " + p.name
                             + " 校验失败: " + out.error.detail;
            return out;
        }
        // 名称唯一三面核对：工作集既有名＋替换名（替换先移除——其名会被
        // 替换条目重新占用，须豁免）＋批内其他新条目名（I-REQ-3）。
        const bool releasedByReplace =
            std::find(batch.replaceNames.begin(), batch.replaceNames.end(), p.name)
            != batch.replaceNames.end();
        for (const auto& existing : ws_.points.entries) {
            if (existing.name == p.name && !releasedByReplace) {
                out.error = RequirementError{};
                out.error.code = RequirementErrorCode::DuplicateName;
                out.error.params.emplace_back("name", p.name);
                out.error.detail = "requirements/editor: 集合内名称重复（I-REQ-3——"
                                   "批次整批拒绝，不静默改写，NFR-COR-03）";
                return out;
            }
        }
        for (const auto& prior : batchNames) {
            if (prior == p.name) {
                out.error = RequirementError{};
                out.error.code = RequirementErrorCode::DuplicateName;
                out.error.params.emplace_back("name", p.name);
                out.error.detail = "requirements/editor: 批次内名称重复（I-REQ-3）";
                return out;
            }
        }
        batchNames.push_back(p.name);
        // 集合内 id 唯一（新条目持临时句柄——与既有条目撞 id 即违约；
        // 跨集合半区在下方全集核对统一执行）。
        for (const auto& existing : ws_.points.entries) {
            if (existing.objectId == p.objectId) {
                out.error = RequirementError{};
                out.error.code = RequirementErrorCode::MalformedPayload;
                out.error.params.emplace_back("object-id", p.objectId.toCanonical());
                out.error.detail = "requirements/editor: 批次条目 ObjectId 与工作集"
                                   "既有条目冲突（I-REQ-2——身份不混用）";
                return out;
            }
        }
    }

    // ---- ③写入（先替换移除、后追加新条目＋排序）——校验全部通过后才
    //      动工作集（拒绝面不产生部分写入）。
    for (const auto& name : batch.replaceNames) {
        auto& entries = ws_.points.entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&name](const TaskPoint& p) { return p.name == name; }),
                      entries.end());
    }
    for (const auto& p : batch.newPoints) {
        ws_.points.entries.push_back(p);
    }
    sortEntriesByObjectId(ws_.points.entries);

    // ---- ④跨集合 id 唯一复核（I-REQ-2 跨集合半区——全集视图核对，
    //      与单条 applyEdit 同源；违例回滚快照）。
    if (auto e = checkCrossSetIdUniqueness(ws_.points.entries, ws_.regions.entries,
                                           ws_.conditions.entries, ws_.plans.entries)) {
        ws_ = std::move(snapshot);
        out.error = std::move(*e);
        out.error.detail = "requirements/editor: " + out.error.detail
                         + "（I-REQ-2 跨集合半区）";
        return out;
    }

    // ---- ⑤接受——恰一次撤销入栈（一次入栈、一次整体回滚的批次面）＋
    //      批次警告诊断透传（拒绝面已全部返回，此处恒为接受面）。
    const std::string line =
        batch.summary.empty() ? ("批次应用 " + std::to_string(batch.newPoints.size())
                               + " 条")
                              : batch.summary;
    undoStack_.push_back(UndoStep{std::move(snapshot), line});
    redoStack_.clear();
    if (!summary_.empty()) {
        summary_ += "\n";
    }
    summary_ += line;
    ++editCount_;
    out.accepted = true;
    out.changeSummary = line;
    out.diagnostics = batch.diagnostics;
    return out;
}

// =====================================================================
// 局部撤销/重做（§9.3 undoLocal/redoLocal——零修订）
// =====================================================================

bool RequirementEditor::undoLocal() noexcept
{
    // 无已载入基线/空栈＝无可撤销——工作集不变（返回 false 不抛）。
    if (!loaded_ || undoStack_.empty()) {
        return false;
    }
    // 弹栈顶：当前工作集入重做栈、快照回工作集（逆序回退一步）。
    UndoStep step = std::move(undoStack_.back());
    undoStack_.pop_back();
    redoStack_.push_back(UndoStep{ws_, std::move(step.summaryLine)});
    ws_ = std::move(step.snapshot);
    // 摘要回退（截掉最后一步的行——按行逆推）。
    const auto pos = summary_.rfind('\n');
    summary_ = (pos == std::string::npos) ? std::string{} : summary_.substr(0, pos);
    return true;
}

bool RequirementEditor::redoLocal() noexcept
{
    if (!loaded_ || redoStack_.empty()) {
        return false;
    }
    UndoStep step = std::move(redoStack_.back());
    redoStack_.pop_back();
    undoStack_.push_back(UndoStep{ws_, std::move(step.summaryLine)});
    ws_ = std::move(step.snapshot);
    if (!summary_.empty()) {
        summary_ += "\n";
    }
    summary_ += step.summaryLine;
    return true;
}

RequirementDraftStatus RequirementEditor::draftStatus() const
{
    RequirementDraftStatus st;
    st.dirty = loaded_ && editCount_ > 0;
    st.baseRevisionId = baseRevisionId_;
    st.edits = editCount_;
    return st;
}

}  // namespace sdurws::ird::requirements
