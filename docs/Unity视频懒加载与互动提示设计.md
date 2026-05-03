# Unity 视频懒加载与互动提示设计

## 1. 当前问题分析

当前场景使用 AVProVideo 播放展厅视频。场景中一共有 6 个 `MediaPlayer`：

- `MediaPlayer`：`毛泽东.mp4`
- `MediaPlayer (1)`：`毛泽东2.mp4`
- `MediaPlayer (2)`：`唐延杰.mp4`
- `抗战1_MediaPlayer`：`抗战1.mp4`
- `抗战2_MediaPlayer`：`抗战2.mp4`
- `抗战3_MediaPlayer`：`抗战3.mp4`

原始配置中这些播放器都是：

- `m_AutoOpen = true`
- `m_AutoStart = true`

AVPro 的 `MediaPlayer` 会在 `Start()` 中根据 `m_AutoOpen` 自动打开视频，因此原来的行为是：场景一启动，所有视频都尝试加载和播放。这样会带来几个问题：

- 启动时 IO、解码和显存压力集中。
- 玩家根本没走到展项附近，视频也已经占用资源。
- 视频越多，后续扩展展厅时性能问题越明显。

另外，当前 6 个 `MediaPlayer` 对象本身都在 `(0, 0, 0)`，不能直接用播放器对象的位置判断玩家距离。真正的视频展示位置来自 `ApplyToMaterial` 绑定的材质，以及场景中使用这些材质的 Renderer。

## 2. 已实现方案

### 2.1 视频懒加载

新增脚本：

`RedCulture/Assets/Scripts/RedCulture/Runtime/World/RedCultureMediaLazyLoader.cs`

职责：

- 在 `Awake()` 中关闭 AVPro 的 `m_AutoOpen` 和 `m_AutoStart`。
- 自动查找当前播放器对应的 `ApplyToMaterial`。
- 根据 `ApplyToMaterial._material` 找到真正显示视频的 Renderer。
- 玩家靠近展示 Renderer 时调用 `OpenVideoFromFile()`。
- 玩家离开展示 Renderer 后调用 `CloseVideo()` 释放视频资源。
- 按 `E` 可以播放/暂停当前视频。

当前默认范围：

- 加载距离：`7m`
- 卸载距离：`10m`

使用卸载距离大于加载距离，是为了避免玩家站在边界附近时视频反复打开/关闭。

### 2.2 世界空间互动提示

新增脚本：

`RedCulture/Assets/Scripts/RedCulture/Runtime/World/RedCultureWorldInteractionPrompt.cs`

职责：

- 在可互动点生成世界空间光圈。
- 玩家靠近时显示文字提示。
- 玩家进入交互范围后提示 `按 E 互动`。
- 支持按键回调，后续答题点、语音讲解点、传送点都可以复用。

当前视觉表现：

- 地面金色互动光圈。
- 展项上方悬浮提示文字。
- 靠近后光圈加粗并显示交互按键。

### 2.3 玩家定位

新增脚本：

`RedCulture/Assets/Scripts/RedCulture/Runtime/World/RedCulturePlayerLocator.cs`

职责：

- 优先查找 `Player` 标签。
- 其次查找 `Camera.main`。
- 再兜底查找 `FirstPersonCharacter` / `FPSController`。

这样可以适配当前场景里的第一人称控制器结构。

## 3. 当前场景已修改内容

当前场景里的 6 个 AVPro `MediaPlayer` 都已经挂载了 `RedCultureMediaLazyLoader`。

同时已持久化关闭：

- `m_AutoOpen = false`
- `m_AutoStart = false`

运行时验证结果：

- 6 个播放器中，只有玩家当前位置附近的 2 个视频被加载。
- 远处 4 个视频保持未加载状态。
- 自动生成了 14 个互动提示锚点，因为部分视频材质被多个 Renderer 使用。
- Unity Console 没有错误日志。

## 4. 后续如何给其他互动点加提示

如果后续有这些对象：

- 场景切换点
- 答题触发点
- AI 语音讲解点
- 展品说明点
- NPC 对话点

都可以直接挂载：

`RedCultureWorldInteractionPrompt`

推荐做法：

1. 在目标位置创建一个空物体，例如 `InteractionPoint_XXX`。
2. 把空物体放在地面或展品前方。
3. 添加 `RedCultureWorldInteractionPrompt`。
4. 设置提示文字，例如：
   - `靠近查看展品`
   - `按 E 开始答题`
   - `按 E 听语音讲解`
   - `按 E 切换场景`
5. 在业务脚本中订阅 `Interacted` 事件，触发后端接口或场景切换。

## 5. 下一步建议

建议下一步做一个统一的 `RedCultureSceneInteractionPoint`：

- 支持配置 `interactionType`：视频、答题、语音讲解、传送。
- 支持配置 `sceneId`、`triggerId`、`topic`。
- 玩家按 `E` 后自动调用后端互动接口。
- 和当前后端 `interactions/start`、`interactions/answer` 串起来。

这样 Unity 场景编辑时只需要摆放互动点，不需要每个展项都写一套脚本。
