/**
 * @file WorkingSetCommand.cpp
 * @brief 完整工作集一次规划：精确身份映射、完整引用校验与历史逆放。
 *
 * 设计依据：PM-04/PM-18、MDL-06/13/15，WP-13-T24 B4c-2。
 * project独占持久取号与写入；本实现只构造计划，失败不发布任何部件。
 */
#include "WorkingSetCommand.hpp"
#include <sdurws/ird/modeling/Codec.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace sdurws::ird::modeling {
namespace {
/** @brief 五对象的精确类型token；[in] value为Codec产物，返回权威token，不猜名称。 */
std::string_view tokenOf(const ObjectVariant& value)
{
    return std::visit([](const auto& typed)->std::string_view {
        using T=std::decay_t<decltype(typed)>;
        if constexpr(std::is_same_v<T,RobotDesign>){return kRobotDesignObjectType;}
        else if constexpr(std::is_same_v<T,ToolDefinition>){return kToolDefinitionObjectType;}
        else if constexpr(std::is_same_v<T,SceneObject>){return kSceneObjectObjectType;}
        else if constexpr(std::is_same_v<T,PoseSet>){return kNamedPoseSetObjectType;}
        else{return kRobotDrivetrainObjectType;}
    },value);
}

/** @brief 编码已通过解码且只做了身份回填的值；失败为内部契约违约，抛logic_error。 */
std::vector<std::uint8_t> encodeValue(const ObjectVariant& value)
{
    auto encoded=RobotDesignCodec{}.encode(value,kCurrentFormatVersion);
    if(!encoded.ok()){throw std::logic_error("完整工作集回填后编码失败："+encoded.error().detail);}
    return encoded.get();
}

/** @brief 从根收集完整部件引用及类型；重复/跨类别复用同ID返回空，输入不变。 */
std::optional<std::map<std::string,std::string>> partRefs(const RobotDesign& root)
{
    std::map<std::string,std::string> refs;
    const auto add=[&](const core::ObjectId& id,std::string_view token){
        return id.isValid()&&refs.emplace(id.toCanonical(),std::string(token)).second;
    };
    for(const auto& id:root.toolRefs){if(!add(id,kToolDefinitionObjectType)){return std::nullopt;}}
    for(const auto& id:root.sceneRefs){if(!add(id,kSceneObjectObjectType)){return std::nullopt;}}
    if(root.poseSetRef&&!add(*root.poseSetRef,kNamedPoseSetObjectType)){return std::nullopt;}
    if(root.drivetrainRef&&!add(*root.drivetrainRef,kRobotDrivetrainObjectType)){return std::nullopt;}
    return refs;
}

/** @brief 无效请求的唯一纯值结果；调用方共用prepare会清除拒绝态计划。 */
DecodeOutcome invalidWorkingSet()
{
    DecodeOutcome out;out.outcome=project::PrepareOutcome::RejectedInvalidInput;return out;
}

/**
 * @brief 根据固定基线规划完整正向或逆放命令，不执行写入/编译。
 * @param ctx [in,out] project同步上下文，仅调用objectId分配持久身份。
 * @param payload [in] v2帧，根首槽；完整正向禁止混入旧removals段。
 * @param revision [in] 本次固定修订，含全部保留对象，防临时身份碰撞。
 * @param baseline [in] 当前根引用的活动工作集。
 * @param out [out] 完整计划写入；失败由外层prepare清空，不得消费半计划。
 * @return 规划结果；结构错误为RejectedInvalidInput，真实存储错误透传。
 * 所有SI物理量归原Codec/断言，纯规划不另算任何机器人数学。
 */
DecodeOutcome planWorkingSet(project::HandlerContext& ctx,const CommandPayload& payload,
    const project::RevisionView& revision,const ModelingWorkingSet& baseline,project::CommandPlan& out)
{
    if(payload.objects.empty()||!payload.removals.empty()){return invalidWorkingSet();}
    const auto& rootSlot=payload.objects.front();
    if(rootSlot.objectTypeToken!=kRobotDesignObjectType){return invalidWorkingSet();}
    const auto decodedRoot=RobotDesignCodec{}.decode(rootSlot.objectBytes,kCurrentFormatVersion);
    if(!decodedRoot.ok()||!std::holds_alternative<RobotDesign>(decodedRoot.get())){return invalidWorkingSet();}
    auto root=std::get<RobotDesign>(decodedRoot.get());
    const bool restore=payload.mode==CommandPayload::Mode::Restore;
    if(rootSlot.allocateNew){
        if(restore||rootSlot.objectId.isValid()||baseline.rootObjectId){return invalidWorkingSet();}
    }else if(!baseline.rootObjectId||rootSlot.objectId!=*baseline.rootObjectId){return invalidWorkingSet();}

    // 第一阶段完整校验：引用键与槽身份严格一一对应，不按名称或数组位置猜关系。
    const auto refs=partRefs(root);if(!refs){return invalidWorkingSet();}
    std::set<std::string> known;
    for(const auto& ref:revision.objectRefs){known.insert(ref.objectId.toCanonical());}
    for(const auto& joint:baseline.design.joints){known.insert(joint.objectId.toCanonical());}
    for(const auto& link:baseline.design.links){known.insert(link.objectId.toCanonical());}
    for(const auto& joint:root.joints){known.insert(joint.objectId.toCanonical());}
    for(const auto& link:root.links){known.insert(link.objectId.toCanonical());}
    std::set<std::string> seen;
    std::vector<ObjectVariant> parts;
    for(std::size_t i=1;i<payload.objects.size();++i){
        const auto& slot=payload.objects[i];const auto key=slot.objectId.toCanonical();
        if(!slot.objectId.isValid()||!seen.insert(key).second){return invalidWorkingSet();}
        const auto decoded=RobotDesignCodec{}.decode(slot.objectBytes,kCurrentFormatVersion);
        if(!decoded.ok()||tokenOf(decoded.get())!=slot.objectTypeToken
           ||std::holds_alternative<RobotDesign>(decoded.get())){return invalidWorkingSet();}
        const bool innerMatches=std::visit([&](const auto& typed){
            using T=std::decay_t<decltype(typed)>;
            if constexpr(std::is_same_v<T,RobotDesign>){return false;}
            else{return typed.objectId==slot.objectId;}
        },decoded.get());
        if(!innerMatches){return invalidWorkingSet();}
        if(slot.allocateNew){
            if(restore||known.count(key)){return invalidWorkingSet();}
        }else{
            const auto found=std::find_if(revision.objectRefs.begin(),revision.objectRefs.end(),
                [&](const auto& ref){return ref.objectId==slot.objectId&&ref.objectTypeToken==slot.objectTypeToken;});
            if(found==revision.objectRefs.end()){return invalidWorkingSet();}
        }
        if(!restore){
            const auto ref=refs->find(key);
            if(ref==refs->end()||ref->second!=slot.objectTypeToken){return invalidWorkingSet();}
        }
        parts.push_back(decoded.get());
    }
    if(!restore&&seen.size()!=refs->size()){return invalidWorkingSet();}

    // 第二阶段取号及精确引用回填。仅新部件换ID，旧ID保持；引用集合的规范顺序仍由唯一Codec负责。
    const auto rootId=rootSlot.allocateNew?ctx.objectId():rootSlot.objectId;
    std::map<std::string,core::ObjectId> mapped;
    std::vector<project::ObjectWrite> writes(1);
    writes.front().objectId=rootId;writes.front().objectTypeToken=std::string(kRobotDesignObjectType);
    DecodeOutcome result;result.affectedOids.push_back(rootId);
    for(std::size_t i=1;i<payload.objects.size();++i){
        const auto& slot=payload.objects[i];auto value=parts[i-1];
        const auto id=slot.allocateNew?ctx.objectId():slot.objectId;
        mapped.emplace(slot.objectId.toCanonical(),id);
        std::visit([&](auto& typed){
            using T=std::decay_t<decltype(typed)>;
            if constexpr(!std::is_same_v<T,RobotDesign>){typed.objectId=id;}
        },value);
        project::ObjectWrite write;write.objectId=id;write.objectTypeToken=slot.objectTypeToken;
        write.payloadCanonical=restore?slot.objectBytes:encodeValue(value);
        writes.push_back(std::move(write));result.affectedOids.push_back(id);
    }
    if(!restore){
        const auto remap=[&](core::ObjectId& id){id=mapped.at(id.toCanonical());};
        for(auto& id:root.toolRefs){remap(id);}for(auto& id:root.sceneRefs){remap(id);}
        if(root.poseSetRef){remap(*root.poseSetRef);}if(root.drivetrainRef){remap(*root.drivetrainRef);}
        if(root.defaultTcp){
            const auto tool=mapped.find(root.defaultTcp->toolOid.toCanonical());
            if(tool==mapped.end()){return invalidWorkingSet();}
            root.defaultTcp->toolOid=tool->second;
        }
    }
    writes.front().payloadCanonical=restore?rootSlot.objectBytes:encodeValue(ObjectVariant{root});

    // 第三阶段由同一固定引用集与计划覆盖层重建活动闭包。Restore可重新引用
    // 已解除引用但仍留在修订对象集中的旧部件，不把保留对象误当成第二份活动部件。
    const auto candidate=readReferencedWorkingSet(ctx.query(),revision,root,rootId,writes);
    if(!candidate){return invalidWorkingSet();}
    result.candidate=*candidate;result.travelRelevant=true;
    const auto oldRefs=partRefs(baseline.design);const auto newRefs=partRefs(root);
    if(!oldRefs||!newRefs){return invalidWorkingSet();}
    for(const auto& item:*oldRefs){
        if(!newRefs->count(item.first)){
            result.removedRefs.push_back(*core::ObjectId::tryFromCanonical(item.first));
        }
    }
    out.objectWrites=std::move(writes);return result;
}

/**
 * @brief 每次prepare独立的栈适配器，复用既有final断言管线并借用固定基线。
 * 生命周期仅限外层prepare；不跨调用保留查询/快照，不引入线程共享可变态。
 */
class WorkingSetPreparation final:public IModelingCommandHandler {
public:
    /** @brief [in] services沿旧端口借用纪律；revision为本次prepare固定引用集。 */
    WorkingSetPreparation(const HandlerServices& services,const project::RevisionView& revision)
        :IModelingCommandHandler(services,true),m_revision(revision){}
    /** @brief 返回新增命令族token；纯值，不读取会话或HEAD。 */
    std::string commandType()const override{return std::string(kCmdApplyModelingWorkingSet);}
protected:
    /** @brief 共用管线调用的同步钩子；参数/失败语义沿planWorkingSet，不持有out。 */
    DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,const CommandPayload& payload,
        const ModelingWorkingSet& baseline,project::CommandPlan& out)override
    {return planWorkingSet(ctx,payload,m_revision,baseline,out);}
private:
    const project::RevisionView& m_revision; ///< 外层prepare调用方持有，同步期间借用。
};

