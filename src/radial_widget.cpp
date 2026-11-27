#include "radial_widget.hpp"

#include <KWindowEffects>
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace {

constexpr const char *kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

constexpr float kQuadVerts[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
    -1.0f,  1.0f,
     1.0f, -1.0f,
     1.0f,  1.0f,
};

} // namespace

RadialWidget::RadialWidget(const CliOverrides &cli, QWidget *parent)
    : QOpenGLWidget(parent),
      m_cli(cli) {
    m_cfg = loadRadialConfig(m_cli);

    setWindowTitle("Vizdial");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setProperty("_kde_no_window_blur", true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    resize(m_cfg.width, m_cfg.height);

    m_rawLeft.assign(m_cfg.raysPerWing, 0.0f);
    m_rawRight.assign(m_cfg.raysPerWing, 0.0f);
    m_envLeft.assign(m_cfg.raysPerWing, 0.0f);
    m_envRight.assign(m_cfg.raysPerWing, 0.0f);

    if (!m_cli.demo) {
        m_feed.configure(m_cfg);
    }
    m_reloadDebounce.setSingleShot(true);
    m_reloadDebounce.setInterval(80);
    connect(&m_reloadDebounce, &QTimer::timeout, this, &RadialWidget::reloadAll);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) { syncWatcherPaths(); m_reloadDebounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) { syncWatcherPaths(); m_reloadDebounce.start(); });
    syncWatcherPaths();
    m_toastTimer.setSingleShot(true);
    m_toastTimer.setInterval(1600);
    connect(&m_toastTimer, &QTimer::timeout, this, [this]() { m_toastText.clear(); update(); });

    m_clock.start();
    m_lastNs = m_clock.nsecsElapsed();

    const int intervalMs = std::max(4, static_cast<int>(std::lround(1000.0 / std::max(30, m_cfg.framerate))));
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &RadialWidget::onFrameTick);
    m_frameTimer.start(intervalMs);
}

RadialWidget::~RadialWidget() {
    m_frameTimer.stop();
    m_feed.stop();
    makeCurrent();
    m_program.reset();
    m_vbo.destroy();
    m_vao.destroy();
    doneCurrent();
}

void RadialWidget::initializeGL() {
    initializeOpenGLFunctions();
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(kQuadVerts, sizeof(kQuadVerts));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_vbo.release();
    m_vao.release();

    loadShaderProgram();
    if (m_cfg.fullscreen) showFullScreen();
}

void RadialWidget::resizeGL(int, int) {}

bool RadialWidget::loadShaderProgram() {
    QFile f(shaderFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_shaderError = "Cannot open shader: " + shaderFilePath();
        return false;
    }
    auto next = std::make_unique<QOpenGLShaderProgram>();
    if (!next->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !next->addShaderFromSourceCode(QOpenGLShader::Fragment, f.readAll()) ||
        !next->link()) {
        m_shaderError = next->log();
        return false;
    }
    m_program = std::move(next);
    m_shaderError.clear();
    return true;
}

