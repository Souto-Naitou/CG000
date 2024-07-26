#pragma once

/// <summary>
/// 3次元ベクトル
/// </summary>
struct Vector3 {
	float x;
	float y;
	float z;
	Vector3 operator-(const Vector3& _v) const;
	Vector3 operator-() const;
	Vector3 operator+(const Vector3& _v) const;
	Vector3 operator*(const float& _f) const;
	void	operator+=(const Vector3& _r);
};