#pragma once

#include "config.hpp"

#include <QByteArray>
#include <array>
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
    struct LogBand {
        float fCenter = 0.0f;
        float fLo = 0.0f;
        float fHi = 0.0f;
        float tiltGain = 1.0f;
        float highMix = 0.0f;
    };

    void startAudio(const QString &source);
    void rebuildBands(const RadialConfig &cfg);
    void initFft();
    void destroyFft();
    void evalChannel(const std::vector<float> &samples, size_t startIdx, std::vector<float> &outDb);

    static void ctxStateCb(pa_context *c, void *userdata);
    static void streamReadCb(pa_stream *s, size_t nbytes, void *userdata);

    static constexpr int kSampleRate = 48000;
    static constexpr size_t kRingSize = 8192;
    static constexpr int kWinLow = 8192;
    static constexpr int kFftLow = 32768;
    static constexpr int kWinHigh = 2048;
    static constexpr int kFftHigh = 4096;

    pa_threaded_mainloop *m_ml = nullptr;
    pa_context *m_ctx = nullptr;
    pa_stream *m_stream = nullptr;
    bool m_audioActive = false;

    QString m_source = "auto";
    QByteArray m_sourceBytes;

    std::mutex m_ringMutex;
    std::vector<float> m_ringL;
    std::vector<float> m_ringR;
    size_t m_writePos = 0;

    std::vector<float> m_snapL;
    std::vector<float> m_snapR;
    std::vector<float> m_snapMono;

    std::vector<float> m_hannLow;
    std::vector<float> m_hannHigh;
    float *m_inLow = nullptr;
    float *m_inHigh = nullptr;
    fftwf_complex *m_outLow = nullptr;
    fftwf_complex *m_outHigh = nullptr;
    fftwf_plan m_planLow = nullptr;
    fftwf_plan m_planHigh = nullptr;

    std::vector<float> m_powLow;
    std::vector<float> m_powHigh;
    std::vector<float> m_dbLeft;
    std::vector<float> m_dbRight;

    std::vector<LogBand> m_bands;
    int m_bandN = -1;
    int m_bandLowHz = -1;
    int m_bandHighHz = -1;
    float m_bandTilt = -999.0f;
    bool m_bandZonesEnabled = true;
    std::array<FreqZone, 5> m_bandZones{};

    float m_agcPeakDb = -28.0f;
};
