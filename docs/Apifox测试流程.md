# Apifox 测试流程

本文档用于测试 `redculture_server` 的第一条完整业务链路：

```text
健康检查 -> 登录 -> 创建房间 -> 开始红色文化互动 -> 提交答案 -> 获取 TTS 音频 -> 查看房间列表
```

## 一、启动后端服务

在 Ubuntu 虚拟机里构建：

```bash
cmake --build /home/chen/Code/service/build/debug --target redculture_server -j
```

启动：

```bash
/home/chen/Code/service/build/debug/app/redculture_server/redculture_server --host 0.0.0.0 --port 8080
```

服务收到每次 HTTP 请求后都会输出一条访问日志，例如：

```text
[info] http_access method=POST target=/api/v1/auth/login status=200 elapsed_ms=3 remote=192.168.10.1:53210
```

说明：

- `method`：HTTP 方法。
- `target`：接口路径。
- `status`：HTTP 状态码。
- `elapsed_ms`：后端处理耗时。
- `remote`：客户端地址。

为了避免敏感信息泄露，日志不会输出请求体，也不会输出 `Authorization` token。

如果需要启用 PostgreSQL：

```bash
export RCS_POSTGRES_URI=postgresql://postgres:postgres@127.0.0.1:5432/redculture
```

本地开发不设置数据库也能跑通接口，只是业务响应里的 `storage_saved` 会是 `false`。

## 二、Apifox 环境变量

创建一个环境，例如：`RedCultureService-Local`。

建议变量：

| 变量名 | 示例值 | 说明 |
| --- | --- | --- |
| `baseUrl` | `http://192.168.10.130:8080` | Windows 访问 Ubuntu 虚拟机时使用虚拟机 IP |
| `playerId` | `player_001` | 测试玩家 ID |
| `account` | `unity_editor` | 测试账号 |
| `password` | `123456` | 测试密码 |
| `token` | 空 | 登录接口后置脚本写入 |
| `roomId` | 空 | 创建房间后置脚本写入 |
| `interactionId` | 空 | 开始互动后置脚本写入 |
| `flowId` | 空 | 开始互动后置脚本写入 |
| `audioId` | 空 | 提交答案后置脚本写入 |

如果 Apifox 和服务都在 Ubuntu 里，可以把 `baseUrl` 设置为：

```text
http://127.0.0.1:8080
```

## 三、统一响应格式

app 层业务接口返回 Java/SpringBoot 风格：

```json
{
  "code": 200,
  "msg": "success",
  "data": {}
}
```

说明：

- `code = 200` 表示业务成功。
- `code = 400` 表示请求参数错误。
- `code = 401` 表示未登录或 token 无效。
- `data` 是接口业务数据。
- TTS 音频下载接口返回二进制音频，不使用该 JSON 格式。

## 四、接口 1：健康检查

请求：

```text
GET {{baseUrl}}/api/v1/ops/health
```

预期：

```json
{
  "code": 200,
  "msg": "health checked",
  "data": {
    "healthy": true
  }
}
```

如果服务还未 ready，可以继续测 `ready`：

```text
GET {{baseUrl}}/api/v1/ops/ready
```

## 五、接口 2：注册

请求：

```text
POST {{baseUrl}}/api/v1/auth/register
```

Header：

```text
Content-Type: application/json
```

Body：

```json
{
  "player_id": "{{playerId}}",
  "account": "{{account}}",
  "password": "{{password}}",
  "display_name": "Unity 测试玩家"
}
```

说明：注册接口需要启用 PostgreSQL，否则会返回 `503 storage is required for register`。密码会以 bcrypt hash 形式写入 `rcs_users.password_hash`，不会明文入库。

后置脚本：

```javascript
const body = pm.response.json();
pm.environment.set("token", body.data.token);
pm.environment.set("sessionId", body.data.session.session_id);
```
## 六、接口 3：登录

请求：

```text
POST {{baseUrl}}/api/v1/auth/login
```

Header：

```text
Content-Type: application/json
```

Body：

```json
{
  "player_id": "{{playerId}}",
  "account": "{{account}}",
  "password": "{{password}}"
}
```

预期响应：

```json
{
  "code": 200,
  "msg": "login success",
  "data": {
    "token": "jwt-token",
    "session": {
      "session_id": 1,
      "player_id": "player_001",
      "account": "unity_editor",
      "connection_id": 0
    }
  }
}
```

后置脚本：

```javascript
const body = pm.response.json();
pm.environment.set("token", body.data.token);
pm.environment.set("sessionId", body.data.session.session_id);
```

