#include "radial_widget.hpp"

#include <KWindowEffects>
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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

    const float alphaAtk = 1.0f - std::exp(-dtSec / std::max(0.0004f, m_cfg.attackMs * 0.001f));
    const float alphaDec = 1.0f - std::exp(-dtSec / std::max(0.001f, m_cfg.decayMs * 0.001f));
    const float tSec = static_cast<float>(m_clock.elapsed()) * 0.001f;

    for (int i = 0; i < n; ++i) {
        float raw = m_rawLeft[i];
        if (m_cli.demo) {
            const float u = static_cast<float>(i) / static_cast<float>(std::max(1, n - 1));
            raw = std::clamp(0.45f + 0.4f * std::sin(tSec * 4.5f + u * 8.0f), 0.0f, 1.0f);
        }
        const float tgt = std::clamp(std::pow(std::clamp(raw * m_cfg.gain, 0.0f, 1.0f), m_cfg.gamma), 0.0f, 1.0f);
        m_envLeft[i] += (tgt - m_envLeft[i]) * ((tgt > m_envLeft[i]) ? alphaAtk : alphaDec);
        m_envRight[i] = m_envLeft[i];
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
}

void RadialWidget::applyBlurRegion() {
    if (!windowHandle()) return;
    QRegion reg;
    if (isFullScreen() || m_cfg.cornerRadius < 1.0f) {
        reg = QRegion(rect());
    } else {
        QPainterPath path;
        const qreal r = static_cast<qreal>(m_cfg.cornerRadius);
        path.addRoundedRect(QRectF(1.0, 1.0, std::max(2.0, width() - 2.0), std::max(2.0, height() - 2.0)), r, r);
        for (const QPolygonF &poly : path.toSubpathPolygons()) {
            reg += QRegion(poly.toPolygon());
        }
    }
    KWindowEffects::enableBlurBehind(windowHandle(), true, reg);
}

void RadialWidget::showEvent(QShowEvent *event) {
    QOpenGLWidget::showEvent(event);
    applyBlurRegion();
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

void RadialWidget::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Q:
        qApp->quit();
        return;
    case Qt::Key_F:
    case Qt::Key_F11:
        toggleFullscreen();
        return;
    default:
        QOpenGLWidget::keyPressEvent(event);
    }
}

void RadialWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !isFullScreen() && windowHandle()) {
        windowHandle()->startSystemMove();
    }
}

void RadialWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) toggleFullscreen();
}
