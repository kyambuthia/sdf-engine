#include "scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

inline float length3(float x, float y, float z) {
    return std::sqrt(x * x + y * y + z * z);
}

inline void rot2(float& a, float& b, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    float na = c * a - s * b;
    float nb = s * a + c * b;
    a = na;
    b = nb;
}

} // namespace

void SdfScene::clear() {
    std::memset(&data_, 0, sizeof(data_));
    data_.meta[1] = -0.85f;
    data_.meta[2] = 0.0f;
}

void SdfScene::addNode(const SdfNodeGpu& node) {
    int count = nodeCount();
    if (count >= MAX_SDF_NODES) return;
    data_.nodes[count] = node;
    data_.meta[0] = static_cast<float>(count + 1);
}

void SdfScene::removeNode(int index) {
    int count = nodeCount();
    if (index < 0 || index >= count) return;
    for (int i = index; i < count - 1; ++i) {
        data_.nodes[i] = data_.nodes[i + 1];
    }
    std::memset(&data_.nodes[count - 1], 0, sizeof(SdfNodeGpu));
    data_.meta[0] = static_cast<float>(count - 1);
    recomputeBounds();
}

SdfNodeGpu* SdfScene::nodeAt(int index) {
    if (index < 0 || index >= nodeCount()) return nullptr;
    return &data_.nodes[index];
}

const SdfNodeGpu* SdfScene::nodeAt(int index) const {
    if (index < 0 || index >= nodeCount()) return nullptr;
    return &data_.nodes[index];
}

float SdfScene::evalNodeDist(const float p[3], const SdfNodeGpu& node) const {
    float qx = p[0] - node.pos[0];
    float qy = p[1] - node.pos[1];
    float qz = p[2] - node.pos[2];

    if (std::abs(node.rot[1]) > 1e-4f) rot2(qx, qz, node.rot[1]);
    if (std::abs(node.rot[0]) > 1e-4f) rot2(qy, qz, node.rot[0]);
    if (std::abs(node.rot[2]) > 1e-4f) rot2(qx, qy, node.rot[2]);

    int kind = static_cast<int>(node.type + 0.5f);
    if (kind == 0) {
        return length3(qx, qy, qz) - node.size[0];
    } else if (kind == 1) {
        float ax = std::abs(qx) - node.size[0];
        float ay = std::abs(qy) - node.size[1];
        float az = std::abs(qz) - node.size[2];
        float out = length3(std::max(ax, 0.0f), std::max(ay, 0.0f), std::max(az, 0.0f));
        float in = std::min(std::max(ax, std::max(ay, az)), 0.0f);
        return out + in - node.blend;
    } else if (kind == 2) {
        float rx = std::sqrt(qx * qx + qz * qz) - node.size[0];
        return std::sqrt(rx * rx + qy * qy) - node.size[1];
    } else {
        float cx = qx - clampf(qx, -node.size[0], node.size[0]);
        return length3(cx, qy, qz) - node.size[1];
    }
}

int SdfScene::pickNode(const float ro[3], const float rd[3]) const {
    int count = nodeCount();
    if (count <= 0) return -1;

    float t = 0.0f;
    const float maxDist = 40.0f;
    const float spacing = data_.meta[2];

    for (int step = 0; step < 96; ++step) {
        float p[3] = {ro[0] + rd[0] * t, ro[1] + rd[1] * t, ro[2] + rd[2] * t};
        if (spacing > 0.01f) {
            p[0] = std::fmod(p[0] + 0.5f * spacing, spacing);
            if (p[0] < 0.0f) p[0] += spacing;
            p[0] -= 0.5f * spacing;
            p[2] = std::fmod(p[2] + 0.5f * spacing, spacing);
            if (p[2] < 0.0f) p[2] += spacing;
            p[2] -= 0.5f * spacing;
        }

        float minD = 1e5f;
        int bestIdx = -1;
        for (int i = 0; i < count; ++i) {
            float d = evalNodeDist(p, data_.nodes[i]);
            if (d < minD) {
                minD = d;
                bestIdx = i;
            }
        }
        if (minD < 0.005f) {
            return bestIdx;
        }
        t += std::max(0.005f, minD);
        if (t > maxDist) break;
    }
    return -1;
}

void SdfScene::recomputeBounds() {
    if (data_.meta[2] > 0.01f || nodeCount() == 0) {
        data_.bounds[0] = 0.0f;
        data_.bounds[1] = 0.0f;
        data_.bounds[2] = 0.0f;
        data_.bounds[3] = 0.0f;
        return;
    }
    float cx = 0.0f, cy = 0.45f, cz = 0.0f;
    float maxR = 0.0f;
    for (int i = 0; i < nodeCount(); ++i) {
        const auto& n = data_.nodes[i];
        float dx = n.pos[0] - cx;
        float dy = n.pos[1] - cy;
        float dz = n.pos[2] - cz;
        float centerDist = std::sqrt(dx * dx + dy * dy + dz * dz);
        float ext = std::sqrt(n.size[0] * n.size[0] + n.size[1] * n.size[1] + n.size[2] * n.size[2]);
        float nodeRadius = ext + n.size[1] + std::max(0.5f, n.blend) + 0.35f;
        maxR = std::max(maxR, centerDist + nodeRadius);
    }
    data_.bounds[0] = cx;
    data_.bounds[1] = cy;
    data_.bounds[2] = cz;
    data_.bounds[3] = maxR;
}

