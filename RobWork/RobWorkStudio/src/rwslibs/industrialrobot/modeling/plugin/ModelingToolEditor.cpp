/**
 * @file ModelingToolEditor.cpp
 * @brief MDL-13/04/05工具管理与完整候选事务，依据WP-13-T24 B3h。
 * 安装/TCP沿原域编辑流，资源沿io，物性复用既有唯一合法性/质心迁移，不写项目历史。
 */
#include "ModelingToolEditor.hpp"
#include "src/LocalNameValidation.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
namespace sdurws::ird::modeling {
std::optional<ModelingWorkingSet> evaluateToolEdit(const ModelingWorkingSet& before,
    const ToolEditRequest& request,const ResourceProbeFn& probe,std::string& error)
{
    error.clear();
    const auto reject=[&](const std::string& why)->std::optional<ModelingWorkingSet>{error=why;return std::nullopt;};
    // 身份及根引用必须一致；以ID寻址，绝不能因列表重排把旧请求套到另一工具。
    if(!request.target.isValid()){return reject("工具身份无效，请重新选择。");}
    const auto found=std::find_if(before.toolObjects.begin(),before.toolObjects.end(),
        [&](const ToolDefinition& tool){return tool.objectId==request.target;});
    const bool creating=request.action==ToolEditAction::Create;
    const auto refs=std::count(before.design.toolRefs.begin(),before.design.toolRefs.end(),request.target);
    if(creating){
        bool occupied=before.rootObjectId==request.target||found!=before.toolObjects.end()||refs!=0;
        for(const auto& joint:before.design.joints){occupied=occupied||joint.objectId==request.target;}
        for(const auto& link:before.design.links){occupied=occupied||link.objectId==request.target;}
        for(const auto& scene:before.sceneObjects){occupied=occupied||scene.objectId==request.target;}
        occupied=occupied||(before.poseSetObject&&before.poseSetObject->objectId==request.target)
            ||(before.drivetrainObject&&before.drivetrainObject->objectId==request.target);
        if(occupied){return reject("新工具身份已被占用，请重新创建。");}
    }else if(found==before.toolObjects.end()||refs!=1
        ||std::count_if(before.toolObjects.begin(),before.toolObjects.end(),
            [&](const ToolDefinition& tool){return tool.objectId==request.target;})!=1){
        return reject("工具或根引用已改变，请刷新后重新选择。");
    }
    auto candidate=before;
    const bool currentDefault=before.design.defaultTcp&&before.design.defaultTcp->toolOid==request.target;
    if(request.action==ToolEditAction::Remove){
        if(!request.confirmRemove){return reject("请先明确确认删除；共享资源和历史保留。");}
        // 多工具不能猜测新的默认TCP；先沿原TCP页显式选定，才能删除旧默认工具。
        if(currentDefault&&before.design.toolRefs.size()>1){return reject("当前工具是默认TCP所属工具，请先在其他工具TCP页设置默认后再删除。");}
        auto& objects=candidate.toolObjects;
        objects.erase(std::remove_if(objects.begin(),objects.end(),
            [&](const ToolDefinition& tool){return tool.objectId==request.target;}),objects.end());
        auto& toolRefs=candidate.design.toolRefs;
        toolRefs.erase(std::remove(toolRefs.begin(),toolRefs.end(),request.target),toolRefs.end());
        if(currentDefault){candidate.design.defaultTcp.reset();}
        candidate.changes.push_back({"toolRefs","删除工具 "+found->localName+"；共享资源与历史保留"});
        return candidate;
    }
    if(!inputquality::isLegalLocalName(request.localName)){
        return reject("工具名称须非空，仅含英文字母、数字、下划线、点或短横线。");
    }
    bool nameTaken=false;
    for(const auto& joint:before.design.joints){nameTaken=nameTaken||joint.localName==request.localName;}
    for(const auto& link:before.design.links){nameTaken=nameTaken||link.localName==request.localName;}
    for(const auto& scene:before.sceneObjects){nameTaken=nameTaken||scene.localName==request.localName;}
    for(const auto& tool:before.toolObjects){nameTaken=nameTaken||(tool.objectId!=request.target&&tool.localName==request.localName);}
    if(nameTaken){return reject("工具名称与已有对象重复，请使用其他名称。");}
    if(!before.design.defaultTcp&&!request.makeDefault){
        return reject("请明确勾选本工具为默认TCP；可在安装/TCP页继续编辑初始TCP。");
    }
    if(currentDefault&&!request.makeDefault){
        return reject("不能取消唯一默认引用，请先在其他工具TCP页设置新的默认TCP。");
    }
    ToolDefinition tool=creating?ToolDefinition{}:*found;
    tool.objectId=request.target;tool.localName=request.localName;tool.displayName=request.displayName;
    // 用户在新建页看见并确认初始TCP在工具原点；更新绝不重建目录或重置安装/物性。
    if(creating){TcpEntry tcp;tcp.key="tcp";tcp.displayName="初始TCP";tool.tcpList.push_back(tcp);}
    const auto withGeometry=evaluateManagedGeometry(candidate,tool.geometry,request.source,request.resourceOrPath,probe,error);
    if(!withGeometry){return std::nullopt;}
    candidate=*withGeometry;
    if(request.makeDefault){
        if(tool.tcpList.empty()){return reject("工具没有可用TCP，请先补齐TCP目录。");}
        const auto key=currentDefault?before.design.defaultTcp->tcpKey:tool.tcpList.front().key;
        if(std::none_of(tool.tcpList.begin(),tool.tcpList.end(),[&](const TcpEntry& tcp){return tcp.key==key;})){
            return reject("当前默认TCP已不存在，请刷新TCP目录后重试。");
        }
        candidate.design.defaultTcp=TcpRef{tool.objectId,key};
    }
    if(creating){candidate.toolObjects.push_back(tool);candidate.design.toolRefs.push_back(tool.objectId);}
    else {candidate.toolObjects[static_cast<std::size_t>(found-before.toolObjects.begin())]=tool;}
    if(candidate==before){return candidate;}
    candidate.changes.push_back({"toolObjects",(creating?"新增工具 ":"更新工具 ")+tool.localName});
    return candidate;
}

ModelingToolEditor::ModelingToolEditor(QWidget* parent,Accept accept,Choose choose)
    :QWidget(parent),m_accept(std::move(accept)),m_choose(std::move(choose))
{
    setObjectName("ird_modeling_tool_editor");auto* layout=new QVBoxLayout(this);
    auto* hint=new QLabel("工具随法兰安装。新工具初始TCP在工具原点；确认后可到安装/TCP页编辑位姿和完整TCP目录。确认只写草稿。",this);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto* form=new QFormLayout;layout->addLayout(form);
    m_objects=new QComboBox(this);m_objects->setObjectName("ird_tool_objects");form->addRow("工具",m_objects);
    m_name=new QLineEdit(this);m_name->setObjectName("ird_tool_name");form->addRow("局部名称",m_name);
    m_display=new QLineEdit(this);m_display->setObjectName("ird_tool_display");form->addRow("显示名称",m_display);
    m_source=new QComboBox(this);m_source->setObjectName("ird_tool_source");
    m_source->addItems({"无几何","已有几何资源","外部网格文件"});form->addRow("几何来源",m_source);
    m_resource=new QComboBox(this);m_resource->setObjectName("ird_tool_resource");form->addRow("已有资源",m_resource);
    auto* pathRow=new QHBoxLayout;m_path=new QLineEdit(this);m_path->setReadOnly(true);m_path->setObjectName("ird_tool_path");pathRow->addWidget(m_path,1);
    m_browse=new QPushButton("选择文件…",this);m_browse->setObjectName("ird_tool_browse");pathRow->addWidget(m_browse);form->addRow("外部网格",pathRow);
    m_default=new QCheckBox("设为默认TCP所属工具（沿用当前默认键，其他工具使用首个TCP）",this);m_default->setObjectName("ird_tool_default");layout->addWidget(m_default);
    m_confirm=new QCheckBox("确认删除当前工具（共享资源与历史保留）",this);m_confirm->setObjectName("ird_tool_confirm_remove");layout->addWidget(m_confirm);
    auto* buttons=new QHBoxLayout;layout->addLayout(buttons);
    m_apply=new QPushButton("确认写入草稿",this);m_apply->setObjectName("ird_tool_apply");buttons->addWidget(m_apply);
    m_remove=new QPushButton("删除工具",this);m_remove->setObjectName("ird_tool_remove");buttons->addWidget(m_remove);
    auto* cancel=new QPushButton("取消暂存",this);cancel->setObjectName("ird_tool_cancel");buttons->addWidget(cancel);
    auto* pose=new QPushButton("编辑安装与TCP",this);pose->setObjectName("ird_tool_pose");layout->addWidget(pose);
    m_feedback=new QLabel(this);m_feedback->setWordWrap(true);m_feedback->setObjectName("ird_tool_feedback");layout->addWidget(m_feedback);
    // 工具与连杆共用BodyData语义；子页出口必须同时核对工具及完整管理基线。
    m_body=new ModelingBodyEditor(this,[this](const core::ObjectId& id,const BodyData& before,const ManualBodyResult& after,std::string& error){
        if(!m_writable||!m_before||m_id!=id){error="工具目标或权限已改变，未写入物性。";return false;}
        auto candidate=*m_before;
        const auto it=std::find_if(candidate.toolObjects.begin(),candidate.toolObjects.end(),[&](const ToolDefinition& tool){return tool.objectId==id;});
        if(it==candidate.toolObjects.end()||it->body!=before){error="工具物性基线已改变，请重新确认。";return false;}
        it->body=after.body;candidate.changes.push_back({"tools.body",after.summary});
        return m_accept&&m_accept(*m_before,candidate,id,error);
    });
    m_body->setObjectName("ird_tool_body_editor");layout->addWidget(m_body);
    connect(m_objects,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(m_before&&index>=0&&m_choose){m_choose(core::ObjectId::tryFromCanonical(m_objects->itemData(index).toString().toStdString()));}
    });
    connect(m_source,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateInputs();});
    connect(m_apply,&QPushButton::clicked,this,[this]{submit(false);});
    connect(m_remove,&QPushButton::clicked,this,[this]{submit(true);});
    connect(cancel,&QPushButton::clicked,this,[this]{cancelPending();});
    connect(m_browse,&QPushButton::clicked,this,[this]{
        if(!m_writable||!m_before){return;}
        // 文件窗口的嵌套事件可能重开项目、恢复草稿或销毁页面；只接受同一代际的结果。
        const auto before=*m_before;const auto id=m_id;const auto generation=m_generation;
        const QPointer<ModelingToolEditor> live(this);
        const auto path=QFileDialog::getOpenFileName(this,"选择工具几何",{},"几何文件 (*.stl *.obj *.dae);;全部文件 (*.*)");
        if(!live){return;}
        if(!m_writable||!m_before||*m_before!=before||m_id!=id||generation!=m_generation){
            m_feedback->setText("目标、草稿或权限已改变，已丢弃旧文件选择。");return;
        }
        if(!path.isEmpty()){m_path->setText(path);m_path->setToolTip(path);m_path->setCursorPosition(0);}
    });
    updateInputs();
}

