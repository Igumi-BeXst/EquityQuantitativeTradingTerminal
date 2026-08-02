# 安全设计

## 目标

StockTerminal 将**开源公开 + 纯本地单机**分享给他人使用。本文件说明敏感数据的保护方式、威胁模型，以及用户应当了解的安全边界。

## 受保护的敏感数据

| 数据 | 说明 |
|------|------|
| 数据源 token | Tushare / AKShare 等数据服务的访问凭证 |
| 券商 API 凭证 | XTP 等券商接口的账号/密码/证书（预留） |
| AI 服务 API key | Claude / GPT 等 AI 服务的密钥（预留） |
| 数据库口令 | SQLite 本地库口令（可选） |

## 保护机制

### 1. CredentialStore — DPAPI 加密

所有敏感凭证通过 `CredentialStore` 存储：

- **加密算法**：Windows DPAPI (`CryptProtectData`/`CryptUnprotectData`)
- **密钥管理**：由 Windows 系统管理，基于当前用户凭证派生，**无需应用自存密钥**
- **存储位置**：`%APPDATA%\StockTerminal\secrets\secrets.json`
- **存储格式**：整个 JSON 序列化 → DPAPI 加密 → Base64 → 落盘
- **绑定用户**：只有加密时的 Windows 用户能解密

### 2. AppPaths — 用户目录隔离

```
%APPDATA%\StockTerminal\
├── config\        (非敏感配置)
├── data\          (SQLite 数据库)
├── secrets\       (加密凭证, DPAPI)
└── logs\          (日志)
```

- 程序目录保持只读，所有私有数据放用户目录
- 分享给他人使用时，每个用户的凭证互不干扰

### 3. 零遥测

- 程序**不收集、不上传**任何用户数据
- 数据源 HTTP 请求为**只读获取行情**，不涉及用户数据上报
- 无崩溃上报、无使用统计、无远程通知

## 威胁模型

| 威胁 | 防护 | 说明 |
|------|------|------|
| 磁盘被盗/离线读取 | ✅ DPAPI | 无当前用户凭证无法解密 |
| 其他 Windows 用户 | ✅ DPAPI | 绑定加密时的用户 |
| 程序文件被逆向分析 | ✅ | 凭证不在程序目录/代码中 |
| 备份文件泄露 | ✅ | 备份的是密文 |
| 以当前用户权限运行的木马 | ⚠️ 有限 | 同权限可调用同 API 解密 |

### 木马场景的客观边界

**任何本地密钥保护都挡不住已在你账号下运行的木马**——DPAPI 就是为"当前登录用户"解密的，木马以你的权限运行即可调用相同的 API。这是所有本地软件的客观局限，不是本软件的缺陷。

对木马泄露的真正防线在**服务端侧**：
1. **token 最小权限** — 只申请所需的最小权限
2. **token 定期轮换** — 泄露后风险可控
3. **服务端风控** — 数据商/券商自身的 IP 白名单、设备指纹、异地检测

## 用户实践建议

1. **定期轮换 token**，不要使用长期不换的高权限凭证
2. **不要分享 secrets.json 文件**，它是绑定你账号的密文
3. 保持操作系统安全（杀软、UAC、不运行未知程序）
4. 认领最小权限的 token，够用即可

## 实现位置

| 组件 | 路径 |
|------|------|
| CredentialStore | `src/core/credential_store.h/cpp` |
| AppPaths | `src/core/app_paths.h/cpp` |
| 配置集成 | `src/core/config_manager.h/cpp` |
| 凭证管理 UI | P5 阶段 `src/ui/panels/` |

## 验证方式

1. 单元测试 `test_credential_store`：加密/解密往返、持久化、密文检查
2. `strings secrets.json` 无法读出明文 token
3. 编译零警告
