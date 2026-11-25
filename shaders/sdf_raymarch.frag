#version 300 es
precision highp float;

#define MAX_NODES 32

struct SdfNode {
    vec4 posType;    // xyz = position, w = primitive type (0=Sphere,1=RoundBox,2=Torus,3=Capsule)
    vec4 sizeOp;     // xyz = size, w = op (0=Union,1=Subtract,2=Intersect,3=SmoothUnion)
    vec4 colorBlend; // rgb = albedo, a = blend/rounding k
    vec4 rotPad;     // xyz = Euler rotation (rad), w = pad
};

layout(std140) uniform SceneBlock {
    vec4 uSceneMeta; // x = nodeCount, y = groundY, z = repeatSpacing, w = unused
    vec4 uBounds;    // xyz = bounding sphere center, w = radius (0 = unbounded)
    SdfNode uNodes[MAX_NODES];
};

uniform vec2 uResolution;
uniform float uTime;
uniform int uScene;
uniform int uQuality;
uniform float uFogDensity;
uniform float uShadowStrength;
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

float sdCapsule(vec3 p, float halfLen, float r) {
    p.x -= clamp(p.x, -halfLen, halfLen);
    return length(p) - r;
}

mat2 rot2D(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, -s, s, c);
}

float evalPrimitive(vec3 p, SdfNode node) {
    vec3 q = p - node.posType.xyz;
    vec3 rot = node.rotPad.xyz;
    if (abs(rot.y) > 1e-4) q.xz *= rot2D(rot.y);
    if (abs(rot.x) > 1e-4) q.yz *= rot2D(rot.x);
    if (abs(rot.z) > 1e-4) q.xy *= rot2D(rot.z);

    int kind = int(node.posType.w + 0.5);
    if (kind == 0) return sdSphere(q, node.sizeOp.x);
    if (kind == 1) return sdRoundBox(q, node.sizeOp.xyz, node.colorBlend.a);
    if (kind == 2) return sdTorus(q, node.sizeOp.xy);
    return sdCapsule(q, node.sizeOp.x, node.sizeOp.y);
}

// -----------------------------------------------------------------------------
// SDF Boolean & Smooth Blending Operations (returns vec4(dist, rgb))
// -----------------------------------------------------------------------------

vec4 opUnion(vec4 a, vec4 b) {
    return (a.x < b.x) ? a : b;
}

vec4 opSubtract(vec4 a, vec4 b) {
    return (a.x > -b.x) ? a : vec4(-b.x, b.yzw);
}

vec4 opIntersect(vec4 a, vec4 b) {
    return (a.x > b.x) ? a : b;
}

vec4 opSmoothUnion(vec4 a, vec4 b, float k) {
    float h = clamp(0.5 + 0.5 * (b.x - a.x) / max(k, 1e-4), 0.0, 1.0);
    float d = mix(b.x, a.x, h) - k * h * (1.0 - h);
    vec3 col = mix(b.yzw, a.yzw, h);
    return vec4(d, col);
}

// -----------------------------------------------------------------------------
// Data-Driven 3D Shapes Evaluation (ground plane handled analytically)
// -----------------------------------------------------------------------------

vec4 mapShapes(vec3 p) {
    vec3 sampleP = p;
    float spacing = uSceneMeta.z;
    if (spacing > 0.01) {
        sampleP.xz = mod(p.xz + 0.5 * spacing, spacing) - 0.5 * spacing;
    }

    int count = int(uSceneMeta.x + 0.5);
    vec4 shapeRes = vec4(1e5, vec3(0.5));
    bool hasShape = false;

    for (int i = 0; i < MAX_NODES; i++) {
        if (i >= count) break;
        SdfNode node = uNodes[i];
        float d = evalPrimitive(sampleP, node);
        vec4 cand = vec4(d, node.colorBlend.rgb);

        if (!hasShape) {
            shapeRes = cand;
            hasShape = true;
        } else {
            int op = int(node.sizeOp.w + 0.5);
            if (op == 1) {
                shapeRes = opSubtract(shapeRes, cand);
            } else if (op == 2) {
                shapeRes = opIntersect(shapeRes, cand);
            } else if (op == 3) {
                shapeRes = opSmoothUnion(shapeRes, cand, node.colorBlend.a);
            } else {
                shapeRes = opUnion(shapeRes, cand);
            }
        }
    }
    return shapeRes;
}