/** @brief 注册表持有的无会话状态处理器；服务闭包不可变，仅命令执行槽调用。 */
class WorkingSetHandler final:public project::ICommandHandler {
public:
    /** @brief [in] services为装配服务副本，所借用端口目标须覆盖注册表生命周期。 */
    explicit WorkingSetHandler(HandlerServices services):m_services(std::move(services)){}
    /** @brief 返回完整工作集token，旧五token不受影响。 */
    std::string commandType()const override{return std::string(kCmdApplyModelingWorkingSet);}
    /** @brief 复用v2帧，不改变已保存inverse的解码版本。 */
    std::uint32_t currentPayloadVersion()const override{return kCommandPayloadVersion;}
    /** @brief 同步委托栈适配器，沿原prepare输入/输出/异常契约，零共享调用期状态。 */
    project::PrepareOutcome prepare(project::HandlerContext& ctx,const project::CommandEnvelope& envelope,
        const project::RevisionView& revision,project::CommandPlan& out,
        std::vector<core::DiagnosticRecord>& diags)override
    {WorkingSetPreparation adapter(m_services,revision);return adapter.prepare(ctx,envelope,revision,out,diags);}
private:
    HandlerServices m_services; ///< 装配服务值副本；借用目标由L5管理。
};
} // namespace

