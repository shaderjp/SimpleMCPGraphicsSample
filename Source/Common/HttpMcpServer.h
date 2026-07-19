#pragma once

#include "McpTransportServer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sample::common
{
    class SceneStateStore;

    struct HttpMcpServerOptions
    {
        std::uint16_t port = 5000;
        std::vector<std::string> allowedOrigins;
        std::chrono::seconds sessionIdleTimeout{ 30 * 60 };
        std::chrono::seconds cleanupInterval{ 10 };
        std::size_t maximumSessions = 32;
    };

    struct HttpMcpRequest
    {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    struct HttpMcpResponse
    {
        int status = 200;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    class HttpMcpEndpoint
    {
    public:
        static constexpr std::size_t MaximumMessageBytes = 1024 * 1024;

        HttpMcpEndpoint(SceneStateStore& store, HttpMcpServerOptions options);
        ~HttpMcpEndpoint();

        HttpMcpEndpoint(const HttpMcpEndpoint&) = delete;
        HttpMcpEndpoint& operator=(const HttpMcpEndpoint&) = delete;

        HttpMcpResponse Handle(const HttpMcpRequest& request);
        void ExpireIdleSessions();
        void CloseAllSessions();
        [[nodiscard]] std::size_t ActiveSessionCount() const;
        [[nodiscard]] std::string EndpointUrl() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class HttpMcpServer final : public IMcpTransportServer
    {
    public:
        HttpMcpServer(SceneStateStore& store, HttpMcpServerOptions options);
        ~HttpMcpServer() override;

        HttpMcpServer(const HttpMcpServer&) = delete;
        HttpMcpServer& operator=(const HttpMcpServer&) = delete;

        bool Start(void* nativeWindow, std::string& error) override;
        void Stop() override;
        void Join() override;
        [[nodiscard]] bool IsRunning() const noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    [[nodiscard]] bool ParseHttpMcpCommandLine(
        std::uint16_t defaultPort,
        HttpMcpServerOptions& options,
        bool& showHelp,
        std::string& error);
    [[nodiscard]] std::string HttpMcpCommandLineHelp(std::uint16_t defaultPort);
}