void SdfScene::buildPreset(int presetIndex, float t, float blendK, int opOverride) {
    clear();

    auto applyOverride = [&]() {
        if (opOverride < 0) return;
        int cnt = nodeCount();
        for (int i = 1; i < cnt; ++i) {
            data_.nodes[i].op = static_cast<float>(opOverride);
            if (opOverride == static_cast<int>(SdfOp::SmoothUnion)) {
                data_.nodes[i].blend = blendK;
            }
        }
    };

    if (presetIndex == 0) {
        addNode({
            {0.0f, 0.35f, 0.0f}, static_cast<float>(SdfPrimitive::RoundBox),
            {0.55f, 0.55f, 0.55f}, static_cast<float>(SdfOp::Union),
            {0.26f, 0.62f, 0.96f}, 0.12f,
            {0.0f, t * 0.4f, 0.0f}, 0.0f
        });
        addNode({
            {std::sin(t * 1.1f) * 1.15f, 0.45f + std::cos(t * 1.7f) * 0.25f, std::cos(t * 0.9f) * 0.6f},
            static_cast<float>(SdfPrimitive::Sphere),
            {0.48f, 0.0f, 0.0f}, static_cast<float>(SdfOp::SmoothUnion),
            {0.96f, 0.42f, 0.32f}, blendK,
            {0.0f, 0.0f, 0.0f}, 0.0f
        });
        addNode({
            {-std::cos(t * 0.9f) * 1.1f, 0.35f + std::sin(t * 1.3f) * 0.3f, std::sin(t * 1.2f) * 0.7f},
            static_cast<float>(SdfPrimitive::Sphere),
            {0.38f, 0.0f, 0.0f}, static_cast<float>(SdfOp::SmoothUnion),
            {0.32f, 0.90f, 0.68f}, blendK,
            {0.0f, 0.0f, 0.0f}, 0.0f
        });
    } else if (presetIndex == 1) {
        addNode({
            {0.0f, 0.40f, 0.0f}, static_cast<float>(SdfPrimitive::RoundBox),
            {0.75f, 0.75f, 0.75f}, static_cast<float>(SdfOp::Union),
            {0.26f, 0.62f, 0.96f}, 0.08f,
            {0.0f, t * 0.35f, 0.0f}, 0.0f
        });
        addNode({
            {0.45f * std::sin(t), 0.65f, 0.45f * std::cos(t)},
            static_cast<float>(SdfPrimitive::Sphere),
            {0.72f, 0.0f, 0.0f}, static_cast<float>(SdfOp::Subtract),
            {0.96f, 0.42f, 0.32f}, 0.0f,
            {0.0f, 0.0f, 0.0f}, 0.0f
        });
        addNode({
            {0.0f, 0.40f, 0.0f}, static_cast<float>(SdfPrimitive::Torus),
            {0.45f, 0.16f, 0.0f}, static_cast<float>(SdfOp::Union),
            {0.32f, 0.90f, 0.68f}, 0.0f,
            {1.5708f, t * 0.35f, 0.0f}, 0.0f
        });
    } else if (presetIndex == 2) {
        data_.meta[2] = 2.6f;
        addNode({
            {0.0f, 0.45f, 0.0f}, static_cast<float>(SdfPrimitive::Torus),
            {0.55f, 0.18f, 0.0f}, static_cast<float>(SdfOp::Union),
            {0.96f, 0.42f, 0.32f}, 0.0f,
            {t * 0.5f, t * 0.8f, 0.0f}, 0.0f
        });
        addNode({
            {0.0f, 0.45f, 0.0f}, static_cast<float>(SdfPrimitive::Sphere),
            {0.25f + 0.06f * std::sin(t * 2.0f), 0.0f, 0.0f}, static_cast<float>(SdfOp::SmoothUnion),
            {0.32f, 0.90f, 0.68f}, blendK * 0.5f,
            {0.0f, 0.0f, 0.0f}, 0.0f
        });
    } else {
        addNode({
            {0.0f, 0.55f + 0.15f * std::sin(t * 1.5f), 0.0f},
            static_cast<float>(SdfPrimitive::Capsule),
            {0.70f, 0.28f, 0.0f}, static_cast<float>(SdfOp::Union),
            {0.96f, 0.42f, 0.32f}, 0.0f,
            {0.0f, t * 0.5f, 0.0f}, 0.0f
        });
        addNode({
            {0.0f, 0.55f + 0.15f * std::sin(t * 1.5f), 0.0f},
            static_cast<float>(SdfPrimitive::Sphere),
            {0.42f, 0.0f, 0.0f}, static_cast<float>(SdfOp::SmoothUnion),
            {0.32f, 0.90f, 0.68f}, blendK * 0.6f,
            {0.0f, 0.0f, 0.0f}, 0.0f
        });
        const float corners[4][2] = {{-1.5f, -1.5f}, {1.5f, -1.5f}, {-1.5f, 1.5f}, {1.5f, 1.5f}};
        for (int i = 0; i < 4; ++i) {
            addNode({
                {corners[i][0], 0.45f, corners[i][1]},
                static_cast<float>(SdfPrimitive::RoundBox),
                {0.24f, 1.3f, 0.24f}, static_cast<float>(SdfOp::Union),
                {0.26f, 0.62f, 0.96f}, 0.06f,
                {0.0f, 0.0f, 0.0f}, 0.0f
            });
        }
    }

    applyOverride();
    recomputeBounds();
}
