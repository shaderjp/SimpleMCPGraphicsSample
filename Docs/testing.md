# ビルドとテスト

## 単体テスト

`SimpleMCPGraphicsSampleTests` は外部テストフレームワークを使わない console
アプリケーションです。GPU やウィンドウを作成せず、共通層を検証します。

```powershell
msbuild .\Tests\SimpleMCPGraphicsSampleTests.vcxproj /m /p:Configuration=Debug /p:Platform=x64
.\Bin\SimpleMCPGraphicsSampleTests\x64\Debug\SimpleMCPGraphicsSampleTests.exe
```

主な検証対象:

- Camera / Light / Transform の既定値、部分更新、Reset
- 実値変更時だけ revision が増えること
- MCP 書き込みゲートと UI 更新の独立性
- NaN / Inf、範囲外、無効な視線、ゼロ light direction、無効なTransform
- 複数スレッドからの部分更新
- initialize lifecycle、Tools / Resources、schema、error code
- notification に応答しないこと
- 分割入力、複数行、CRLF、1 MiB 上限

## stdio スモークテスト

先に対象 configuration の両 GUI プロジェクトをビルドします。

```powershell
msbuild .\SimpleMCPGraphicsSample.sln /restore /m /p:Configuration=Debug /p:Platform=x64
powershell -ExecutionPolicy Bypass -File .\Tests\SmokeTest.ps1 -Configuration Debug
```

スクリプトは両 EXE を `--mcp` で起動し、initialize、Tools / Resources 一覧、
設定と再取得、未知 method、stdin 切断時の終了を確認します。stdout の各行は
単独の JSON として parse され、診断文字列の混入も検出します。

## ビルドマトリクス

```powershell
msbuild .\SimpleMCPGraphicsSample.sln /restore /m /p:Configuration=Debug /p:Platform=x64
msbuild .\SimpleMCPGraphicsSample.sln /restore /m /p:Configuration=Release /p:Platform=x64
```

各 configuration で次を確認します。

- D3D12 と Vulkan の両実行ファイル
- `SimpleMCPGraphicsSample.vs.cso` / `.ps.cso`
- `SimpleMCPGraphicsSample.vs.spv` / `.ps.spv`
- D3D12 出力の `D3D12Core.dll` と SDK layers / configuration files
- 実行ファイル横の `Assets/SciFiHelmet`
- repository root 以外を current directory にした起動

## 手動受入

1. D3D12 を通常起動し、helmet と ImGui が表示される。
2. Camera / Light / Transform の各値と Reset が描画へ反映される。
3. Vulkan でも同じ見た目と操作を確認する。
4. MCP client から UI の変更を `get_scene_state` で取得する。
5. MCP の変更が次フレームの UI と描画へ反映される。
6. `Allow MCP writes` OFF では読み取りだけ成功し、set/reset が `isError` になる。
7. stdin 切断でウィンドウが閉じる。
8. ウィンドウを先に閉じた場合も client が EOF を受け、プロセスが残らない。
9. D3D12 debug layer と Vulkan validation layer に警告がない。

GPU validation はドライバーや環境に依存するため、対応 GPU 上で手動確認してください。
