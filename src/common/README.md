# Common 实现目录

该目录保存 common 模块中需要编译的 `.cpp` 实现。

注意：

- `Result<T>` 是模板类型，必须保持 header-only，放在 `include/rcs/common/result.hpp`。
- 只有非模板、需要单独编译的公共实现才放到这里。