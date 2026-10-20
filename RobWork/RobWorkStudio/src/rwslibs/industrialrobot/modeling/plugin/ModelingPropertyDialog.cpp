/**
 * @file ModelingPropertyDialog.cpp
 * @brief F09/MDL-05：三图元、多段、密度及段元位姿的真实录入窗口。
 *
 * 依据modeling §5.3：段坐标系原点是均质段几何中心，z沿长度；T_link_seg
 * 相对连杆系。解析后复用本单元ZYX正解和唯一物性估算器，禁止复制公式。
 * 本轮确认值在UI线程栈上存活，取消零写入；估算结果不是动力学可信证据。
 */
#include "ModelingPropertyDialog.hpp"
#include "../src/RpyMath.hpp" // 同单元唯一ZYX正解，不跨单元包含私有头。
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <array>
#include <cmath>

namespace sdurws::ird::modeling {
namespace {
/** @brief 材料菜单值；密度只从域内默认表查询，UI不维护第二份数值表。 */
const std::array<std::pair<const char*,const char*>,6> kMaterials{{
    {"steel","钢"},{"aluminum","铝"},{"cast-iron","铸铁"},
    {"titanium-alloy","钛合金"},{"engineering-plastic","工程塑料"},{"custom","自定义密度"}}};

/**
 * @brief 解析一格显式有限SI数值；不补缺失值、不将NaN/Inf替成零。
 * @param table [in] 当前表格借用引用，UI线程。
 * @param row/column [in] 有效格索引，单位由表头明确。
 * @param value [out] 成功时原双精度值；失败时不供调用方消费。
 * @param error [out] 失败定位到一基行号及表头，供窗口原位显示。
 * @return 解析成功且有限为true；错误保持用户原文，不关闭窗口。
 */
bool readNumber(const QTableWidget& table,int row,int column,double& value,QString& error)
{
    bool ok=false;
    value=QLocale::c().toDouble(table.item(row,column)->text().trimmed(),&ok);
    if(!ok||!std::isfinite(value)){
        error=QStringLiteral("第%1段 / %2：请输入有限数值，单位见表头。")
            .arg(row+1).arg(table.horizontalHeaderItem(column)->text());return false;
    }
    return true;
}
}

std::optional<std::vector<SegmentSpec>> editModelingPropertySegments(const std::string& linkName)
{
    QDialog dialog;dialog.setObjectName("ird_modeling_property_segments");
    dialog.setWindowTitle(QStringLiteral("连杆几何物性估算：")+QString::fromStdString(linkName));
    dialog.resize(1240,640);
    auto* layout=new QVBoxLayout(&dialog);
    auto* note=new QLabel(QStringLiteral("逐段填写尺寸；实心圆柱填r、L，空心填r外、r内、L，长方体填a、b、L。"
        "段原点在几何中心，z沿L；位置与ZYX姿态相对当前连杆，R=Rz(yaw)Ry(pitch)Rx(roll)。"
        "确认后按材料密度合成质量、质心及质心基准惯量，来源为几何估算；不修改视觉网格。"),&dialog);
    note->setWordWrap(true);layout->addWidget(note);
    auto* table=new QTableWidget(0,12,&dialog);table->setObjectName("ird_modeling_segments_table");
    table->setHorizontalHeaderLabels({QStringLiteral("图元"),QStringLiteral("r外 / a (m)"),
        QStringLiteral("r内 / b (m)"),QStringLiteral("L (m)"),QStringLiteral("材料"),
        QStringLiteral("密度 (kg/m³)"),QStringLiteral("x (m)"),QStringLiteral("y (m)"),
        QStringLiteral("z (m)"),QStringLiteral("roll (rad)"),QStringLiteral("pitch (rad)"),QStringLiteral("yaw (rad)")});
    table->setSortingEnabled(false);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    for(int column=0;column<12;++column){table->setColumnWidth(column,column==0||column==4?120:100);}
    layout->addWidget(table,1);
    auto* rowActions=new QHBoxLayout;
    auto* add=new QPushButton(QStringLiteral("增加段元"),&dialog);add->setObjectName("ird_modeling_segments_add");
    auto* remove=new QPushButton(QStringLiteral("删除当前段元"),&dialog);remove->setObjectName("ird_modeling_segments_remove");
    rowActions->addWidget(add);rowActions->addWidget(remove);rowActions->addStretch();layout->addLayout(rowActions);
    auto* feedback=new QLabel(QStringLiteral("填写至少一个段元；尺寸不自动猜测，取消不写入。"),&dialog);
    feedback->setObjectName("ird_modeling_segments_feedback");feedback->setWordWrap(true);layout->addWidget(feedback);
    // 行回调不捕获旧下标，删除前行后按控件现址查当前行，避免将材料/类型
    // 改动写到别的段元。QObject连接以dialog为上下文，窗口销毁即断开。
    const auto findRow=[table](QWidget* cell,int column){
        for(int row=0;row<table->rowCount();++row){if(table->cellWidget(row,column)==cell){return row;}}
        return -1;
    };
    const auto appendRow=[&]{
        const int row=table->rowCount();table->insertRow(row);
        for(int column=1;column<12;++column){if(column!=4){
            table->setItem(row,column,new QTableWidgetItem(column>=6?QStringLiteral("0"):QString()));
        }}
        auto* type=new QComboBox(table);type->addItems({QStringLiteral("实心圆柱"),QStringLiteral("空心圆柱"),QStringLiteral("长方体")});
        table->setCellWidget(row,0,type);
        auto* material=new QComboBox(table);
        for(const auto& entry:kMaterials){material->addItem(QString::fromUtf8(entry.second),QString::fromLatin1(entry.first));}
        table->setCellWidget(row,4,material);
        const auto refreshShape=[table,type,findRow]{
            const int index=findRow(type,0);if(index<0){return;}
            auto* second=table->item(index,2);
            // 实心圆柱不消费第二尺寸，切换类型保留用户原文以便来回比较。
            second->setFlags(type->currentIndex()==0?Qt::ItemIsSelectable:
                Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsEditable);
        };
        const auto refreshDensity=[table,material,findRow]{
            const int index=findRow(material,4);if(index<0){return;}
            auto* density=table->item(index,5);const auto key=material->currentData().toString().toStdString();
            if(const auto value=defaultMaterialDensity(key)){
                density->setText(QString::number(*value,'g',17));density->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
            }else{
                density->setText(QString());density->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsEditable);
            }
        };
        QObject::connect(type,&QComboBox::currentIndexChanged,&dialog,[refreshShape](int){refreshShape();});
        QObject::connect(material,&QComboBox::currentIndexChanged,&dialog,[refreshDensity](int){refreshDensity();});
        refreshShape();refreshDensity();table->setCurrentCell(row,1);
    };
    QObject::connect(add,&QPushButton::clicked,&dialog,appendRow);
    QObject::connect(remove,&QPushButton::clicked,&dialog,[table]{
        if(table->currentRow()>=0){table->removeRow(table->currentRow());}
    });
    appendRow();
    std::optional<std::vector<SegmentSpec>> accepted;
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认估算并写入草稿"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("ird_modeling_segments_confirm");
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(table->rowCount()==0){
            feedback->setText(QStringLiteral("请增加至少一个段元，并填写其尺寸与材料。"));return;
        }
        std::vector<SegmentSpec> segments;
        for(int row=0;row<table->rowCount();++row){
            const auto type=qobject_cast<QComboBox*>(table->cellWidget(row,0))->currentIndex();
            std::array<double,12> numbers{};QString error;
            for(int column=1;column<12;++column){
                if(column==4||(column==2&&type==0)){continue;}
                if(!readNumber(*table,row,column,numbers[column],error)){
                    feedback->setText(error);table->setCurrentCell(row,column);return;
                }
            }
            SegmentSpec segment;
            if(type==0){segment.primitive=SolidCylinderSpec{numbers[1],numbers[3]};}
            else if(type==1){segment.primitive=HollowCylinderSpec{numbers[1],numbers[2],numbers[3]};}
            else{segment.primitive=BoxSpec{numbers[1],numbers[2],numbers[3]};}
            segment.linkFromSegment=rw::math::Transform3D<double>(
                {numbers[6],numbers[7],numbers[8]},rpymath::rpyToRotation(numbers[9],numbers[10],numbers[11]));
            const auto key=qobject_cast<QComboBox*>(table->cellWidget(row,4))->currentData().toString().toStdString();
            segment.material.materialId=key;
            // 默认材料只传域内键；显式密度带UserProvided，不伪造材料表来源。
            if(key=="custom"){
                segment.material.density=core::SourcedValue<double>::provided(numbers[5],
                    core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
            }
            // 域内错误detail面向诊断日志，不把@pre或字段路径直接呈现给用户。
            // 每段仍调用唯一估算器定位失败行，本层仅翻译纠错说明，不复制公式。
            std::vector<core::DiagnosticRecord> segmentDiags;
            const auto single=PropertyEstimator{}.estimateLink({segment},segmentDiags);
            if(!single.ok()){
                QString advice;
                switch(single.error().code){
                case EstimateErrorCode::IllegalDimension:
                    advice=QStringLiteral("尺寸与密度必须大于零；空心圆柱须外半径大于内半径，单位见表头。");break;
                case EstimateErrorCode::MaterialDensityMissing:
                    advice=QStringLiteral("请选择有效材料或填写自定义密度（kg/m³）。");break;
                case EstimateErrorCode::SynthesisFailed:
                    advice=QStringLiteral("计算结果超出有效范围，请检查尺寸、密度与位置的数值及单位。");break;
                }
                feedback->setText(QStringLiteral("第%1段：估算未通过。%2").arg(row+1).arg(advice));
                table->setCurrentCell(row,1);return;
            }
            segments.push_back(std::move(segment));
        }
        std::vector<core::DiagnosticRecord> diags;
        const auto result=PropertyEstimator{}.estimateLink(segments,diags);
        if(!result.ok()){
            feedback->setText(QStringLiteral("估算未通过：多段合成结果超出有效范围，请检查尺寸、密度、位置及单位。"));return;
        }
        // 预检与流程写回均真实消费同一完整向量；窗口只移交值，不持工作集指针。
        accepted=std::move(segments);dialog.accept();
    });
    dialog.exec();return accepted;
}
}
