# SimpleMCPGraphicsSample

Direct3D 12 / Vulkan で SciFiHelmet を描画する Windows C++ サンプルに、
Dear ImGui の操作パネルと stdio MCP サーバーを組み込んだ最小構成の実装です。
UI と MCP は同じシーン状態を更新するため、UI から変更したカメラ、ライト、モデルTransformを
MCP クライアントで取得でき、MCP からの変更も次の描画フレームへ反映されます。

このリポジトリは、C++ アプリケーションに MCP サーバーを直接実装する際の
読みやすい出発点を目的としています。HTTP transport、認証、Prompts、Tasks、
Sampling などは意図的に含めていません。

![D3D12版をMCP接続した画面](Docs/images/d3d12-mcp.png)

## サンプル構成

| プロジェクト | 内容 |
| --- | --- |
| `SimpleMCPGraphicsSampleD3D12` | Direct3D 12 + Agility SDK + ImGui + stdio MCP |
| `SimpleMCPGraphicsSampleVulkan` | Vulkan + ImGui + stdio MCP |
| `SimpleMCPGraphicsSampleTests` | 状態管理、JSON-RPC、MCP、改行フレーミングの単体テスト |

各描画バックエンドは独立した実行ファイルです。描画コードは無理に抽象化せず、
状態管理、MCP、ImGui パネルだけを `Source/Common` で共有しています。

## 必要な環境

- Windows 10 または Windows 11
- Visual Studio 2022
  - Desktop development with C++
  - MSVC v143
  - Windows 10/11 SDK
- Vulkan SDK 1.4.341.1（Vulkan プロジェクトをビルドする場合）
  - `VULKAN_SDK` 環境変数が SDK ルートを指していること
  - `Include/vulkan/vulkan.h`、`Lib/vulkan-1.lib`、`Bin/dxc.exe` があること
- Git（submodule の取得に使用）

## 取得とビルド

```powershell
git clone --recurse-submodules https://github.com/shaderjp/SimpleMCPGraphicsSample.git
cd SimpleMCPGraphicsSample
```

既に clone 済みの場合は次を実行します。

```powershell
git submodule update --init --recursive
```

Visual Studio で `SimpleMCPGraphicsSample.sln` を開き、`x64` の `Debug` または
`Release` を選んでビルドします。Developer PowerShell から全プロジェクトを
ビルドする場合は次のとおりです。

```powershell
msbuild .\SimpleMCPGraphicsSample.sln /restore /m /p:Configuration=Debug /p:Platform=x64
```

出力は以下へ生成されます。

```text
Bin/<ProjectName>/x64/<Configuration>/
Obj/<ProjectName>/x64/<Configuration>/
```

D3D12 は `Microsoft.Direct3D.D3D12 1.619.1` と
`Microsoft.Direct3D.DXC 1.8.2505.32` を PackageReference で復元し、
Agility SDK 619 を実行ファイル横へ配置します。Vulkan は `VULKAN_SDK` の
DXC を使います。共有 HLSL は Shader Model 6.6 で DXIL / SPIR-V にそれぞれ
コンパイルされます。

## 実行ファイルの起動手順

D3D12版とVulkan版は別々の実行ファイルです。最初はD3D12版を起動し、画面が表示されることを
確認するのがおすすめです。Vulkan版を使う場合はVulkan SDKと対応ドライバーが必要です。

### 1. ビルド構成を確認する

以下は`Debug`、`x64`でビルドした場合の実行ファイルです。

| 版 | 実行ファイル |
| --- | --- |
| D3D12 | `Bin\SimpleMCPGraphicsSampleD3D12\x64\Debug\SimpleMCPGraphicsSampleD3D12.exe` |
| Vulkan | `Bin\SimpleMCPGraphicsSampleVulkan\x64\Debug\SimpleMCPGraphicsSampleVulkan.exe` |

`Release`でビルドした場合は、パス中の`Debug`を`Release`へ読み替えてください。
実行ファイルが見つからない場合は、先にVisual Studioで対象プロジェクトをビルドします。

### 2. GUIアプリとして起動する

普段の描画確認やImGuiの操作では、`--mcp`を付けずに起動します。次のいずれかの方法を
使用してください。

#### Visual Studioから起動する

1. `SimpleMCPGraphicsSample.sln`をVisual Studio 2022で開きます。
2. ツールバーで構成を`Debug`、プラットフォームを`x64`にします。
3. ソリューションエクスプローラーでD3D12またはVulkanプロジェクトを右クリックします。
4. `スタートアップ プロジェクトに設定`を選択します。
5. `F5`でデバッグ実行、または`Ctrl+F5`でデバッグなし実行します。

