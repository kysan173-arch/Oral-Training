# Oral Training Backend

单个 Windows x64 程序同时运行 Crow API 和可恢复 AI Worker。PostgreSQL 保存用户隔离的会话、消息租约、评分/复盘和任务尝试；DeepSeek 网关保持既有请求、Prompt、解析及调用内重试逻辑。

## 本地启动

1. 设置环境变量（程序不会自动加载 `.env`）。
2. 备份已有数据库，并按顺序执行迁移：

   ```powershell
   $psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\001_initial.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\002_roleplay.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\003_reliability.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\004_identity.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\005_pair_and_state_repair.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\006_learner_insights.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\007_training_experience.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\008_supervisor_growth.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\009_legacy_report_totals.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\010_knowledge_catalog.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\011_roleplay_rag_mvp.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\012_custom_patient_profile.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\013_recommendation_scenario.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\014_training_plans.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\015_supervisor_team.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\016_message_emotion.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\017_hint_per_round.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\018_scenario_reaction_rules.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\019_roleplay_free_template.sql
   ```

   `003` 会完整归档历史重复轮次后建立唯一索引，回填回复状态，并为已有 `generating` 记录补任务。`004` 保留所有旧记录并归属到 `demo-user-001`。`005` 按“最新回复 + 其之前最近一次输入”修复被拆开的历史问答，并补建完成会话缺失的报告或任务；被替换的消息、报告和任务状态都会归档。迁移本身不会调用模型，执行 `005` 至 `019` 期间必须保持后端停止，全部迁移完成后再启动。`010` 增加服务、知识、不可变版本、发布审计及独立草稿生成队列；`011` 增加角色互换 RAG 快照、证据 trace 和消息引用。`012` 至 `019` 依次为自定义患者画像、推荐场景、培训计划与指派、主管团队归属、消息情绪、轮次内提示唯一键、场景反应规则与自由模拟模板。

3. 构建并启动：

   ```powershell
   cmake -S . -B build-msvc -G 'Visual Studio 17 2022' -A x64
   cmake --build build-msvc --config Release
   $env:PATH='C:\Program Files\PostgreSQL\18\bin;' + $env:PATH
   .\build-msvc\Release\oral_training_backend.exe
   ```

## 环境变量

参见 `.env.example`。本机默认 `AUTH_MODE=demo`，小程序仍通过 `/auth/wechat` 取得服务端令牌，但不会访问微信接口。单机构生产配置至少应包含：

```dotenv
PRODUCTION=true
AUTH_MODE=wechat
WECHAT_APP_ID=<appid>
WECHAT_APP_SECRET=<secret>
ALLOWED_ORIGIN=https://your-mini-program-gateway.example
REQUIRE_HTTPS=true
TRUSTED_PROXY_IPS=127.0.0.1,::1
ALLOW_RUNTIME_API_KEY=false
AI_WORKER_CONCURRENCY=1
KNOWLEDGE_WORKER_CONCURRENCY=1
DATABASE_POOL_SIZE=12
DATABASE_POOL_WAIT_MS=3000
```

TLS 在反向代理终止。`TRUSTED_PROXY_IPS` 是以逗号分隔的精确代理 IP 列表，必须包含实际连接后端的每一层可信代理；程序只接受这些代理提供的 `X-Forwarded-Proto`，并从 `X-Forwarded-For` 右侧逐层剥离可信代理后确定限流客户端。代理应覆盖协议头并正确追加或覆盖客户端地址头。

生产模式要求微信登录、HTTPS、HTTPS Origin 和非空可信代理列表。布尔值只接受 `true/false`、`1/0`、`yes/no`、`on/off`（忽略大小写），整数必须完整合法；任何无效或降级配置都会让程序拒绝启动。学员报告 Worker 并发默认 1、最大 4，知识草稿 Worker 由 `KNOWLEDGE_WORKER_CONCURRENCY` 控制，默认 1、最大 2。API、身份服务和两个 Worker 池共享惰性连接池，默认最多 12 个连接、最长等待 3 秒；连接池大小必须至少为两个 Worker 并发数之和加 2，池耗尽时 API 返回 503 `DATABASE_BUSY`。

未配置模型密钥时健康检查返回 503，直到通过环境变量或仅限本机的运行时配置入口完成设置；小程序仍能读取该 503 响应并显示本地密钥配置入口。

## 发布步骤

1. 备份数据库。
2. 对生产库只读执行 `migrations/preflight_reliability.sql`，并在副本或测试库运行 `tests/migration_reliability.ps1`。
3. 停服，对生产库按顺序执行迁移。
4. 部署新程序并确认 `/api/health` 返回 HTTP 200，且 `ready`、`database`、`workerRunning` 为 true，`workersInDatabaseBackoff` 为 0；同时检查任务和连接池计数。
5. 先运行无模型烟测；其他检查通过后，只运行一次受控 `smoke.ps1 -WithModel`。

第一阶段验收完成前仅在本机或受控局域网使用。

## 验证

```powershell
ctest --test-dir build-msvc -C Release --output-on-failure
.\tests\static_checks.ps1
.\tests\smoke.ps1
```

在未配置模型的临时测试后端上，可验证幂等、租约、结束/重试和 abandoned 状态：

```powershell
.\tests\state_machine.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
```

知识目录、存储与管理 API 使用一次性 schema 验证，不会清理未核对范围的数据库：

```powershell
.\tests\knowledge_catalog_migration.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
.\tests\knowledge_store_database.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
.\tests\knowledge_admin_api.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
```

迁移测试要求一次性数据库名包含 `test` 或 `ci`：

```powershell
.\tests\migration_reliability.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
```

在同一测试库中，设置 `ORAL_TRAINING_TEST_DATABASE_URL` 后运行
`build-msvc\Release\database_feature_test.exe`，可验证提示、签到、收藏、主管聚合看板与成员摘要。

并发测试会进行一次受控患者模型调用并在测试后删除精确会话：

```powershell
.\tests\concurrency.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
```

接口和错误码见 `../docs/api.md`。
