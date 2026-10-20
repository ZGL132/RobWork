/**
 * @file ModelingBodyEditor.cpp
 * @brief MDL-05/F10：连杆物性编辑求值及内联界面，依据modeling §5.3/L-8。
 * 先对值副本真实求值，再交面板复核身份/基线；不新增数学公式或项目写路径。
 */
#include "ModelingBodyEditor.hpp"
#include "../src/InertiaMath.hpp" // 同单元唯一惯量判定，不跨单元借私有实现。
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>
namespace sdurws::ird::modeling {
std::optional<ManualBodyResult> evaluateManualBodyEdit(
    const BodyData& before,const ManualBodyEdit& edit,std::string& error)
{
    error.clear();BodyData candidate=before;
    const auto user=core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
    // 第一步只接受显式勾选值；相同值保留来源，避免确认无修改却伪造新事实。
    if(edit.mass){
        if(!std::isfinite(*edit.mass)||*edit.mass<=0){error="质量须为有限正数（kg）。";return std::nullopt;}
        if(!before.mass.tryValue()||before.mass.value()!=*edit.mass){
            candidate.mass=core::SourcedValue<double>::provided(*edit.mass,user);
        }
    }
    if(edit.centerOfMass){
        for(int i=0;i<3;++i){if(!std::isfinite((*edit.centerOfMass)[i])){
            error="质心三个分量须为有限数值（物体局部坐标系，m）。";return std::nullopt;
        }}
    }
    const bool changedCom=edit.centerOfMass
        && (!before.centerOfMass.tryValue()||before.centerOfMass.value()!=*edit.centerOfMass);
    std::string choice;
    // 第二步守住M-2：改变质心必须有明确处置；缺旧基准只允许完整提供。
    if(changedCom){
        if(!edit.resolution){error="质心已改变，请明确选择平行轴迁移或覆盖完整张量。";return std::nullopt;}
        const bool complete=candidate.mass.tryValue()&&before.centerOfMass.tryValue()&&before.inertia.tryValue();
        if(*edit.resolution==CentroidEditResolution::MigrateInertia){
            if(!complete){error="平行轴迁移需要已提供的质量、旧质心和旧惯量；请改用完整张量覆盖。";return std::nullopt;}
            if(edit.inertia){error="迁移与完整张量覆盖不能同时提交，请只选择一种处置。";return std::nullopt;}
        }else if(!edit.inertia){
            error="覆盖时必须填写完整惯量六分量（kg·m²）。";return std::nullopt;
        }
        if(complete){
            // 域前置已检查；本轮质量可能显式更新，迁移按该已确认质量计算。
            if(applyCentroidEdit(candidate,*edit.centerOfMass,edit.resolution,edit.inertia)){
                error="质心处置尚未确认，请重新选择。";return std::nullopt;
            }
        }else{
            // 初始无基准不能调用迁移契约；完整输入直接创建用户物性事实。
            candidate.centerOfMass=core::SourcedValue<rw::math::Vector3D<double>>::provided(*edit.centerOfMass,user);
            candidate.inertia=core::SourcedValue<InertiaTensor>::provided(*edit.inertia,user);
        }
        choice=std::string(centroidEditResolutionToken(*edit.resolution));
    }else if(edit.inertia&&(!before.inertia.tryValue()||before.inertia.value()!=*edit.inertia)){
        candidate.inertia=core::SourcedValue<InertiaTensor>::provided(*edit.inertia,user);
    }
    // 第三步复用同单元唯一惯量自检；输入和迁移结果均需有限/SPD/三角成立。
    if(const auto t=candidate.inertia.tryValue()){
        const std::array<std::array<double,3>,3> matrix{{
            {{t->ixx,t->ixy,t->ixz}},{{t->ixy,t->iyy,t->iyz}},{{t->ixz,t->iyz,t->izz}}}};
        const auto failure=inertiamath::synthesisSelfCheck(matrix);
        if(!failure.empty()){
            error="惯量未通过：六分量须有限、张量须正定且满足主惯量三角不等式（kg·m²）。";
            return std::nullopt;
        }
    }
    return ManualBodyResult{std::move(candidate),"手工物性编辑"+(choice.empty()?std::string():"；质心处置="+choice)};
}

ModelingBodyEditor::ModelingBodyEditor(QWidget* parent,Accept accept)
    :QWidget(parent),m_accept(std::move(accept))
{
    setObjectName("ird_modeling_body_editor");
    auto* layout=new QVBoxLayout(this);m_title=new QLabel(this);layout->addWidget(m_title);
    auto* note=new QLabel(QStringLiteral("勾选要修改的项目；未勾选保留原值与来源。"
        "质心为物体局部坐标系m；惯量在质心、姿态与物体局部坐标系一致，kg·m²。"
        "改变质心必须明确选择迁移或完整覆盖；确认只写草稿。"),this);
    note->setWordWrap(true);layout->addWidget(note);
    m_mass=new QCheckBox(QStringLiteral("修改质量 (kg)"),this);m_mass->setObjectName("ird_body_mass_edit");
    m_com=new QCheckBox(QStringLiteral("修改质心 (m)"),this);m_com->setObjectName("ird_body_com_edit");
    m_tensor=new QCheckBox(QStringLiteral("覆盖完整惯量 (kg·m²)"),this);m_tensor->setObjectName("ird_body_inertia_edit");
    for(int i=0;i<10;++i){
        m_values[i]=new QLineEdit(this);m_values[i]->setObjectName(QStringLiteral("ird_body_value_%1").arg(i));
        m_values[i]->setPlaceholderText(QStringLiteral("未提供"));m_values[i]->setMinimumWidth(65);
    }
    layout->addWidget(m_mass);layout->addWidget(m_values[0]);layout->addWidget(m_com);
    auto* position=new QGridLayout;
    for(int i=0;i<3;++i){position->addWidget(new QLabel(QString::fromLatin1("xyz"+i,1)+" (m)",this),0,i);position->addWidget(m_values[i+1],1,i);}
    layout->addLayout(position);
    m_resolution=new QComboBox(this);m_resolution->setObjectName("ird_body_centroid_resolution");
    m_resolution->addItems({QStringLiteral("请选择质心处置（无默认）"),QStringLiteral("按平行轴迁移惯量"),QStringLiteral("覆盖完整惯量张量")});
    layout->addWidget(m_resolution);layout->addWidget(m_tensor);
    auto* tensor=new QGridLayout;const char* names[]={"Ixx","Iyy","Izz","Ixy","Ixz","Iyz"};
    for(int i=0;i<6;++i){
        tensor->addWidget(new QLabel(QString::fromLatin1(names[i])+" (kg·m²)",this),(i/3)*2,i%3);
        tensor->addWidget(m_values[i+4],(i/3)*2+1,i%3);
    }
    layout->addLayout(tensor);
    m_feedback=new QLabel(QStringLiteral("选中一个连杆以编辑物性。"),this);
    m_feedback->setObjectName("ird_body_feedback");m_feedback->setWordWrap(true);layout->addWidget(m_feedback);
    auto* buttons=new QHBoxLayout;
    auto* apply=new QPushButton(QStringLiteral("确认写入草稿"),this);apply->setObjectName("ird_body_apply");
    auto* cancel=new QPushButton(QStringLiteral("取消暂存"),this);cancel->setObjectName("ird_body_cancel");
    buttons->addWidget(apply);buttons->addWidget(cancel);buttons->addStretch();layout->addLayout(buttons);layout->addStretch();
    for(auto* check:{m_mass,m_com,m_tensor}){QObject::connect(check,&QCheckBox::toggled,this,[this](bool){updateInputs();});}
    QObject::connect(m_resolution,&QComboBox::currentIndexChanged,this,[this](int index){
        if(index==1){m_tensor->setChecked(false);}else if(index==2){m_tensor->setChecked(true);}updateInputs();
    });
    QObject::connect(apply,&QPushButton::clicked,this,[this]{submit();});
    QObject::connect(cancel,&QPushButton::clicked,this,[this]{cancelPending();m_feedback->setText(QStringLiteral("已取消暂存，草稿未改变。"));});
    cancelPending();setEnabled(false);
}

void ModelingBodyEditor::bindTarget(std::optional<core::ObjectId> id,const BodyData& body,
    const std::string& name,bool writable,const std::string& objectKind)
{
    const bool changed=m_id!=id||m_before!=body;
    m_id=id;m_before=body;m_writable=writable;setEnabled(id.has_value()&&writable);
    m_title->setText(QString::fromStdString(objectKind)+QStringLiteral("物性：")+QString::fromStdString(name)
        +(writable?QString():QStringLiteral("（只读）")));
    if(changed){cancelPending();}
}

void ModelingBodyEditor::cancelPending()
{
    m_mass->setChecked(false);m_com->setChecked(false);m_tensor->setChecked(false);m_resolution->setCurrentIndex(0);
    const auto show=[this](int i,std::optional<double> value){
        // 17位保留双精度往返；回填后从首位显示，窄窗不能只露尾数而遮住符号/小数点。
        const auto text=value?QString::number(*value,'g',17):QString();
        m_values[i]->setText(text);m_values[i]->setCursorPosition(0);m_values[i]->setToolTip(text);
    };
    show(0,m_before.mass.tryValue());
    for(int i=0;i<3;++i){show(i+1,m_before.centerOfMass.tryValue()?std::optional<double>{m_before.centerOfMass.value()[i]}:std::nullopt);}
    if(const auto t=m_before.inertia.tryValue()){
        const double values[]={t->ixx,t->iyy,t->izz,t->ixy,t->ixz,t->iyz};for(int i=0;i<6;++i){show(i+4,values[i]);}
    }else{for(int i=4;i<10;++i){show(i,std::nullopt);}}
    m_feedback->setText(QStringLiteral("勾选后输入，确认前不修改草稿；取消恢复本页基线。"));updateInputs();
}

void ModelingBodyEditor::updateInputs()
{
    m_values[0]->setEnabled(m_mass->isChecked());
    for(int i=1;i<4;++i){m_values[i]->setEnabled(m_com->isChecked());}
    m_resolution->setEnabled(m_com->isChecked());
    const bool migrating=m_com->isChecked()&&m_resolution->currentIndex()==1;
    m_tensor->setEnabled(!migrating);
    for(int i=4;i<10;++i){m_values[i]->setEnabled(m_tensor->isChecked()&&!migrating);}
}

void ModelingBodyEditor::submit()
{
    if(!m_id||!m_writable){return;} // 出口防只读；父控件禁用并非唯一防线。
    ManualBodyEdit edit;std::array<double,10> values{};
    const bool migrating=m_com->isChecked()&&m_resolution->currentIndex()==1;
    for(int i=0;i<10;++i){
        const bool use=i==0?m_mass->isChecked():i<4?m_com->isChecked():m_tensor->isChecked()&&!migrating;
        if(!use){continue;}
        bool ok=false;values[i]=QLocale::c().toDouble(m_values[i]->text().trimmed(),&ok);
        if(!ok||!std::isfinite(values[i])){
            m_feedback->setText(QStringLiteral("请输入完整有限数值，单位见字段标签；原输入已保留。"));m_values[i]->setFocus();return;
        }
    }
    if(m_mass->isChecked()){edit.mass=values[0];}
    if(m_com->isChecked()){
        edit.centerOfMass=rw::math::Vector3D<double>{values[1],values[2],values[3]};
        if(m_resolution->currentIndex()==1){edit.resolution=CentroidEditResolution::MigrateInertia;}
        else if(m_resolution->currentIndex()==2){edit.resolution=CentroidEditResolution::OverwriteInertia;}
    }
    if(m_tensor->isChecked()&&!migrating){edit.inertia=InertiaTensor{values[4],values[5],values[6],values[7],values[8],values[9]};}
    std::string error;const auto result=evaluateManualBodyEdit(m_before,edit,error);
    if(!result){m_feedback->setText(QString::fromStdString(error));return;}
    if(result->body==m_before){m_feedback->setText(QStringLiteral("没有物性数值变化，草稿未写入。"));return;}
    // 接受回调可同步刷新本页，因此不保留对成员基线/目标的引用。
    const auto id=*m_id;const auto before=m_before;
    if(!m_accept||!m_accept(id,before,*result,error)){
        m_feedback->setText(error.empty()?QStringLiteral("编辑出口未装配，草稿未改变。"):QString::fromStdString(error));return;
    }
    // 同步后置动作可能切换选择；只更新本轮目标，不能污染新页基线。
    if(m_id!=id){return;}
    m_before=result->body;cancelPending();
    // 摘要保留域契约要求的稳定处置token；窗口只呈现其中文语义。
    const auto human=QString::fromStdString(result->summary)
        .replace(QString::fromUtf8(centroidEditResolutionToken(CentroidEditResolution::MigrateInertia).data()),QStringLiteral("按平行轴迁移惯量"))
        .replace(QString::fromUtf8(centroidEditResolutionToken(CentroidEditResolution::OverwriteInertia).data()),QStringLiteral("覆盖完整惯量张量"));
    m_feedback->setText(QStringLiteral("已写入草稿：")+human);
}
}
