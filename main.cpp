#include <Windows.h>
#include <cstdint>
#include <format>
#include <string>
// ↓ファイルを作る
// ファイルやディレクトリに関する操作を行うライブラリ
#include <filesystem>
// ファイルに書いたり読んだりするライブラリ
#include <fstream>
#include <sstream>
// 時間を扱うライブラリ
#include <cassert>
#include <chrono>
#include <d3d12.h>
#include <dxgi1_6.h>
// デバッグ用
#include <dbghelp.h>
#include <strsafe.h>
// リソースリークチェック
#include <dxgidebug.h>
// DXC
#include "Math.h"
#include "MathTypes.h"
#include "externals/DirectXTex/Directxtex.h"
#include "externals/DirectXTex/d3dx12.h"
#include <dxcapi.h>
#include <vector>
#include <wrl.h>

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);




#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
// デバッグ用
#pragma comment(lib, "Dbghelp.lib")
// リソースリークチェック
#pragma comment(lib, "dxguid.lib")
// DXC
#pragma comment(lib, "dxcompiler.lib")

struct Vector4 {
	float x, y, z, w;
};

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct Material {
	Vector4 color;
	int32_t enableLighting;
	float paddding[3];
	Matrix4x4 uvTransform;
};

struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

struct DirectionalLight {
	Vector4 color;     // 光の色
	Vector3 direction; // 光の方向
	float intensity;   // 光の強さ
};

// マテリアルデータ構造体
struct MaterialData {
	std::string textureFilePath;
};

// モデルデータ構造体
struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};

double pi = 3.14;

// リソースチェック
struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

Vector3 Normalize(const Vector3& v) {
	float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	if (length == 0.0f)
		return {0.0f, 0.0f, 0.0f};
	return {v.x / length, v.y / length, v.z / length};
}

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = {0};
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();

	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{0};
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);

	return EXCEPTION_EXECUTE_HANDLER;
}

std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

void Log(const std::string& message) { OutputDebugStringA(message.c_str()); }

void Log(std::ostream& os, const std::string& message) {
	os << message << std ::endl;
	OutputDebugStringA(message.c_str());
}

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
	// ウィンドウが破棄された
	case WM_DESTROY:
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}
	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// CompileShader関数
IDxcBlob* CompileShader(
    // CompilerするShederファイルへのパス
    const std::wstring& filePath,
    // Compilerに使用するProfile
    const wchar_t* profile,
    // 初期化で生成したものを3つ
    IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler) {
	// これからシェーダーをコンパイルする旨をログに出す
	Log(ConvertString(std::format(L"Begin compileShader, path:{}, profile:{}\n", filePath, profile)));
	// ======================
	// hlslファイルを読み込む
	// ======================
	Microsoft::WRL::ComPtr <IDxcBlobEncoding> shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	// 読めなかったら止める
	assert(SUCCEEDED(hr));
	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	// UTF8の文字コードであることを通知
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	// ======================
	// Compileする
	// ======================
	LPCWSTR arguments[] = {
	    // コンパイル対象のhlslファイル名
	    filePath.c_str(),
	    // エントリーポイントの指定。基本的にmain以外にはしない
	    L"-E",
	    L"main",
	    // shaderProfileの設定
	    L"-T",
	    profile,
	    // デバッグ用の情報を埋め込む
	    L"-Zi",
	    L"-Qembed_debug",
	    // 最適化を外しておく
	    L"-Od",
	    // メモリレイアウトは行優先
	    L"-Zpr",
	};
	// 実際にShaderをコンパイルする
	Microsoft::WRL::ComPtr <IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler->Compile(
	    // 読み込んだファイル
	    &shaderSourceBuffer,
	    // コンパイルオプション
	    arguments,
	    // コンパイルオプションの数
	    _countof(arguments),
	    // includeが含まれた諸々
	    includeHandler,
	    // コンパイル結果
	    IID_PPV_ARGS(&shaderResult));
	// コンパイルエラーではなくdxcが起動できないほど致命的な状況
	assert(SUCCEEDED(hr));

	// ==================================
	// 警告・エラーが出てないか確認する
	// ==================================
	// 警告・エラーが出ていたらログに出して止める
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());
		// 警告・エラーだめ絶対
		assert(false);
	}

	// ==================================
	// Compile結果を受け取って返す
	// ==================================
	// コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	// 成功したログを出す
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
	// 実行用のバイナリを返却
	return shaderBlob;
}

