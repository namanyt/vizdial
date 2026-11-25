#include "config.hpp"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <map>

namespace {

QColor parseHex(const QString &raw, const QColor &fallback) {
    QString s = raw.trimmed();
    s.remove('\'').remove('"');
    if (!s.startsWith('#')) {
        return fallback;
    }
    QColor c(s);
    return c.isValid() ? c : fallback;
}

QColor mixColors(const QColor &a, const QColor &b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return QColor::fromRgbF(
        a.redF() + (b.redF() - a.redF()) * t,
        a.greenF() + (b.greenF() - a.greenF()) * t,
        a.blueF() + (b.blueF() - a.blueF()) * t
    );
}

using IniMap = std::map<QString, std::map<QString, QString>>;

IniMap readIniRaw(const QString &path) {
    IniMap out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    QTextStream in(&f);
    QString section;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
            continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        const int eq = line.indexOf('=');
        if (eq > 0) {
            out[section][line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
        }
    }
    return out;
}

QString iniStr(const IniMap &m, const QString &sec, const QString &key, const QString &def) {
    auto sit = m.find(sec);
    if (sit == m.end()) return def;
    auto kit = sit->second.find(key);
    return (kit != sit->second.end()) ? kit->second : def;
}

int iniInt(const IniMap &m, const QString &sec, const QString &key, int def) {
    bool ok = false;
    const int v = iniStr(m, sec, key, "").toInt(&ok);
    return ok ? v : def;
}

float iniFloat(const IniMap &m, const QString &sec, const QString &key, float def) {
    bool ok = false;
    const float v = iniStr(m, sec, key, "").toFloat(&ok);
    return ok ? v : def;
}

QColor readIniKeyColor(const QString &path, const QString &sectionName, const QString &key, const QColor &fallback) {
    const IniMap m = readIniRaw(path);
    const QString raw = iniStr(m, sectionName, key, "");
    return raw.isEmpty() ? fallback : parseHex(raw, fallback);
}

void applyAutoTheme(RadialConfig &cfg) {
    const QColor kdeBg = readIniKeyColor(kdeGlobalsPath(), "Colors:Complementary", "BackgroundNormal", QColor("#131318"));
    cfg.bgColor = kdeBg.isValid() ? kdeBg : QColor("#131318");

    const QString mPath = matugenThemePath();
    const QColor g1 = readIniKeyColor(mPath, "color", "gradient_color_1", QColor("#5c63b8"));
    const QColor g2 = readIniKeyColor(mPath, "color", "gradient_color_2", QColor("#bfc2ff"));
    const QColor g3 = readIniKeyColor(mPath, "color", "gradient_color_3", QColor("#e0e0ff"));

    cfg.borderColor = g2;
    cfg.ringColor = g2;
    cfg.rayInnerColor = mixColors(g1, g2, 0.50f);
    cfg.rayMidColor = g2;
    cfg.rayOuterColor = mixColors(g3, QColor("#ffffff"), 0.55f);
}

} // namespace

QString configFilePath() {
    return QDir::homePath() + "/.config/vizdial/config.ini";
}

QString shaderFilePath() {
    return QDir::homePath() + "/.config/vizdial/radial.frag";
}

QString matugenThemePath() {
    return QDir::homePath() + "/.config/cava/themes/matugen";
}

QString kdeGlobalsPath() {
    return QDir::homePath() + "/.config/kdeglobals";
}

RadialConfig loadRadialConfig(const CliOverrides &cli) {
    RadialConfig cfg;
    const IniMap ini = readIniRaw(configFilePath());

    cfg.width = std::clamp(iniInt(ini, "window", "width", cfg.width), 320, 2800);
    cfg.height = std::clamp(iniInt(ini, "window", "height", cfg.height), 320, 2000);
    cfg.cornerRadius = std::clamp(iniFloat(ini, "window", "corner_radius", cfg.cornerRadius), 0.0f, 96.0f);
    cfg.bgOpacity = std::clamp(iniFloat(ini, "window", "bg_opacity", cfg.bgOpacity), 0.0f, 1.0f);
    cfg.borderOpacity = std::clamp(iniFloat(ini, "window", "border_opacity", cfg.borderOpacity), 0.0f, 1.0f);

    cfg.source = iniStr(ini, "audio", "source", cfg.source).trimmed();
    if (cfg.source.isEmpty()) cfg.source = "auto";
    cfg.framerate = std::clamp(iniInt(ini, "audio", "framerate", cfg.framerate), 30, 240);
    cfg.dbRange = std::clamp(iniFloat(ini, "audio", "db_range", cfg.dbRange), 18.0f, 72.0f);
    cfg.attackMs = std::clamp(iniFloat(ini, "audio", "attack_ms", cfg.attackMs), 0.5f, 80.0f);
    cfg.decayMs = std::clamp(iniFloat(ini, "audio", "decay_ms", cfg.decayMs), 2.0f, 250.0f);
    cfg.gain = std::clamp(iniFloat(ini, "audio", "gain", cfg.gain), 0.2f, 5.0f);
    cfg.gamma = std::clamp(iniFloat(ini, "audio", "gamma", cfg.gamma), 0.35f, 2.5f);

    cfg.raysPerWing = std::clamp(iniInt(ini, "visual", "rays_per_wing", cfg.raysPerWing), 16, 256);
    cfg.baseRadius = std::clamp(iniFloat(ini, "visual", "base_radius", cfg.baseRadius), 0.05f, 0.42f);
    cfg.maxLength = std::clamp(iniFloat(ini, "visual", "max_length", cfg.maxLength), 0.04f, 0.32f);
    cfg.rayWidthPx = std::clamp(iniFloat(ini, "visual", "ray_width_px", cfg.rayWidthPx), 0.8f, 14.0f);
    cfg.ringOpacity = std::clamp(iniFloat(ini, "visual", "ring_opacity", cfg.ringOpacity), 0.0f, 1.0f);
    cfg.tipGlow = std::clamp(iniFloat(ini, "visual", "tip_glow", cfg.tipGlow), 0.0f, 1.0f);

    cfg.colorMode = iniStr(ini, "colors", "mode", cfg.colorMode).trimmed().toLower();
    if (cli.hasFullscreen) cfg.fullscreen = cli.fullscreen;
    if (cli.forceEe) cfg.source = "easyeffects_sink.monitor";
    if (!cli.source.isEmpty()) cfg.source = cli.source;
    if (!cli.colorMode.isEmpty()) cfg.colorMode = cli.colorMode;
    if (cli.bars > 0) cfg.raysPerWing = std::clamp(cli.bars, 16, 256);

    if (cfg.colorMode == "monochrome") {
        cfg.bgColor = QColor("#111115");
        cfg.borderColor = QColor("#e6e6eb");
        cfg.ringColor = QColor("#d8d8e0");
        cfg.rayInnerColor = QColor("#b8bac4");
        cfg.rayMidColor = QColor("#e4e6ed");
        cfg.rayOuterColor = QColor("#ffffff");
    } else if (cfg.colorMode != "custom") {
        applyAutoTheme(cfg);
    }

    return cfg;
}
