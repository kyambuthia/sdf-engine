#version 300 es
precision highp float;

uniform vec2 uResolution;
uniform float uTime;
uniform int uScene;
uniform int uQuality;
uniform vec3 uCamPos;
uniform vec3 uCamForward;
uniform vec3 uCamRight;
uniform vec3 uCamUp;

out vec4 fragColor;

// -----------------------------------------------------------------------------
// SDF Primitives
// -----------------------------------------------------------------------------

float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

float sdRoundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float sdCapsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float sdPlane(vec3 p, float y) {
    return p.y - y;
}

// -----------------------------------------------------------------------------
// SDF Boolean & Smooth Blending Operations
// -----------------------------------------------------------------------------

// Returns vec2(distance, materialId)
vec2 opUnion(vec2 a, vec2 b) {
    return (a.x < b.x) ? a : b;
}

float opSubtract(float a, float b) {
    return max(a, -b);
}

float opIntersect(float a, float b) {
    return max(a, b);
}

// Polynomial smooth minimum (blends shapes organically)
vec2 opSmoothUnion(vec2 a, vec2 b, float k) {
    float h = clamp(0.5 + 0.5 * (b.x - a.x) / k, 0.0, 1.0);
    float d = mix(b.x, a.x, h) - k * h * (1.0 - h);
    float mat = mix(b.y, a.y, h);
    return vec2(d, mat);
}

mat2 rot2D(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, -s, s, c);
}

// -----------------------------------------------------------------------------
// Scene Definitions (uScene 0..3)
// -----------------------------------------------------------------------------

vec2 mapScene(vec3 p) {
    // Ground plane (material 0 = checkerboard)
    vec2 res = vec2(sdPlane(p, -0.85), 0.0);

    if (uScene == 0) {
        // Scene 0: Smooth Blending Playground
        vec3 boxPos = p - vec3(0.0, 0.35, 0.0);
        boxPos.xz *= rot2D(uTime * 0.4);
        float box = sdRoundBox(boxPos, vec3(0.55), 0.12);

        vec3 s1Pos = p - vec3(sin(uTime * 1.1) * 1.15, 0.45 + cos(uTime * 1.7) * 0.25, cos(uTime * 0.9) * 0.6);
        float s1 = sdSphere(s1Pos, 0.48);

        vec3 s2Pos = p - vec3(-cos(uTime * 0.9) * 1.1, 0.35 + sin(uTime * 1.3) * 0.3, sin(uTime * 1.2) * 0.7);
        float s2 = sdSphere(s2Pos, 0.38);

        vec2 blend = opSmoothUnion(vec2(box, 1.0), vec2(s1, 2.0), 0.45);
        blend = opSmoothUnion(blend, vec2(s2, 3.0), 0.45);
        res = opUnion(res, blend);
    } else if (uScene == 1) {
        // Scene 1: CSG Booleans (Subtraction & Intersection)
        vec3 q = p - vec3(0.0, 0.4, 0.0);
        q.xz *= rot2D(uTime * 0.35);

        float box = sdRoundBox(q, vec3(0.75), 0.08);
        float cutter = sdSphere(q - vec3(0.45 * sin(uTime), 0.25, 0.45 * cos(uTime)), 0.72);
        float carved = opSubtract(box, cutter);

        float innerCore = sdTorus(q.xzy, vec2(0.45, 0.16));
        res = opUnion(res, vec2(carved, 1.0));
        res = opUnion(res, vec2(innerCore, 3.0));
    } else if (uScene == 2) {
        // Scene 2: Infinite Domain Repetition
        vec3 q = p - vec3(0.0, 0.45, 0.0);
        float spacing = 2.6;
        vec3 cell = q;
        cell.xz = mod(q.xz + 0.5 * spacing, spacing) - 0.5 * spacing;

        // Twist each repeated torus gently
        cell.xz *= rot2D(cell.y * 1.2 + uTime * 0.8);
        float torus = sdTorus(cell, vec2(0.55, 0.18));
        float core = sdSphere(cell, 0.25 + 0.06 * sin(uTime * 2.0));
        vec2 obj = opSmoothUnion(vec2(torus, 2.0), vec2(core, 3.0), 0.2);
        res = opUnion(res, obj);
    } else {
        // Scene 3: Procedural Pillars & Floating Capsule Ring
        vec3 q = p;
        float spacing = 3.0;
        vec3 pillarP = q;
        pillarP.xz = mod(q.xz + 0.5 * spacing, spacing) - 0.5 * spacing;
        float pillar = sdRoundBox(pillarP - vec3(0.0, 0.6, 0.0), vec3(0.28, 1.5, 0.28), 0.06);

        vec3 ringP = p - vec3(0.0, 0.55 + 0.15 * sin(uTime * 1.5), 0.0);
        ringP.xz *= rot2D(uTime * 0.5);
        float cap = sdCapsule(ringP, vec3(-0.7, 0.0, 0.0), vec3(0.7, 0.0, 0.0), 0.28);
        float orb = sdSphere(p - vec3(0.0, 0.55 + 0.15 * sin(uTime * 1.5), 0.0), 0.42);

        res = opUnion(res, vec2(pillar, 1.0));
        res = opUnion(res, opSmoothUnion(vec2(cap, 2.0), vec2(orb, 3.0), 0.25));
    }

    return res;
}