// ====================================
// Resource作成の関数化
// ====================================
Microsoft::WRL::ComPtr<ID3D12Resource> 
CreateBufferResource(const Microsoft::WRL::ComPtr <ID3D12Device>& device, size_t sizeInBytes) {

	// 頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	// UploadHeapを使う
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	// 頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	// バッファリソース。てぅすちゃの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	// リソースのサイズ。今回はVector4を3頂点分
	vertexResourceDesc.Width = sizeInBytes;
	// バッファの場合はこれらは1にする決まり
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	// 実際に頂点にリソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResource));
	assert(SUCCEEDED(hr));
	return vertexResource;
}

// ======================================
// CreateDescriptorHeapの関数化
// ======================================
// Descriptorはゲームの中に1つだけ
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
CreateDescriptorHeap(const Microsoft::WRL::ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	// ヒープの設定
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}

// ====================================
// Textureデータを読み込む
// ====================================
DirectX::ScratchImage LoadTexture(const std::string& filePath) {
	// テクスチャファイルを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// ミップマップの作製
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	return mipImages;
}

Microsoft::WRL::ComPtr<ID3D12Resource> 
CreateTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, const DirectX::TexMetadata& metadata) {

	// metadataを基にresourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// Resorceを生成してreturnする
	Microsoft::WRL::ComPtr <ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
	    &heapProperties,                // Heapの設定
	    D3D12_HEAP_FLAG_NONE,           // Heapの特殊な設定。特になし。
	    &resourceDesc,                  // Resourceの設定
	    D3D12_RESOURCE_STATE_COPY_DEST, // データ転送される設定
	    nullptr,                        // Clear最適値。使わないのでnullptr
	    IID_PPV_ARGS(&resource));       // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

// TextureResourceにデータを転送する
[[nodiscard]]
    Microsoft::WRL::ComPtr<ID3D12Resource>
    UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>&texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device, ID3D12GraphicsCommandList* commandList) {
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));
	Microsoft::WRL::ComPtr <ID3D12Resource>
	intermediateResource = CreateBufferResource(device, intermediateSize);
	UpdateSubresources(commandList, texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());
	// Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceに変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);
	return intermediateResource;
}

Microsoft::WRL::ComPtr <ID3D12Resource> 
CreateDepthStencilTextureResource(const Microsoft::WRL::ComPtr <ID3D12Device>& device, int32_t width, int32_t height) {
	// DepthStencil用のResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	// Textureの幅
	resourceDesc.Width = width;
	// Textureの高さ
	resourceDesc.Height = height;
	// mipmapの数
	resourceDesc.MipLevels = 1;
	// 奥行き or 配列Textureの配列数
	resourceDesc.DepthOrArraySize = 1;
	// DepthStencilとして利用可能なフォーマット
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	// サンプリングカウント。1固定
	resourceDesc.SampleDesc.Count = 1;
	// 2次元
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	// DepthStencilとして使う通知
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	// VRAM上に作る
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;


	// 深度値のクリア最適化設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f; // 深度値のクリア値。1.0fは最大値
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; //フォーマット。resourceと合わせる

	// Resourceを生成して返す
	Microsoft::WRL::ComPtr <ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
	    &heapProperties,                  // Heapの設定
	    D3D12_HEAP_FLAG_NONE,             // Heapの特殊な設定。特になし。
	    &resourceDesc,                    // Resourceの設定
	    D3D12_RESOURCE_STATE_DEPTH_WRITE, // Depth書き込み用の状態
	    &depthClearValue,                 // Clear最適値
	    IID_PPV_ARGS(&resource));         // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

// mtlファイルを読む関数
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	// 1.中で必要となる変数の宣言
	MaterialData materialData; // 構築するMaterialData
	std::string line;          // ファイルから読み込んだ1行を格納するもの

	// 2.ファイルを開く
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	assert(file.is_open());                             // ファイルが開けなかったら止める

	// 3.実際にファイルを読み、ModelDataを構築していく
	while (std::getline(file, line)) {
		std::string identifier;     // 行の先頭の識別子を格納するもの
		std::istringstream s(line); // 行を分解するためのストリーム
		s >> identifier;            // 先頭の識別子を読む

		// identifierに応じた処理
		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename; // テクスチャファイル名を読み込む
			// 連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename; // テクスチャファイルパスを設定
		}
	}
	// 4.読み込んだマテリアルデータを返す
	return materialData;
}


