#include "KinematicAnalysisWidget.hpp"

#include <QLabel>
#include <QVBoxLayout>

KinematicAnalysisWidget::KinematicAnalysisWidget(QWidget* parent) :
    QWidget(parent),
    _studio(NULL),
    _workcell(NULL)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Kinematic Analysis")));
    setLayout(layout);
}

void KinematicAnalysisWidget::setRobWorkStudio(rws::RobWorkStudio* studio)
{
    _studio = studio;
}

void KinematicAnalysisWidget::setWorkCell(rw::models::WorkCell* workcell)
{
    _workcell = workcell;
}
