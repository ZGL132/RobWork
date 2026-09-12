/**
 * @file   Currentness.cpp
 * @brief  当前性纯投影的实现——computeCurrentness 五步算法（§8.1 计算步骤
 *         原文逐条承载）＋失效原因比对＋诊断构造＋CurrentnessIndex 会话缓存。
 *
 * 设计依据：units/evidence.md §8.1/§4.2.4/§10.1⑥/§10.4、CON-02/CON-05、
 * 任务契约 tasks/foundation/EV-T08.json（≙WP-05-T08）；实现口径 I-1～I-6
 * 登记于契约头 Currentness.hpp 文件头注释与单元卡 v0.9 变更记录。
 *
 * 实现纪律：
 *   - 纯投影：本文件全部函数无 I/O、无全局可变状态、不修改任何入参
 *     （CurrentnessIndex 的内存缓存除外——会话态，见其实现）；
 *   - 目标 sliceId 计算唯一经 SliceBuilder（CR-02——摘要算法唯一，禁止
 *     在此复制任何身份计算）；
 *   - 不可判定恒以 status=空＋诊断表达，禁止默认 Current（任务约束§五.8）；
 *   - 输出确定性：reasons 按 (kind,key) 字典序、诊断按声明序（同输入必
 *     同输出，NFR-COR-02）。
 */

#include <sdurws/ird/evidence/Currentness.hpp>

