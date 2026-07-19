#include "HttpMcpServer.h"

#include "McpDispatcher.h"
#include "SceneState.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#pragma warning(push)
#pragma warning(disable: 4127 4819)
#include "httplib.h"
#pragma warning(pop)
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cwchar>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace sample::common
{
    namespace
    {
        using Json = nlohmann::json;
        using Clock = std::chrono::steady_clock;

        std::string LowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
            {
                if (character >= 'A' && character <= 'Z')
                {
                    return static_cast<char>(character - 'A' + 'a');
                }
                return static_cast<char>(character);
            });
            return value;
        }

        std::string TrimAscii(std::string value)
        {
            const auto isSpace = [](unsigned char character)
            {
                return character == ' ' || character == '\t' || character == '\r' || character == '\n';
            };
            while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
            {
                value.pop_back();
            }
            return value;
        }

        std::optional<std::string> FindHeader(
            const std::map<std::string, std::string>& headers,
            const std::string& name)
        {
            const std::string lowerName = LowerAscii(name);
            for (const auto& [key, value] : headers)
            {
                if (LowerAscii(key) == lowerName)
                {
                    return value;
                }
            }
            return std::nullopt;
        }

        bool IsJsonContentType(const std::string& value)
        {
            const std::string lower = LowerAscii(TrimAscii(value));
            return lower == "application/json" || lower.rfind("application/json;", 0) == 0;
        }

        bool AcceptsStreamableHttp(const std::string& value)
        {
            const std::string lower = LowerAscii(value);
            return lower.find("application/json") != std::string::npos &&
                lower.find("text/event-stream") != std::string::npos;
        }

        HttpMcpResponse EmptyResponse(int status)
        {
            HttpMcpResponse response;
            response.status = status;
            response.headers.emplace("Cache-Control", "no-store");
            return response;
        }

        HttpMcpResponse JsonRpcError(int status, int code, const std::string& message)
        {
            HttpMcpResponse response;
            response.status = status;
            response.headers.emplace("Cache-Control", "no-store");
            response.headers.emplace("Content-Type", "application/json; charset=utf-8");
            response.body = Json{
                { "jsonrpc", "2.0" },
                { "id", nullptr },
                { "error", { { "code", code }, { "message", message } } },
            }.dump();
            return response;
        }

        HttpMcpResponse JsonResponse(std::string body)
        {
            HttpMcpResponse response;
            response.status = 200;
            response.headers.emplace("Cache-Control", "no-store");
            response.headers.emplace("Content-Type", "application/json; charset=utf-8");
            response.body = std::move(body);
            return response;
        }

        std::string GenerateSessionId()
        {
            std::array<unsigned char, 32> bytes{};
            const NTSTATUS status = BCryptGenRandom(
                nullptr,
                bytes.data(),
                static_cast<ULONG>(bytes.size()),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (status < 0)
            {
                throw std::runtime_error("BCryptGenRandom failed while creating an MCP session ID.");
            }

            std::ostringstream stream;
            stream << std::hex << std::setfill('0');
            for (const unsigned char byte : bytes)
            {
                stream << std::setw(2) << static_cast<unsigned int>(byte);
            }
            return stream.str();
        }

        std::string WideToUtf8(const wchar_t* value)
        {
            if (value == nullptr || *value == L'\0')
            {
                return {};
            }
            const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
            if (required <= 1)
            {
                return {};
            }
            std::string result(static_cast<std::size_t>(required), '\0');
            WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
            result.pop_back();
            return result;
        }
    }

    struct HttpMcpEndpoint::Impl
    {
        struct Session
        {
            explicit Session(SceneStateStore& store)
                : dispatcher(store)
            {
            }

            McpDispatcher dispatcher;
            std::mutex operationMutex;
            Clock::time_point lastActivity = Clock::now();
            std::atomic<bool> initialized{ false };
            std::atomic<bool> closed{ false };
        };

        Impl(SceneStateStore& stateStore, HttpMcpServerOptions serverOptions)
            : store(stateStore), options(std::move(serverOptions))
        {
            allowedOrigins.push_back("http://127.0.0.1:" + std::to_string(options.port));
            allowedOrigins.push_back("http://localhost:" + std::to_string(options.port));
            for (std::string origin : options.allowedOrigins)
            {
                origin = TrimAscii(std::move(origin));
                if (!origin.empty() &&
                    std::find(allowedOrigins.begin(), allowedOrigins.end(), origin) == allowedOrigins.end())
                {
                    allowedOrigins.push_back(std::move(origin));
                }
            }
        }

        HttpMcpResponse ValidateOrigin(const HttpMcpRequest& request) const
        {
            const std::optional<std::string> origin = FindHeader(request.headers, "Origin");
            if (!origin)
            {
                return EmptyResponse(200);
            }
            if (std::find(allowedOrigins.begin(), allowedOrigins.end(), *origin) == allowedOrigins.end())
            {
                return JsonRpcError(403, -32000, "Forbidden Origin header.");
            }
            return EmptyResponse(200);
        }

        HttpMcpResponse ValidateSessionHeaders(
            const HttpMcpRequest& request,
            std::string& sessionId) const
        {
            const std::optional<std::string> header = FindHeader(request.headers, "MCP-Session-Id");
            if (!header || header->empty())
            {
                return JsonRpcError(400, -32600, "MCP-Session-Id header is required.");
            }
            const std::optional<std::string> protocol = FindHeader(request.headers, "MCP-Protocol-Version");
            if (!protocol || *protocol != McpDispatcher::ProtocolVersion)
            {
                return JsonRpcError(400, -32600, "Unsupported or missing MCP-Protocol-Version header.");
            }
            sessionId = *header;
            return EmptyResponse(200);
        }

        std::shared_ptr<Session> FindSession(const std::string& id) const
        {
            std::lock_guard lock(sessionsMutex);
            const auto found = sessions.find(id);
            return found == sessions.end() ? nullptr : found->second;
        }

        void PublishSessionStatus()
        {
            std::size_t active = 0;
            std::size_t initialized = 0;
            {
                std::lock_guard lock(sessionsMutex);
                active = sessions.size();
                for (const auto& [id, session] : sessions)
                {
                    (void)id;
                    if (session->initialized.load(std::memory_order_acquire) &&
                        !session->closed.load(std::memory_order_acquire))
                    {
                        ++initialized;
                    }
                }
            }
            store.SetMcpSessionStatus(
                static_cast<std::uint32_t>(active),
                static_cast<std::uint32_t>(initialized));
        }

        HttpMcpResponse HandleInitialize(const HttpMcpRequest& request, const Json& message)
        {
            if (FindHeader(request.headers, "MCP-Session-Id"))
            {
                return JsonRpcError(400, -32600, "initialize must not include MCP-Session-Id.");
            }

            ExpireIdleSessions();
            {
                std::lock_guard lock(sessionsMutex);
                if (sessions.size() >= options.maximumSessions)
                {
                    return JsonRpcError(503, -32603, "Maximum MCP session count reached.");
                }
            }

            auto session = std::make_shared<Session>(store);
            const McpDispatchResult dispatched = session->dispatcher.DispatchLine(request.body);
            if (!dispatched.hasResponse)
            {
                return JsonRpcError(400, -32600, "initialize must be a JSON-RPC request.");
            }

            Json responseJson;
            try
            {
                responseJson = Json::parse(dispatched.responseLine);
            }
            catch (...)
            {
                return JsonRpcError(500, -32603, "Failed to serialize initialize response.");
            }
            if (!responseJson.is_object() || !responseJson.contains("result"))
            {
                return JsonResponse(dispatched.responseLine);
            }

            std::string sessionId;
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                sessionId = GenerateSessionId();
                std::lock_guard lock(sessionsMutex);
                if (sessions.size() >= options.maximumSessions)
                {
                    return JsonRpcError(503, -32603, "Maximum MCP session count reached.");
                }
                if (sessions.emplace(sessionId, session).second)
                {
                    break;
                }
                sessionId.clear();
            }
            if (sessionId.empty())
            {
                return JsonRpcError(500, -32603, "Failed to allocate a unique MCP session ID.");
            }

            PublishSessionStatus();
            HttpMcpResponse response = JsonResponse(dispatched.responseLine);
            response.headers.emplace("MCP-Session-Id", sessionId);
            (void)message;
            return response;
        }

        HttpMcpResponse HandlePost(const HttpMcpRequest& request)
        {
            if (request.body.size() > HttpMcpEndpoint::MaximumMessageBytes)
            {
                return JsonRpcError(413, -32700, "MCP message exceeds the 1 MiB input limit.");
            }
            const std::optional<std::string> contentType = FindHeader(request.headers, "Content-Type");
            if (!contentType || !IsJsonContentType(*contentType))
            {
                return JsonRpcError(415, -32600, "Content-Type must be application/json.");
            }
            const std::optional<std::string> accept = FindHeader(request.headers, "Accept");
            if (!accept || !AcceptsStreamableHttp(*accept))
            {
                return JsonRpcError(406, -32600, "Accept must include application/json and text/event-stream.");
            }

            Json message;
            try
            {
                message = Json::parse(request.body);
            }
            catch (...)
            {
                return JsonRpcError(400, -32700, "Parse error");
            }
            if (!message.is_object())
            {
                return JsonRpcError(400, -32600, "The HTTP body must contain one JSON-RPC message.");
            }

            const bool hasMethod = message.contains("method") && message.at("method").is_string();
            const std::string method = hasMethod ? message.at("method").get<std::string>() : std::string{};
            if (method == "initialize")
            {
                return HandleInitialize(request, message);
            }

            std::string sessionId;
            HttpMcpResponse validation = ValidateSessionHeaders(request, sessionId);
            if (validation.status != 200)
            {
                return validation;
            }
            const std::shared_ptr<Session> session = FindSession(sessionId);
            if (!session)
            {
                return JsonRpcError(404, -32001, "MCP session not found or expired.");
            }

            std::unique_lock operationLock(session->operationMutex);
            if (session->closed.load(std::memory_order_acquire))
            {
                return JsonRpcError(404, -32001, "MCP session not found or expired.");
            }
            session->lastActivity = Clock::now();

            const bool isClientResponse = !hasMethod && message.contains("id") &&
                (message.contains("result") || message.contains("error"));
            if (isClientResponse)
            {
                return EmptyResponse(202);
            }
            if (!hasMethod)
            {
                return JsonRpcError(400, -32600, "Invalid JSON-RPC message.");
            }

            const McpDispatchResult dispatched = session->dispatcher.DispatchLine(request.body);
            if (dispatched.initializedTransition)
            {
                session->initialized.store(true, std::memory_order_release);
                operationLock.unlock();
                PublishSessionStatus();
            }
            if (!dispatched.hasResponse)
            {
                return EmptyResponse(202);
            }
            return JsonResponse(dispatched.responseLine);
        }

        HttpMcpResponse HandleDelete(const HttpMcpRequest& request)
        {
            std::string sessionId;
            HttpMcpResponse validation = ValidateSessionHeaders(request, sessionId);
            if (validation.status != 200)
            {
                return validation;
            }
            const std::shared_ptr<Session> session = FindSession(sessionId);
            if (!session)
            {
                return JsonRpcError(404, -32001, "MCP session not found or expired.");
            }

            {
                std::lock_guard operationLock(session->operationMutex);
                session->closed.store(true, std::memory_order_release);
            }
            {
                std::lock_guard lock(sessionsMutex);
                const auto found = sessions.find(sessionId);
                if (found != sessions.end() && found->second == session)
                {
                    sessions.erase(found);
                }
            }
            PublishSessionStatus();
            return EmptyResponse(204);
        }

        HttpMcpResponse Handle(const HttpMcpRequest& request)
        {
            if (request.path != "/mcp")
            {
                return EmptyResponse(404);
            }
            HttpMcpResponse origin = ValidateOrigin(request);
            if (origin.status != 200)
            {
                return origin;
            }

            const std::string method = LowerAscii(request.method);
            if (method == "post")
            {
                return HandlePost(request);
            }
            if (method == "delete")
            {
                return HandleDelete(request);
            }
            if (method == "get")
            {
                HttpMcpResponse response = EmptyResponse(405);
                response.headers.emplace("Allow", "POST, DELETE");
                return response;
            }
            HttpMcpResponse response = EmptyResponse(405);
            response.headers.emplace("Allow", "POST, DELETE");
            return response;
        }

        void ExpireIdleSessions()
        {
            const Clock::time_point cutoff = Clock::now() - options.sessionIdleTimeout;
            std::vector<std::pair<std::string, std::shared_ptr<Session>>> candidates;
            {
                std::lock_guard lock(sessionsMutex);
                for (const auto& entry : sessions)
                {
                    candidates.push_back(entry);
                }
            }

            bool removed = false;
            for (const auto& [id, session] : candidates)
            {
                std::lock_guard operationLock(session->operationMutex);
                if (session->closed.load(std::memory_order_acquire) || session->lastActivity > cutoff)
                {
                    continue;
                }
                session->closed.store(true, std::memory_order_release);
                std::lock_guard lock(sessionsMutex);
                const auto found = sessions.find(id);
                if (found != sessions.end() && found->second == session)
                {
                    sessions.erase(found);
                    removed = true;
                }
            }
            if (removed)
            {
                PublishSessionStatus();
            }
        }

        void CloseAllSessions()
        {
            std::unordered_map<std::string, std::shared_ptr<Session>> closing;
            {
                std::lock_guard lock(sessionsMutex);
                closing.swap(sessions);
            }
            for (const auto& [id, session] : closing)
            {
                (void)id;
                std::lock_guard operationLock(session->operationMutex);
                session->closed.store(true, std::memory_order_release);
            }
            store.SetMcpSessionStatus(0, 0);
        }

        std::size_t ActiveSessionCount() const
        {
            std::lock_guard lock(sessionsMutex);
            return sessions.size();
        }

        SceneStateStore& store;
        HttpMcpServerOptions options;
        std::vector<std::string> allowedOrigins;
        mutable std::mutex sessionsMutex;
        std::unordered_map<std::string, std::shared_ptr<Session>> sessions;
    };

    HttpMcpEndpoint::HttpMcpEndpoint(SceneStateStore& store, HttpMcpServerOptions options)
        : impl_(std::make_unique<Impl>(store, std::move(options)))
    {
    }

    HttpMcpEndpoint::~HttpMcpEndpoint()
    {
        impl_->CloseAllSessions();
    }

    HttpMcpResponse HttpMcpEndpoint::Handle(const HttpMcpRequest& request)
    {
        return impl_->Handle(request);
    }

    void HttpMcpEndpoint::ExpireIdleSessions()
    {
        impl_->ExpireIdleSessions();
    }

    void HttpMcpEndpoint::CloseAllSessions()
    {
        impl_->CloseAllSessions();
    }

    std::size_t HttpMcpEndpoint::ActiveSessionCount() const
    {
        return impl_->ActiveSessionCount();
    }

    std::string HttpMcpEndpoint::EndpointUrl() const
    {
        return "http://127.0.0.1:" + std::to_string(impl_->options.port) + "/mcp";
    }

    struct HttpMcpServer::Impl
    {
        Impl(SceneStateStore& stateStore, HttpMcpServerOptions serverOptions)
            : store(stateStore), options(std::move(serverOptions)), endpoint(stateStore, options)
        {
        }

        ~Impl()
        {
            Stop();
            Join();
        }

        static HttpMcpRequest AdaptRequest(const httplib::Request& request, const char* method)
        {
            HttpMcpRequest adapted;
            adapted.method = method;
            adapted.path = request.path;
            adapted.body = request.body;
            for (const auto& [name, value] : request.headers)
            {
                adapted.headers[name] = value;
            }
            return adapted;
        }

        static void ApplyResponse(const HttpMcpResponse& source, httplib::Response& destination)
        {
            destination.status = source.status;
            for (const auto& [name, value] : source.headers)
            {
                if (LowerAscii(name) != "content-type")
                {
                    destination.set_header(name, value);
                }
            }
            if (!source.body.empty())
            {
                const auto contentType = FindHeader(source.headers, "Content-Type");
                destination.set_content(source.body, contentType.value_or("application/json; charset=utf-8"));
            }
        }

        bool Start(std::string& error)
        {
            if (worker.joinable())
            {
                error = "The MCP HTTP server has already been started.";
                return false;
            }

            server.set_payload_max_length(HttpMcpEndpoint::MaximumMessageBytes);
            server.Post("/mcp", [this](const httplib::Request& request, httplib::Response& response)
            {
                ApplyResponse(endpoint.Handle(AdaptRequest(request, "POST")), response);
            });
            server.Get("/mcp", [this](const httplib::Request& request, httplib::Response& response)
            {
                ApplyResponse(endpoint.Handle(AdaptRequest(request, "GET")), response);
            });
            server.Delete("/mcp", [this](const httplib::Request& request, httplib::Response& response)
            {
                ApplyResponse(endpoint.Handle(AdaptRequest(request, "DELETE")), response);
            });

            if (!server.bind_to_port("127.0.0.1", options.port))
            {
                error = "Cannot bind MCP HTTP endpoint " + endpoint.EndpointUrl() + ".";
                return false;
            }

            stopping.store(false, std::memory_order_release);
            running.store(true, std::memory_order_release);
            store.ConfigureMcpTransport("streamable-http", endpoint.EndpointUrl());
            store.SetMcpRequested(true);
            store.SetMcpRunning(true);
            try
            {
                worker = std::thread([this]
                {
                    if (!server.listen_after_bind() && !stopping.load(std::memory_order_acquire))
                    {
                        store.RecordMcpActivity(
                            "transport",
                            {},
                            false,
                            "MCP HTTP listener stopped unexpectedly.",
                            false);
                    }
                    running.store(false, std::memory_order_release);
                    store.SetMcpRunning(false);
                });
                cleanupWorker = std::thread([this]
                {
                    std::unique_lock lock(cleanupMutex);
                    while (!stopping.load(std::memory_order_acquire))
                    {
                        if (cleanupCondition.wait_for(lock, options.cleanupInterval, [this]
                        {
                            return stopping.load(std::memory_order_acquire);
                        }))
                        {
                            break;
                        }
                        lock.unlock();
                        endpoint.ExpireIdleSessions();
                        lock.lock();
                    }
                });
            }
            catch (const std::exception& exception)
            {
                error = std::string("Failed to create MCP HTTP worker thread: ") + exception.what();
                Stop();
                Join();
                return false;
            }
            error.clear();
            return true;
        }

        void Stop()
        {
            if (stopping.exchange(true, std::memory_order_acq_rel))
            {
                return;
            }
            cleanupCondition.notify_all();
            server.stop();
        }

        void Join()
        {
            if (cleanupWorker.joinable() && cleanupWorker.get_id() != std::this_thread::get_id())
            {
                cleanupWorker.join();
            }
            if (worker.joinable() && worker.get_id() != std::this_thread::get_id())
            {
                worker.join();
            }
            endpoint.CloseAllSessions();
            running.store(false, std::memory_order_release);
            store.SetMcpRunning(false);
        }

        SceneStateStore& store;
        HttpMcpServerOptions options;
        HttpMcpEndpoint endpoint;
        httplib::Server server;
        std::thread worker;
        std::thread cleanupWorker;
        std::mutex cleanupMutex;
        std::condition_variable cleanupCondition;
        std::atomic<bool> stopping{ false };
        std::atomic<bool> running{ false };
    };

    HttpMcpServer::HttpMcpServer(SceneStateStore& store, HttpMcpServerOptions options)
        : impl_(std::make_unique<Impl>(store, std::move(options)))
    {
    }

    HttpMcpServer::~HttpMcpServer() = default;

    bool HttpMcpServer::Start(void* nativeWindow, std::string& error)
    {
        (void)nativeWindow;
        return impl_->Start(error);
    }

    void HttpMcpServer::Stop()
    {
        impl_->Stop();
    }

    void HttpMcpServer::Join()
    {
        impl_->Join();
    }

    bool HttpMcpServer::IsRunning() const noexcept
    {
        return impl_->running.load(std::memory_order_acquire);
    }

    bool ParseHttpMcpCommandLine(
        std::uint16_t defaultPort,
        HttpMcpServerOptions& options,
        bool& showHelp,
        std::string& error)
    {
        options = HttpMcpServerOptions{};
        options.port = defaultPort;
        showHelp = false;

        int argumentCount = 0;
        wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            error = "CommandLineToArgvW failed.";
            return false;
        }

        bool succeeded = true;
        for (int index = 1; index < argumentCount && succeeded; ++index)
        {
            const std::wstring argument(arguments[index]);
            if (argument == L"--help" || argument == L"-h" || argument == L"/?")
            {
                showHelp = true;
            }
            else if (argument == L"--port")
            {
                if (++index >= argumentCount)
                {
                    error = "--port requires a value in [1, 65535].";
                    succeeded = false;
                    break;
                }
                wchar_t* end = nullptr;
                const long value = std::wcstol(arguments[index], &end, 10);
                if (end == arguments[index] || *end != L'\0' || value < 1 || value > 65535)
                {
                    error = "--port requires a value in [1, 65535].";
                    succeeded = false;
                    break;
                }
                options.port = static_cast<std::uint16_t>(value);
            }
            else if (argument == L"--allow-origin")
            {
                if (++index >= argumentCount)
                {
                    error = "--allow-origin requires an exact origin value.";
                    succeeded = false;
                    break;
                }
                std::string origin = WideToUtf8(arguments[index]);
                const std::size_t authorityStart = origin.rfind("http://", 0) == 0 ? 7u :
                    (origin.rfind("https://", 0) == 0 ? 8u : std::string::npos);
                const bool hasInvalidSuffix = authorityStart == std::string::npos ||
                    authorityStart >= origin.size() ||
                    origin.find_first_of("/?# ", authorityStart) != std::string::npos;
                if (origin.empty() || hasInvalidSuffix)
                {
                    error = "--allow-origin must be an absolute HTTP or HTTPS origin.";
                    succeeded = false;
                    break;
                }
                options.allowedOrigins.push_back(std::move(origin));
            }
            else if (argument == L"-warp" || argument == L"/warp")
            {
                // Retained for the D3D12 sample and ignored by the HTTP option parser.
            }
            else
            {
                error = "Unknown command-line argument: " + WideToUtf8(arguments[index]);
                succeeded = false;
            }
        }
        LocalFree(arguments);
        if (succeeded)
        {
            error.clear();
        }
        return succeeded;
    }

    std::string HttpMcpCommandLineHelp(std::uint16_t defaultPort)
    {
        return
            "Options:\r\n"
            "  --port <1..65535>          HTTP listen port (default " + std::to_string(defaultPort) + ")\r\n"
            "  --allow-origin <origin>    Add an exact allowed Origin; may be repeated\r\n"
            "  --help                     Show this help\r\n"
            "The server always binds to 127.0.0.1 and exposes /mcp.\r\n";
    }
}
