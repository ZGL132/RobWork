#include <QApplication>
#include <QFile>
#include <QIcon>

#include <iostream>

int main (int argc, char** argv)
{
    QApplication app (argc, argv);
    Q_INIT_RESOURCE (engineeringrequirements_resources);
    const QIcon icon (":/engineeringrequirements/engineeringrequirements_icon.png");
    QFile resource (":/engineeringrequirements/engineeringrequirements_icon.png");
    if (icon.isNull () || !resource.exists () || icon.availableSizes ().isEmpty ()) {
        std::cerr << "EngineeringRequirements icon resource is unavailable\n";
        return 1;
    }
    return 0;
}