void ModelingToolEditor::bindTarget(const ModelingWorkingSet* ws,std::optional<core::ObjectId> id,bool writable)
{
    const bool same=ws&&m_before&&*ws==*m_before&&m_id==id;
    if(m_writable!=writable){++m_generation;}
    m_writable=writable;
    if(!same){m_before=ws?std::optional<ModelingWorkingSet>(*ws):std::nullopt;m_id=id;cancelPending();}
    // 权限变化也要传给物性子页，不能只把父页按钮灰显。
    const ToolDefinition* tool=nullptr;
    if(m_before&&m_id){for(const auto& entry:m_before->toolObjects){if(entry.objectId==*m_id){tool=&entry;break;}}}
    m_body->bindTarget(tool?m_id:std::nullopt,tool?tool->body:BodyData{},tool?tool->localName:"",writable,"工具");
    updateInputs();
}

void ModelingToolEditor::cancelPending()
{
    ++m_generation;const QSignalBlocker block(m_objects);
    m_objects->clear();m_objects->addItem("新增工具",QString());m_resource->clear();m_name->clear();m_display->clear();
    m_path->clear();m_default->setChecked(false);m_confirm->setChecked(false);m_source->setCurrentIndex(0);m_feedback->clear();
    if(m_before){
        for(const auto& tool:m_before->toolObjects){m_objects->addItem(QString::fromStdString(tool.localName),QString::fromStdString(tool.objectId.toCanonical()));}
        for(const auto& resource:m_before->design.resourceManifest){
            if(!existingGeometryKind(*m_before,resource.resourceId)){continue;}
            const auto label=resource.externalRecord?QString::fromStdString(std::filesystem::u8path(resource.externalRecord->absPath).filename().u8string())
                :QStringLiteral("已固化几何 %1").arg(m_resource->count()+1);
            m_resource->addItem(label,QString::fromStdString(resource.resourceId));
        }
        for(const auto& tool:m_before->toolObjects){
            if(!m_id||tool.objectId!=*m_id){continue;}
            m_objects->setCurrentIndex(m_objects->findData(QString::fromStdString(tool.objectId.toCanonical())));
            m_name->setText(QString::fromStdString(tool.localName));m_display->setText(QString::fromStdString(tool.displayName));
            m_default->setChecked(m_before->design.defaultTcp&&m_before->design.defaultTcp->toolOid==tool.objectId);
            if(tool.geometry){m_source->setCurrentIndex(1);m_resource->setCurrentIndex(m_resource->findData(QString::fromStdString(tool.geometry->resourceRefId)));}
        }
    }
    m_body->cancelPending();updateInputs();
}

