#include <Windows.h>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <format>
#include <dxcapi.h>
#include <dxgidebug.h>
#include <numbers>
#include <wrl.h>

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/DirectXTex/d3dx12.h"
#include "externals/DirectXTex/DirectXTex.h"

#include "structs.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "Logger.h"
#include "ConvertString.h"
#include "math/Vector4.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include "Matrix4x4/calc/matrix4calc.h"
#include "Vector3/calc/vector3calc.h"
#include "Model.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
	// CompilerするShaderファイルへのパス
	const std::wstring& filePath,
	// Compilerに使用するProfile
	const wchar_t* profile,
	//初期化で生成したものを3つ
	const Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils,
	const Microsoft::WRL::ComPtr<IDxcCompiler3>& dxcCompiler,
	const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler
);

// Grobal
Material* materialData = nullptr;
Material* materialDataSprite = nullptr;
DirectionalLight* dirLightData = nullptr;

sTransform transformModel = {};
sTransform transformSprite = {};
sTransform cameraTransform = {};
sTransform uvTransformSprite = { {1.0f, 1.0f, 1.0f} };

float rotateSpeed = {};
bool lightingWindow = {};
bool isDrawSprite = {};
bool isDrawSphere = {1};

ImGuiListData modelList;
ImGuiListData textureList;
ImGuiListData lightingTypeList;

std::vector<ModelScene> modelScenes;
unsigned int numCurrentModelIndex = 4u;
unsigned int numCurrentModelIndexPrev = 0u;

std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> textureSrvHandleCPUs;
std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> textureSrvHandleGPUs;

int numUploadedTexture = 0;
const uint32_t kSubDivision = 16u;
unsigned int vertexCount = 0;
bool isChangedModelSelect = false;

Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(const Microsoft::WRL::ComPtr<ID3D12Device>& _device, size_t _sizeInBytes);
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(const Microsoft::WRL::ComPtr<ID3D12Device>& _device, D3D12_DESCRIPTOR_HEAP_TYPE _heapType, UINT _numDescriptors, bool _shaderVisible);
DirectX::ScratchImage LoadTexture(const std::string _filePath);
Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& _device, const DirectX::TexMetadata& _metadata);
void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& _texture, const DirectX::ScratchImage& _mipImages);
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>&, int32_t _width, int32_t _height);
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& _descriptorHeap, uint32_t _descriptorSize, uint32_t _index);
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& _descriptorHeap, uint32_t _descriptorSize, uint32_t _index);
void ImGuiWindow();
void ImGuiTemplateTransform(const char* _id, float* _scale, float* _rotate, float* _translate);
void CreateNewTexture(const Microsoft::WRL::ComPtr<ID3D12Device>& _device,
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& _srvDescriptorHeap,
	const uint32_t _kDescriptorSizeSRV,
	const char* _path,
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& _textureResources
);
Microsoft::WRL::ComPtr<ID3D12Resource> CreateVertexResource(ID3D12Device* _device);

void BuildSphere(VertexData* _vertexData, uint32_t _subDivision, unsigned int& _vertexCount);

// Windowsアプリでのエントリポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	// リークチェック
	D3DResourceLeakChecker leakCheck;

	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);

	WNDCLASS wc{};
	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名(なんでもおけ)
	wc.lpszClassName = L"CG2WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);

	modelList.label.push_back("Sphere");
	modelList.label.push_back("Plane");
	modelList.label.push_back("Utah Teapot");
	modelList.label.push_back("Stanford Bunny");
	modelList.label.push_back("Fence");
	modelList.numIndex = 0u;


	textureList.label.push_back("[Built-in texture]");
	textureList.label.push_back("uvChecker.png");
	textureList.label.push_back("MonsterBall.png");
	textureList.numIndex = 1u;


	// - - - - - - - - - - - - - - - - - - - - - - - - - - //

	// クライアント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textureResources;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - //

	// ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };

	// クライアント領域をもとに実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	HWND hwnd = CreateWindow(
		wc.lpszClassName,		// 利用するクラス名
		L"CG2",					// タイトルバーの文字
		WS_OVERLAPPEDWINDOW,	// よく見るウィンドウスタイル
		CW_USEDEFAULT,			// 表示X座標(ウィンドウ出現位置？)
		CW_USEDEFAULT,			// 表示Y座標
		wrc.right - wrc.left,	// ウィンドウ横幅
		wrc.bottom - wrc.top,	// ウィンドウ縦幅
		nullptr,				// 親ウィンドウハンドル(子ウィンドウ作るときに使うかも？)
		nullptr,				// メニューハンドル
		wc.hInstance,			// インスタンスハンドル
		nullptr					// オプション
	);
	ShowWindow(hwnd, SW_SHOW);


