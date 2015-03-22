//--------------------------------------------------------------------------------------
// File: Tutorial05.cpp
//
// This application demonstrates animation using matrix transformations
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
// Modified by shobomaru.
//--------------------------------------------------------------------------------------
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include "resource.h"

using namespace DirectX;

#include <intrin.h>
#include <stdexcept>
#include <Shlwapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Mferror.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Mfuuid.lib")

using namespace std;

//! \brief COM smart pointer
template<typename T>
class com_ptr
{
	static_assert(is_base_of<IUnknown, T>::value, "T is not a COM.");
	using this_type = com_ptr<T>;
	T* mPtr = nullptr;
public:
	com_ptr()					{}
	com_ptr(T* p)				{ mPtr = p; }
	~com_ptr()					{ release(); }
	void release()
	{
		if(mPtr) {
			auto t = mPtr;
			mPtr = nullptr;
			t->Release();
		}
	}
	T*& get()					{ return mPtr; }
	T* operator->() const		{ return mPtr; }
	this_type& operator=(T* p)
	{
		release();
		mPtr = p;
		return *this;
	 }
	this_type& operator=(this_type&) = delete;
};

void CHK(HRESULT hr)
{
	if(FAILED(hr))
		throw runtime_error("");
}

template<typename T>
void CHK(T* p)
{
	if(FAILED(hr))
		throw runtime_error("HRESULT failed.");
}

template<typename T>
void CHK(com_ptr<T>& p)
{
	if(p.get() == nullptr)
		throw runtime_error("Nullptr.");
}

//! \brief Movie writer
class movie_writer
{
	com_ptr<IStream> mComStream;
	com_ptr<IMFByteStream> mOutputStream;
	com_ptr<IMFAttributes> mOutputAttr;
	com_ptr<IMFSinkWriter> mSinkWriter;
	DWORD mStreamIndex;
	com_ptr<IMFMediaType> mOutputType;
	com_ptr<IMFMediaType> mInputType;
	unsigned int mFrameSize;

