#include "SceneState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <utility>

namespace sample::common
{
    namespace
    {
        constexpr float EqualityEpsilon = 1.0e-6f;
        constexpr float DirectionEpsilon = 1.0e-6f;
        constexpr float ParallelLimit = 0.999f;

        bool IsFinite(float value) noexcept
        {
            return std::isfinite(value) != 0;
        }

        bool IsFinite(const Vector3& value) noexcept
        {
            return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
        }

        bool IsFinite(const Color3& value) noexcept
        {
            return IsFinite(value.r) && IsFinite(value.g) && IsFinite(value.b);
        }

        bool InRange(float value, float minimum, float maximum) noexcept
        {
            return value >= minimum && value <= maximum;
        }

        bool InRange(const Vector3& value, float minimum, float maximum) noexcept
        {
            return InRange(value.x, minimum, maximum) &&
                InRange(value.y, minimum, maximum) &&
                InRange(value.z, minimum, maximum);
        }

        bool ValidateCamera(const CameraState& camera, std::string& error)
        {
            if (!IsFinite(camera.position) || !IsFinite(camera.target) || !IsFinite(camera.fovDegrees))
            {
                error = "Camera values must be finite.";
                return false;
            }

            if (!InRange(camera.position, -10.0f, 10.0f) || !InRange(camera.target, -10.0f, 10.0f))
            {
                error = "Camera position and target components must be in [-10, 10].";
                return false;
            }

            if (!InRange(camera.fovDegrees, 15.0f, 90.0f))
            {
                error = "Camera FOV must be in [15, 90] degrees.";
                return false;
            }

            const float dx = camera.target.x - camera.position.x;
            const float dy = camera.target.y - camera.position.y;
            const float dz = camera.target.z - camera.position.z;
            const float lengthSquared = dx * dx + dy * dy + dz * dz;
            if (lengthSquared <= DirectionEpsilon * DirectionEpsilon)
            {
                error = "Camera position and target must be different.";
                return false;
            }

            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            const float upDot = std::fabs(dy * inverseLength);
            if (upDot >= ParallelLimit)
            {
                error = "Camera view direction must not be parallel to the world up vector.";
                return false;
            }

            return true;
        }

        bool ValidateAndNormalizeLight(LightState& light, bool normalizeDirection, std::string& error)
        {
            if (!IsFinite(light.direction) || !IsFinite(light.color) || !IsFinite(light.intensity))
            {
                error = "Light values must be finite.";
                return false;
            }

            const float lengthSquared = light.direction.x * light.direction.x +
                light.direction.y * light.direction.y +
                light.direction.z * light.direction.z;
            if (lengthSquared <= DirectionEpsilon * DirectionEpsilon)
            {
                error = "Light direction must be non-zero.";
                return false;
            }

            if (!InRange(light.color.r, 0.0f, 1.0f) ||
                !InRange(light.color.g, 0.0f, 1.0f) ||
                !InRange(light.color.b, 0.0f, 1.0f))
            {
                error = "Light color components must be in [0, 1].";
                return false;
            }

            if (!InRange(light.intensity, 0.0f, 10.0f))
            {
                error = "Light intensity must be in [0, 10].";
                return false;
            }

            if (normalizeDirection)
            {
                const float inverseLength = 1.0f / std::sqrt(lengthSquared);
                light.direction.x *= inverseLength;
                light.direction.y *= inverseLength;
                light.direction.z *= inverseLength;
            }
            return true;
        }

        bool ValidateTransform(const TransformState& transform, std::string& error)
        {
            if (!IsFinite(transform.translation) || !IsFinite(transform.rotationDegrees) || !IsFinite(transform.scale))
            {
                error = "Transform values must be finite.";
                return false;
            }

            if (!InRange(transform.translation, -5.0f, 5.0f))
            {
                error = "Transform translation components must be in [-5, 5].";
                return false;
            }
            if (!InRange(transform.rotationDegrees, -180.0f, 180.0f))
            {
                error = "Transform rotation components must be in [-180, 180] degrees.";
                return false;
            }
            if (!InRange(transform.scale, 0.1f, 3.0f))
            {
                error = "Transform scale components must be in [0.1, 3].";
                return false;
            }
            return true;
        }

        std::string CurrentUtcTimestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
            const std::time_t value = std::chrono::system_clock::to_time_t(now);
            std::tm utc{};
            gmtime_s(&utc, &value);

