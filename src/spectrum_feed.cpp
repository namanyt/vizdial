#include "spectrum_feed.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool zonesEqual(const std::array<FreqZone, 5> &a, const std::array<FreqZone, 5> &b) {
    for (size_t i = 0; i < 5; ++i) {
        if (std::abs(a[i].minHz - b[i].minHz) > 0.1f || std::abs(a[i].maxHz - b[i].maxHz) > 0.1f || std::abs(a[i].share - b[i].share) > 0.01f || std::abs(a[i].gain - b[i].gain) > 0.005f || std::abs(a[i].influence - b[i].influence) > 0.005f) return false;
    }
    return true;
}

float integrateBandPower(const std::vector<float> &power, float df, float fLo, float fHi, float fCenter) {
    const int maxBin = static_cast<int>(power.size()) - 1;
    const float b0 = std::clamp(fLo / df, 1.0f, static_cast<float>(maxBin - 1));
    const float b1 = std::clamp(fHi / df, b0 + 1e-4f, static_cast<float>(maxBin));
    const float width = b1 - b0;

    const int i0 = static_cast<int>(std::floor(b0));
    const int i1 = static_cast<int>(std::floor(b1));

    if (i0 == i1 || width < 1.0f) {
        const float bc = std::clamp(fCenter / df, 1.0f, static_cast<float>(maxBin - 1));
        const int idx = static_cast<int>(std::floor(bc));
        const float frac = bc - static_cast<float>(idx);
        const float p = power[idx] * (1.0f - frac) + power[idx + 1] * frac;
        return std::sqrt(std::max(0.0f, p * width));
    }

    float sum = (static_cast<float>(i0 + 1) - b0) * power[i0];
    for (int k = i0 + 1; k < i1 && k < maxBin; ++k) sum += power[k];
    if (i1 <= maxBin) sum += (b1 - static_cast<float>(i1)) * power[i1];
    return std::sqrt(std::max(0.0f, sum));
}

} // namespace

SpectrumFeed::SpectrumFeed()
    : m_ringL(kRingSize, 0.0f),
      m_ringR(kRingSize, 0.0f),
      m_snapL(kWinLow, 0.0f),
      m_snapR(kWinLow, 0.0f),
      m_snapMono(kWinLow, 0.0f),
      m_hannLow(kWinLow, 0.0f),
      m_hannHigh(kWinHigh, 0.0f),
      m_powLow(kFftLow / 2 + 1, 0.0f),
      m_powHigh(kFftHigh / 2 + 1, 0.0f) {
    initFft();
}

SpectrumFeed::~SpectrumFeed() {
    stop();
    destroyFft();
}

void SpectrumFeed::initFft() {
    double sumLow = 0.0;
    for (int i = 0; i < kWinLow; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(kWinLow - 1);
        const double s = std::sin(kPi * std::pow(u, 1.8));
        const double w = s * s;
        m_hannLow[i] = static_cast<float>(w);
        sumLow += w;
    }
    const float normLow = static_cast<float>(2.0 / std::max(1.0, sumLow));
    for (float &v : m_hannLow) v *= normLow;

    double sumHigh = 0.0;
    for (int i = 0; i < kWinHigh; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(kWinHigh - 1);
        const double s = std::sin(kPi * std::pow(u, 1.4));
        const double w = s * s;
        m_hannHigh[i] = static_cast<float>(w);
        sumHigh += w;
    }
    const float normHigh = static_cast<float>(2.0 / std::max(1.0, sumHigh));
    for (float &v : m_hannHigh) v *= normHigh;

    m_inLow = fftwf_alloc_real(kFftLow);
    m_inHigh = fftwf_alloc_real(kFftHigh);
    m_outLow = fftwf_alloc_complex(kFftLow / 2 + 1);
    m_outHigh = fftwf_alloc_complex(kFftHigh / 2 + 1);
    std::memset(m_inLow, 0, sizeof(float) * kFftLow);
    std::memset(m_inHigh, 0, sizeof(float) * kFftHigh);

    m_planLow = fftwf_plan_dft_r2c_1d(kFftLow, m_inLow, m_outLow, FFTW_ESTIMATE);
    m_planHigh = fftwf_plan_dft_r2c_1d(kFftHigh, m_inHigh, m_outHigh, FFTW_ESTIMATE);
}

