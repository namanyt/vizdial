#include "config.hpp"
#include "radial_widget.hpp"

#include <QApplication>
#include <QSocketNotifier>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <csignal>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int sigFd[2] = {-1, -1};

void handlePosixSignal(int) {
    char b = 1;
    if (sigFd[0] >= 0) {
        (void)::write(sigFd[0], &b, 1);
    }
}

void printUsage() {
    std::cout << "vizdial - Frosted radial 5-zone audio visualizer\n\n"
              << "Options:\n"
              << "  -f, --full          Start fullscreen\n"
              << "  --ee                Capture easyeffects_sink.monitor\n"
              << "  --bars <N>          Rays per wing (total circle = 2*N, up to 256)\n"
              << "  --low <HZ>          Zone 1 lower cutoff in Hz (default 20)\n"
              << "  --high <HZ>         Zone 5 higher cutoff in Hz (default 20000)\n"
              << "  --zones             Print active 5-zone ray allocation and exit\n"
              << "  --decay <MS>        Snappy release decay in ms (default 20)\n"
              << "  --gain <FLOAT>      Global gain multiplier\n"
              << "  --sens <FLOAT>      Global sensitivity (0.10-2.00; <1 calmer)\n"
              << "  --width <PX>        Window width (default 840)\n"
              << "  --height <PX>       Window height (default 840)\n"
              << "  --stereo            Use stereo L/R wings instead of mono exact mirror\n"
              << "  --monochrome        Use silver-white monochrome palette\n"
              << "  --source <SRC>      Explicit PipeWire/PulseAudio monitor source\n"
              << "  --demo              Render synthetic test spectrum\n"
              << "  -h, --help          Show help\n";
}

} // namespace

int main(int argc, char *argv[]) {
    CliOverrides cli;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "-h" || a == "--help") {
            printUsage();
            return 0;
        } else if (a == "-f" || a == "--full" || a == "--fullscreen") {
            cli.hasFullscreen = true;
            cli.fullscreen = true;
        } else if (a == "--ee" || a == "--easyeffects") {
            cli.forceEe = true;
        } else if (a == "--stereo") {
            cli.mirrorMode = "stereo";
        } else if (a == "--exact") {
            cli.mirrorMode = "exact";
        } else if (a == "--monochrome") {
            cli.colorMode = "monochrome";
        } else if (a == "--demo") {
            cli.demo = true;
        } else if (a == "--zones") {
            cli.printZones = true;
        } else if (a == "--bars" && i + 1 < argc) {
            cli.bars = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (a == "--low" && i + 1 < argc) {
            cli.lowHz = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (a == "--high" && i + 1 < argc) {
            cli.highHz = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (a == "--decay" && i + 1 < argc) {
            cli.decayMs = QString::fromLocal8Bit(argv[++i]).toFloat();
        } else if (a == "--gain" && i + 1 < argc) {
            cli.gain = QString::fromLocal8Bit(argv[++i]).toFloat();
        } else if (a == "--sens" && i + 1 < argc) {
            cli.sensitivity = QString::fromLocal8Bit(argv[++i]).toFloat();
        } else if (a == "--width" && i + 1 < argc) {
            cli.width = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (a == "--height" && i + 1 < argc) {
            cli.height = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (a == "--source" && i + 1 < argc) {
            cli.source = QString::fromLocal8Bit(argv[++i]);
        }
    }

    if (cli.printZones) {
        const RadialConfig cfg = loadRadialConfig(cli);
        const auto counts = computeZoneRayCounts(cfg);
        std::cout << "Total rays/wing: " << cfg.raysPerWing << " (" << (cfg.raysPerWing * 2) << " total)"
                  << " | bar_height=" << cfg.maxLength << " | global_sens=" << cfg.sensitivity << "\n";
        for (int z = 0; z < 5; ++z) {
            const FreqZone &fz = cfg.zones[z];
            std::cout << "  Zone " << (z + 1) << ": " << fz.minHz << "-" << fz.maxHz << " Hz"
                      << " | rays=" << counts[z] << " (share=" << fz.share << ")"
                      << " | gain=" << fz.gain
                      << " | influence=" << fz.influence
                      << " | sensitivity=" << fz.sensitivity << "\n";
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

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd) == 0) {
        std::signal(SIGINT, handlePosixSignal);
        std::signal(SIGTERM, handlePosixSignal);
        auto *sn = new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &app);
        QObject::connect(sn, &QSocketNotifier::activated, &app, [&app, sn]() {
            sn->setEnabled(false);
            char b;
            (void)::read(sigFd[1], &b, 1);
            app.quit();
        });
    }

    RadialWidget win(cli);
    win.show();
    return app.exec();
}
