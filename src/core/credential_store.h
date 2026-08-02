#pragma once

#include "foundation/utils/json_utils.h"
#include <string>
#include <optional>
#include <mutex>

namespace st {

/// 敏感凭证安全存储 — 使用 Windows DPAPI 加密
///
/// 保护对象：数据源 token、券商 API 凭证、AI 服务 key、数据库口令
///
/// 加密方式：整个 secret JSON 序列化后用 CryptProtectData 加密 → Base64 → 写文件
/// 绑定当前 Windows 用户，其他用户/磁盘离线窃取无法解密。
///
/// 威胁模型详见 docs/security.md
class CredentialStore {
public:
    static CredentialStore* instance();

    CredentialStore();
    ~CredentialStore();

    /// 初始化并加载加密文件（secretPath 不存在时创建空库）
    bool init(const std::string& secretPath);

    /// 保存凭证（覆盖同名 key）
    void setSecret(const std::string& key, const std::string& value);

    /// 读取凭证，不存在/解密失败返回 nullopt
    std::optional<std::string> getSecret(const std::string& key);

    /// 删除凭证
    bool removeSecret(const std::string& key);

    /// 是否有指定 key
    bool hasSecret(const std::string& key) const;

    bool isInitialized() const { return initialized_; }
    const std::string& secretPath() const { return secretPath_; }

private:
    /// 加密明文 → Base64 字符串
    static std::string encrypt(const std::string& plaintext);
    /// 解密 Base64 密文 → 明文（失败返回空串）
    static std::string decrypt(const std::string& ciphertextBase64);

    /// 持久化到磁盘（加密写入）
    bool flush() const;

    std::string secretPath_;
    bool initialized_ = false;
    utils::Json data_;              // { key: base64密文 }

    mutable std::mutex mutex_;
};

} // namespace st
