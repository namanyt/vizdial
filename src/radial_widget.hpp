#pragma once

#include "config.hpp"
#include "spectrum_feed.hpp"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QTimer>
#include <memory>
#include <vector>

class RadialWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit RadialWidget(const CliOverrides &cli, QWidget *parent = nullptr);
    ~RadialWidget() override;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    void onFrameTick();

private:
    void applyBlurRegion();
    bool loadShaderProgram();
    void updateEnvelopes(float dtSec);
    void toggleFullscreen();

    CliOverrides m_cli;
    RadialConfig m_cfg;
    SpectrumFeed m_feed;
    QFileSystemWatcher m_watcher;

    QTimer m_frameTimer;
    QElapsedTimer m_clock;
    qint64 m_lastNs = 0;

    std::vector<float> m_rawLeft;
    std::vector<float> m_rawRight;
    std::vector<float> m_envLeft;
    std::vector<float> m_envRight;

    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    QString m_shaderError;
};
