# MCP インターフェース

## Transport と対応範囲

- MCP protocol version: `2025-11-25`
- JSON-RPC version: `2.0`
- transport: stdio
- encoding: UTF-8
- framing: 1行につき1個の JSON メッセージ
- 受信メッセージ上限: 1 MiB
- batch request: 非対応（invalid request）

`Content-Length` ヘッダーは使用しません。この文書はstdio版を対象とします。HTTP版は
[Streamable HTTP MCP](http-mcp.md)、両transportの使い分けは
[stdio版とStreamable HTTP版の違い](mcp-transports.md)を参照してください。認証、Prompts、Tasks、Sampling、
Resource subscription、list-changed notification は対象外です。

対応メソッドは次のとおりです。

- `initialize`
- `notifications/initialized`
- `ping`
- `tools/list`
- `tools/call`
- `resources/list`
- `resources/read`
- `resources/templates/list`（空配列）
- `notifications/cancelled`（即時処理のため受理して無視）

server capabilities は `tools` と `resources` だけです。notification には応答しません。

## Tools

すべての Tool は closed-world の `inputSchema`、完全な Scene State の
`outputSchema`、read-only / idempotent / open-world 情報を annotations で公開します。
成功時は `structuredContent` と、同じ JSON の `TextContent` を返します。

### get_scene_state

引数なしの読み取り専用 Tool です。

```json
{}
```

### set_camera

1項目以上を指定する部分更新です。Vector は `x`、`y`、`z` をすべて指定します。

```json
{
  "position": { "x": 0.0, "y": 0.4, "z": 5.2 },
  "target": { "x": 0.0, "y": 0.12, "z": 0.0 },
  "fov_degrees": 45.0
}
```

### set_light

1項目以上を指定する部分更新です。

```json
{
  "direction": { "x": -0.35, "y": -0.8, "z": 0.45 },
  "color": { "r": 1.0, "g": 0.96, "b": 0.88 },
  "intensity": 5.0
}
```

direction は非ゼロである必要があり、保存時に正規化されます。

### set_transform

モデルの平行移動、XYZ回転角、スケールを1項目以上指定する部分更新です。回転角はdegreesです。

```json
{
  "translation": { "x": 0.4, "y": -0.2, "z": 0.0 },
  "rotation_degrees": { "x": 10.0, "y": 35.0, "z": 0.0 },
  "scale": { "x": 1.1, "y": 1.1, "z": 1.1 }
}
```

モデルは自動回転しません。Transformが変更されるまで同じ姿勢を維持します。

### reset_scene

`target` は `camera`、`light`、`transform`、`all` のいずれかです。省略時は `all` です。

```json
{ "target": "all" }
```

## Scene State

```json
{
  "revision": 0,
  "camera": {
    "position": { "x": 0.0, "y": 0.4, "z": 5.2 },
    "target": { "x": 0.0, "y": 0.12, "z": 0.0 },
    "fov_degrees": 45.0
  },
  "light": {
    "direction": { "x": -0.35, "y": -0.8, "z": 0.45 },
    "color": { "r": 1.0, "g": 0.96, "b": 0.88 },
    "intensity": 5.0
  },
  "transform": {
    "translation": { "x": 0.0, "y": 0.0, "z": 0.0 },
    "rotation_degrees": { "x": 0.0, "y": 0.0, "z": 0.0 },
    "scale": { "x": 1.2, "y": 1.2, "z": 1.2 }
  }
}
```

`set_light` で direction を更新した場合、その値は状態へ保存する前に正規化されます。
既定値と Reset はプランで定めた値をそのまま復元します。

## Resources

| URI | MIME type | 内容 |
| --- | --- | --- |
| `graphics://scene/state` | `application/json` | Tool と同じ Scene State snapshot |
| `graphics://app/info` | `application/json` | app/version、backend、GPU、model、viewport、renderer、FPS、frame count、MCP 状態 |

Resource は購読ではなく、必要な時点で再読込してください。

## 入力検証

- すべての数値は finite
- Camera position / target の各成分: `[-10, 10]`
- FOV: `[15, 90]` degrees
- position と target の一致を拒否
- view direction と固定 up vector がほぼ平行になる状態を拒否
- Light direction: finite な非ゼロベクトル（保存時に正規化）
- Color: `[0, 1]`
- Intensity: `[0, 10]`
- Transform translation: `[-5, 5]`
- Transform rotation: `[-180, 180]` degrees
- Transform scale: `[0.1, 3]`

形式が正しい Tool の値域違反や `Allow MCP writes` OFF による拒否は、JSON-RPC error
ではなく Tool result の `isError: true` で返します。拒否時は状態を変更しません。

## JSON-RPC エラー

| code | 用途 |
| ---: | --- |
| `-32700` | Parse error |
| `-32600` | Invalid Request |
| `-32601` | Method not found |
| `-32602` | Invalid params / unknown tool |
| `-32603` | Internal error |
| `-32002` | Resource not found |

## 汎用クライアントでの確認順

1. 実行ファイルを `--mcp` 付き stdio server として起動
2. `initialize` を送信し、`protocolVersion` と capabilities を確認
3. `notifications/initialized` を送信
4. `tools/list` と `resources/list` を取得
5. `get_scene_state` で revision を確認
6. `set_camera`、`set_light`、`set_transform` のいずれかを部分更新で呼ぶ
7. `get_scene_state` と `graphics://scene/state` を再取得して同値を確認
8. UI の `Allow MCP writes` を OFF にし、読み取り成功・書き込み拒否を確認
9. stdin を閉じ、アプリケーションが終了することを確認

仕様の一次資料は [MCP 2025-11-25 specification](https://modelcontextprotocol.io/specification/2025-11-25) を参照してください。
具体的な操作requestは [MCP実行例](mcp-examples.md) を参照してください。
