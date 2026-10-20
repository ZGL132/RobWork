/**
 * @file ModelingSceneEditor.cpp
 * @brief MDL-15/AT-28：场景事务与内联管理实现；依据WP-13-T24 B3f。
 * 资源安全读取唯一归io，世界变换唯一沿既有PartPoseEdit；本文件不写项目修订。
 */
#include "ModelingSceneEditor.hpp"
#include "GeometryResourceFlow.hpp"
#include "src/LocalNameValidation.hpp" // 同单元私有规则：与规范桥同一输入质量门。
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>
#include <cctype>
namespace sdurws::ird::modeling {
std::optional<ModelingWorkingSet> evaluateSceneEdit(const ModelingWorkingSet& before,
    const SceneEditRequest& request,const ResourceProbeFn& probe,std::string& error)
{
    error.clear();
    const auto reject=[&error](const std::string& why)->std::optional<ModelingWorkingSet>{
        error=why;return std::nullopt;
    };
    // 先核对身份和引用一致性，再产生候选；拒绝路径始终不动调用方工作集。
    if(!request.target.isValid()){return reject("场景对象身份无效，请重新选择目标。");}
    const auto found=std::find_if(before.sceneObjects.begin(),before.sceneObjects.end(),
        [&](const SceneObject& object){return object.objectId==request.target;});
    const auto refCount=std::count(before.design.sceneRefs.begin(),before.design.sceneRefs.end(),request.target);
    const bool creating=request.action==SceneEditAction::Create;
    if(creating){
        bool occupied=before.rootObjectId==request.target||found!=before.sceneObjects.end()||refCount!=0;
        for(const auto& link:before.design.links){occupied=occupied||link.objectId==request.target;}
        for(const auto& joint:before.design.joints){occupied=occupied||joint.objectId==request.target;}
        for(const auto& tool:before.toolObjects){occupied=occupied||tool.objectId==request.target;}
        occupied=occupied||(before.poseSetObject&&before.poseSetObject->objectId==request.target)
            ||(before.drivetrainObject&&before.drivetrainObject->objectId==request.target);
        if(occupied){return reject("新场景对象身份已被占用，请重新创建。");}
    }else if(found==before.sceneObjects.end()||refCount!=1
        ||std::count_if(before.sceneObjects.begin(),before.sceneObjects.end(),
            [&](const SceneObject& object){return object.objectId==request.target;})!=1){
        return reject("场景对象或根引用已改变，请刷新后重新选择。");
    }
    // 更新和删除共用角色门；先删除后检查会允许环境页删掉导入的机器人/工具角色。
    if(!creating&&found->role!=SceneObjectRole::EnvironmentObject&&found->role!=SceneObjectRole::Workpiece){
        return reject("当前对象属于机器人、工具或负载角色，不能在场景页改写或删除。");
    }
    auto candidate=before;
    if(request.action==SceneEditAction::Remove){
        if(!request.confirmRemove){return reject("请先明确确认删除；共享资源和项目历史将保留。");}
        // 只解除当前草稿引用，绝不删除资源清单或历史字节。跨域引用归项目提交裁决。
        auto& objects=candidate.sceneObjects;
        objects.erase(std::remove_if(objects.begin(),objects.end(),
            [&](const SceneObject& object){return object.objectId==request.target;}),objects.end());
        auto& refs=candidate.design.sceneRefs;
        refs.erase(std::remove(refs.begin(),refs.end(),request.target),refs.end());
        candidate.changes.push_back({"sceneRefs","删除场景对象 "+found->localName+"；共享资源与历史保留"});
        return candidate;
    }
    // 这里仅验证输入名称质量和模型内唯一性；runtime名称映射仍保持唯一权威。
    if(!inputquality::isLegalLocalName(request.localName)){
        return reject("场景名称须非空，仅含英文字母、数字、下划线、点或短横线。");
    }
    bool nameTaken=false;
    for(const auto& link:before.design.links){nameTaken=nameTaken||link.localName==request.localName;}
    for(const auto& joint:before.design.joints){nameTaken=nameTaken||joint.localName==request.localName;}
    for(const auto& tool:before.toolObjects){nameTaken=nameTaken||tool.localName==request.localName;}
    for(const auto& object:before.sceneObjects){
        nameTaken=nameTaken||(object.objectId!=request.target&&object.localName==request.localName);
    }
    if(nameTaken){return reject("场景名称与已有对象重复，请使用其他名称。");}
    if(request.role!=SceneObjectRole::EnvironmentObject&&request.role!=SceneObjectRole::Workpiece){
        return reject("本页只管理环境物体或工件；机器人连杆、工具、负载请使用各自编辑入口。");
    }
    SceneObject object=creating?SceneObject{}:*found;
    object.objectId=request.target;object.localName=request.localName;object.role=request.role;
    const auto withGeometry=evaluateManagedGeometry(candidate,object.geometry,request.source,
        request.resourceOrPath,probe,error);
    if(!withGeometry){return std::nullopt;}
    candidate=*withGeometry;
    if(creating){candidate.sceneObjects.push_back(object);candidate.design.sceneRefs.push_back(request.target);}
    else {candidate.sceneObjects[static_cast<std::size_t>(found-before.sceneObjects.begin())]=object;}
    if(candidate==before){return candidate;} // 恒等确认不制造编辑摘要和局部历史。
    candidate.changes.push_back({"sceneObjects",(creating?"新增场景对象 ":"更新场景对象 ")+object.localName});
    return candidate;
}

ModelingSceneEditor::ModelingSceneEditor(QWidget* parent,Accept accept,Choose choose)
    :QWidget(parent),m_accept(std::move(accept)),m_choose(std::move(choose))
{
    setObjectName("ird_modeling_scene_editor");
    auto* layout=new QVBoxLayout(this);
    auto* hint=new QLabel(QStringLiteral("场景固定帧／障碍物（世界坐标系固连）。确认只改草稿；顶栏应用草稿才产生项目修订。"
        "使用『编辑世界位姿』编辑位置m／姿态rad；不随基座安装旋转。"),this);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto* form=new QFormLayout();layout->addLayout(form);
    m_objects=new QComboBox(this);m_objects->setObjectName("ird_scene_objects");form->addRow("场景对象",m_objects);
    m_name=new QLineEdit(this);m_name->setObjectName("ird_scene_name");form->addRow("局部名称",m_name);
    m_role=new QComboBox(this);m_role->setObjectName("ird_scene_role");
    m_role->addItem("环境物体",static_cast<int>(SceneObjectRole::EnvironmentObject));
    m_role->addItem("工件",static_cast<int>(SceneObjectRole::Workpiece));form->addRow("角色",m_role);
    m_source=new QComboBox(this);m_source->setObjectName("ird_scene_source");
    m_source->addItems({"无几何（固定帧）","已有几何资源","外部网格文件"});form->addRow("几何来源",m_source);
    m_resource=new QComboBox(this);m_resource->setObjectName("ird_scene_resource");form->addRow("已有资源",m_resource);
    auto* pathRow=new QHBoxLayout();m_path=new QLineEdit(this);m_path->setObjectName("ird_scene_path");
    m_path->setReadOnly(true);pathRow->addWidget(m_path,1);
    m_browse=new QPushButton("选择文件…",this);m_browse->setObjectName("ird_scene_browse");pathRow->addWidget(m_browse);
    form->addRow("外部网格",pathRow);
    m_confirm=new QCheckBox("确认删除当前对象（共享资源和历史保留）",this);
    m_confirm->setObjectName("ird_scene_confirm_remove");layout->addWidget(m_confirm);
    auto* buttons=new QHBoxLayout();layout->addLayout(buttons);
    m_apply=new QPushButton("确认写入草稿",this);m_apply->setObjectName("ird_scene_apply");buttons->addWidget(m_apply);
    m_remove=new QPushButton("删除对象",this);m_remove->setObjectName("ird_scene_remove");buttons->addWidget(m_remove);
    auto* cancel=new QPushButton("取消暂存",this);cancel->setObjectName("ird_scene_cancel");buttons->addWidget(cancel);
    auto* pose=new QPushButton("编辑世界位姿",this);pose->setObjectName("ird_scene_pose");layout->addWidget(pose);
    m_feedback=new QLabel(this);m_feedback->setWordWrap(true);m_feedback->setObjectName("ird_scene_feedback");layout->addWidget(m_feedback);
    layout->addStretch(1);
    connect(m_source,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateInputs();});
    connect(m_objects,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(!m_before||index<0){return;}
        const auto id=core::ObjectId::tryFromCanonical(m_objects->itemData(index).toString().toStdString());
        // 出口可能同步刷新基线，先保存值再调用，之后不再使用旧工作集引用。
        if(m_choose){m_choose(id);}
    });
    connect(m_browse,&QPushButton::clicked,this,[this]{
        if(!m_writable||!m_before){return;}
        // QFileDialog会运行嵌套事件循环，期间项目切换、局部历史或只读广播都可能重绑本页。
        // 持按值基线及暂存代际，不跨exec保存引用；即使切走又切回相同值，旧选择也必须作废。
        const auto before=*m_before;const auto id=m_id;const auto generation=m_pendingGeneration;
        const QPointer<ModelingSceneEditor> live(this); // 父面板若在嵌套循环关闭，不再解引用已析构页面。
        const auto path=QFileDialog::getOpenFileName(this,"选择场景几何",{},
            "几何文件 (*.stl *.obj *.dae);;全部文件 (*.*)");
        if(!live){return;}
        if(!m_writable||!m_before||*m_before!=before||m_id!=id||m_pendingGeneration!=generation){
            m_feedback->setText("目标、草稿或编辑权限已改变，已丢弃旧文件选择；请在当前对象重新选择。");
            return;
        }
        if(!path.isEmpty()){m_path->setText(path);m_path->setToolTip(path);m_path->setCursorPosition(0);}
    });
    connect(m_apply,&QPushButton::clicked,this,[this]{submit(false);});
    connect(m_remove,&QPushButton::clicked,this,[this]{submit(true);});
    connect(cancel,&QPushButton::clicked,this,[this]{cancelPending();});
    updateInputs();
}