void RadialWidget::updateEnvelopes(float dtSec) {
    const int n = m_cfg.raysPerWing;
    if (!m_cli.demo) {
        m_feed.computeBands(m_cfg, dtSec, m_rawLeft, m_rawRight);
    }

    const float alphaDec = 1.0f - std::exp(-dtSec / std::max(0.001f, m_cfg.decayMs * 0.001f));
    const float tSec = static_cast<float>(m_clock.elapsed()) * 0.001f;

    for (int i = 0; i < n; ++i) {
        float rawL = m_rawLeft[i];
        float rawR = m_rawRight[i];
        if (m_cli.demo) {
            const float u = static_cast<float>(i) / static_cast<float>(std::max(1, n - 1));
            const float kick = std::pow(0.5f + 0.5f * std::sin(tSec * 5.2f), 4.0f);
            const float raw = std::clamp(
                std::exp(-std::pow((u - 0.14f) / 0.14f, 2.0f)) * (0.48f + 0.50f * kick) +
                std::exp(-std::pow((u - 0.50f) / 0.16f, 2.0f)) * (0.38f + 0.28f * std::sin(tSec * 7.5f + u * 11.0f)) +
                std::exp(-std::pow((u - 0.84f) / 0.14f, 2.0f)) * (0.30f + 0.25f * std::sin(tSec * 13.0f + u * 24.0f)),
                0.0f, 1.0f);
            rawL = raw;
            rawR = raw;
        }
        const float tgtL = std::clamp(std::pow(std::clamp(rawL * m_cfg.gain, 0.0f, 1.0f), m_cfg.gamma), 0.0f, 1.0f);
        const float tgtR = std::clamp(std::pow(std::clamp(rawR * m_cfg.gain, 0.0f, 1.0f), m_cfg.gamma), 0.0f, 1.0f);
        const float sens = m_cli.demo ? std::clamp(m_cfg.sensitivity, 0.10f, 2.00f) : m_feed.raySensitivity(m_cfg, i);
        const float zgScale = m_cli.demo ? 1.0f : std::max(0.10f, m_feed.rayGainScale(m_cfg, i));
        const float calm = std::max(0.0f, 1.0f - sens);
        const float effAtkMs = (sens < 1.0f) ? (m_cfg.attackMs * (1.0f + 2.5f * calm)) : (m_cfg.attackMs / sens);
        const float alphaAtk = 1.0f - std::exp(-dtSec / std::max(0.0004f, effAtkMs * 0.001f));
        auto stepCh = [&](float tgt, float &env) {
            float rate = (tgt > env) ? alphaAtk : alphaDec;
            if (calm > 0.001f) {
                const float dead = 0.18f * calm;
                const float relD = std::abs(tgt - env) / zgScale;
                if (relD < dead) rate *= 0.12f + 0.88f * std::pow(std::clamp(relD / std::max(0.01f, dead), 0.0f, 1.0f), 1.5f);
            }
            env += (tgt - env) * rate;
            if (env < 0.0005f) env = 0.0f;
        };
        stepCh(tgtL, m_envLeft[i]);
        stepCh(tgtR, m_envRight[i]);
    }
}

void RadialWidget::onFrameTick() {
    const qint64 nowNs = m_clock.nsecsElapsed();
    const float dt = std::clamp(static_cast<float>(nowNs - m_lastNs) * 1e-9f, 0.001f, 0.05f);
    m_lastNs = nowNs;
    updateEnvelopes(dt);
    update();
}

