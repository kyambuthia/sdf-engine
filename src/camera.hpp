#pragma once

#include <algorithm>
#include <cmath>

struct OrbitCamera {
    float yaw = 0.65f;
    float pitch = 0.32f;
    float distance = 4.5f;
    float target[3] = {0.0f, 0.35f, 0.0f};

    void orbit(float dx, float dy) {
        yaw -= dx * 0.006f;
        pitch = std::clamp(pitch + dy * 0.006f, -1.45f, 1.45f);
    }

    void zoom(float delta) {
        distance = std::clamp(distance - delta * 0.35f, 1.4f, 18.0f);
    }

    void reset() {
        yaw = 0.65f;
        pitch = 0.32f;
        distance = 4.5f;
        target[0] = 0.0f;
        target[1] = 0.35f;
        target[2] = 0.0f;
    }

    void computeBasis(float pos[3], float forward[3], float right[3], float up[3]) const {
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);

        pos[0] = target[0] + distance * cp * sy;
        pos[1] = target[1] + distance * sp;
        pos[2] = target[2] + distance * cp * cy;

        // Forward = normalize(target - pos)
        float fx = target[0] - pos[0];
        float fy = target[1] - pos[1];
        float fz = target[2] - pos[2];
        float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (flen > 1e-6f) {
            fx /= flen;
            fy /= flen;
            fz /= flen;
        }
        forward[0] = fx;
        forward[1] = fy;
        forward[2] = fz;

        // Right = normalize(cross(forward, worldUp(0,1,0)))
        float rx = -fz;
        float ry = 0.0f;
        float rz = fx;
        float rlen = std::sqrt(rx * rx + rz * rz);
        if (rlen > 1e-6f) {
            rx /= rlen;
            rz /= rlen;
        } else {
            rx = 1.0f;
            rz = 0.0f;
        }
        right[0] = rx;
        right[1] = ry;
        right[2] = rz;

        // Up = cross(right, forward)
        up[0] = ry * fz - rz * fy;
        up[1] = rz * fx - rx * fz;
        up[2] = rx * fy - ry * fx;
    }
};
