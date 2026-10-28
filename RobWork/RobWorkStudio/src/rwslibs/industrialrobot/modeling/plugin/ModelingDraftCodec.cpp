/**
 * @file ModelingDraftCodec.cpp
 * @brief PM-04/08完整草稿封装：唯一对象Codec字节+根身份+变更摘要，依据B4b。
 * Qt只在插件中封装JSON；不在这里实现物性/位姿/身份算法或工程存储。
 */
#include "ModelingDraftCodec.hpp"
#include <sdurws/ird/modeling/Codec.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <type_traits>
namespace sdurws::ird::modeling {
namespace {
/** @brief [in] text为域错误说明；只用于解码内部短路，统一转为返回值。 */
[[noreturn]] void rejectDraft(const std::string& text){throw std::runtime_error(text);}
/** @brief [in] text须为字符串；拒绝JSON类型漂移，不把null/数字猜为空串。 */
std::string textValue(const QJsonValue& text)
{
    if(!text.isString()){rejectDraft("草稿字段不是字符串。");}
    return text.toString().toUtf8().toStdString();
}
/** @brief [in] value按原Codec编码并以小写hex封装，物理量不经JSON浮点。 */
QString objectHex(const ObjectVariant& value)
{
    const auto encoded=RobotDesignCodec{}.encode(value,kCurrentFormatVersion);
    if(!encoded.ok()){rejectDraft("草稿对象编码失败："+encoded.error().detail);}
    const auto& bytes=encoded.get();
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()),
        static_cast<qsizetype>(bytes.size())).toHex());
}
/** @brief [in] hex严格偶数字符hex，不接受QByteArray::fromHex静默跳过非法字符。 */
ObjectVariant decodeObject(const std::string& hex)
{
    if(hex.empty()||hex.size()%2!=0){rejectDraft("草稿对象hex为空或长度不完整。");}
    for(const char c:hex){
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))){rejectDraft("草稿对象hex含非法字符。");}
    }
    const auto raw=QByteArray::fromHex(QByteArray(hex.data(),static_cast<qsizetype>(hex.size())));
    const Bytes bytes(raw.begin(),raw.end());
    const auto decoded=RobotDesignCodec{}.decode(bytes,kCurrentFormatVersion);
    if(!decoded.ok()){rejectDraft("草稿对象解码失败："+decoded.error().detail);}
    return decoded.get();
}
/** @brief [in] text为原Codec的完整对象字节，类型须与指定草稿槽完全一致。 */
template<class T> T typedObject(const QJsonValue& text)
{
    const auto object=decodeObject(textValue(text));
    const auto* typed=std::get_if<T>(&object);
    if(!typed){rejectDraft("草稿对象类型与槽位不一致。");}
    return *typed;
}
/** @brief [in] items为必填数组，按原工作集顺序恢复，身份随后统一交叉核对。 */
template<class T> std::vector<T> typedArray(const QJsonValue& items)
{
    if(!items.isArray()){rejectDraft("草稿部件列表缺失或不是数组。");}
    std::vector<T> out;
    for(const auto& value:items.toArray()){out.push_back(typedObject<T>(value));}
    return out;
}
/** @brief [in] value为可选单例槽，只有显式null代表不存在；缺字段不能静默缺省。 */
template<class T> std::optional<T> optionalObject(const QJsonValue& value)
{
    if(value.isNull()){return std::nullopt;}
    return typedObject<T>(value);
}
/**
 * @brief [in] ws完整候选的引用与嵌入身份检查；不代替就绪/数值/项目引用保护。
 * 对象集必须恰好覆盖根引用；防止好根配旧部件、重复身份或未引用残留被静默恢复。
 */
