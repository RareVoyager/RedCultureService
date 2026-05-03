# 后端接口测试文档

本文档用于测试 `redculture_server` 当前已经注册的 HTTP 接口。接口入口统一为：

```text
{{baseUrl}} = http://127.0.0.1:8080
```

如果从 Windows 访问 Ubuntu 虚拟机，请把 `baseUrl` 改成虚拟机 IP，例如：

```text
http://192.168.10.130:8080
```

## 一、启动前准备

### 1. 构建服务

在 Ubuntu 虚拟机中执行：

```bash
cmake --build /home/chen/Code/service/build/debug --target redculture_server -j 14
```

### 2. 启动 PostgreSQL 连接

注册接口和账号密码登录依赖 PostgreSQL。启动服务前建议设置：

```bash
export RCS_POSTGRES_URI=postgresql://postgres:postgres@127.0.0.1:5432/redculture
export RCS_JWT_SECRET=local-dev-secret
export RCS_HTTP_HOST=0.0.0.0
export RCS_HTTP_PORT=8080
```

如果没有设置 `RCS_POSTGRES_URI`：

- `/api/v1/auth/register` 会返回 `503 storage is required for register`。
- 账号密码登录会返回 `503 storage is required for password login`。
- 本地开发模式下，部分接口仍可以通过 body 中的 `player_id` 测试。

### 3. 启动服务

```bash
/home/chen/Code/service/build/debug/app/redculture_server/redculture_server --host 0.0.0.0 --port 8080
```

## 二、统一响应格式

除 TTS 音频下载和 Prometheus metrics 外，业务接口统一返回：

```json
{
  "code": 200,
  "msg": "success",
  "data": {}
}
```

常见状态：

| HTTP 状态 | code | 说明 |
| --- | ---: | --- |
| 200 | 200 | 请求成功 |
| 400 | 400 | 参数错误 |
| 401 | 401 | token 缺失或无效 |
| 404 | 404 | 资源不存在 |
| 409 | 409 | 账号或玩家 ID 已存在 |
| 503 | 503 | 依赖服务不可用，例如数据库未连接 |

## 三、Apifox 导入 JSON

已生成可导入的测试集合：

```text
docs/RedCultureService接口测试集合.postman_collection.json
```

导入方式：

1. 打开 Apifox。
2. 选择“导入”。
3. 选择 Postman Collection / JSON 文件。
4. 选择 `docs/RedCultureService接口测试集合.postman_collection.json`。
5. 修改环境变量 `baseUrl` 为你的后端地址。

建议变量：

| 变量 | 示例 | 说明 |
| --- | --- | --- |
| `baseUrl` | `http://192.168.10.130:8080` | 后端地址 |
| `playerId` | `player_001` | 主测试玩家 ID |
| `account` | `unity_editor` | 主测试账号 |
| `password` | `123456` | 主测试密码 |
| `token` | 自动写入 | 注册/登录成功后保存 |
| `roomId` | 自动写入 | 创建房间成功后保存 |
| `interactionId` | 自动写入 | 开始互动成功后保存 |
| `flowId` | 自动写入 | 开始互动成功后保存 |
| `audioId` | 自动写入 | 提交答案成功后保存 |

## 四、推荐测试顺序

```text
1. GET  /api/v1/ops/health
2. GET  /api/v1/ops/ready
3. POST /api/v1/auth/register
4. POST /api/v1/auth/login
5. POST /api/v1/rooms/create
6. GET  /api/v1/rooms
7. POST /api/v1/interactions/start
8. POST /api/v1/interactions/answer
9. GET  /api/v1/tts/audio?audio_id={{audioId}}
10. GET /api/v1/ops/metrics
```

`POST /api/v1/ops/shutdown` 会让服务进入停服流程，只建议最后手动执行，不要放进自动化批量测试。

## 五、当前所有接口

### 1. 健康检查

```http
GET /api/v1/ops/health
```

响应示例：

```json
{
  "code": 200,
  "msg": "health checked",
  "data": {
    "healthy": true,
    "ready": true,
    "status": "running",
    "components": []
  }
}
```

### 2. 就绪检查

```http
GET /api/v1/ops/ready
```

