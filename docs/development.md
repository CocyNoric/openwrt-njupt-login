# 构建与开发

## 目录

```text
src/        C++ 认证、联网探测与状态控制
openwrt/    SDK 包定义、UCI 模板、procd 与网络脚本
cmake/      交叉编译工具链配置
tools/      Windows 构建、aarch64 打包与安装脚本
tests/      核心、模拟门户和隔离网络脚本测试
examples/   不含真实凭据的配置示例
docs/       使用与开发说明
```

`build*/`、`.deps/` 和 `dist/` 分别存放构建产物、下载的依赖和安装包，均不提交。个人配置可放在 `local.conf`，现场记录可放在 `docs/local/`，这两个位置也已忽略。

## 获取源码与本机构建

需要 C++17 工具链、CMake 3.20+、libcurl 7.66+ 开发文件；运行 Python 测试和打包脚本需要 Python 3.10+。

```sh
git clone https://github.com/CocyNoric/openwrt-njupt-login.git
cd openwrt-njupt-login
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Linux 模拟门户测试使用 loopback 与设备绑定，需要相应 capability，必要时以 root 运行 CTest。网络脚本测试隔离替换系统网络命令，不修改主机路由。Windows 测试使用 loopback、`mark=0` 和空设备名，POSIX 信号退出测试会跳过。

Windows 可使用 Visual Studio C++ 工具链。先将 [dependencies.json](../tools/dependencies.json) 中的 libcurl 源码校验并解压到 `.deps/curl-8.22.0`，再运行：

```powershell
./tools/build-windows.ps1 -CurlSource "$PWD/.deps/curl-8.22.0" -Python 'PATH_TO_PYTHON'
# 已有本地 libcurl 构建时可省略 -CurlSource
```

Shell 测试需要 Bash，Windows 支持使用 Git for Windows 自带的 Bash。

## OpenWrt SDK 包

先在设备上用 `ubus call system board` 与 `uname -m` 确认固件、target/subtarget 和架构，再使用匹配的 Linux SDK。将源码放入 SDK 的 `package/openwrt-njupt-login`；只复制所需文件：

```sh
mkdir -p package/openwrt-njupt-login
cp -r /path/to/openwrt-njupt-login/src /path/to/openwrt-njupt-login/openwrt \
    package/openwrt-njupt-login/
cp /path/to/openwrt-njupt-login/CMakeLists.txt /path/to/openwrt-njupt-login/Makefile \
    /path/to/openwrt-njupt-login/LICENSE package/openwrt-njupt-login/
make menuconfig
# Network -> openwrt-njupt-login 选择 M
make package/openwrt-njupt-login/compile V=s -j4
```

包依赖 `libstdcpp`、`libcurl`、`ca-bundle`、`ip-full` 和 `flock`，使用固件动态库。ipk/apk 格式取决于 SDK 和固件。生成的包名为 `openwrt-njupt-login`，安装后的命令和服务名为 `njupt-login`。

## aarch64 静态安装包

Windows 交叉编译使用 Zig 0.15.2、libcurl 8.22.0 和 Mbed TLS 3.6.7，目标为 `aarch64-linux-musl`。按 [dependencies.json](../tools/dependencies.json) 的来源和 SHA256 下载、校验后，在 `.deps/` 中解压为对应版本目录：

```text
.deps/zig-x86_64-windows-0.15.2/
.deps/curl-8.22.0/
.deps/mbedtls-3.6.7/
```

```powershell
./tools/build-aarch64.ps1 -Python 'PATH_TO_PYTHON'
& 'PATH_TO_PYTHON' tools/package-aarch64.py
```

打包脚本校验 ELF64、aarch64 和静态链接属性，生成 `dist/openwrt-njupt-login-0.1.0-r4-aarch64-static.tar.gz`、归档校验和及 `build-info.json`，并附带项目和第三方许可证。安装方法见 [使用说明](usage.md)。静态依赖的更新需要重新构建安装包；本机测试和静态链接检查不能替代路由器实测。
