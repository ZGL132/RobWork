/**
 * @file ModelingDiffDialog.cpp
 * @brief WP-13-T24/F16：按原报告完整呈现差异，复用会话对象定位。
 *
 * 依据MDL-08/UX-13/AT-12：比较不覆盖基线；长报告可滚动/选择复制，
 * 机器身份只作内部定位，界面显示名称。无Qt计算逻辑，不重算差异语义。
 */
#include "ModelingDiffDialog.hpp"
#include <sdurws/ird/modeling/ModelDiff.hpp>
#include <sdurws/ird/ui/UiText.hpp>
#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include <map>

namespace sdurws::ird::modeling {
namespace {
/** @brief 提取本工作集可读名称目录；[in] ws为只读值，返回目录自持字符串。 */
std::map<std::string,std::string> namesFor(const ModelingWorkingSet& ws)
{
    std::map<std::string,std::string> names;
    if(ws.rootObjectId){names[ws.rootObjectId->toCanonical()]="模型 "+ws.design.displayName;}
    for(const auto& joint:ws.design.joints){names[joint.objectId.toCanonical()]="关节 "+joint.localName;}
    for(const auto& link:ws.design.links){names[link.objectId.toCanonical()]="连杆 "+link.localName;}
    for(const auto& tool:ws.toolObjects){names[tool.objectId.toCanonical()]="工具 "+tool.localName;}
    for(const auto& scene:ws.sceneObjects){names[scene.objectId.toCanonical()]="场景 "+scene.localName;}
    if(ws.poseSetObject){names[ws.poseSetObject->objectId.toCanonical()]="命名位姿";}
    if(ws.drivetrainObject){names[ws.drivetrainObject->objectId.toCanonical()]="传动配置";}
    return names;
}

/**
 * @brief 将报告文本变成可见文本；[in] raw原报告，[in] names为该侧名称目录。
 * @return 不含内部内容摘要/对象ID的字符串，保留原数值和来源；空文本明确标空。
 * 这是呈现转换，不改服务报告；未知引用不伪造名称，摘要只提示其发生变化。
 */
QString visibleText(const std::string& raw,const std::map<std::string,std::string>& names)
{
    if(raw.empty()){return QStringLiteral("（空）");}
    QString text=QString::fromStdString(raw);
    for(const auto& item:names){
        text.replace(QString::fromStdString(item.first),QString::fromStdString(item.second));
    }
    text.replace(QRegularExpression(QStringLiteral("obj-[0-9a-f]{32}")),QStringLiteral("（未解析引用）"));
    text.replace(QRegularExpression(QStringLiteral("[0-9a-fA-F]{64,}")),QStringLiteral("（内容摘要）"));
    ui::ensureNoInternalIdentity(text.toStdString());
    return text;
}

/**
 * @brief 根据原字段与当前/基线关节类型注明物理单位，零数值计算。
 * @param entry [in] 原差异条目；[in] baseline/candidate用于关节rad/m类别解析。
 * @return 单位文本，非物理量为“—”；不按数组旧下标猜关节类型。
 */
QString unitFor(const ModelDiffEntry& entry,const ModelingWorkingSet& baseline,
                const ModelingWorkingSet& candidate)
{
    const auto& field=entry.field;
    if(field=="mass"){return QStringLiteral("kg");}
    if(field=="centerOfMass"||field=="basePosition"){return QStringLiteral("m");}
    if(field=="inertia"){return QStringLiteral("kg·m²");}
    if(field=="customEaa"){return QStringLiteral("rad");}
    if(field=="origin"){return QStringLiteral("平移 m；旋转矩阵无量纲");}
    if(field=="dhDerived"){return QStringLiteral("d/a: m；θ/α: rad");}
    if(field=="zeroOffset"||field=="bounds"||field=="workingRange"){
        QString units[2];int side=0;
        for(const auto* ws:{&baseline,&candidate}){
            for(const auto& joint:ws->design.joints){
                if(joint.objectId==entry.objectId){units[side]=joint.type==JointType::Prismatic?
                    QStringLiteral("m"):QStringLiteral("rad");break;}
            }
            ++side;
        }
        // 类型切换时前后数值的单位可能不同，不能只用草稿类型标注两侧。
        if(!units[0].isEmpty()&&!units[1].isEmpty()&&units[0]!=units[1]){
            return QStringLiteral("基线 %1；草稿 %2").arg(units[0],units[1]);
        }
        if(!units[1].isEmpty()){return units[1];}
        if(!units[0].isEmpty()){return units[0];}
        return QStringLiteral("rad / m（对象已移除）");
    }
    return QStringLiteral("—");
}

/** @brief 三组的固定中文标题；[in] group来自原报告，返回展示值，无共享状态。 */
QString groupTitle(ModelDiffGroup group)
{
    switch(group){
    case ModelDiffGroup::Structure:return QStringLiteral("结构");
    case ModelDiffGroup::Parameters:return QStringLiteral("参数");
    case ModelDiffGroup::Properties:return QStringLiteral("物性");
    }
    return {};
}

/** @brief 原变化态的中文呈现；[in] kind为增删改词表，不另判差异。 */
QString changeTitle(ModelDiffChangeKind kind)
{
    switch(kind){
    case ModelDiffChangeKind::Added:return QStringLiteral("新增");
    case ModelDiffChangeKind::Removed:return QStringLiteral("删除");
    case ModelDiffChangeKind::Modified:return QStringLiteral("修改");
    }
    return {};
}
}

void showModelingDiffDialog(const ModelDiffReport& report,
    const ModelingWorkingSet& baseline,const ModelingWorkingSet& candidate,
    const ModelingDiffLocator& locate)
{
    // 三组和组内顺序完全沿原服务，表格禁止排序，确保行与真实条目一一对应。
    std::vector<const ModelDiffEntry*> rows;
    for(const auto* group:{&report.structure,&report.parameters,&report.properties}){
        for(const auto& entry:*group){rows.push_back(&entry);}
    }
    const auto baselineNames=namesFor(baseline),candidateNames=namesFor(candidate);
    QDialog dialog;
    dialog.setObjectName("ird_modeling_diff");dialog.setWindowTitle(QStringLiteral("与基线比较：逐项差异"));
    dialog.resize(1140,650);
    auto* layout=new QVBoxLayout(&dialog);
    auto* note=new QLabel(QStringLiteral("结构 %1 条 / 参数 %2 条 / 物性 %3 条。"
        "双击行或点击定位；查看和定位不改变基线、草稿及项目修订。")
        .arg(report.structure.size()).arg(report.parameters.size()).arg(report.properties.size()),&dialog);
    note->setWordWrap(true);layout->addWidget(note);
    auto* table=new QTableWidget(static_cast<int>(rows.size()),9,&dialog);
    table->setObjectName("ird_modeling_diff_table");
    table->setHorizontalHeaderLabels({QStringLiteral("分组"),QStringLiteral("对象"),QStringLiteral("字段路径"),
        QStringLiteral("变化"),QStringLiteral("单位"),QStringLiteral("基线值与来源"),QStringLiteral("草稿值与来源"),
        QStringLiteral("值变化"),QStringLiteral("来源变化")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);table->setSortingEnabled(false);
    table->setWordWrap(false);table->horizontalHeader()->setStretchLastSection(true);
    // 常用窗口先容纳全部九列（尤其“来源变化”），窄窗口仍可横向滚动；
    // 长原文不撑大对话框，在下方完整查看/选择复制。
    const int widths[]={60,110,175,55,90,200,200,65,75};
    for(int col=0;col<9;++col){table->setColumnWidth(col,widths[col]);}
    for(int row=0;row<table->rowCount();++row){
        const auto& entry=*rows[row];
        QString name=QStringLiteral("模型 ")+QString::fromStdString(candidate.design.displayName);
        if(entry.field=="resource"){name=QStringLiteral("资源清单条目");}
        if(entry.objectId.isValid()){
            const auto key=entry.objectId.toCanonical();
            const auto current=candidateNames.find(key),old=baselineNames.find(key);
            if(current!=candidateNames.end()){name=QString::fromStdString(current->second);}
            else if(old!=baselineNames.end()){name=QString::fromStdString(old->second);}
            else{name=QStringLiteral("引用或资源（无树节点）");}
        }
        const QStringList cells={groupTitle(entry.group),visibleText(name.toStdString(),candidateNames),
            visibleText(entry.subjectPath,candidateNames),changeTitle(entry.kind),unitFor(entry,baseline,candidate),
            // 空字符串是合法字段值；只有原报告的增删类型才表明整条对象不存在。
            entry.kind==ModelDiffChangeKind::Added?QStringLiteral("（不存在）"):
                visibleText(entry.baselineText,baselineNames),
            entry.kind==ModelDiffChangeKind::Removed?QStringLiteral("（不存在）"):
                visibleText(entry.candidateText,candidateNames),
            entry.valueChanged?QStringLiteral("是"):QStringLiteral("否"),
            entry.provenanceChanged?QStringLiteral("是"):QStringLiteral("否")};
        for(int col=0;col<cells.size();++col){
            auto* item=new QTableWidgetItem(cells[col]);item->setToolTip(cells[col]);table->setItem(row,col,item);
        }
    }
    layout->addWidget(table,1);
    auto* detail=new QPlainTextEdit(&dialog);detail->setObjectName("ird_modeling_diff_detail");
    detail->setReadOnly(true);detail->setMaximumHeight(150);
    detail->setPlaceholderText(QStringLiteral("选择差异行查看完整前后值；支持选择复制。"));layout->addWidget(detail);
    auto* feedback=new QLabel(rows.empty()?QStringLiteral("两模型逐字段全等，无差异。"):
        QStringLiteral("选择差异行以查看详情或定位对象。"),&dialog);
    feedback->setObjectName("ird_modeling_diff_feedback");feedback->setWordWrap(true);layout->addWidget(feedback);
    auto* jump=new QPushButton(QStringLiteral("定位对象"),&dialog);jump->setObjectName("ird_modeling_diff_locate");
    jump->setEnabled(!rows.empty());layout->addWidget(jump);
    // 只保存原条目借用指针，窗口关闭即全部释放；不能定位时由模块回执说明。
    const auto locateCurrent=[&]{
        const int row=table->currentRow();if(row<0||row>=static_cast<int>(rows.size())){return;}
        const auto response=locate?locate(*rows[row]):std::string("无法定位：当前未装配对象定位入口。");
        feedback->setText(visibleText(response,candidateNames));
    };
    QObject::connect(jump,&QPushButton::clicked,&dialog,locateCurrent);
    QObject::connect(table,&QTableWidget::cellDoubleClicked,&dialog,[&](int,int){locateCurrent();});
    QObject::connect(table,&QTableWidget::currentCellChanged,&dialog,[&](int row,int,int,int){
        if(row<0){detail->clear();return;}
        detail->setPlainText(QStringLiteral("%1 / %2 / %3\n基线：%4\n草稿：%5")
            .arg(table->item(row,1)->text(),table->item(row,2)->text(),table->item(row,4)->text(),
                 table->item(row,5)->text(),table->item(row,6)->text()));
    });
    if(!rows.empty()){table->setCurrentCell(0,0);}
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);
    dialog.exec();
}
}
