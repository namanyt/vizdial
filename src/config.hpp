#pragma once

#include <QColor>
#include <QString>
#include <QVector3D>

struct RadialConfig {
    int width = 840;
    int height = 840;
    bool fullscreen = false;
    float cornerRadius = 28.0f;
    float bgOpacity = 0.45f;
    float borderOpacity = 0.16f;

    QString mirrorMode = "exact";
    QString source = "auto";
    int framerate = 120;
    int lowerCutoffHz = 20;
    int higherCutoffHz = 9500;
    float tiltDbPerOct = 1.5f;
    float dbRange = 44.0f;
    float attackMs = 2.0f;
    float decayMs = 20.0f;
    float gain = 1.05f;
    float gamma = 0.90f;

    int raysPerWing = 128;
    float baseRadius = 0.205f;
    float maxLength = 0.220f;
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
    int bars = -1;
    int lowHz = -1;
    int highHz = -1;
    float decayMs = -1.0f;
    float gain = -1.0f;
    QString mirrorMode;
    QString source;
    QString colorMode;
};

QString configFilePath();
QString shaderFilePath();
QString matugenThemePath();
QString kdeGlobalsPath();

RadialConfig loadRadialConfig(const CliOverrides &cli = {});
