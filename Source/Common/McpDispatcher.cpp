#include "McpDispatcher.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace sample::common
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr int ParseError = -32700;
        constexpr int InvalidRequest = -32600;
        constexpr int MethodNotFound = -32601;
        constexpr int InvalidParams = -32602;
        constexpr int InternalError = -32603;
        constexpr int ResourceNotFound = -32002;

        struct HandlerResult
        {
            bool hasResponse = true;
            Json response;
            bool succeeded = true;
            std::string tool;
            std::string message;
        };

        Json ErrorResponse(const Json& id, int code, const std::string& message)
        {
            return Json{
                { "jsonrpc", "2.0" },
                { "id", id },
                { "error", { { "code", code }, { "message", message } } },
            };
        }

        Json ResultResponse(const Json& id, Json result)
        {
            return Json{
                { "jsonrpc", "2.0" },
                { "id", id },
                { "result", std::move(result) },
            };
        }

        bool IsValidId(const Json& id)
        {
            return id.is_string() || id.is_number_integer() || id.is_number_unsigned() || id.is_null();
        }

        bool HasOnlyKeys(const Json& object, std::initializer_list<const char*> keys)
        {
            if (!object.is_object())
            {
                return false;
            }
            std::unordered_set<std::string> allowed;
            for (const char* key : keys)
            {
                allowed.emplace(key);
            }
            for (const auto& item : object.items())
            {
                if (allowed.find(item.key()) == allowed.end())
                {
                    return false;
                }
            }
            return true;
        }

        bool ReadFloat(const Json& value, float& output)
        {
            if (!value.is_number())
            {
                return false;
            }
            const double number = value.get<double>();
            if (!std::isfinite(number) ||
                number < -static_cast<double>(std::numeric_limits<float>::max()) ||
                number > static_cast<double>(std::numeric_limits<float>::max()))
            {
                return false;
            }
            output = static_cast<float>(number);
            return true;
        }

        bool ReadVector3(const Json& value, Vector3& output)
        {
            return value.is_object() && value.size() == 3 && HasOnlyKeys(value, { "x", "y", "z" }) &&
                value.contains("x") && value.contains("y") && value.contains("z") &&
                ReadFloat(value.at("x"), output.x) &&
                ReadFloat(value.at("y"), output.y) &&
                ReadFloat(value.at("z"), output.z);
        }

        bool ReadColor3(const Json& value, Color3& output)
        {
            return value.is_object() && value.size() == 3 && HasOnlyKeys(value, { "r", "g", "b" }) &&
                value.contains("r") && value.contains("g") && value.contains("b") &&
                ReadFloat(value.at("r"), output.r) &&
                ReadFloat(value.at("g"), output.g) &&
                ReadFloat(value.at("b"), output.b);
        }

        Json ToJson(const Vector3& value)
        {
            return { { "x", value.x }, { "y", value.y }, { "z", value.z } };
        }

        Json ToJson(const Color3& value)
        {
            return { { "r", value.r }, { "g", value.g }, { "b", value.b } };
        }

        Json ToJson(const SceneState& state)
        {
            return {
                { "revision", state.revision },
                { "camera", {
                    { "position", ToJson(state.camera.position) },
                    { "target", ToJson(state.camera.target) },
                    { "fov_degrees", state.camera.fovDegrees },
                } },
                { "light", {
                    { "direction", ToJson(state.light.direction) },
                    { "color", ToJson(state.light.color) },
                    { "intensity", state.light.intensity },
                } },
                { "transform", {
                    { "translation", ToJson(state.transform.translation) },
                    { "rotation_degrees", ToJson(state.transform.rotationDegrees) },
                    { "scale", ToJson(state.transform.scale) },
                } },
            };
        }

        Json VectorSchema(const char* first, const char* second, const char* third, float minimum, float maximum)
        {
            const Json number = { { "type", "number" }, { "minimum", minimum }, { "maximum", maximum } };
            return {
                { "type", "object" },
                { "properties", {
                    { first, number },
                    { second, number },
                    { third, number },
                } },
                { "required", { first, second, third } },
                { "additionalProperties", false },
            };
        }

        Json UnboundedVectorSchema(const char* first, const char* second, const char* third)
        {
            const Json number = { { "type", "number" } };
            return {
                { "type", "object" },
                { "properties", {
                    { first, number },
                    { second, number },
                    { third, number },
                } },
                { "required", { first, second, third } },
                { "additionalProperties", false },
            };
        }

        Json SceneOutputSchema()
        {
            return {
                { "$schema", "https://json-schema.org/draft/2020-12/schema" },
                { "type", "object" },
                { "properties", {
                    { "revision", { { "type", "integer" }, { "minimum", 0 } } },
                    { "camera", {
                        { "type", "object" },
                        { "properties", {
                            { "position", VectorSchema("x", "y", "z", -10.0f, 10.0f) },
                            { "target", VectorSchema("x", "y", "z", -10.0f, 10.0f) },
                            { "fov_degrees", { { "type", "number" }, { "minimum", 15.0f }, { "maximum", 90.0f } } },
                        } },
                        { "required", { "position", "target", "fov_degrees" } },
                        { "additionalProperties", false },
                    } },
                    { "light", {
                        { "type", "object" },
                        { "properties", {
                            { "direction", VectorSchema("x", "y", "z", -1.0f, 1.0f) },
                            { "color", VectorSchema("r", "g", "b", 0.0f, 1.0f) },
                            { "intensity", { { "type", "number" }, { "minimum", 0.0f }, { "maximum", 10.0f } } },
                        } },
                        { "required", { "direction", "color", "intensity" } },
                        { "additionalProperties", false },
                    } },
                    { "transform", {
                        { "type", "object" },
                        { "properties", {
                            { "translation", VectorSchema("x", "y", "z", -5.0f, 5.0f) },
                            { "rotation_degrees", VectorSchema("x", "y", "z", -180.0f, 180.0f) },
                            { "scale", VectorSchema("x", "y", "z", 0.1f, 3.0f) },
                        } },
                        { "required", { "translation", "rotation_degrees", "scale" } },
                        { "additionalProperties", false },
                    } },
                } },
                { "required", { "revision", "camera", "light", "transform" } },
                { "additionalProperties", false },
            };
        }

        Json ToolAnnotations(bool readOnly)
        {
            return {
                { "readOnlyHint", readOnly },
                { "destructiveHint", false },
                { "idempotentHint", true },
                { "openWorldHint", false },
            };
        }

        Json ToolList()
        {
            const Json outputSchema = SceneOutputSchema();
            return Json::array({
                {
                    { "name", "get_scene_state" },
                    { "title", "Get scene state" },
                    { "description", "Return the current camera, directional light, model transform, and revision." },
                    { "inputSchema", {
                        { "$schema", "https://json-schema.org/draft/2020-12/schema" },
                        { "type", "object" },
                        { "properties", Json::object() },
                        { "additionalProperties", false },
                    } },
                    { "outputSchema", outputSchema },
                    { "annotations", ToolAnnotations(true) },
                },
                {
                    { "name", "set_camera" },
                    { "title", "Set camera" },
                    { "description", "Partially update camera position, target, or field of view." },
                    { "inputSchema", {
                        { "$schema", "https://json-schema.org/draft/2020-12/schema" },
                        { "type", "object" },
                        { "properties", {
                            { "position", VectorSchema("x", "y", "z", -10.0f, 10.0f) },
                            { "target", VectorSchema("x", "y", "z", -10.0f, 10.0f) },
                            { "fov_degrees", { { "type", "number" }, { "minimum", 15.0f }, { "maximum", 90.0f } } },
                        } },
                        { "minProperties", 1 },
                        { "additionalProperties", false },
                    } },
                    { "outputSchema", outputSchema },
                    { "annotations", ToolAnnotations(false) },
                },
                {
                    { "name", "set_light" },
                    { "title", "Set directional light" },
                    { "description", "Partially update directional light direction, color, or intensity." },
                    { "inputSchema", {
                        { "$schema", "https://json-schema.org/draft/2020-12/schema" },
                        { "type", "object" },
                        { "properties", {
                            { "direction", UnboundedVectorSchema("x", "y", "z") },
                            { "color", VectorSchema("r", "g", "b", 0.0f, 1.0f) },
                            { "intensity", { { "type", "number" }, { "minimum", 0.0f }, { "maximum", 10.0f } } },
                        } },
                        { "minProperties", 1 },
                        { "additionalProperties", false },
                    } },
                    { "outputSchema", outputSchema },
                    { "annotations", ToolAnnotations(false) },
                },
                {
                    { "name", "set_transform" },
                    { "title", "Set model transform" },
                    { "description", "Partially update model translation, XYZ rotation in degrees, or scale." },
                    { "inputSchema", {
                        { "$schema", "https://json-schema.org/draft/2020-12/schema" },
                        { "type", "object" },
                        { "properties", {
                            { "translation", VectorSchema("x", "y", "z", -5.0f, 5.0f) },
                            { "rotation_degrees", VectorSchema("x", "y", "z", -180.0f, 180.0f) },
                            { "scale", VectorSchema("x", "y", "z", 0.1f, 3.0f) },
                        } },
                        { "minProperties", 1 },
                        { "additionalProperties", false },
                    } },
                    { "outputSchema", outputSchema },
                    { "annotations", ToolAnnotations(false) },
                },
                {
                    { "name", "reset_scene" },
                    { "title", "Reset scene" },
                    { "description", "Reset camera, light, transform, or the complete scene to sample defaults." },
                    { "inputSchema", {
                        { "$schema", "https://json-schema.org/draft/2020-12/schema" },
                        { "type", "object" },
                        { "properties", {
                            { "target", { { "type", "string" }, { "enum", { "camera", "light", "transform", "all" } }, { "default", "all" } } },
                        } },
                        { "additionalProperties", false },
                    } },
                    { "outputSchema", outputSchema },
                    { "annotations", ToolAnnotations(false) },
                },
            });
        }

        Json ToolSuccess(const SceneState& state)
        {
            Json structured = ToJson(state);
            return {
                { "content", Json::array({ { { "type", "text" }, { "text", structured.dump() } } }) },
                { "structuredContent", std::move(structured) },
                { "isError", false },
            };
        }

        Json ToolFailure(const std::string& message)
        {
            return {
                { "content", Json::array({ { { "type", "text" }, { "text", message } } }) },
                { "isError", true },
            };
        }

        HandlerResult ProtocolFailure(const Json& id, int code, const std::string& message, std::string tool = {})
        {
            return HandlerResult{ true, ErrorResponse(id, code, message), false, std::move(tool), message };
        }

        HandlerResult ToolExecutionResult(const Json& id, const std::string& tool, const UpdateResult& update)
        {
            if (!update.succeeded)
            {
                return HandlerResult{
                    true,
                    ResultResponse(id, ToolFailure(update.message)),
                    false,
                    tool,
                    update.message,
                };
            }
            return HandlerResult{ true, ResultResponse(id, ToolSuccess(update.state)), true, tool, {} };
        }

        HandlerResult HandleToolCall(SceneStateStore& store, const Json& id, const Json& params)
        {
            if (!params.is_object() || !HasOnlyKeys(params, { "name", "arguments" }) ||
                !params.contains("name") || !params.at("name").is_string())
            {
                return ProtocolFailure(id, InvalidParams, "tools/call requires a tool name and optional object arguments.");
            }

            const std::string tool = params.at("name").get<std::string>();
            const Json arguments = params.contains("arguments") ? params.at("arguments") : Json::object();
            if (!arguments.is_object())
            {
                return ProtocolFailure(id, InvalidParams, "Tool arguments must be an object.", tool);
            }

            if (tool == "get_scene_state")
            {
                if (!arguments.empty())
                {
                    return ProtocolFailure(id, InvalidParams, "get_scene_state does not accept arguments.", tool);
                }
                return HandlerResult{ true, ResultResponse(id, ToolSuccess(store.GetSceneState())), true, tool, {} };
            }

            if (tool == "set_camera")
            {
                if (arguments.empty() || !HasOnlyKeys(arguments, { "position", "target", "fov_degrees" }))
                {
                    return ProtocolFailure(id, InvalidParams, "set_camera requires at least one recognized field.", tool);
                }
                CameraPatch patch;
                if (arguments.contains("position"))
                {
                    Vector3 value;
                    if (!ReadVector3(arguments.at("position"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "position must contain numeric x, y, and z only.", tool);
                    }
                    patch.position = value;
                }
                if (arguments.contains("target"))
                {
                    Vector3 value;
                    if (!ReadVector3(arguments.at("target"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "target must contain numeric x, y, and z only.", tool);
                    }
                    patch.target = value;
                }
                if (arguments.contains("fov_degrees"))
                {
                    float value = 0.0f;
                    if (!ReadFloat(arguments.at("fov_degrees"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "fov_degrees must be a finite number.", tool);
                    }
                    patch.fovDegrees = value;
                }
                return ToolExecutionResult(id, tool, store.ApplyCameraPatch(patch, MutationSource::Mcp));
            }

            if (tool == "set_light")
            {
                if (arguments.empty() || !HasOnlyKeys(arguments, { "direction", "color", "intensity" }))
                {
                    return ProtocolFailure(id, InvalidParams, "set_light requires at least one recognized field.", tool);
                }
                LightPatch patch;
                if (arguments.contains("direction"))
                {
                    Vector3 value;
                    if (!ReadVector3(arguments.at("direction"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "direction must contain numeric x, y, and z only.", tool);
                    }
                    patch.direction = value;
                }
                if (arguments.contains("color"))
                {
                    Color3 value;
                    if (!ReadColor3(arguments.at("color"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "color must contain numeric r, g, and b only.", tool);
                    }
                    patch.color = value;
                }
                if (arguments.contains("intensity"))
                {
                    float value = 0.0f;
                    if (!ReadFloat(arguments.at("intensity"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "intensity must be a finite number.", tool);
                    }
                    patch.intensity = value;
                }
                return ToolExecutionResult(id, tool, store.ApplyLightPatch(patch, MutationSource::Mcp));
            }

            if (tool == "set_transform")
            {
                if (arguments.empty() || !HasOnlyKeys(arguments, { "translation", "rotation_degrees", "scale" }))
                {
                    return ProtocolFailure(id, InvalidParams, "set_transform requires at least one recognized field.", tool);
                }
                TransformPatch patch;
                if (arguments.contains("translation"))
                {
                    Vector3 value;
                    if (!ReadVector3(arguments.at("translation"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "translation must contain numeric x, y, and z only.", tool);
                    }
                    patch.translation = value;
                }
                if (arguments.contains("rotation_degrees"))
                {
                    Vector3 value;
                    if (!ReadVector3(arguments.at("rotation_degrees"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "rotation_degrees must contain numeric x, y, and z only.", tool);
                    }
                    patch.rotationDegrees = value;
                }
                if (arguments.contains("scale"))
                {
                    Vector3 value;
                    if (!ReadVector3(arguments.at("scale"), value))
                    {
                        return ProtocolFailure(id, InvalidParams, "scale must contain numeric x, y, and z only.", tool);
                    }
                    patch.scale = value;
                }
                return ToolExecutionResult(id, tool, store.ApplyTransformPatch(patch, MutationSource::Mcp));
            }

            if (tool == "reset_scene")
            {
                if (!HasOnlyKeys(arguments, { "target" }))
                {
                    return ProtocolFailure(id, InvalidParams, "reset_scene accepts only target.", tool);
                }
                ResetTarget target = ResetTarget::All;
                if (arguments.contains("target"))
                {
                    if (!arguments.at("target").is_string())
                    {
                        return ProtocolFailure(id, InvalidParams, "reset target must be camera, light, transform, or all.", tool);
                    }
                    const std::string value = arguments.at("target").get<std::string>();
                    if (value == "camera") target = ResetTarget::Camera;
                    else if (value == "light") target = ResetTarget::Light;
                    else if (value == "transform") target = ResetTarget::Transform;
                    else if (value != "all")
                    {
                        return ProtocolFailure(id, InvalidParams, "reset target must be camera, light, transform, or all.", tool);
                    }
                }
                return ToolExecutionResult(id, tool, store.ResetScene(target, MutationSource::Mcp));
            }

            return ProtocolFailure(id, InvalidParams, "Unknown tool: " + tool, tool);
        }

        Json AppInfoJson(const ApplicationSnapshot& app)
        {
            return {
                { "application", { { "name", app.applicationName }, { "version", app.applicationVersion } } },
                { "renderer", {
                    { "backend", app.rendererBackend },
                    { "gpu_name", app.gpuName },
                    { "ready", app.rendererReady },
                } },
                { "model", app.modelName },
                { "viewport", { { "width", app.viewportWidth }, { "height", app.viewportHeight } } },
                { "statistics", { { "fps", app.fps }, { "frame_count", app.frameCount } } },
                { "mcp", {
                    { "requested", app.mcpRequested },
                    { "running", app.mcpRunning },
                    { "initialized", app.mcpInitialized },
                    { "writes_enabled", app.mcpWritesEnabled },
                    { "request_count", app.mcpRequestCount },
                    { "last_error", app.mcpLastError },
                } },
            };
        }

        HandlerResult HandleResourceRead(SceneStateStore& store, const Json& id, const Json& params)
        {
            if (!params.is_object() || !HasOnlyKeys(params, { "uri" }) ||
                !params.contains("uri") || !params.at("uri").is_string())
            {
                return ProtocolFailure(id, InvalidParams, "resources/read requires a URI string.");
            }

            const std::string uri = params.at("uri").get<std::string>();
            Json contents;
            if (uri == "graphics://scene/state")
            {
                contents = ToJson(store.GetSceneState());
            }
            else if (uri == "graphics://app/info")
            {
                contents = AppInfoJson(store.GetApplicationSnapshot());
            }
            else
            {
                return ProtocolFailure(id, ResourceNotFound, "Resource not found: " + uri);
            }

            return HandlerResult{
                true,
                ResultResponse(id, {
                    { "contents", Json::array({ {
                        { "uri", uri },
                        { "mimeType", "application/json" },
                        { "text", contents.dump() },
                    } }) },
                }),
                true,
                {},
                {},
            };
        }
    }

    McpDispatcher::McpDispatcher(SceneStateStore& store)
        : store_(store)
    {
    }

    McpDispatchResult McpDispatcher::DispatchLine(std::string_view line)
    {
        Json request;
        try
        {
            request = Json::parse(line.begin(), line.end());
        }
        catch (const std::exception& exception)
        {
            const std::string message = std::string("Parse error: ") + exception.what();
            store_.RecordMcpActivity("<parse>", {}, false, message);
            return { true, ErrorResponse(nullptr, ParseError, "Parse error").dump() };
        }

        Json id = nullptr;
        if (request.is_object() && request.contains("id") && IsValidId(request.at("id")))
        {
            id = request.at("id");
        }

        if (!request.is_object() || !request.contains("jsonrpc") || request.at("jsonrpc") != "2.0" ||
            !request.contains("method") || !request.at("method").is_string() ||
            (request.contains("id") && !IsValidId(request.at("id"))))
        {
            store_.RecordMcpActivity("<invalid>", {}, false, "Invalid JSON-RPC request.");
            return { true, ErrorResponse(id, InvalidRequest, "Invalid Request").dump() };
        }

        const std::string method = request.at("method").get<std::string>();
        const bool isNotification = !request.contains("id");
        const Json params = request.contains("params") ? request.at("params") : Json::object();
        if (!params.is_object())
        {
            store_.RecordMcpActivity(method, {}, false, "params must be an object.", !isNotification);
            if (isNotification)
            {
                return {};
            }
            return { true, ErrorResponse(id, InvalidParams, "params must be an object.").dump() };
        }

        HandlerResult handled;
        try
        {
            std::lock_guard lock(protocolMutex_);

            if (method == "initialize")
            {
                if (isNotification || initializeSeen_ || !params.contains("protocolVersion") ||
                    !params.at("protocolVersion").is_string() ||
                    !params.contains("clientInfo") || !params.at("clientInfo").is_object())
                {
                    handled = ProtocolFailure(id, InvalidRequest, "Invalid initialize request.");
                }
                else
                {
                    initializeSeen_ = true;
                    handled.response = ResultResponse(id, {
                        { "protocolVersion", ProtocolVersion },
                        { "capabilities", { { "tools", Json::object() }, { "resources", Json::object() } } },
                        { "serverInfo", { { "name", "SimpleMCPGraphicsSample" }, { "version", "1.0.0" } } },
                        { "instructions", "Read the scene state before changing it. Write tools can be disabled with Allow MCP writes in the application UI." },
                    });
                }
            }
            else if (method == "notifications/initialized")
            {
                if (!isNotification)
                {
                    handled = ProtocolFailure(id, InvalidRequest, "notifications/initialized must be a notification.");
                }
                else if (!initializeSeen_)
                {
                    handled.hasResponse = false;
                    handled.succeeded = false;
                    handled.message = "Unexpected initialized notification.";
                }
                else
                {
                    handled.hasResponse = false;
                    initialized_ = true;
                    store_.SetMcpInitialized(true);
                }
            }
            else if (method == "notifications/cancelled")
            {
                if (isNotification)
                {
                    handled.hasResponse = false;
                }
                else
                {
                    handled = ProtocolFailure(id, InvalidRequest, "notifications/cancelled must be a notification.");
                }
            }
            else if (method == "ping")
            {
                if (isNotification)
                {
                    handled.hasResponse = false;
                }
                else
                {
                    handled.response = ResultResponse(id, Json::object());
                }
            }
            else if (!initialized_)
            {
                if (isNotification)
                {
                    handled.hasResponse = false;
                    handled.succeeded = false;
                    handled.message = "Server is not initialized.";
                }
                else
                {
                    handled = ProtocolFailure(id, InvalidRequest, "Server is not initialized.");
                }
            }
            else if (method == "tools/list")
            {
                handled.response = ResultResponse(id, { { "tools", ToolList() } });
                handled.hasResponse = !isNotification;
            }
            else if (method == "tools/call")
            {
                handled = isNotification ? HandlerResult{ false } : HandleToolCall(store_, id, params);
            }
            else if (method == "resources/list")
            {
                handled.response = ResultResponse(id, {
                    { "resources", Json::array({
                        {
                            { "uri", "graphics://scene/state" },
                            { "name", "Scene state" },
                            { "description", "Current camera, directional light, model transform, and revision." },
                            { "mimeType", "application/json" },
                        },
                        {
                            { "uri", "graphics://app/info" },
                            { "name", "Application information" },
                            { "description", "Renderer, GPU, viewport, frame statistics, and MCP status." },
                            { "mimeType", "application/json" },
                        },
                    }) },
                });
                handled.hasResponse = !isNotification;
            }
            else if (method == "resources/read")
            {
                handled = isNotification ? HandlerResult{ false } : HandleResourceRead(store_, id, params);
            }
            else if (method == "resources/templates/list")
            {
                handled.response = ResultResponse(id, { { "resourceTemplates", Json::array() } });
                handled.hasResponse = !isNotification;
            }
            else
            {
                handled.succeeded = false;
                handled.message = "Method not found: " + method;
                if (isNotification)
                {
                    handled.hasResponse = false;
                }
                else
                {
                    handled.response = ErrorResponse(id, MethodNotFound, "Method not found");
                }
            }
        }
        catch (const std::exception& exception)
        {
            handled = isNotification
                ? HandlerResult{ false, {}, false, {}, exception.what() }
                : ProtocolFailure(id, InternalError, "Internal error");
        }
        catch (...)
        {
            handled = isNotification
                ? HandlerResult{ false, {}, false, {}, "Unknown internal error." }
                : ProtocolFailure(id, InternalError, "Internal error");
        }

        store_.RecordMcpActivity(method, handled.tool, handled.succeeded, handled.message, !isNotification);
        if (!handled.hasResponse || isNotification)
        {
            return {};
        }
        return { true, handled.response.dump() };
    }
}
