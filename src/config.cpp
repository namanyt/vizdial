#include "config.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <map>

namespace {

QColor parseColorAny(const QString &raw, const QColor &fallback) {
    QString s = raw.trimmed();
    s.remove('\'').remove('"');
    if (s.isEmpty()) return fallback;
    if (s.startsWith('#')) {
        QColor c(s);
        return c.isValid() ? c : fallback;
    }
    const QStringList rgb = s.split(',', Qt::SkipEmptyParts);
    if (rgb.size() >= 3) {
        bool okR = false, okG = false, okB = false;
        const int r = rgb[0].trimmed().toInt(&okR);
        const int g = rgb[1].trimmed().toInt(&okG);
        const int b = rgb[2].trimmed().toInt(&okB);
        if (okR && okG && okB) {
            return QColor(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
        }
    }
    return fallback;
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
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';') || line.startsWith("%23")) {
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

bool iniBool(const IniMap &m, const QString &sec, const QString &key, bool def) {
    const QString s = iniStr(m, sec, key, "").trimmed().toLower();
    if (s == "true" || s == "1" || s == "yes") return true;
    if (s == "false" || s == "0" || s == "no") return false;
    return def;
}

QColor readIniKeyColor(const IniMap &m, const QString &sectionName, const QString &key, const QColor &fallback) {
    const QString raw = iniStr(m, sectionName, key, "");
    return raw.isEmpty() ? fallback : parseColorAny(raw, fallback);
}

void applyAutoTheme(RadialConfig &cfg) {
    const IniMap kdeIni = readIniRaw(kdeGlobalsPath());
    const QColor kdeBg = readIniKeyColor(kdeIni, "Colors:Complementary", "BackgroundNormal", QColor("#131318"));
    cfg.bgColor = kdeBg.isValid() ? kdeBg : QColor("#131318");

    QColor accent("#bfc2ff");
    accent = readIniKeyColor(kdeIni, "Colors:Complementary", "DecorationFocus", accent);
    accent = readIniKeyColor(kdeIni, "General", "LastUsedCustomAccentColor", accent);
    accent = readIniKeyColor(kdeIni, "General", "AccentColor", accent);

    QColor g1 = mixColors(accent, QColor("#202446"), 0.55f);
    QColor g2 = accent;
    QColor g3 = mixColors(accent, QColor("#ffffff"), 0.35f);

    const QString mPath = matugenThemePath();
    if (!mPath.isEmpty() && QFile::exists(mPath)) {
        const IniMap mIni = readIniRaw(mPath);
        g1 = readIniKeyColor(mIni, "color", "gradient_color_1", g1);
        g2 = readIniKeyColor(mIni, "color", "gradient_color_2", g2);
        g3 = readIniKeyColor(mIni, "color", "gradient_color_3", g3);
    }

    cfg.borderColor = g2;
    cfg.ringColor = g2;
    cfg.rayInnerColor = mixColors(g1, g2, 0.50f);
    cfg.rayMidColor = g2;
    cfg.rayOuterColor = mixColors(g3, QColor("#ffffff"), 0.55f);
}

} // namespace

QString configDirPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/vizdial";
}

QString configFilePath() {
    return configDirPath() + "/config.ini";
}

QString userShaderFilePath() {
    return configDirPath() + "/radial.frag";
}

QString resolveShaderPath() {
    const QString userShader = userShaderFilePath();
    if (QFile::exists(userShader)) {
        return userShader;
    }
    const QString sysShader = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "vizdial/radial.frag");
    if (!sysShader.isEmpty()) {
        return sysShader;
    }
    return QStringLiteral(":/shaders/radial.frag");
}

QString matugenThemePath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/cava/themes/matugen";
}

QString kdeGlobalsPath() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/kdeglobals";
}