void ModelingToolEditor::updateInputs()
{
    const bool enabled=m_writable&&m_before.has_value();
    for(auto* field:{m_name,m_display}){field->setEnabled(enabled);}
    m_source->setEnabled(enabled);m_resource->setEnabled(enabled&&m_source->currentIndex()==1);
    m_path->setEnabled(enabled&&m_source->currentIndex()==2);m_browse->setEnabled(enabled&&m_source->currentIndex()==2);
    m_default->setEnabled(enabled);m_apply->setEnabled(enabled);m_remove->setEnabled(enabled&&m_id.has_value());
    m_confirm->setEnabled(enabled&&m_id.has_value());m_objects->setEnabled(m_before.has_value());
    findChild<QPushButton*>("ird_tool_pose")->setEnabled(m_id.has_value());
}

void ModelingToolEditor::submit(bool remove)
{
    if(!m_writable||!m_before){m_feedback->setText("当前项目只读或已关闭，未修改草稿。");return;}
    ToolEditRequest request;request.action=remove?ToolEditAction::Remove:(m_id?ToolEditAction::Update:ToolEditAction::Create);
    request.target=m_id?*m_id:core::ObjectId::generate();request.localName=m_name->text().toStdString();
    request.displayName=m_display->text().toStdString();request.makeDefault=m_default->isChecked();request.confirmRemove=m_confirm->isChecked();
    request.source=static_cast<ManagedGeometrySource>(m_source->currentIndex());
    request.resourceOrPath=request.source==ManagedGeometrySource::External?m_path->text().toStdString():m_resource->currentData().toString().toStdString();
    const auto before=*m_before;std::string error;
    const auto after=evaluateToolEdit(before,request,makeIoResourceProbe(),error);
    if(!after){m_feedback->setText(QString::fromStdString(error));return;}
    if(*after==before){cancelPending();m_feedback->setText("内容未改变，未增加草稿历史。");return;}
    const auto selected=remove?std::optional<core::ObjectId>{}:std::optional<core::ObjectId>{request.target};
    if(!m_accept||!m_accept(before,*after,selected,error)){
        m_feedback->setText(QString::fromStdString(error.empty()?"工具编辑服务未连接，未修改草稿。":error));return;
    }
    if(m_before&&*m_before==*after){m_feedback->setText(remove?"已从草稿删除；共享资源与历史保留。":"已写入草稿；可继续编辑物性、安装和TCP。");}
}
}