// -----------------------------------------------------------------------------
// Raymarching, Normals, Shadows & Ambient Occlusion
// -----------------------------------------------------------------------------

vec2 raymarch(vec3 ro, vec3 rd) {
    float t = 0.0;
    float mat = -1.0;
    const float maxDist = 45.0;

    int maxSteps = (uQuality > 0) ? 110 : 56;
    for (int i = 0; i < 110; i++) {
        if (i >= maxSteps) break;
        vec3 p = ro + rd * t;
        vec2 hit = mapScene(p);
        if (hit.x < 0.001) {
            mat = hit.y;
            break;
        }
        t += hit.x;
        if (t > maxDist) {
            break;
        }
    }
    return vec2(t, mat);
}

// Tetrahedron finite-difference normal (4 map evaluations)
vec3 calcNormal(vec3 p) {
    const float eps = 0.001;
    const vec2 k = vec2(1.0, -1.0);
    return normalize(
        k.xyy * mapScene(p + k.xyy * eps).x +
        k.yyx * mapScene(p + k.yyx * eps).x +
        k.yxy * mapScene(p + k.yxy * eps).x +
        k.xxx * mapScene(p + k.xxx * eps).x
    );
}

float calcSoftShadow(vec3 ro, vec3 rd, float mint, float maxt, float k) {
    float res = 1.0;
    float t = mint;
    for (int i = 0; i < 32; i++) {
        float h = mapScene(ro + rd * t).x;
        res = min(res, k * h / t);
        t += clamp(h, 0.02, 0.25);
        if (res < 0.005 || t > maxt) break;
    }
    return clamp(res, 0.0, 1.0);
}

float calcAO(vec3 pos, vec3 nor) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; i++) {
        float h = 0.02 + 0.12 * float(i) / 4.0;
        float d = mapScene(pos + h * nor).x;
        occ += (h - d) * sca;
        sca *= 0.85;
    }
    return clamp(1.0 - 2.2 * occ, 0.0, 1.0);
}

vec3 getMaterialColor(float matId, vec3 p) {
    if (matId < 0.5) {
        // Ground checkerboard
        float checker = mod(floor(p.x) + floor(p.z), 2.0);
        return mix(vec3(0.14, 0.16, 0.20), vec3(0.24, 0.27, 0.33), checker);
    }
    if (matId < 1.5) return vec3(0.26, 0.62, 0.96); // Cobalt blue
    if (matId < 2.5) return vec3(0.96, 0.42, 0.32); // Coral orange
    return vec3(0.32, 0.90, 0.68);                  // Mint teal
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    const float focalLength = 1.65;
    vec3 rd = normalize(uCamRight * uv.x + uCamUp * uv.y + uCamForward * focalLength);
    vec3 ro = uCamPos;

    // Sky gradient
    vec3 sky = mix(vec3(0.06, 0.08, 0.13), vec3(0.16, 0.24, 0.38), clamp(rd.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 col = sky;

    vec2 hit = raymarch(ro, rd);
    float t = hit.x;
    float matId = hit.y;

    if (matId >= 0.0) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);
        vec3 albedo = getMaterialColor(matId, p);

        vec3 lightDir = normalize(vec3(0.65, 0.85, 0.45));
        float diff = max(dot(n, lightDir), 0.0);
        float shadow = (uQuality > 0) ? calcSoftShadow(p + n * 0.004, lightDir, 0.02, 12.0, 12.0) : 1.0;
        float ao = (uQuality > 0) ? calcAO(p, n) : 1.0;

        vec3 halfDir = normalize(lightDir - rd);
        float spec = pow(max(dot(n, halfDir), 0.0), 48.0);
        float fresnel = pow(1.0 - max(dot(n, -rd), 0.0), 4.0);

        vec3 lit = albedo * (0.18 * ao + 1.15 * diff * shadow);
        lit += vec3(0.95, 0.97, 1.0) * spec * shadow * 0.45;
        lit += vec3(0.35, 0.55, 0.90) * fresnel * 0.25 * ao;

        // Distance fog
        float fog = 1.0 - exp(-0.0018 * t * t);
        col = mix(lit, sky, clamp(fog, 0.0, 1.0));
    }

    // Gamma correction
    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