响应示例：

```json
{
  "code": 200,
  "msg": "ready checked",
  "data": {
    "ready": true,
    "status": "running"
  }
}
```

### 3. 版本信息

```http
GET /api/v1/ops/version
```

响应示例：

```json
{
  "code": 200,
  "msg": "version fetched",
  "data": {
    "serviceName": "red_culture_service",
    "version": "0.1.0",
    "environment": "local"
  }
}
```

### 4. Prometheus 指标

```http
GET /api/v1/ops/metrics
```

该接口返回 `text/plain`，不是统一 JSON 格式。

### 5. 停服请求

```http
POST /api/v1/ops/shutdown
```

Body：

```text
manual shutdown from apifox
```

注意：该接口会触发服务停服流程，只能最后手动执行。

### 6. 注册

```http
POST /api/v1/auth/register
Content-Type: application/json
```

Body：

```json
{
  "player_id": "{{playerId}}",
  "account": "{{account}}",
  "password": "{{password}}",
  "display_name": "Unity测试玩家",
  "avatar_url": "",
  "connection_id": 0,
  "metadata": {
    "client": "apifox",
    "platform": "unity-editor"
  }
}
```

成功后会返回 `token`，并把 bcrypt 后的密码写入 PostgreSQL。

### 7. 登录

```http
POST /api/v1/auth/login
Content-Type: application/json
```

账号密码登录 Body：

```json
{
  "account": "{{account}}",
  "password": "{{password}}",
  "connection_id": 0
}
```

Token 登录 Body：

```json
{
  "token": "{{token}}",
  "connection_id": 0
}
```

### 8. 创建房间

```http
POST /api/v1/rooms/create
Authorization: Bearer {{token}}
Content-Type: application/json
```

Body：

```json
{
  "mode": "story",
  "max_players": 4,
  "auto_start_when_full": false,
  "connection_id": 0
}
```

### 9. 加入房间

```http
POST /api/v1/rooms/join
Content-Type: application/json
```

本地开发模式 Body：

```json
{
  "player_id": "{{secondPlayerId}}",
  "room_id": "{{roomId}}",
  "connection_id": 0
}
```

如果使用生产鉴权，需要先给第二个玩家注册/登录，再使用第二个玩家的 token。

### 10. 房间列表

```http
GET /api/v1/rooms
```

响应示例：

```json
{
  "code": 200,
  "msg": "list rooms success",
  "data": {
    "rooms": []
  }
}
```

### 11. 开始答题互动

```http
POST /api/v1/interactions/start
Authorization: Bearer {{token}}
Content-Type: application/json
```

Body：

```json
{
  "room_id": "{{roomId}}",
  "scene_id": "museum_hall",
  "trigger_id": "trigger_long_march",
  "topic": "长征精神",
  "question_prompt": "请围绕 {topic} 生成一个适合 Unity 互动场景的问题。",
  "metadata": {
    "client_version": "unity-editor",
    "test_case": "apifox"
  }
}
```

### 12. 提交答案

```http
POST /api/v1/interactions/answer
Authorization: Bearer {{token}}
Content-Type: application/json
```

Body：

```json
{
  "interaction_id": "{{interactionId}}",
  "answer": "我看到了坚定理想信念、团结奋斗和不怕困难的长征精神。"
}
```

成功后如果生成了语音，会返回：

```json
{
  "audio_id": "tts:xxx:task:1",
  "audio_url": "/api/v1/tts/audio?audio_id=tts:xxx:task:1",
  "audio_mime_type": "audio/mpeg"
}
```

### 13. 获取 TTS 音频 GET

```http
GET /api/v1/tts/audio?audio_id={{audioId}}
```

该接口返回音频字节，当前本地 mock 返回的是伪 MP3 字节，用于验证链路。

### 14. 获取 TTS 音频 POST

```http
POST /api/v1/tts/audio
Content-Type: application/json
```

Body：

```json
{
  "audio_id": "{{audioId}}"
}
```

## 六、日志验证

每次请求都会产生访问日志，格式类似：

```text
[info] http_access method=POST target=/api/v1/auth/login status=200 elapsed_ms=3 remote=192.168.10.1:53210
```

