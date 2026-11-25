#version 330 core

in vec2 v_uv;
out vec4 fragColor;

uniform vec2 u_resolution;
uniform int u_bars;
uniform float u_bars_left[256];
uniform float u_bars_right[256];

uniform float u_base_radius;
uniform float u_max_len;
uniform float u_ray_width_px;
uniform float u_ring_opacity;

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
    if (r < baseR - 2.0 || r > baseR + maxLen + 4.0) {
        return 0.0;
    }

    float alpha = isRight ? atan(p.x, p.y) : atan(-p.x, p.y);
    float sector = PI / float(count);
    int idx = clamp(int(floor(alpha / sector)), 0, count - 1);

    float halfAngle = 0.5 * (u_ray_width_px / max(r, 1.0));
    float thetaC = (float(idx) + 0.5) * sector;
    float dTheta = abs(alpha - thetaC);
    float wAlpha = 1.0 - smoothstep(halfAngle * 0.65, halfAngle * 1.35, dTheta);
    if (wAlpha <= 0.001) {
        return 0.0;
    }

    float amp = clamp(isRight ? u_bars_right[idx] : u_bars_left[idx], 0.0, 1.0);
    float len = amp * maxLen;
    if (len < 1.0) {
        return 0.0;
    }

    float innerFade = smoothstep(baseR - 0.75, baseR + 0.75, r);
    float outerFade = smoothstep(-0.75, 0.75, (baseR + len) - r);
    outAmp = amp;
    outRadialT = clamp((r - baseR) / max(len, 1.0), 0.0, 1.0);
    return wAlpha * innerFade * outerFade;
}

void main() {
    vec2 center = 0.5 * u_resolution;
    vec2 p = gl_FragCoord.xy - center;
    float minDim = min(u_resolution.x, u_resolution.y);

    float dBox = sdRoundedBox(p, center - 0.5, u_corner_radius);
    float cardMask = 1.0 - smoothstep(-0.75, 0.75, dBox);
    if (cardMask <= 0.001) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 col = u_bg_color;
    float outAlpha = u_bg_opacity;

    float borderBand = (1.0 - smoothstep(0.0, 1.4, abs(dBox + 0.5))) * u_border_opacity;
    if (borderBand > 0.001) {
        col = mix(col, u_border_color, clamp(borderBand / max(outAlpha, borderBand), 0.0, 1.0));
        outAlpha = max(outAlpha, borderBand);
    }

    float baseR = u_base_radius * minDim;
    float ringMask = (1.0 - smoothstep(0.0, 1.25, abs(length(p) - baseR))) * u_ring_opacity;
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
