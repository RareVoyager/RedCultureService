# auth

会话与鉴权层头文件：登录鉴权、Token 校验、会话生命周期接口。

## 当前职责

- `session_auth_service.hpp`：定义鉴权配置、JWT claims、会话结构、登录结果和会话管理服务。
- `password_hasher.hpp`：封装 bcrypt 密码哈希和校验接口。
- 负责签发 JWT、校验 JWT、创建/刷新会话、查询会话、关闭会话和清理过期会话。

## 设计边界

当前模块不直接访问数据库，也不直接依赖网络接入层。账号密码的 bcrypt 哈希由本模块封装，用户读取和写入仍由 storage 模块完成。