void ModelingSceneEditor::bindTarget(const ModelingWorkingSet* ws,std::optional<core::ObjectId> id,bool writable)
{
    const bool same=ws&&m_before&&*ws==*m_before&&id==m_id;
    if(m_writable!=writable){++m_pendingGeneration;}
    m_writable=writable;
    if(!same){m_before=ws?std::optional<ModelingWorkingSet>(*ws):std::nullopt;m_id=id;cancelPending();}
    updateInputs();
}

void ModelingSceneEditor::cancelPending()
{
    // 显式取消与基线重绑均使正在打开的文件选择失效，不能在返回后恢复已取消输入。
    ++m_pendingGeneration;
    const QSignalBlocker block(m_objects);
    m_objects->clear();m_objects->addItem("新增场景对象",QString());
    m_resource->clear();m_confirm->setChecked(false);m_path->clear();m_feedback->clear();
    m_name->clear();m_role->setCurrentIndex(0);m_source->setCurrentIndex(0);
    if(m_before){
        for(const auto& object:m_before->sceneObjects){
            m_objects->addItem(QString::fromStdString(object.localName),QString::fromStdString(object.objectId.toCanonical()));
        }
        for(const auto& resource:m_before->design.resourceManifest){
            if(!existingGeometryKind(*m_before,resource.resourceId)){continue;}
            // 路径用于人读辨认，内部资源键只放itemData，避免用户需认识res-N。
            const auto label=resource.externalRecord
                ?QString::fromStdString(std::filesystem::u8path(resource.externalRecord->absPath).filename().u8string())
                :QStringLiteral("已固化几何 %1").arg(m_resource->count()+1);
            m_resource->addItem(label,QString::fromStdString(resource.resourceId));
        }
        const auto found=std::find_if(m_before->sceneObjects.begin(),m_before->sceneObjects.end(),
            [this](const SceneObject& object){return m_id&&object.objectId==*m_id;});
        if(found!=m_before->sceneObjects.end()){
            m_objects->setCurrentIndex(m_objects->findData(QString::fromStdString(found->objectId.toCanonical())));
            m_name->setText(QString::fromStdString(found->localName));
            m_role->setCurrentIndex(m_role->findData(static_cast<int>(found->role)));
            if(found->geometry){m_source->setCurrentIndex(1);
                m_resource->setCurrentIndex(m_resource->findData(QString::fromStdString(found->geometry->resourceRefId)));}
        }
    }
    updateInputs();
}