// objを読む関数
ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
	// 中で必要となる変数の宣言
	ModelData modelData; // 構築するModelData
	std::vector<Vector4> positions;		// 位置
	std::vector<Vector3> normals;   // 法線
	std::vector<Vector2> texcoords;     // テクスチャ座標
	std::string line;					// ファイルから読み込んだ1行を格納するもの

	// ファイルを開く
	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	// 実際にファイルを読み、ModelDataを構築していく
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier; // 先頭の識別子を読む

		// identifilerに応じた処理
		// 頂点位置
		if (identifier == "v") {
			// 位置情報の読み込み
			Vector4 position;		
			s >> position.x >> position.y >> position.z;
			position.x *= -1.0f;
			position.w = 1.0f; // w成分は1.0fにする
			positions.push_back(position);

			// 頂点テクスチャ座標
		} else if (identifier == "vt") {
			// テクスチャ座標の読み込み
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);

			// 頂点法線
		} else if (identifier == "vn") {
			// 法線の読み込み
			Vector3 normal;		
			s >> normal.x >> normal.y >> normal.z;
			normal.x *= -1.0f;
			normals.push_back(normal);

			// 面の情報
		} else if (identifier == "f") {
			VertexData triangle[3];
			// 面は三角形限定。その他は未対応
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				// 頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for (int32_t element = 0; element < 3; ++element) {
					std::string index;

					// 区切りでインデックスを読んでいく
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}

				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				//VertexData vertex = {position, texcoord, normal};
				//modelData.vertices.push_back(vertex);
				triangle[faceVertex] = {position, texcoord, normal};
			}
			// 頂点を逆順で登録することで、周り順を逆にする
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		} else if (identifier == "mtllib") {
			// materialTempLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename; 
			// 基本的にobjファイルと同一階層にmtlは存在させるので、ディレクトリ名とファイル名を残す
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		
		}


	}

file.close();     // ファイルを閉じる
return modelData; // 読み込んだModelDataを返す
}




// CPUの関数
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) 
{ 
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

// GPUの関数
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}


int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	D3DResourceLeakChecker leakChecker;


	CoInitializeEx(0, COINIT_MULTITHREADED);

	// 誰も補足しなかった場合に補足するための関数
	SetUnhandledExceptionFilter(ExportDump);

	// ↓ファイルを作る
	// ログのディレクトリを用意
	std::filesystem::create_directory("logs");

	// 現在時刻を取得(UTC時刻)
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	// ログファイルの名前にコンマ何秒入らないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

	// 日本時間(PCの設定時間)に変換
	std::chrono::zoned_time localTime{std::chrono::current_zone(), nowSeconds};

	// formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);

	// 時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";

	// ファイルを使って書き込み準備
	std::ofstream logStream(logFilePath);

	WNDCLASS wc{};

	wc.lpfnWndProc = WindowProc;

	wc.lpszClassName = L"CG2WnidowClas";

	wc.hInstance = GetModuleHandle(nullptr);

	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClass(&wc);

	// クライアントの領域サイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	// ウィンドウサイズを表す構造体にクラインと領域を入れる
	RECT wrc = {0, 0, kClientWidth, kClientHeight};

	// クライアント領域をもとに実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウの生成
	HWND hwnd = CreateWindow(
	    wc.lpszClassName,     // 利用するクラス名
	    L"CG2",               // タイトルバーの文字
	    WS_OVERLAPPEDWINDOW,  // よく見るウィンドウスタイル
	    CW_USEDEFAULT,        // 表示X座標
	    CW_USEDEFAULT,        // 表示Y座標
	    wrc.right - wrc.left, // ウィンドウ横幅
	    wrc.bottom - wrc.top, // ウィンドウ縦幅
	    nullptr,              // 親ウィンドウハンドル
	    nullptr,              // メニューハンドル
	    wc.hInstance,         // インスタンスハンドル
	    nullptr               // オプション
	);