接口内部错误会额外输出带源码位置的日志：

```text
[warning] api_error location=/home/chen/Code/service/app/redculture_server/src/api/controllers/auth_controller.cpp:181 function=registerUser http_status=400 code=400 msg=account is required
```
## 七、当前问题排查

### 1. `/api/v1/ops/metrics` 为什么不是 JSON？

这是正常现象。`/api/v1/ops/metrics` 按 Prometheus 规范返回 `text/plain`，示例：

```text
# TYPE rcs_auth_sessions gauge
rcs_auth_sessions 0
# TYPE rcs_rooms gauge
rcs_rooms 0
# TYPE rcs_ai_queued_tasks gauge
rcs_ai_queued_tasks 0
# TYPE rcs_tts_queued_tasks gauge
rcs_tts_queued_tasks 0
```

在 Apifox 中不要把该接口的响应断言为 JSON，直接按“文本响应”查看即可。需要 JSON 的运维接口请使用：

```text
GET /api/v1/ops/health
GET /api/v1/ops/ready
GET /api/v1/ops/version
```

### 2. 注册接口返回 `503 storage is required for register`

这表示后端当前没有连接 PostgreSQL。注册接口必须写入 `rcs_users`，所以数据库未连接时会返回 503。

启动服务前请确认已经设置：

```bash
export RCS_POSTGRES_URI=postgresql://postgres:postgres@127.0.0.1:5432/redculture
```

然后重新启动 `redculture_server`。启动日志中应该能看到类似：

```text
storage_connected ...
```

如果看到 `storage_connect_failed` 或 `storage_disabled`，注册接口仍然会返回 503。

### 3. 你的测试密码 `123` 长度不足

当前注册接口要求密码长度至少 6 位。建议先改成：

```json
{
  "password": "123456"
}
```

现在注册接口会先做本地参数校验，所以密码太短会优先返回：

```json
{
  "code": 400,
  "msg": "password length must be at least 6",
  "data": null
}
```

密码合格后，如果 PostgreSQL 没连上，才会返回：

```json
{
  "code": 503,
  "msg": "storage is required for register",
  "data": null
}
```
### 4. 如何确认后端进程真的拿到了数据库配置？

如果你设置了环境变量但仍然返回 503，最常见原因是：环境变量设置在一个终端里，但后端进程不是从这个终端启动的。比如从 CLion Run Configuration 启动时，SSH 终端里的 `export RCS_POSTGRES_URI=...` 不会自动传给 CLion 启动的进程。

推荐直接使用命令行参数启动，最不容易出错：

```bash
/home/chen/Code/service/build/debug/app/redculture_server/redculture_server \
  --host 0.0.0.0 \
  --port 8080 \
  --postgres-uri 'postgresql://postgres:postgres@127.0.0.1:5432/redculture'
```

或者在同一个 Ubuntu 终端里执行：

```bash
export RCS_POSTGRES_URI='postgresql://postgres:postgres@127.0.0.1:5432/redculture'
echo "$RCS_POSTGRES_URI"
/home/chen/Code/service/build/debug/app/redculture_server/redculture_server --host 0.0.0.0 --port 8080
```

启动后先访问：

```text
GET /api/v1/ops/health
```

查看 `data.components` 中 `component = storage` 的结果：

- `message = disabled: ...`：说明后端进程没有拿到 `RCS_POSTGRES_URI`，或没有传 `--postgres-uri`。
- `message = disconnected: ...`：说明后端拿到了配置，但 PostgreSQL 连接失败，后面的错误文本就是 libpqxx 给出的具体原因。
- `message = connected`：说明数据库已连接，注册接口可以继续测试。

### 5. 如何单独验证 PostgreSQL 可连接？

在 Ubuntu 虚拟机里执行：

```bash
pg_isready -h 127.0.0.1 -p 5432 -d redculture -U postgres
psql 'postgresql://postgres:postgres@127.0.0.1:5432/redculture' -c 'select 1;'
```

如果数据库不存在，可以先创建：

```bash
sudo -u postgres createdb redculture
```

如果密码不对，请先确认 PostgreSQL 用户密码，或按你的实际账号修改连接串。