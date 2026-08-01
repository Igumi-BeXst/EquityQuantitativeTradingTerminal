#pragma once

#include "foundation/utils/json_utils.h"
#include <string>
#include <mutex>
#include <QObject>

namespace st {

/// JSON-based hierarchical configuration manager.
/// Uses nlohmann::json internally, supports nested key paths ("a.b.c").
class ConfigManager : public QObject {
    Q_OBJECT

public:
    static ConfigManager* instance();

    explicit ConfigManager(QObject* parent = nullptr);

    /// Load from JSON file
    bool load(const std::string& configPath);

    /// Save current config to file
    bool save();

    /// Get value by dotted path, returns default if not found
    template<typename T>
    T get(const std::string& key, const T& defaultValue) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto* node = utils::getNested(data_, key);
        if (node && node->is_number() && std::is_arithmetic_v<T>) {
            return node->get<T>();
        }
        if (node && node->is_string() && std::is_same_v<T, std::string>) {
            return node->get<T>();
        }
        if (node && node->is_boolean() && std::is_same_v<T, bool>) {
            return node->get<T>();
        }
        return defaultValue;
    }

    /// Set value by dotted path
    template<typename T>
    void set(const std::string& key, const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        utils::setNested(data_, key, value);
    }

    /// Full JSON access for complex types
    const utils::Json& data() const { return data_; }

signals:
    void configChanged(const QString& key);

private:
    utils::Json data_;
    std::string configPath_;
    mutable std::mutex mutex_;
};

} // namespace st
