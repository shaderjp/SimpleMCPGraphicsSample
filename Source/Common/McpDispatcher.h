#pragma once

#include "SceneState.h"

#include <mutex>
#include <string>
#include <string_view>

namespace sample::common
{
    struct McpDispatchResult
    {
        bool hasResponse = false;
        std::string responseLine;
    };

    class McpDispatcher
    {
    public:
        static constexpr const char* ProtocolVersion = "2025-11-25";

        explicit McpDispatcher(SceneStateStore& store);
        McpDispatchResult DispatchLine(std::string_view line);

    private:
        SceneStateStore& store_;
        std::mutex protocolMutex_;
        bool initializeSeen_ = false;
        bool initialized_ = false;
    };
}