#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
	{
		// デバッグレイヤーを有効化する
		debugController.Get()->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController.Get()->SetEnableGPUBasedValidation(TRUE);
	}
#endif // _DEBUG

	// DXGIファクトリーの生成
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr;
	// HRESULTはWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	hr = CreateDXGIFactory(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));

	// 使用するアダプタ用の変数. 最初にぬるぽ入れておく
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	// 良い順にアダプタを頼む(性能が良い順？)
	for (UINT i = 0;
		dxgiFactory.Get()->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(useAdapter.GetAddressOf())) != DXGI_ERROR_NOT_FOUND;
		++i)
	{
		// アダプターの情報を取得する Description : 説明, 概要
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter.Get()->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr)); // 取得できねえならやべえ！
		// ソフトウェアアダプタでなければ採用！(仮想GPU?)
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
		{
			// 採用したアダプタの情報をログに出力。 wstringの方なので注意
			Log(std::format("Use Adapter:{}\n", ConvertString(adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr; // ソフトウェアアダプタの場合は見なかったことにする
	}
	// 適切なアダプタが見つからなかったため起動不可
	assert(useAdapter != nullptr);

	/// D3D12Deviceの生成

	Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i)
	{
		// 採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr))
		{
			// 生成できたのでログ出力を行ってループを抜ける
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}
	// デバイスの生成がうまくいかなかったので起動できない
	assert(device != nullptr);
	Log("Complete create D3D12Device!!!\n"); // 初期化完了のログを出力

#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
	{
		// やばいエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時に止まる <- 解放忘れが判明したら、コメントアウト
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		//警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		// 指定したメッセージの表示を制限する
		infoQueue->PushStorageFilter(&filter);
	}
#endif // _DEBUG

	const uint32_t kDescriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t kDescriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t kDescriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// 出力ウィンドウへの文字出力
	Log(std::format("Hello, {}\n", "DirectX!"));

	// コマンドキューを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	// コマンドキューの生成がうまくいかなかったので起動できない。
	assert(SUCCEEDED(hr));
	if (!commandQueue) return -1;

	// コマンドアロケータを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));
	if (!commandAllocator) return -1;

	// コマンドリストを生成する
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	// コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// スワップチェーンを生成する
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;								// 画面の幅。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Height = kClientHeight;							// 画面の高さ。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;				// 色の形式
	swapChainDesc.SampleDesc.Count = 1;								// マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;	// 描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2;									// ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;		// モニタにうつしたら、中身を破棄

	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// ディスクリプタヒープの生成
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// SwapChainからResourceを引っ張ってくる
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	// うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));

	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	// うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// 初期値0でFenceを作る
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントを作成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);

	// dxcCompilerを初期化
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

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0; // 0から始まる
	descriptorRange[0].NumDescriptors = 1; // 数は1つ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

	/// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// RootParameter作成。複数設定できるので配列。今回は結果1つだけなので長さ１の配列
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;	// CBVを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;	// PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0;					// レジスタ番号０とバインド

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;	// CBVを使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;// VertexShaderで使う
	rootParameters[1].Descriptor.ShaderRegister = 0;					// レジスタ番号０とバインド

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange; // Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使用する
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用する
	rootParameters[3].Descriptor.ShaderRegister = 1;					// レジスタ番号1を使用する

	descriptionRootSignature.pParameters = rootParameters;				// ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters);	// 配列の長さ

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // BilinearFilter
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0 ~ 1の範囲外をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; // 比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX; // ありったけのーを使う
	staticSamplers[0].ShaderRegister = 0; // レジスタ番号0を使用する
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderを使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr))
	{
		Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	// バイナリをもとに生成
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

	/// InputLayout
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

	/// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};
	// すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	// 裏面（時計回り）を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	/// ShaderをCompileする
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3D.VS.hlsl",
		L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3D.PS.hlsl",
		L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get());
	assert(pixelShaderBlob != nullptr);

	// DespStencilResource
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);
	// DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないため、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Format。基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2dTexture
	// DSVHeapの戦闘にDSVを作る
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart());
	// DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
	// Depthの機能を有効にする
	depthStencilDesc.DepthEnable = true;
	// 書き込みする
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	/// PSOを生成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();	// RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;	// InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob.Get()->GetBufferPointer(),
	vertexShaderBlob.Get()->GetBufferSize() };						// VertexShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob.Get()->GetBufferPointer(),
	pixelShaderBlob.Get()->GetBufferSize() };							// PixelShader
	graphicsPipelineStateDesc.BlendState = blendDesc;			// BlendState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;	// RasterizerState
	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// 利用するトポロジ（形状）のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// どのように画面に色を打ち込むかの設定（気にしなくて良い）
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	// DepthStencilの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	// 実際に生成
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));
	if (!graphicsPipelineState) return -1;



	/// CreateBuffer --- --- --- --- ---

		// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = CreateBufferResource(device, sizeof(Material));

	/// モデル読み込み	--- --- --- --- ---

	modelScenes.push_back({
		.modelData = {},
		.material = nullptr,
		.selectedTextureIndex = 0
		}
	);
	// 書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&modelScenes.back().material));
	// 白色がデフォルト
	modelScenes.back().material->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	modelScenes.back().material->enableLighting = false;
	modelScenes.back().material->uvTransform = MakeIdentity4x4();


    /// 板ポリゴン
	modelScenes.push_back({
		.modelData = LoadObjFile("resources", "plane.obj"),
		.material = nullptr,
		.selectedTextureIndex = 0
		}
	);
	// 書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&modelScenes.back().material));
	// 白色がデフォルト
	modelScenes.back().material->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	modelScenes.back().material->enableLighting = false;
	modelScenes.back().material->uvTransform = MakeIdentity4x4();


    /// ティーポット
	modelScenes.push_back({
		.modelData = LoadObjFile("resources", "teapot.obj"),
		.material = nullptr,
		.selectedTextureIndex = 0
		}
	);
	// 書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&modelScenes.back().material));
	// 白色がデフォルト
	modelScenes.back().material->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	modelScenes.back().material->enableLighting = false;
	modelScenes.back().material->uvTransform = MakeIdentity4x4();


    /// バニー
	modelScenes.push_back({
	.modelData = LoadObjFile("resources", "bunny.obj"),
	.material = nullptr,
	.selectedTextureIndex = 0
		}
	);
	// 書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&modelScenes.back().material));
	// 白色がデフォルト
	modelScenes.back().material->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	modelScenes.back().material->enableLighting = false;
	modelScenes.back().material->uvTransform = MakeIdentity4x4();


    /// フェンス
	modelScenes.push_back({
	.modelData = LoadObjFile("resources", "fence.obj"),
	.material = nullptr,
	.selectedTextureIndex = 0
		}
	);
	// 書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&modelScenes.back().material));
	// 白色がデフォルト
	modelScenes.back().material->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	modelScenes.back().material->enableLighting = false;
	modelScenes.back().material->uvTransform = MakeIdentity4x4();


	// WVP用のリソースを作る。Matrix4x4 一つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = CreateBufferResource(device, sizeof(TransformationMatrix));
	// データを書き込む
	TransformationMatrix* wvpData = nullptr;
	// 書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	// 単位行列を書き込んでおく
	wvpData->WVP = MakeIdentity4x4();

	modelScenes.back().selectedTextureIndex = modelList.numIndex;
	if (numCurrentModelIndex) // != 0
		modelScenes[numCurrentModelIndex].material->color = modelScenes[numCurrentModelIndex].modelData.materialData.diffuse;

	Microsoft::WRL::ComPtr vertexResource = CreateVertexResource(device.Get());

	// 頂点バッファービューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * vertexCount);
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	Microsoft::WRL::ComPtr<ID3D12Resource> dirLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	dirLightResource->Map(0, nullptr, reinterpret_cast<void**>(&dirLightData));

	dirLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	dirLightData->direction = { 0.0f, -1.0f, 0.0f };
	dirLightData->intensity = 1.0f;

	/// Model - --- --- --- ---
	/// -- --- --- --- --- --- ---
	/// Sprite --- --- --- --- ---

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = CreateBufferResource(device, sizeof(Material));

	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));

	materialDataSprite->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialDataSprite->enableLighting = false;
	materialDataSprite->uvTransform = MakeIdentity4x4();

	// 頂点リソースを作成
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);
	// IBVの作成
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	// リソースの先頭のアドレスから使用する
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズはインデックス6つ分のサイズ
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	// インデックスはint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;	indexDataSprite[1] = 1;	indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;

	// Sprite用の頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 4);
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite = {};
	// リソースの先頭のアドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点６つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;
	// 1頂点あたりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	// 頂点データを設定する
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
	// 1枚目の三角形
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f }; // 左下
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f }; // 左上
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f }; // 右下
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[3].position = { 640.0f, 0.0f, 0.0f, 1.0f }; // 右上
	vertexDataSprite[3].texcoord = { 1.0f, 0.0f };

	// Sprite用のTransformMatrix用のリソースを作る。Matrix4x4の1つ分サイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix)); // TransformationMatrix
	// データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	// 単位行列を書き込んでおく
	transformationMatrixDataSprite->WVP = MakeIdentity4x4();
	transformSprite =
	{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};

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

	// Transform変数
	transformModel =
	{
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{-0.8f, 0.0f, 0.0f}
	};
	cameraTransform =
	{
		{1.f, 1.0f, 1.0f},
		{0.65f, 0.0f, 0.0f},
		{0.0f, 8.f, -10.0f}
	};

	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);