void RadialWidget::paintGL() {
    const float dpr = static_cast<float>(devicePixelRatioF());
    const int pw = std::max(1, static_cast<int>(std::lround(width() * dpr)));
    const int ph = std::max(1, static_cast<int>(std::lround(height() * dpr)));

    glViewport(0, 0, pw, ph);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_program) {
        m_program->bind();
        m_program->setUniformValue("u_resolution", QVector2D(pw, ph));
        m_program->setUniformValue("u_dpr", dpr);
        const int bars = std::clamp(m_cfg.raysPerWing, 8, 256);
        m_program->setUniformValue("u_bars", bars);
        m_program->setUniformValueArray("u_bars_left", m_envLeft.data(), bars, 1);
        m_program->setUniformValueArray("u_bars_right", m_envRight.data(), bars, 1);
        m_program->setUniformValue("u_base_radius", m_cfg.baseRadius);
        m_program->setUniformValue("u_max_len", m_cfg.maxLength);
        m_program->setUniformValue("u_ray_width_px", m_cfg.rayWidthPx);
        m_program->setUniformValue("u_ring_opacity", m_cfg.ringOpacity);
        m_program->setUniformValue("u_tip_glow", m_cfg.tipGlow);
        m_program->setUniformValue("u_corner_radius", isFullScreen() ? 0.0f : m_cfg.cornerRadius);
        m_program->setUniformValue("u_bg_opacity", m_cfg.bgOpacity);
        m_program->setUniformValue("u_border_opacity", isFullScreen() ? 0.0f : m_cfg.borderOpacity);
        m_program->setUniformValue("u_bg_color", RadialConfig::toVec3(m_cfg.bgColor));
        m_program->setUniformValue("u_border_color", RadialConfig::toVec3(m_cfg.borderColor));
        m_program->setUniformValue("u_ring_color", RadialConfig::toVec3(m_cfg.ringColor));
        m_program->setUniformValue("u_ray_inner", RadialConfig::toVec3(m_cfg.rayInnerColor));
        m_program->setUniformValue("u_ray_mid", RadialConfig::toVec3(m_cfg.rayMidColor));
        m_program->setUniformValue("u_ray_outer", RadialConfig::toVec3(m_cfg.rayOuterColor));

        m_vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_vao.release();
        m_program->release();
    }
    if (!m_toastText.isEmpty() || !m_shaderError.isEmpty() || (m_hovered && m_cfg.hoverHint)) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QString zoneTag = (m_selectedZone >= 0) ? QString("Z%1 · ").arg(m_selectedZone + 1) : QString();
        const QString text = !m_shaderError.isEmpty()
            ? m_shaderError.section('\n', 0, 1)
            : (!m_toastText.isEmpty()
                ? m_toastText
                : QString("%1%2r · h%3% · %4ms | ↑↓ height  1-5 zone  Shift+↑↓ gain  ,/. vol  S save")
                      .arg(zoneTag).arg(m_cfg.raysPerWing * 2).arg(m_cfg.maxLength * 100.0f, 0, 'f', 1).arg(m_cfg.decayMs, 0, 'f', 0));
        QFont font("monospace", 8);
        painter.setFont(font);
        const QFontMetrics fm(font);
        const int pillW = std::min(width() - 32, fm.horizontalAdvance(text) + 28);
        const QRectF pill((width() - pillW) * 0.5, height() - 44.0, pillW, 26);
        painter.setPen(QPen(QColor(224, 224, 255, 38), 1.0));
        painter.setBrush(QColor(15, 16, 22, 165));
        painter.drawRoundedRect(pill, 13.0, 13.0);
        painter.setPen(!m_shaderError.isEmpty() ? QColor("#ffb4ab") : QColor(228, 225, 233, 225));
        painter.drawText(pill, Qt::AlignCenter, text);
        painter.end();
    }
}

void RadialWidget::applyBlurRegion() {
    if (!windowHandle()) return;
    QRegion reg;
    const int w = width();
    const int h = height();
    if (isFullScreen() || m_cfg.cornerRadius < 1.0f || w <= 4 || h <= 4) {
        reg = QRegion(rect());
    } else {
        constexpr int inset = 1;
        const int innerW = w - 2 * inset;
        const int innerH = h - 2 * inset;
        const qreal maxR = 0.5 * static_cast<qreal>(std::min(innerW, innerH));
        const qreal r = std::clamp(static_cast<qreal>(m_cfg.cornerRadius), 1.0, maxR);
        const int ri = std::clamp(static_cast<int>(std::ceil(r)), 1, std::min(innerW, innerH) / 2);
        for (int row = 0; row < ri; ++row) {
            const qreal dy = std::max(0.0, r - static_cast<qreal>(row));
            const qreal dx = std::sqrt(std::max(0.0, r * r - dy * dy));
            const int xInset = inset + static_cast<int>(std::ceil(std::max(0.0, r - dx)));
            const int spanW = w - 2 * xInset;
            if (spanW <= 0) continue;
            const int yTop = inset + row;
            const int yBot = h - 1 - (inset + row);
            reg += QRect(xInset, yTop, spanW, 1);
            if (yBot > yTop) reg += QRect(xInset, yBot, spanW, 1);
        }
        const int midH = h - 2 * (inset + ri);
        if (midH > 0) reg += QRect(inset, inset + ri, innerW, midH);
    }
    KWindowEffects::enableBlurBehind(windowHandle(), true, reg);
    KWindowEffects::enableBackgroundContrast(windowHandle(), true, 1.06, 1.0, 1.12, reg);
}

void RadialWidget::showEvent(QShowEvent *event) {
    QOpenGLWidget::showEvent(event);
    applyBlurRegion();
    QTimer::singleShot(0, this, &RadialWidget::applyBlurRegion);
}

void RadialWidget::resizeEvent(QResizeEvent *event) {
    QOpenGLWidget::resizeEvent(event);
    applyBlurRegion();
}

