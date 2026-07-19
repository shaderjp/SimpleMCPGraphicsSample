#pragma once

#include "McpDispatcher.h"
#include "McpTransportServer.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sample::common
{
    struct FramingResult
    {
        std::vector<std::string> messages;
        bool failed = false;
        std::string error;
    };

    class NewlineJsonFramer
    {
    public:
        static constexpr std::size_t MaximumMessageBytes = 1024 * 1024;

        FramingResult Feed(std::string_view bytes);
        FramingResult Finish();
        void Reset();

    private:
        std::string pending_;
        bool failed_ = false;
    };

    [[nodiscard]] bool CommandLineHasMcpFlag();
    [[nodiscard]] bool ValidateMcpStandardHandles(std::string& error);

    class StdioMcpServer final : public IMcpTransportServer
    {
    public:
        explicit StdioMcpServer(SceneStateStore& store);
        ~StdioMcpServer();

        StdioMcpServer(const StdioMcpServer&) = delete;
        StdioMcpServer& operator=(const StdioMcpServer&) = delete;

        bool Start(void* nativeWindow, std::string& error) override;
        void Stop() override;
        void Join() override;
        [[nodiscard]] bool IsRunning() const noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
