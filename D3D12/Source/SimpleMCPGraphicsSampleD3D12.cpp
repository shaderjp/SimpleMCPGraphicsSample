#include "stdafx.h"
#include "SimpleMCPGraphicsSampleD3D12.h"
#include "ControlPanel.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

namespace
{
    constexpr uint32_t HelmetIndexCount = 70074;
    constexpr uint32_t HelmetVertexCount = 70074;
    constexpr size_t IndexOffset = 0;
    constexpr size_t PositionOffset = 280296;
    constexpr size_t NormalOffset = 1121184;
    constexpr size_t TangentOffset = 1962072;
    constexpr size_t TexcoordOffset = 3083256;

    const std::array<const wchar_t*, SimpleMCPGraphicsSampleD3D12::TextureCount> TextureNames =
    {
        L"SciFiHelmet_BaseColor.png",
        L"SciFiHelmet_MetallicRoughness.png",
        L"SciFiHelmet_Normal.png",
        L"SciFiHelmet_AmbientOcclusion.png",
    };

    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || value[0] == L'\0')
        {
            return "Unknown GPU";
        }

        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return "Unknown GPU";
        }

        std::string result(static_cast<size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), required, nullptr, nullptr) == 0)
        {
            return "Unknown GPU";
        }
        result.resize(static_cast<size_t>(required - 1));
        return result;
    }

    void WriteStderr(const std::string& message)
    {
        HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
        if (stderrHandle == nullptr || stderrHandle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        const std::string line = message + "\r\n";
        DWORD written = 0;
        WriteFile(stderrHandle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
}

SimpleMCPGraphicsSampleD3D12::SimpleMCPGraphicsSampleD3D12(
    UINT width,
    UINT height,
    std::wstring name,
    sample::common::McpTransportFactory mcpTransportFactory) :
    DXSample(width, height, name),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0),
    m_cbvSrvDescriptorSize(0),
    m_imguiDescriptorSize(0),
    m_imguiDescriptorCursor(0),
    m_mappedConstantBuffer(nullptr),
    m_fpsWindowStart(std::chrono::steady_clock::now()),
    m_frameCount(0),
    m_framesInFpsWindow(0),
    m_fps(0.0),
    m_frameIndex(0),
    m_fenceEvent(nullptr),
    m_fenceValue(0)
{
    if (mcpTransportFactory)
    {
        m_mcpServer = mcpTransportFactory(m_sceneState);
    }
    m_sceneState.UpdateRendererInfo("D3D12", "Initializing", false);
}

bool SimpleMCPGraphicsSampleD3D12::OnWindowCreated(HWND window)
{
    m_sceneState.SetMcpRequested(m_mcpServer != nullptr);
    if (!m_mcpServer)
    {
        return true;
    }

    std::string error;
    if (!m_mcpServer->Start(static_cast<void*>(window), error))
    {
        WriteStderr("Failed to start MCP server: " + error);
        return false;
    }
    return true;
}

void SimpleMCPGraphicsSampleD3D12::OnInit()
{
    ThrowIfFailed(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    LoadModel();
    LoadPipeline();
    LoadAssets();
    m_fpsWindowStart = std::chrono::steady_clock::now();
    m_sceneState.SetRendererReady(true);
}

void SimpleMCPGraphicsSampleD3D12::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
        DXGI_ADAPTER_DESC adapterDesc = {};
        ThrowIfFailed(warpAdapter->GetDesc(&adapterDesc));
        m_sceneState.UpdateRendererInfo("D3D12", WideToUtf8(adapterDesc.Description), false);
        ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);
        DXGI_ADAPTER_DESC1 adapterDesc = {};
        ThrowIfFailed(hardwareAdapter->GetDesc1(&adapterDesc));
        m_sceneState.UpdateRendererInfo("D3D12", WideToUtf8(adapterDesc.Description), false);
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), Win32Application::GetHwnd(), &swapChainDesc, nullptr, nullptr, &swapChain));
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));
    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT n = 0; n < FrameCount; n++)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
    cbvSrvHeapDesc.NumDescriptors = 1 + TextureCount;
    cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap)));
    m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
    imguiHeapDesc.NumDescriptors = ImGuiDescriptorCount;
    imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&imguiHeapDesc, IID_PPV_ARGS(&m_imguiDescriptorHeap)));
    m_imguiDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