void checkDraftClosure(const ModelingWorkingSet& ws)
{
    std::set<std::string> ids;
    const auto add=[&](const core::ObjectId& id){
        if(!id.isValid()||!ids.insert(id.toCanonical()).second){rejectDraft("草稿对象身份无效或重复。");}
    };
    if(ws.rootObjectId){add(*ws.rootObjectId);}
    for(const auto& j:ws.design.joints){add(j.objectId);}
    for(const auto& l:ws.design.links){add(l.objectId);}
    const auto checkList=[&](const auto& refs,const auto& values){
        if(refs.size()!=values.size()){rejectDraft("草稿根引用与部件数量不一致。");}
        std::set<std::string> refIds;
        for(const auto& id:refs){if(!id.isValid()||!refIds.insert(id.toCanonical()).second){rejectDraft("草稿根引用身份无效或重复。");}}
        for(const auto& value:values){add(value.objectId);if(refIds.erase(value.objectId.toCanonical())!=1){rejectDraft("草稿部件未被根引用或身份错位。");}}
    };
    checkList(ws.design.toolRefs,ws.toolObjects);checkList(ws.design.sceneRefs,ws.sceneObjects);
    const auto checkSingle=[&](const auto& ref,const auto& value){
        if(ref.has_value()!=value.has_value()){rejectDraft("草稿单例引用与部件不一致。");}
        if(value){add(value->objectId);if(*ref!=value->objectId){rejectDraft("草稿单例对象身份错位。");}}
    };
    checkSingle(ws.design.poseSetRef,ws.poseSetObject);checkSingle(ws.design.drivetrainRef,ws.drivetrainObject);
    if(ws.design.defaultTcp){
        const auto& ref=*ws.design.defaultTcp;
        const auto tool=std::find_if(ws.toolObjects.begin(),ws.toolObjects.end(),[&](const ToolDefinition& t){return t.objectId==ref.toolOid;});
        if(tool==ws.toolObjects.end()||std::none_of(tool->tcpList.begin(),tool->tcpList.end(),
            [&](const TcpEntry& tcp){return tcp.key==ref.tcpKey;})){rejectDraft("草稿默认TCP引用不存在。");}
    }
}
/** @brief [in] design旧根；[in] baseline只提供确切ID部件，缺失必须拒绝而非补默认对象。 */
ModelingWorkingSet legacyWorkingSet(const RobotDesign& design,const ModelingWorkingSet& baseline)
{
    auto out=baseline;out.design=design;out.toolObjects.clear();out.sceneObjects.clear();
    const auto copy=[&](const auto& refs,const auto& source,auto& target){
        for(const auto& id:refs){
            const auto found=std::find_if(source.begin(),source.end(),[&](const auto& v){return v.objectId==id;});
            if(found==source.end()){rejectDraft("旧根草稿缺少引用部件，不能恢复已丢内容。");}
            target.push_back(*found);
        }
    };
    copy(design.toolRefs,baseline.toolObjects,out.toolObjects);copy(design.sceneRefs,baseline.sceneObjects,out.sceneObjects);
    if(!design.poseSetRef){out.poseSetObject.reset();}
    if(!design.drivetrainRef){out.drivetrainObject.reset();}
    // 旧格式没有摘要；只有真实内容改变才补未应用事实，不能把同值基线伪造成修改。
    if(objectHex(ObjectVariant{out.design})!=objectHex(ObjectVariant{baseline.design})){
        out.changes.push_back({"draft.restore","恢复旧格式未应用草稿"});
    }
    return out;
}
}

std::string encodeModelingDraft(const ModelingWorkingSet& ws)
{
    // 对象集合的顺序属于工作集值；不为了封装重排或修改引用。对象内部排序归Codec。
    const auto array=[](const auto& values){QJsonArray out;for(const auto& value:values){out.append(objectHex(ObjectVariant{value}));}return out;};
    QJsonArray changes;
    for(const auto& change:ws.changes){changes.append(QJsonObject{{"subject",QString::fromStdString(change.subject)},{"summary",QString::fromStdString(change.summary)}});}
    QJsonArray allocated;for(const auto& id:ws.allocatedObjectIds){allocated.append(QString::fromStdString(id.toCanonical()));}
    const QJsonObject document{{"format","ird-modeling-draft"},{"version",1},
        {"rootId",ws.rootObjectId?QJsonValue(QString::fromStdString(ws.rootObjectId->toCanonical())):QJsonValue(QJsonValue::Null)},
        {"root",objectHex(ObjectVariant{ws.design})},{"tools",array(ws.toolObjects)},{"scenes",array(ws.sceneObjects)},
        {"poses",ws.poseSetObject?QJsonValue(objectHex(ObjectVariant{*ws.poseSetObject})):QJsonValue(QJsonValue::Null)},
        {"drivetrain",ws.drivetrainObject?QJsonValue(objectHex(ObjectVariant{*ws.drivetrainObject})):QJsonValue(QJsonValue::Null)},
        {"changes",changes},{"allocatedObjectIds",allocated}};
    return QJsonDocument(document).toJson(QJsonDocument::Compact).toStdString();
}

