#include "Math.h"
#include <cmath>




Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			result.m[row][col] = 0;
			for (int k = 0; k < 4; ++k) {
				result.m[row][col] += m1.m[row][k] * m2.m[k][col];
			}
		}
	}
	return result;
}

// アフィン変換行列の作成
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rot, const Vector3& translate) {

	Matrix4x4 result{};

	Matrix4x4 scaleMatrix{};
	scaleMatrix.m[0][0] = scale.x;
	scaleMatrix.m[0][1] = 0;
	scaleMatrix.m[0][2] = 0;
	scaleMatrix.m[0][3] = 0;
	scaleMatrix.m[1][0] = 0;
	scaleMatrix.m[1][1] = scale.y;
	scaleMatrix.m[1][2] = 0;
	scaleMatrix.m[1][3] = 0;
	scaleMatrix.m[2][0] = 0;
	scaleMatrix.m[2][1] = 0;
	scaleMatrix.m[2][2] = scale.z;
	scaleMatrix.m[2][3] = 0;
	scaleMatrix.m[3][0] = 0;
	scaleMatrix.m[3][1] = 0;
	scaleMatrix.m[3][2] = 0;
	scaleMatrix.m[3][3] = 1;

	// X軸回転行列
	Matrix4x4 rotateMatrixX = {};
	rotateMatrixX.m[0][0] = 1.0f;
	rotateMatrixX.m[0][1] = 0.0f;
	rotateMatrixX.m[0][2] = 0.0f;
	rotateMatrixX.m[0][3] = 0.0f;
	rotateMatrixX.m[1][0] = 0.0f;
	rotateMatrixX.m[1][1] = std::cos(rot.x);
	rotateMatrixX.m[1][2] = std::sin(rot.x);
	rotateMatrixX.m[1][3] = 0.0f;
	rotateMatrixX.m[2][0] = 0.0f;
	rotateMatrixX.m[2][1] = -std::sin(rot.x);
	rotateMatrixX.m[2][2] = std::cos(rot.x);
	rotateMatrixX.m[2][3] = 0.0f;
	rotateMatrixX.m[3][0] = 0.0f;
	rotateMatrixX.m[3][1] = 0.0f;
	rotateMatrixX.m[3][2] = 0.0f;
	rotateMatrixX.m[3][3] = 1.0f;

	// Y軸回転行列
	Matrix4x4 rotateMatrixY = {};
	rotateMatrixY.m[0][0] = std::cos(rot.y);
	rotateMatrixY.m[0][1] = 0.0f;
	rotateMatrixY.m[0][2] = -std::sin(rot.y);
	rotateMatrixY.m[0][3] = 0.0f;
	rotateMatrixY.m[1][0] = 0.0f;
	rotateMatrixY.m[1][1] = 1.0f;
	rotateMatrixY.m[1][2] = 0.0f;
	rotateMatrixY.m[1][3] = 0.0f;
	rotateMatrixY.m[2][0] = std::sin(rot.y);
	rotateMatrixY.m[2][1] = 0.0f;
	rotateMatrixY.m[2][2] = std::cos(rot.y);
	rotateMatrixY.m[2][3] = 0.0f;
	rotateMatrixY.m[3][0] = 0.0f;
	rotateMatrixY.m[3][1] = 0.0f;
	rotateMatrixY.m[3][2] = 0.0f;
	rotateMatrixY.m[3][3] = 1.0f;

	// Z軸回転行列
	Matrix4x4 rotateMatrixZ = {};
	rotateMatrixZ.m[0][0] = std::cos(rot.z);
	rotateMatrixZ.m[0][1] = std::sin(rot.z);
	rotateMatrixZ.m[0][2] = 0.0f;
	rotateMatrixZ.m[0][3] = 0.0f;
	rotateMatrixZ.m[1][0] = -std::sin(rot.z);
	rotateMatrixZ.m[1][1] = std::cos(rot.z);
	rotateMatrixZ.m[1][2] = 0.0f;
	rotateMatrixZ.m[1][3] = 0.0f;
	rotateMatrixZ.m[2][0] = 0.0f;
	rotateMatrixZ.m[2][1] = 0.0f;
	rotateMatrixZ.m[2][2] = 1.0f;
	rotateMatrixZ.m[2][3] = 0.0f;
	rotateMatrixZ.m[3][0] = 0.0f;
	rotateMatrixZ.m[3][1] = 0.0f;
	rotateMatrixZ.m[3][2] = 0.0f;
	rotateMatrixZ.m[3][3] = 1.0f;

	// X、Y、Z軸回転行列の合成（Z→Y→X）
	Matrix4x4 rotateMatrixXYZ = {};
	rotateMatrixXYZ = Multiply(rotateMatrixX, Multiply(rotateMatrixY, rotateMatrixZ));

	Matrix4x4 translateMatrix = {};
	translateMatrix.m[0][0] = 1;
	translateMatrix.m[0][1] = 0;
	translateMatrix.m[0][2] = 0;
	translateMatrix.m[0][3] = 0;
	translateMatrix.m[1][0] = 0;
	translateMatrix.m[1][1] = 1;
	translateMatrix.m[1][2] = 0;
	translateMatrix.m[1][3] = 0;
	translateMatrix.m[2][0] = 0;
	translateMatrix.m[2][1] = 0;
	translateMatrix.m[2][2] = 1;
	translateMatrix.m[2][3] = 0;
	translateMatrix.m[3][0] = translate.x;
	translateMatrix.m[3][1] = translate.y;
	translateMatrix.m[3][2] = translate.z;
	translateMatrix.m[3][3] = 1;

	result = Multiply(scaleMatrix, Multiply(rotateMatrixXYZ, translateMatrix));

	return result;
}


void Math::Updata() {

	Matrix4x4 worldMatrix = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);

}