void SimpleMCPGraphicsSampleD3D12::LoadAssets()
{
    CD3DX12_DESCRIPTOR_RANGE cbvRange;
    cbvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, TextureCount, 0);

    CD3DX12_ROOT_PARAMETER rootParameters[2];
    rootParameters[0].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

    UINT8* vertexShaderBytes = nullptr;
    UINT8* pixelShaderBytes = nullptr;
    UINT vertexShaderDataLength = 0;
    UINT pixelShaderDataLength = 0;
    ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"SimpleMCPGraphicsSample.vs.cso").c_str(), &vertexShaderBytes, &vertexShaderDataLength));
    std::unique_ptr<UINT8, decltype(&free)> vertexShaderData(vertexShaderBytes, &free);
    ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"SimpleMCPGraphicsSample.ps.cso").c_str(), &pixelShaderBytes, &pixelShaderDataLength));
    std::unique_ptr<UINT8, decltype(&free)> pixelShaderData(pixelShaderBytes, &free);

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, tangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, texcoord), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderData.get(), vertexShaderDataLength);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderData.get(), pixelShaderDataLength);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    const CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const UINT vertexBufferSize = static_cast<UINT>(m_vertices.size() * sizeof(Vertex));
    const CD3DX12_RESOURCE_DESC vertexBufferDescription = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)));
    UINT8* vertexDataBegin = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&vertexDataBegin)));
    memcpy(vertexDataBegin, m_vertices.data(), vertexBufferSize);
    m_vertexBuffer->Unmap(0, nullptr);
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;

    const UINT indexBufferSize = static_cast<UINT>(m_indices.size() * sizeof(uint32_t));
    const CD3DX12_RESOURCE_DESC indexBufferDescription = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &indexBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer)));
    UINT8* indexDataBegin = nullptr;
    ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&indexDataBegin)));
    memcpy(indexDataBegin, m_indices.data(), indexBufferSize);
    m_indexBuffer->Unmap(0, nullptr);
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    const CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC depthDescription = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT,
        m_width,
        m_height,
        1,
        0,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    ThrowIfFailed(m_device->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &depthDescription,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthOptimizedClearValue,
        IID_PPV_ARGS(&m_depthStencil)));
    m_device->CreateDepthStencilView(m_depthStencil.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    const UINT constantBufferSize = CalculateConstantBufferByteSize(sizeof(SceneConstantBuffer));
    const CD3DX12_RESOURCE_DESC constantBufferDescription = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &constantBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)));
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = constantBufferSize;
    m_device->CreateConstantBufferView(&cbvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
    ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedConstantBuffer)));
    UpdateConstantBuffer();

    for (UINT i = 0; i < TextureCount; ++i)
    {
        CreateTexture(i, LoadTexture(ResolveHelmetAssetPath(TextureNames[i])));
    }

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    WaitForPreviousFrame();
    m_textureUploadHeaps.clear();
    InitializeImGui();
}

void SimpleMCPGraphicsSampleD3D12::OnUpdate()
{
    BuildUI();
    UpdateConstantBuffer();
}

void SimpleMCPGraphicsSampleD3D12::OnRender()
{
    PopulateCommandList();
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    ThrowIfFailed(m_swapChain->Present(1, 0));
    WaitForPreviousFrame();

    ++m_frameCount;
    ++m_framesInFpsWindow;
    const auto now = std::chrono::steady_clock::now();
    const double elapsedSeconds = std::chrono::duration<double>(now - m_fpsWindowStart).count();
    if (elapsedSeconds >= 0.5)
    {
        m_fps = static_cast<double>(m_framesInFpsWindow) / elapsedSeconds;
        m_framesInFpsWindow = 0;
        m_fpsWindowStart = now;
    }
    m_sceneState.UpdateFrameStats(m_fps, m_frameCount);
}

void SimpleMCPGraphicsSampleD3D12::OnDestroy()
{
    m_sceneState.SetRendererReady(false);
    if (m_mcpServer)
    {
        m_mcpServer->Stop();
        m_mcpServer->Join();
    }

    WaitForPreviousFrame();
    ShutdownImGui();
    if (m_constantBuffer)
    {
        m_constantBuffer->Unmap(0, nullptr);
        m_mappedConstantBuffer = nullptr;
    }
    if (m_fenceEvent != nullptr)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    CoUninitialize();
}

