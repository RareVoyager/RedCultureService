# auth

会话与鉴权实现：认证流程、Token 验签、会话状态维护。

## 文件说明

- `session_auth_service.cpp`：基于 `jwt-cpp` 实现 HS256 JWT 签发和校验，并维护内存会话表。
- `password_hasher.cpp`：基于 Linux `crypt_r`/libxcrypt 实现 bcrypt 密码哈希和校验。

## 后续扩展

后续可以接入 Redis 会话缓存、刷新令牌、踢下线、同账号多端策略和权限角色模型。
