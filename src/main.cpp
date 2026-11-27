#include "config.hpp"
#include "radial_widget.hpp"

#include <QApplication>
#include <QStyleFactory>
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
        } else if (a == "--zones") {
            cli.printZones = true;
        } else if (a == "--sens" && i + 1 < argc) {
            cli.sensitivity = QString::fromLocal8Bit(argv[++i]).toFloat();
        } else if (a == "--bars" && i + 1 < argc) {
            cli.bars = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (a == "--source" && i + 1 < argc) {
            cli.source = QString::fromLocal8Bit(argv[++i]);
        }
    }

    if (cli.printZones) {
        const RadialConfig cfg = loadRadialConfig(cli);
        const auto counts = computeZoneRayCounts(cfg);
        std::cout << "Total rays/wing: " << cfg.raysPerWing << " (" << (cfg.raysPerWing * 2) << " total)\n";
        for (int z = 0; z < 5; ++z) {
            std::cout << "  Zone " << (z + 1) << ": " << cfg.zones[z].minHz << "-" << cfg.zones[z].maxHz << " Hz | rays=" << counts[z] << "\n";
        }
        return 0;
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
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }

    RadialWidget win(cli);
    win.show();
    return app.exec();
}