void SimpleMCPGraphicsSampleD3D12::LoadModel()
{
    const std::wstring path = ResolveHelmetAssetPath(L"SciFiHelmet.bin");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    }

    const std::streamoff endPosition = file.tellg();
    if (endPosition < 0 || static_cast<uint64_t>(endPosition) > static_cast<uint64_t>((std::numeric_limits<std::streamsize>::max)()))
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(ERROR_BAD_FORMAT));
    }
    const size_t fileSize = static_cast<size_t>(endPosition);
    const auto rangeFits = [fileSize](size_t offset, size_t elementSize, size_t elementCount)
    {
        return offset <= fileSize && elementSize != 0 && elementCount <= (fileSize - offset) / elementSize;
    };
    if (!rangeFits(IndexOffset, sizeof(uint32_t), HelmetIndexCount) ||
        !rangeFits(PositionOffset, sizeof(XMFLOAT3), HelmetVertexCount) ||
        !rangeFits(NormalOffset, sizeof(XMFLOAT3), HelmetVertexCount) ||
        !rangeFits(TangentOffset, sizeof(XMFLOAT4), HelmetVertexCount) ||
        !rangeFits(TexcoordOffset, sizeof(XMFLOAT2), HelmetVertexCount))
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(ERROR_BAD_FORMAT));
    }

    std::vector<uint8_t> data(fileSize);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize)))
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(ERROR_READ_FAULT));
    }

    const auto* indices = reinterpret_cast<const uint32_t*>(data.data() + IndexOffset);
    const auto* positions = reinterpret_cast<const XMFLOAT3*>(data.data() + PositionOffset);
    const auto* normals = reinterpret_cast<const XMFLOAT3*>(data.data() + NormalOffset);
    const auto* tangents = reinterpret_cast<const XMFLOAT4*>(data.data() + TangentOffset);
    const auto* texcoords = reinterpret_cast<const XMFLOAT2*>(data.data() + TexcoordOffset);

    m_indices.assign(indices, indices + HelmetIndexCount);
    m_vertices.resize(HelmetVertexCount);
    for (uint32_t i = 0; i < HelmetVertexCount; ++i)
    {
        m_vertices[i] = { positions[i], normals[i], tangents[i], texcoords[i] };
    }
}

std::wstring SimpleMCPGraphicsSampleD3D12::ResolveHelmetAssetPath(const wchar_t* fileName)
{
    std::wstring deployedPath = GetAssetFullPath(L"Assets\\SciFiHelmet\\");
    deployedPath += fileName;
    const DWORD deployedAttributes = GetFileAttributesW(deployedPath.c_str());
    if (deployedAttributes != INVALID_FILE_ATTRIBUTES && (deployedAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return deployedPath;
    }

    std::wstring repositoryPath = GetAssetFullPath(L"..\\..\\..\\..\\Assets\\SciFiHelmet\\");
    repositoryPath += fileName;
    return repositoryPath;
}

void SimpleMCPGraphicsSampleD3D12::InitializeImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_Init(Win32Application::GetHwnd());

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = m_device.Get();
    initInfo.CommandQueue = m_commandQueue.Get();
    initInfo.NumFramesInFlight = FrameCount;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    initInfo.UserData = this;
    initInfo.SrvDescriptorHeap = m_imguiDescriptorHeap.Get();
    initInfo.SrvDescriptorAllocFn = AllocateImGuiDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeImGuiDescriptor;
    ThrowIfFailed(ImGui_ImplDX12_Init(&initInfo) ? S_OK : E_FAIL);
}

void SimpleMCPGraphicsSampleD3D12::ShutdownImGui()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void SimpleMCPGraphicsSampleD3D12::BuildUI()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    sample::common::DrawControlPanel(m_sceneState);

    ImGui::Render();
}

SimpleMCPGraphicsSampleD3D12::ImageData SimpleMCPGraphicsSampleD3D12::LoadTexture(const std::wstring& path) const
{
    ComPtr<IWICImagingFactory2> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));

    ComPtr<IWICBitmapDecoder> decoder;
    ThrowIfFailed(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame));

    ImageData image;
    ThrowIfFailed(frame->GetSize(&image.width, &image.height));
    const uint64_t pixelBytes = static_cast<uint64_t>(image.width) * image.height * 4;
    if (image.width == 0 || image.height == 0 || pixelBytes > UINT_MAX)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(ERROR_BAD_FORMAT));
    }
    image.pixels.resize(static_cast<size_t>(pixelBytes));

    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(&converter));
    ThrowIfFailed(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    ThrowIfFailed(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data()));
    return image;
}

