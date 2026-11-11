# 使用说明

## 安装

使用与路由器固件、CPU 架构匹配的安装包。SDK 包由固件的 `opkg` 或 `apk` 安装，依赖由对应软件源提供，构建方法见 [构建说明](development.md)。

aarch64 静态安装包需要 OpenWrt 服务环境、`ip-full`、`ca-bundle` 和 `flock`。先从当前固件软件源安装依赖，再将生成的归档及 `.sha256` 文件上传到路由器 `/tmp`：

```sh
cd /tmp
sha256sum -c openwrt-njupt-login-0.1.0-r4-aarch64-static.tar.gz.sha256
tar -xzf openwrt-njupt-login-0.1.0-r4-aarch64-static.tar.gz
cd openwrt-njupt-login-0.1.0-r4-aarch64-static
sha256sum -c payload.sha256
sh install.sh
```

静态安装包为手动安装的 tar.gz，不登记到包管理器。安装脚本检查架构、文件校验和及运行依赖；发现已有程序或服务时拒绝覆盖，并保留已有 UCI 配置。首次安装后服务默认关闭，固件升级后可能需要重新安装。

## 配置并启动

编辑 `/etc/config/njupt-login`，至少配置以下内容：

```uci
config login 'main'
    option enabled '1'
    option network 'wan'
    option username 'YOUR_USERNAME'
    option password 'YOUR_PASSWORD'
    option portal_ip '10.10.244.11'
    option interval '15'
    option failure_threshold '3'
```

`network` 是连接校园网的逻辑接口名；账号后缀按学校认证要求填写。完整模板见 [njupt-login.config](../openwrt/files/njupt-login.config)。用编辑器填写凭据，避免把实际密码留在命令历史中。

```sh
chmod 600 /etc/config/njupt-login
/etc/init.d/njupt-login enable
/etc/init.d/njupt-login restart
logread -e njupt-login
/etc/init.d/njupt-login status
```

启动后生成 `/var/run/njupt-login/config`，目录权限为 700，文件权限为 600。修改配置后重启服务。停止服务用 `/etc/init.d/njupt-login stop`；停止不会向校园网发送注销请求。

## 联网探测与重试

- 启动时先检查联网状态，已经在线则跳过登录。
- 默认每 15 秒探测，曾经在线后连续 3 轮失败才重新认证；登录完成后再次检查联网。
- 认证失败按 10、20、40 秒递增退避，最长 300 秒；门户明确提示费用超支时默认等待 900 秒。
- WAN 不可用时等待链路，WAN 地址变化时重新获取设备与地址。

默认按以下顺序探测，首个成功即视为在线；只有两个端点都失败才计为一轮失败：

1. **阿里 DNS `223.5.5.5`（优先）**：请求 `https://223.5.5.5/resolve?name=bilibili.com&type=A`，要求 HTTP 200 且正文含 `"Status":0`。
2. **B 站（备用）**：请求 `https://www.bilibili.com/robots.txt`，固定 IPv4 为 `223.111.249.190`，要求 HTTP 200 且正文含 `Disallow: /medialist/detail/`。`bilibili.com` 裸域名会跳转，因此直接使用 `www.bilibili.com`。

两个探测均使用 HTTPS 并校验证书。正常情况下每分钟约 4 次轻量请求，15 秒间隔通常负担很小；实际周期还包括请求和网络规则检查耗时。B 站固定 IPv4 已于 2026-10-17 验证，后续可能需要维护。请在目标校园网确认探测点登录后可达、未登录时不可达，尤其注意 DNS 服务可能在未认证时放行，避免误判在线。

自定义域名探测需同步设置 `probe_url`、真实的 `probe_ip`、`probe_status` 和 `probe_body`；设置自定义 `probe_url` 后，备用端点由 `probe2_*` 显式配置，不会自动添加默认 B 站端点。

这里的保活是联网探测与掉线重认证。长期在线和不同校园认证部署的兼容性需在目标网络验证；认证门户沿用 HTTP 协议，密码会按该协议在校园链路上传输。

客户端兼容旧版登录接口和新版 EPortal 门户：旧接口返回 EPortal 应用页面时，自动改用 `/eportal/portal/login` JSONP 接口，并继续通过外网探测确认联网。新版接口要求 GET，请求包含账号和密码，因此不要记录或分享完整认证 URL。

## OpenClash 与出口

客户端绑定校园 WAN 的设备和 IPv4，以标记 `0x4e4a` 匹配专用策略路由及 OpenClash 输出链的 RETURN 规则。每个周期会检查并恢复规则；OpenClash 不同版本和运行模式需要在设备上验证。

需要在 OpenClash 重启后立即恢复规则时，在现有 `/etc/openclash/custom/openclash_custom_firewall_rules.sh` 的 `exit 0` 前加入：

```sh
/usr/libexec/njupt-login/openclash-bypass setup
```

本服务使用路由表 `20260`、规则优先级 `100`、路由协议 `186`；检测到冲突会拒绝修改。可检查实际出口：

```sh
ip -4 -N rule show
ip -4 route show table 20260
ip -4 route get 223.5.5.5 from YOUR_WAN_IP mark 0x4e4a
```

校园设备的直连规则、LAN 到 WAN 的转发和 NAT 由现有网络配置管理。

## 命令行

`njupt-login --help` 可查看参数；本机配置示例见 [client.conf](../examples/client.conf)。

| 参数 | 用途 |
| --- | --- |
| `--validate` | 仅校验配置，不发送网络请求、不修改路由 |
| `--check` | 仅检查联网；OpenWrt 模式仍会刷新网络规则 |
| `--once` | 执行一次探测或认证周期后退出 |

退出码：`0` 成功，`1` 本周期未确认联网，`2` 配置或初始化错误。运行本机示例前请填写 WAN IPv4 和设备名；不要同时启动多个认证实例。
