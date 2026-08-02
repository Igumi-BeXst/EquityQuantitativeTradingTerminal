#include "core/credential_store.h"
#include "core/log_manager.h"
#include <QByteArray>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#endif

#include <filesystem>
#include <vector>

namespace st {

CredentialStore* CredentialStore::instance() {
    static CredentialStore store;
    return &store;
}

CredentialStore::CredentialStore() = default;

CredentialStore::~CredentialStore() = default;

#ifdef _WIN32

std::string CredentialStore::encrypt(const std::string& plaintext) {
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    in.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"StockTerminalCredentials", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        LogManager::instance()->log(LogLevel::Error, "CryptProtectData failed, error={}",
                                    static_cast<int>(GetLastError()));
        return {};
    }

    QByteArray binary(reinterpret_cast<const char*>(out.pbData),
                      static_cast<int>(out.cbData));
    LocalFree(out.pbData);

    // 存 Base64（JSON 要求有效 UTF-8 字符串）
    return binary.toBase64().toStdString();
}

std::string CredentialStore::decrypt(const std::string& ciphertext) {
    // 先 Base64 解码
    QByteArray binary = QByteArray::fromBase64(QByteArray::fromStdString(ciphertext));
    if (binary.isEmpty() && !ciphertext.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "Base64 decode failed");
        return {};
    }

    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(binary.data());
    in.cbData = static_cast<DWORD>(binary.size());

    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr,
                            nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        LogManager::instance()->log(LogLevel::Warn, "CryptUnprotectData failed, error={}",
                                    static_cast<int>(GetLastError()));
        return {};
    }

    std::string plaintext(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return plaintext;
}

#else
// 非 Windows: 明文占位（跨平台预留）
std::string CredentialStore::encrypt(const std::string& plaintext) { return plaintext; }
std::string CredentialStore::decrypt(const std::string& ciphertext) { return ciphertext; }
#endif

bool CredentialStore::init(const std::string& secretPath) {
    secretPath_ = secretPath;

    // 确保父目录存在
    std::filesystem::path path(secretPath);
    auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            LogManager::instance()->log(LogLevel::Error, "Cannot create secret dir: {}", ec.message());
            return false;
        }
    }

    // 加载已有加密文件
    auto loaded = utils::loadJsonFile(secretPath);
    if (loaded) {
        data_ = std::move(*loaded);
    } else {
        data_ = utils::Json::object();
        // 首次初始化: 创建空库文件
        flush();
    }

    initialized_ = true;
    return true;
}

bool CredentialStore::flush() const {
    if (secretPath_.empty()) return false;
    return utils::saveJsonFile(secretPath_, data_);
}

void CredentialStore::setSecret(const std::string& key, const std::string& value) {
    if (key.empty() || value.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto encrypted = encrypt(value);
    if (encrypted.empty()) return;
    data_[key] = utils::Json(encrypted);
    flush();
}

std::optional<std::string> CredentialStore::getSecret(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end() || !it->is_string()) {
        return std::nullopt;
    }
    auto decrypted = decrypt(it->get<std::string>());
    if (decrypted.empty()) return std::nullopt;
    return decrypted;
}

bool CredentialStore::removeSecret(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (data_.contains(key)) {
        data_.erase(key);
        flush();
        return true;
    }
    return false;
}

bool CredentialStore::hasSecret(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.contains(key);
}

} // namespace st