#pragma region テクスチャを読み込む

	// SRVを作成するDescriptorHeapの場所を決める

	for (int i = 0; i < textureList.label.size(); i++)
	{
		std::string texturePath;
		if (i == 0) // [Built-in texture]
		{
			texturePath = modelScenes[numCurrentModelIndex].modelData.materialData.textureFilePath;
		}
		else // その他
		{
			std::string textureName = textureList.label[i];
			texturePath = "Resources/" + textureName;
		}
		if (i == 0 && numCurrentModelIndex == 0)
		{
			std::string textureName = textureList.label[1];
			texturePath = "Resources/" + textureName;
		}


		CreateNewTexture(device, srvDescriptorHeap, kDescriptorSizeSRV, texturePath.c_str(), textureResources);
	}

#pragma endregion

#pragma region ImGui Initialize

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(
		device.Get(),
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
	);

#pragma endregion

	MSG msg{}; // MSG構造体 OSから受け取る？
	while (msg.message != WM_QUIT)
	{
		// Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);	// メッセージを解釈？
			DispatchMessage(&msg);	// デキュー的な？
		}
		else
		{
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			// ゲームの処理
			transformModel.rotate.y += rotateSpeed;


			// WVPMatrixの作成・更新
			Matrix4x4 worldMatrix = MakeAffineMatrix(transformModel.scale, transformModel.rotate, transformModel.translate);
			Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 worldViewProjectionMatrix =
				Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			wvpData->WVP = worldViewProjectionMatrix;
			wvpData->World = worldMatrix;

			// Sprite用WVPMatrixの作成
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 WVPMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
			transformationMatrixDataSprite->WVP = WVPMatrixSprite;

			Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));
			materialDataSprite->uvTransform = uvTransformMatrix;

			ImGuiWindow();

			if (isChangedModelSelect)
			{
				vertexResource = CreateVertexResource(device.Get());
				vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
				vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * vertexCount);
				vertexBufferView.StrideInBytes = sizeof(VertexData);
				modelScenes[numCurrentModelIndexPrev].selectedTextureIndex = textureList.numIndex;
				textureList.numIndex = modelScenes[numCurrentModelIndex].selectedTextureIndex;
				if (modelList.numIndex == 1) transformModel.rotate.y = 3.130f;
                if (modelList.numIndex == 4) transformModel.rotate.y = 3.130f;
			}

			// ディスクリプタの先頭を取得する
			D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			// RTVを2つ作るのでディスクリプタを2つ用意
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
			// まず1つ目を作る。1つ目は最初のところに作る。作る場所をこちらで指定する必要がある
			rtvHandles[0] = rtvStartHandle;
			device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
			// 2つ目のディスクリプタハンドルを得る（自力で）
			rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			// 2つ目を作る
			device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

			// これから買い込むバックバッファのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			// TransitionBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};
			// 今回のバリアはTransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			// Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			// バリアを張る対象のリソース。現在のバックバッファに対して行う
			barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
			// 遷移前（現在）のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			// 遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// 描画先のRTVを設定する
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);
			// 指定した色で画面全体をクリアする
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; // 青っぽい色。RGBAの順
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeaps[] = { srvDescriptorHeap.Get() };
			commandList->SetDescriptorHeaps(1, descriptorHeaps->GetAddressOf());

			commandList->RSSetViewports(1, &viewport);			// Viewportを設定
			commandList->RSSetScissorRects(1, &scissorRect);	// Scissorを設定
			// RootSignatureを設定。PSOに設定しているけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(graphicsPipelineState.Get());		// PSOを設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);	// VBVを設定
			// 形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけば良い
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			// マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
			// wvp用CBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

			//if (selectedIndexTextureNameList == 0){}
			//else if(selectedIndexTextureNameList == 1) commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);
			//else if(selectedIndexTextureNameList == 2) commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPUs[textureList.numIndex]);

			commandList->SetGraphicsRootConstantBufferView(3, dirLightResource->GetGPUVirtualAddress());


			// 描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
			// 指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			// 描画！（DrawCall/ドローコール）。頂点
			if (isDrawSphere)
				commandList->DrawInstanced(vertexCount, 1, 0, 0);

			// Spriteの描画。変更が必要なものだけ変更する。
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
			commandList->IASetIndexBuffer(&indexBufferViewSprite); // IBVを設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
			// TransformationMatrixCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
			// SRVの設定
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPUs[1]);

			// 描画！（DrawCall/ドローコール）。スプライト
			if (isDrawSprite)
				commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

			// ImGuiの描画
			ImGui::Render();

			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

			// 画面に描く処理は全て終わり、画面に映すので、状態を遷移
			// 今回はRenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// GPUにコマンドリストの実行を行わせる
			Microsoft::WRL::ComPtr<ID3D12CommandList> commandLists[] = { commandList.Get() };
			commandQueue->ExecuteCommandLists(1, commandLists->GetAddressOf());
			// GPUとISに画面の交換を行うよう通知する
			swapChain->Present(1, 0);

			// Fenceの値を更新
			fenceValue++;

			// GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
			commandQueue->Signal(fence.Get(), fenceValue);

			// Fenceの値が指定したSignal値にたどり着いているか確認する
			// GetCompletedValueの初期値はFence作成時に渡した初期値
			if (fence->GetCompletedValue() < fenceValue)
			{
				// 指定したSignalにたどり着いていないので、たどり着くまで待つようにイベントを設定する
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				// イベントを待つ
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

	CloseHandle(fenceEvent);

	CloseWindow(hwnd);


	CoUninitialize();
	return 0;
}