void RadialWidget::toggleFullscreen() {
    if (isFullScreen()) {
        showNormal();
        resize(m_cfg.width, m_cfg.height);
    } else {
        showFullScreen();
    }
    applyBlurRegion();
}

void RadialWidget::syncWatcherPaths() {
    for (const QString &d : {QFileInfo(configFilePath()).absolutePath(), QFileInfo(matugenThemePath()).absolutePath()}) {
        if (QFile::exists(d) && !m_watcher.directories().contains(d)) m_watcher.addPath(d);
    }
    for (const QString &p : {configFilePath(), shaderFilePath(), matugenThemePath()}) {
        if (QFile::exists(p) && !m_watcher.files().contains(p)) m_watcher.addPath(p);
    }
}

void RadialWidget::reloadAll() {
    syncWatcherPaths();
    m_cfg = loadRadialConfig(m_cli);
    if (!isFullScreen()) resize(m_cfg.width, m_cfg.height);
    applyBlurRegion();
    if (!m_cli.demo) m_feed.configure(m_cfg);
    makeCurrent();
    loadShaderProgram();
    doneCurrent();
    update();
}

void RadialWidget::showToast(const QString &text) { m_toastText = text; m_toastTimer.start(); update(); }
void RadialWidget::saveConfigToDisk() { showToast(saveRadialConfig(m_cfg) ? "Saved config" : "Failed saving config"); }
void RadialWidget::adjustHeight(float delta) {
    m_cfg.maxLength = std::clamp(m_cfg.maxLength + delta, 0.04f, 0.30f);
    showToast(QString("Bar height: %1%").arg(m_cfg.maxLength * 100.0f, 0, 'f', 1));
}
void RadialWidget::adjustZoneOrGlobalGain(float delta) {
    if (m_selectedZone >= 0 && m_selectedZone < 5) {
        FreqZone &fz = m_cfg.zones[m_selectedZone];
        fz.gain = std::clamp(fz.gain + delta, 0.10f, 4.00f);
        showToast(QString("Z%1 gain: %2 | vol-track: %3").arg(m_selectedZone + 1).arg(fz.gain, 0, 'f', 2).arg(fz.influence, 0, 'f', 2));
    } else {
        m_cfg.gain = std::clamp(m_cfg.gain + delta, 0.20f, 5.00f);
        showToast(QString("Global gain: %1x").arg(m_cfg.gain, 0, 'f', 2));
    }
}
void RadialWidget::adjustZoneOrGlobalSensitivity(float delta) {
    if (m_selectedZone >= 0 && m_selectedZone < 5) {
        FreqZone &fz = m_cfg.zones[m_selectedZone];
        fz.sensitivity = std::clamp(fz.sensitivity + delta, 0.10f, 2.00f);
        showToast(QString("Z%1 sensitivity: %2").arg(m_selectedZone + 1).arg(fz.sensitivity, 0, 'f', 2));
    } else {
        m_cfg.sensitivity = std::clamp(m_cfg.sensitivity + delta, 0.10f, 2.00f);
        showToast(QString("Global sensitivity: %1").arg(m_cfg.sensitivity, 0, 'f', 2));
    }
}

void RadialWidget::adjustZoneInfluence(float delta) {
    if (m_selectedZone >= 0 && m_selectedZone < 5) {
        FreqZone &fz = m_cfg.zones[m_selectedZone];
        fz.influence = std::clamp(fz.influence + delta, 0.0f, 1.0f);
        showToast(QString("Z%1 vol-track: %2").arg(m_selectedZone + 1).arg(fz.influence, 0, 'f', 2));
    } else {
        float avg = 0.0f;
        for (int z = 0; z < 5; ++z) {
            m_cfg.zones[z].influence = std::clamp(m_cfg.zones[z].influence + delta, 0.0f, 1.0f);
            avg += m_cfg.zones[z].influence;
        }
        showToast(QString("All zones vol-track: %1").arg(avg / 5.0f, 0, 'f', 2));
    }
}

