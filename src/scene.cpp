#include "scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

void SdfScene::clear() {
    std::memset(&data_, 0, sizeof(data_));
    data_.meta[1] = -0.85f; // default groundY
    data_.meta[2] = 0.0f;   // repeatSpacing off
}

void SdfScene::addNode(const SdfNodeGpu& node) {
    int count = static_cast<int>(data_.meta[0]);
    if (count >= MAX_SDF_NODES) return;
    data_.nodes[count] = node;
    data_.meta[0] = static_cast<float>(count + 1);
}

void SdfScene::buildPreset(int presetIndex, float t, float blendK, int opOverride) {
    clear();

    auto applyOverride = [&]() {
        if (opOverride < 0) return;
        int cnt = static_cast<int>(data_.meta[0]);
        for (int i = 1; i < cnt; ++i) {
            data_.nodes[i].op = static_cast<float>(opOverride);
            if (opOverride == static_cast<int>(SdfOp::SmoothUnion)) {
                data_.nodes[i].blend = blendK;
            }
        }
    };
    if (presetIndex == 0) {
        // 01: Smooth Blending Playground
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
        // 02: CSG Booleans (Box minus Sphere + Inner Torus)
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
        // 03: Infinite Domain Repetition
        data_.meta[2] = 2.6f; // repeatSpacing
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
        // 04: Procedural Cluster (Pillars + Floating Capsule & Orb)
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
        // 4 corner pillars
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

    // Compute conservative bounding sphere for ray-sphere early-out (when not repeating)
    if (data_.meta[2] > 0.01f || nodeCount() == 0) {
        data_.bounds[0] = 0.0f;
        data_.bounds[1] = 0.0f;
        data_.bounds[2] = 0.0f;
        data_.bounds[3] = 0.0f;
    } else {
        float cx = 0.0f, cy = 0.45f, cz = 0.0f;
        float maxR = 0.0f;
        for (int i = 0; i < nodeCount(); ++i) {
            const auto& n = data_.nodes[i];
            float dx = n.pos[0] - cx;
            float dy = n.pos[1] - cy;
            float dz = n.pos[2] - cz;
            float centerDist = std::sqrt(dx * dx + dy * dy + dz * dz);
            float ext = std::sqrt(n.size[0] * n.size[0] + n.size[1] * n.size[1] + n.size[2] * n.size[2]);
            // Torus outer radius + minor radius or sphere/box/capsule + blend margin
            float nodeRadius = ext + n.size[1] + std::max(0.5f, n.blend) + 0.35f;
            maxR = std::max(maxR, centerDist + nodeRadius);
        }
        data_.bounds[0] = cx;
        data_.bounds[1] = cy;
        data_.bounds[2] = cz;
        data_.bounds[3] = maxR;
    }
}
