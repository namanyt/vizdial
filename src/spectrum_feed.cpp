#include "spectrum_feed.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

SpectrumFeed::SpectrumFeed()
    : m_ring(kFftSize, 0.0f),
      m_snap(kFftSize, 0.0f),
      m_hann(kFftSize, 0.0f) {
    double sum = 0.0;
    for (int i = 0; i < kFftSize; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (kFftSize - 1)));
        m_hann[i] = static_cast<float>(w);
        sum += w;
    }
    const float norm = static_cast<float>(2.0 / std::max(1.0, sum));
    for (float &v : m_hann) v *= norm;

    m_in = fftwf_alloc_real(kFftSize);
    m_out = fftwf_alloc_complex(kFftSize / 2 + 1);
    m_plan = fftwf_plan_dft_r2c_1d(kFftSize, m_in, m_out, FFTW_ESTIMATE);
}

SpectrumFeed::~SpectrumFeed() {
    stop();
    if (m_plan) fftwf_destroy_plan(m_plan);
    if (m_in) fftwf_free(m_in);
    if (m_out) fftwf_free(m_out);
}

void SpectrumFeed::configure(const RadialConfig &cfg) {
    if (!m_audioActive || m_source != cfg.source) {
        stop();
        startAudio(cfg.source);
    }
}

void SpectrumFeed::startAudio(const QString &source) {
    m_source = source;
    m_sourceBytes = m_source.toUtf8();

    m_ml = pa_threaded_mainloop_new();
    if (!m_ml || pa_threaded_mainloop_start(m_ml) < 0) {
        return;
    }
    pa_threaded_mainloop_lock(m_ml);
    m_ctx = pa_context_new(pa_threaded_mainloop_get_api(m_ml), "vizdial");
    if (m_ctx) {
        pa_context_set_state_callback(m_ctx, &SpectrumFeed::ctxStateCb, this);
        pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
        m_audioActive = true;
    }
    pa_threaded_mainloop_unlock(m_ml);
}

void SpectrumFeed::stop() {
    m_audioActive = false;
    if (!m_ml) return;
    pa_threaded_mainloop_lock(m_ml);
    if (m_stream) {
        pa_stream_set_read_callback(m_stream, nullptr, nullptr);
        pa_stream_disconnect(m_stream);
        pa_stream_unref(m_stream);
        m_stream = nullptr;
    }
    if (m_ctx) {
        pa_context_set_state_callback(m_ctx, nullptr, nullptr);
        pa_context_disconnect(m_ctx);
        pa_context_unref(m_ctx);
        m_ctx = nullptr;
    }
    pa_threaded_mainloop_unlock(m_ml);
    pa_threaded_mainloop_stop(m_ml);
    pa_threaded_mainloop_free(m_ml);
    m_ml = nullptr;
}

void SpectrumFeed::ctxStateCb(pa_context *c, void *userdata) {
    auto *self = static_cast<SpectrumFeed *>(userdata);
    if (pa_context_get_state(c) != PA_CONTEXT_READY) return;

    pa_sample_spec ss{PA_SAMPLE_FLOAT32LE, static_cast<uint32_t>(kSampleRate), 2};
    pa_buffer_attr attr{};
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.tlength = static_cast<uint32_t>(-1);
    attr.prebuf = static_cast<uint32_t>(-1);
    attr.minreq = static_cast<uint32_t>(-1);
    attr.fragsize = 384 * 2 * sizeof(float);

    self->m_stream = pa_stream_new(c, "spectrum-monitor", &ss, nullptr);
    if (!self->m_stream) return;
    pa_stream_set_read_callback(self->m_stream, &SpectrumFeed::streamReadCb, self);
    const char *dev = (self->m_source == "auto") ? "@DEFAULT_MONITOR@" : self->m_sourceBytes.constData();
    pa_stream_connect_record(self->m_stream, dev, &attr, PA_STREAM_ADJUST_LATENCY);
}

void SpectrumFeed::streamReadCb(pa_stream *s, size_t, void *userdata) {
    auto *self = static_cast<SpectrumFeed *>(userdata);
    while (pa_stream_readable_size(s) > 0) {
        const void *data = nullptr;
        size_t bytes = 0;
        if (pa_stream_peek(s, &data, &bytes) < 0 || bytes == 0) break;
        const size_t frames = bytes / (2 * sizeof(float));
        if (frames > 0) {
            std::lock_guard<std::mutex> lk(self->m_ringMutex);
            const auto *src = static_cast<const float *>(data);
            for (size_t i = 0; i < frames; ++i) {
                const float mono = src ? 0.5f * (src[i * 2] + src[i * 2 + 1]) : 0.0f;
                self->m_ring[self->m_writePos] = mono;
                self->m_writePos = (self->m_writePos + 1) % kFftSize;
            }
        }
        pa_stream_drop(s);
    }
}

void SpectrumFeed::evalMono(std::vector<float> &outDb) {
    size_t wp = 0;
    {
        std::lock_guard<std::mutex> lk(m_ringMutex);
        wp = m_writePos;
        std::memcpy(m_snap.data(), m_ring.data(), sizeof(float) * kFftSize);
    }

    float peakAbs = 0.0f;
    for (int i = 0; i < kFftSize; ++i) {
        const float s = m_snap[(wp + i) % kFftSize];
        peakAbs = std::max(peakAbs, std::abs(s));
        m_in[i] = s * m_hann[i];
    }
    if (peakAbs < 2e-5f) {
        std::fill(outDb.begin(), outDb.end(), -120.0f);
        return;
    }

    fftwf_execute(m_plan);
    const float df = static_cast<float>(kSampleRate) / static_cast<float>(kFftSize);
    const int maxBin = kFftSize / 2;
    const int n = static_cast<int>(outDb.size());
    for (int i = 0; i < n; ++i) {
        const float f0 = 35.0f + (12000.0f - 35.0f) * (static_cast<float>(i) / std::max(1, n));
        const float f1 = 35.0f + (12000.0f - 35.0f) * (static_cast<float>(i + 1) / std::max(1, n));
        const int b0 = std::clamp(static_cast<int>(f0 / df), 1, maxBin);
        const int b1 = std::clamp(static_cast<int>(f1 / df), b0, maxBin);
        float sum = 0.0f;
        for (int k = b0; k <= b1; ++k) {
            sum += m_out[k][0] * m_out[k][0] + m_out[k][1] * m_out[k][1];
        }
        outDb[i] = 20.0f * std::log10(std::sqrt(sum) + 1e-7f);
    }
}

void SpectrumFeed::computeBands(const RadialConfig &cfg, float dtSec, std::vector<float> &outLeft, std::vector<float> &outRight) {
    const int n = cfg.raysPerWing;
    m_db.assign(n, -120.0f);
    evalMono(m_db);
    outLeft.assign(n, 0.0f);

    float maxDb = -120.0f;
    for (float v : m_db) maxDb = std::max(maxDb, v);
    if (maxDb > m_agcPeakDb) {
        m_agcPeakDb += (maxDb - m_agcPeakDb) * (1.0f - std::exp(-dtSec / 0.10f));
    } else if (maxDb > -65.0f) {
        m_agcPeakDb = std::max(-28.0f, m_agcPeakDb - 1.5f * dtSec);
    }

    const float botDb = m_agcPeakDb - cfg.dbRange;
    const float invRange = 1.0f / std::max(1.0f, cfg.dbRange);
    for (int i = 0; i < n; ++i) {
        outLeft[i] = std::clamp((m_db[i] - botDb) * invRange, 0.0f, 1.0f);
    }
    outRight = outLeft;
}
