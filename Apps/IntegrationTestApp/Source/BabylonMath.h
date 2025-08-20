#pragma once

namespace IntegrationTestApp
{

class Rect
{
public:
    float top;
    float left;
    float right;
    float bottom;
    
    Rect()
    : left(0), top(0), right(0), bottom(0) {}
    
    Rect(float left, float top, float right, float bottom)
    : left(left), top(top), right(right), bottom(bottom) {}
    
    float Left() const { return left; }
    float Top() const { return top; }
    float Right() const { return right; } // Fixed: changed from `x + width` to `right`
    float Bottom() const { return bottom; } // Fixed: changed from `y + height` to `bottom`
};

class Matrix4
{
public:
    float m[16];
    
    Matrix4()
    {
        // Initialize as identity matrix
        m[0] = 1;
        m[1] = 0;
        m[2] = 0;
        m[3] = 0;
        
        m[4] = 0;
        m[5] = 1;
        m[6] = 0;
        m[7] = 0;
        
        m[8] = 0;
        m[9] = 0;
        m[10] = 1;
        m[11] = 0;
        
        m[12] = 0;
        m[13] = 0;
        m[14] = 0;
        m[15] = 1;
    }
    
    Matrix4(const float* values)
    {
        for (int i = 0; i < 16; ++i)
            m[i] = values[i];
    }
    
    static Matrix4 Identity()
    {
        return Matrix4();
    }
    
    Matrix4 operator*(const Matrix4& other) const
    {
        Matrix4 result;
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                result.m[row * 4 + col] = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    result.m[row * 4 + col] += m[row * 4 + k] * other.m[k * 4 + col];
                }
            }
        }
        return result;
    }
};

class Vector3
{
public:
    float x, y, z;
    
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
};

class ICameraTransform
{
public:
    ICameraTransform()
    : m_nearClip(0.1f), m_farClip(1000.0f), m_position(0, 0, 0), m_targetPoint(0, 0, -1), m_upVector(0, 1, 0), m_fovInDegree(60.0f)
    {
    }
    
    float GetNearClip() const { return m_nearClip; }
    float GetFarClip() const { return m_farClip; }
    Vector3 GetPosition() const { return m_position; }
    Vector3 GetTargetPoint() const { return m_targetPoint; }
    Vector3 GetUpVector() const { return m_upVector; }
    float GetFovInDegree() const { return m_fovInDegree; }
    
    void SetNearClip(float nearClip) { m_nearClip = nearClip; }
    void SetFarClip(float farClip) { m_farClip = farClip; }
    void SetPosition(const Vector3& position) { m_position = position; }
    void SetTargetPoint(const Vector3& targetPoint) { m_targetPoint = targetPoint; }
    void SetUpVector(const Vector3& upVector) { m_upVector = upVector; }
    void SetFovInDegree(float fovInDegree) { m_fovInDegree = fovInDegree; }
    
private:
    float m_nearClip;
    float m_farClip;
    Vector3 m_position;
    Vector3 m_targetPoint;
    Vector3 m_upVector;
    float m_fovInDegree;
};
}
