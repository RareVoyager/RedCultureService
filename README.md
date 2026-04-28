## 程序运行

1. 在cmd命令窗口执行如下命令，配置依赖。

```cmd
conan install . --output-folder=build/conan --build=missing --profile:host=gccConfig --profile:build=gccConfig 
```

2. 在 Clion 的cmake配置里面添加如下内容。

```cmd
-DCMAKE_TOOLCHAIN_FILE="../conan/conan_toolchain.cmake"
```

​	写如上内容的话，默认编译文件夹与conan文件夹同级

3. C++生成器选择Ninja

4. 配置Debug 以及Release 运行环境。



## 服务器架构

### 一、包管理工具

​	使用 **conan 2** 进行包管理。用来统一管理三方库或其他组件库

### 二、 项目使用到的三方库以及作用(已经添加到conanfile.py 文件中)

**核心运行时**

1. `boost`（asio/beast 等网络与基础组件）
2. `spdlog`（日志）
3. `fmt`（格式化，通常被 spdlog 间接依赖）
4. `nlohmann_json`（JSON）
5. `yaml-cpp`（配置）
6. `openssl`（TLS/加密）
7. `protobuf`（协议）
8. `grpc`（服务间 RPC）

**数据与缓存**
1. `redis-plus-plus`（Redis C++ 客户端）
2. `hiredis`（redis-plus-plus 底层依赖）
3. `libpqxx`（PostgreSQL C++ 客户端）
4. `zlib`（压缩，很多库会间接用到）

**可观测性**
1. `prometheus-cpp`（指标监控）
2. `opentelemetry-cpp`（链路追踪，可选但建议预留）

**测试与质量**

1. `gtest`（单元测试）

**实用增强（备选方案）**

1. `jwt-cpp`（JWT 鉴权）



## 三、服务器包含的核心模块

1. **网络接入层** 
连接管理、协议编解码、心跳、断线重连、限流。

2. **会话与鉴权模块** 
登录鉴权、Token 校验、玩家会话生命周期管理。

3. **房间与匹配模块** 
创建/加入/离开房间、匹配队列、房间状态维护。

4. **游戏状态同步模块** 
玩家位置/动作同步、状态广播、服务器权威校验。

5. **AI 交互编排模块** 
触发点检测、调用 AI 服务、题目与讲解流程控制、超时重试。

6. **语音/TTS 模块** 
AI 文本转语音请求、音频资源回传、缓存策略。

7. **数据存储模块** 
用户数据、答题记录、进度存档、日志落库（DB/Redis）。

8. **配置与热更新模块** 
配置加载、环境切换、可控参数更新（触发点/题库策略）。

9. **日志与监控模块** 
结构化日志、指标采集、告警（延迟、错误率、在线人数）。

10. **运维与部署模块** 
健康检查、优雅停服、版本发布回滚、基础管理接口。
