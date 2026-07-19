#include "ControlPanel.h"

#include <imgui.h>

#include <cstdio>
#include <string>

namespace sample::common
{
    namespace
    {
        void DrawCameraPanel(SceneStateStore& store, const SceneState& scene)
        {
            ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(330.0f, 190.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Camera"))
            {
                ImGui::End();
                return;
            }

            float position[3] = { scene.camera.position.x, scene.camera.position.y, scene.camera.position.z };
            if (ImGui::DragFloat3("Position", position, 0.01f, -10.0f, 10.0f, "%.2f"))
            {
                CameraPatch patch;
                patch.position = Vector3{ position[0], position[1], position[2] };
                store.ApplyCameraPatch(patch, MutationSource::Ui);
            }

            float target[3] = { scene.camera.target.x, scene.camera.target.y, scene.camera.target.z };
            if (ImGui::DragFloat3("Target", target, 0.01f, -10.0f, 10.0f, "%.2f"))
            {
                CameraPatch patch;
                patch.target = Vector3{ target[0], target[1], target[2] };
                store.ApplyCameraPatch(patch, MutationSource::Ui);
            }

            float fov = scene.camera.fovDegrees;
            if (ImGui::SliderFloat("FOV (degrees)", &fov, 15.0f, 90.0f, "%.1f"))
            {
                CameraPatch patch;
                patch.fovDegrees = fov;
                store.ApplyCameraPatch(patch, MutationSource::Ui);
            }

            if (ImGui::Button("Reset camera"))
            {
                store.ResetScene(ResetTarget::Camera, MutationSource::Ui);
            }
            ImGui::SameLine();
            ImGui::Text("revision: %llu", static_cast<unsigned long long>(scene.revision));
            ImGui::End();
        }

        void DrawLightPanel(SceneStateStore& store, const SceneState& scene)
        {
            ImGui::SetNextWindowPos(ImVec2(20.0f, 225.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(330.0f, 190.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Directional Light"))
            {
                ImGui::End();
                return;
            }

            float direction[3] = { scene.light.direction.x, scene.light.direction.y, scene.light.direction.z };
            if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.2f"))
            {
                LightPatch patch;
                patch.direction = Vector3{ direction[0], direction[1], direction[2] };
                store.ApplyLightPatch(patch, MutationSource::Ui);
            }

            float color[3] = { scene.light.color.r, scene.light.color.g, scene.light.color.b };
            if (ImGui::ColorEdit3("Color", color))
            {
                LightPatch patch;
                patch.color = Color3{ color[0], color[1], color[2] };
                store.ApplyLightPatch(patch, MutationSource::Ui);
            }

            float intensity = scene.light.intensity;
            if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 10.0f, "%.2f"))
            {
                LightPatch patch;
                patch.intensity = intensity;
                store.ApplyLightPatch(patch, MutationSource::Ui);
            }

            if (ImGui::Button("Reset light"))
            {
                store.ResetScene(ResetTarget::Light, MutationSource::Ui);
            }
            ImGui::End();
        }

        void DrawTransformPanel(SceneStateStore& store, const SceneState& scene)
        {
            ImGui::SetNextWindowPos(ImVec2(20.0f, 430.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380.0f, 190.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Model Transform"))
            {
                ImGui::End();
                return;
            }

            float translation[3] = {
                scene.transform.translation.x,
                scene.transform.translation.y,
                scene.transform.translation.z,
            };
            if (ImGui::DragFloat3("Translation", translation, 0.01f, -5.0f, 5.0f, "%.2f"))
            {
                TransformPatch patch;
                patch.translation = Vector3{ translation[0], translation[1], translation[2] };
                store.ApplyTransformPatch(patch, MutationSource::Ui);
            }

            float rotation[3] = {
                scene.transform.rotationDegrees.x,
                scene.transform.rotationDegrees.y,
                scene.transform.rotationDegrees.z,
            };
            if (ImGui::DragFloat3("Rotation (deg)", rotation, 0.5f, -180.0f, 180.0f, "%.1f"))
            {
                TransformPatch patch;
                patch.rotationDegrees = Vector3{ rotation[0], rotation[1], rotation[2] };
                store.ApplyTransformPatch(patch, MutationSource::Ui);
            }

            float scale[3] = { scene.transform.scale.x, scene.transform.scale.y, scene.transform.scale.z };
            if (ImGui::DragFloat3("Scale", scale, 0.01f, 0.1f, 3.0f, "%.2f"))
            {
                TransformPatch patch;
                patch.scale = Vector3{ scale[0], scale[1], scale[2] };
                store.ApplyTransformPatch(patch, MutationSource::Ui);
            }

            if (ImGui::Button("Reset transform"))
            {
                store.ResetScene(ResetTarget::Transform, MutationSource::Ui);
            }
            ImGui::End();
        }

        const char* ConnectionText(const ApplicationSnapshot& application)
        {
            if (!application.mcpRequested) return "Not started (launch with --mcp)";
            if (!application.mcpRunning) return "Disconnected";
            if (application.mcpTransport == "streamable-http" && application.mcpActiveSessions == 0)
            {
                return "Listening";
            }
            if (!application.mcpInitialized) return "Connected, initializing";
            return "Connected";
        }

        void DrawMcpPanel(SceneStateStore& store, const ApplicationSnapshot& application)
        {
            ImGui::SetNextWindowPos(ImVec2(920.0f, 20.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(340.0f, 470.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("MCP"))
            {
                ImGui::End();
                return;
            }

            ImGui::Text("Status: %s", ConnectionText(application));
            ImGui::Text("Transport: %s", application.mcpTransport.c_str());
            if (!application.mcpEndpoint.empty())
            {
                ImGui::TextWrapped("Endpoint: %s", application.mcpEndpoint.c_str());
            }
            if (application.mcpTransport == "streamable-http")
            {
                ImGui::Text("Sessions: %u", application.mcpActiveSessions);
            }
            bool writesEnabled = application.mcpWritesEnabled;
            ImGui::BeginDisabled(!application.mcpRequested);
            if (ImGui::Checkbox("Allow MCP writes", &writesEnabled))
            {
                store.SetMcpWritesEnabled(writesEnabled);
            }
            ImGui::EndDisabled();
            ImGui::Text("Requests: %llu", static_cast<unsigned long long>(application.mcpRequestCount));

            ImGui::SeparatorText("Last error");
            if (application.mcpLastError.empty())
            {
                ImGui::TextDisabled("None");
            }
            else
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(application.mcpLastError.c_str());
                ImGui::PopTextWrapPos();
            }

            ImGui::SeparatorText("Request log");
            if (ImGui::Button("Clear"))
            {
                store.ClearMcpLogs();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("latest %zu / %zu", application.mcpLogs.size(), SceneStateStore::MaxLogEntries);

            if (ImGui::BeginChild("MCP request log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
            {
                for (const McpLogEntry& entry : application.mcpLogs)
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        entry.succeeded ? ImVec4(0.75f, 0.9f, 0.75f, 1.0f) : ImVec4(1.0f, 0.65f, 0.65f, 1.0f));
                    ImGui::TextUnformatted(entry.timestamp.c_str());
                    ImGui::PopStyleColor();

                    std::string operation = entry.method;
                    if (!entry.tool.empty())
                    {
                        operation += " / ";
                        operation += entry.tool;
                    }
                    operation += entry.succeeded ? " [ok]" : " [failed]";
                    ImGui::TextUnformatted(operation.c_str());
                    if (!entry.message.empty())
                    {
                        ImGui::TextWrapped("%s", entry.message.c_str());
                    }
                    ImGui::Separator();
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
            ImGui::End();
        }
    }

    void DrawControlPanel(SceneStateStore& store)
    {
        const SceneState scene = store.GetSceneState();
        const ApplicationSnapshot application = store.GetApplicationSnapshot();
        DrawCameraPanel(store, scene);
        DrawLightPanel(store, scene);
        DrawTransformPanel(store, scene);
        DrawMcpPanel(store, application);
    }
}
