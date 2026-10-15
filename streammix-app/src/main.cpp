#include <QApplication>
#include "main_window.h"
#include "streammix/version.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("StreamMix");
    app.setApplicationVersion(sm::VERSION_STRING);
    app.setOrganizationName("StreamMix");
    app.setOrganizationDomain("streammix.live");

    sm::MainWindow window;
    window.show();

    return app.exec();
}