void SpectrumFeed::destroyFft() {
    if (m_planLow) fftwf_destroy_plan(m_planLow);
    if (m_planHigh) fftwf_destroy_plan(m_planHigh);
    if (m_inLow) fftwf_free(m_inLow);
    if (m_inHigh) fftwf_free(m_inHigh);
    if (m_outLow) fftwf_free(m_outLow);
    if (m_outHigh) fftwf_free(m_outHigh);
    m_planLow = nullptr;
    m_planHigh = nullptr;
    m_inLow = m_inHigh = nullptr;
    m_outLow = m_outHigh = nullptr;
}

void SpectrumFeed::configure(const RadialConfig &cfg) {
    rebuildBands(cfg);
    if (!m_audioActive || m_source != cfg.source) {
        stop();
        startAudio(cfg.source);
    }
}

void SpectrumFeed::rebuildBands(const RadialConfig &cfg) {
    if (m_bandN == cfg.raysPerWing &&
        m_bandLowHz == cfg.lowerCutoffHz &&
        m_bandHighHz == cfg.higherCutoffHz &&
        std::abs(m_bandTilt - cfg.tiltDbPerOct) < 1e-3f &&
        std::abs(m_bandBlendRays - cfg.zoneBlendRays) < 1e-3f &&
        m_bandZonesEnabled == cfg.zonesEnabled &&
        zonesEqual(m_bandZones, cfg.zones)) {
        return;
    }

    m_bandN = cfg.raysPerWing;
    m_bandLowHz = cfg.lowerCutoffHz;
    m_bandHighHz = cfg.higherCutoffHz;
    m_bandTilt = cfg.tiltDbPerOct;
    m_bandBlendRays = cfg.zoneBlendRays;
    m_bandZonesEnabled = cfg.zonesEnabled;
    m_bandZones = cfg.zones;

    m_bands.clear();
    m_bands.reserve(m_bandN);
    m_dbLeft.assign(m_bandN, -120.0f);
    m_dbRight.assign(m_bandN, -120.0f);
    m_normLeft.assign(m_bandN, 0.0f);
    m_normRight.assign(m_bandN, 0.0f);
    auto smoothstep01 = [](float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); };

    auto pushSlice = [&](float fMin, float fMax, int count, int zoneIdx) {
        const float ratio = std::max(1.01f, fMax / std::max(4.0f, fMin));
        for (int j = 0; j < count; ++j) {
            const float tC = (static_cast<float>(j) + 0.5f) / static_cast<float>(count);
            const float t0 = static_cast<float>(j) / static_cast<float>(count);
            const float t1 = static_cast<float>(j + 1) / static_cast<float>(count);
            LogBand b;
            b.fCenter = fMin * std::pow(ratio, tC);
            b.fLo = fMin * std::pow(ratio, t0);
            b.fHi = fMin * std::pow(ratio, t1);
            b.tiltGain = std::pow(10.0f, (cfg.tiltDbPerOct * std::log2(b.fCenter / 250.0f)) / 20.0f);
            b.highMix = std::clamp((b.fCenter - 180.0f) / 160.0f, 0.0f, 1.0f);
            b.zoneIdx = zoneIdx;
            b.blendZone = zoneIdx;
            const float wMax = std::min(cfg.zoneBlendRays, 0.45f * static_cast<float>(count));
            if (cfg.zonesEnabled && wMax > 0.25f) {
                const float dLeft = static_cast<float>(j) + 0.5f;
                const float dRight = static_cast<float>(count - 1 - j) + 0.5f;
                if (dLeft < wMax) {
                    const float t = 0.5f - 0.5f * (dLeft / wMax);
                    b.blendZone = (zoneIdx + 4) % 5;
                    b.blendWeight = smoothstep01(t * 2.0f) * 0.5f;
                    b.seamMask = std::clamp(b.blendWeight * 2.0f, 0.0f, 1.0f);
                } else if (dRight < wMax) {
                    const float t = 0.5f - 0.5f * (dRight / wMax);
                    b.blendZone = (zoneIdx + 1) % 5;
                    b.blendWeight = smoothstep01(t * 2.0f) * 0.5f;
                    b.seamMask = std::clamp(b.blendWeight * 2.0f, 0.0f, 1.0f);
                }
            }
            m_bands.push_back(b);
        }
    };
    if (cfg.zonesEnabled) {
        const auto counts = computeZoneRayCounts(cfg);
        for (int z = 0; z < 5; ++z) pushSlice(cfg.zones[z].minHz, cfg.zones[z].maxHz, counts[z], z);
    } else {
        pushSlice(static_cast<float>(cfg.lowerCutoffHz), static_cast<float>(cfg.higherCutoffHz), m_bandN, 0);
    }
}