std::optional<ModelingWorkingSet> decodeModelingDraft(const std::string& payload,
    const ModelingWorkingSet& baseline,std::string& error)
{
    error.clear();
    // 64MiB为本私有域帧的解析上限（含hex膨胀），先于Qt分配JSON树检查。
    constexpr std::size_t maxPayload=64U*1024U*1024U;
    if(payload.empty()||payload.size()>maxPayload){error="草稿域载荷为空或超出64MiB解析预算。";return std::nullopt;}
    try{
        ModelingWorkingSet out;
        const auto first=payload.find_first_not_of(" \t\r\n");
        if(first==std::string::npos){rejectDraft("草稿域载荷为空白。");}
        if(payload[first]!='{'){
            const auto value=decodeObject(payload);const auto* root=std::get_if<RobotDesign>(&value);
            if(!root){rejectDraft("旧草稿载荷不是机器人根对象。");}
            out=legacyWorkingSet(*root,baseline);
        }else{
            QJsonParseError parse;
            const auto json=QJsonDocument::fromJson(QByteArray(payload.data(),static_cast<qsizetype>(payload.size())),&parse);
            if(parse.error!=QJsonParseError::NoError||!json.isObject()){rejectDraft("草稿JSON损坏或含尾随数据。");}
            const auto doc=json.object();
            if(doc.value("format")!="ird-modeling-draft"||!doc.value("version").isDouble()
                ||doc.value("version").toDouble()!=1){rejectDraft("不支持的建模草稿域格式或版本。");}
            const auto rootId=doc.value("rootId");
            if(!rootId.isNull()){
                out.rootObjectId=core::ObjectId::tryFromCanonical(textValue(rootId));
                if(!out.rootObjectId){rejectDraft("草稿根身份无效。");}
            }
            out.design=typedObject<RobotDesign>(doc.value("root"));
            out.toolObjects=typedArray<ToolDefinition>(doc.value("tools"));out.sceneObjects=typedArray<SceneObject>(doc.value("scenes"));
            out.poseSetObject=optionalObject<PoseSet>(doc.value("poses"));out.drivetrainObject=optionalObject<DrivetrainDesign>(doc.value("drivetrain"));
            if(doc.contains("allocatedObjectIds")){
                if(!doc.value("allocatedObjectIds").isArray()){rejectDraft("草稿身份分配历史不是数组。");}
                std::set<std::string> seen;
                for(const auto& value:doc.value("allocatedObjectIds").toArray()){
                    const auto id=core::ObjectId::tryFromCanonical(textValue(value));
                    if(!id||!seen.insert(id->toCanonical()).second){rejectDraft("草稿身份分配历史无效或重复。");}
                    out.allocatedObjectIds.push_back(*id);
                }
            }
            if(!doc.value("changes").isArray()){rejectDraft("草稿未应用摘要缺失。");}
            for(const auto& entry:doc.value("changes").toArray()){
                if(!entry.isObject()){rejectDraft("草稿变更摘要不是对象。");}
                const auto change=entry.toObject();out.changes.push_back({textValue(change.value("subject")),textValue(change.value("summary"))});
            }
        }
        checkDraftClosure(out);
        return out;
    }catch(const std::runtime_error& failure){error=failure.what();return std::nullopt;}
}
}