std::optional<ModelingWorkingSet> readReferencedWorkingSet(
    const project::IProjectQueryPort& query,const project::RevisionView& revision,
    const RobotDesign& root,const core::ObjectId& rootId,const std::vector<project::ObjectWrite>& writes)
{
    if(!partRefs(root)){return std::nullopt;}
    ModelingWorkingSet result;result.rootObjectId=rootId;result.design=root;
    const auto read=[&](const core::ObjectId& id,std::string_view token,auto& destination){
        std::vector<std::uint8_t> bytes;
        const auto planned=std::find_if(writes.begin(),writes.end(),[&](const auto& w){return w.objectId==id;});
        if(planned!=writes.end()){
            if(planned->objectTypeToken!=token){return false;}bytes=planned->payloadCanonical;
        }else{
            const auto ref=std::find_if(revision.objectRefs.begin(),revision.objectRefs.end(),
                [&](const auto& r){return r.objectId==id;});
            if(ref==revision.objectRefs.end()||ref->objectTypeToken!=token){return false;}
            bytes=query.object(id,ref->contentVersion);
        }
        const auto decoded=RobotDesignCodec{}.decode(bytes,kCurrentFormatVersion);
        if(!decoded.ok()){return false;}
        using Part=std::decay_t<decltype(destination)>;const auto* part=std::get_if<Part>(&decoded.get());
        if(!part||part->objectId!=id){return false;}destination=*part;return true;
    };
    for(const auto& id:root.toolRefs){ToolDefinition t;if(!read(id,kToolDefinitionObjectType,t)){return std::nullopt;}result.toolObjects.push_back(std::move(t));}
    for(const auto& id:root.sceneRefs){SceneObject s;if(!read(id,kSceneObjectObjectType,s)){return std::nullopt;}result.sceneObjects.push_back(std::move(s));}
    if(root.poseSetRef){PoseSet p;if(!read(*root.poseSetRef,kNamedPoseSetObjectType,p)){return std::nullopt;}result.poseSetObject=std::move(p);}
    if(root.drivetrainRef){DrivetrainDesign d;if(!read(*root.drivetrainRef,kRobotDrivetrainObjectType,d)){return std::nullopt;}result.drivetrainObject=std::move(d);}
    return result;
}

