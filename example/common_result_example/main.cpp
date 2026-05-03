#include "rcs/common/result.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

using rcs::common::Result;

struct User {
    int id{};
    std::string name;
};

void testSuccessWithoutData()
{
    auto result = Result<int>::success();

    std::cout << "testSuccessWithoutData\n";
    std::cout << "code = " << result.code() << '\n';
    std::cout << "msg = " << result.msg() << '\n';
    std::cout << "ok = " << result.ok() << '\n';
    std::cout << "hasData = " << result.hasData() << '\n';

    try {
        int value = result.data();
        std::cout << "data = " << value << '\n';
    } catch (const std::logic_error& e) {
        std::cout << "catch exception: " << e.what() << '\n';
    }

    std::cout << "------------------------\n";
}

void testSuccessWithIntData()
{
    auto result = Result<int>::success(100);

    std::cout << "testSuccessWithIntData\n";
    std::cout << "code = " << result.code() << '\n';
    std::cout << "msg = " << result.msg() << '\n';
    std::cout << "ok = " << result.ok() << '\n';
    std::cout << "hasData = " << result.hasData() << '\n';
    std::cout << "data = " << result.data() << '\n';

    std::cout << "------------------------\n";
}

void testSuccessWithMsgAndData()
{
    auto result = Result<int>::success("query user count success", 10);

    std::cout << "testSuccessWithMsgAndData\n";
    std::cout << "code = " << result.code() << '\n';
    std::cout << "msg = " << result.msg() << '\n';
    std::cout << "ok = " << result.ok() << '\n';
    std::cout << "hasData = " << result.hasData() << '\n';
    std::cout << "data = " << result.data() << '\n';

    std::cout << "------------------------\n";
}

void testError()
{
    auto result = Result<int>::error("database connection failed");

    std::cout << "testError\n";
    std::cout << "code = " << result.code() << '\n';
    std::cout << "msg = " << result.msg() << '\n';
    std::cout << "ok = " << result.ok() << '\n';
    std::cout << "hasData = " << result.hasData() << '\n';

    if (!result) {
        std::cout << "result is false\n";
    }

    std::cout << "------------------------\n";
}

void testErrorWithCustomCode()
{
    auto result = Result<int>::error(404, "user not found");

    std::cout << "testErrorWithCustomCode\n";
    std::cout << "code = " << result.code() << '\n';
    std::cout << "msg = " << result.msg() << '\n';
    std::cout << "ok = " << result.ok() << '\n';

    std::cout << "------------------------\n";
}

void testVoidResult()
{
    auto result = Result<void>::success("delete success");

    std::cout << "testVoidResult\n";
    std::cout << "code = " << result.code() << '\n';
    std::cout << "msg = " << result.msg() << '\n';
    std::cout << "ok = " << result.ok() << '\n';

    std::cout << "------------------------\n";
}

void testStringResult()
{
    auto result1 = Result<std::string>::success("hello");

    std::cout << "testStringResult\n";
    std::cout << "result1.code = " << result1.code() << '\n';
    std::cout << "result1.msg = " << result1.msg() << '\n';
    std::cout << "result1.data = " << result1.data() << '\n';

    auto result2 = Result<std::string>::successMsg("only change success message");

    std::cout << "result2.code = " << result2.code() << '\n';
    std::cout << "result2.msg = " << result2.msg() << '\n';
    std::cout << "result2.hasData = " << result2.hasData() << '\n';

    std::cout << "------------------------\n";
}

void testVectorResult()
{
    std::vector<int> nums{1, 2, 3, 4, 5};

    auto result = Result<std::vector<int>>::success("query list success", nums);

    std::cout << "testVectorResult\n";
    std::cout << "code = " << result.code() << '\n';
    std::cout << "msg = " << result.msg() << '\n';
    std::cout << "ok = " << result.ok() << '\n';

    std::cout << "data = ";
    for (int num : result.data()) {
        std::cout << num << ' ';
    }
    std::cout << '\n';

    std::cout << "------------------------\n";
}

void testCustomStructResult()
{
    User user{1, "chenanqi"};

    auto result = Result<User>::success("query user success", user);

    std::cout << "testCustomStructResult\n";
    std::cout << "code = " << result.code() << '\n';
    std::cout << "msg = " << result.msg() << '\n';
    std::cout << "ok = " << result.ok() << '\n';
    std::cout << "user.id = " << result.data().id << '\n';
    std::cout << "user.name = " << result.data().name << '\n';

    std::cout << "------------------------\n";
}

int main()
{
    testSuccessWithoutData();
    testSuccessWithIntData();
    testSuccessWithMsgAndData();
    testError();
    testErrorWithCustomCode();
    testVoidResult();
    testStringResult();
    testVectorResult();
    testCustomStructResult();

    return 0;
}
