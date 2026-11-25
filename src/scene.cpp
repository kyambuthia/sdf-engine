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

void SdfScene::buildPreset(int presetIndex, float t) {
    clear();

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
            {0.96f, 0.42f, 0.32f}, 0.45f,
            {0.0f, 0.0f, 0.0f}, 0.0f
        });
        addNode({
            {-std::cos(t * 0.9f) * 1.1f, 0.35f + std::sin(t * 1.3f) * 0.3f, std::sin(t * 1.2f) * 0.7f},
            static_cast<float>(SdfPrimitive::Sphere),
            {0.38f, 0.0f, 0.0f}, static_cast<float>(SdfOp::SmoothUnion),
            {0.32f, 0.90f, 0.68f}, 0.45f,
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
            {0.32f, 0.90f, 0.68f}, 0.2f,
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
            {0.32f, 0.90f, 0.68f}, 0.25f,
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
}