LRESULT WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{

	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
	{
		return true;
	}

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg)
	{
	case WM_DESTROY:	// ウィンドウが破壊された
		// OSに対して、アプリ終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);

}

Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile, const Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils, const Microsoft::WRL::ComPtr<IDxcCompiler3>& dxcCompiler, const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler)
{
	/// 1. hlslファイルを読み込む

	// これからシェーダーをコンパイルする旨をログに出す
	Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));
	// hlslファイルを読む
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	// 読めなかったら止める
	assert(SUCCEEDED(hr));
	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8; // UTF-8の文字コードであることを通知

	/// 2. Compileする
	LPCWSTR arguments[] = {
		filePath.c_str(),			// コンパイル対象のhlslファイル名
		L"-E", L"main",				// エントリーポイントの指定。基本的にmain以外にはしない
		L"-T", profile,				// ShaderProfileの設定
		L"-Zi", L"-Qembed_debug",	// デバッグ用の情報を埋め込む
		L"-Od",						// 最適化を外しておく
		L"-Zpr",					// メモリレイアウトは行優先
	};
	// 実際にShaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,		// 読み込んだファイル
		arguments,					// コンパイルオプション
		_countof(arguments),		// コンパイルオプションの数
		includeHandler.Get(),				// includeが含まれた諸々
		IID_PPV_ARGS(&shaderResult)	// コンパイル結果
	);
	// コンパイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	/// 3. 警告・エラーが出ていないか確認する
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0)
	{
		Log(shaderError->GetStringPointer());
		// 警告・エラーダメゼッタイ
		assert(false);
	}

	/// 4. Compile結果を受け取って返す

	// コンパイル結果から実行用のバイナリ部分を取得
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	// 成功したログを出す
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
	// 実行用のバイナリを返却
	return shaderBlob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(const Microsoft::WRL::ComPtr<ID3D12Device>& _device, size_t _sizeInBytes)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> result = nullptr;
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = _sizeInBytes;
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	HRESULT hr = _device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&result));
	assert(SUCCEEDED(hr));
	return result;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(const Microsoft::WRL::ComPtr<ID3D12Device>& _device, D3D12_DESCRIPTOR_HEAP_TYPE _heapType, UINT _numDescriptors, bool _shaderVisible)
{
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = _heapType;
	descriptorHeapDesc.NumDescriptors = _numDescriptors;
	descriptorHeapDesc.Flags = _shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = _device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	return descriptorHeap;
}

