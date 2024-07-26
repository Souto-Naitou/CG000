#include "vector3calc.h"

#include <assert.h>
#include <math.h>

Vector3 Add(const Vector3& _v1, const Vector3& _v2)
{
	return Vector3(
		_v1.x + _v2.x,
		_v1.y + _v2.y,
		_v1.z + _v2.z
	);
}

Vector3 Subtract(const Vector3& _v1, const Vector3& _v2)
{
	return Vector3(
		_v1.x - _v2.x,
		_v1.y - _v2.y,
		_v1.z - _v2.z
	);
}

Vector3 Multiply(float _scalar, const Vector3& _v)
{
	return Vector3(
		_scalar * _v.x,
		_scalar * _v.y,
		_scalar * _v.z
	);
}

float Dot(const Vector3& _v1, const Vector3& _v2)
{
	return float(
		_v1.x * _v2.x +
		_v1.y * _v2.y +
		_v1.z * _v2.z
		);
}

float Length(const Vector3& _v)
{
	return sqrtf(
		_v.x * _v.x +
		_v.y * _v.y +
		_v.z * _v.z
		);
}

Vector3 Normalize(const Vector3& _v)
{
	float length = Length(_v);
	return Vector3(
		_v.x / length,
		_v.y / length,
		_v.z / length
		);
}

Vector3 Transform(const Vector3& _vector, const Matrix4x4& _matrix)
{
	Vector3 result{};
	result.x = 
		_vector.x * _matrix.m[0][0] +
		_vector.y * _matrix.m[1][0] + 
		_vector.z * _matrix.m[2][0] +
		1.0f * _matrix.m[3][0];
	result.y =
		_vector.x * _matrix.m[0][1] +
		_vector.y * _matrix.m[1][1] +
		_vector.z * _matrix.m[2][1] +
		1.0f * _matrix.m[3][1];
	result.z =
		_vector.x * _matrix.m[0][2] +
		_vector.y * _matrix.m[1][2] +
		_vector.z * _matrix.m[2][2] +
		1.0f * _matrix.m[3][2];
	float w =
		_vector.x * _matrix.m[0][3] +
		_vector.y * _matrix.m[1][3] +
		_vector.z * _matrix.m[2][3] +
		1.0f * _matrix.m[3][3];
	assert(w != 0.0f);
	result.x /= w;
	result.y /= w;
	result.z /= w;
	return result;
}

Vector3 Cross(const Vector3& _v1, const Vector3& _v2)
{
	Vector3 result{};
	result.x = _v1.y * _v2.z - _v1.z * _v2.y;
	result.y = _v1.z * _v2.x - _v1.x * _v2.z;
	result.z = _v1.x * _v2.y - _v1.y * _v2.x;

	return result;
}

Vector3 Project(const Vector3& _v1, const Vector3& _v2)
{
	float v2_norm = Length(_v2);
	Vector3 v2_unit =
	{
		_v2.x / v2_norm,
		_v2.y / v2_norm,
		_v2.z / v2_norm
	};

	Vector3 result{};

	result = Multiply(Dot(_v1, v2_unit), v2_unit);

	return result;
}

Vector3 Lerp(const Vector3& _v1, const Vector3& _v2, float _t)
{
	return Add(Multiply((1.0f - _t), _v1), Multiply(_t, _v2));
}

Vector3 Reflect(const Vector3& _input, const Vector3& _normal)
{
	return Vector3(_input - _normal * 2.0f * Dot(_input, _normal));
}


Vector3 Vector3::operator-(const Vector3& _v) const
{
	return Subtract(*this, _v);
}

Vector3 Vector3::operator-() const
{
	Vector3 result{};
	result.x = -x;
	result.y = -y;
	result.z = -z;
	return result;
}

Vector3 Vector3::operator+(const Vector3& _v) const
{
	return Add(*this, _v);
}

Vector3 Vector3::operator*(const float& _f) const
{
	return Multiply(_f, *this);
}

void Vector3::operator+=(const Vector3& _r)
{
	x = x + _r.x;
	y = y + _r.y;
	z = z + _r.z;
	return;
}