プロジェクトの起動引数は空にしてあります。F5でもGUIだけが起動するため、描画処理や
ImGuiへ通常どおりブレークポイントを設定できます。

#### PowerShellから起動する

リポジトリのルートディレクトリで、起動したい版のコマンドを1つだけ実行します。

```powershell
# D3D12版
.\Bin\SimpleMCPGraphicsSampleD3D12\x64\Debug\SimpleMCPGraphicsSampleD3D12.exe

# Vulkan版
.\Bin\SimpleMCPGraphicsSampleVulkan\x64\Debug\SimpleMCPGraphicsSampleVulkan.exe
```

エクスプローラーから上記の`.exe`をダブルクリックしても起動できます。起動すると1280 x 720の
ウィンドウが開き、Camera、Directional Light、Model TransformをImGuiから操作できます。

### 3. MCPサーバーとして起動する

MCPを使用するときは、実行ファイルを手動で起動せず、CodexなどのMCPクライアントへ登録します。
クライアントが実行ファイルへ`--mcp`を渡し、MCP通信に必要なstdin/stdout pipeを接続します。
登録方法は次の「Codex CLIへ登録」を参照してください。

> [!IMPORTANT]
> PowerShell、エクスプローラー、Visual Studioの起動引数から`--mcp`を直接指定しないでください。
> pipeが接続されていないため、`Cannot start MCP stdio transport`をstderrへ表示し、終了コード2で
> 終了します。これはクラッシュではなく、誤った起動方法を検出した結果です。

MCPクライアントから起動するとGUIも表示され、MCPパネルのStatusが`Connected`になります。
`Allow MCP writes`をOFFにすると、状態取得、Resources、ping、ログは利用したまま、外部からの
Camera、Light、Transform変更だけを拒否できます。

### Codex CLI へ登録

D3D12 または Vulkan のどちらか一方を登録してください。パスは実際の clone 先へ
置き換えます。

このリポジトリにはプロジェクト用の[`.codex/config.toml`](.codex/config.toml)も同梱しています。
D3D12版が既定で有効、Vulkan版が無効です。Vulkan版を使う場合は、設定ファイル内の
`enabled`を入れ替えてください。両方を同時に有効にする必要はありません。

プロジェクト設定を利用する場合は、先にRelease/x64をビルドしてからリポジトリのルートを
Codexで開きます。初回にプロジェクトの信頼確認が表示された場合は、内容を確認して信頼します。
以下の`codex mcp add`操作は不要です。

個人用の`~/.codex/config.toml`へ登録する場合、またはCLIから明示的に登録する場合は、
以下のコマンドを使用します。

```powershell
codex mcp add simple-graphics-d3d12 -- D:\Git\SimpleMCPGraphicsSample\Bin\SimpleMCPGraphicsSampleD3D12\x64\Release\SimpleMCPGraphicsSampleD3D12.exe --mcp
```

```powershell
codex mcp add simple-graphics-vulkan -- D:\Git\SimpleMCPGraphicsSample\Bin\SimpleMCPGraphicsSampleVulkan\x64\Release\SimpleMCPGraphicsSampleVulkan.exe --mcp
```

`~/.codex/config.toml`（または信頼済みプロジェクトの `.codex/config.toml`）へ直接
記述する例です。

```toml
[mcp_servers.simple_graphics_d3d12]
command = "D:\\Git\\SimpleMCPGraphicsSample\\Bin\\SimpleMCPGraphicsSampleD3D12\\x64\\Release\\SimpleMCPGraphicsSampleD3D12.exe"
args = ["--mcp"]
startup_timeout_sec = 10
tool_timeout_sec = 10
default_tools_approval_mode = "writes"
```

Vulkan を使う場合はセクション名と `command` を Vulkan の実行ファイルへ変更します。
同一シーンを二重起動しないよう、通常は一方だけを有効にしてください。

## ドキュメント

- [アーキテクチャ](Docs/architecture.md)
- [入門者向けプログラミングガイド](Docs/programming-guide.md)
- [MCP Tools / Resources とプロトコル](Docs/mcp.md)
- [MCP操作例（Camera / Light / Transform）](Docs/mcp-examples.md)
- [ビルド・テスト・手動受入](Docs/testing.md)
- [スクリーンショット](Docs/screenshots.md)
- [third-party とライセンス](Docs/third-party.md)

## ライセンス

ソースコードは [MIT License](LICENSE) です。SciFiHelmet のモデル、テクスチャ、
クレジットは [Assets/SciFiHelmet](Assets/SciFiHelmet) を参照してください。
モデルデータは CC0-1.0 です。
