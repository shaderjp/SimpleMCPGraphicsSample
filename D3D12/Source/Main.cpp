//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "SimpleMCPGraphicsSampleD3D12.h"
#include "StdioMcpServer.h"

namespace
{
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
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const bool mcpRequested = sample::common::CommandLineHasMcpFlag();
    if (mcpRequested)
    {
        std::string error;
        if (!sample::common::ValidateMcpStandardHandles(error))
        {
            WriteStderr("Cannot start MCP stdio transport: " + error);
            return 2;
        }
    }

    try
    {
        sample::common::McpTransportFactory transportFactory;
        if (mcpRequested)
        {
            transportFactory = [](sample::common::SceneStateStore& store)
            {
                return std::make_unique<sample::common::StdioMcpServer>(store);
            };
        }
        SimpleMCPGraphicsSampleD3D12 sample(
            1280,
            720,
            L"Simple MCP Graphics Sample - D3D12",
            std::move(transportFactory));
        return Win32Application::Run(&sample, hInstance, nCmdShow);
    }
    catch (const std::exception& exception)
    {
        WriteStderr(std::string("Application error: ") + exception.what());
        return 1;
    }
}
