# Git 工作流

## 分支策略
- `main`: 稳定版本
- `dev`: 开发分支
- `feature/*`: 功能分支
- `fix/*`: 修复分支

## Commit 格式
```
<type>: <short description>

<optional body>
```

类型: feat, fix, refactor, test, docs, build

## 示例
```
feat: add StockCode type with market detection

Parse stock codes from SH600519 and 600519 formats.
Includes unit tests covering all A-share exchanges.
```