// Returns vec2(tNear, tFar) for ray-sphere intersection; tFar < 0.0 means miss.
vec2 intersectSphere(vec3 ro, vec3 rd, vec4 sphere) {
    vec3 oc = ro - sphere.xyz;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - sphere.w * sphere.w;
    float h = b * b - c;
    if (h < 0.0) return vec2(1e5, -1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

// -----------------------------------------------------------------------------
// Over-Relaxed Sphere Tracing + Analytic Ground + Bounding Cull
// -----------------------------------------------------------------------------

// Returns vec4(t, albedo.rgb) and sets isGround = true if analytic floor hit.
vec4 raymarch(vec3 ro, vec3 rd, out bool isGround) {
    isGround = false;
    const float maxDist = 45.0;

    // Analytic ground plane intersection
    float tGround = maxDist;
    if (rd.y < -1e-4) {
        float tg = (uSceneMeta.y - ro.y) / rd.y;
        if (tg > 0.0 && tg < maxDist) {
            tGround = tg;
        }
    }

    float tStart = 0.0;
    float tEnd = tGround;

    // Bounding sphere early-out for non-repeating scenes
    if (uBounds.w > 0.01) {
        vec2 span = intersectSphere(ro, rd, uBounds);
        if (span.y < 0.0 || span.x > tGround) {
            if (tGround < maxDist) {
                isGround = true;
                vec3 gp = ro + rd * tGround;
                float checker = mod(floor(gp.x) + floor(gp.z), 2.0);
                return vec4(tGround, mix(vec3(0.14, 0.16, 0.20), vec3(0.24, 0.27, 0.33), checker));
            }
            return vec4(maxDist, vec3(-1.0));
        }
        tStart = max(0.0, span.x);
        tEnd = min(tGround, span.y);
    }

    // Over-relaxation sphere tracing (omega = 1.45)
    float t = tStart;
    float omega = 1.45;
    float prevRadius = 0.0;
    float stepLen = 0.0;
    int maxSteps = (uQuality > 0) ? 96 : 48;

    for (int i = 0; i < 96; i++) {
        if (i >= maxSteps || t > tEnd) break;

        vec3 p = ro + rd * t;
        vec4 hit = mapShapes(p);
        float r = hit.x;

        // Overstep check: if spheres don't overlap, fall back safely
        if (omega > 1.0 && (prevRadius + abs(r)) < stepLen) {
            t -= stepLen * (1.0 - 1.0 / omega);
            omega = 1.0;
            prevRadius = 0.0;
            stepLen = 0.0;
            continue;
        }

        float eps = max(0.001, 0.0012 * t);
        if (r < eps) {
            return vec4(t, hit.yzw);
        }

        prevRadius = r;
        stepLen = r * omega;
        t += stepLen;
    }

    if (tGround < maxDist) {
        isGround = true;
        vec3 gp = ro + rd * tGround;
        float checker = mod(floor(gp.x) + floor(gp.z), 2.0);
        return vec4(tGround, mix(vec3(0.14, 0.16, 0.20), vec3(0.24, 0.27, 0.33), checker));
    }
    return vec4(maxDist, vec3(-1.0));
}

vec3 calcNormal(vec3 p) {
    const float eps = 0.001;
    const vec2 k = vec2(1.0, -1.0);
    return normalize(
        k.xyy * mapShapes(p + k.xyy * eps).x +
        k.yyx * mapShapes(p + k.yyx * eps).x +
        k.yxy * mapShapes(p + k.yxy * eps).x +
        k.xxx * mapShapes(p + k.xxx * eps).x
    );
}

float calcSoftShadow(vec3 ro, vec3 rd, float mint, float maxt, float k) {
    if (uBounds.w > 0.01) {
        vec2 span = intersectSphere(ro, rd, uBounds);
        if (span.y < 0.0) return 1.0;
    }
    float res = 1.0;
    float t = mint;
    for (int i = 0; i < 24; i++) {
        float h = mapShapes(ro + rd * t).x;
        res = min(res, k * h / t);
        t += clamp(h, 0.025, 0.30);
        if (res < 0.005 || t > maxt) break;
    }
    return clamp(res, 0.0, 1.0);
}

float calcAO(vec3 pos, vec3 nor) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 4; i++) {
        float h = 0.03 + 0.14 * float(i) / 3.0;
        float d = min(mapShapes(pos + h * nor).x, (pos.y + h * nor.y) - uSceneMeta.y);
        occ += (h - d) * sca;
        sca *= 0.85;
    }
    return clamp(1.0 - 2.0 * occ, 0.0, 1.0);
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    const float focalLength = 1.65;
    vec3 rd = normalize(uCamRight * uv.x + uCamUp * uv.y + uCamForward * focalLength);
    vec3 ro = uCamPos;

    vec3 sky = mix(vec3(0.06, 0.08, 0.13), vec3(0.16, 0.24, 0.38), clamp(rd.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 col = sky;

    bool isGround = false;
    vec4 hit = raymarch(ro, rd, isGround);
    float t = hit.x;
    vec3 albedo = hit.yzw;

    if (albedo.x >= 0.0) {
        vec3 p = ro + rd * t;
        vec3 n = isGround ? vec3(0.0, 1.0, 0.0) : calcNormal(p);

        vec3 lightDir = normalize(vec3(0.65, 0.85, 0.45));
        float diff = max(dot(n, lightDir), 0.0);
        float rawShadow = (uQuality > 0 && uShadowStrength > 0.01)
            ? calcSoftShadow(p + n * 0.004, lightDir, 0.02, 12.0, 12.0)
            : 1.0;
        float shadow = mix(1.0, rawShadow, uShadowStrength);
        float ao = (uQuality > 0 && !isGround) ? calcAO(p, n) : 1.0;

        vec3 halfDir = normalize(lightDir - rd);
        float spec = isGround ? 0.0 : pow(max(dot(n, halfDir), 0.0), 48.0);
        float fresnel = isGround ? 0.0 : pow(1.0 - max(dot(n, -rd), 0.0), 4.0);

        vec3 lit = albedo * (0.18 * ao + 1.15 * diff * shadow);
        lit += vec3(0.95, 0.97, 1.0) * spec * shadow * 0.45;
        lit += vec3(0.35, 0.55, 0.90) * fresnel * 0.25 * ao;

        float fog = 1.0 - exp(-uFogDensity * t * t);
        col = mix(lit, sky, clamp(fog, 0.0, 1.0));
    }

    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
