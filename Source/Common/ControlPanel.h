#pragma once

#include "SceneState.h"

namespace sample::common
{
    // Call after ImGui::NewFrame() and before ImGui::Render().
    void DrawControlPanel(SceneStateStore& store);
}
