// Engine math. Left-handed, Y up, row vectors: v' = v * M, translation in row 3.
// Angles: yaw 0 faces +Z and grows turning right (toward +X); pitch > 0 looks up.
#pragma once

#include <algorithm>
#include <cmath>

namespace eng {

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTwoPi = kPi * 2.0f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }

// Wraps degrees into [-180, 180).
inline float wrap_degrees(float d) {
    d = std::fmod(d + 180.0f, 360.0f);
    if (d < 0) d += 360.0f;
    return d - 180.0f;
}
// Shortest interpolation between two headings in degrees.
inline float lerp_degrees(float a, float b, float t) { return a + wrap_degrees(b - a) * t; }

struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3(const float* p) : x(p[0]), y(p[1]), z(p[2]) {}
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length_sq(const Vec3& v) { return dot(v, v); }
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    return l > 1e-8f ? v / l : Vec3{0, 0, 0};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    Vec3 xyz() const { return {x, y, z}; }
};

// Unit direction for a yaw/pitch pair in degrees.
inline Vec3 angles_to_forward(float yaw_deg, float pitch_deg) {
    float y = yaw_deg * kDegToRad, p = pitch_deg * kDegToRad;
    return {std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)};
}
inline Vec3 yaw_to_right(float yaw_deg) {
    float y = yaw_deg * kDegToRad;
    return {std::cos(y), 0.0f, -std::sin(y)};
}
inline float forward_to_yaw(const Vec3& f) { return std::atan2(f.x, f.z) * kRadToDeg; }
inline float forward_to_pitch(const Vec3& f) {
    return std::atan2(f.y, std::sqrt(f.x * f.x + f.z * f.z)) * kRadToDeg;
}

// Rotation as x y z w, the order model files store it in.
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

inline Quat normalize(const Quat& q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l <= 1e-8f) return {};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

