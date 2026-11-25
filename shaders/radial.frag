#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform vec2 u_resolution;
uniform float u_dpr;

uniform int u_bars;
uniform float u_bars_left[256];
uniform float u_bars_right[256];

uniform float u_base_radius;
uniform float u_max_len;
uniform float u_ray_width_px;
uniform float u_ring_opacity;
uniform float u_tip_glow;

uniform float u_corner_radius;
uniform float u_bg_opacity;
uniform float u_border_opacity;

uniform vec3 u_bg_color;
uniform vec3 u_border_color;
uniform vec3 u_ring_color;
uniform vec3 u_ray_inner;
uniform vec3 u_ray_mid;
uniform vec3 u_ray_outer;

const float PI = 3.14159265358979323846;

float sdRoundedBox(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float evalWing(vec2 p, int count, bool isRight, out float outAmp, out float outRadialT) {
    outAmp = 0.0;
    outRadialT = 0.0;

    float minDim = min(u_resolution.x, u_resolution.y);
    float baseR = u_base_radius * minDim;
    float maxLen = u_max_len * minDim;
    float r = length(p);

    float aa = 1.0;
    if (r < baseR - aa * 2.0 || r > baseR + maxLen + aa * 4.0) {
        return 0.0;
    }

    // Left wing: 12 o'clock (bass) -> 6 o'clock (treble)
    // Right wing: flipped 180 deg so 6 o'clock = bass -> 12 o'clock = treble
    float alpha = isRight ? atan(p.x, -p.y) : atan(-p.x, p.y);
    float sector = PI / float(count);
    int idx = clamp(int(floor(alpha / sector)), 0, count - 1);

    float halfW = 0.5 * u_ray_width_px * u_dpr;
    float maxSafeHalf = 0.44 * baseR * sin(sector);
    halfW = min(halfW, max(0.6, maxSafeHalf));

    float bestMask = 0.0;
    float bestAmp = 0.0;
    float bestT = 0.0;

    for (int offset = -1; offset <= 1; ++offset) {
        int j = idx + offset;
        if (j < 0 || j >= count) {
            continue;
        }
        float thetaC = (float(j) + 0.5) * sector;
        float dTheta = abs(alpha - thetaC);
        if (dTheta > 1.5707963) {
            continue;
        }

        float dPerp = r * sin(dTheta);
        float wAlpha = 1.0 - smoothstep(halfW - 0.65, halfW + 0.65, dPerp);
        if (wAlpha <= 0.001) {
            continue;
        }

        float amp = clamp(isRight ? u_bars_right[j] : u_bars_left[j], 0.0, 1.0);
        if (amp <= 0.002) {
            continue;
        }

        float len = amp * maxLen;
        if (len < 1.0) {
            continue;
        }

        float innerFade = smoothstep(baseR - 0.75, baseR + 0.75, r);
        float tipDist = (baseR + len) - r;
        float outerFade = smoothstep(-0.75, 0.75, tipDist);

        float glow = 0.0;
        if (u_tip_glow > 0.001 && tipDist > -5.0 * u_dpr && tipDist < 2.0 * u_dpr && r >= baseR) {
            float dTip = max(0.0, -tipDist);
            glow = u_tip_glow * 0.25 * exp(-dTip / (1.8 * u_dpr)) * amp;
        }

        float m = wAlpha * max(innerFade * outerFade, glow);
        if (m > bestMask) {
            bestMask = m;
            bestAmp = amp;
            bestT = clamp((r - baseR) / max(len, 1.0), 0.0, 1.0);
        }
    }

    outAmp = bestAmp;
    outRadialT = bestT;
    return bestMask;
}

void main() {
    vec2 center = 0.5 * u_resolution;
    vec2 p = gl_FragCoord.xy - center;
    float minDim = min(u_resolution.x, u_resolution.y);

    float radiusPx = u_corner_radius * u_dpr;
    float dBox = sdRoundedBox(p, center - 0.5, radiusPx);
    float cardMask = 1.0 - smoothstep(-0.75, 0.75, dBox);
    if (cardMask <= 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 col = u_bg_color;
    float outAlpha = u_bg_opacity;

    float borderBand = (1.0 - smoothstep(0.0, 1.4 * u_dpr, abs(dBox + 0.5 * u_dpr))) * u_border_opacity;
    if (borderBand > 0.001) {
        col = mix(col, u_border_color, clamp(borderBand / max(outAlpha, borderBand), 0.0, 1.0));
        outAlpha = max(outAlpha, borderBand);
    }

    float baseR = u_base_radius * minDim;
    float ringMask = (1.0 - smoothstep(0.0, 1.25 * u_dpr, abs(length(p) - baseR))) * u_ring_opacity;
    if (ringMask > 0.001) {
        float newA = ringMask + outAlpha * (1.0 - ringMask);
        col = (u_ring_color * ringMask + col * outAlpha * (1.0 - ringMask)) / max(newA, 1e-5);
        outAlpha = newA;
    }

    float amp = 0.0;
    float radialT = 0.0;
    bool isRight = (p.x >= 0.0);
    float rayMask = evalWing(p, clamp(u_bars, 8, 256), isRight, amp, radialT);
    if (rayMask > 0.001) {
        float colorPos = clamp(0.65 * radialT + 0.35 * amp, 0.0, 1.0);
        vec3 rayCol = (colorPos < 0.55)
            ? mix(u_ray_inner, u_ray_mid, colorPos / 0.55)
            : mix(u_ray_mid, u_ray_outer, (colorPos - 0.55) / 0.45);
        float srcA = clamp(rayMask, 0.0, 1.0);
        float newA = srcA + outAlpha * (1.0 - srcA);
        col = (rayCol * srcA + col * outAlpha * (1.0 - srcA)) / max(newA, 1e-5);
        outAlpha = newA;
    }

    outAlpha *= cardMask;
    fragColor = vec4(col * outAlpha, outAlpha);
}