void SpectrumFeed::startAudio(const QString &source) {
    m_source = source;
    m_sourceBytes = m_source.toUtf8();
    m_ml = pa_threaded_mainloop_new();
    if (!m_ml || pa_threaded_mainloop_start(m_ml) < 0) return;

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
                self->m_ringL[self->m_writePos] = src ? src[i * 2] : 0.0f;
                self->m_ringR[self->m_writePos] = src ? src[i * 2 + 1] : 0.0f;
                self->m_writePos = (self->m_writePos + 1) % kRingSize;
            }
        }
        pa_stream_drop(s);
    }
}

void SpectrumFeed::evalChannel(const std::vector<float> &samples, size_t startIdx, std::vector<float> &outDb) {
    for (int i = 0; i < kWinLow; ++i) {
        m_inLow[i] = samples[(startIdx + static_cast<size_t>(i)) % kRingSize] * m_hannLow[i];
    }
    std::memset(m_inLow + kWinLow, 0, sizeof(float) * (kFftLow - kWinLow));
    fftwf_execute(m_planLow);

    const int nLowBins = kFftLow / 2 + 1;
    for (int k = 0; k < nLowBins; ++k) {
        m_powLow[k] = m_outLow[k][0] * m_outLow[k][0] + m_outLow[k][1] * m_outLow[k][1];
    }

    const size_t highOffset = (startIdx + static_cast<size_t>(kWinLow - kWinHigh)) % kRingSize;
    for (int i = 0; i < kWinHigh; ++i) {
        m_inHigh[i] = samples[(highOffset + static_cast<size_t>(i)) % kRingSize] * m_hannHigh[i];
    }
    std::memset(m_inHigh + kWinHigh, 0, sizeof(float) * (kFftHigh - kWinHigh));
    fftwf_execute(m_planHigh);

    const int nHighBins = kFftHigh / 2 + 1;
    for (int k = 0; k < nHighBins; ++k) {
        m_powHigh[k] = m_outHigh[k][0] * m_outHigh[k][0] + m_outHigh[k][1] * m_outHigh[k][1];
    }

    constexpr float dfLow = static_cast<float>(kSampleRate) / static_cast<float>(kFftLow);
    constexpr float dfHigh = static_cast<float>(kSampleRate) / static_cast<float>(kFftHigh);

    outDb.resize(m_bands.size());
    for (size_t i = 0; i < m_bands.size(); ++i) {
        const LogBand &b = m_bands[i];
        const float rLow = (b.highMix < 0.999f) ? integrateBandPower(m_powLow, dfLow, b.fLo, b.fHi, b.fCenter) : 0.0f;
        const float rHigh = (b.highMix > 0.001f) ? integrateBandPower(m_powHigh, dfHigh, b.fLo, b.fHi, b.fCenter) : 0.0f;
        const float rms = (rLow * (1.0f - b.highMix) + rHigh * b.highMix) * b.tiltGain;
        outDb[i] = 20.0f * std::log10(rms + 1e-7f);
    }
}