DirectX::ScratchImage LoadTexture(const std::string _filePath)
{
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(_filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	return mipImages;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& _device, const DirectX::TexMetadata& _metadata)
{
	// metadataをもとにResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(_metadata.width);
	resourceDesc.Height = UINT(_metadata.height);
	resourceDesc.MipLevels = UINT16(_metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(_metadata.arraySize);
	resourceDesc.Format = _metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(_metadata.dimension);

	// 利用するHeapの設定。非常に特殊な運用。 02_04exで一般的なケース版がある
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	// Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = _device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& _texture, const DirectX::ScratchImage& _mipImages)
{
	// Meta情報を取得
	const DirectX::TexMetadata& metadata = _mipImages.GetMetadata();
	// 全MipMapについて
	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel)
	{
		// MipMapLevelを指定して各Imageを取得
		const DirectX::Image* img = _mipImages.GetImage(mipLevel, 0, 0);
		// Textureに転送
		HRESULT hr = _texture->WriteToSubresource(
			UINT(mipLevel),
			nullptr,
			img->pixels,
			UINT(img->rowPitch),
			UINT(img->slicePitch)
		);
		assert(SUCCEEDED(hr));
	}
}
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& _device, int32_t _width, int32_t _height)
{
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = _width;
	resourceDesc.Height = _height;
	resourceDesc.MipLevels = 1; // mipmapの数
	resourceDesc.DepthOrArraySize = 1;	// 奥行き or 配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAMに

	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f; // 1.0f(最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

	// Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = _device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));


	return resource;
}
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& _descriptorHeap, uint32_t _descriptorSize, uint32_t _index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = _descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (_descriptorSize * _index); // ポインタをヒープの始めからインデックス分インクリメント
	return handleCPU;
}
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& _descriptorHeap, uint32_t _descriptorSize, uint32_t _index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = _descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (_descriptorSize * _index);
	return handleGPU;
}

