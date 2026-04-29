# Common 公共组件

该目录保存跨模块复用的轻量公共类型。

当前包含：

- `service.hpp`：服务基础示例接口。
- `result.hpp`：统一返回类型 `Result<T>` 和 `Result<void>`。

`Result<T>` 约定：

- 成功：`ok() == true`，`code() == "OK"`，`msg() == "success"`，并且可以通过 `data()` 取得返回数据。
- 失败：`ok() == false`，`code()` 表示错误码，`msg()` 表示错误信息，通常不包含 data。
- `Result<void>` 用于只需要表达成功或失败、不需要数据的函数。

模板实现必须放在头文件中，避免链接阶段找不到实例化代码。