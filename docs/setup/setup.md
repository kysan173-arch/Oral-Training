# 本地配置教程入口

完整步骤统一维护在 [RAG 本地配置与数据初始化教程](RAG本地配置与数据初始化教程.md)，适用于 Windows 与 Linux 后端，包含数据库迁移、构建、模型配置、完整测试数据、两种训练验收和故障排查。

只需配置检索模型时，直接阅读 [EmbeddingGemma 2 配置教程](embeddinggemma-setup.md)。该文包含已有服务检查、新机器安装、可运行的文本 API 示例及后端参数表。

| 用途 | 配置位置 | 示例 |
|---|---|---|
| 生成患者台词、客服回复和报告 | 小程序「我的 → LiteLLM 模型配置」，按账号保存 | 自己的网关 Base URL、模型别名、Key |
| 知识向量化与语义检索 | 后端 `RAG_EMBEDDING_*` 环境变量 | `http://127.0.0.1:8765/v1`、`embeddinggemma-2` |

旧教程中的 `DEEPSEEK_API_KEY`、`DEEPSEEK_MODEL`、`ALLOW_RUNTIME_API_KEY` 和三个 `RAG_*_ENABLED` 开关已不生效。公共健康检查的 `modelConfigured=null` 表示模型配置属于个人账号，不代表所有用户都没配模型。

开发与离线回归命令见 [后端说明](../../backend/README.md)，请求格式见 [API 文档](../architecture/api.md)。