namespace sdurws::ird::evidence {

// =====================================================================
// 内部辅助（全部为局部纯函数——无共享状态，文件外不可见）
// =====================================================================

namespace {

/**
 * @brief 构造一条当前性诊断记录（建议码＝kDiagCurrentness* 常量；码值
 *        权威归 diagnostics StableCodeRegistry——本函数只承载记录本体，
 *        不承担注册职责，PA-1）。
 *
 * context 固定 "currentness-project"（诊断来源上下文——ui/reporting 消费
 * 时可据此区分当前性投影诊断与评估器层诊断）；localName/runtimeName 置空
 * （名称反解归消费方经 runtime⑥端口——R-4，投影层不拼接名称）。
 *
 * @param code    [in] 建议码（EVI-* 形态——DiagCode 句法合法）
 * @param subject [in] 主体对象（可空——依赖键级问题无对象锚；Object 类
 *                无法解析时其 oid 在 nullopt 场景不可得，恒空）
 * @param cause   [in] 原因描述（人读中文——必须非空，C-3 校验）
 * @param action  [in] 建议动作（人读中文——必须非空，C-3 校验）
 * @return 诊断记录（make() 工厂产出——句法/必填校验通过）
 */
core::DiagnosticRecord makeCurrentnessDiag(std::string_view code,
                                           std::optional<core::ObjectId> subject,
                                           std::string cause,
                                           std::string action)
{
    return core::DiagnosticRecord::make(std::string{code}, std::move(subject),
                                        std::nullopt, std::nullopt,
                                        "currentness-project", std::move(cause),
                                        std::move(action));
}

/**
 * @brief 按 DependencyKind 把条目载荷差异映射为失效类别（§8.1 步骤 4 词表
 *        的 kind→kind 对照——条目级比对的单点映射，保证同 Kind 同映射）。
 *
 * Environment→EnvironmentChanged 与"整体兜底"SliceContentChanged 不经本
 * 函数（前者同表映射，后者无条目可依——直接在调用点构造）。
 *
 * @param kind [in] 涉事条目的依赖 Kind（全 7 值均有映射——全函数）
 * @return 对应的失效类别
 */
InvalidationKind invalidationKindFor(DependencyKind kind) noexcept
{
    switch (kind) {
    case DependencyKind::Object:         return InvalidationKind::ObjectContentChanged;
    case DependencyKind::Configuration:  return InvalidationKind::ConfigurationChanged;
    case DependencyKind::Policy:         return InvalidationKind::PolicyChanged;
    case DependencyKind::NameMap:        return InvalidationKind::NameMapChanged;
    case DependencyKind::SampleSet:      return InvalidationKind::SampleBaselineChanged;
    case DependencyKind::UpstreamResult: return InvalidationKind::UpstreamResultChanged;
    case DependencyKind::Environment:    return InvalidationKind::EnvironmentChanged;
    }
    // 不可达（switch 全枚举无 default——遗漏新值编译器告警暴露）；返回
    // 兜底值仅为消除告警路径——与 SliceContentChanged 的"无条目语义"不同，
    // 此处永不产出。
    return InvalidationKind::SliceContentChanged;
}

/**
 * @brief 提取条目载荷的"旧→新"差异摘要文本（detail 数据源——确定性：
 *        同差异必同文本，NFR-COR-02）。
 *
 * 各 Kind 取其身份承载字段：Object 取两版 contentVersion、Configuration 取
 * 配置内容身份、Policy/NameMap 取内容身份、SampleSet 取样本集身份、
 * UpstreamResult 取上游切片身份、Environment 取要素值 token。规范文本
 * （cv-/cid- 前缀形态）保证跨进程可读且逐字节确定。
 *
 * @param oldEntry [in] 结果侧（历史）条目
 * @param newEntry [in] 目标侧（当前）条目
 * @return 形如 "旧 <A> → 新 <B>" 的中文摘要
 */
std::string payloadDiffDetail(const DependencyEntry& oldEntry,
                              const DependencyEntry& newEntry)
{
    // 旧值提取：载荷在场且 Kind 对应才取身份文本，否则以占位说明（防御——
    // 调用点保证 Kind 相同，此分支仅为可读性兜底）。
    const auto describe = [](const DependencyEntry& e) -> std::string {
        switch (e.kind) {
        case DependencyKind::Object: {
            const auto& p = std::get<ObjectDependencyPayload>(e.payload);
            return p.contentVersion.toCanonical();
        }
        case DependencyKind::Configuration: {
            const auto& p = std::get<ConfigurationDependencyPayload>(e.payload);
            return p.contentIdentity.toCanonical();
        }
        case DependencyKind::Policy: {
            const auto& p = std::get<PolicyDependencyPayload>(e.payload);
            return p.policyContentIdentity.toCanonical();
        }
        case DependencyKind::NameMap: {
            const auto& p = std::get<NameMapDependencyPayload>(e.payload);
            return p.nameMapContentIdentity.toCanonical();
        }
        case DependencyKind::SampleSet: {
            const auto& p = std::get<SampleSetDependencyPayload>(e.payload);
            return p.sampleSetIdentity.toCanonical();
        }
        case DependencyKind::UpstreamResult: {
            const auto& p = std::get<UpstreamResultDependencyPayload>(e.payload);
            return p.upstreamSliceId.toCanonical();
        }
        case DependencyKind::Environment: {
            const auto& p = std::get<EnvironmentDependencyPayload>(e.payload);
            return p.valueToken;
        }
        }
        return "<载荷不可读>";   // 不可达（全枚举 switch——防御占位）
    };
    return "旧 " + describe(oldEntry) + " → 新 " + describe(newEntry);
}

/**
 * @brief 逐条目比对生成失效原因清单（§8.1 步骤 4"逐条目失效原因清单"）。
 *
 * 比对序（输出序＝确定性来源）：先按目标条目序（SliceBuilder 冻结时的
 * (kind,key) 字典序——§4.2.2 稳定存储）遍历；再按历史条目序补"仅在历史
 * 切片出现"的条目。同 (kind,key) 的判定优先级：条件翻转（applied 差异）
 * 先于载荷差异——条件翻转通常会连带载荷重解析，条件语义是首要提示。
 *
 * @param oldEntries [in] 结果侧切片条目（SliceBuilder 冻结序）
 * @param newEntries [in] 目标侧重建切片条目（同一冻结序）
 * @return 失效原因清单（确定性顺序；Current 场景调用方不调用本函数）
 */
std::vector<InvalidationReason> diffEntries(const std::vector<DependencyEntry>& oldEntries,
                                            const std::vector<DependencyEntry>& newEntries)
{
    std::vector<InvalidationReason> reasons;
    // (kind,key) → 条目 的查找 lambda（比对期线性扫描——条目数小且已按
    // (kind,key) 冻结序排列；免建哈希表，保持入参 const）。
    auto findIn = [](const std::vector<DependencyEntry>& entries,
                     const DependencyEntry& probe) -> const DependencyEntry* {
        for (const DependencyEntry& e : entries) {
            if (e.kind == probe.kind && e.key == probe.key) {
                return &e;
            }
        }
        return nullptr;
    };

    // 第一遍：目标条目——新旧都有（比对差异）或仅目标有（条目新增）。
    for (const DependencyEntry& n : newEntries) {
        const DependencyEntry* o = findIn(oldEntries, n);
        if (o == nullptr) {
            // 仅目标上下文存在的条目＝历史切片没有消费过该键的当前值——
            // 身份必然不同，按 Kind 给出条目级原因（detail 注明方向）。
            reasons.push_back({n.key, invalidationKindFor(n.kind),
                               "该依赖条目仅在当前上下文出现（历史切片无此条目）"});
            continue;
        }
        // 优先级 1：条件适用性翻转（D-10——applied/notAppliedReason 是
        // 条目身份的一部分，翻转即切片身份变化的首要语义）。
        if (o->applied != n.applied) {
            reasons.push_back({n.key, InvalidationKind::ConditionFlipped,
                               std::string{"条件适用性翻转："}
                                   + (o->applied ? "历史已适用" : "历史未适用")
                                   + " → "
                                   + (n.applied ? "当前已适用" : "当前未适用")});
            continue;
        }
        // 优先级 2：载荷差异——按 Kind 映射失效类别，detail 携带旧→新
        // 身份文本（KIN-13 重算提示的数据源，§4.2.4）。
        if (!(*o == n)) {
            reasons.push_back({n.key, invalidationKindFor(n.kind),
                               payloadDiffDetail(*o, n)});
        }
        // 完全相等（applied＋载荷全同）→ 不产原因（理论上不该发生——
        // 全部条目相等则 sliceId 相等走 Current 分支；防御性跳过）。
    }

    // 第二遍：历史条目中目标没有的（条目在当前上下文消失——如目标上下文
    // 的依赖解析不再产出该键；历史序遍历，输出序仍确定）。
    for (const DependencyEntry& o : oldEntries) {
        if (findIn(newEntries, o) == nullptr) {
            reasons.push_back({o.key, invalidationKindFor(o.kind),
                               "该依赖条目已从当前上下文消失（目标重建无此条目）"});
        }
    }
    return reasons;
}

}  // namespace

// =====================================================================
// ResultRef——envelope→身份摘要投影
// =====================================================================

ResultRef ResultRef::fromEnvelope(const ResultEnvelope& envelope)
{
    ResultRef r;
    // 五要素逐一拷贝（§8.1 输入第 1 项的字段清单）——不触碰载荷/证据/
    // 诊断（当前性判定不消费它们）。
    r.task = envelope.task;
    r.evaluationKey = envelope.evaluationKey;
    r.evaluatorContractVersion = envelope.evaluatorContractVersion;
    r.sliceId = envelope.sliceId;
    r.inputBaselineId = envelope.inputBaselineId;
    return r;
}

// =====================================================================
// computeCurrentness——五步算法（步骤序＝§8.1 原文）
// =====================================================================

CurrentnessResult computeCurrentness(const ResultRef& result,
                                     const CurrentnessTarget& target,
                                     const CurrentnessSources& sources)
{
    // ---- 步骤 0（调用方契约前置检查——fail-fast，不计入算法步骤）----
    // 重建事实面未注入＝无法重建目标切片，属调用方组装违约（§8.1 输入
    // 第 3 项是必填源）；空声明集＝"无依赖的评估"，没有失效语义（§4.2.2
    // 条目 ≥1 同源口径）。两者都应 fail-fast 而非产出不可判定结果——
    // 不可判定是"目标上下文的状态"，不是"调用方忘了给输入"。
    if (sources.targetSnapshot == nullptr || sources.facts == nullptr) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            std::string{"当前性目标重建事实面未注入：targetSnapshot 与 facts 均为必填（§8.1 输入第 3 项）"});
    }
    if (sources.declarations.empty()) {
        throw EvidenceError(EvidenceErrorCode::DeclarationInvalid,
                            std::string{"当前性重建声明集为空——无依赖的评估没有失效语义（§4.2.2 条目 ≥1 同源口径）"});
    }

    // 输出骨架：evaluatedAgainst 恒为输入 target 的原样快照（§8.1 步骤 5
    // "判定所相对什么"的记录面）。
    CurrentnessResult out;
    out.evaluatedAgainst = target;

    // ---- 步骤 1：上下文匹配（§8.1 步骤 1 原文＋规则表行 1）----
    // 当前性相对什么计算＝(project, branch, evaluationKey, 当前配置内容身份)
    // ——规则表行 1 的四要素定义"上下文"；本步骤执行其中可从结果引用取得
    // 的三个匹配面（当前配置内容身份经步骤 3 的目标条目解析进入身份比较
    // ——不在此单独比较）。原项目结果不能成为另一项目/会话的当前结果
    // （TASK-03；S7 切面的拦截点）。实现口径 I-6：结果侧 TaskIdentity 无
    // scheme 承载面，分支匹配按 branch（scheme 仅存于 target 记录面）。
    if (result.task.project != target.projectId
        || result.task.branch != target.branchId) {
        // 不可判定{cross-context}：不产生任何持久状态，仅诊断（EV-CUR-4；
        // P-EV-4 计算形态——status=空，不是第三持久态）。
        out.status = std::nullopt;
        out.unevaluableCause = UnevaluableCause::CrossContext;
        out.diagnostics.push_back(makeCurrentnessDiag(
            kDiagCurrentnessCrossContext, std::nullopt,
            "结果属于项目 " + result.task.project.toCanonical() + " 分支 "
                + result.task.branch.toCanonical() + "，与查询目标（项目 "
                + target.projectId.toCanonical() + " 分支 "
                + target.branchId.toCanonical() + "）不属同一上下文——历史结果"
                "不能跨上下文充当当前结果（TASK-03）",
            "在结果所属项目/分支内查询其当前性，或对当前上下文发起新评估"));
        return out;
    }
    // 评估键是上下文四要素之一（规则表行 1）——结果与查询不属于同一评估
    // 器时（拿 A 评估的结果查 B 评估的当前性），比较本身无语义，同为跨
    // 上下文形态（诊断文案区分失配维度，便于定位调用方组装错误）。
    if (result.evaluationKey != target.evaluationKey) {
        out.status = std::nullopt;
        out.unevaluableCause = UnevaluableCause::CrossContext;
        out.diagnostics.push_back(makeCurrentnessDiag(
            kDiagCurrentnessCrossContext, std::nullopt,
            "结果评估键 " + result.evaluationKey + " 与查询目标评估键 "
                + target.evaluationKey + " 不一致——评估键是当前性上下文的"
                "组成部分（§8.1 规则表行 1），跨评估键的当前性查询无语义",
            "以产生该结果的评估器键发起当前性查询，或核对目标上下文的评估键"));
        return out;
    }

    // ---- 步骤 2：契约匹配（§8.1 步骤 2 原文）----
    // 结果的评估器契约版本 ≠ 当前 descriptor 契约版本 → Superseded——
    // 算法在此短路：契约升级意味着依赖声明语义可能已变化，条目级比较失去
    // 前提（EV-CPA-2 同款保守方向）。HEAD/时间/修订号不参与（内容身份纪律）。
    if (result.evaluatorContractVersion != sources.currentContractVersion) {
        out.status = CurrentnessStatus::Superseded;
        // 契约变化无具体涉事条目（dependencyKey 置空——reason 结构注释）；
        // detail 携带两版号（十进制无符号），提示"重新评估而非复用"。
        out.reasons.push_back(
            {"", InvalidationKind::EvaluatorContractChanged,
             "评估器契约版本已变化：旧 " + std::to_string(result.evaluatorContractVersion)
                 + " → 新 " + std::to_string(sources.currentContractVersion)
                 + "（旧结果的依赖声明语义不可靠，须重评）"});
        return out;
    }

    // ---- 步骤 3：目标切片重建（§8.1 步骤 3 原文）----
    // 以同一 descriptor 的依赖声明（sources.declarations——步骤 2 已保证
    // 契约版本与结果一致），对目标上下文当前事实逐键解析当前条目。
    // 解析策略：**全量解析后再检查**——任一键失败都要把全部失败键列入
    // 诊断（缺失全量列出，NFR-COR-03/CORE 汇总层同口径），不在首个失败处
    // 短路，便于一次看清全部缺口。
    std::vector<DependencyEntry> targetEntries;
    std::vector<std::string> unresolvedKeys;
    for (const DependencyDeclaration& d : sources.declarations) {
        std::optional<DependencyEntry> entry = sources.facts->tryCurrentEntry(d);
        if (!entry.has_value()) {
            // 该依赖在目标上下文无法解析（对象缺失/资源缺失/工况对象不存在
            // ——§8.1 步骤 3 括号注记的三形态，由事实源归类）。
            unresolvedKeys.push_back(d.key);
            continue;
        }
        // 事实源返回的条目必须与声明同键同 Kind（声明—条目对账——
        // §4.2.3② 冻结期语义；不符＝事实源实现违约，fail-fast 而非静默
        // 纠正——静默纠正会掩盖事实源 bug，NFR-COR-03）。
        if (entry->key != d.key || entry->kind != d.kind) {
            throw EvidenceError(
                EvidenceErrorCode::DeclarationInvalid,
                "依赖事实源返回条目与声明不符：声明（key=" + d.key
                    + "，kind=" + std::string{dependencyKindToken(d.kind)}
                    + "）得到（key=" + entry->key + "，kind="
                    + std::string{dependencyKindToken(entry->kind)} + "）");
        }
        targetEntries.push_back(std::move(*entry));
    }
    if (!unresolvedKeys.empty()) {
        // 不可判定{unresolved-dependency}：**不得默认 Current**（任务约束
        // §五.8；EV-CUR-2 观测点＝status==nullopt 且 diagnostics 非空）。
        // 逐键一条诊断（声明序——确定性），全部无法解析键一次看全。
        out.status = std::nullopt;
        out.unevaluableCause = UnevaluableCause::UnresolvedDependency;
        for (const std::string& key : unresolvedKeys) {
            out.diagnostics.push_back(makeCurrentnessDiag(
                kDiagCurrentnessUnresolvedDependency, std::nullopt,
                "依赖在目标上下文无法解析：" + key
                    + "（对象缺失/资源缺失/工况对象不存在——当前性不可判定）",
                "补齐该依赖在当前上下文的对象后重新查询；不得在依赖缺失时"
                "将结果当作当前（无默认 Current）"));
        }
        return out;
    }

    // 解析齐全：经 SliceBuilder 在目标快照上冻结目标切片——sliceId 计算
    // 唯一经 SliceBuilder/SliceCodec（CR-02；本函数不复制任何身份计算）。
    // 评估面取目标键＋当前契约版本（§8.1 步骤 3"以同一 descriptor 重建"
    // 的身份面）。
    InputSlice targetSlice;
    try {
        SliceBuilder builder;
        builder.setEvaluation(target.evaluationKey, sources.currentContractVersion);
        builder.setConsumesCanonicalModel(sources.consumesCanonicalModel);
        for (DependencyEntry& e : targetEntries) {
            builder.addEntry(std::move(e));
        }
        targetSlice = builder.build(*sources.targetSnapshot);
    } catch (const EvidenceError& e) {
        // 目标切片冻结失败（最常见：事实源返回的 Object 条目 (oid,cv) 不在
        // 目标快照 objectClosure 内——事实源与快照非同一事实面，或目标上下文
        // 在组装与查询之间演进）。转译为不可判定{unresolved-dependency}：
        // 目标事实无法定格＝无法判定，同样不得默认 Current；原错误细节
        // （含就地定位）完整随诊断透出——不静默吞错（NFR-COR-03）。
        out.status = std::nullopt;
        out.unevaluableCause = UnevaluableCause::UnresolvedDependency;
        out.diagnostics.push_back(makeCurrentnessDiag(
            kDiagCurrentnessUnresolvedDependency, std::nullopt,
            std::string{"目标上下文切片重建失败（依赖事实无法冻结）："}
                + e.what(),
            "以同一目标上下文事实面重新组装快照与依赖解析后再次查询"));
        return out;
    }

    // ---- 步骤 4：比较（§8.1 步骤 4＋§5.1 身份纪律）----
    // 判据分层（实现口径 I-8，登记单元卡 v0.9）：
    //   a) sliceId 全值捷径：result.sliceId == targetSlice.sliceId ⇒ Current
    //      ——sliceId 编码含快照锚（SliceCodec full 的 [32] snapshotId，
    //      EV-T04 布局），全值相等意味着锚＋评估面＋条目全同（同一次冻结
    //      的重查询场景）；
    //   b) 条目级对比（主判据）：§5.1 身份纪律原文"失效判定用条目级
    //      ContentVersion/内容身份对比（哪个依赖条目变了），从不用'修订号
    //      变了'代替"。sliceId 编码含快照锚，而目标重建切片锚定目标快照
    //      （HEAD 前进 ⇒ 锚必不同），全值比较会让"HEAD 前进本身"出现在
    //      判定里——违反步骤 4 括号注记。故 sliceId 不等时按条目集逐条
    //      对比：无差异 ⇒ Current（锚/修订不参与失效判定——S7 正例）；
    //      有差异 ⇒ Superseded＋逐条目失效原因。
    //   c) 身份摘要形态降级（resultSlice 缺席，I-4）：结果侧条目不可得，
    //      只能做 sliceId 全值比较——跨修订（锚不同）恒 Superseded，保守
    //      方向（绝不误 Current，detail 注明降级原因）。
    const bool entriesIdentical = sources.resultSlice != nullptr
        && sources.resultSlice->entries == targetSlice.entries;
    if (result.sliceId == targetSlice.sliceId || entriesIdentical) {
        // Current：HEAD 前进本身不出现在判定里——仅当它造成被消费条目
        // 内容变化才在条目对比中体现（S7 正例：仅电机成本变更→运动学
        // 切片条目不变→Current）。reasons 恒空（presence 纪律）。
        out.status = CurrentnessStatus::Current;
        return out;
    }

    // Superseded：附逐条目失效原因。resultSlice 在场 → 条目级比对（设计
    // 主形态——KIN-13 重算提示数据源）；缺席（身份摘要形态，I-4）→ 单条
    // 整体原因。比对两侧都取 SliceBuilder 冻结产物（(kind,key) 字典序——
    // §4.2.2 稳定存储），reasons 输出序由此确定（NFR-COR-02）。
    out.status = CurrentnessStatus::Superseded;
    if (sources.resultSlice != nullptr) {
        out.reasons = diffEntries(sources.resultSlice->entries, targetSlice.entries);
        // 防御：sliceId 与条目对比均判不等，但条目级比对未发现差异——
        // 理论不可达（锚不同＋条目相同已走 Current 分支；落到这里说明
        // 条目集不等但 diff 无输出，属内部不变量破坏）。显式暴露而非
        // 静默空清单（NFR-COR-03——Superseded 必须有原因支撑）。
        if (out.reasons.empty()) {
            out.reasons.push_back(
                {result.evaluationKey, InvalidationKind::SliceContentChanged,
                 "切片内容身份不等但条目级比对无差异——切片非条目字段"
                 "（评估面/快照锚）与条目状态不一致，须核查切片冻结链路"});
        }
    } else {
        out.reasons.push_back(
            {result.evaluationKey, InvalidationKind::SliceContentChanged,
             "结果切片条目不可得（身份摘要形态）——切片全值身份不等（含"
             "快照锚定差异，保守判过期）：旧 " + result.sliceId.toCanonical()
                 + " → 新 " + targetSlice.sliceId.toCanonical()
                 + "；提供结果侧切片可执行条目级精确判定"});
    }
    return out;
}

