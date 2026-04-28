import os
from conan import ConanFile
from conan.tools.files import copy
from conan.tools.cmake import CMakeDeps, CMakeToolchain

class RedCultureServiceConan(ConanFile):
    name = "red_culture_service"
    version = "1.0.0"
    settings = "os", "compiler", "build_type", "arch"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_grpc": [True, False],
        "with_tests": [True, False],
    }

    default_options = {
        "shared": False,  # 建议默认静态链接，减少部署麻烦
        "fPIC": True,
        "with_grpc": True,
        "with_tests": True,
        # 传递给依赖库的配置
        "boost/*:header_only": True, # 如果只用 asio/beast，可以尝试 header-only 减小体积
        "spdlog/*:header_only": True,
    }

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def requirements(self):
        # --- 基础组件 ---
        self.requires("boost/1.84.0")
        self.requires("spdlog/1.14.1")
        # self.requires("fmt/10.2.1")     # 显式指定，确保版本兼容
        self.requires("nlohmann_json/3.11.3")
        self.requires("yaml-cpp/0.8.0")
        self.requires("openssl/3.3.2")
        self.requires("zlib/1.3.1")

        # --- 数据与缓存 ---
        self.requires("redis-plus-plus/1.3.10")
        self.requires("hiredis/1.2.0")
        self.requires("libpqxx/7.9.1")

        # --- 可观测性 ---
        self.requires("prometheus-cpp/1.2.4")

        # --- 实用增强 ---
        self.requires("jwt-cpp/0.7.0")

        # grpc 相关
        self.requires("protobuf/5.27.0")
        self.requires("grpc/1.67.1")

        # 测试相关
        self.test_requires("gtest/1.15.0")

    def generate(self):
        # 生成 CMake 配置文件 (用于 find_package)
        tc = CMakeDeps(self)
        tc.generate()

        # 生成编译器工具链配置
        tc = CMakeToolchain(self)
        tc.generate()

        # 动态库拷贝逻辑改进
        if self.options.shared:
            dest = os.path.join(self.build_folder, str(self.settings.build_type), "bin")
            for dep in self.dependencies.values():
                # 拷贝 Windows 下的 DLL
                if self.settings.os == "Windows":
                    for bindir in dep.cpp_info.bindirs:
                        copy(self, "*.dll", bindir, dest)
                # 拷贝 Linux 下的 SO
                else:
                    for libdir in dep.cpp_info.libdirs:
                        copy(self, "*.so*", libdir, dest)

    def layout(self):
        # 定义标准的目录结构
        self.folders.build = "build"
        self.folders.generators = "build/generators"