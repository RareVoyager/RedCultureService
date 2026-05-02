# Common 公共组件

该目录保存跨模块复用的轻量公共类型。

当前包含：

- `service.hpp`：服务基础示例接口。
- `result.hpp`：统一返回类型 `Result<T>` 和 `Result<void>`。

`Result<T>` 对齐常见 Java/SpringBoot 响应体：

- `code`：整数状态码，默认成功为 `200`，默认失败为 `500`。
- `msg`：提示信息，默认成功为 `success`，默认失败为 `error`。
- `data`：返回数据。C++ 里使用 `std::optional<T>` 保存，因此无数据成功和失败结果都不需要构造一个假的 `T`。

常用写法：

```cpp
auto ok = rcs::common::Result<int>::success(100);
auto fail = rcs::common::Result<int>::error(400, "bad request");
auto saved = rcs::common::Result<void>::success();
```

注意：当 `T` 是 `std::string` 时，`success("xxx")` 会被视为字符串 data。如果只想修改成功提示信息，请使用 `success_msg("xxx")`。

模板实现必须放在头文件中，避免链接阶段找不到实例化代码。