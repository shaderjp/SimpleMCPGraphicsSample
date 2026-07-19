#include "SimpleMCPGraphicsSampleVulkan.h"

#include "HttpMcpServer.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    constexpr std::uint16_t DefaultHttpPort = 5001;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    sample::common::HttpMcpServerOptions options;
    bool showHelp = false;
    std::string error;
    if (!sample::common::ParseHttpMcpCommandLine(DefaultHttpPort, options, showHelp, error))
    {
        std::fprintf(stderr, "SimpleMCPGraphicsSampleHttpVulkan: %s\n", error.c_str());
        std::fprintf(stderr, "%s", sample::common::HttpMcpCommandLineHelp(DefaultHttpPort).c_str());
        return 2;
    }
    if (showHelp)
    {
        std::fprintf(stderr, "%s", sample::common::HttpMcpCommandLineHelp(DefaultHttpPort).c_str());
        return 0;
    }

    try
    {
        sample::common::McpTransportFactory transportFactory = [options](sample::common::SceneStateStore& store)
        {
            return std::make_unique<sample::common::HttpMcpServer>(store, options);
        };
        SimpleMCPGraphicsSampleVulkan app(
            1280,
            720,
            L"SimpleMCPGraphicsSample - HTTP Vulkan",
            std::move(transportFactory));
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "SimpleMCPGraphicsSampleHttpVulkan: %s\n", exception.what());
        MessageBoxA(nullptr, exception.what(), "SimpleMCPGraphicsSample - HTTP Vulkan", MB_OK | MB_ICONERROR);
        return -1;
    }
}