## 七、接口 4：创建房间

请求：

```text
POST {{baseUrl}}/api/v1/rooms/create
```

Header：

```text
Content-Type: application/json
Authorization: Bearer {{token}}
```

Body：

```json
{
  "mode": "story",
  "max_players": 4,
  "auto_start_when_full": true
}
```

预期响应：

```json
{
  "code": 200,
  "msg": "create room success",
  "data": {
    "room": {
      "room_id": 1,
      "mode": "story",
      "state": "waiting",
      "members": [
        {
          "player_id": "player_001",
          "connection_id": 0,
          "ready": false
        }
      ]
    }
  }
}
```

后置脚本：

```javascript
const body = pm.response.json();
pm.environment.set("roomId", body.data.room.room_id);
```

## 八、接口 5：开始红色文化互动

请求：

```text
POST {{baseUrl}}/api/v1/interactions/start
```

Header：

```text
Content-Type: application/json
Authorization: Bearer {{token}}
```

Body：

```json
{
  "room_id": "{{roomId}}",
  "scene_id": "museum_hall",
  "trigger_id": "trigger_long_march",
  "topic": "长征精神",
  "metadata": {
    "client_version": "unity-editor",
    "test_case": "apifox"
  }
}
```

预期响应：

```json
{
  "code": 200,
  "msg": "start interaction success",
  "data": {
    "interaction_id": 1,
    "flow_id": 1,
    "question": "AI 生成的问题文本",
    "storage_saved": false
  }
}
```

后置脚本：

```javascript
const body = pm.response.json();
pm.environment.set("interactionId", body.data.interaction_id);
pm.environment.set("flowId", body.data.flow_id);
```

## 八、接口 5：提交答案

请求：

```text
POST {{baseUrl}}/api/v1/interactions/answer
```

Header：

```text
Content-Type: application/json
Authorization: Bearer {{token}}
```

Body：

```json
{
  "interaction_id": "{{interactionId}}",
  "answer": "我看到了坚定理想信念、团结奋斗和不怕困难的长征精神。"
}
```

预期响应：

```json
{
  "code": 200,
  "msg": "answer interaction success",
  "data": {
    "interaction_id": 1,
    "explanation": "AI 生成的讲解文本",
    "audio_id": "tts:xxx:task:1",
    "audio_url": "/api/v1/tts/audio?audio_id=tts:xxx:task:1",
    "storage_saved": false
  }
}
```

后置脚本：

```javascript
const body = pm.response.json();
pm.environment.set("audioId", body.data.audio_id);
```

## 九、接口 6：获取 TTS 音频

请求：

```text
GET {{baseUrl}}/api/v1/tts/audio?audio_id={{audioId}}
```

Header：

```text
Authorization: Bearer {{token}}
```

预期：

- HTTP 状态码 `200`。
- `Content-Type` 当前 mock 为 `audio/mpeg`。
- 响应体是音频字节。

注意：

当前本地 mock TTS 返回的是假 MP3 字节，用于验证后端链路。接入真实 TTS 后，该接口会返回真实音频。

## 十、接口 7：查看房间列表

请求：

```text
GET {{baseUrl}}/api/v1/rooms
```

预期响应：

```json
{
  "code": 200,
  "msg": "list rooms success",
  "data": {
    "rooms": []
  }
}
```

## 十一、错误用例

### 1. 登录缺少 player_id

```text
POST {{baseUrl}}/api/v1/auth/login
```

Body：

```json
{}
```

预期：

```json
{
  "code": 400,
  "msg": "player_id is required",
  "data": null
}
```

### 2. 创建房间不带 token

```text
POST {{baseUrl}}/api/v1/rooms/create
```

Body：

```json
{
  "mode": "story"
}
```

预期：

```json
{
  "code": 401,
  "msg": "missing bearer token or player_id in local dev mode",
  "data": null
}
```

### 3. 提交不存在的 interaction_id

```text
POST {{baseUrl}}/api/v1/interactions/answer
```

Body：

```json
{
  "interaction_id": 999999,
  "answer": "测试答案"
}
```

预期：

```json
{
  "code": 400,
  "msg": "interaction was not found",
  "data": null
}
```

## 十二、推荐测试顺序

```text
1. GET health
2. POST login，并保存 token
3. POST rooms/create，并保存 roomId
4. POST interactions/start，并保存 interactionId 和 flowId
5. POST interactions/answer，并保存 audioId
6. GET tts/audio，验证音频下载
7. GET rooms，确认房间仍存在
```

这条链路跑通后，Unity 端就可以按同样顺序接入。
