#pragma once

#include "config.hpp"

#include <QByteArray>
#include <fftw3.h>
#include <mutex>
#include <pulse/pulseaudio.h>
#include <vector>

class SpectrumFeed {
public:
    SpectrumFeed();
    ~SpectrumFeed();

    void configure(const RadialConfig &cfg);
    void stop();
    void computeBands(const RadialConfig &cfg, float dtSec, std::vector<float> &outLeft, std::vector<float> &outRight);

private:
    void startAudio(const QString &source);
    void evalMono(std::vector<float> &outDb);

    static void ctxStateCb(pa_context *c, void *userdata);
    static void streamReadCb(pa_stream *s, size_t nbytes, void *userdata);

    static constexpr int kSampleRate = 48000;
    static constexpr int kFftSize = 4096;

    pa_threaded_mainloop *m_ml = nullptr;
    pa_context *m_ctx = nullptr;
    pa_stream *m_stream = nullptr;
    bool m_audioActive = false;

    QString m_source = "auto";
    QByteArray m_sourceBytes;

    std::mutex m_ringMutex;
    std::vector<float> m_ring;
    size_t m_writePos = 0;

    std::vector<float> m_snap;
    std::vector<float> m_hann;
    float *m_in = nullptr;
    fftwf_complex *m_out = nullptr;
    fftwf_plan m_plan = nullptr;
    std::vector<float> m_db;

    float m_agcPeakDb = -28.0f;
};
