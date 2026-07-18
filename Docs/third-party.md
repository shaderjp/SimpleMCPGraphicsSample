# Third-party とライセンス

## Dear ImGui

- Repository: <https://github.com/ocornut/imgui>
- Version: `v1.92.8`
- Commit: `8936b58fe26e8c3da834b8f60b06511d537b4c63`
- Location: `ThirdParty/imgui` Git submodule
- License: MIT（submodule 内の `LICENSE.txt`）

## nlohmann/json

- Repository: <https://github.com/nlohmann/json>
- Version: `v3.12.0`
- Commit: `55f93686c01528224f448c19128836e7df245f72`
- Location: `ThirdParty/json` Git submodule
- License: MIT（submodule 内の `LICENSE.MIT`）

## Microsoft Direct3D 12 Agility SDK / DXC

- `Microsoft.Direct3D.D3D12` NuGet package: `1.619.1`
- `Microsoft.Direct3D.DXC` NuGet package: `1.8.2505.32`
- D3D12 SDK version exported by the application: `619`

各 package のライセンスは NuGet restore 後の package 内容を参照してください。

## Vulkan SDK

Vulkan headers、loader library、DXC はローカルの `VULKAN_SDK` から参照し、
repository には複製しません。検証環境は LunarG Vulkan SDK 1.4.341.1 です。

## SciFiHelmet

- Original model: “Sci Fi Helmet” by Michael Pavlovic
- glTF conversion: Norbert Nopper
- Source collection: KhronosGroup glTF Sample Assets
- Model / textures: CC0-1.0

クレジットと上流のライセンスメタデータは
`Assets/SciFiHelmet/README.md` および `Assets/SciFiHelmet/LICENSE.md` に同梱しています。
アプリケーションは `.gltf` を parse せず、同梱 `.bin` と PNG を直接読みます。

## 参照実装

描画部分は `CodexRealTimeGraphicsSamples` の SciFiHelmet / ImGuiLighting sample を
移植元としています。ローカルの `CodexRealTimeGraphicsSamples-main/` は参照専用で、
この repository の `.gitignore` により追跡されません。