void ImGuiSettingBegin();
void ImGuiSettingEnd();
void ImGuiWindow()
{
	ImGui::SetNextWindowPos(ImVec2(980, 0));
	ImGui::SetNextWindowSize(ImVec2(300, 720));
	ImGuiSettingBegin();
	int windowflags = 0;
	windowflags |= ImGuiWindowFlags_NoResize;
	windowflags |= ImGuiWindowFlags_NoTitleBar;
	ImGui::Begin("Dev", (bool*)false, windowflags);
	/// begin

	ImGui::Text("Model");
	{
		std::string combined;
		for (const char* str : modelList.label)
		{
			combined += str;
			combined += '\0';
		}
		ImGui::Combo("### Model", reinterpret_cast<int*>(&modelList.numIndex), combined.c_str(), static_cast<int>(modelList.label.size()));
		if (modelList.numIndex != numCurrentModelIndex)
		{
			isChangedModelSelect = true;
			numCurrentModelIndexPrev = numCurrentModelIndex;
			numCurrentModelIndex = modelList.numIndex;
		}
		else isChangedModelSelect = false;
	}
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::BeginTabBar("DEVWINDOW_TABBER");

	if (ImGui::BeginTabItem("Model"))
	{
		ImGui::PushID("MODEL_TABITEM");
		ImGui::Spacing();

		ImGui::Checkbox("Enable", &isDrawSphere);
		ImGui::Spacing();

		if(ImGui::CollapsingHeader("Transform"))
		{
			ImGuiTemplateTransform("SPHERE_TRANSFORM", &transformModel.scale.x, &transformModel.rotate.x, &transformModel.translate.x);
			ImGui::DragFloat("Rotate Speed", &rotateSpeed, 0.001f);
		}
		ImGui::Spacing();

		if(ImGui::CollapsingHeader("Material"))
		{
			ImGui::Spacing();
			ImGui::PushID("SPHERE_MATERIAL");
			ImGui::ColorEdit4("Color", &modelScenes[numCurrentModelIndex].material->color.x);
			if (ImGui::Button("Select Texture"))
				ImGui::OpenPopup("SELECT_TEXTURE");
			if (ImGui::BeginPopup("SELECT_TEXTURE"))
			{
				ImGui::BeginListBox("Texture List", ImVec2(150, 60));
				for (int i = 0; i < textureList.label.size(); i++)
				{
					const bool isSelect = textureList.numIndex == i;
					if (ImGui::Selectable(textureList.label[i], isSelect))
					{
						textureList.numIndex = i;
					}

					if (isSelect)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndListBox();

				ImGui::EndPopup();
			}

			ImGui::PopID();
		}
		ImGui::Spacing();

		if(ImGui::CollapsingHeader("Lighting"))
		{
			ImGui::Spacing();
			ImGui::PushID("SPHERE_LIGHTING");
			ImGui::Checkbox("Enable Lighting", reinterpret_cast<bool*>(&modelScenes[numCurrentModelIndex].material->enableLighting));
			ImGui::Combo("Type", reinterpret_cast<int*>(&lightingTypeList.numIndex), "Lambertian Reflectance\0Half Lambert\0", 2);
			if (lightingTypeList.numIndex == 0) modelScenes[numCurrentModelIndex].material->lightingType = LightingType::LambertianReflectance;
			else if (lightingTypeList.numIndex == 1) modelScenes[numCurrentModelIndex].material->lightingType = LightingType::HarfLambert;
			if (ImGui::DragFloat3("Direction", &dirLightData->direction.x, 0.01f))
			{
				dirLightData->direction = Normalize(dirLightData->direction);
			}
			ImGui::ColorEdit4("Color", &dirLightData->color.x);
			ImGui::DragFloat("Intensity", &dirLightData->intensity, 0.01f);
			ImGui::PopID();
		}
		ImGui::Spacing();

		ImGui::PopID();
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Sprite"))
	{
		ImGui::PushID("SPRITE_TABITEM");
		ImGui::Spacing();

		ImGui::Checkbox("Enable", &isDrawSprite);
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Transform"))
		{
			ImGui::Spacing();
			ImGui::DragFloat3("Scale", &transformSprite.scale.x, 0.01f);
			ImGui::DragFloat3("Rotate", &transformSprite.rotate.x, 0.01f);
			ImGui::DragFloat3("Translate", &transformSprite.translate.x, 1.0f);
		}
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("UV Transform"))
		{
			ImGui::Spacing();
			ImGui::PushID("UV_TRANSFORM");
			ImGui::DragFloat2("Scale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("Rotate", &uvTransformSprite.rotate.z);
			ImGui::DragFloat2("Translate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::PopID();
		}
		ImGui::Spacing();

		ImGui::PopID();
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Camera"))
	{
		ImGui::PushID("CAMERA_TABITEM");
		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Transform"))
		{
			ImGui::DragFloat3("Rotate", &cameraTransform.rotate.x, 0.01f);
			ImGui::DragFloat3("Translate", &cameraTransform.translate.x, 0.01f);
		}
		ImGui::PopID();

		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();

	/// end
	ImGuiSettingEnd();
	ImGui::End();
}

void ImGuiSettingBegin()
{
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImU32(0xce5037ff));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImU32(0xff050505));
	ImGui::PushStyleColor(ImGuiCol_Header, ImU32(0xff551906));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImU32(0xff440606));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImU32(0xff250606));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
}

void ImGuiSettingEnd()
{
	ImGui::PopStyleColor(5);
	ImGui::PopStyleVar();
}

void ImGuiTemplateTransform(const char* _id, float* _scale, float* _rotate, float* _translate)
{
	ImGui::PushID(_id);
	ImGui::Spacing();
	ImGui::DragFloat3("Scale", _scale, 0.01f);
	ImGui::DragFloat3("Rotate", _rotate, 0.01f);
	ImGui::DragFloat3("Translate", _translate, 0.01f);
	ImGui::Spacing();
	ImGui::PopID();
}

void CreateNewTexture(const Microsoft::WRL::ComPtr<ID3D12Device>& _device,
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& _srvDescriptorHeap,
	const uint32_t _kDescriptorSizeSRV,
	const char* _path,
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& _textureResources)
{
	DirectX::ScratchImage mipImage = LoadTexture(_path);
	const DirectX::TexMetadata& metadata = mipImage.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = CreateTextureResource(_device, metadata);
	_textureResources.push_back(textureResource);
	UploadTextureData(textureResource, mipImage);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	numUploadedTexture++;
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(_srvDescriptorHeap, _kDescriptorSizeSRV, numUploadedTexture);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(_srvDescriptorHeap, _kDescriptorSizeSRV, numUploadedTexture);
	textureSrvHandleCPUs.push_back(textureSrvHandleCPU);
	textureSrvHandleGPUs.push_back(textureSrvHandleGPU);
	_device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);
	return;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateVertexResource(ID3D12Device* _device)
{
	if (numCurrentModelIndex == 0)
	{
		vertexCount = kSubDivision * kSubDivision * 6;
	}
	else
	{
		vertexCount = static_cast<unsigned int>(modelScenes[numCurrentModelIndex].modelData.vertices.size());
	}

	// 頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = CreateBufferResource(_device, sizeof(VertexData) * vertexCount);

	// 頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	if (numCurrentModelIndex == 0)
	{
		BuildSphere(vertexData, kSubDivision, vertexCount);
	}
	else
	{
		vertexCount = static_cast<unsigned int>(modelScenes[numCurrentModelIndex].modelData.vertices.size());
		std::memcpy(vertexData, modelScenes[numCurrentModelIndex].modelData.vertices.data(), sizeof(VertexData) * vertexCount);
	}

	return vertexResource;
}

void BuildSphere(VertexData* _vertexData, uint32_t _subDivision, unsigned int& _vertexCount)
{
	_vertexCount = _subDivision * _subDivision * 6;
	// 経度分割1つ分の角度
	const float kLonEvery = std::numbers::pi_v<float> *2.0f / float(_subDivision);
	// 緯度分割1つ分の角度
	const float kLatEvery = std::numbers::pi_v<float> / float(_subDivision);
	uint32_t startIndex = 0;
	// 緯度の方向に分割
	for (uint32_t latIndex = 0; latIndex < _subDivision; ++latIndex)
	{
		float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * latIndex;
		// 経度の方向に分割しながら線を描く
		for (uint32_t lonIndex = 0; lonIndex < _subDivision; ++lonIndex)
		{
			float lon = lonIndex * kLonEvery;
			float u, v;

			// 頂点にデータを入力する。基準点a
			_vertexData[startIndex].position.x = std::cosf(lat) * std::cosf(lon);
			_vertexData[startIndex].position.y = std::sinf(lat);
			_vertexData[startIndex].position.z = std::cosf(lat) * std::sinf(lon);
			_vertexData[startIndex].position.w = 1.0f;
			_vertexData[startIndex].normal.x = _vertexData[startIndex].position.x;
			_vertexData[startIndex].normal.y = _vertexData[startIndex].position.y;
			_vertexData[startIndex].normal.z = _vertexData[startIndex].position.z;
			u = float(lonIndex) / float(_subDivision);
			v = 1.0f - float(latIndex) / float(_subDivision);
			_vertexData[startIndex++].texcoord = { u, v };
			// b
			_vertexData[startIndex].position.x = std::cosf(lat + kLatEvery) * std::cosf(lon);
			_vertexData[startIndex].position.y = std::sinf(lat + kLatEvery);
			_vertexData[startIndex].position.z = std::cosf(lat + kLatEvery) * std::sinf(lon);
			_vertexData[startIndex].position.w = 1.0f;
			_vertexData[startIndex].normal.x = _vertexData[startIndex].position.x;
			_vertexData[startIndex].normal.y = _vertexData[startIndex].position.y;
			_vertexData[startIndex].normal.z = _vertexData[startIndex].position.z;
			u = float(lonIndex) / float(_subDivision);
			v = 1.0f - float(latIndex + 1) / float(_subDivision);
			_vertexData[startIndex++].texcoord = { u, v };
			// c
			_vertexData[startIndex].position.x = std::cosf(lat) * std::cosf(lon + kLonEvery);
			_vertexData[startIndex].position.y = std::sinf(lat);
			_vertexData[startIndex].position.z = std::cosf(lat) * std::sinf(lon + kLonEvery);
			_vertexData[startIndex].position.w = 1.0f;
			_vertexData[startIndex].normal.x = _vertexData[startIndex].position.x;
			_vertexData[startIndex].normal.y = _vertexData[startIndex].position.y;
			_vertexData[startIndex].normal.z = _vertexData[startIndex].position.z;
			u = float(lonIndex + 1) / float(_subDivision);
			v = 1.0f - float(latIndex) / float(_subDivision);
			_vertexData[startIndex++].texcoord = { u, v };

			// d
			_vertexData[startIndex].position.x = std::cosf(lat + kLatEvery) * std::cosf(lon + kLonEvery);
			_vertexData[startIndex].position.y = std::sinf(lat + kLatEvery);
			_vertexData[startIndex].position.z = std::cosf(lat + kLatEvery) * std::sinf(lon + kLonEvery);
			_vertexData[startIndex].position.w = 1.0f;
			_vertexData[startIndex].normal.x = _vertexData[startIndex].position.x;
			_vertexData[startIndex].normal.y = _vertexData[startIndex].position.y;
			_vertexData[startIndex].normal.z = _vertexData[startIndex].position.z;
			u = float(lonIndex + 1) / float(_subDivision);
			v = 1.0f - float(latIndex + 1) / float(_subDivision);
			_vertexData[startIndex++].texcoord = { u, v };
			// c2
			_vertexData[startIndex].position.x = std::cosf(lat) * std::cosf(lon + kLonEvery);
			_vertexData[startIndex].position.y = std::sinf(lat);
			_vertexData[startIndex].position.z = std::cosf(lat) * std::sinf(lon + kLonEvery);
			_vertexData[startIndex].position.w = 1.0f;
			_vertexData[startIndex].normal.x = _vertexData[startIndex].position.x;
			_vertexData[startIndex].normal.y = _vertexData[startIndex].position.y;
			_vertexData[startIndex].normal.z = _vertexData[startIndex].position.z;
			u = float(lonIndex + 1) / float(_subDivision);
			v = 1.0f - float(latIndex) / float(_subDivision);
			_vertexData[startIndex++].texcoord = { u, v };
			// b2
			_vertexData[startIndex].position.x = std::cosf(lat + kLatEvery) * std::cosf(lon);
			_vertexData[startIndex].position.y = std::sinf(lat + kLatEvery);
			_vertexData[startIndex].position.z = std::cosf(lat + kLatEvery) * std::sinf(lon);
			_vertexData[startIndex].position.w = 1.0f;
			_vertexData[startIndex].normal.x = _vertexData[startIndex].position.x;
			_vertexData[startIndex].normal.y = _vertexData[startIndex].position.y;
			_vertexData[startIndex].normal.z = _vertexData[startIndex].position.z;
			u = float(lonIndex) / float(_subDivision);
			v = 1.0f - float(latIndex + 1) / float(_subDivision);
			_vertexData[startIndex++].texcoord = { u, v };
		}
	}
}