	com_ptr<IMFMediaBuffer> mBuffer;
	UINT64 mTotalTime = 0;
public:
	movie_writer(const TCHAR* path,
				unsigned int width,
				unsigned int height,
				unsigned int frameRate)
	{
		CHK(CoInitialize(nullptr));
		CHK(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
		//CHK(mComStream = SHCreateMemStream(nullptr, 0));
		//CHK(MFCreateMFByteStreamOnStream(mComStream.get(), &mOutputStream.get()));
		CHK(MFCreateAttributes(&mOutputAttr.get(), 10));
		CHK(mOutputAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
		CHK(mOutputAttr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
		CHK(MFCreateSinkWriterFromURL(path, nullptr, mOutputAttr.get(), &mSinkWriter.get()));
		CHK(MFCreateMediaType(&mOutputType.get()));
		CHK(mOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
		CHK(mOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
		CHK(mOutputType->SetUINT32(MF_MT_AVG_BITRATE, 1 * 1024 * 1024));
		CHK(mOutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
		CHK(MFSetAttributeSize(mOutputType.get(), MF_MT_FRAME_SIZE, width, height));
		CHK(MFSetAttributeRatio(mOutputType.get(), MF_MT_FRAME_RATE, frameRate, 1));
		CHK(MFSetAttributeRatio(mOutputType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
		CHK(mSinkWriter->AddStream(mOutputType.get(), &mStreamIndex));
		mOutputType.release();
		CHK(MFCreateMediaType(&mInputType.get()));
		CHK(mInputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
		CHK(mInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
		CHK(mInputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
		CHK(MFSetAttributeSize(mInputType.get(), MF_MT_FRAME_SIZE, width, height));
		CHK(MFSetAttributeRatio(mInputType.get(), MF_MT_FRAME_RATE, frameRate, 1));
		CHK(MFSetAttributeRatio(mInputType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
		CHK(mSinkWriter->SetInputMediaType(mStreamIndex, mInputType.get(), nullptr));
		mInputType.release();
		mFrameSize = 4 * width * height;
		CHK(MFCreateMemoryBuffer(mFrameSize, &mBuffer.get()));
		CHK(mSinkWriter->BeginWriting());
	}
	~movie_writer()
	{
	}
	void write(const char* data, UINT64 duration)
	{
		mBuffer.release();
		CHK(MFCreateMemoryBuffer(mFrameSize, &mBuffer.get()));
		BYTE* destPtr;
		CHK(mBuffer->Lock(&destPtr, nullptr, nullptr));
		if (reinterpret_cast<INT_PTR>(destPtr) % 16 != 0)
			throw runtime_error("Alignment error.");
#if 0
		for(auto i = 0u; i < mFrameSize; i += 16)
		{
			auto d = _mm_lddqu_si128(reinterpret_cast<const __m128i*>(data + i));
			_mm_stream_si128(reinterpret_cast<__m128i*>(destPtr + i), d);
		}
		_mm_mfence();
#else
		memcpy(destPtr, data, mFrameSize);
#endif
		CHK(mBuffer->Unlock());
		CHK(mBuffer->SetCurrentLength(mFrameSize));
		
		com_ptr<IMFSample> sample;
		CHK(MFCreateSample(&sample.get()));
		CHK(sample->AddBuffer(mBuffer.get()));
		CHK(sample->SetSampleTime(mTotalTime));
		CHK(sample->SetSampleDuration(duration));
		mTotalTime += duration;
		CHK(mSinkWriter->WriteSample(mStreamIndex, sample.get()));
	}
	void finalize()
	{
		CHK(mSinkWriter->Flush(mStreamIndex));
		CHK(mSinkWriter->Finalize());
		mTotalTime = 0;
	}
};


//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct SimpleVertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};


struct ConstantBuffer
{
	XMMATRIX mWorld;
	XMMATRIX mView;
	XMMATRIX mProjection;
};


//--------------------------------------------------------------------------------------
// Global Variables
//--------------------------------------------------------------------------------------
HINSTANCE               g_hInst = NULL;
HWND                    g_hWnd = NULL;
D3D_DRIVER_TYPE         g_driverType = D3D_DRIVER_TYPE_NULL;
D3D_FEATURE_LEVEL       g_featureLevel = D3D_FEATURE_LEVEL_11_0;
ID3D11Device*           g_pd3dDevice = NULL;
ID3D11DeviceContext*    g_pImmediateContext = NULL;
IDXGISwapChain*         g_pSwapChain = NULL;
ID3D11RenderTargetView* g_pRenderTargetView = NULL;
ID3D11Texture2D*        g_pDepthStencil = NULL;
ID3D11DepthStencilView* g_pDepthStencilView = NULL;
ID3D11VertexShader*     g_pVertexShader = NULL;
ID3D11PixelShader*      g_pPixelShader = NULL;
ID3D11InputLayout*      g_pVertexLayout = NULL;
ID3D11Buffer*           g_pVertexBuffer = NULL;
ID3D11Buffer*           g_pIndexBuffer = NULL;
ID3D11Buffer*           g_pConstantBuffer = NULL;
XMMATRIX                g_World1;
XMMATRIX                g_World2;
XMMATRIX                g_View;
XMMATRIX                g_Projection;

// new resource
ID3D11Texture2D*        g_DisplayBackBuffer = nullptr;
ID3D11Texture2D*        g_StagingBackBuffer = nullptr;


//--------------------------------------------------------------------------------------
// Forward declarations
//--------------------------------------------------------------------------------------
HRESULT InitWindow( HINSTANCE hInstance, int nCmdShow );
HRESULT InitDevice();
void CleanupDevice();
LRESULT CALLBACK    WndProc( HWND, UINT, WPARAM, LPARAM );
void Render();


//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    if( FAILED( InitWindow( hInstance, nCmdShow ) ) )
        return 0;

    if( FAILED( InitDevice() ) )
    {
        CleanupDevice();
        return 0;
    }

    // Main message loop
    MSG msg = {0};
	{
		timeBeginPeriod(1);
		DWORD prevTime = GetTickCount();

		movie_writer mw(L"d3d11movie.mp4", 640, 480, 30);
		while( WM_QUIT != msg.message )
		{
			if( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
			{
				TranslateMessage( &msg );
				DispatchMessage( &msg );
			}
			else
			{
				Render();

				DWORD currentTime = GetTickCount();
				DWORD step = currentTime - prevTime;
				if (step > 33)
				{
					if (step > 1000)
						step = 1000; // Avoid very long frame
					g_pImmediateContext->CopyResource(g_StagingBackBuffer, g_DisplayBackBuffer);
					D3D11_MAPPED_SUBRESOURCE res;
					CHK(g_pImmediateContext->Map(g_StagingBackBuffer, 0, D3D11_MAP_READ, 0, &res));
					mw.write((char*)res.pData, 10000 * step); // FIXME: Consider "pitch"
					g_pImmediateContext->Unmap(g_StagingBackBuffer, 0);
					prevTime = currentTime;
				}
			}
		}
		mw.finalize();
		timeEndPeriod(1);
	}
	MFShutdown();

    CleanupDevice();

    return ( int )msg.wParam;
}


//--------------------------------------------------------------------------------------
// Register class and create window
//--------------------------------------------------------------------------------------
HRESULT InitWindow( HINSTANCE hInstance, int nCmdShow )
{
    // Register class
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof( WNDCLASSEX );
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon( hInstance, ( LPCTSTR )IDI_TUTORIAL1 );
    wcex.hCursor = LoadCursor( NULL, IDC_ARROW );
    wcex.hbrBackground = ( HBRUSH )( COLOR_WINDOW + 1 );
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"TutorialWindowClass";
    wcex.hIconSm = LoadIcon( wcex.hInstance, ( LPCTSTR )IDI_TUTORIAL1 );
    if( !RegisterClassEx( &wcex ) )
        return E_FAIL;

    // Create window
    g_hInst = hInstance;
    RECT rc = { 0, 0, 640, 480 };
    AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );
    g_hWnd = CreateWindow( L"TutorialWindowClass", L"Direct3D 11 Tutorial 5", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance,
                           NULL );
    if( !g_hWnd )
        return E_FAIL;

    ShowWindow( g_hWnd, nCmdShow );

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Helper for compiling shaders with D3DX11
//--------------------------------------------------------------------------------------
HRESULT CompileShaderFromFile( WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut )
{
    HRESULT hr = S_OK;

    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
    // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows 
    // the shaders to be optimized and to run exactly the way they will run in 
    // the release configuration of this program.
    dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

    ID3DBlob* pErrorBlob;
    hr = D3DCompileFromFile( szFileName, NULL, NULL, szEntryPoint, szShaderModel, 
        dwShaderFlags, 0, ppBlobOut, &pErrorBlob );
    if( FAILED(hr) )
    {
        if( pErrorBlob != NULL )
            OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );
        if( pErrorBlob ) pErrorBlob->Release();
        return hr;
    }
    if( pErrorBlob ) pErrorBlob->Release();

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT InitDevice()
{
    HRESULT hr = S_OK;

    RECT rc;
    GetClientRect( g_hWnd, &rc );
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    UINT createDeviceFlags = 0;
    createDeviceFlags |= D3D11_CREATE_DEVICE_BGRA_SUPPORT; // modified
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_DRIVER_TYPE driverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT numDriverTypes = ARRAYSIZE( driverTypes );

	D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE( featureLevels );

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory( &sd, sizeof( sd ) );
    sd.BufferCount = 1;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // modified
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    for( UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++ )
    {
        g_driverType = driverTypes[driverTypeIndex];
        hr = D3D11CreateDeviceAndSwapChain( NULL, g_driverType, NULL, createDeviceFlags, featureLevels, numFeatureLevels,
                                            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &g_featureLevel, &g_pImmediateContext );
        if( SUCCEEDED( hr ) )
            break;
    }
    if( FAILED( hr ) )
        return hr;

    // Create a render target view
    ID3D11Texture2D* pBackBuffer = NULL;
    hr = g_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), ( LPVOID* )&pBackBuffer );
    if( FAILED( hr ) )
        return hr;
    g_DisplayBackBuffer = pBackBuffer;

    D3D11_TEXTURE2D_DESC backBufferDesc;
    pBackBuffer->GetDesc(&backBufferDesc);
    backBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    backBufferDesc.Usage = D3D11_USAGE_STAGING;
    backBufferDesc.BindFlags = 0;
    hr = g_pd3dDevice->CreateTexture2D(&backBufferDesc, nullptr, &g_StagingBackBuffer);
    if( FAILED( hr ) )
        return hr;

    hr = g_pd3dDevice->CreateRenderTargetView( pBackBuffer, NULL, &g_pRenderTargetView );
    //pBackBuffer->Release(); // modified
    if( FAILED( hr ) )
        return hr;

    // Create depth stencil texture
    D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory( &descDepth, sizeof(descDepth) );
    descDepth.Width = width;
    descDepth.Height = height;
    descDepth.MipLevels = 1;
    descDepth.ArraySize = 1;
    descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    descDepth.SampleDesc.Count = 1;
    descDepth.SampleDesc.Quality = 0;
    descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    descDepth.CPUAccessFlags = 0;
    descDepth.MiscFlags = 0;
    hr = g_pd3dDevice->CreateTexture2D( &descDepth, NULL, &g_pDepthStencil );
    if( FAILED( hr ) )
        return hr;

    // Create the depth stencil view
    D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
	ZeroMemory( &descDSV, sizeof(descDSV) );
    descDSV.Format = descDepth.Format;
    descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    descDSV.Texture2D.MipSlice = 0;
    hr = g_pd3dDevice->CreateDepthStencilView( g_pDepthStencil, &descDSV, &g_pDepthStencilView );
    if( FAILED( hr ) )
        return hr;

    g_pImmediateContext->OMSetRenderTargets( 1, &g_pRenderTargetView, g_pDepthStencilView );

    // Setup the viewport
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports( 1, &vp );

	// Compile the vertex shader
	ID3DBlob* pVSBlob = NULL;
    hr = CompileShaderFromFile( L"Tutorial05.fx", "VS", "vs_4_0", &pVSBlob );
    if( FAILED( hr ) )
    {
        MessageBox( NULL,
                    L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
        return hr;
    }

	// Create the vertex shader
	hr = g_pd3dDevice->CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), NULL, &g_pVertexShader );
	if( FAILED( hr ) )
	{	
		pVSBlob->Release();
        return hr;
	}

    // Define the input layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = ARRAYSIZE( layout );

    // Create the input layout
	hr = g_pd3dDevice->CreateInputLayout( layout, numElements, pVSBlob->GetBufferPointer(),
                                          pVSBlob->GetBufferSize(), &g_pVertexLayout );
	pVSBlob->Release();
	if( FAILED( hr ) )
        return hr;

    // Set the input layout
    g_pImmediateContext->IASetInputLayout( g_pVertexLayout );

	// Compile the pixel shader
	ID3DBlob* pPSBlob = NULL;
    hr = CompileShaderFromFile( L"Tutorial05.fx", "PS", "ps_4_0", &pPSBlob );
    if( FAILED( hr ) )
    {
        MessageBox( NULL,
                    L"The FX file cannot be compiled.  Please run this executable from the directory that contains the FX file.", L"Error", MB_OK );
        return hr;
    }

	// Create the pixel shader
	hr = g_pd3dDevice->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &g_pPixelShader );
	pPSBlob->Release();
    if( FAILED( hr ) )
        return hr;

    // Create vertex buffer
    SimpleVertex vertices[] =
    {
        { XMFLOAT3( -1.0f, 1.0f, -1.0f ), XMFLOAT4( 0.0f, 0.0f, 1.0f, 1.0f ) },
        { XMFLOAT3( 1.0f, 1.0f, -1.0f ), XMFLOAT4( 0.0f, 1.0f, 0.0f, 1.0f ) },
        { XMFLOAT3( 1.0f, 1.0f, 1.0f ), XMFLOAT4( 0.0f, 1.0f, 1.0f, 1.0f ) },
        { XMFLOAT3( -1.0f, 1.0f, 1.0f ), XMFLOAT4( 1.0f, 0.0f, 0.0f, 1.0f ) },
        { XMFLOAT3( -1.0f, -1.0f, -1.0f ), XMFLOAT4( 1.0f, 0.0f, 1.0f, 1.0f ) },
        { XMFLOAT3( 1.0f, -1.0f, -1.0f ), XMFLOAT4( 1.0f, 1.0f, 0.0f, 1.0f ) },
        { XMFLOAT3( 1.0f, -1.0f, 1.0f ), XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f ) },
        { XMFLOAT3( -1.0f, -1.0f, 1.0f ), XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f ) },
    };
    D3D11_BUFFER_DESC bd;
	ZeroMemory( &bd, sizeof(bd) );
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( SimpleVertex ) * 8;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory( &InitData, sizeof(InitData) );
    InitData.pSysMem = vertices;
    hr = g_pd3dDevice->CreateBuffer( &bd, &InitData, &g_pVertexBuffer );
    if( FAILED( hr ) )
        return hr;

    // Set vertex buffer
    UINT stride = sizeof( SimpleVertex );
    UINT offset = 0;
    g_pImmediateContext->IASetVertexBuffers( 0, 1, &g_pVertexBuffer, &stride, &offset );

    // Create index buffer
    WORD indices[] =
    {
        3,1,0,
        2,1,3,

        0,5,4,
        1,5,0,

        3,4,7,
        0,4,3,

        1,6,5,
        2,6,1,

        2,7,6,
        3,7,2,

        6,4,5,
        7,4,6,
    };
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( WORD ) * 36;        // 36 vertices needed for 12 triangles in a triangle list
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = 0;
    InitData.pSysMem = indices;
    hr = g_pd3dDevice->CreateBuffer( &bd, &InitData, &g_pIndexBuffer );
    if( FAILED( hr ) )
        return hr;

    // Set index buffer
    g_pImmediateContext->IASetIndexBuffer( g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0 );

    // Set primitive topology
    g_pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	// Create the constant buffer
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(ConstantBuffer);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
    hr = g_pd3dDevice->CreateBuffer( &bd, NULL, &g_pConstantBuffer );
    if( FAILED( hr ) )
        return hr;

    // Initialize the world matrix
	g_World1 = XMMatrixIdentity();
	g_World2 = XMMatrixIdentity();

    // Initialize the view matrix
	XMVECTOR Eye = XMVectorSet( 0.0f, 1.0f, -5.0f, 0.0f );
	XMVECTOR At = XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
	XMVECTOR Up = XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
	g_View = XMMatrixLookAtLH( Eye, At, Up );

    // Initialize the projection matrix
	g_Projection = XMMatrixPerspectiveFovLH( XM_PIDIV2, width / (FLOAT)height, 0.01f, 100.0f );

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void CleanupDevice()
{
    if( g_pImmediateContext ) g_pImmediateContext->ClearState();

    if( g_pConstantBuffer ) g_pConstantBuffer->Release();
    if( g_pVertexBuffer ) g_pVertexBuffer->Release();
    if( g_pIndexBuffer ) g_pIndexBuffer->Release();
    if( g_pVertexLayout ) g_pVertexLayout->Release();
    if( g_pVertexShader ) g_pVertexShader->Release();
    if( g_pPixelShader ) g_pPixelShader->Release();
    if( g_pDepthStencil ) g_pDepthStencil->Release();
    if( g_pDepthStencilView ) g_pDepthStencilView->Release();
    if( g_pRenderTargetView ) g_pRenderTargetView->Release();
    if( g_pSwapChain ) g_pSwapChain->Release();
    if( g_pImmediateContext ) g_pImmediateContext->Release();
    if( g_pd3dDevice ) g_pd3dDevice->Release();

    if( g_DisplayBackBuffer ) g_DisplayBackBuffer->Release();
    if( g_StagingBackBuffer ) g_StagingBackBuffer->Release();
}


//--------------------------------------------------------------------------------------
// Called every time the application receives a message
//--------------------------------------------------------------------------------------
LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    PAINTSTRUCT ps;
    HDC hdc;

    switch( message )
    {
        case WM_PAINT:
            hdc = BeginPaint( hWnd, &ps );
            EndPaint( hWnd, &ps );
            break;

        case WM_DESTROY:
            PostQuitMessage( 0 );
            break;

        default:
            return DefWindowProc( hWnd, message, wParam, lParam );
    }

    return 0;
}


//--------------------------------------------------------------------------------------
// Render a frame
//--------------------------------------------------------------------------------------
void Render()
{
    // Update our time
    static float t = 0.0f;
    if( g_driverType == D3D_DRIVER_TYPE_REFERENCE )
    {
        t += ( float )XM_PI * 0.0125f;
    }
    else
    {
        static DWORD dwTimeStart = 0;
        DWORD dwTimeCur = GetTickCount();
        if( dwTimeStart == 0 )
            dwTimeStart = dwTimeCur;
        t = ( dwTimeCur - dwTimeStart ) / 1000.0f;
    }

    // 1st Cube: Rotate around the origin
	g_World1 = XMMatrixRotationY( t );

    // 2nd Cube:  Rotate around origin
    XMMATRIX mSpin = XMMatrixRotationZ( -t );
    XMMATRIX mOrbit = XMMatrixRotationY( -t * 2.0f );
	XMMATRIX mTranslate = XMMatrixTranslation( -4.0f, 0.0f, 0.0f );
	XMMATRIX mScale = XMMatrixScaling( 0.3f, 0.3f, 0.3f );

	g_World2 = mScale * mSpin * mTranslate * mOrbit;

    //
    // Clear the back buffer
    //
    float ClearColor[4] = { 0.0f, 0.125f, 0.3f, 1.0f }; //red, green, blue, alpha
    g_pImmediateContext->ClearRenderTargetView( g_pRenderTargetView, ClearColor );

    //
    // Clear the depth buffer to 1.0 (max depth)
    //
    g_pImmediateContext->ClearDepthStencilView( g_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0 );

    //
    // Update variables for the first cube
    //
    ConstantBuffer cb1;
	cb1.mWorld = XMMatrixTranspose( g_World1 );
	cb1.mView = XMMatrixTranspose( g_View );
	cb1.mProjection = XMMatrixTranspose( g_Projection );
	g_pImmediateContext->UpdateSubresource( g_pConstantBuffer, 0, NULL, &cb1, 0, 0 );

    //
    // Render the first cube
    //
	g_pImmediateContext->VSSetShader( g_pVertexShader, NULL, 0 );
	g_pImmediateContext->VSSetConstantBuffers( 0, 1, &g_pConstantBuffer );
	g_pImmediateContext->PSSetShader( g_pPixelShader, NULL, 0 );
	g_pImmediateContext->DrawIndexed( 36, 0, 0 );

    //
    // Update variables for the second cube
    //
    ConstantBuffer cb2;
	cb2.mWorld = XMMatrixTranspose( g_World2 );
	cb2.mView = XMMatrixTranspose( g_View );
	cb2.mProjection = XMMatrixTranspose( g_Projection );
	g_pImmediateContext->UpdateSubresource( g_pConstantBuffer, 0, NULL, &cb2, 0, 0 );

    //
    // Render the second cube
    //
	g_pImmediateContext->DrawIndexed( 36, 0, 0 );

    //
    // Present our back buffer to our front buffer
    //
    g_pSwapChain->Present( 0, 0 );
}


