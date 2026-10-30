#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFontDatabase>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("qtNorth");
    QCoreApplication::setApplicationName("qtNorth");
    QCoreApplication::setApplicationVersion(QStringLiteral(QTNORTH_VERSION));
    app.setDesktopFileName("rip.north.qtNorth");
    app.setStyle("Fusion");
    QFont font = app.font();
    const auto families = QFontDatabase::families();
    for (const auto &family : {"Inter", "Segoe UI", "Noto Sans CJK JP", "Noto Sans JP", "Noto Sans", "Yu Gothic UI"}) {
        if (families.contains(QString::fromLatin1(family))) { font.setFamily(QString::fromLatin1(family)); break; }
    }
    font.setPointSize(9);
    app.setFont(font);
    QCommandLineParser parser;
    parser.setApplicationDescription("qtNorth — north.rip desktop client");
    parser.addHelpOption(); parser.addVersionOption();
    parser.process(app);
    MainWindow window;
    window.show();
    return app.exec();
}
