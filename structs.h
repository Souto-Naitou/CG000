#pragma once
#include "math/Vector4.h"
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "Matrix4x4.h"

struct VertexData
{
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct Material
{
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};

struct TransformationMatrix
{
	Matrix4x4 WVP;
	Matrix4x4 World;
};

struct DirectionalLight
{
	Vector4 color; //!< ライトの色
	Vector3 direction; //!< ライトの向き (正規化必須)
	float intensity; //!< 輝度
};