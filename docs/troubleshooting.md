# 常见问题排查

## 编译问题
- Qt not found: 确保 vcpkg 安装 qt6 且 CMAKE_TOOLCHAIN_FILE 正确
- spdlog not found: vcpkg install spdlog

## 运行时问题
- DLL missing: 将 build/vcpkg_installed/*/bin 加入 PATH
- SQLite error: 确保有读写 data/ 目录权限
