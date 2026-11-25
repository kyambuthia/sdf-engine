#version 300 es
precision highp float;

uniform vec2 uResolution;
uniform float uTime;
uniform int uHasSelection;

out vec4 fragColor;

float sdCircle2D(vec2 p, float r) {
    return length(p) - r;
}

float sdBox2D(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

void main() {
    // Aspect-correct centered coordinates in [-aspect, aspect] x [-1, 1]
    vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;
    float px = 1.5 / uResolution.y;

    // 1. Center crosshair reticle using 2D SDF boxes
    float hBar = sdBox2D(uv, vec2(0.022, 0.0012));
    float vBar = sdBox2D(uv, vec2(0.0012, 0.022));
    float centerHole = sdCircle2D(uv, 0.006);
    float cross = max(min(hBar, vBar), -centerHole);
    float ring = abs(sdCircle2D(uv, 0.032)) - 0.0012;

    float dReticle = min(cross, ring);
    float reticleAlpha = 1.0 - smoothstep(-px, px, dReticle);

    // 2. Crisp 2D SDF vector frame in bottom-right corner
    vec2 cornerPos = vec2(uResolution.x / uResolution.y * 0.5 - 0.09, -0.5 + 0.085);
    vec2 q = uv - cornerPos;
    float frame = abs(sdBox2D(q, vec2(0.055, 0.028)) - 0.006) - 0.0015;
    float dotDist = sdCircle2D(q - vec2(-0.036, 0.0), 0.0065 + 0.0012 * sin(uTime * 3.0));
    float frameAlpha = 1.0 - smoothstep(-px, px, frame);
    float dotAlpha = 1.0 - smoothstep(-px, px, dotDist);

    vec3 col = vec3(0.84, 0.88, 0.94);
    vec3 dotCol = (uHasSelection > 0) ? vec3(0.29, 0.87, 0.50) : vec3(0.38, 0.65, 0.98);

    float alpha = max(reticleAlpha * 0.45, frameAlpha * 0.65);
    vec3 outCol = col;
    if (dotAlpha > alpha) {
        outCol = dotCol;
        alpha = dotAlpha * 0.9;
    }

    if (alpha < 0.01) discard;
    fragColor = vec4(outCol, alpha);
}