// ===================================
// エラー表示
// ===================================
#ifdef _DEBUG
	Microsoft::WRL::ComPtr <ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);
	Log(logStream, "HelloDirectX!!!\n");
	Log(logStream, ConvertString(std::format(L"WindowSize : {},{}\n", kClientWidth, kClientHeight)));

	// DXGIファクトリーの生成
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr;
	// 関数が成功したかどうかをSUCCEEDEマクロで判断できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));

	// 使用するアダプタ用の変数。最初にnullptrを入れておく
	Microsoft::WRL::ComPtr <IDXGIAdapter4> useAdapter = nullptr;

	// 良い順にアダプタを頼む
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
		// アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
		assert(SUCCEEDED(hr));
		// ソフトウェアアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			// 採用したアダプタの情報をログに出力。wstringのほうなので注意
			Log(logStream, ConvertString(std::format(L"Use Adapater:{}\n", adapterDesc.Description)));
			break;
		}
		// ソフトウェアアダプタの場合は見なかったことにする
		useAdapter = nullptr;
	}
	// 適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);

	Microsoft::WRL::ComPtr <ID3D12Device> device = nullptr;
	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0};
	const char* featureLevelStrings[] = {"12.2", "12.1", "12.0"};
	// 高い順に生成ができるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device));
		if (SUCCEEDED(hr)) {
			Log(std::format("featureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}
	assert(device != nullptr);
	Log(logStream, ConvertString(L"Complete create D3D12Device!!!\n"));

// ===================================
// エラー表示
// ===================================
#ifdef _DEBUG
	Microsoft::WRL::ComPtr <ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// ヤバイエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// 警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {

		    D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE

		};
		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
	}
#endif

	// コマンドキューを生成する
	Microsoft::WRL::ComPtr <ID3D12CommandQueue> commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	// コマンドキューの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// ===============================
	// 命令保存用のメモリ管理機構
	// ===============================
	// コマンドアロケータを生成する
	Microsoft::WRL::ComPtr <ID3D12CommandAllocator> commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
	// コマンドリストを生成する
	Microsoft::WRL::ComPtr <ID3D12GraphicsCommandList> commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	// コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// =================================
	// モニター表示
	// =================================
	// スワップチェーンを生成する
	Microsoft::WRL::ComPtr <IDXGISwapChain4> swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	// コマンドキュー、ウィンドウハンドル、設定して生成する
	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// ディスクリプタヒープの生成
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	// DSV用のディスクリプタヒープを生成する。数は1つ。DSVはShader内で触るものではないのでShaderVisibleはfalse
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);


	// p12から
	// SwapCainからResourceを引っ張ってくる
	Microsoft::WRL::ComPtr <ID3D12Resource> swapChainResources[2] = {nullptr};
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	// うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	// ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	// RTVを2つ作るのでディスクリプタを2つ用意
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	// 1つ目
	rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
	// 2つ目のディスクリプタハンドルを得る(自力で)
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	// 2つ目
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

	// 初期値0でFenceを作る
	Microsoft::WRL::ComPtr <ID3D12Fence> fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// FenceのSignalを持つためのイベントを作成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);

	// dxCompilerを初期化
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないが、includeに対応するための設定を行っておく
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	// Mathインスタンスの初期化
	// Math* math = new Math();

	// ====================================
	// RootSignature作成
	// ====================================
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// descriptorRange
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	// 0から始まる
	descriptorRange[0].BaseShaderRegister = 0;
	// 数は1つ
	descriptorRange[0].NumDescriptors = 1;
	// SRVを使う
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	// Offsetを自動計算
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// RootParameter作成。複数設定できるので配列。長さ2の配列
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	// CBVを使う
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// PixelShaderで使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	// レジスタ番号0とバインド
	rootParameters[0].Descriptor.ShaderRegister = 0;
	// CBVを使う
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// PixelShaderで使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	// レジスタ番号0とバインド
	rootParameters[1].Descriptor.ShaderRegister = 0;

	// DescriptorTableを使う
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	// PixelShaderで使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	// Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	// Tableで利用する数
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	// CBVを使う
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// PixelShaderで使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	// レジスタ番号1を使う
	rootParameters[3].Descriptor.ShaderRegister = 1;



	// Samplerの設定
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	// バイリニアフィルタ
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	// 0~1の範囲外をリピート
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	// 比較しない
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	// ありったけのMipmapを使う
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	// レジスタ番号0を使う
	staticSamplers[0].ShaderRegister = 0;
	// PixelShaderで使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// ルートパラメータ配列へのポインタ
	descriptionRootSignature.pParameters = rootParameters;
	// 配列の長さ
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	// マテリアル用のリソース

	Microsoft::WRL::ComPtr <ID3D12Resource> materialResource = CreateBufferResource(device, sizeof(Material));

	Material* materialData = nullptr;

	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	materialData->uvTransform = MakeIdentity4x4();

	// WVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr <ID3D12Resource> wvpResource = CreateBufferResource(device, sizeof(TransformationMatrix));
	// データを書き込む
	TransformationMatrix* wvpData = nullptr;
	// 書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	// 単位行列を書き込んでおく
	wvpData->World = MakeIdentity4x4();

	// シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr <ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr <ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	// バイナリを元に生成
	Microsoft::WRL::ComPtr <ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

	// InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};
	// 全ての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// RaisiterzerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	// 裏面(時計回り)を表示しない(D3D12_CULL_MODE_BACK)
	// D3D12_CULL_MODE_NONEで両面表示
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// Shaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3D.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(vertexShaderBlob != nullptr);

	// PixelShaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3D.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(pixelShaderBlob != nullptr);

	// DepthStenicilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	// Depthの機能を有効化にする
	depthStencilDesc.DepthEnable = true;
	// 書き込み
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	
	// ==========================
	// PSOを生成する
	// ==========================
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	// RootSignature
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
	// InputLayout
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	// VertexShader
	graphicsPipelineStateDesc.VS = {vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize()};
	// PixelShader
	graphicsPipelineStateDesc.PS = {pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize()};
	// BlendState
	graphicsPipelineStateDesc.BlendState = blendDesc;
	// RasterrizerState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// 利用するトポロジ(形状)のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// どのように画面に色を打ち込むかの設定(気にしなくていい)
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	// DepthStencilStateの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 実際に生成
	Microsoft::WRL::ComPtr <ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

	// =======================================================================================
	// ================================
	// VertexResourceを生成する
	// ================================
	// 頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	// UploadHeapを使う
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	// 頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	// バッファリソース。テクスチャの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	// リソースのサイズ。今回はVector4を3頂点分
	vertexResourceDesc.Width = sizeof(VertexData) * 3;
	// バッファの場合はこれらは1にする決まり
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	// 実際に頂点にリソースを作る

	// ============================
	// 球
	// ============================
	// 分割数
	const uint32_t kSubdivision = 32;

	uint32_t vertexCount = (kSubdivision + 1) * (kSubdivision + 1);

	// モデル読み込み
	ModelData modelData = LoadObjFile("resources", "plane.obj");
	// 頂点リソースを作る
	Microsoft::WRL::ComPtr <ID3D12Resource> vertexResource = CreateBufferResource(device, sizeof(VertexData) * modelData.vertices.size());
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	// リソースの先頭のアドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点のサイズ
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	// 1頂点当たりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);


	// 頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;
	// 書き込むためのアドレス取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	// 頂点データをリソースにコピー
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());



	//// 球の頂点データを作成する
	//// 経度分割1つ分の角度 φb
	//const float kLonEvery = float(pi * 2.0f / float(kSubdivision));
	//// 緯度分割1つ分の角度 Θb
	//const float kLatEvery = float(pi / float(kSubdivision));
	//
	//
	//// 緯度の方向に分割
	//for (uint32_t latIndex = 0; latIndex < (kSubdivision+1); ++latIndex) {
	//	// 現在の緯度
	//	float lat = -float(pi / 2.0f) + kLatEvery * latIndex;
	//	// 経度の方向に分割 0~2π
	//	for (uint32_t lonIndex = 0; lonIndex < (kSubdivision+1); ++lonIndex) {
	//		// uint32_t index = (latIndex * kSubdivision + lonIndex) * 6;
	//		//  φ
	//		float lon = lonIndex * kLonEvery;
	//
	//		VertexData vertA = {
	//		    {
	//			std::cosf(lat) * std::cosf(lon), 
	//			std::sinf(lat), 
	//			std::cosf(lat) * std::sinf(lon), 
	//			1.0f
	//			},
    //            {
	//			float(lonIndex) / float(kSubdivision),
	//			1.0f - float(latIndex) / float(kSubdivision)
	//			},
    //            {
    //             std::cosf(lat) * std::cosf(lon),
    //             std::sinf(lat),
    //             std::cosf(lat) * std::sinf(lon), 
	//		    }
    //        };
	//
	//		uint32_t start = (latIndex * (kSubdivision+1) + lonIndex);
	//		vertexData[start] = vertA;
	//	}
	//}

	// ============================
	// Sprite
	// ============================

	// DescriptorSizeを取得する
	const uint32_t descriptorSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//const uint32_t descriptorRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//const uint32_t descriptorDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);


	// 頂点にリソースを作る
	Microsoft::WRL::ComPtr <ID3D12Resource> vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * vertexCount);
	assert(SUCCEEDED(hr));

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	// リソースの先頭のアドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点3つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * vertexCount;
	// 1頂点当たりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	// 頂点リソースにデータを書き込む
	VertexData* vertexDataSprite = nullptr;
	// 書き込むためのアドレス取得
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// 無駄なデータを削減
	// 三角形2枚
	// 左下
	vertexDataSprite[0].position = {0.0f, 360.f, 0.0f, 1.0f};
	vertexDataSprite[0].texcoord = {0.0f, 1.0f};
	vertexDataSprite[0].normal = {0.0f,0.0f, 1.0f};
	// 左上
	vertexDataSprite[1].position = {0.0f, 0.0f, 0.0f, 1.0f};
	vertexDataSprite[1].texcoord = {0.0f, 0.0f};
	vertexDataSprite[1].normal = {0.0f, 0.0f, 1.0f};

	// 右下
	vertexDataSprite[2].position = {640.0f, 360.0f, 0.0f, 1.0f};
	vertexDataSprite[2].texcoord = {1.0f, 1.0f};
	vertexDataSprite[2].normal = {0.0f, 0.0f, 1.0f};

	// 右上
	vertexDataSprite[3].position = {640.0f, 0.0f, 0.0f, 1.0f};
	vertexDataSprite[3].texcoord = {1.0f, 0.0f};
	vertexDataSprite[3].normal = {0.0f, 0.0f, 1.0f};


	// Sprite用のTransformMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr <ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix));
	// データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	// 書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	// 単位行列を書き込んでおく
	transformationMatrixDataSprite->WVP = MakeIdentity4x4();

	// =======================================================================================


	// =======================================================================================
	// =================================
	// index
	// =================================
	
	const uint32_t indexCount = kSubdivision * kSubdivision * 6;


	// 球
	Microsoft::WRL::ComPtr <ID3D12Resource> indexResourceSphere = CreateBufferResource(device, sizeof(uint32_t) * indexCount);
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSphere{};
	indexBufferViewSphere.BufferLocation = indexResourceSphere->GetGPUVirtualAddress();
	indexBufferViewSphere.SizeInBytes = sizeof(uint32_t) * indexCount;
	indexBufferViewSphere.Format = DXGI_FORMAT_R32_UINT;
	uint32_t* indexDataSphere = nullptr;
	indexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSphere));

	// インデックスの生成（UVスフィア方式）
	// 注意：頂点の生成コードと同じ subdivision ロジックと対応させる
	uint32_t index = 0;
	for (uint32_t lat = 0; lat < kSubdivision; ++lat) {
		for (uint32_t lon = 0; lon < kSubdivision; ++lon) {
			uint32_t current = lat * (kSubdivision + 1) + lon;
			uint32_t next = current + kSubdivision + 1;

			// 2つの三角形を構成 (quad → 2 triangles)
			indexDataSphere[index++] = current;
			indexDataSphere[index++] = next;
			indexDataSphere[index++] = current + 1;

			indexDataSphere[index++] = current + 1;
			indexDataSphere[index++] = next;
			indexDataSphere[index++] = next + 1;
		}
	}


	// スプライト
	Microsoft::WRL::ComPtr <ID3D12Resource> indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * indexCount);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	// リソースの先頭のアドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズはインデックス6つ分のサイズ
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * indexCount;
	// インデックスはuint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;
	// インデックスリソースにデータを書き込む
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));



	//for (uint32_t latindec = 0;latIndex)

	for (uint32_t lat = 0; lat < kSubdivision; ++lat) {
		for (uint32_t lon = 0; lon < kSubdivision; ++lon) {
			indexDataSprite[0] = 0;
			indexDataSprite[1] = 1;
			indexDataSprite[2] = 2;
			indexDataSprite[3] = 1;
			indexDataSprite[4] = 3;
			indexDataSprite[5] = 2;
		}
	}

	// =======================================================================================



	// ==============================
	// ViewportとScissor
	// ==============================
	// ビューポート
	D3D12_VIEWPORT viewport{};
	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形
	D3D12_RECT scissorRect{};
	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

	Vector3 cameraPosition{0.0f, 0.0f, 0.0f};
	int kwindowWidth = 1280;
	int kwindowHeight = 720;

	// transformの変数を作る
	Transform transform{
	    {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };
	Transform cameraTransform{
	    {1.0f, 1.0f, 1.0f },
        {0.0f, 0.0f, 0.0f },
        {0.0f, 0.0f, -5.0f}
    };

	// SpriteのTransform
	Transform transformSprite{
	    {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };

	Transform uvTransformSprite{
	    {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };

	// imguiの初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(
	    device.Get(), swapChainDesc.BufferCount, rtvDesc.Format, srvDescriptorHeap.Get(), srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

		// 2枚目のTexture
	DirectX::ScratchImage mipImages2 = LoadTexture(modelData.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	Microsoft::WRL::ComPtr <ID3D12Resource> textureResource2 = CreateTextureResource(device, metadata2);
	Microsoft::WRL::ComPtr<ID3D12Resource> val2 = UploadTextureData(textureResource2, mipImages2, device.Get(), commandList.Get());


	// metDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	// 2Dテクスチャ
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSRV, 2);

	// SRVの生成
	device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);



	// 1枚目のTexture
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr <ID3D12Resource> textureResource = CreateTextureResource(device, metadata);
	Microsoft::WRL::ComPtr<ID3D12Resource> val = UploadTextureData(textureResource, mipImages, device.Get(), commandList.Get());
	Microsoft::WRL::ComPtr <ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

	// metDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	// 2Dテクスチャ
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// SRVを作成するDescriptorHeapの場所を決める
	//D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	//D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	//textureSrvHandleCPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//textureSrvHandleGPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSRV, 1);

	// SRVの生成
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	// Format。基本的にはResourceに合わせる
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	// 2Dテクスチャ
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	// DSVHeapの先頭にDSVを作る
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// SRV切り替え
	bool useMonsterBall = true;





// 球用マテリアル（ライティング有効）
	Microsoft::WRL::ComPtr <ID3D12Resource> materialResourceSphere = CreateBufferResource(device, sizeof(Material));
	Material* materialDataSphere = nullptr;
	materialResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSphere));
	materialDataSphere->enableLighting = true;
	materialDataSphere->color = {1.0f, 1.0f, 1.0f, 1.0f};
	materialDataSphere->uvTransform = MakeIdentity4x4();


	// スプライト用マテリアル（ライティング無効）
	Microsoft::WRL::ComPtr <ID3D12Resource> materialResourceSprite = CreateBufferResource(device, sizeof(Material));
	Material* materialDataSprite = nullptr;
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
	materialDataSprite->enableLighting = false;
	materialDataSprite->color = {1.0f, 1.0f, 1.0f, 1.0f}; 
	materialDataSprite->uvTransform = MakeIdentity4x4();

	// liting用のマテリアルを作成
	Microsoft::WRL::ComPtr <ID3D12Resource> directionalLight = CreateBufferResource(device, sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLight->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	directionalLightData->direction = {0.0f, -1.0f, 0.0f};
	directionalLightData->intensity = 1.0f;



	// ==============================
	// ゲームループ
	// ==============================
	MSG msg{};
	// ウィンドウの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT) {
		// Windowにメッセージが着てたら最優先で処理される
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {

			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, static_cast<float>(kwindowWidth) / static_cast<float>(kwindowHeight), 0.1f, 100.0f);

			
			Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 projectiomMatrix = MakePerspectiveFovMatrix(0.45f, static_cast<float>(kwindowWidth) / static_cast<float>(kwindowHeight), 0.1f, 100.0f);
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectiomMatrix));
			wvpData->World = worldMatrix;
			wvpData->WVP = worldViewProjectionMatrix;

			// Sprite用のWorldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectiomMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f,float(kClientWidth),float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectiomMatrixSprite));
			transformationMatrixDataSprite->World = worldMatrixSprite;
			transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;


			Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));
			materialDataSprite->uvTransform = uvTransformMatrix;


			ImGui::Begin("camera");
			ImGui::DragFloat3("Camera.translate", &cameraTransform.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat3("Camera.rotate.", &cameraTransform.rotate.x, 0.01f, -10.0f, 10.0f);
			ImGui::End();

			ImGui::Begin("object");
			ImGui::DragFloat3("rotate.", &transform.rotate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat3("translate.", &transform.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat3("scale.", &transform.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::ColorEdit4("Color", &materialData->color.x);
			ImGui::ColorEdit4("litingColor", &(directionalLightData->color).x);
			ImGui::DragFloat3("litingColor.direction", &(directionalLightData->direction).x,0.01f, -10.0f, 10.0f);
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);
			ImGui::End();

			
			ImGui::Begin("sprite");
			ImGui::DragFloat3("rotate.", &transformSprite.rotate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat3("translate.", &transformSprite.translate.x, 0.1f);
			ImGui::DragFloat3("scale.", &transformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x,0.01f,-10.0f,10.0f);
			ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
			ImGui::End();



			// ゲーム処理

			// 開発用のUIの処理。実際に開発用のUIを出す場合はここをゲーム固有の処理に置き換える
			ImGui::ShowDemoWindow();
			// ImGuiの内部コマンドを生成する
			ImGui::Render();

			// ================================
			// 画面の色を変換
			// ================================
			// これから書き込むバックバッファのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			// TransitionBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};

			// 描画用のDescriptorHeapの設定
			ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap.Get()};

			// 今回のバリアはTransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			// Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			// バリアを張る対象のリソース。現在のバッグバッファに対して行う
			barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();

			// 遷移前(現在)のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			// 遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// 描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			// 描画先のRTVを設定する
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
			// 指定した色で画面全体をクリアする
			float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f};
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

			// 描画用のDescriptorHeap
			commandList->SetDescriptorHeaps(1, descriptorHeaps);

			// 指定した深度で画面全体をクリアにする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);


			// =====================
			// 描画
			// =====================
			

			// =====================================================
			// 球
			// Viewportを設定
			commandList->RSSetViewports(1, &viewport);
			// Scirssorを設定
			commandList->RSSetScissorRects(1, &scissorRect);
			// RootSignatureを設定。PSOに設定しているけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature.Get());
			// PSOを設定
			commandList->SetPipelineState(graphicsPipelineState.Get());
			// VBVを設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

			commandList->IASetIndexBuffer(&indexBufferViewSphere);
			// 形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけばよい
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			// 球のマテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSphere->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
			// lighting用のCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(3, directionalLight->GetGPUVirtualAddress());
			// wvp用のCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
			// SRVのDescriptorTableの戦闘を設定。2はrootParameters[2]である。
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
			// MonsterBall
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);
			// 変数を見て利用するSRV
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);

			
			// 描画。(DrawCall/ドローコール)。3頂点で1つのインスタンス
			commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
			// ======================================================================

			//====================================================
			// スプライト
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
			// IBVを設定
			commandList->IASetIndexBuffer(&indexBufferViewSprite);
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
			//commandList->SetGraphicsRootConstantBufferView(3, directionalLight->GetGPUVirtualAddress()); 
			// spriteを常にuvCheckerにする
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

			// 6個のインデックスを使用し、1つのインスタンスを描画。そのほかは当面0でいい
			commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

			//======================================================================

			// 実際のcommandListのImGuiの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

			// 画面に描く処理はすべて終わり、画面に映すので、状態を遷移
			// 今回はRenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// コマンドリストの内容を確定させる。全てのコマンドを積んでからCloseすること
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// 画面のクリア処理
			// GPUにコマンドリストの実行を行わせる
			ID3D12CommandList* commandLists[] = {commandList.Get()};
			commandQueue->ExecuteCommandLists(1, commandLists);
			// GPUとOSに画面の交換を行うように通知する
			swapChain->Present(1, 0);

			// Fenceの値を更新
			fenceValue++;
			// GPUがここまでたどり着いたときに、Fenceの値を指定した値の代入するようにSignalを送る
			commandQueue->Signal(fence.Get(), fenceValue);

			if (fence->GetCompletedValue() < fenceValue) {

				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				WaitForSingleObject(fenceEvent, INFINITE);
			}
			// 次のフレーム用のコマンドリストを準備
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator.Get(), nullptr);
			assert(SUCCEEDED(hr));
		}
	}

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	// 解放処理





	CloseWindow(hwnd);


	CoUninitialize();
	return 0;
}
