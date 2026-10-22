/**
 * @file JointFormSession.cpp
 * @brief GF-A整表意图组装，数值解析/单位归ui/core，物理规则和RPY正解归modeling领域。
 * 依据MDL-01/09/12、UX-05；纯暂存不产生修订、历史、资源或正式三维变化。
 */
#include "JointFormSession.hpp"
#include "RpyPresentation.hpp"
#include <algorithm>
#include <cmath>
namespace sdurws::ird::modeling {
namespace {
/** @brief 把已提供关节值投影成SI数值；缺供保留空，原点相对父连杆系。 */
std::map<std::string,std::optional<double>> valuesOf(const JointEntry& j)
{
    std::map<std::string,std::optional<double>> v;
    const auto axis=j.axis.tryValue(); const auto pose=j.origin.tryValue();
    const char* xyz[]={"x","y","z"};
    for(int i=0;i<3;++i){v[std::string("axis-")+xyz[i]]=axis?std::optional<double>{(*axis)[i]}:std::nullopt;
        v[std::string("origin-")+xyz[i]]=pose?std::optional<double>{pose->d()[i]}:std::nullopt;}
    const auto rpy=pose?rpyview::rotationToRpy(pose->r()):std::array<double,3>{};
    const char* angles[]={"r","p","yaw"};
    for(int i=0;i<3;++i){v[std::string("origin-")+angles[i]]=pose?std::optional<double>{rpy[i]}:std::nullopt;}
    v["zero-offset"]=j.zeroOffset;v["max-velocity"]=j.maxVelocity.tryValue();v["max-acceleration"]=j.maxAcceleration.tryValue();
    const auto range=j.type==JointType::Continuous?j.workingRange.tryValue():j.bounds.tryValue();
    const std::string prefix=j.type==JointType::Continuous?"working-range-":"bounds-";
    v[prefix+"min"]=range?std::optional<double>{range->first}:std::nullopt;
    v[prefix+"max"]=range?std::optional<double>{range->second}:std::nullopt;
    return v;
}
}
JointFormSession::JointFormSession(const JointEntry& baseline,AuthorityMode authority)
    :m_baseline(baseline),m_authority(authority),m_type(baseline.type),m_name(baseline.localName)
{rebuild(valuesOf(baseline));}
void JointFormSession::rebuild(const std::map<std::string,std::optional<double>>& values)
{
    std::vector<ui::QuantityFieldSpec> specs;
    const auto add=[&](const std::string& key,const std::string& label,core::QuantityKind kind,const char* unit){
        const auto token=core::UnitToken::find(unit).value();specs.push_back(ui::makeQuantityFieldSpec(key,label,kind,token,token));};
    using K=core::QuantityKind;
    for(const char* axis:{"x","y","z"}){add(std::string("axis-")+axis,std::string("轴向 ")+axis,K::Dimensionless,"1");}
    for(const char* axis:{"x","y","z"}){add(std::string("origin-")+axis,std::string("原点 ")+axis,K::Length,"m");}
    for(const char* axis:{"r","p","yaw"}){add(std::string("origin-")+axis,std::string("姿态 ")+axis,K::Angle,"rad");}
    const bool linear=m_type==JointType::Prismatic||(m_type==JointType::Fixed&&m_baseline.type==JointType::Prismatic);
    add("zero-offset","零位偏置",linear?K::Length:K::Angle,linear?"m":"rad");
    const std::string prefix=m_type==JointType::Continuous?"working-range-":"bounds-";
    add(prefix+"min",m_type==JointType::Continuous?"工作范围下限":"限位下限",linear?K::Length:K::Angle,linear?"m":"rad");
    add(prefix+"max",m_type==JointType::Continuous?"工作范围上限":"限位上限",linear?K::Length:K::Angle,linear?"m":"rad");
    add("max-velocity","最大速度",linear?K::LinearVelocity:K::AngularVelocity,linear?"m/s":"rad/s");
    add("max-acceleration","最大加速度",linear?K::LinearAcceleration:K::AngularAcceleration,linear?"m/s^2":"rad/s^2");
    m_model=std::make_shared<ui::ParamEditModel>(std::move(specs));
    for(const auto& spec:m_model->fields()){const auto it=values.find(spec.key);m_model->setBaseline(spec.key,it==values.end()?std::nullopt:it->second);}
}
bool JointFormSession::stageType(JointType type)
{
    if(m_model->hasErrors()){return false;} // 非法文本先纠正，不能借重建模型吞掉。
    std::map<std::string,std::optional<double>> values;
    for(const auto& spec:m_model->fields()){values[spec.key]=m_model->currentValueSi(spec.key);}
    const bool dimensionChanged=type!=JointType::Fixed&&((type==JointType::Prismatic)!=(m_type==JointType::Prismatic)||m_type==JointType::Fixed);
    if(dimensionChanged){for(const char* key:{"zero-offset","bounds-min","bounds-max","working-range-min","working-range-max","max-velocity","max-acceleration"}){values[key]=std::nullopt;}}
    if(type==m_baseline.type){
        const auto original=valuesOf(m_baseline);
        for(const char* key:{"zero-offset","bounds-min","bounds-max","working-range-min","working-range-max","max-velocity","max-acceleration"}){
            const auto it=original.find(key);if(it!=original.end()){values[key]=it->second;}}
    }
    m_type=type;rebuild(values);return true;
}
void JointFormSession::usePreviousInterval()
{
    const auto range=m_baseline.type==JointType::Continuous?m_baseline.workingRange.tryValue():m_baseline.bounds.tryValue();
    if(!range || m_type==JointType::Prismatic || m_baseline.type==JointType::Prismatic){return;}
    const std::string p=m_type==JointType::Continuous?"working-range-":"bounds-";
    m_model->setBaseline(p+"min",range->first);m_model->setBaseline(p+"max",range->second);
}
std::optional<JointEntry> JointFormSession::candidate(std::string& field,std::string& error) const
{
    field.clear();error.clear();
    if(m_model->hasErrors()){const auto first=m_model->errors().front();field=first.first;error=first.second;return std::nullopt;}
    auto result=m_baseline;result.type=m_type;result.localName=m_name;
    const auto require=[&](const char* key)->std::optional<double>{const auto v=m_model->currentValueSi(key);if(!v&&error.empty()){field=key;error="请补全该组输入；未提供不能用0替代";}return v;};
    const auto original=valuesOf(m_baseline);
    const auto changed=[&](const std::string& key){const auto it=original.find(key);return m_model->currentValueSi(key)!=(it==original.end()?std::optional<double>{}:it->second);};
    const auto provenance=core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
    if(changed("axis-x")||changed("axis-y")||changed("axis-z")){
        const auto x=require("axis-x"),y=require("axis-y"),z=require("axis-z");
        if(x&&y&&z){result.axis=core::SourcedValue<rw::math::Vector3D<double>>::provided({*x,*y,*z},provenance);}}
    bool originChanged=false;for(const char* key:{"origin-x","origin-y","origin-z","origin-r","origin-p","origin-yaw"}){originChanged=originChanged||changed(key);}
    if(originChanged){
        const auto x=require("origin-x"),y=require("origin-y"),z=require("origin-z"),r=require("origin-r"),p=require("origin-p"),w=require("origin-yaw");
        if(x&&y&&z&&r&&p&&w){
            // RPY正解调用既有领域入口，临时工作集仅用于组装，绝不产生真实编辑回调。
            ModelingWorkingSet temp;temp.design.authority=m_authority;temp.design.joints.push_back(result);
            if(const auto e=applyJointFieldEdit(temp,0,JointEditField::Origin,JointOriginEditValue{*x,*y,*z,*r,*p,*w})){field="origin-x";error=std::string(jointEditErrorCodeToken(e->code))+"："+e->detail;}
            else{
                const auto& pose=temp.design.joints[0].origin.value();bool equivalent=false;
                if(const auto prior=m_baseline.origin.tryValue()){
                    // Euler相差整周/奇异别名可表示同一旋转；1e-12只吸收三角函数
                    // 双精度舍入，平移仍精确比较，不让纯表示变换产生空草稿历史。
                    equivalent=pose.d()==prior->d();
                    for(int row=0;row<3;++row){for(int col=0;col<3;++col){equivalent=equivalent&&std::abs(pose.r()(row,col)-prior->r()(row,col))<=1e-12;}}
                }
                if(!equivalent){result.origin=temp.design.joints[0].origin;}
            }
        }
    }
    if(m_type!=JointType::Fixed){if(const auto zero=require("zero-offset")){result.zeroOffset=*zero;}}
    const bool typeChanged=m_type!=m_baseline.type;
    const std::string prefix=m_type==JointType::Continuous?"working-range-":"bounds-";
    const auto lower=m_model->currentValueSi(prefix+"min"),upper=m_model->currentValueSi(prefix+"max");
    if(m_type!=JointType::Fixed && (typeChanged||changed(prefix+"min")||changed(prefix+"max"))){
        if(m_type==JointType::Continuous&&!lower&&!upper){result.workingRange=core::SourcedValue<JointLimits>::notProvided();}
        else if(!lower||!upper){field=prefix+"min";error="请补全区间两端并显式确认类型切换影响";}
        else if(m_type==JointType::Continuous){result.workingRange=core::SourcedValue<JointLimits>::provided({*lower,*upper},provenance);}
        else{result.bounds=core::SourcedValue<JointLimits>::provided({*lower,*upper},provenance);}
    }
    if(typeChanged){
        if(m_type==JointType::Continuous){result.bounds=core::SourcedValue<JointLimits>::notApplicable();}
        else if(m_baseline.type==JointType::Continuous){result.workingRange=core::SourcedValue<JointLimits>::notApplicable();}
    }
    const auto numeric=[&](const char* key,auto& value){const auto v=m_model->currentValueSi(key);
        if(m_type==JointType::Fixed){if(typeChanged){value=core::SourcedValue<double>::notApplicable();}}
        else if(v){if(typeChanged||changed(key)){value=core::SourcedValue<double>::provided(*v,provenance);}}
        else if(typeChanged){value=core::SourcedValue<double>::notProvided();}
        else if(changed(key)){field=key;error="已有值不能通过清空文本删除，请输入数值或重置";}};
    numeric("max-velocity",result.maxVelocity);numeric("max-acceleration",result.maxAcceleration);
    if(!error.empty()){return std::nullopt;}return result;
}
bool JointFormSession::dirty() const
{
    if(m_model->hasErrors()||m_name!=m_baseline.localName||m_type!=m_baseline.type){return true;}
    std::string field,error;const auto value=candidate(field,error);return value?*value!=m_baseline:!m_model->pendingChanges().empty();
}
void JointFormSession::reset(){m_type=m_baseline.type;m_name=m_baseline.localName;rebuild(valuesOf(m_baseline));}
}
