#include "McpDispatcher.h"
#include "SceneState.h"
#include "StdioMcpServer.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Json = nlohmann::json;
    using namespace sample::common;

    class TestRunner
    {
    public:
        void Check(bool condition, const std::string& description)
        {
            if (condition)
            {
                ++passed_;
                return;
            }
            ++failed_;
            std::cerr << "FAILED: " << description << '\n';
        }

        template<typename Function>
        void Case(const char* name, Function&& function)
        {
            try
            {
                function();
                std::cout << "[case] " << name << '\n';
            }
            catch (const std::exception& exception)
            {
                ++failed_;
                std::cerr << "EXCEPTION in " << name << ": " << exception.what() << '\n';
            }
            catch (...)
            {
                ++failed_;
                std::cerr << "UNKNOWN EXCEPTION in " << name << '\n';
            }
        }

        int Finish() const
        {
            std::cout << "Passed checks: " << passed_ << ", failed checks: " << failed_ << '\n';
            return failed_ == 0 ? 0 : 1;
        }

    private:
        int passed_ = 0;
        int failed_ = 0;
    };

    Json Dispatch(McpDispatcher& dispatcher, const Json& request)
    {
        const McpDispatchResult response = dispatcher.DispatchLine(request.dump());
        if (!response.hasResponse)
        {
            throw std::runtime_error("Expected a JSON-RPC response.");
        }
        return Json::parse(response.responseLine);
    }

    void Initialize(McpDispatcher& dispatcher)
    {
        Dispatch(dispatcher, {
            { "jsonrpc", "2.0" },
            { "id", 1 },
            { "method", "initialize" },
            { "params", {
                { "protocolVersion", McpDispatcher::ProtocolVersion },
                { "capabilities", Json::object() },
                { "clientInfo", { { "name", "tests" }, { "version", "1" } } },
            } },
        });
        const McpDispatchResult initialized = dispatcher.DispatchLine(Json{
            { "jsonrpc", "2.0" },
            { "method", "notifications/initialized" },
            { "params", Json::object() },
        }.dump());
        if (initialized.hasResponse)
        {
            throw std::runtime_error("initialized notification produced a response.");
        }
    }

    const Json& Result(const Json& response)
    {
        if (!response.contains("result"))
        {
            throw std::runtime_error("Response does not contain result: " + response.dump());
        }
        return response.at("result");
    }

    int ErrorCode(const Json& response)
    {
        return response.at("error").at("code").get<int>();
    }
}

