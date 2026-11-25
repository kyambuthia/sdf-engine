#pragma once

#include <cstdint>
#include <vector>

enum class SdfPrimitive : int {
    Sphere = 0,
    RoundBox = 1,
    Torus = 2,
    Capsule = 3
};

enum class SdfOp : int {
    Union = 0,
    Subtract = 1,
    Intersect = 2,
    SmoothUnion = 3
};

// Matches std140 SceneBlock layout in sdf_raymarch.frag (64 bytes per node).
struct alignas(16) SdfNodeGpu {
    float pos[3];
    float type;       // SdfPrimitive as float
    float size[3];
    float op;         // SdfOp as float
    float color[3];
    float blend;      // smooth union k
    float rot[3];     // Euler angles (radians)
    float _pad;
};

constexpr int MAX_SDF_NODES = 32;

struct alignas(16) SceneUniformData {
    float meta[4]; // x = nodeCount, y = groundY, z = repeatSpacing (0 = off), w = unused
    SdfNodeGpu nodes[MAX_SDF_NODES];
};

class SdfScene {
public:
    void clear();
    void addNode(const SdfNodeGpu& node);
    void buildPreset(int presetIndex, float time);

    const SceneUniformData& gpuData() const { return data_; }
    int nodeCount() const { return static_cast<int>(data_.meta[0]); }

private:
    SceneUniformData data_{};
};