void RadialWidget::keyPressEvent(QKeyEvent *event) {
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    switch (event->key()) {
    case Qt::Key_Escape: case Qt::Key_Q: qApp->quit(); return;
    case Qt::Key_F: case Qt::Key_F11: toggleFullscreen(); return;
    case Qt::Key_Up: if (shift) adjustZoneOrGlobalGain(0.05f); else adjustHeight(0.015f); return;
    case Qt::Key_Down: if (shift) adjustZoneOrGlobalGain(-0.05f); else adjustHeight(-0.015f); return;
    case Qt::Key_Plus: case Qt::Key_Equal: adjustHeight(0.015f); return;
    case Qt::Key_Minus: adjustHeight(-0.015f); return;
    case Qt::Key_1: case Qt::Key_2: case Qt::Key_3: case Qt::Key_4: case Qt::Key_5:
        m_selectedZone = event->key() - Qt::Key_1;
        showToast(QString("Z%1 (%2-%3Hz) gain %4").arg(m_selectedZone + 1).arg(qRound(m_cfg.zones[m_selectedZone].minHz)).arg(qRound(m_cfg.zones[m_selectedZone].maxHz)).arg(m_cfg.zones[m_selectedZone].gain, 0, 'f', 2));
        return;
    case Qt::Key_0: m_selectedZone = -1; showToast("All zones deselected"); return;
    case Qt::Key_Comma: case Qt::Key_Less: adjustZoneInfluence(-0.10f); return;
    case Qt::Key_Period: case Qt::Key_Greater: adjustZoneInfluence(0.10f); return;
    case Qt::Key_BracketRight: m_cfg.decayMs = std::clamp(m_cfg.decayMs + 1.0f, 2.0f, 100.0f); showToast(QString("Decay: %1 ms").arg(m_cfg.decayMs, 0, 'f', 0)); return;
    case Qt::Key_BracketLeft: m_cfg.decayMs = std::clamp(m_cfg.decayMs - 1.0f, 2.0f, 100.0f); showToast(QString("Decay: %1 ms").arg(m_cfg.decayMs, 0, 'f', 0)); return;
    case Qt::Key_Right: if (shift) { adjustZoneOrGlobalSensitivity(0.05f); return; } m_cfg.raysPerWing = std::clamp(m_cfg.raysPerWing + 8, 24, 256); if (!m_cli.demo) m_feed.configure(m_cfg); showToast(QString("Rays: %1").arg(m_cfg.raysPerWing * 2)); return;
    case Qt::Key_Left: if (shift) { adjustZoneOrGlobalSensitivity(-0.05f); return; } m_cfg.raysPerWing = std::clamp(m_cfg.raysPerWing - 8, 24, 256); if (!m_cli.demo) m_feed.configure(m_cfg); showToast(QString("Rays: %1").arg(m_cfg.raysPerWing * 2)); return;
    case Qt::Key_M: m_cfg.mirrorMode = (m_cfg.mirrorMode == "exact") ? "stereo" : "exact"; if (!m_cli.demo) m_feed.configure(m_cfg); showToast(QString("Wing mirror: %1").arg(m_cfg.mirrorMode)); return;
    case Qt::Key_C: m_cli.colorMode = (m_cfg.colorMode == "monochrome") ? "auto" : "monochrome"; reloadAll(); return;
    case Qt::Key_Z: showToast(formatZonesSummary(m_cfg)); return;
    case Qt::Key_S: saveConfigToDisk(); return;
    case Qt::Key_R: m_cli.colorMode.clear(); reloadAll(); showToast("Reloaded config & shader"); return;
    default: QOpenGLWidget::keyPressEvent(event);
    }
}

void RadialWidget::wheelEvent(QWheelEvent *event) { if (event->angleDelta().y() != 0) adjustHeight(event->angleDelta().y() > 0 ? 0.010f : -0.010f); }
void RadialWidget::enterEvent(QEnterEvent *event) { QOpenGLWidget::enterEvent(event); m_hovered = true; update(); }
void RadialWidget::leaveEvent(QEvent *event) { QOpenGLWidget::leaveEvent(event); m_hovered = false; update(); }

void RadialWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !isFullScreen() && windowHandle()) {
        windowHandle()->startSystemMove();
    }
}

void RadialWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) toggleFullscreen();
}
