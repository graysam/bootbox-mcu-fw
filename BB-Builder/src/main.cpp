#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    BBB::MainWindow window;
    window.show();

    return app.exec();
}
