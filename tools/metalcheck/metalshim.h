/* A stand-in for <metal_stdlib>, so shaders/trace.metal can be type-checked
 * by an ordinary C++ compiler on a machine with no Metal toolchain.
 *
 * MSL is a C++14 dialect. A host compiler will therefore catch undeclared
 * names, misspelled functions, wrong argument counts and type mismatches --
 * which is most of what goes wrong in a hand translation, and in particular
 * catches a `params` argument dropped from one of the six functions that
 * take one. It cannot check Metal attributes, address spaces, or anything
 * at all about how the shader runs. On a Mac, use the real compiler:
 *
 *     xcrun -sdk macosx metal -c shaders/trace.metal -o /dev/null
 *
 * Swizzles are methods here (v.xyz()) because C++ has no member aliasing;
 * metalcheck.py rewrites `.xyz` to `.xyz()` on the way in.
 *
 * This file only needs to declare what the tracer actually uses. If a new
 * metal_stdlib call lands in trace.metal, it lands here too -- an "undeclared
 * identifier" from this shim means exactly that, not a bug in the shader.
 */
#ifndef HOLO_METALSHIM_H
#define HOLO_METALSHIM_H

#include <cmath>

#if defined(_MSC_VER)
#pragma warning(disable : 4201)   /* nameless struct/union is the whole trick */
#endif

struct float2 {
    float x, y;
    float2() : x(0), y(0) {}
    float2(float a, float b) : x(a), y(b) {}
};

struct float3 {
    union {
        struct { float x, y, z; };
        struct { float r, g, b; };
    };
    float3() : x(0), y(0), z(0) {}
    explicit float3(float a) : x(a), y(a), z(a) {}
    float3(float a, float b, float c) : x(a), y(b), z(c) {}
    float2 xy() const { return float2(x, y); }
};

struct float4 {
    float x, y, z, w;
    float4() : x(0), y(0), z(0), w(0) {}
    float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    float4(float3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
    float2 xy() const { return float2(x, y); }
    float3 xyz() const { return float3(x, y, z); }
    float3 yzw() const { return float3(y, z, w); }
};

/* float3 arithmetic, including the scalar broadcasts MSL allows */
inline float3 operator+(float3 a, float3 b) { return float3(a.x+b.x, a.y+b.y, a.z+b.z); }
inline float3 operator-(float3 a, float3 b) { return float3(a.x-b.x, a.y-b.y, a.z-b.z); }
inline float3 operator*(float3 a, float3 b) { return float3(a.x*b.x, a.y*b.y, a.z*b.z); }
inline float3 operator/(float3 a, float3 b) { return float3(a.x/b.x, a.y/b.y, a.z/b.z); }
inline float3 operator*(float3 a, float s)  { return float3(a.x*s, a.y*s, a.z*s); }
inline float3 operator*(float s, float3 a)  { return float3(a.x*s, a.y*s, a.z*s); }
inline float3 operator/(float3 a, float s)  { return float3(a.x/s, a.y/s, a.z/s); }
inline float3 operator+(float3 a, float s)  { return float3(a.x+s, a.y+s, a.z+s); }
inline float3 operator-(float3 a, float s)  { return float3(a.x-s, a.y-s, a.z-s); }
inline float3 operator-(float3 a)           { return float3(-a.x, -a.y, -a.z); }
inline float3 &operator+=(float3 &a, float3 b) { a = a + b; return a; }

/* float4 arithmetic */
inline float4 operator*(float4 a, float s)  { return float4(a.x*s, a.y*s, a.z*s, a.w*s); }
inline float4 operator*(float s, float4 a)  { return float4(a.x*s, a.y*s, a.z*s, a.w*s); }
inline float4 operator+(float4 a, float4 b) { return float4(a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w); }

/* the metal_stdlib calls the tracer uses */
inline float dot(float3 a, float3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline float3 cross(float3 a, float3 b) {
    return float3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}
inline float3 normalize(float3 v) { return v / std::sqrt(dot(v, v)); }
inline float3 reflect(float3 i, float3 n) { return i - n * (2.0f * dot(i, n)); }

inline float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float3 clamp(float3 v, float lo, float hi) {
    return float3(clamp(v.x, lo, hi), clamp(v.y, lo, hi), clamp(v.z, lo, hi));
}
inline float3 mix(float3 a, float3 b, float t) { return a + (b - a) * t; }
inline float3 pow(float3 a, float3 e) {
    return float3(std::pow(a.x, e.x), std::pow(a.y, e.y), std::pow(a.z, e.z));
}

using std::sqrt; using std::abs; using std::floor;
using std::cos;  using std::sin; using std::atan;
inline float max(float a, float b) { return a > b ? a : b; }
inline float min(float a, float b) { return a < b ? a : b; }

#endif