int main()
{
    TestRunner tests;

    tests.Case("scene defaults, partial updates, revision, and reset", [&]
    {
        SceneStateStore store;
        const SceneState defaults = store.GetSceneState();
        tests.Check(defaults.revision == 0, "initial revision is zero");
        tests.Check(Equal(defaults.camera, SceneStateStore::DefaultCamera()), "camera defaults match specification");
        tests.Check(Equal(defaults.camera.position, Vector3{ 0.0f, 0.4f, 5.2f }), "camera default views the front of the model");
        tests.Check(Equal(defaults.light, SceneStateStore::DefaultLight()), "light defaults match specification");
        tests.Check(Equal(defaults.transform, SceneStateStore::DefaultTransform()), "transform defaults match specification");
        tests.Check(Equal(defaults.transform.scale, Vector3{ 1.2f, 1.2f, 1.2f }), "transform default preserves the model scale");

        CameraPatch camera;
        camera.fovDegrees = 60.0f;
        UpdateResult update = store.ApplyCameraPatch(camera, MutationSource::Ui);
        tests.Check(update.succeeded && update.changed && update.state.revision == 1, "camera patch increments revision");
        tests.Check(NearlyEqual(update.state.camera.fovDegrees, 60.0f), "camera patch updates only requested field");
        tests.Check(Equal(update.state.camera.position, defaults.camera.position), "camera position is not overwritten");

        update = store.ApplyCameraPatch(camera, MutationSource::Ui);
        tests.Check(update.succeeded && !update.changed && update.state.revision == 1, "same camera patch is idempotent");

        LightPatch light;
        light.intensity = 2.0f;
        update = store.ApplyLightPatch(light, MutationSource::Ui);
        tests.Check(update.succeeded && update.changed && update.state.revision == 2, "light patch increments revision once");
        tests.Check(Equal(update.state.light.direction, defaults.light.direction), "partial light patch preserves direction");

        TransformPatch transform;
        transform.rotationDegrees = Vector3{ 10.0f, 20.0f, 30.0f };
        update = store.ApplyTransformPatch(transform, MutationSource::Ui);
        tests.Check(update.succeeded && update.changed && update.state.revision == 3, "transform patch increments revision once");
        tests.Check(Equal(update.state.transform.translation, defaults.transform.translation), "partial transform patch preserves translation");

        update = store.ResetScene(ResetTarget::Transform, MutationSource::Ui);
        tests.Check(update.succeeded && update.changed && update.state.revision == 4, "transform reset increments revision");
        tests.Check(Equal(update.state.transform, defaults.transform), "transform reset restores defaults");

        update = store.ResetScene(ResetTarget::Camera, MutationSource::Ui);
        tests.Check(update.succeeded && update.changed && update.state.revision == 5, "camera reset increments revision");
        tests.Check(Equal(update.state.camera, defaults.camera), "camera reset restores defaults");
        tests.Check(NearlyEqual(update.state.light.intensity, 2.0f), "camera reset preserves light");

        update = store.ResetScene(ResetTarget::All, MutationSource::Ui);
        tests.Check(update.succeeded && update.changed && update.state.revision == 6, "all reset increments revision once");
        tests.Check(Equal(update.state.light, defaults.light), "all reset restores light");
        update = store.ResetScene(ResetTarget::All, MutationSource::Ui);
        tests.Check(update.succeeded && !update.changed && update.state.revision == 6, "reset is idempotent");
    });

    tests.Case("validation and MCP write gate", [&]
    {
        SceneStateStore store;
        CameraPatch emptyCamera;
        tests.Check(!store.ApplyCameraPatch(emptyCamera, MutationSource::Ui).succeeded, "empty camera patch is rejected");

        CameraPatch invalid;
        invalid.fovDegrees = std::numeric_limits<float>::quiet_NaN();
        tests.Check(!store.ApplyCameraPatch(invalid, MutationSource::Ui).succeeded, "NaN is rejected");
        invalid.fovDegrees = std::numeric_limits<float>::infinity();
        tests.Check(!store.ApplyCameraPatch(invalid, MutationSource::Ui).succeeded, "infinity is rejected");
        invalid.fovDegrees = 14.0f;
        tests.Check(!store.ApplyCameraPatch(invalid, MutationSource::Ui).succeeded, "FOV below range is rejected");
        invalid.fovDegrees = 91.0f;
        tests.Check(!store.ApplyCameraPatch(invalid, MutationSource::Ui).succeeded, "FOV above range is rejected");

        invalid = {};
        invalid.position = Vector3{ 11.0f, 0.0f, 0.0f };
        tests.Check(!store.ApplyCameraPatch(invalid, MutationSource::Ui).succeeded, "camera component outside range is rejected");
        invalid.position = SceneStateStore::DefaultCamera().target;
        tests.Check(!store.ApplyCameraPatch(invalid, MutationSource::Ui).succeeded, "matching position and target are rejected");
        invalid.position = Vector3{ 0.0f, -1.0f, 0.0f };
        invalid.target = Vector3{ 0.0f, 1.0f, 0.0f };
        tests.Check(!store.ApplyCameraPatch(invalid, MutationSource::Ui).succeeded, "view parallel to up is rejected");

        LightPatch invalidLight;
        invalidLight.direction = Vector3{ 0.0f, 0.0f, 0.0f };
        tests.Check(!store.ApplyLightPatch(invalidLight, MutationSource::Ui).succeeded, "zero light direction is rejected");
        invalidLight.direction = Vector3{ 2.0f, 0.0f, 0.0f };
        const UpdateResult largeDirection = store.ApplyLightPatch(invalidLight, MutationSource::Ui);
        tests.Check(largeDirection.succeeded && Equal(largeDirection.state.light.direction, Vector3{ 1.0f, 0.0f, 0.0f }),
            "finite non-zero light direction is normalized without an artificial component range");
        invalidLight = {};
        invalidLight.color = Color3{ 1.1f, 0.0f, 0.0f };
        tests.Check(!store.ApplyLightPatch(invalidLight, MutationSource::Ui).succeeded, "light color outside range is rejected");
        invalidLight = {};
        invalidLight.intensity = 10.1f;
        tests.Check(!store.ApplyLightPatch(invalidLight, MutationSource::Ui).succeeded, "light intensity outside range is rejected");

        TransformPatch invalidTransform;
        tests.Check(!store.ApplyTransformPatch(invalidTransform, MutationSource::Ui).succeeded, "empty transform patch is rejected");
        invalidTransform.translation = Vector3{ std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f };
        tests.Check(!store.ApplyTransformPatch(invalidTransform, MutationSource::Ui).succeeded, "transform NaN is rejected");
        invalidTransform.translation = Vector3{ 5.1f, 0.0f, 0.0f };
        tests.Check(!store.ApplyTransformPatch(invalidTransform, MutationSource::Ui).succeeded, "transform translation outside range is rejected");
        invalidTransform = {};
        invalidTransform.rotationDegrees = Vector3{ 0.0f, 181.0f, 0.0f };
        tests.Check(!store.ApplyTransformPatch(invalidTransform, MutationSource::Ui).succeeded, "transform rotation outside range is rejected");
        invalidTransform = {};
        invalidTransform.scale = Vector3{ 1.0f, 0.0f, 1.0f };
        tests.Check(!store.ApplyTransformPatch(invalidTransform, MutationSource::Ui).succeeded, "transform scale outside range is rejected");

        LightPatch normalized;
        normalized.direction = Vector3{ 0.5f, 0.0f, 0.0f };
        const UpdateResult normalizedResult = store.ApplyLightPatch(normalized, MutationSource::Ui);
        tests.Check(normalizedResult.succeeded && Equal(normalizedResult.state.light.direction, Vector3{ 1.0f, 0.0f, 0.0f }),
            "accepted light direction is normalized");

        store.SetMcpWritesEnabled(false);
        CameraPatch gated;
        gated.fovDegrees = 50.0f;
        const std::uint64_t revision = store.GetSceneState().revision;
        tests.Check(!store.ApplyCameraPatch(gated, MutationSource::Mcp).succeeded, "MCP write is rejected when gate is off");
        tests.Check(store.GetSceneState().revision == revision, "rejected MCP write does not change revision");
        tests.Check(store.ApplyCameraPatch(gated, MutationSource::Ui).succeeded, "UI write remains enabled when MCP gate is off");
        TransformPatch gatedTransform;
        gatedTransform.translation = Vector3{ 1.0f, 0.0f, 0.0f };
        tests.Check(!store.ApplyTransformPatch(gatedTransform, MutationSource::Mcp).succeeded, "transform MCP write obeys the gate");
    });

    tests.Case("concurrent partial updates and bounded log", [&]
    {
        SceneStateStore store;
        std::atomic<bool> start{ false };
        std::thread positionWriter([&]
        {
            while (!start.load(std::memory_order_acquire)) {}
            for (int index = 0; index < 100; ++index)
            {
                CameraPatch patch;
                patch.position = Vector3{ 1.0f, 0.4f, 5.2f };
                store.ApplyCameraPatch(patch, MutationSource::Ui);
            }
        });
        std::thread fovWriter([&]
        {
            while (!start.load(std::memory_order_acquire)) {}
            for (int index = 0; index < 100; ++index)
            {
                CameraPatch patch;
                patch.fovDegrees = 65.0f;
                store.ApplyCameraPatch(patch, MutationSource::Ui);
            }
        });
        std::thread transformWriter([&]
        {
            while (!start.load(std::memory_order_acquire)) {}
            for (int index = 0; index < 100; ++index)
            {
                TransformPatch patch;
                patch.translation = Vector3{ 0.5f, 0.0f, 0.0f };
                store.ApplyTransformPatch(patch, MutationSource::Ui);
            }
        });
        start.store(true, std::memory_order_release);
        positionWriter.join();
        fovWriter.join();
        transformWriter.join();

        const SceneState state = store.GetSceneState();
        tests.Check(Equal(state.camera.position, Vector3{ 1.0f, 0.4f, 5.2f }), "concurrent position update is retained");
        tests.Check(NearlyEqual(state.camera.fovDegrees, 65.0f), "concurrent FOV update is retained");
        tests.Check(Equal(state.transform.translation, Vector3{ 0.5f, 0.0f, 0.0f }), "concurrent transform update is retained");
        tests.Check(state.revision == 3, "idempotent concurrent updates increment revision only for real changes");

        for (int index = 0; index < 205; ++index)
        {
            store.RecordMcpActivity("ping", {}, true, {});
        }
        const ApplicationSnapshot app = store.GetApplicationSnapshot();
        tests.Check(app.mcpRequestCount == 205, "request count is monotonic");
        tests.Check(app.mcpLogs.size() == SceneStateStore::MaxLogEntries, "log ring retains only 200 entries");
        store.ClearMcpLogs();
        tests.Check(store.GetApplicationSnapshot().mcpLogs.empty(), "log clear empties ring");
    });

    tests.Case("JSON-RPC lifecycle, tools, schemas, and notifications", [&]
    {
        SceneStateStore store;
        McpDispatcher dispatcher(store);

        Json response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "tools/list" }, { "params", Json::object() },
        });
        tests.Check(ErrorCode(response) == -32600, "tool call before initialization is rejected");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" },
            { "id", "init" },
            { "method", "initialize" },
            { "params", {
                { "protocolVersion", McpDispatcher::ProtocolVersion },
                { "capabilities", Json::object() },
                { "clientInfo", { { "name", "tests" }, { "version", "1" } } },
            } },
        });
        tests.Check(Result(response).at("protocolVersion") == "2025-11-25", "initialize negotiates fixed stable protocol");
        tests.Check(Result(response).at("capabilities").size() == 2, "only tools and resources capabilities are declared");

        const McpDispatchResult initialized = dispatcher.DispatchLine(Json{
            { "jsonrpc", "2.0" }, { "method", "notifications/initialized" }, { "params", Json::object() },
        }.dump());
        tests.Check(!initialized.hasResponse, "initialized notification receives no response");
        tests.Check(store.GetApplicationSnapshot().mcpInitialized, "initialized state is exposed to UI/resource");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 2 }, { "method", "ping" }, { "params", Json::object() },
        });
        tests.Check(Result(response).empty(), "ping returns empty result");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 3 }, { "method", "tools/list" }, { "params", Json::object() },
        });
        const Json& tools = Result(response).at("tools");
        tests.Check(tools.size() == 5, "five tools are listed");
        for (const Json& tool : tools)
        {
            tests.Check(tool.contains("inputSchema") && tool.contains("outputSchema") && tool.contains("annotations"),
                "every tool has input/output schemas and annotations");
            tests.Check(tool.at("inputSchema").at("additionalProperties") == false, "tool input schemas are closed world");
            tests.Check(tool.at("annotations").at("idempotentHint") == true &&
                tool.at("annotations").at("openWorldHint") == false, "tool annotations describe idempotent closed-world behavior");
        }

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 4 }, { "method", "tools/call" },
            { "params", { { "name", "get_scene_state" }, { "arguments", Json::object() } } },
        });
        const Json structured = Result(response).at("structuredContent");
        const Json textual = Json::parse(Result(response).at("content").at(0).at("text").get<std::string>());
        tests.Check(structured == textual, "tool text and structured content contain identical state");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 5 }, { "method", "tools/call" },
            { "params", { { "name", "set_camera" }, { "arguments", { { "fov_degrees", 55.0 } } } } },
        });
        tests.Check(Result(response).at("isError") == false, "valid set_camera succeeds");
        const std::uint64_t revision = Result(response).at("structuredContent").at("revision").get<std::uint64_t>();
        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 6 }, { "method", "tools/call" },
            { "params", { { "name", "set_camera" }, { "arguments", { { "fov_degrees", 55.0 } } } } },
        });
        tests.Check(Result(response).at("structuredContent").at("revision") == revision, "same tool write is idempotent");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 7 }, { "method", "tools/call" },
            { "params", { { "name", "set_transform" }, { "arguments", {
                { "translation", { { "x", 0.4 }, { "y", -0.2 }, { "z", 0.1 } } },
                { "rotation_degrees", { { "x", 5.0 }, { "y", 35.0 }, { "z", 0.0 } } },
            } } } },
        });
        tests.Check(Result(response).at("isError") == false, "valid set_transform succeeds");
        tests.Check(Result(response).at("structuredContent").at("transform").at("rotation_degrees").at("y") == 35.0,
            "set_transform returns the updated model rotation");

        store.SetMcpWritesEnabled(false);
        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 8 }, { "method", "tools/call" },
            { "params", { { "name", "set_light" }, { "arguments", { { "intensity", 1.0 } } } } },
        });
        tests.Check(Result(response).at("isError") == true, "MCP write gate returns a tool execution error");
        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 9 }, { "method", "tools/call" },
            { "params", { { "name", "get_scene_state" }, { "arguments", Json::object() } } },
        });
        tests.Check(Result(response).at("isError") == false, "read tool remains available with write gate off");

        const std::vector<Json> notifications = {
            Json{ { "jsonrpc", "2.0" }, { "method", "notifications/cancelled" }, { "params", { { "requestId", 99 } } } },
            Json{ { "jsonrpc", "2.0" }, { "method", "unknown/notification" }, { "params", Json::object() } },
            Json{ { "jsonrpc", "2.0" }, { "method", "ping" }, { "params", Json::object() } },
        };
        for (const Json& notification : notifications)
        {
            tests.Check(!dispatcher.DispatchLine(notification.dump()).hasResponse, "notifications never receive a response");
        }
    });

    tests.Case("resources and JSON-RPC errors", [&]
    {
        SceneStateStore store;
        store.UpdateRendererInfo("D3D12", "Test GPU", true);
        store.UpdateFrameStats(60.0, 123);
        McpDispatcher dispatcher(store);
        Initialize(dispatcher);

        Json response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 10 }, { "method", "resources/list" }, { "params", Json::object() },
        });
        tests.Check(Result(response).at("resources").size() == 2, "two resources are listed");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 11 }, { "method", "resources/read" },
            { "params", { { "uri", "graphics://scene/state" } } },
        });
        Json resource = Json::parse(Result(response).at("contents").at(0).at("text").get<std::string>());
        tests.Check(resource.contains("camera") && resource.contains("light") && resource.contains("transform") &&
            resource.contains("revision"), "scene resource contains full snapshot");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 12 }, { "method", "resources/read" },
            { "params", { { "uri", "graphics://app/info" } } },
        });
        resource = Json::parse(Result(response).at("contents").at(0).at("text").get<std::string>());
        tests.Check(resource.at("renderer").at("backend") == "D3D12", "app resource reports renderer backend");
        tests.Check(resource.at("statistics").at("frame_count") == 123, "app resource reports frame count");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 13 }, { "method", "resources/templates/list" }, { "params", Json::object() },
        });
        tests.Check(Result(response).at("resourceTemplates").empty(), "resource template list is empty");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 14 }, { "method", "resources/read" },
            { "params", { { "uri", "graphics://missing" } } },
        });
        tests.Check(ErrorCode(response) == -32002, "missing resource uses MCP resource-not-found code");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 15 }, { "method", "unknown/method" }, { "params", Json::object() },
        });
        tests.Check(ErrorCode(response) == -32601, "unknown method uses method-not-found code");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 16 }, { "method", "tools/call" },
            { "params", { { "name", "missing_tool" }, { "arguments", Json::object() } } },
        });
        tests.Check(ErrorCode(response) == -32602, "unknown tool uses invalid-params code");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 17 }, { "method", "tools/call" },
            { "params", { { "name", "set_camera" }, { "arguments", Json::object() } } },
        });
        tests.Check(ErrorCode(response) == -32602, "malformed tool input uses invalid-params code");

        response = Dispatch(dispatcher, {
            { "jsonrpc", "2.0" }, { "id", 18 }, { "method", "tools/call" },
            { "params", { { "name", "set_camera" }, { "arguments", { { "fov_degrees", 200.0 } } } } },
        });
        tests.Check(Result(response).at("isError") == true, "out-of-range value is a tool execution error");

        McpDispatchResult raw = dispatcher.DispatchLine("{");
        tests.Check(raw.hasResponse && ErrorCode(Json::parse(raw.responseLine)) == -32700, "invalid JSON uses parse-error code");
        raw = dispatcher.DispatchLine(R"({"jsonrpc":"1.0","id":19,"method":"ping"})");
        tests.Check(raw.hasResponse && ErrorCode(Json::parse(raw.responseLine)) == -32600, "invalid JSON-RPC envelope uses invalid-request code");
        raw = dispatcher.DispatchLine(R"([{"jsonrpc":"2.0","id":20,"method":"ping"}])");
        tests.Check(raw.hasResponse && ErrorCode(Json::parse(raw.responseLine)) == -32600, "JSON-RPC batches are rejected");
    });

    tests.Case("newline framing, CRLF, EOF, and 1 MiB limit", [&]
    {
        NewlineJsonFramer framer;
        FramingResult result = framer.Feed("{\"a\":");
        tests.Check(result.messages.empty() && !result.failed, "partial input is buffered");
        result = framer.Feed("1}\r\n{\"b\":2}\nthird");
        tests.Check(result.messages.size() == 2, "multiple messages are extracted from one chunk");
        tests.Check(result.messages.at(0) == R"({"a":1})", "CR is stripped from CRLF input");
        tests.Check(result.messages.at(1) == R"({"b":2})", "LF-delimited message is preserved");
        result = framer.Finish();
        tests.Check(result.messages.size() == 1 && result.messages.at(0) == "third", "unterminated final message is emitted on EOF");

        framer.Reset();
        std::string maximum(NewlineJsonFramer::MaximumMessageBytes, 'x');
        result = framer.Feed(maximum);
        tests.Check(!result.failed, "exactly 1 MiB is accepted");
        result = framer.Feed("\n");
        tests.Check(!result.failed && result.messages.size() == 1 && result.messages.at(0).size() == maximum.size(),
            "1 MiB message is emitted intact");

        framer.Reset();
        result = framer.Feed(std::string(NewlineJsonFramer::MaximumMessageBytes + 1, 'x'));
        tests.Check(result.failed && result.messages.empty(), "message over 1 MiB fails without partial delivery");
    });

    return tests.Finish();
}
