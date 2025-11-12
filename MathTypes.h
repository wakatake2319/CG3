#pragma once
struct Vector2 {
	float x, y;
};

struct Vector3 {
	float x, y, z;
};

struct Vector4 {
	float x, y, z, w;
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct Matrix4x4 {
	float m[4][4];
};

struct Matrix3x3 {
	float m[3][3];
};

//struct float32_t4 {
//	float x;
//	float y;
//	float z;
//	float w;
//};



//struct float32_t4x4 {
//	float m[4][4];
//};

// パーティクル構造体
struct Particle {
	Transform transform;
	Vector3 velocity;
	Vector4 color;
};


// パーティクルGPU構造体
struct ParticleForGPU {
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};