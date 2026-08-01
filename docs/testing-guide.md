# 测试策略

## 测试框架
GoogleTest

## 测试分类
- Foundation: 每个类型和工具函数 → 单元测试
- Core: EventBus/Config/Log 等 → 单元测试
- Engine: 策略/回测/费用 → 单元测试 + 集成测试
- UI: 手动验证

## 运行测试
```bash
ctest --preset default
```
