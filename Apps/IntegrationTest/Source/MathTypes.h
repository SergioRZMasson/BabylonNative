#pragma once

#include <cmath>

namespace IntegrationTest
{

struct Vector3
{
    float x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector3 operator+(const Vector3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vector3 operator-(const Vector3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float Length() const { return std::sqrt(x * x + y * y + z * z); }

    Vector3 Normalized() const
    {
        float len = Length();
        if (len < 1e-8f)
            return {0, 0, 0};
        return {x / len, y / len, z / len};
    }

    static Vector3 Cross(const Vector3& a, const Vector3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
    }
};

struct Matrix4
{
    float m[16];

    Matrix4()
    {
        m[0] = 1;  m[1] = 0;  m[2] = 0;  m[3] = 0;
        m[4] = 0;  m[5] = 1;  m[6] = 0;  m[7] = 0;
        m[8] = 0;  m[9] = 0;  m[10] = 1; m[11] = 0;
        m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
    }

    static Matrix4 Identity() { return Matrix4(); }
};

struct Rect
{
    float left, top, right, bottom;

    Rect() : left(0), top(0), right(0), bottom(0) {}
    Rect(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
};

class ICameraTransform
{
public:
    ICameraTransform()
        : m_nearClip(0.1f)
        , m_farClip(1000.0f)
        , m_position(0, 0, 0)
        , m_targetPoint(0, 0, -1)
        , m_upVector(0, 1, 0)
        , m_fovInDegree(60.0f)
    {
    }

    float GetNearClip() const { return m_nearClip; }
    float GetFarClip() const { return m_farClip; }
    Vector3 GetPosition() const { return m_position; }
    Vector3 GetTargetPoint() const { return m_targetPoint; }
    Vector3 GetUpVector() const { return m_upVector; }
    float GetFovInDegree() const { return m_fovInDegree; }

    void SetNearClip(float v) { m_nearClip = v; }
    void SetFarClip(float v) { m_farClip = v; }
    void SetPosition(const Vector3& v) { m_position = v; }
    void SetTargetPoint(const Vector3& v) { m_targetPoint = v; }
    void SetUpVector(const Vector3& v) { m_upVector = v; }
    void SetFovInDegree(float v) { m_fovInDegree = v; }

private:
    float m_nearClip;
    float m_farClip;
    Vector3 m_position;
    Vector3 m_targetPoint;
    Vector3 m_upVector;
    float m_fovInDegree;
};

} // namespace IntegrationTest
