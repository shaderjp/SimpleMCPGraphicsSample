#pragma once

#include <functional>
#include <memory>
#include <string>

namespace sample::common
{
    class SceneStateStore;

    class IMcpTransportServer
    {
    public:
        virtual ~IMcpTransportServer() = default;

        virtual bool Start(void* nativeWindow, std::string& error) = 0;
        virtual void Stop() = 0;
        virtual void Join() = 0;
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
    };

    using McpTransportFactory =
        std::function<std::unique_ptr<IMcpTransportServer>(SceneStateStore& store)>;
}
