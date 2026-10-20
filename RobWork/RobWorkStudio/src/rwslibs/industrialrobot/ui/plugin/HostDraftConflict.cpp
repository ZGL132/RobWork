/**
 * @file HostDraftConflict.cpp
 * @brief PM-04/AT-29：真实冲突窗口及保存门控；域重建仍由控制器和域源唯一执行。
 */
#include "HostDraftConflict.hpp"
#include <QDialog>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <stdexcept>
namespace sdurws::ird::ui {
namespace {
/**
 * @brief 将保存端口稳定token映射为可采取行动的中文原因，开发detail不得进入窗口。
 * @param token [in] 对端原稳定错误键；未知键不猜权限/磁盘原因。
 * @return 人可读原因；纯值函数，无状态、不修改对端错误分类。
 */
QString saveFailureReason(const std::string& token)
{
    if(token=="disk-full"){return QStringLiteral("磁盘空间不足，请释放项目所在磁盘空间");}
    if(token=="media-read-only"){return QStringLiteral("项目介质只读，请恢复可写权限");}
    if(token=="access-denied"){return QStringLiteral("没有写入权限，请检查项目目录权限");}
    if(token=="lock-held-by-other"){return QStringLiteral("项目被其他实例锁定，请先解除写入占用");}
    if(token=="context-closed"){return QStringLiteral("项目已经关闭，请重新打开项目");}
    return QStringLiteral("保存未完成，请检查诊断并确认项目目录可写后重试");
}
}
DraftConflictResolution presentDraftConflict(QWidget* parent,IDraftController& controller,
    const ModuleDraftHandle& module,const std::function<bool()>& stillCurrent)
{
    if(!stillCurrent){throw std::invalid_argument("草稿冲突窗口缺少会话有效性检查");}
    if(!stillCurrent()||!controller.hasSession()){return DraftConflictResolution::Unavailable;}
    const auto data=controller.staleConflictData(module);
    if(!data.present){return DraftConflictResolution::Unavailable;}

    // 第一步只落原稿。命令服务拒绝并没有保存完整域payload，不能省略此处，
    // 更不能在磁盘失败后先清掉原草稿/历史以换取界面上的“重新编辑成功”。
    bool saved=false;QString failure;
    try{
        const auto outcome=controller.saveAll(SaveTrigger::ApplyRetained);
        saved=outcome.failedCount==0&&outcome.savedCount>0;
        if(!saved){failure=QStringLiteral("旧稿保存失败：%1。原草稿仍保留，暂不能重新编辑。")
            .arg(saveFailureReason(outcome.firstError?outcome.firstError->errorToken:std::string{}));}
    }catch(const std::exception& error){
        // 异常可含路径、身份或内部实现信息，仅留开发日志，窗口保持可操作的指引。
        qWarning().noquote()<<"草稿冲突保留失败："<<error.what();
        failure=QStringLiteral("旧稿保存失败，请检查诊断及项目可写状态。原草稿仍保留，暂不能重新编辑。");
    }
    if(!stillCurrent()||!controller.hasSession()){return DraftConflictResolution::Unavailable;}

    QDialog dialog(parent);dialog.setObjectName("ird_draft_conflict");
    dialog.setWindowTitle(QStringLiteral("草稿基线冲突"));dialog.resize(760,430);
    auto* layout=new QVBoxLayout(&dialog);
    // 只显示同源详情值；文本全部按纯文本处理，模型名称不能被当成HTML执行。
    const auto label=[&](const QString& text,const char* name){
        auto* item=new QLabel(text,&dialog);item->setObjectName(name);item->setWordWrap(true);
        item->setTextFormat(Qt::PlainText);item->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(item);return item;
    };
    label(QStringLiteral("%1草稿基于旧版本，不能直接应用。%2\n重新编辑会读取当前版本的完整基线；旧稿将保留供逐项核对，之后仍需您明确应用。")
        .arg(QString::fromStdString(data.moduleDisplayName),saved?QStringLiteral("原草稿已保存。"):QString()),"ird_draft_conflict_note");
    label(QStringLiteral("草稿基线：%1\n当前版本：%2\n前进摘要：%3")
        .arg(QString::fromStdString(data.draftBaseRevision),QString::fromStdString(data.currentTipRevision),
             QString::fromStdString(data.advancedSummary)),"ird_draft_conflict_revisions");
    QStringList names;for(const auto& name:data.involvedObjectNames){names.push_back(QString::fromStdString(name));}
    auto* objects=label(names.empty()?QStringLiteral("未提供可定位的差异对象；请重新获取冲突详情。")
        :QStringLiteral("涉事对象：\n%1").arg(names.join('\n')),"ird_draft_conflict_objects");
    objects->hide();
    auto* error=label(failure,"ird_draft_conflict_error");layout->addStretch(1);
    auto* buttons=new QHBoxLayout();layout->addLayout(buttons);
    auto* reedit=new QPushButton(QStringLiteral("基于当前版本重新编辑（推荐）"),&dialog);
    reedit->setObjectName("ird_draft_conflict_reedit");reedit->setDefault(true);
    reedit->setEnabled(saved&&core::RevisionId::tryFromCanonical(data.currentTipRevision).has_value());
    auto* diff=new QPushButton(QStringLiteral("查看差异对象"),&dialog);diff->setObjectName("ird_draft_conflict_diff");
    auto* cancel=new QPushButton(QStringLiteral("取消"),&dialog);cancel->setObjectName("ird_draft_conflict_cancel");
    buttons->addWidget(reedit);buttons->addWidget(diff);buttons->addWidget(cancel);
    DraftConflictResolution resolution=DraftConflictResolution::Cancelled;
    QObject::connect(diff,&QPushButton::clicked,&dialog,[&]{objects->show();});
    QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    QObject::connect(reedit,&QPushButton::clicked,&dialog,[&]{
        // 第二步：模态事件循环可能已关闭/切换项目或推进tip。保活指针只证明
        // 对象尚存，不能替代当前性校验；漂移时禁止用旧确认覆盖新内容。
        try{
            if(!stillCurrent()||!controller.hasSession()){
                error->setText(QStringLiteral("项目或草稿已变化，请取消并重新发起冲突处置；原草稿未被覆盖。"));
                reedit->setEnabled(false);return;
            }
            const auto latest=controller.staleConflictData(module);
            if(!latest.present||latest.currentTipRevision!=data.currentTipRevision
                ||latest.draftBaseRevision!=data.draftBaseRevision){
                error->setText(QStringLiteral("冲突基线已变化，请取消并重新获取详情。"));
                reedit->setEnabled(false);return;
            }
            controller.reEditOnCurrentRevision(module); // 完整读取成功返回后才清冲突/脏代次。
            resolution=DraftConflictResolution::Rebuilt;dialog.accept();
        }catch(const std::exception& why){
            qWarning().noquote()<<"草稿冲突重建失败："<<why.what();
            error->setText(QStringLiteral("无法读取当前版本的完整模型，请检查项目数据和诊断后重试或取消。原草稿仍保留。"));
        }
    });
    dialog.exec();return resolution; // 关闭窗口与取消完全同义，零重建/零自动提交。
}
}
