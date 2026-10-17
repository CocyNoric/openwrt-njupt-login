# openwrt-njupt-login

面向 OpenWrt / iStoreOS 的南京邮电大学校园网认证客户端，支持自动登录、联网探测和掉线重认证，并提供 UCI 配置、procd 服务及 OpenClash 认证流量绕行。

认证与探测流量通过校园 WAN 发出；日常上网流量由路由器现有的网络与代理配置处理。保活采用定时联网探测，未实现专用的 Dr.COM UDP 心跳。

## 技术栈

- **C++17 + libcurl**：HTTP 认证、联网探测与重试。
- **OpenWrt UCI / procd + Shell**：配置、服务管理、WAN 热插拔和策略路由。
- **CMake**：本机构建与 OpenWrt SDK 集成；静态 aarch64 构建使用 Zig、musl 和 Mbed TLS。
- **Python + CTest**：模拟门户与网络脚本测试、安装包打包。

## 快速上手

### 1. 下载并安装

从 [Releases](https://github.com/CocyNoric/openwrt-njupt-login/releases) 下载与路由器架构、固件匹配的预编译安装包，也可以按照 [构建说明](docs/development.md) 自行构建。

以下以 **aarch64 静态 tar.gz 包首次安装**为例。将安装包及对应的 `.sha256` 文件上传到路由器 `/tmp`，通过 SSH 以 root 登录路由器，安装运行依赖：

```sh
if command -v apk >/dev/null 2>&1; then
    apk update && apk add ip-full ca-bundle flock
else
    opkg update && opkg install ip-full ca-bundle flock
fi
```

校验并解压安装包，然后安装（文件名替换为实际下载的版本）：

```sh
cd /tmp
archive='openwrt-njupt-login-0.1.0-r4-aarch64-static.tar.gz'
sha256sum -c "$archive.sha256" && \
    tar -xzf "$archive" && \
    cd "${archive%.tar.gz}" && \
    sh install.sh
```

### 2. 配置账号与接口

安装后服务默认关闭。打开配置文件：

```sh
vi /etc/config/njupt-login
```

修改已有配置中的以下字段，保留其余默认项：

```uci
config login 'main'
    option enabled '1'
    option network 'wan'
    option username 'YOUR_USERNAME'
    option password 'YOUR_PASSWORD'
    option portal_ip '10.10.244.11'
```

将 `YOUR_USERNAME` 和 `YOUR_PASSWORD` 替换为校园网账号与密码；账号需要运营商后缀时一并填写。`network` 填连接校园网的逻辑接口名，通常为 `wan`；`portal_ip` 按实际认证门户填写。

### 3. 启动并确认联网

```sh
chmod 600 /etc/config/njupt-login
/etc/init.d/njupt-login enable
/etc/init.d/njupt-login restart
/etc/init.d/njupt-login status
logread -e njupt-login
```

`enable` 设置开机自启，`restart` 立即启动服务。日志出现 `state=online` 表示联网探测通过；修改配置后再次执行 `restart` 即可生效。

更多配置、SDK 包安装及 OpenClash 设置见 [使用说明](docs/usage.md)。项目包名为 `openwrt-njupt-login`，程序、服务和配置文件名为 `njupt-login`。

## License

Copyright 2026 CocyNoric.

本项目采用 [Apache License 2.0](LICENSE)。第三方依赖遵循各自的许可证，静态安装包附带相关许可文本。