void SimpleMCPGraphicsSampleD3D12::CreateTexture(UINT index, const ImageData& image)
{
    D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, image.width, image.height);
    const CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_textures[index])));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_textures[index].Get(), 0, 1);
    ComPtr<ID3D12Resource> uploadHeap;
    const CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC uploadBufferDescription = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &uploadBufferDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadHeap)));

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = image.pixels.data();
    textureData.RowPitch = static_cast<LONG_PTR>(image.width) * 4;
    textureData.SlicePitch = textureData.RowPitch * image.height;
    UpdateSubresources(m_commandList.Get(), m_textures[index].Get(), uploadHeap.Get(), 0, 0, 1, &textureData);
    const CD3DX12_RESOURCE_BARRIER textureReadyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_textures[index].Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &textureReadyBarrier);
    m_textureUploadHeaps.push_back(uploadHeap);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), 1 + index, m_cbvSrvDescriptorSize);
    m_device->CreateShaderResourceView(m_textures[index].Get(), &srvDesc, srvHandle);
}

void SimpleMCPGraphicsSampleD3D12::PopulateCommandList()
{
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvSrvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootDescriptorTable(0, m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 1, m_cbvSrvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(1, srvHandle);
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    const CD3DX12_RESOURCE_BARRIER beginFrameBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &beginFrameBarrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    m_commandList->DrawIndexedInstanced(static_cast<UINT>(m_indices.size()), 1, 0, 0, 0);

    ID3D12DescriptorHeap* imguiHeaps[] = { m_imguiDescriptorHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    const CD3DX12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &presentBarrier);
    ThrowIfFailed(m_commandList->Close());
}

void SimpleMCPGraphicsSampleD3D12::WaitForPreviousFrame()
{
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void SimpleMCPGraphicsSampleD3D12::UpdateConstantBuffer()
{
    const sample::common::SceneState scene = m_sceneState.GetSceneState();

    const sample::common::TransformState& transform = scene.transform;
    const XMMATRIX model =
        XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z) *
        XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(transform.rotationDegrees.x),
            XMConvertToRadians(transform.rotationDegrees.y),
            XMConvertToRadians(transform.rotationDegrees.z)) *
        XMMatrixTranslation(transform.translation.x, transform.translation.y, transform.translation.z);
    const XMVECTOR cameraPosition = XMVectorSet(scene.camera.position.x, scene.camera.position.y, scene.camera.position.z, 1.0f);
    const XMVECTOR cameraTarget = XMVectorSet(scene.camera.target.x, scene.camera.target.y, scene.camera.target.z, 1.0f);
    XMMATRIX view = XMMatrixLookAtLH(cameraPosition, cameraTarget, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(scene.camera.fovDegrees), m_aspectRatio, 0.1f, 100.0f);

    XMStoreFloat4x4(&m_constantBufferData.model, model);
    XMStoreFloat4x4(&m_constantBufferData.modelViewProjection, model * view * projection);
    m_constantBufferData.cameraPosition = XMFLOAT4(scene.camera.position.x, scene.camera.position.y, scene.camera.position.z, 1.0f);
    const XMVECTOR lightDirection = XMVector3Normalize(XMVectorSet(
        scene.light.direction.x,
        scene.light.direction.y,
        scene.light.direction.z,
        0.0f));
    XMFLOAT3 normalizedLightDirection;
    XMStoreFloat3(&normalizedLightDirection, lightDirection);
    m_constantBufferData.lightDirection = XMFLOAT4(
        normalizedLightDirection.x,
        normalizedLightDirection.y,
        normalizedLightDirection.z,
        0.0f);
    m_constantBufferData.lightColor = XMFLOAT4(scene.light.color.r, scene.light.color.g, scene.light.color.b, scene.light.intensity);
    memcpy(m_mappedConstantBuffer, &m_constantBufferData, sizeof(m_constantBufferData));
}

void SimpleMCPGraphicsSampleD3D12::AllocateImGuiDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
    auto* sample = static_cast<SimpleMCPGraphicsSampleD3D12*>(info->UserData);
    const UINT descriptorIndex = sample->m_imguiDescriptorCursor++;
    if (descriptorIndex >= ImGuiDescriptorCount)
    {
        ThrowIfFailed(E_OUTOFMEMORY);
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(sample->m_imguiDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, sample->m_imguiDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(sample->m_imguiDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, sample->m_imguiDescriptorSize);
    *outCpuHandle = cpuHandle;
    *outGpuHandle = gpuHandle;
}

void SimpleMCPGraphicsSampleD3D12::FreeImGuiDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
{
}