std::unique_ptr<project::ICommandHandler> makeWorkingSetCommandHandler(const HandlerServices& services)
{return std::make_unique<WorkingSetHandler>(services);}

DecodeOutcome planLegacyFixedRestore(const project::IProjectQueryPort& query,
    const project::RevisionView& revision,const CommandPayload& payload,
    const ModelingWorkingSet& baseline,project::CommandPlan& out)
{
    if(payload.mode!=CommandPayload::Mode::Restore||payload.objects.empty()
       ||!payload.removals.empty()||!baseline.rootObjectId){return invalidWorkingSet();}
    RobotDesign root=baseline.design;
    std::set<std::string> seen;
    std::vector<project::ObjectWrite> writes;
    DecodeOutcome result;
    // 第一步只校验并构造局部覆盖层。历史槽可包含目前未活动的对象，
    // 但必须已存在于该不可变修订；不能以“能替换当前部件”代替身份/类型核验。
    for(const auto& slot:payload.objects){
        if(slot.allocateNew||!slot.objectId.isValid()
           ||!seen.insert(slot.objectId.toCanonical()).second){return invalidWorkingSet();}
        const auto ref=std::find_if(revision.objectRefs.begin(),revision.objectRefs.end(),
            [&](const auto& r){return r.objectId==slot.objectId;});
        if(ref==revision.objectRefs.end()||ref->objectTypeToken!=slot.objectTypeToken){return invalidWorkingSet();}
        const auto decoded=RobotDesignCodec{}.decode(slot.objectBytes,kCurrentFormatVersion);
        if(!decoded.ok()||tokenOf(decoded.get())!=slot.objectTypeToken){return invalidWorkingSet();}
        const bool identityMatches=std::visit([&](const auto& typed){
            using T=std::decay_t<decltype(typed)>;
            if constexpr(std::is_same_v<T,RobotDesign>){
                if(slot.objectId!=*baseline.rootObjectId){return false;}
                root=typed;result.travelRelevant=true;return true;
            }else{return typed.objectId==slot.objectId;}
        },decoded.get());
        if(!identityMatches){return invalidWorkingSet();}
        project::ObjectWrite write;write.objectId=slot.objectId;write.objectTypeToken=slot.objectTypeToken;
        write.payloadCanonical=slot.objectBytes;writes.push_back(std::move(write));
        result.affectedOids.push_back(slot.objectId);
    }
    // 第二步从最终根投影活动闭包，未写的历史部件按同一修订精确cv补齐。
    // 根先/后或缺席都不影响结果；无关保留对象仅写其历史字节，不进入活动断言。
    const auto candidate=readReferencedWorkingSet(query,revision,root,*baseline.rootObjectId,writes);
    if(!candidate){return invalidWorkingSet();}
    result.candidate=*candidate;
    const auto oldRefs=partRefs(baseline.design);const auto newRefs=partRefs(root);
    if(!oldRefs||!newRefs){return invalidWorkingSet();}
    for(const auto& item:*oldRefs){if(!newRefs->count(item.first)){
        result.removedRefs.push_back(*core::ObjectId::tryFromCanonical(item.first));
    }}
    // PM-18/MDL-06：历史帧可能跨命令族携带根及部件；纯命名位姿族的默认
    // false位不能掩盖实际工具/场景/关节变化，否则项目已恢复、运行时仍用旧值。
    // 仅排除明确不进入运行时的位姿集/引用和编辑日志，其他完整值变化保守重编译。
    // 两个副本都来自同一固定修订及本次覆盖，不读取HEAD，也不改写持久原字节。
    const auto runtimeInputs=[](ModelingWorkingSet value){
        value.poseSetObject.reset();value.design.poseSetRef.reset();value.changes.clear();
        return value;
    };
    out.requiresDualCompile=runtimeInputs(baseline)!=runtimeInputs(*candidate);
    out.objectWrites=std::move(writes);return result;
}
} // namespace sdurws::ird::modeling
