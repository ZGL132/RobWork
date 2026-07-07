#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP

#include <QWidget>

namespace rw { namespace models { class WorkCell; } }
namespace rws { class RobWorkStudio; }

class KinematicAnalysisWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KinematicAnalysisWidget(QWidget* parent = NULL);

    void setRobWorkStudio(rws::RobWorkStudio* studio);
    void setWorkCell(rw::models::WorkCell* workcell);

private:
    rws::RobWorkStudio* _studio;
    rw::models::WorkCell* _workcell;
};

#endif
