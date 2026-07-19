#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sample::common
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Color3
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };

    struct CameraState
    {
        Vector3 position;
        Vector3 target;
        float fovDegrees = 45.0f;
    };

    struct LightState
    {
        Vector3 direction;
        Color3 color;
        float intensity = 5.0f;
    };

    struct TransformState
    {
        Vector3 translation;
        Vector3 rotationDegrees;
        Vector3 scale;
    };

    struct SceneState
    {
        std::uint64_t revision = 0;
        CameraState camera;
        LightState light;
        TransformState transform;
    };

    struct CameraPatch
    {
        std::optional<Vector3> position;
        std::optional<Vector3> target;
        std::optional<float> fovDegrees;

        [[nodiscard]] bool Empty() const noexcept;
    };

    struct LightPatch
    {
        std::optional<Vector3> direction;
        std::optional<Color3> color;
        std::optional<float> intensity;

        [[nodiscard]] bool Empty() const noexcept;
    };

    struct TransformPatch
    {
        std::optional<Vector3> translation;
        std::optional<Vector3> rotationDegrees;
        std::optional<Vector3> scale;

        [[nodiscard]] bool Empty() const noexcept;
    };

    enum class MutationSource
    {
        Ui,
        Mcp,
    };

    enum class ResetTarget
    {
        Camera,
        Light,
        Transform,
        All,
    };

    struct UpdateResult
    {
        bool succeeded = false;
        bool changed = false;
        std::string message;
        SceneState state;
    };

    struct McpLogEntry
    {
        std::string timestamp;
        std::string method;
        std::string tool;
        bool succeeded = false;
        std::string message;
    };

    struct ApplicationSnapshot
    {
        std::string applicationName = "SimpleMCPGraphicsSample";
        std::string applicationVersion = "1.0.0";
        std::string rendererBackend = "Unknown";
        std::string gpuName = "Unknown";
        std::string modelName = "SciFiHelmet";
        std::uint32_t viewportWidth = 1280;
        std::uint32_t viewportHeight = 720;
        bool rendererReady = false;
        double fps = 0.0;
        std::uint64_t frameCount = 0;

        bool mcpRequested = false;
        bool mcpRunning = false;
        bool mcpInitialized = false;
        bool mcpWritesEnabled = true;
        std::string mcpTransport = "none";
        std::string mcpEndpoint;
        std::uint32_t mcpActiveSessions = 0;
        std::uint64_t mcpRequestCount = 0;
        std::string mcpLastError;
        std::vector<McpLogEntry> mcpLogs;
    };

    class SceneStateStore
    {
    public:
        static constexpr std::size_t MaxLogEntries = 200;

        SceneStateStore();

        [[nodiscard]] static CameraState DefaultCamera() noexcept;
        [[nodiscard]] static LightState DefaultLight() noexcept;
        [[nodiscard]] static TransformState DefaultTransform() noexcept;

        [[nodiscard]] SceneState GetSceneState() const;
        [[nodiscard]] ApplicationSnapshot GetApplicationSnapshot() const;

        UpdateResult ApplyCameraPatch(const CameraPatch& patch, MutationSource source);
        UpdateResult ApplyLightPatch(const LightPatch& patch, MutationSource source);
        UpdateResult ApplyTransformPatch(const TransformPatch& patch, MutationSource source);
        UpdateResult ResetScene(ResetTarget target, MutationSource source);

        void SetMcpWritesEnabled(bool enabled);
        [[nodiscard]] bool McpWritesEnabled() const;
        void SetMcpRequested(bool requested);
        void SetMcpRunning(bool running);
        void SetMcpInitialized(bool initialized);
        void ConfigureMcpTransport(std::string transport, std::string endpoint);
        void SetMcpSessionStatus(std::uint32_t activeSessions, std::uint32_t initializedSessions);
        void RecordMcpActivity(
            const std::string& method,
            const std::string& tool,
            bool succeeded,
            const std::string& message,
            bool countAsRequest = true);
        void ClearMcpLogs();

        void UpdateRendererInfo(std::string backend, std::string gpuName, bool ready);
        void SetRendererReady(bool ready);
        void UpdateFrameStats(double fps, std::uint64_t frameCount);

    private:
        [[nodiscard]] UpdateResult RejectedLocked(std::string message) const;
        [[nodiscard]] bool McpWriteAllowedLocked(MutationSource source) const noexcept;

        mutable std::mutex mutex_;
        SceneState scene_;
        ApplicationSnapshot application_;
        std::deque<McpLogEntry> logs_;
    };

    [[nodiscard]] bool NearlyEqual(float left, float right) noexcept;
    [[nodiscard]] bool Equal(const Vector3& left, const Vector3& right) noexcept;
    [[nodiscard]] bool Equal(const Color3& left, const Color3& right) noexcept;
    [[nodiscard]] bool Equal(const CameraState& left, const CameraState& right) noexcept;
    [[nodiscard]] bool Equal(const LightState& left, const LightState& right) noexcept;
    [[nodiscard]] bool Equal(const TransformState& left, const TransformState& right) noexcept;
}