// Shortest-arc interpolation; `t` outside 0..1 extrapolates, which callers do not do.
inline Quat slerp(const Quat& a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        d = -d;
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    if (d > 0.9995f)
        return normalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                          a.w + (b.w - a.w) * t});
    const float theta = std::acos(d < -1.0f ? -1.0f : d);
    const float s = std::sin(theta);
    const float wa = std::sin((1 - t) * theta) / s, wb = std::sin(t * theta) / s;
    return normalize({a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    static Mat4 identity() { return {}; }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j] + m[i][3] * o.m[3][j];
        return r;
    }

    Vec3 transform_point(const Vec3& v) const {
        return {v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + m[3][0],
                v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + m[3][1],
                v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + m[3][2]};
    }
    Vec3 transform_vector(const Vec3& v) const {
        return {v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
                v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
                v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2]};
    }
    Vec4 transform(const Vec4& v) const {
        return {v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + v.w * m[3][0],
                v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + v.w * m[3][1],
                v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + v.w * m[3][2],
                v.x * m[0][3] + v.y * m[1][3] + v.z * m[2][3] + v.w * m[3][3]};
    }

    static Mat4 translation(const Vec3& t) {
        Mat4 r;
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }
    static Mat4 scale(const Vec3& s) {
        Mat4 r;
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        return r;
    }
    static Mat4 rotation_x(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[1][1] = c; r.m[1][2] = s;
        r.m[2][1] = -s; r.m[2][2] = c;
        return r;
    }
    static Mat4 rotation_y(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c; r.m[0][2] = -s;
        r.m[2][0] = s; r.m[2][2] = c;
        return r;
    }
    static Mat4 rotation_z(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c; r.m[0][1] = s;
        r.m[1][0] = -s; r.m[1][1] = c;
        return r;
    }
    // Object facing yaw (degrees): its local +Z becomes angles_to_forward(yaw, 0).
    static Mat4 yaw(float yaw_deg) { return rotation_y(yaw_deg * kDegToRad); }

    // Rotation for row vectors: v * from_quaternion(q) turns v by q.
    static Mat4 from_quaternion(const Quat& q) {
        const float x = q.x, y = q.y, z = q.z, w = q.w;
        Mat4 r;
        r.m[0][0] = 1 - 2 * (y * y + z * z); r.m[0][1] = 2 * (x * y + z * w); r.m[0][2] = 2 * (x * z - y * w);
        r.m[1][0] = 2 * (x * y - z * w); r.m[1][1] = 1 - 2 * (x * x + z * z); r.m[1][2] = 2 * (y * z + x * w);
        r.m[2][0] = 2 * (x * z + y * w); r.m[2][1] = 2 * (y * z - x * w); r.m[2][2] = 1 - 2 * (x * x + y * y);
        return r;
    }
    // The rotation part as a quaternion. The matrix must have no scale or shear.
    Quat to_quaternion() const {
        Quat q;
        const float trace = m[0][0] + m[1][1] + m[2][2];
        if (trace > 0) {
            const float s = std::sqrt(trace + 1.0f) * 2;
            q.w = 0.25f * s;
            q.x = (m[1][2] - m[2][1]) / s;
            q.y = (m[2][0] - m[0][2]) / s;
            q.z = (m[0][1] - m[1][0]) / s;
        } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
            const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2;
            q.w = (m[1][2] - m[2][1]) / s;
            q.x = 0.25f * s;
            q.y = (m[1][0] + m[0][1]) / s;
            q.z = (m[2][0] + m[0][2]) / s;
        } else if (m[1][1] > m[2][2]) {
            const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2;
            q.w = (m[2][0] - m[0][2]) / s;
            q.x = (m[1][0] + m[0][1]) / s;
            q.y = 0.25f * s;
            q.z = (m[2][1] + m[1][2]) / s;
        } else {
            const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2;
            q.w = (m[0][1] - m[1][0]) / s;
            q.x = (m[2][0] + m[0][2]) / s;
            q.y = (m[2][1] + m[1][2]) / s;
            q.z = 0.25f * s;
        }
        return normalize(q);
    }

    // Local basis rows (right, up, forward) and a position.
    static Mat4 from_basis(const Vec3& right, const Vec3& up, const Vec3& forward, const Vec3& pos) {
        Mat4 r;
        r.m[0][0] = right.x; r.m[0][1] = right.y; r.m[0][2] = right.z; r.m[0][3] = 0;
        r.m[1][0] = up.x; r.m[1][1] = up.y; r.m[1][2] = up.z; r.m[1][3] = 0;
        r.m[2][0] = forward.x; r.m[2][1] = forward.y; r.m[2][2] = forward.z; r.m[2][3] = 0;
        r.m[3][0] = pos.x; r.m[3][1] = pos.y; r.m[3][2] = pos.z; r.m[3][3] = 1;
        return r;
    }

    static Mat4 look_to_lh(const Vec3& eye, const Vec3& forward, const Vec3& up_hint) {
        Vec3 z = normalize(forward);
        Vec3 x = normalize(cross(up_hint, z));
        if (length_sq(x) < 1e-6f) x = normalize(cross(Vec3{0, 0, 1}, z));
        Vec3 y = cross(z, x);
        Mat4 r;
        r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x; r.m[0][3] = 0;
        r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y; r.m[1][3] = 0;
        r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z; r.m[2][3] = 0;
        r.m[3][0] = -dot(x, eye); r.m[3][1] = -dot(y, eye); r.m[3][2] = -dot(z, eye); r.m[3][3] = 1;
        return r;
    }

    // Right-handed view (Pure3D's own convention): the camera looks down its -Z.
    static Mat4 look_to_rh(const Vec3& eye, const Vec3& forward, const Vec3& up_hint) {
        Vec3 z = normalize(-forward);
        Vec3 x = normalize(cross(up_hint, z));
        if (length_sq(x) < 1e-6f) x = normalize(cross(Vec3{0, 0, 1}, z));
        Vec3 y = cross(z, x);
        Mat4 r;
        r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x; r.m[0][3] = 0;
        r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y; r.m[1][3] = 0;
        r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z; r.m[2][3] = 0;
        r.m[3][0] = -dot(x, eye); r.m[3][1] = -dot(y, eye); r.m[3][2] = -dot(z, eye); r.m[3][3] = 1;
        return r;
    }

    static Mat4 perspective_rh(float fov_y_rad, float aspect, float zn, float zf) {
        float ys = 1.0f / std::tan(fov_y_rad * 0.5f);
        float xs = ys / aspect;
        Mat4 r;
        r.m[0][0] = xs; r.m[1][1] = ys;
        r.m[2][2] = zf / (zn - zf); r.m[2][3] = -1;
        r.m[3][2] = zn * zf / (zn - zf); r.m[3][3] = 0;
        return r;
    }

    static Mat4 perspective_lh(float fov_y_rad, float aspect, float zn, float zf) {
        float ys = 1.0f / std::tan(fov_y_rad * 0.5f);
        float xs = ys / aspect;
        Mat4 r;
        r.m[0][0] = xs; r.m[1][1] = ys;
        r.m[2][2] = zf / (zf - zn); r.m[2][3] = 1;
        r.m[3][2] = -zn * zf / (zf - zn); r.m[3][3] = 0;
        return r;
    }

    static Mat4 ortho_offcenter_lh(float l, float r_, float b, float t, float zn, float zf) {
        Mat4 r;
        r.m[0][0] = 2 / (r_ - l);
        r.m[1][1] = 2 / (t - b);
        r.m[2][2] = 1 / (zf - zn);
        r.m[3][0] = (l + r_) / (l - r_);
        r.m[3][1] = (t + b) / (b - t);
        r.m[3][2] = zn / (zn - zf);
        return r;
    }

    Mat4 transposed() const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) r.m[i][j] = m[j][i];
        return r;
    }

    Mat4 inverse() const;
};

inline Mat4 Mat4::inverse() const {
    const float* a = &m[0][0];
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    Mat4 r;
    if (std::fabs(det) < 1e-12f) return r;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) (&r.m[0][0])[i] = inv[i] * det;
    return r;
}

struct Aabb {
    Vec3 min{1e30f, 1e30f, 1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void add(const Vec3& p) { min = vmin(min, p); max = vmax(max, p); }
    void add(const Aabb& b) { if (b.valid()) { min = vmin(min, b.min); max = vmax(max, b.max); } }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extent() const { return (max - min) * 0.5f; }
    bool overlaps(const Aabb& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y && min.z <= o.max.z &&
               max.z >= o.min.z;
    }
};

// Slab test. Returns the entry distance along the (not necessarily unit) direction in [0, max_t], or -1.
inline float ray_aabb(const Vec3& origin, const Vec3& inv_dir, const Aabb& box, float max_t) {
    float t0 = 0.0f, t1 = max_t;
    for (int a = 0; a < 3; ++a) {
        float n = (box.min[a] - origin[a]) * inv_dir[a];
        float f = (box.max[a] - origin[a]) * inv_dir[a];
        if (n > f) std::swap(n, f);
        if (std::isnan(n)) n = -1e30f;
        if (std::isnan(f)) f = 1e30f;
        t0 = std::max(t0, n);
        t1 = std::min(t1, f);
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    static constexpr Color rgb(unsigned hex, float a = 1.0f) {
        return {float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, a};
    }
    Color with_alpha(float na) const { return {r, g, b, na}; }
    Color scaled(float s) const { return {r * s, g * s, b * s, a}; }
};

}  // namespace eng
