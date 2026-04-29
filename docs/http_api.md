# RedCultureService HTTP API

本文件记录当前 C++ 后端第一批可供 Unity 调用的 HTTP JSON 接口。

## 启动服务

```bash
cmake --build /home/chen/Code/service/build/debug --target redculture_server -j
/home/chen/Code/service/build/debug/app/redculture_server/redculture_server --host 0.0.0.0 --port 8080
```

也可以通过环境变量覆盖：

```bash
export RCS_HTTP_HOST=0.0.0.0
export RCS_HTTP_PORT=8080
export RCS_JWT_SECRET=local-dev-secret
export RCS_POSTGRES_URI=postgresql://postgres:postgres@127.0.0.1:5432/redculture
```

`RCS_POSTGRES_URI` 设置后服务会尝试连接 PostgreSQL，并把互动答题、进度和事件日志写入数据库；不设置时仍可本地联调，接口返回里的 `storage_saved` 会是 `false`。

## 健康检查

```bash
curl http://127.0.0.1:8080/api/v1/ops/health
curl http://127.0.0.1:8080/api/v1/ops/ready
```

## 登录

本地开发模式下可以直接传 `player_id`，后端会签发 JWT 并创建会话。上线前应关闭开发登录，改成平台票据或账号密码校验。

```bash
curl -X POST http://127.0.0.1:8080/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"player_id":"player_001","account":"demo"}'
```

返回示例：

```json
{
  "ok": true,
  "token": "jwt-token",
  "session": {
    "session_id": 1,
    "player_id": "player_001",
    "account": "demo",
    "connection_id": 0
  }
}
```

## 创建房间

```bash
curl -X POST http://127.0.0.1:8080/api/v1/rooms/create \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <jwt-token>" \
  -d '{"mode":"story","max_players":4,"auto_start_when_full":true}'
```

## 加入房间

```bash
curl -X POST http://127.0.0.1:8080/api/v1/rooms/join \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <jwt-token>" \
  -d '{"room_id":1}'
```

## 房间列表

```bash
curl http://127.0.0.1:8080/api/v1/rooms
```

## AI 触发

当前使用 C++ 本地 mock AI 客户端，方便先打通 Unity 联调链路；后续可以把 `IAiClient` 替换成真实大模型 HTTP/gRPC 客户端。

```bash
curl -X POST http://127.0.0.1:8080/api/v1/ai/trigger \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <jwt-token>" \
  -d '{"room_id":1,"scene_id":"museum_hall","topic":"长征精神"}'
```

## AI 答案提交

`/api/v1/ai/trigger` 返回的 `flow.flow_id` 可用于提交答案，后端会推进讲解任务。

```bash
curl -X POST http://127.0.0.1:8080/api/v1/ai/answer \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <jwt-token>" \
  -d '{"flow_id":1,"answer":"我看到了长征途中团结奋斗和坚定理想信念。"}'
```

## 游戏业务：开始红色文化互动

正式 Unity 游戏流程建议优先使用业务接口，而不是直接调用底层 AI 接口。

```bash
curl -X POST http://127.0.0.1:8080/api/v1/interactions/start \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <jwt-token>" \
  -d '{"room_id":1,"scene_id":"museum_hall","trigger_id":"trigger_long_march","topic":"长征精神"}'
```

返回中重点字段：

```json
{
  "ok": true,
  "interaction_id": 1,
  "flow_id": 1,
  "question": "AI 生成的问题文本",
  "storage_saved": false
}
```

## 游戏业务：提交答案

```bash
curl -X POST http://127.0.0.1:8080/api/v1/interactions/answer \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <jwt-token>" \
  -d '{"interaction_id":1,"answer":"我看到了坚定理想信念和团结奋斗。"}'
```

返回中重点字段：

```json
{
  "ok": true,
  "interaction_id": 1,
  "explanation": "AI 生成的讲解文本",
  "audio_id": "tts:xxx:task:1",
  "audio_url": "/api/v1/tts/audio?audio_id=tts:xxx:task:1",
  "storage_saved": false
}
```

## 获取 TTS 音频

```bash
curl "http://127.0.0.1:8080/api/v1/tts/audio?audio_id=<audio-id>" --output explanation.mp3
```

当前本地 mock TTS 返回的是假 MP3 字节，用来验证接口链路；接入真实 TTS 后这里会返回真实音频。

## Unity 调用示例

```csharp
using System.Text;
using System.Threading.Tasks;
using UnityEngine;
using UnityEngine.Networking;

public static class RedCultureApi
{
    public static async Task<string> PostJsonAsync(string url, string json, string token = null)
    {
        using var request = new UnityWebRequest(url, "POST");
        request.uploadHandler = new UploadHandlerRaw(Encoding.UTF8.GetBytes(json));
        request.downloadHandler = new DownloadHandlerBuffer();
        request.SetRequestHeader("Content-Type", "application/json");

        if (!string.IsNullOrEmpty(token))
        {
            request.SetRequestHeader("Authorization", "Bearer " + token);
        }

        var operation = request.SendWebRequest();
        while (!operation.isDone)
        {
            await Task.Yield();
        }

        if (request.result != UnityWebRequest.Result.Success)
        {
            Debug.LogError(request.error + " " + request.downloadHandler.text);
            return null;
        }

        return request.downloadHandler.text;
    }
}
```
