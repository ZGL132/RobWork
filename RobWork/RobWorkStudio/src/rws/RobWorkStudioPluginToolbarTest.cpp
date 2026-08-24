#include "RobWorkStudioPlugin.hpp"

#include <QApplication>
#include <QToolBar>
#include <QToolButton>

#include <iostream>

namespace {

class ProbePlugin final : public rws::RobWorkStudioPlugin
{
  public:
    explicit ProbePlugin (const QString& name) :
        rws::RobWorkStudioPlugin (name, QIcon ())
    {}
};

bool checkStyle (const QString& name, Qt::ToolButtonStyle expected)
{
    QToolBar toolbar;
    toolbar.setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    ProbePlugin plugin (name);
    plugin.setupToolBar (&toolbar);

    QAction* action = toolbar.actions ().constFirst ();
    QToolButton* button = qobject_cast< QToolButton* > (toolbar.widgetForAction (action));
    if (button == nullptr || button->toolButtonStyle () != expected || action->text () != name ||
        action->toolTip () != name) {
        std::cerr << "toolbar style check failed for " << name.toStdString () << '\n';
        return false;
    }
    return true;
}

} // namespace

int main (int argc, char** argv)
{
    QApplication app (argc, argv);
    bool passed = true;
    for (const QString& name : {QStringLiteral ("EngineeringRequirements"),
                                QStringLiteral ("RobotModelBuilder"),
                                QStringLiteral ("KinematicAnalysis"),
                                QStringLiteral ("StructureOptimizer")}) {
        passed = checkStyle (name, Qt::ToolButtonIconOnly) && passed;
    }
    passed = checkStyle (QStringLiteral ("OtherPlugin"), Qt::ToolButtonTextBesideIcon) && passed;
    return passed ? 0 : 1;
}