// =====================================================================
// CurrentnessIndex——会话态投影缓存
// =====================================================================

std::optional<CurrentnessResult>
CurrentnessIndex::find(const core::RunId& runId, std::string_view targetContextId) const
{
    // 加锁查表（会话态缓存可能被事件线程与查询线程并发访问——头文件
    // 线程安全注释）；命中返回拷贝，调用方修改副本不影响缓存。
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(Key{runId, std::string{targetContextId}});
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    return it->second;
}

void CurrentnessIndex::store(const core::RunId& runId, std::string_view targetContextId,
                             const CurrentnessResult& result)
{
    // 缓存键契约：runId 非保留值＋上下文标识非空串（空串无法与"未指定"
    // 区分——presence 噪声；头文件 store 注释）。违约 fail-fast；码面复用
    // CacheIncompatible（缓存面的调用方入参违约——不新增错误码，Errors.hpp
    // 表尾追加纪律的保守执行）。
    if (!runId.isValid() || targetContextId.empty()) {
        throw EvidenceError(EvidenceErrorCode::CacheIncompatible,
                            "CurrentnessIndex 缓存键非法：runId 须非零且 targetContextId 须非空串");
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries[Key{runId, std::string{targetContextId}}] = result;
}

void CurrentnessIndex::invalidateRun(const core::RunId& runId)
{
    // 修订事件（RevisionCommitted/DependencyInvalidated——事件不携带数据，
    // core D-09）到达后的失效原语：按运行粒度整体失效（无法精确定位受
    // 影响目标上下文，保守正确）；幂等——无命中静默返回。
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->first.first == runId) {
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
}

void CurrentnessIndex::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

}  // namespace sdurws::ird::evidence
