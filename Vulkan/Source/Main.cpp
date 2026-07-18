#include "SimpleMCPGraphicsSampleVulkan.h"

#include "StdioMcpServer.h"

#include <cstdio>
#include <stdexcept>
#include <string>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    const bool mcpMode = sample::common::CommandLineHasMcpFlag();
    if (mcpMode)
    {
        std::string error;
        if (!sample::common::ValidateMcpStandardHandles(error))
        {
            std::fprintf(stderr, "SimpleMCPGraphicsSampleVulkan: %s\n", error.c_str());
            return 2;
        }
    }

    try
    {
        SimpleMCPGraphicsSampleVulkan app(1280, 720, L"SimpleMCPGraphicsSample - Vulkan", mcpMode);
        return app.Run(instance, showCommand);
    }
    catch (const std::exception& error)
    {
        if (mcpMode)
        {
            std::fprintf(stderr, "SimpleMCPGraphicsSampleVulkan: %s\n", error.what());
        }
        else
        {
            MessageBoxA(nullptr, error.what(), "SimpleMCPGraphicsSample - Vulkan", MB_OK | MB_ICONERROR);
        }
        return -1;
    }
}
