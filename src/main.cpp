#include "config.hpp"
#include "radial_widget.hpp"

#include <QApplication>
#include <QSurfaceFormat>
#include <iostream>

int main(int argc, char *argv[]) {
    CliOverrides cli;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "-h" || a == "--help") {
            std::cout << "vizdial - Radial audio visualizer\n"
                      << "  -f, --full    Fullscreen\n"
                      << "  --ee          Capture easyeffects_sink.monitor\n"
                      << "  --bars <N>    Rays per wing\n"
                      << "  --demo        Demo waveform\n";
            return 0;
        } else if (a == "-f" || a == "--full" || a == "--fullscreen") {
            cli.hasFullscreen = true;
            cli.fullscreen = true;
        } else if (a == "--ee" || a == "--easyeffects") {
            cli.forceEe = true;
        } else if (a == "--demo") {
            cli.demo = true;
        } else if (a == "--bars" && i + 1 < argc) {
            cli.bars = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (a == "--source" && i + 1 < argc) {
            cli.source = QString::fromLocal8Bit(argv[++i]);
        }
    }

    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setAlphaBufferSize(8);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("vizdial");

    RadialWidget win(cli);
    win.show();
    return app.exec();
}