            char buffer[40]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                utc.tm_year + 1900,
                utc.tm_mon + 1,
                utc.tm_mday,
                utc.tm_hour,
                utc.tm_min,
                utc.tm_sec,
                static_cast<long long>(milliseconds.count()));
            return buffer;
        }
    }

    bool CameraPatch::Empty() const noexcept
    {
        return !position && !target && !fovDegrees;
    }

    bool LightPatch::Empty() const noexcept
    {
        return !direction && !color && !intensity;
    }

    bool TransformPatch::Empty() const noexcept
    {
        return !translation && !rotationDegrees && !scale;
    }

    SceneStateStore::SceneStateStore()
    {
        scene_.camera = DefaultCamera();
        scene_.light = DefaultLight();
        scene_.transform = DefaultTransform();
    }

    CameraState SceneStateStore::DefaultCamera() noexcept
    {
        return CameraState{ { 0.0f, 0.4f, 5.2f }, { 0.0f, 0.12f, 0.0f }, 45.0f };
    }

    LightState SceneStateStore::DefaultLight() noexcept
    {
        return LightState{ { -0.35f, -0.8f, 0.45f }, { 1.0f, 0.96f, 0.88f }, 5.0f };
    }

    TransformState SceneStateStore::DefaultTransform() noexcept
    {
        return TransformState{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.2f, 1.2f, 1.2f } };
    }

    SceneState SceneStateStore::GetSceneState() const
    {
        std::lock_guard lock(mutex_);
        return scene_;
    }

    ApplicationSnapshot SceneStateStore::GetApplicationSnapshot() const
    {
        std::lock_guard lock(mutex_);
        ApplicationSnapshot snapshot = application_;
        snapshot.mcpLogs.assign(logs_.begin(), logs_.end());
        return snapshot;
    }

    UpdateResult SceneStateStore::ApplyCameraPatch(const CameraPatch& patch, MutationSource source)
    {
        std::lock_guard lock(mutex_);
        if (!McpWriteAllowedLocked(source))
        {
            return RejectedLocked("MCP writes are disabled in the application UI.");
        }
        if (patch.Empty())
        {
            return RejectedLocked("At least one camera field is required.");
        }

        CameraState candidate = scene_.camera;
        if (patch.position) candidate.position = *patch.position;
        if (patch.target) candidate.target = *patch.target;
        if (patch.fovDegrees) candidate.fovDegrees = *patch.fovDegrees;

        std::string error;
        if (!ValidateCamera(candidate, error))
        {
            return RejectedLocked(std::move(error));
        }

        const bool changed = !Equal(candidate, scene_.camera);
        if (changed)
        {
            scene_.camera = candidate;
            ++scene_.revision;
        }
        return UpdateResult{ true, changed, {}, scene_ };
    }

    UpdateResult SceneStateStore::ApplyLightPatch(const LightPatch& patch, MutationSource source)
    {
        std::lock_guard lock(mutex_);
        if (!McpWriteAllowedLocked(source))
        {
            return RejectedLocked("MCP writes are disabled in the application UI.");
        }
        if (patch.Empty())
        {
            return RejectedLocked("At least one light field is required.");
        }

        LightState candidate = scene_.light;
        if (patch.direction) candidate.direction = *patch.direction;
        if (patch.color) candidate.color = *patch.color;
        if (patch.intensity) candidate.intensity = *patch.intensity;

        std::string error;
        if (!ValidateAndNormalizeLight(candidate, patch.direction.has_value(), error))
        {
            return RejectedLocked(std::move(error));
        }

        const bool changed = !Equal(candidate, scene_.light);
        if (changed)
        {
            scene_.light = candidate;
            ++scene_.revision;
        }
        return UpdateResult{ true, changed, {}, scene_ };
    }

    UpdateResult SceneStateStore::ApplyTransformPatch(const TransformPatch& patch, MutationSource source)
    {
        std::lock_guard lock(mutex_);
        if (!McpWriteAllowedLocked(source))
        {
            return RejectedLocked("MCP writes are disabled in the application UI.");
        }
        if (patch.Empty())
        {
            return RejectedLocked("At least one transform field is required.");
        }

        TransformState candidate = scene_.transform;
        if (patch.translation) candidate.translation = *patch.translation;
        if (patch.rotationDegrees) candidate.rotationDegrees = *patch.rotationDegrees;
        if (patch.scale) candidate.scale = *patch.scale;

        std::string error;
        if (!ValidateTransform(candidate, error))
        {
            return RejectedLocked(std::move(error));
        }

        const bool changed = !Equal(candidate, scene_.transform);
        if (changed)
        {
            scene_.transform = candidate;
            ++scene_.revision;
        }
        return UpdateResult{ true, changed, {}, scene_ };
    }

    UpdateResult SceneStateStore::ResetScene(ResetTarget target, MutationSource source)
    {
        std::lock_guard lock(mutex_);
        if (!McpWriteAllowedLocked(source))
        {
            return RejectedLocked("MCP writes are disabled in the application UI.");
        }

        bool changed = false;
        if (target == ResetTarget::Camera || target == ResetTarget::All)
        {
            const CameraState defaults = DefaultCamera();
            if (!Equal(defaults, scene_.camera))
            {
                scene_.camera = defaults;
                changed = true;
            }
        }
        if (target == ResetTarget::Light || target == ResetTarget::All)
        {
            const LightState defaults = DefaultLight();
            if (!Equal(defaults, scene_.light))
            {
                scene_.light = defaults;
                changed = true;
            }
        }
        if (target == ResetTarget::Transform || target == ResetTarget::All)
        {
            const TransformState defaults = DefaultTransform();
            if (!Equal(defaults, scene_.transform))
            {
                scene_.transform = defaults;
                changed = true;
            }
        }
        if (changed)
        {
            ++scene_.revision;
        }
        return UpdateResult{ true, changed, {}, scene_ };
    }

    void SceneStateStore::SetMcpWritesEnabled(bool enabled)
    {
        std::lock_guard lock(mutex_);
        application_.mcpWritesEnabled = enabled;
    }

    bool SceneStateStore::McpWritesEnabled() const
    {
        std::lock_guard lock(mutex_);
        return application_.mcpWritesEnabled;
    }

    void SceneStateStore::SetMcpRequested(bool requested)
    {
        std::lock_guard lock(mutex_);
        application_.mcpRequested = requested;
    }

    void SceneStateStore::SetMcpRunning(bool running)
    {
        std::lock_guard lock(mutex_);
        application_.mcpRunning = running;
        if (!running)
        {
            application_.mcpInitialized = false;
        }
    }

    void SceneStateStore::SetMcpInitialized(bool initialized)
    {
        std::lock_guard lock(mutex_);
        application_.mcpInitialized = initialized;
    }

    void SceneStateStore::RecordMcpActivity(
        const std::string& method,
        const std::string& tool,
        bool succeeded,
        const std::string& message,
        bool countAsRequest)
    {
        std::lock_guard lock(mutex_);
        if (countAsRequest)
        {
            ++application_.mcpRequestCount;
        }
        if (!succeeded && !message.empty())
        {
            application_.mcpLastError = message;
        }

        logs_.push_back(McpLogEntry{ CurrentUtcTimestamp(), method, tool, succeeded, message });
        while (logs_.size() > MaxLogEntries)
        {
            logs_.pop_front();
        }
    }

    void SceneStateStore::ClearMcpLogs()
    {
        std::lock_guard lock(mutex_);
        logs_.clear();
    }

    void SceneStateStore::UpdateRendererInfo(std::string backend, std::string gpuName, bool ready)
    {
        std::lock_guard lock(mutex_);
        application_.rendererBackend = std::move(backend);
        application_.gpuName = std::move(gpuName);
        application_.rendererReady = ready;
    }

    void SceneStateStore::SetRendererReady(bool ready)
    {
        std::lock_guard lock(mutex_);
        application_.rendererReady = ready;
    }

    void SceneStateStore::UpdateFrameStats(double fps, std::uint64_t frameCount)
    {
        std::lock_guard lock(mutex_);
        application_.fps = std::isfinite(fps) && fps >= 0.0 ? fps : 0.0;
        application_.frameCount = frameCount;
    }

    UpdateResult SceneStateStore::RejectedLocked(std::string message) const
    {
        return UpdateResult{ false, false, std::move(message), scene_ };
    }

    bool SceneStateStore::McpWriteAllowedLocked(MutationSource source) const noexcept
    {
        return source != MutationSource::Mcp || application_.mcpWritesEnabled;
    }

    bool NearlyEqual(float left, float right) noexcept
    {
        return std::fabs(left - right) <= EqualityEpsilon;
    }

    bool Equal(const Vector3& left, const Vector3& right) noexcept
    {
        return NearlyEqual(left.x, right.x) && NearlyEqual(left.y, right.y) && NearlyEqual(left.z, right.z);
    }

    bool Equal(const Color3& left, const Color3& right) noexcept
    {
        return NearlyEqual(left.r, right.r) && NearlyEqual(left.g, right.g) && NearlyEqual(left.b, right.b);
    }

    bool Equal(const CameraState& left, const CameraState& right) noexcept
    {
        return Equal(left.position, right.position) && Equal(left.target, right.target) &&
            NearlyEqual(left.fovDegrees, right.fovDegrees);
    }

    bool Equal(const LightState& left, const LightState& right) noexcept
    {
        return Equal(left.direction, right.direction) && Equal(left.color, right.color) &&
            NearlyEqual(left.intensity, right.intensity);
    }

    bool Equal(const TransformState& left, const TransformState& right) noexcept
    {
        return Equal(left.translation, right.translation) && Equal(left.rotationDegrees, right.rotationDegrees) &&
            Equal(left.scale, right.scale);
    }
}
