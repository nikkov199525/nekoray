## 构建 nekobox_core

### 目录结构

```
  | nekoray
  |   go/cmd/*
  | sing-box
  | ......
```

### 常规构建

1. `bash libs/get_source.sh` （自动下载目录结构，自动 checkout commit）
2. `GOOS=windows GOARCH=amd64 bash libs/build_go.sh`

具体支持的 GOOS 和 GOARCH 请看 `libs/build_go.sh`

非官方构建无需编译 `updater` `launcher`

### Core versions

sing-box 源码提交和 Xray 发布版本都固定在 `libs/get_source_env.sh`。`libs/get_source.sh` 从官方 SagerNet 仓库检出 sing-box；`libs/build_go.sh` 将 sing-box 版本写入 `nekobox_core`。发布脚本按固定版本下载官方 Xray 二进制文件，以保证构建可复现。

Xray 26.3.27 在 2026-06-01 后拒绝 `allowInsecure`。因此 GUI 只对 sing-box 应用跳过证书验证；Xray 配置必须使用有效证书或 `pinnedPeerCertSha256`。

sing-box 1.13 已删除旧的 inbound 字段和 DNS outbound。GUI 现在生成 route actions；`nekobox_core` 也会在读取旧版 GUI 配置时自动迁移这些字段。

`geoip.dat`、`geosite.dat` 和 sing-box `.srs` 规则来自 `runetfreedom/russia-v2ray-rules-dat` 的 `release` 分支，并在构建时校验 SHA-256。
