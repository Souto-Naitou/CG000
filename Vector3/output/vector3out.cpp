#include "vector3out.h"

#include <Novice.h>

static const int kColumnWidth = 60;

void VectorScreenPrint(int _x, int _y, const Vector3& _vector, const char* _label)
{
	Novice::ScreenPrintf(_x, _y, "%0.2f", _vector.x);
	Novice::ScreenPrintf(_x + kColumnWidth, _y, "%0.2f", _vector.y);
	Novice::ScreenPrintf(_x + kColumnWidth * 2, _y, "%0.2f", _vector.z);
	Novice::ScreenPrintf(_x + kColumnWidth * 3, _y, _label);
}