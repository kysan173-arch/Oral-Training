# 口腔客服智能陪练 · Oral Training

微信小程序与 C++ 后端，支持客服训练、患者模拟、基于服务知识的 RAG、历史恢复、评分复盘及主管聚合看板。后端支持 Windows 和 Linux，PostgreSQL 保存用户隔离的数据、不可变资料版本和可恢复 AI 任务。

## 配置教程

首次运行请从 [RAG 本地配置与数据初始化教程](docs/setup/RAG本地配置与数据初始化教程.md) 开始，按顺序完成数据库迁移、构建、模型服务、后端启动和测试数据导入。

| 文档 | 内容 |
|---|---|
| [完整本地配置教程](docs/setup/RAG本地配置与数据初始化教程.md) | Windows/Linux、备份与迁移、个人模型配置、完整测试数据、两种训练验收 |
| [EmbeddingGemma 2 配置教程](docs/setup/embeddinggemma-setup.md) | 已有服务检查、新机器安装、文本 API、向量验证、缓存版本及跨机连接 |
| [完整手动测试知识库清单](docs/validation/manual-knowledge-checklist.md) | 服务与场景、逐项提问和预期、发布时效、资料冲突、版本快照 |
| [后端开发说明](backend/README.md) | 构建、集成测试、可靠 Worker、个人密钥存储和部署 |
| [API 文档](docs/architecture/api.md) | 请求、响应、鉴权和错误码 |
| [文档导航](docs/README.md) | 产品规划、配置教程、技术设计、训练规范、验证记录与成果汇报 |

## 运行架构

```text
微信小程序 → Crow API + Worker → PostgreSQL（默认 5432）
              默认 8080     ├→ EmbeddingGemma 2（默认 8765，语义检索）
                           └→ 每个账号的生成模型网关（对话、评分、复盘）
```

生成模型和检索模型分别配置：

- 生成模型：小程序「我的 → LiteLLM 模型配置」，按账号保存 Base URL、模型别名、Key。
- 检索模型：后端 `RAG_EMBEDDING_*`，默认 `http://127.0.0.1:8765/v1`、`embeddinggemma-2`、768 维。
- 数据库：按统一入口迁移到 `036_embeddinggemma_cache.sql`；向量缓存不要求 pgvector。

公共 `/api/health` 返回 `ready=true` 不代表两个模型均可用。请按教程分别测试向量接口和当前账号的生成模型。旧 `DEEPSEEK_API_KEY`、`DEEPSEEK_MODEL`、`ALLOW_RUNTIME_API_KEY` 和三个 `RAG_*_ENABLED` 开关已停用。

## 本地数据与验收

完成配置并启动后端与 Embedding 后，可先预检再导入完整合成测试数据：

```bash
python3 -B backend/seed_local_test_data.py --database oral_training
python3 -B backend/seed_local_test_data.py --database oral_training --apply
```

脚本只接受回环数据库、精确匹配的数据库名和非生产 demo 后端，不清库，可重复执行。它提供测试主管/学员、已发布及草稿服务、知识、两种训练历史、报告状态、引用、计划和学习记录。会调用本地 Embedding，不调用生成模型。Windows 使用 `python`，环境要求见完整教程。

导入后从「我的 → 切换演示账号」选择测试账号；新账号实时训练前需配置自己的生成模型。所有价格、知识、对话和报告均为虚构测试资料。

完整手测还需运行知识扩展：先执行 `python3 -B backend/prepare_manual_knowledge.py --database oral_training` 预检，再加 `--apply` 发布补充知识。详细问题与预期见上方手测清单；新增知识从新会话生效。

## 开发验证

Linux：

```bash
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build -j 2
ctest --test-dir backend/build --output-on-failure
python3 -B backend/tests/litellm_integration_test.py --pg-bin /usr/lib/postgresql/16/bin
python3 -B backend/tests/run_embedding_checks.py
```

Windows：

```powershell
cmake -S backend -B backend/build-msvc -G 'Visual Studio 17 2022' -A x64
cmake --build backend/build-msvc --config Release
ctest --test-dir backend/build-msvc -C Release --output-on-failure
& backend/tests/static_checks.ps1
```

隔离数据库集成测试使用临时 PostgreSQL 和模拟网关，不触碰现有业务库、不调用真实生成模型。真实模型验收应在离线检查通过后设置预算并受控执行，参见后端说明。

本地配置、密码、API Key、主密钥、数据库备份和构建输出不要提交。生产需微信鉴权、HTTPS、精确 CORS、可信代理和审核后的资料；学员数据按用户隔离，主管只取得规定范围的聚合信息。多机构租户、排行榜、兑换与团队任务运营不在当前范围内。
