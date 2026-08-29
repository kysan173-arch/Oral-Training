# 口腔客服智能陪练

微信小程序 + Windows C++ 后端，提供两条独立训练链路：

- 学员扮演客服，与 DeepSeek 模拟患者多轮对话，完成后生成五维报告。
- 学员扮演患者，查看标准客服示范、学习要点和无分数复盘。

当前版本已具备可靠消息幂等、可恢复 AI Worker、微信登录、单机构 `learner/admin` 权限、用户数据隔离、场景分类、合规训练提示、话术收藏、每日签到积分、主管聚合和成员学习摘要。积分只有每日签到来源；多机构租户、排行榜、兑换和团队任务运营不在本轮范围内。

> 仅用于模拟训练，不构成医疗建议。请勿输入真实患者姓名、电话、病历或其他隐私信息。

## 目录

```text
app.* / pages/ / utils/          微信小程序
backend/src/main.cpp             API、模型网关、Worker 入口
backend/src/reliable_store.h     消息租约、任务队列、事务状态机
backend/src/identity.h           登录、角色、限流
backend/migrations/              PostgreSQL 有序迁移
backend/tests/                   静态、迁移、并发和烟测
docs/api.md                      公共 API 契约
```

## 初始化数据库

先备份历史数据库，并只读执行 `backend/migrations/preflight_reliability.sql` 记录重复轮次和异常状态。按顺序执行全部迁移；`003` 会归档重复消息并补可靠任务，迁移过程不会调用模型，`004` 会把历史记录保留在演示用户下。

```powershell
$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\001_initial.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\002_roleplay.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\003_reliability.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\004_identity.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\005_learner_insights.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\006_training_experience.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\007_supervisor_growth.sql
```

## 构建与启动后端

需要 Windows 10/11、Visual Studio 2022 C++ 工作负载、CMake 3.20+ 和 PostgreSQL 客户端库。

```powershell
cmake -S backend -B backend\build-msvc -G 'Visual Studio 17 2022' -A x64 `
  -DPostgreSQL_ROOT='C:/Program Files/PostgreSQL/18'
cmake --build backend\build-msvc --config Release

$env:DATABASE_URL='postgresql://oral_training_app:<password>@127.0.0.1:5432/oral_training'
$env:DEEPSEEK_API_KEY='<DeepSeek API Key>'
$env:AUTH_MODE='demo'
$env:ALLOW_RUNTIME_API_KEY='false'
$env:PATH='C:\Program Files\PostgreSQL\18\bin;' + $env:PATH
& '.\backend\build-msvc\Release\oral_training_backend.exe'
```

本机默认监听 `http://127.0.0.1:8080/api`。单个程序同时运行 API 和可靠 Worker，默认 Worker 并发 1。

完整环境变量见 [backend/.env.example](backend/.env.example)，后端说明见 [backend/README.md](backend/README.md)。旧的预发布二进制不包含本轮迁移、身份和 Worker 修复，部署当前代码时必须重新构建或重新打包。

## 微信小程序

1. 在微信开发者工具中导入仓库根目录。
2. 保持后端运行并点击“编译”。
3. 开发环境默认访问 `http://127.0.0.1:8080/api`。
4. 体验版/正式版通过扩展配置或 `utils/config.js` 设置 HTTPS `apiBaseUrl`，并加入微信请求域名白名单。

前端启动时调用 `wx.login`，然后通过 `/api/auth/wechat` 获取服务端令牌。`AUTH_MODE=demo` 时该路径登录保留的演示用户；生产必须使用 `AUTH_MODE=wechat`。

主管账号由受控的数据库运维流程把已验证用户设为 `admin`；小程序不提供任何自助提权入口。测试库保留了带 `Test` 前缀的主管和学员样本，便于查看主管聚合与成员详情。

## 生产最小配置

```dotenv
PRODUCTION=true
AUTH_MODE=wechat
WECHAT_APP_ID=<appid>
WECHAT_APP_SECRET=<secret>
ALLOW_RUNTIME_API_KEY=false
ALLOWED_ORIGIN=https://your-gateway.example
REQUIRE_HTTPS=true
AI_WORKER_CONCURRENCY=1
```

后端应放在 HTTPS 反向代理之后，代理传递 `X-Forwarded-Proto: https`。生产模式拒绝通配 CORS并自动禁用首页运行时密钥上传。不要把数据库、模型密钥、微信密钥或 bearer token 写进前端或仓库。

## 验证

```powershell
cmake --build backend\build-msvc --config Release
ctest --test-dir backend\build-msvc -C Release --output-on-failure
& '.\backend\tests\static_checks.ps1'
```

后端和已迁移数据库运行时执行无模型 API 烟测：

```powershell
& '.\backend\tests\smoke.ps1'
```

迁移和并发测试只允许一次性数据库（数据库名必须包含 `test` 或 `ci`）：

```powershell
& '.\backend\tests\migration_reliability.ps1' -DatabaseUrl 'postgresql://.../oral_training_test'
& '.\backend\tests\concurrency.ps1' -DatabaseUrl 'postgresql://.../oral_training_test'
```

在已迁移的测试库中，可额外验证训练提示、签到幂等、话术收藏、主管聚合与成员详情：

```powershell
$env:ORAL_TRAINING_TEST_DATABASE_URL = 'postgresql://.../oral_training_test'
& '.\backend\build-msvc\Release\database_feature_test.exe'
```

所有离线和无模型检查通过后，只运行一次受控真实模型烟测：

```powershell
& '.\backend\tests\smoke.ps1' -WithModel
```

最后在微信开发者工具手测：快速切换两种模式、断网恢复、回复 pending、30 秒结果页操作、续练、历史展开和数据页状态。

接口、状态机和错误码见 [docs/api.md](docs/api.md)。
