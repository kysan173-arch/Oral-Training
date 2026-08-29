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
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\005_learner_insights.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\006_training_experience.sql
   & $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\007_supervisor_growth.sql
   ```

   `003` 会完整归档历史重复轮次后建立唯一索引，回填回复状态，并为已有 `generating` 记录补任务；迁移本身不会调用模型。`004` 保留所有旧记录并归属到 `demo-user-001`。

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
ALLOW_RUNTIME_API_KEY=false
AI_WORKER_CONCURRENCY=1
```

TLS 在反向代理终止，代理必须覆盖并传递 `X-Forwarded-Proto: https`。生产模式拒绝通配 CORS，且即使误配也会关闭页面上传模型密钥。Worker 并发默认 1、最大 4。

## 发布步骤

1. 备份数据库。
2. 对生产库只读执行 `migrations/preflight_reliability.sql`，并在副本或测试库运行 `tests/migration_reliability.ps1`。
3. 停服，对生产库按顺序执行迁移。
4. 部署新程序并检查 `/api/health` 的 `database`、`workerRunning`、`pendingJobs`、`deadJobs`。
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

迁移测试要求一次性数据库名包含 `test` 或 `ci`：

```powershell
.\tests\migration_reliability.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
```

在同一测试库中，设置 `ORAL_TRAINING_TEST_DATABASE_URL` 后运行
`build-msvc\Release\database_feature_test.exe`，可验证提示、签到、收藏、主管聚合和成员摘要。

并发测试会进行一次受控患者模型调用并在测试后删除精确会话：

```powershell
.\tests\concurrency.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
```

接口和错误码见 `../docs/api.md`。