void ensureDefaultConfigExists() {
    const QString dst = configFilePath();
    if (QFile::exists(dst)) {
        return;
    }
    QDir().mkpath(configDirPath());
    QString src = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "vizdial/config.ini");
    if (src.isEmpty()) {
        src = QStringLiteral(":/config/config.ini");
    }
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        return;
    }
    QSaveFile out(dst);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(in.readAll());
        out.commit();
    }
}

RadialConfig loadRadialConfig(const CliOverrides &cli) {
    ensureDefaultConfigExists();

    RadialConfig cfg;
    const IniMap ini = readIniRaw(configFilePath());

    cfg.width = std::clamp(iniInt(ini, "window", "width", cfg.width), 320, 2800);
    cfg.height = std::clamp(iniInt(ini, "window", "height", cfg.height), 320, 2000);
    cfg.cornerRadius = std::clamp(iniFloat(ini, "window", "corner_radius", cfg.cornerRadius), 0.0f, 96.0f);
    cfg.bgOpacity = std::clamp(iniFloat(ini, "window", "bg_opacity", cfg.bgOpacity), 0.0f, 1.0f);
    cfg.borderOpacity = std::clamp(iniFloat(ini, "window", "border_opacity", cfg.borderOpacity), 0.0f, 1.0f);
    cfg.hoverHint = iniBool(ini, "window", "hover_hint", cfg.hoverHint);

    cfg.mirrorMode = iniStr(ini, "audio", "mirror_mode", cfg.mirrorMode).trimmed().toLower();
    if (cfg.mirrorMode != "stereo") cfg.mirrorMode = "exact";
    cfg.source = iniStr(ini, "audio", "source", cfg.source).trimmed();
    if (cfg.source.isEmpty()) cfg.source = "auto";
    cfg.framerate = std::clamp(iniInt(ini, "audio", "framerate", cfg.framerate), 30, 240);
    cfg.lowerCutoffHz = std::clamp(iniInt(ini, "audio", "lower_cutoff_freq", cfg.lowerCutoffHz), 5, 400);
    cfg.higherCutoffHz = std::clamp(iniInt(ini, "audio", "higher_cutoff_freq", cfg.higherCutoffHz), 2000, 20000);
    cfg.tiltDbPerOct = std::clamp(iniFloat(ini, "audio", "tilt_db_per_oct", cfg.tiltDbPerOct), -6.0f, 9.0f);
    cfg.dbRange = std::clamp(iniFloat(ini, "audio", "db_range", cfg.dbRange), 18.0f, 72.0f);
    cfg.attackMs = std::clamp(iniFloat(ini, "audio", "attack_ms", cfg.attackMs), 0.5f, 80.0f);
    cfg.decayMs = std::clamp(iniFloat(ini, "audio", "decay_ms", cfg.decayMs), 2.0f, 250.0f);
    cfg.gain = std::clamp(iniFloat(ini, "audio", "gain", cfg.gain), 0.2f, 5.0f);
    cfg.gamma = std::clamp(iniFloat(ini, "audio", "gamma", cfg.gamma), 0.35f, 2.5f);
    cfg.sensitivity = std::clamp(iniFloat(ini, "audio", "sensitivity", cfg.sensitivity), 0.10f, 2.00f);

    cfg.zonesEnabled = iniBool(ini, "zones", "enabled", cfg.zonesEnabled);
    cfg.zoneBlendRays = std::clamp(iniFloat(ini, "zones", "zone_blend_rays", cfg.zoneBlendRays), 0.0f, 16.0f);
    for (int z = 0; z < 5; ++z) {
        const QString prefix = QString("zone%1_").arg(z + 1);
        FreqZone &fz = cfg.zones[z];
        fz.minHz = std::clamp(iniFloat(ini, "zones", prefix + "min_hz", fz.minHz), 5.0f, 18000.0f);
        fz.maxHz = std::clamp(iniFloat(ini, "zones", prefix + "max_hz", fz.maxHz), fz.minHz + 5.0f, 20000.0f);
        fz.share = std::clamp(iniFloat(ini, "zones", prefix + "share", fz.share), 0.0f, 1000.0f);
        fz.gain = std::clamp(iniFloat(ini, "zones", prefix + "gain", fz.gain), 0.1f, 4.0f);
        fz.influence = std::clamp(iniFloat(ini, "zones", prefix + "influence", fz.influence), 0.0f, 1.0f);
        fz.sensitivity = std::clamp(iniFloat(ini, "zones", prefix + "sensitivity", fz.sensitivity), 0.10f, 2.00f);
    }

    cfg.raysPerWing = std::clamp(iniInt(ini, "visual", "rays_per_wing", cfg.raysPerWing), 16, 256);
    cfg.baseRadius = std::clamp(iniFloat(ini, "visual", "base_radius", cfg.baseRadius), 0.05f, 0.42f);
    cfg.maxLength = std::clamp(iniFloat(ini, "visual", "max_length", cfg.maxLength), 0.04f, 0.32f);
    cfg.rayWidthPx = std::clamp(iniFloat(ini, "visual", "ray_width_px", cfg.rayWidthPx), 0.8f, 14.0f);
    cfg.ringOpacity = std::clamp(iniFloat(ini, "visual", "ring_opacity", cfg.ringOpacity), 0.0f, 1.0f);
    cfg.tipGlow = std::clamp(iniFloat(ini, "visual", "tip_glow", cfg.tipGlow), 0.0f, 1.0f);

    cfg.colorMode = iniStr(ini, "colors", "mode", cfg.colorMode).trimmed().toLower();
    cfg.bgColor = parseColorAny(iniStr(ini, "colors", "bg_color", "#131318"), cfg.bgColor);
    cfg.borderColor = parseColorAny(iniStr(ini, "colors", "border_color", "#bfc2ff"), cfg.borderColor);
    cfg.ringColor = parseColorAny(iniStr(ini, "colors", "ring_color", "#bfc2ff"), cfg.ringColor);
    cfg.rayInnerColor = parseColorAny(iniStr(ini, "colors", "ray_inner_color", "#858ae3"), cfg.rayInnerColor);
    cfg.rayMidColor = parseColorAny(iniStr(ini, "colors", "ray_mid_color", "#bfc2ff"), cfg.rayMidColor);
    cfg.rayOuterColor = parseColorAny(iniStr(ini, "colors", "ray_outer_color", "#f2f2ff"), cfg.rayOuterColor);

    if (cli.hasFullscreen) cfg.fullscreen = cli.fullscreen;
    if (cli.forceEe) cfg.source = "easyeffects_sink.monitor";
    if (!cli.source.isEmpty()) cfg.source = cli.source;
    if (!cli.mirrorMode.isEmpty()) cfg.mirrorMode = cli.mirrorMode;
    if (!cli.colorMode.isEmpty()) cfg.colorMode = cli.colorMode;
    if (cli.bars > 0) cfg.raysPerWing = std::clamp(cli.bars, 16, 256);
    if (cli.lowHz > 0) {
        cfg.lowerCutoffHz = std::clamp(cli.lowHz, 5, 400);
        cfg.zones[0].minHz = static_cast<float>(cfg.lowerCutoffHz);
        if (cfg.zones[0].maxHz <= cfg.zones[0].minHz + 5.0f) {
            cfg.zones[0].maxHz = cfg.zones[0].minHz + 10.0f;
        }
    }
    if (cli.highHz > 0) {
        cfg.higherCutoffHz = std::clamp(cli.highHz, 2000, 20000);
        cfg.zones[4].maxHz = static_cast<float>(cfg.higherCutoffHz);
        if (cfg.zones[4].maxHz <= cfg.zones[4].minHz + 100.0f) {
            cfg.zones[4].minHz = std::max(500.0f, cfg.zones[4].maxHz * 0.5f);
        }
    }
    if (cfg.higherCutoffHz <= cfg.lowerCutoffHz * 2) cfg.higherCutoffHz = cfg.lowerCutoffHz * 2;
    if (cli.decayMs > 0.0f) cfg.decayMs = std::clamp(cli.decayMs, 2.0f, 250.0f);
    if (cli.gain > 0.0f) cfg.gain = std::clamp(cli.gain, 0.2f, 5.0f);
    if (cli.sensitivity > 0.0f) cfg.sensitivity = std::clamp(cli.sensitivity, 0.10f, 2.00f);
    if (cli.width > 0) cfg.width = std::clamp(cli.width, 320, 2800);
    if (cli.height > 0) cfg.height = std::clamp(cli.height, 320, 2000);

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

bool saveRadialConfig(const RadialConfig &cfg) {
    QDir().mkpath(QFileInfo(configFilePath()).absolutePath());
    QSaveFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << "# Vizdial configuration (hot-reloads automatically on save)\n\n";
    out << "[window]\n"
        << "width = " << cfg.width << "\n"
        << "height = " << cfg.height << "\n"
        << "corner_radius = " << QString::number(cfg.cornerRadius, 'f', 1) << "\n"
        << "bg_opacity = " << QString::number(cfg.bgOpacity, 'f', 2) << "\n"
        << "border_opacity = " << QString::number(cfg.borderOpacity, 'f', 2) << "\n"
        << "hover_hint = " << (cfg.hoverHint ? "true" : "false") << "\n\n";

    out << "[audio]\n"
        << "mirror_mode = " << cfg.mirrorMode << "\n"
        << "source = " << cfg.source << "\n"
        << "framerate = " << cfg.framerate << "\n"
        << "lower_cutoff_freq = " << cfg.lowerCutoffHz << "\n"
        << "higher_cutoff_freq = " << cfg.higherCutoffHz << "\n"
        << "tilt_db_per_oct = " << QString::number(cfg.tiltDbPerOct, 'f', 2) << "\n"
        << "db_range = " << QString::number(cfg.dbRange, 'f', 1) << "\n"
        << "attack_ms = " << QString::number(cfg.attackMs, 'f', 1) << "\n"
        << "decay_ms = " << QString::number(cfg.decayMs, 'f', 1) << "\n"
        << "gain = " << QString::number(cfg.gain, 'f', 2) << "\n"
        << "gamma = " << QString::number(cfg.gamma, 'f', 2) << "\n"
        << "sensitivity = " << QString::number(cfg.sensitivity, 'f', 2) << "\n\n";

    out << "[zones]\n"
        << "enabled = " << (cfg.zonesEnabled ? "true" : "false") << "\n"
        << "zone_blend_rays = " << QString::number(cfg.zoneBlendRays, 'f', 1) << "\n";
    static const char *kZoneComments[5] = {
        "# Zone 1: Sub-bass",
        "# Zone 2: Bass",
        "# Zone 3: Low Mids",
        "# Zone 4: High Mids",
        "# Zone 5: Highs / Presence",
    };
    for (int z = 0; z < 5; ++z) {
        const FreqZone &fz = cfg.zones[z];
        const QString p = QString("zone%1_").arg(z + 1);
        out << kZoneComments[z] << "\n"
            << p << "min_hz = " << qRound(fz.minHz) << "\n"
            << p << "max_hz = " << qRound(fz.maxHz) << "\n"
            << p << "share = " << QString::number(fz.share, 'f', 1) << "\n"
            << p << "gain = " << QString::number(fz.gain, 'f', 2) << "\n"
            << p << "influence = " << QString::number(fz.influence, 'f', 2) << "\n"
            << p << "sensitivity = " << QString::number(fz.sensitivity, 'f', 2) << "\n";
    }
    out << "\n";

    out << "[visual]\n"
        << "rays_per_wing = " << cfg.raysPerWing << "\n"
        << "base_radius = " << QString::number(cfg.baseRadius, 'f', 3) << "\n"
        << "max_length = " << QString::number(cfg.maxLength, 'f', 3) << "\n"
        << "ray_width_px = " << QString::number(cfg.rayWidthPx, 'f', 1) << "\n"
        << "ring_opacity = " << QString::number(cfg.ringOpacity, 'f', 2) << "\n"
        << "tip_glow = " << QString::number(cfg.tipGlow, 'f', 2) << "\n\n";

    const IniMap rawIni = readIniRaw(configFilePath());
    out << "[colors]\n"
        << "mode = " << cfg.colorMode << "\n"
        << "bg_color = " << iniStr(rawIni, "colors", "bg_color", "#131318") << "\n"
        << "border_color = " << iniStr(rawIni, "colors", "border_color", "#bfc2ff") << "\n"
        << "ring_color = " << iniStr(rawIni, "colors", "ring_color", "#bfc2ff") << "\n"
        << "ray_inner_color = " << iniStr(rawIni, "colors", "ray_inner_color", "#858ae3") << "\n"
        << "ray_mid_color = " << iniStr(rawIni, "colors", "ray_mid_color", "#bfc2ff") << "\n"
        << "ray_outer_color = " << iniStr(rawIni, "colors", "ray_outer_color", "#f2f2ff") << "\n";

    out.flush();
    return file.commit();
}

std::array<int, 5> computeZoneRayCounts(const RadialConfig &cfg) {
    std::array<int, 5> counts{0, 0, 0, 0, 0};
    double totalShare = 0.0;
    for (int z = 0; z < 5; ++z) totalShare += std::max(0.0f, cfg.zones[z].share);
    if (totalShare <= 1e-6) totalShare = 5.0;

    std::array<double, 5> rems{};
    int assigned = 0;
    for (int z = 0; z < 5; ++z) {
        const double w = (totalShare > 0.0) ? std::max(0.0f, cfg.zones[z].share) : 1.0;
        const double exact = static_cast<double>(cfg.raysPerWing) * (w / totalShare);
        int fl = static_cast<int>(std::floor(exact));
        if (w > 0.0 && fl == 0 && cfg.raysPerWing >= 5) fl = 1;
        counts[z] = fl;
        rems[z] = exact - fl;
        assigned += fl;
    }
    while (assigned < cfg.raysPerWing) {
        int bestZ = 0;
        double bestR = -1e9;
        for (int z = 0; z < 5; ++z) {
            if (cfg.zones[z].share > 0.0f && rems[z] > bestR) { bestR = rems[z]; bestZ = z; }
        }
        counts[bestZ]++;
        rems[bestZ] -= 1.0;
        assigned++;
    }
    while (assigned > cfg.raysPerWing) {
        int bestZ = -1;
        double lowR = 1e9;
        for (int z = 0; z < 5; ++z) {
            if (counts[z] > 1 && rems[z] < lowR) { lowR = rems[z]; bestZ = z; }
        }
        if (bestZ < 0) break;
        counts[bestZ]--;
        rems[bestZ] += 1.0;
        assigned--;
    }
    return counts;
}

QString formatZonesSummary(const RadialConfig &cfg) {
    if (!cfg.zonesEnabled) {
        return QString("Single log span %1–%2 Hz (%3 rays/wing)").arg(cfg.lowerCutoffHz).arg(cfg.higherCutoffHz).arg(cfg.raysPerWing);
    }
    const auto counts = computeZoneRayCounts(cfg);
    QStringList parts;
    for (int z = 0; z < 5; ++z) {
        parts << QString("Z%1 %2-%3Hz:%4r").arg(z + 1).arg(qRound(cfg.zones[z].minHz)).arg(qRound(cfg.zones[z].maxHz)).arg(counts[z]);
    }
    return parts.join(" · ");
}