void SpectrumFeed::computeBands(const RadialConfig &cfg, float dtSec, std::vector<float> &outLeft, std::vector<float> &outRight) {
    rebuildBands(cfg);
    const int n = static_cast<int>(m_bands.size());
    outLeft.assign(n, 0.0f);
    outRight.assign(n, 0.0f);

    size_t wp = 0;
    {
        std::lock_guard<std::mutex> lk(m_ringMutex);
        wp = m_writePos;
        std::memcpy(m_snapL.data(), m_ringL.data(), sizeof(float) * kRingSize);
        std::memcpy(m_snapR.data(), m_ringR.data(), sizeof(float) * kRingSize);
    }

    float peakAbs = 0.0f;
    const size_t checkStart = (wp + kRingSize - kWinHigh) % kRingSize;
    for (int i = 0; i < kWinHigh; ++i) {
        const size_t idx = (checkStart + static_cast<size_t>(i)) % kRingSize;
        peakAbs = std::max(peakAbs, std::max(std::abs(m_snapL[idx]), std::abs(m_snapR[idx])));
    }
    if (peakAbs < 2e-5f) return;

    if (cfg.mirrorMode == "stereo") {
        evalChannel(m_snapL, wp, m_dbLeft);
        evalChannel(m_snapR, wp, m_dbRight);
    } else {
        for (size_t i = 0; i < kRingSize; ++i) m_snapMono[i] = 0.5f * (m_snapL[i] + m_snapR[i]);
        evalChannel(m_snapMono, wp, m_dbLeft);
        m_dbRight = m_dbLeft;
    }

    float maxDb = -120.0f;
    std::array<float, 5> zoneMaxDb{-120.0f, -120.0f, -120.0f, -120.0f, -120.0f};
    for (int i = 0; i < n; ++i) {
        const float bMax = std::max(m_dbLeft[i], m_dbRight[i]);
        maxDb = std::max(maxDb, bMax);
        const int z = std::clamp(m_bands[i].zoneIdx, 0, 4);
        zoneMaxDb[z] = std::max(zoneMaxDb[z], bMax);
    }
    const float rise = 1.0f - std::exp(-dtSec / 0.10f);
    if (maxDb > m_agcPeakDb) m_agcPeakDb += (maxDb - m_agcPeakDb) * rise;
    else if (maxDb > -65.0f) m_agcPeakDb = std::max(-28.0f, m_agcPeakDb - 1.5f * dtSec);
    for (int z = 0; z < 5; ++z) {
        if (zoneMaxDb[z] > m_zonePeakDb[z]) m_zonePeakDb[z] += (zoneMaxDb[z] - m_zonePeakDb[z]) * rise;
        else if (zoneMaxDb[z] > -72.0f) m_zonePeakDb[z] = std::max(-36.0f, m_zonePeakDb[z] - 1.5f * dtSec);
    }
    std::array<float, 5> zoneTopDb{};
    for (int z = 0; z < 5; ++z) {
        const float infl = cfg.zonesEnabled ? std::clamp(cfg.zones[z].influence, 0.0f, 1.0f) : 0.0f;
        zoneTopDb[z] = m_agcPeakDb * (1.0f - infl) + m_zonePeakDb[z] * infl;
    }
    m_normLeft.resize(n);
    m_normRight.resize(n);
    const float invRange = 1.0f / std::max(1.0f, cfg.dbRange);
    for (int i = 0; i < n; ++i) {
        const LogBand &b = m_bands[i];
        const int z = std::clamp(b.zoneIdx, 0, 4);
        const int zb = std::clamp(b.blendZone, 0, 4);
        const float w = b.blendWeight;
        const float zg = cfg.zonesEnabled ? (cfg.zones[z].gain * (1.0f - w) + cfg.zones[zb].gain * w) : 1.0f;
        const float topDb = zoneTopDb[z] * (1.0f - w) + zoneTopDb[zb] * w;
        const float botDb = topDb - cfg.dbRange;
        m_normLeft[i] = std::clamp(std::clamp((m_dbLeft[i] - botDb) * invRange, 0.0f, 1.0f) * zg, 0.0f, 1.0f);
        m_normRight[i] = std::clamp(std::clamp((m_dbRight[i] - botDb) * invRange, 0.0f, 1.0f) * zg, 0.0f, 1.0f);
    }
    constexpr float kKer[5] = {0.10f, 0.22f, 0.36f, 0.22f, 0.10f};
    const int totalRing = 2 * n;
    for (int i = 0; i < n; ++i) {
        const float sm = m_bands[i].seamMask * 0.75f;
        if (sm <= 0.001f) { outLeft[i] = m_normLeft[i]; outRight[i] = m_normRight[i]; continue; }
        float blurL = 0.0f, blurR = 0.0f;
        for (int k = -2; k <= 2; ++k) {
            const float kw = kKer[k + 2];
            const int uL = (i + k + totalRing) % totalRing;
            blurL += ((uL < n) ? m_normLeft[uL] : m_normRight[uL - n]) * kw;
            const int uR = (n + i + k + totalRing) % totalRing;
            blurR += ((uR < n) ? m_normLeft[uR] : m_normRight[uR - n]) * kw;
        }
        outLeft[i] = m_normLeft[i] * (1.0f - sm) + blurL * sm;
        outRight[i] = m_normRight[i] * (1.0f - sm) + blurR * sm;
    }
}
