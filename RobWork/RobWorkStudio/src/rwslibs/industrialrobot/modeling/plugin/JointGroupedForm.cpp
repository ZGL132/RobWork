/**
 * @file JointGroupedForm.cpp
 * @brief GF-A可操作分组表单，所有控件共用同一ParamEditModel，不使用乐观confirmApply。
 * 依据MDL-01/07/12、UX-05/07/09；草稿采纳与正式三维发布明确分离，失败零写入。
 */
#include "JointGroupedForm.hpp"
#include <algorithm>
#include <QStringList>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
namespace sdurws::ird::modeling {
namespace {
/** @brief 创建可折叠区：折叠只控制可见性，不能用checkable GroupBox禁用数据替代。 */
QWidget* section(QVBoxLayout* parent,const QString& title,const QString& key,
                 std::map<QString,bool>& expanded)
{
    auto* owner=parent->parentWidget();auto* button=new QToolButton(owner);
    button->setText(title);button->setObjectName(key+"_toggle");button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);button->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);button->setMinimumWidth(0);button->setToolTip(title);
    button->setStyleSheet("QToolButton { text-align:left; padding:3px; }");
    const bool open=expanded.count(key)?expanded[key]:true;
    button->setChecked(open);button->setArrowType(open?Qt::DownArrow:Qt::RightArrow);
    auto* body=new QWidget(owner);body->setObjectName(key);body->setVisible(open);body->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    parent->addWidget(button);parent->addWidget(body);
    QObject::connect(button,&QToolButton::toggled,body,[body,button,key,&expanded](bool checked){
        expanded[key]=checked;body->setVisible(checked);button->setArrowType(checked?Qt::DownArrow:Qt::RightArrow);});
    return body;
}
}
JointGroupedForm::JointGroupedForm(const JointEntry& baseline,AuthorityMode authority,Submit submit,QWidget* parent)
    :QWidget(parent),m_session(baseline,authority),m_authority(authority),m_submit(std::move(submit))
{
    setObjectName("ird_joint_grouped_form");setMinimumWidth(0);
    auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);
    m_target=new QLabel(this);m_target->setObjectName("ird_joint_target");m_target->setWordWrap(true);root->addWidget(m_target);
    auto* units=new QHBoxLayout();units->addWidget(new QLabel(QStringLiteral("当前页显示单位："),this));
    m_length=new QComboBox(this);m_length->setObjectName("ird_joint_length_unit");m_length->addItems({"m","mm"});
    m_angle=new QComboBox(this);m_angle->setObjectName("ird_joint_angle_unit");m_angle->addItems({"rad","deg"});
    units->addWidget(m_length);units->addWidget(m_angle);units->addStretch();root->addLayout(units);
    auto* scroll=new QScrollArea(this);scroll->setObjectName("ird_joint_form_scroll");scroll->setWidgetResizable(true);scroll->setMinimumWidth(0);
    auto* body=new QWidget(scroll);m_body=new QVBoxLayout(body);m_body->setContentsMargins(0,0,0,0);m_body->setAlignment(Qt::AlignTop);
    // 最小尺寸由reflow按最终单列/三列结果更新，不能让Qt把旧宽栏约束重新钉回去。
    m_body->setSizeConstraint(QLayout::SetNoConstraint);scroll->setWidget(body);root->addWidget(scroll,1);
    auto* basic=section(m_body,QStringLiteral("基础属性"),"ird_joint_group_basic",m_expanded);
    auto* basicLayout=new QGridLayout(basic);
    m_name=new QLineEdit(QString::fromStdString(baseline.localName),basic);m_name->setObjectName("ird_joint_local_name");
    m_type=new QComboBox(basic);m_type->setObjectName("ird_modeling_edit_type");
    for(const auto& t:std::vector<std::pair<JointType,QString>>{
        {JointType::Revolute,QStringLiteral("转动 (Revolute)")},{JointType::Continuous,QStringLiteral("连续转动 (Continuous)")},
        {JointType::Prismatic,QStringLiteral("移动 (Prismatic)")},{JointType::Fixed,QStringLiteral("固定 (Fixed)")}}){
        m_type->addItem(t.second,static_cast<int>(t.first));}
    m_type->setCurrentIndex(m_type->findData(static_cast<int>(baseline.type)));
    basicLayout->addWidget(new QLabel(QStringLiteral("局部名称"),basic),0,0);basicLayout->addWidget(m_name,0,1);
    basicLayout->addWidget(new QLabel(QStringLiteral("关节类型"),basic),1,0);basicLayout->addWidget(m_type,1,1);
    m_status=new QLabel(this);m_status->setObjectName("ird_joint_form_status");m_status->setWordWrap(true);root->addWidget(m_status);
    m_leave=new QWidget(this);auto* leaveLayout=new QVBoxLayout(m_leave);
    leaveLayout->addWidget(new QLabel(QStringLiteral("当前有未确认输入，请选择离开方式："),m_leave));
    for(const auto& entry:std::vector<std::pair<QString,QString>>{{"ird_joint_leave_apply",QStringLiteral("确认后切换")},{"ird_joint_leave_discard",QStringLiteral("放弃输入并切换")},{"ird_joint_leave_stay",QStringLiteral("留在当前对象")}}){
        auto* button=new QPushButton(entry.second,m_leave);button->setObjectName(entry.first);leaveLayout->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,key=entry.first]{
            if(key=="ird_joint_leave_apply"){m_confirmThenLeave=true;prepare();}
            else if(key=="ird_joint_leave_discard"){auto action=std::move(m_afterLeave);resetInputs();if(action){action();}}
            else{m_afterLeave={};m_confirmThenLeave=false;m_leave->hide();}
        });}
    m_leave->hide();root->addWidget(m_leave);
    m_confirm=new QWidget(this);m_confirm->setObjectName("ird_param_confirm_region");auto* confirmLayout=new QVBoxLayout(m_confirm);
    auto* diffScroll=new QScrollArea(m_confirm);diffScroll->setWidgetResizable(true);diffScroll->setMaximumHeight(140);
    m_diff=new QLabel(diffScroll);m_diff->setObjectName("ird_param_confirm_text");m_diff->setWordWrap(true);m_diff->setTextFormat(Qt::PlainText);
    diffScroll->setWidget(m_diff);confirmLayout->addWidget(diffScroll);
    auto* yes=new QPushButton(QStringLiteral("确认写入草稿"),m_confirm);yes->setObjectName("ird_param_confirm_yes");
    auto* no=new QPushButton(QStringLiteral("继续编辑"),m_confirm);no->setObjectName("ird_param_confirm_no");
    confirmLayout->addWidget(yes);confirmLayout->addWidget(no);m_confirm->hide();root->addWidget(m_confirm);
    auto* operations=new QHBoxLayout();m_reset=new QPushButton(QStringLiteral("重置输入"),this);m_reset->setObjectName("ird_param_cancel");
    m_apply=new QPushButton(QStringLiteral("确认写入草稿…"),this);m_apply->setObjectName("ird_param_apply");
    operations->addWidget(m_reset);operations->addStretch();operations->addWidget(m_apply);root->addLayout(operations);
    m_recent=new QLabel(this);m_recent->setObjectName("ird_joint_recent_change");m_recent->setWordWrap(true);root->addWidget(m_recent);
    connect(m_name,&QLineEdit::textEdited,this,[this](const QString& text){m_session.setName(text.toStdString());m_confirm->hide();refreshFields();});
    connect(m_type,&QComboBox::currentIndexChanged,this,[this](int index){
        if(m_updating||index<0){return;}
        if(!m_session.stageType(static_cast<JointType>(m_type->itemData(index).toInt()))){
            QSignalBlocker blocker(m_type);m_type->setCurrentIndex(m_type->findData(static_cast<int>(m_session.type())));
            showError("type","请先纠正非法输入再切换类型");return;}
        m_confirm->hide();rebuildNumeric();refreshFields();
    });
    connect(m_length,&QComboBox::currentTextChanged,this,[this]{changeUnits();});
    connect(m_angle,&QComboBox::currentTextChanged,this,[this]{changeUnits();});
    connect(m_reset,&QPushButton::clicked,this,[this]{resetInputs();});
    connect(m_apply,&QPushButton::clicked,this,[this]{prepare();});
    connect(yes,&QPushButton::clicked,this,[this]{commit();});
    connect(no,&QPushButton::clicked,this,[this]{m_confirm->hide();m_confirmThenLeave=false;});
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape,&QShortcut::activated,this,[this]{
        m_confirm->hide();m_leave->hide();m_afterLeave={};m_confirmThenLeave=false;refreshFields();
    });
    rebuildNumeric();refreshFields();
}
void JointGroupedForm::restorePresentation(const Presentation& state)
{
    for(const auto& pair:state.expanded){
        if(auto* toggle=findChild<QToolButton*>(pair.first+"_toggle")){toggle->setChecked(pair.second);}
    }
    {QSignalBlocker a(m_length),b(m_angle);m_length->setCurrentText(state.length);m_angle->setCurrentText(state.angle);}
    changeUnits();
}
void JointGroupedForm::rebuildNumeric()
{
    // 类型切换重建字段控件，但表单会话持续持有唯一候选，避免旧控件残留。
    if(m_numeric){m_numeric->hide();m_body->removeWidget(m_numeric);m_numeric->setParent(nullptr);m_numeric->deleteLater();}
    m_fields.clear();m_raw.clear();m_numeric=new QWidget(this);auto* stack=new QVBoxLayout(m_numeric);stack->setContentsMargins(0,0,0,0);stack->setAlignment(Qt::AlignTop);stack->setSizeConstraint(QLayout::SetMinimumSize);
    m_body->addWidget(m_numeric);
    auto* axis=section(stack,QStringLiteral("运动轴线（关节局部系，无量纲）"),"ird_joint_group_axis",m_expanded);
    auto* origin=section(stack,QStringLiteral("原点位姿（父连杆系；Rz(Y)Ry(P)Rx(R)）"),"ird_joint_group_origin",m_expanded);
    auto* limits=section(stack,QStringLiteral("物理与工程限制"),"ird_joint_group_limits",m_expanded);
    auto* ga=new QGridLayout(axis);auto* go=new QGridLayout(origin);auto* gl=new QGridLayout(limits);
    const auto model=m_session.model();QObject::connect(m_numeric,&QObject::destroyed,[model]{});
    int index=0;
    for(const auto& spec:model->fields()){
        QWidget* group=index<3?axis:(index<9?origin:limits);QGridLayout* grid=index<3?ga:(index<9?go:gl);
        const int row=index<3?0:(index<9?(index-3)/3:index==9?0:index<=11?1:index-10);
        const int column=index<3?index:(index<9?(index-3)%3:index==11?1:0);
        auto* label=new QLabel(QString::fromStdString(spec.label),group);auto* edit=new QLineEdit(group);
        edit->setObjectName(QString::fromStdString("ird_joint_field_"+spec.key));edit->setMinimumWidth(96);
        auto* unit=new QLabel(group);m_fields.emplace(spec.key,FieldView{label,edit,unit,grid,group,row,column});
        connect(edit,&QLineEdit::textEdited,this,[this,key=spec.key,model](const QString& text){
            if(m_updating||model!=m_session.model()){return;}
            m_raw[key]=text;model->setEditText(key,text.toStdString());m_confirm->hide();
            refreshFields();
        });++index;
    }
    auto* semantics=new QLabel(QStringLiteral("轴非零可归一化，不静默改写；R绕X、P绕Y、Y绕Z。未提供≠0。Continuous范围仅供分析、不取模。"),m_numeric);
    semantics->setWordWrap(true);stack->addWidget(semantics);
    if(m_session.type()!=m_session.baseline().type){
        auto* impact=new QLabel(QStringLiteral("类型切换影响：适用状态将随最终类型改变；跨角/长度量纲的运动参数需重新输入。Continuous硬限位改为不适用，Fixed速度/加速度改为不适用；确认前不会删除草稿值。"),m_numeric);
        impact->setObjectName("ird_joint_type_impact");impact->setWordWrap(true);stack->addWidget(impact);
        auto* carry=new QPushButton(QStringLiteral("明确沿用原区间作为新范围"),m_numeric);carry->setObjectName("ird_joint_carry_interval");
        carry->setEnabled(m_session.type()!=JointType::Prismatic&&m_session.baseline().type!=JointType::Prismatic);
        connect(carry,&QPushButton::clicked,this,[this]{m_session.usePreviousInterval();refreshFields();});stack->addWidget(carry);
    }
    changeUnits();reflow();
}
void JointGroupedForm::refreshFields()
{
    m_updating=true;const auto model=m_session.model();const bool fixed=m_session.type()==JointType::Fixed;
    for(auto& pair:m_fields){const auto& key=pair.first;auto& field=pair.second;
        const auto error=model->statusText(key);
        const auto value=model->currentValueSi(key);
        const QString text=m_raw.count(key)?m_raw[key]:(value?QString::fromStdString(model->displayNumberText(key)):QString());
        if(field.edit->text()!=text){QSignalBlocker blocker(field.edit);field.edit->setText(text);}
        field.edit->setPlaceholderText(QStringLiteral("未提供"));field.unit->setText(QString::fromStdString(std::string(model->displayUnit(key).symbol())));
        const bool derived=m_authority==AuthorityMode::StandardDH&&(key.rfind("axis-",0)==0||key.rfind("origin-",0)==0);
        const bool inapplicable=fixed&&key.rfind("origin-",0)!=0;
        field.edit->setEnabled(m_writable&&!m_stale&&!derived&&!inapplicable);
        field.edit->setToolTip(derived?QStringLiteral("StandardDH派生只读，请经权威转换编辑"):(inapplicable?QStringLiteral("Fixed运动字段不适用"):QString::fromStdString(error)));
        field.edit->setStyleSheet(error.empty()?QString():QStringLiteral("border:1px solid #bd581c"));
    }
    m_name->setEnabled(m_writable&&!m_stale);m_type->setEnabled(m_writable&&!m_stale);
    m_apply->setEnabled(m_writable&&!m_stale);m_reset->setEnabled(true);

    m_target->setText(QStringLiteral("当前关节：%1　权威：%2").arg(QString::fromStdString(m_session.baseline().localName),m_authority==AuthorityMode::StandardDH?QStringLiteral("StandardDH"):QStringLiteral("显式关节")));
    if(m_stale){m_status->setText(QStringLiteral("基线已变化，旧输入已保留；请重置到新草稿后重新编辑。"));}
    else{m_status->setText(m_session.dirty()?QStringLiteral("存在未确认输入；只写草稿，正式三维须项目级应用。"):QStringLiteral("参数与当前草稿一致；选择与显示操作不写项目。"));}
    m_updating=false;
}
void JointGroupedForm::showError(const std::string& key,const std::string& reason)
{
    refreshFields();m_status->setText(QString::fromStdString(reason));
    const auto it=m_fields.find(key);if(it!=m_fields.end()){
        auto* button=m_numeric->findChild<QToolButton*>(it->second.group->objectName()+"_toggle");if(button){button->setChecked(true);}
        it->second.edit->setStyleSheet("border:1px solid #bd581c");it->second.edit->setToolTip(QString::fromStdString(reason));it->second.edit->setFocus();
        if(auto* scroll=findChild<QScrollArea*>("ird_joint_form_scroll")){scroll->ensureWidgetVisible(it->second.edit);}
    }else if(key=="local-name"){m_name->setFocus();}
}
void JointGroupedForm::prepare()
{
    if(!m_writable||m_stale){return;}std::string key,error;auto value=m_session.candidate(key,error);
    if(!value){showError(key,error);return;}
    if(!m_session.dirty()){m_status->setText(QStringLiteral("没有待确认修改"));return;}
    JointFormSession old(m_session.baseline(),m_authority);QStringList diff;
    if(value->localName!=m_session.baseline().localName){diff<<QStringLiteral("名称：%1 → %2").arg(QString::fromStdString(m_session.baseline().localName),QString::fromStdString(value->localName));}
    if(value->type!=m_session.baseline().type){diff<<QStringLiteral("类型：%1 → %2；按上述影响清单更改字段适用状态").arg(QString::fromStdString(std::string(jointTypeToken(m_session.baseline().type))),QString::fromStdString(std::string(jointTypeToken(value->type))));}
    const auto model=m_session.model(),prior=old.model();
    for(const auto& spec:model->fields()){
        const auto found=std::find_if(prior->fields().begin(),prior->fields().end(),[&](const auto& p){return p.key==spec.key;});
        if(found!=prior->fields().end()&&found->kind==spec.kind){prior->setDisplayUnit(spec.key,model->displayUnit(spec.key));}
        const auto before=found==prior->fields().end()?QStringLiteral("未提供"):QString::fromStdString(prior->displayValueText(spec.key));
        if(found==prior->fields().end()||model->currentValueSi(spec.key)!=prior->currentValueSi(spec.key)||value->type!=m_session.baseline().type){
            diff<<QString::fromStdString(spec.label)+"："+before+" → "+QString::fromStdString(model->displayValueText(spec.key));}}
    const auto stateText=[](const auto& v){
        return v.state()==core::FieldState::Provided?QStringLiteral("已提供/")+QString::fromStdString(std::string(core::toToken(v.provenance().kind))):
            (v.state()==core::FieldState::NotApplicable?QStringLiteral("不适用"):QStringLiteral("未提供"));};
    const auto& baseline=m_session.baseline();
    const auto stateDiff=[&](const QString& label,const auto& before,const auto& after){
        if(before!=after){diff<<label+QStringLiteral(" 状态与来源：")+stateText(before)+" → "+stateText(after);}};
    stateDiff(QStringLiteral("轴线"),baseline.axis,value->axis);stateDiff(QStringLiteral("原点"),baseline.origin,value->origin);
    stateDiff(QStringLiteral("硬限位"),baseline.bounds,value->bounds);stateDiff(QStringLiteral("工作范围"),baseline.workingRange,value->workingRange);
    stateDiff(QStringLiteral("最大速度"),baseline.maxVelocity,value->maxVelocity);stateDiff(QStringLiteral("最大加速度"),baseline.maxAcceleration,value->maxAcceleration);
    m_diff->setText(diff.join('\n'));m_confirm->show();m_status->setText(QStringLiteral("请核对完整差异；确认后一次写入草稿。"));
}
void JointGroupedForm::commit()
{
    if(!m_writable||m_stale||m_confirm->isHidden()){return;}
    std::string key,error;auto candidate=m_session.candidate(key,error);if(!candidate){showError(key,error);return;}
    if(const auto e=m_submit(m_session.baseline(),*candidate)){
        const auto reason=e->fieldError?std::string(jointEditErrorCodeToken(e->fieldError->code))+"："+e->detail:e->detail;
        showError(e->fieldKey,reason);return;
    }
    const auto text=m_diff->text();auto action=m_confirmThenLeave?std::move(m_afterLeave):std::function<void()>{};
    m_session=JointFormSession(*candidate,m_authority);m_stale=false;m_incoming.reset();m_afterLeave={};m_confirmThenLeave=false;
    m_name->setText(QString::fromStdString(m_session.name()));
    {QSignalBlocker blocker(m_type);m_type->setCurrentIndex(m_type->findData(static_cast<int>(m_session.type())));}
    m_confirm->hide();m_leave->hide();rebuildNumeric();refreshFields();m_recent->setText(QStringLiteral("上次成功修改：\n")+text);
    if(action){action();}
}
void JointGroupedForm::resetInputs()
{
    if(m_incoming){m_session=JointFormSession(*m_incoming,m_authority);m_incoming.reset();m_stale=false;}
    m_session.reset();m_confirm->hide();m_leave->hide();m_afterLeave={};m_confirmThenLeave=false;
    {QSignalBlocker blocker(m_name);m_name->setText(QString::fromStdString(m_session.name()));}
    {QSignalBlocker blocker(m_type);m_type->setCurrentIndex(m_type->findData(static_cast<int>(m_session.type())));}
    rebuildNumeric();refreshFields();
    // 回调放在本函数末尾；宿主可将本页移出树并deleteLater，禁止继续访问旧控件。
    auto rebound=std::move(m_afterContextReset);if(rebound){rebound();}
}
void JointGroupedForm::invalidateContext(std::function<void()> afterReset)
{
    m_stale=true;m_confirm->hide();m_afterLeave={};m_confirmThenLeave=false;
    if(!m_incoming){m_incoming=m_session.baseline();}
    m_afterContextReset=std::move(afterReset);refreshFields();
}
bool JointGroupedForm::requestLeave(std::function<void()> action)
{
    if(!hasPending()){return true;}m_afterLeave=std::move(action);m_leave->show();return false;
}
void JointGroupedForm::bindCurrent(const JointEntry& joint,AuthorityMode authority,bool writable)
{
    m_writable=writable;
    if(joint!=m_session.baseline()||authority!=m_authority){
        if(hasPending()){m_stale=true;m_incoming=joint;m_authority=authority;}
        else{m_authority=authority;m_session=JointFormSession(joint,authority);resetInputs();}}
    refreshFields();
}
void JointGroupedForm::changeUnits()
{
    const auto model=m_session.model();
    if(model->hasErrors()){
        QSignalBlocker a(m_length),b(m_angle);m_length->setCurrentText(m_lengthUnit);m_angle->setCurrentText(m_angleUnit);
        showError(model->firstErrorKey().value_or(""),"请先纠正非法输入，单位切换不会丢弃原文本");return;
    }
    m_raw.clear(); // 只有显式显示单位切换才重排数值文本，正常输入期间保留原串。
    m_lengthUnit=m_length->currentText();m_angleUnit=m_angle->currentText();
    using K=core::QuantityKind;
    for(const auto& entry:std::vector<std::pair<K,QString>>{{K::Length,m_lengthUnit},{K::Angle,m_angleUnit},
        {K::LinearVelocity,m_lengthUnit+"/s"},{K::LinearAcceleration,m_lengthUnit+"/s^2"},
        {K::AngularVelocity,m_angleUnit+"/s"},{K::AngularAcceleration,m_angleUnit+"/s^2"}}){
        model->setDisplayUnitForKind(entry.first,core::UnitToken::find(entry.second.toStdString()).value());}
    refreshFields();
}
void JointGroupedForm::reflow()
{
    const bool wide=width()>=560;
    for(auto& pair:m_fields){auto& v=pair.second;v.grid->removeWidget(v.label);v.grid->removeWidget(v.edit);v.grid->removeWidget(v.unit);}
    std::map<QGridLayout*,int> rows;
    for(const auto& pair:m_fields){auto* grid=pair.second.grid;for(int row=0;row<grid->rowCount();++row){grid->setRowMinimumHeight(row,0);}}
    // 字段注册顺序控制窄窗和Tab顺序，不能使用std::map的字典序替代业务顺序。
    QLineEdit* previous=m_name;QWidget::setTabOrder(m_name,m_type);QWidget* tab=m_type;
    for(const auto& spec:m_session.model()->fields()){auto& v=m_fields.at(spec.key);const int row=wide?v.row:rows[v.grid]++;const int col=wide?v.column*3:0;
        v.grid->setRowMinimumHeight(row,v.edit->sizeHint().height());
        v.grid->addWidget(v.label,row,col);v.grid->addWidget(v.edit,row,col+1);v.grid->addWidget(v.unit,row,col+2);
        QWidget::setTabOrder(tab,v.edit);tab=v.edit;previous=v.edit;}
    m_numeric->setMinimumSize(0,0);m_numeric->layout()->invalidate();m_numeric->layout()->activate();
    m_body->parentWidget()->setMinimumSize(0,0);m_body->invalidate();m_body->activate();
    if(auto* scroll=findChild<QScrollArea*>("ird_joint_form_scroll")){
        // 滚动区在外层resize时先按旧的三列最小宽度布局；单列重排后主动
        // 同步内容宽度，避免必须再拖一次窗口才解除右侧裁切的滞后一帧。
        auto* body=m_body->parentWidget();
        const int neededWidth=wide?m_body->minimumSize().width():
            std::max(m_numeric->minimumSizeHint().width(),m_name->parentWidget()->minimumSizeHint().width());
        body->setMinimumSize(neededWidth,m_body->minimumSize().height());
        body->resize(std::max(scroll->viewport()->width(),neededWidth),
                     std::max(scroll->viewport()->height(),m_body->minimumSize().height()));
    }
    (void)previous;QWidget::setTabOrder(tab,m_reset);QWidget::setTabOrder(m_reset,m_apply);
}
void JointGroupedForm::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);if(!m_fields.empty()){
        reflow();
        // 外层和滚动区的resize顺序由Qt决定，下一次事件拍复核最终视口尺寸。
        QTimer::singleShot(0,this,[this]{reflow();});
    }
}
}
