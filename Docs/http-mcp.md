# Streamable HTTP MCP

## 対応範囲

HTTP版はMCP `2025-11-25`のStreamable HTTP transportを、同期的なTools / Resourcesに
必要な範囲で実装しています。D3D12とVulkanは別プロセスで、それぞれ独立したscene stateを
持ちます。stdio版との起動方法や終了動作の違いは
[stdio版とStreamable HTTP版の違い](mcp-transports.md)を参照してください。

| 版 | 既定endpoint |
| --- | --- |
| D3D12 | `http://127.0.0.1:5000/mcp` |
| Vulkan | `http://127.0.0.1:5001/mcp` |

- `POST /mcp`: JSON-RPC messageの送信
- `GET /mcp`: `405 Method Not Allowed`。初版はSSEを提供しない
- `DELETE /mcp`: 指定sessionの終了
- encoding: UTF-8
- request body上限: 1 MiB
- batch request: 非対応
- session idle timeout: 30分
- 最大session数: 32

## 起動

HTTP版は起動と同時に`127.0.0.1`で待受を開始します。`--mcp`は使用しません。

```powershell
# D3D12（既定port 5000）
.\Bin\SimpleMCPGraphicsSampleHttpD3D12\x64\Release\SimpleMCPGraphicsSampleHttpD3D12.exe

# Vulkan（既定port 5001）
.\Bin\SimpleMCPGraphicsSampleHttpVulkan\x64\Release\SimpleMCPGraphicsSampleHttpVulkan.exe
```

共通の起動option:

```text
--port <1..65535>
--allow-origin <absolute-origin>   # 複数回指定可能
--help
```

D3D12版では従来の`-warp` / `/warp`も併用できます。bind addressは変更できず、LANや
インターネットへ直接公開する用途には対応していません。

## session lifecycle

1. クライアントは`MCP-Session-Id`を付けずにinitializeをPOSTする。
2. サーバーはinitialize成功応答の`MCP-Session-Id` headerで64文字のsession IDを返す。
3. 以後のPOST / DELETEへ`MCP-Session-Id`と`MCP-Protocol-Version: 2025-11-25`を付ける。
4. `notifications/initialized`をPOSTしてからTools / Resourcesを使用する。
5. 終了時は同じsession headerを付けて`DELETE /mcp`を送信する。

各POSTには次のheaderも必要です。

```http
Content-Type: application/json
Accept: application/json, text/event-stream
```

JSON-RPC requestの応答は`200 application/json`です。notificationとclient responseを
受理した場合はbodyなしの`202 Accepted`です。期限切れ、DELETE済み、または不明な
session IDは`404 Not Found`になります。

## PowerShellでのinitialize例

```powershell
$endpoint = 'http://127.0.0.1:5000/mcp'
$accept = @{ Accept = 'application/json, text/event-stream' }
$body = @{
    jsonrpc = '2.0'
    id = 1
    method = 'initialize'
    params = @{
        protocolVersion = '2025-11-25'
        capabilities = @{}
        clientInfo = @{ name = 'manual-test'; version = '1.0' }
    }
} | ConvertTo-Json -Depth 10 -Compress

$response = Invoke-WebRequest $endpoint -Method Post -ContentType 'application/json' -Headers $accept -Body $body
$session = [string] ($response.Headers['MCP-Session-Id'] | Select-Object -First 1)
```

## localhost security

- listenerはIPv4 loopbackの`127.0.0.1`だけへbindします。
- `Origin` headerが無いネイティブクライアントは許可します。
- `Origin`がある場合、現在のportのlocalhost originまたは`--allow-origin`との完全一致だけを許可します。
- 不正OriginはJSON-RPC処理前に`403 Forbidden`で拒否します。
- TLSや認証はありません。LANやインターネットへ公開しないでください。

## 主なHTTP status

| status | 用途 |
| ---: | --- |
| `200` | JSON-RPC request成功またはJSON-RPC error response |
| `202` | notification / client responseを受理 |
| `204` | session DELETE成功 |
| `400` | transport header、message、protocol versionなどが不正 |
| `403` | Origin拒否 |
| `404` | sessionまたはpathが存在しない |
| `405` | GETなど未対応method |
| `406` | Acceptが不正 |
| `413` | 1 MiB超過 |
| `415` | Content-Typeが不正 |
| `503` | 最大session数に到達 |

Tools / Resources、入力値域、JSON-RPC error codeは[共通MCPインターフェース](mcp.md)と同じです。