void ModelingSceneEditor::updateInputs()
{
    const bool enabled=m_writable&&m_before.has_value();
    m_name->setEnabled(enabled);m_role->setEnabled(enabled);m_source->setEnabled(enabled);
    m_resource->setEnabled(enabled&&m_source->currentIndex()==1);
    m_path->setEnabled(enabled&&m_source->currentIndex()==2);m_browse->setEnabled(enabled&&m_source->currentIndex()==2);
    m_apply->setEnabled(enabled);m_remove->setEnabled(enabled&&m_id.has_value());
    m_confirm->setEnabled(enabled&&m_id.has_value());m_objects->setEnabled(m_before.has_value());
}

void ModelingSceneEditor::submit(bool remove)
{
    // 控件禁用之外再次检查执行出口，避免直接派发clicked绕过只读守卫。
    if(!m_writable||!m_before){m_feedback->setText("当前项目只读或会话已关闭，未修改草稿。");return;}
    SceneEditRequest request;request.action=remove?SceneEditAction::Remove:
        (m_id?SceneEditAction::Update:SceneEditAction::Create);
    request.target=m_id?*m_id:core::ObjectId::generate();request.localName=m_name->text().toStdString();
    request.role=static_cast<SceneObjectRole>(m_role->currentData().toInt());
    request.source=static_cast<SceneGeometrySource>(m_source->currentIndex());
    request.resourceOrPath=request.source==SceneGeometrySource::External?m_path->text().toStdString():m_resource->currentData().toString().toStdString();
    request.confirmRemove=m_confirm->isChecked();
    std::string error;const auto before=*m_before;
    const auto after=evaluateSceneEdit(before,request,makeIoResourceProbe(),error);
    if(!after){m_feedback->setText(QString::fromStdString(error));return;}
    if(*after==before){cancelPending();m_feedback->setText("内容未改变，未增加草稿历史。");return;}
    const auto selected=remove?std::optional<core::ObjectId>{}:std::optional<core::ObjectId>{request.target};
    if(!m_accept||!m_accept(before,*after,selected,error)){
        m_feedback->setText(QString::fromStdString(error.empty()?"场景编辑服务未连接，未修改草稿。":error));return;
    }
    // 出口可同步重绑/切换目标；只在仍然指向本候选时更新反馈，防污染另一个页。
    if(m_before&&*m_before==*after){m_feedback->setText(remove?"已从草稿删除，共享资源与历史保留。":"已写入草稿；可用『编辑世界位姿』调整位置和姿态。");}
}
}
