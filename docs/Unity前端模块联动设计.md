# Unity 前端模块与后端联动设计

本文档用于说明 Unity 客户端如何与 C++ 后端协作。当前目标不是一次性完成所有玩法细节，而是先搭建稳定的前端业务骨架，让注册登录、房间上下文、答题互动、AI 讲解和 TTS 音频可以端到端跑通。

## 1. 从哪里开始看

Unity 运行时入口：`RedCulture/Assets/Scripts/RedCulture/Runtime/Bootstrap/RedCultureAppBootstrap.cs`

这个脚本会在场景加载后自动创建 `RedCultureApp`，并挂载以下运行时模块：

- `RedCultureAuthService`：注册、登录、健康检查。
- `RedCultureRoomService`：创建房间、加入房间、查询房间列表。
- `RedCultureInteractionService`：开始答题互动、提交答案、获取 AI 讲解结果。
- `RedCultureTtsService`：下载并播放后端返回的 TTS 音频。
- `RedCultureAuthPanelView`：登录/注册 UI。
- `RedCultureBackendFeaturePanelView`：登录后的后端功能中心 UI。

## 2. 当前模块划分

### 2.1 Network 网络层

路径：`RedCulture/Assets/Scripts/RedCulture/Runtime/Network`

职责：

- 统一封装 `UnityWebRequest`。
- 统一解析后端 `{ code, msg, data }` 结构。
- 自动追加 `Authorization: Bearer <token>`。
- 支持 JSON POST、JSON GET、普通文本 GET。
- 将后端相对路径转换成完整 URL，例如 `/api/v1/tts/audio?...`。

### 2.2 Auth 认证模块

路径：`RedCulture/Assets/Scripts/RedCulture/Runtime/Auth`

职责：

- 调用 `/api/v1/auth/register`。
- 调用 `/api/v1/auth/login`。
- 保存后端返回的 token、player_id、account、display_name。
- 后续房间、答题、TTS 请求都复用这里的登录态。

### 2.3 Room 房间模块

路径：`RedCulture/Assets/Scripts/RedCulture/Runtime/Room`

后端接口：

- `POST /api/v1/rooms/create`
- `POST /api/v1/rooms/join`
- `GET /api/v1/rooms`

当前用途：

- 即使 Unity 当前是单人体验，也可以创建一个房间作为互动上下文。
- 后端可以记录玩家在哪个房间中触发了答题、讲解等行为。
- 后续如果做多人协作或组队答题，可以继续扩展这个模块。

### 2.4 Interaction 答题互动模块

路径：`RedCulture/Assets/Scripts/RedCulture/Runtime/Interaction`

后端接口：

- `POST /api/v1/interactions/start`
- `POST /api/v1/interactions/answer`

当前用途：

- Unity 传入场景、触发点、主题，请求后端生成问题。
- 玩家提交答案后，后端返回 AI 讲解文本、音频 ID、音频 URL。
- 后续可把 `trigger_id` 绑定到展品、NPC、路线节点或碰撞触发器。

### 2.5 TTS 语音模块

路径：`RedCulture/Assets/Scripts/RedCulture/Runtime/Tts`

后端接口：

- `GET /api/v1/tts/audio?audio_id=...`

当前用途：

- 下载后端返回的音频。
- 使用 Unity `AudioSource` 播放讲解语音。
- 如果后端仍是 mock TTS，Unity 可能提示音频解码失败，这是正常现象；接入真实 TTS 后即可播放真实音频。

### 2.6 UI 模块

路径：`RedCulture/Assets/Scripts/RedCulture/Runtime/UI`

当前 UI：

- `RedCultureAuthPanelView`：登录、注册、显示后端认证结果。
- `RedCultureBackendFeaturePanelView`：登录后显示功能中心，用于测试房间、答题和 TTS。
- `RedCulturePlayerInputLock`：登录面板显示时禁止玩家移动。

## 3. 当前操作流程

1. 启动 C++ 后端。
2. Unity 进入 Play Mode。
3. 登录或注册。
4. 点击“进入游戏”。
5. 右侧显示“后端功能中心”。
6. 点击“创建房间”。
7. 点击“开始答题”。
8. 根据后端返回的问题填写答案。
9. 点击“提交答案”。
10. 查看 AI 讲解文本和音频地址。
11. 点击“播放讲解语音”。

## 4. 后续建议开发顺序

### 第一阶段：把功能中心变成真实玩法入口

- 将“开始答题”绑定到场景中的展品、说明牌或触发区域。
- 每个触发点提供固定的 `trigger_id` 和 `topic`。
- UI 只展示当前问题、答案输入和 AI 讲解，不再展示调试字段。

### 第二阶段：语音导览

- 给场景路线节点添加触发器。
- 玩家靠近节点时调用后端 AI 讲解接口。
- 后端返回讲解文本和 TTS 音频。
- Unity 播放音频，并显示字幕。

### 第三阶段：答题记录和玩家进度

- 登录后从后端拉取玩家答题历史。
- 答题成功后刷新本地 UI。
- 在 Unity 端展示进度、正确率、已解锁内容。

### 第四阶段：房间与多人玩法

- 如果后续确实需要多人互动，再扩展房间 UI。
- 房间模块可以支持邀请码、成员列表、准备状态、多人同步答题。
- 如果当前项目偏单人导览，房间模块只作为后端上下文记录即可。

## 5. 当前重要约定

- 后端地址不展示给普通用户，默认写在 `RedCultureAuthService` 中。
- 所有业务请求都从服务层发起，不要让场景脚本直接拼 HTTP。
- 登录态统一由 `AuthSessionStore` 保存。
- 后端返回的 `audio_url` 可能是相对路径，Unity 会自动拼成完整 URL。
- 真实业务触发点后续应调用 `RedCultureInteractionService`，不要直接依赖测试面板。
