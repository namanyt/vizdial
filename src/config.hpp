#pragma once

#include <QColor>
#include <QString>
#include <QVector3D>
#include <array>

struct FreqZone {
    float minHz = 20.0f;
    float maxHz = 100.0f;
    float share = 20.0f;
    float gain = 1.0f;
    float influence = 0.55f;
};

struct RadialConfig {
    int width = 840;
    int height = 840;
    bool fullscreen = false;
    float cornerRadius = 28.0f;
    float bgOpacity = 0.42f;
    float borderOpacity = 0.16f;
    bool hoverHint = true;

    QString mirrorMode = "exact";
    QString source = "auto";
    int framerate = 120;
    int lowerCutoffHz = 20;
    int higherCutoffHz = 20000;
    float tiltDbPerOct = 1.5f;
    float dbRange = 44.0f;
    float attackMs = 2.0f;
    float decayMs = 20.0f;
    float gain = 1.05f;
    float gamma = 0.90f;

    bool zonesEnabled = true;
    float zoneBlendRays = 10.0f;
    std::array<FreqZone, 5> zones{{
        {20.0f, 100.0f, 24.0f, 0.90f, 1.00f},
        {100.0f, 250.0f, 24.0f, 0.45f, 0.55f},
        {250.0f, 1000.0f, 22.0f, 0.25f, 0.55f},
        {1000.0f, 6000.0f, 20.0f, 0.15f, 0.55f},
        {6000.0f, 20000.0f, 16.0f, 0.10f, 0.55f},
    }};

    int raysPerWing = 224;
    float baseRadius = 0.205f;
    float maxLength = 0.250f;
    float rayWidthPx = 2.2f;
    float ringOpacity = 0.22f;
    float tipGlow = 0.75f;

    QString colorMode = "auto";
    QColor bgColor = QColor("#131318");
    QColor borderColor = QColor("#bfc2ff");
    QColor ringColor = QColor("#bfc2ff");
    QColor rayInnerColor = QColor("#858ae3");
    QColor rayMidColor = QColor("#bfc2ff");
    QColor rayOuterColor = QColor("#f2f2ff");

    static QVector3D toVec3(const QColor &c) {
        return QVector3D(c.redF(), c.greenF(), c.blueF());
    }
};

struct CliOverrides {
    bool fullscreen = false;
    bool hasFullscreen = false;
    bool forceEe = false;
    bool demo = false;
    bool printZones = false;
    int bars = -1;
    int lowHz = -1;
    int highHz = -1;
    float decayMs = -1.0f;
    float gain = -1.0f;
    int width = -1;
    int height = -1;
    QString mirrorMode;
    QString source;
    QString colorMode;
};

QString configFilePath();
QString shaderFilePath();
QString matugenThemePath();
QString kdeGlobalsPath();

RadialConfig loadRadialConfig(const CliOverrides &cli = {});
bool saveRadialConfig(const RadialConfig &cfg);
std::array<int, 5> computeZoneRayCounts(const RadialConfig &cfg);
QString formatZonesSummary(const RadialConfig &cfg);
