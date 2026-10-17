# openwrt-njupt-login

面向 OpenWrt / iStoreOS 的南京邮电大学校园网认证客户端，支持自动登录、联网探测和掉线重认证，并提供 UCI 配置、procd 服务及 OpenClash 认证流量绕行。

认证与探测流量通过校园 WAN 发出；日常上网流量由路由器现有的网络与代理配置处理。保活采用定时联网探测，未实现专用的 Dr.COM UDP 心跳。

## 技术栈

- **C++17 + libcurl**：HTTP 认证、联网探测与重试。
- **OpenWrt UCI / procd + Shell**：配置、服务管理、WAN 热插拔和策略路由。
- **CMake**：本机构建与 OpenWrt SDK 集成；静态 aarch64 构建使用 Zig、musl 和 Mbed TLS。
- **Python + CTest**：模拟门户与网络脚本测试、安装包打包。

## 怎么用

1. 使用匹配固件的 OpenWrt SDK 构建安装包，或构建 aarch64 静态安装包，详见 [构建说明](docs/development.md)。
2. 安装后编辑 `/etc/config/njupt-login`，填写校园网账号、密码和 WAN 接口，并设置 `enabled` 为 `1`。
3. 启动服务并查看日志：

```sh
chmod 600 /etc/config/njupt-login
/etc/init.d/njupt-login enable
/etc/init.d/njupt-login restart
logread -e njupt-login
```

安装步骤、配置示例及 OpenClash 设置见 [使用说明](docs/usage.md)。项目包名为 `openwrt-njupt-login`，程序、服务和配置文件名为 `njupt-login`。

## License

Copyright 2026 CocyNoric and openwrt-njupt-login contributors.

本项目采用 [Apache License 2.0](LICENSE)。第三方依赖遵循各自的许可证，静态安装包附带相关许可文本。
