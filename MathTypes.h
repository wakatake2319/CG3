#pragma once
struct Vector3 {
	float x, y, z;
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct Matrix4x4 {
	float m[4][4];
};
