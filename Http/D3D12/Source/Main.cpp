#include "stdafx.h"
#include "SimpleMCPGraphicsSampleD3D12.h"

#include "HttpMcpServer.h"

namespace
{
    constexpr std::uint16_t DefaultHttpPort = 5000;

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

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    sample::common::HttpMcpServerOptions options;
    bool showHelp = false;
    std::string error;
    if (!sample::common::ParseHttpMcpCommandLine(DefaultHttpPort, options, showHelp, error))
    {
        WriteStderr(error);
        WriteStderr(sample::common::HttpMcpCommandLineHelp(DefaultHttpPort));
        return 2;
    }
    if (showHelp)
    {
        WriteStderr(sample::common::HttpMcpCommandLineHelp(DefaultHttpPort));
        return 0;
    }

    try
    {
        sample::common::McpTransportFactory transportFactory = [options](sample::common::SceneStateStore& store)
        {
            return std::make_unique<sample::common::HttpMcpServer>(store, options);
        };
        SimpleMCPGraphicsSampleD3D12 sample(
            1280,
            720,
            L"Simple MCP Graphics Sample - HTTP D3D12",
            std::move(transportFactory));
        return Win32Application::Run(&sample, instance, showCommand);
    }
    catch (const std::exception& exception)
    {
        WriteStderr(std::string("Application error: ") + exception.what());
        return 1;
    }
}
