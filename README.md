# 本地运行与测试部署文档

本文档指导团队成员在各自 Windows 电脑上把「口腔客服智能陪练」小程序跑起来，包括后端、数据库和微信开发者工具三个部分。

> 仅用于开发测试。请勿提交真实密钥到仓库，也不要在演示数据中输入真实患者隐私信息。

当前版本已具备可靠消息幂等、可恢复 AI Worker、微信登录、单机构 `learner/admin` 权限、用户数据隔离、服务/知识管理，以及角色互换模式的服务选择、会话版本快照、中文检索和回答依据展示。积分只有每日签到来源；多机构租户、排行榜、兑换和团队任务运营不在本轮范围内。

## 1. 运行架构

```
微信开发者工具（小程序前端）
        │  HTTP
        ▼
Crow API  ──▶  DeepSeek（模拟患者 / 评分）
   │
   └──▶  PostgreSQL（会话、消息、评分、签到、收藏等）
```

- 单个 Windows 程序 `oral_training_backend.exe` 同时运行 API 和后台 AI Worker。
- 本机默认监听 `http://127.0.0.1:8080/api`。
- 开发模式 `AUTH_MODE=demo`：小程序仍调用 `/api/auth/wechat`，但后端不真正访问微信，**无需配置微信 AppID/Secret 也能登录测试**。

---

## 2. 环境要求

| 软件 | 版本 | 说明 |
|---|---|---|
| Windows | 10/11 | 64 位 |
| Visual Studio | 2022 | 勾选「使用 C++ 的桌面开发」工作负载 |
| CMake | 3.20+ | 构建后端 |
| PostgreSQL | 16 或 18 | 建议 18（与当前环境一致） |
| PostgreSQL 客户端库 | 与 PostgreSQL 同版本 | 构建时需要，`libpq` |
| 微信开发者工具 | 最新稳定版 | 运行小程序前端 |
| DeepSeek API Key | 有效 | 可选，仅真实模型功能需要 |

---

## 3. 安装并配置 PostgreSQL

### 3.1 安装

1. 下载并安装 PostgreSQL（建议 18），安装时记住管理员密码。
2. 确认安装目录，例如 `C:\Program Files\PostgreSQL\18\`。
3. 安装完成后，把 `C:\Program Files\PostgreSQL\18\bin` 加入系统 `PATH`（或在使用时用绝对路径调用 `psql.exe`）。

先备份历史数据库，并只读执行 `backend/migrations/preflight_reliability.sql` 记录重复轮次和异常状态。按顺序执行全部迁移；迁移过程不会调用模型。执行 `005` 至 `019` 期间必须保持后端停止，全部迁移完成后再启动。`010` 增加服务与知识目录，`011` 增加角色互换 RAG 会话快照、证据 trace 与引用字段。

### 3.2 创建数据库与用户

打开 PowerShell，使用 PostgreSQL 超级用户（通常是 `postgres`）执行：

```powershell
$env:PGCLIENTENCODING='UTF8'
& 'C:\Program Files\PostgreSQL\18\bin\psql.exe' -U postgres -h 127.0.0.1

-- 以下在 psql 交互内执行
CREATE USER oral_training_app WITH PASSWORD 'your_db_password';
CREATE DATABASE oral_training OWNER oral_training_app ENCODING 'UTF8';
GRANT ALL PRIVILEGES ON DATABASE oral_training TO oral_training_app;
\q

$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\001_initial.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\002_roleplay.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\003_reliability.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\004_identity.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\005_pair_and_state_repair.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\006_learner_insights.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\007_training_experience.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\008_supervisor_growth.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\009_legacy_report_totals.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\010_knowledge_catalog.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\011_roleplay_rag_mvp.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\012_custom_patient_profile.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\013_recommendation_scenario.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\014_training_plans.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\015_supervisor_team.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\016_message_emotion.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\017_hint_per_round.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\018_scenario_reaction_rules.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f backend\migrations\019_roleplay_free_template.sql
```

> 生产或共享环境请使用更安全的密码；本地测试可用 `oral_training_pass`。请不要把真实密码提交到仓库。

---

## 4. 获取代码

把仓库克隆或复制到本机，例如：

```powershell
git clone <仓库地址> oral-training
cd oral-training
```

仓库根目录下应包含 `app.*`、`pages/`、`backend/`、`docs/` 等。

---

## 5. 配置后端环境变量

后端**不会自动读取 `.env`**。项目提供了 `backend.env` 模板，启动脚本会自动从它加载环境变量。

1. 进入 `backend/` 目录。
2. 复制模板：

```powershell
cd backend
Copy-Item backend.env.example backend.env
```

3. 编辑 `backend.env`，至少修改数据库密码。**DeepSeek API Key 也在这里配置**——小程序前端已移除密钥输入框，请把 `DEEPSEEK_API_KEY=` 后面的占位值替换为你的真实 Key：

```dotenv
DATABASE_URL=postgresql://oral_training_app:your_db_password@127.0.0.1:5432/oral_training
DEEPSEEK_API_KEY=your_deepseek_key_optional
DEEPSEEK_MODEL=deepseek-v4-flash
PRODUCTION=false
AUTH_MODE=demo
AUTH_TOKEN_TTL_SECONDS=604800
ALLOW_RUNTIME_API_KEY=true
BIND_ADDRESS=127.0.0.1
PORT=8080
ALLOWED_ORIGIN=*
REQUIRE_HTTPS=false
RATE_LIMIT_PER_MINUTE=120
AI_WORKER_CONCURRENCY=1
KNOWLEDGE_WORKER_CONCURRENCY=1
DATABASE_POOL_SIZE=12
DATABASE_POOL_WAIT_MS=3000
```

普通 API 请求超时保持 30 秒；两类逐轮模型消息接口单独使用 120 秒超时，以覆盖后端两次模型尝试。若请求仍中断，页面会按原 `clientMessageId` 查询并保留输入，不会重复计轮。报告或复盘收到 `not_started` 时会根据会话状态恢复任务、返回未完成会话或回到历史记录；页面链接缺少 `sessionId` 时会明确提示并安全导航。

主管账号由受控的数据库运维流程把已验证用户设为 `admin`；小程序不提供任何自助提权入口。测试库保留了带 `Test` 前缀的主管和学员样本，便于查看主管聚合看板。

## 生产最小配置

```dotenv
PRODUCTION=true
AUTH_MODE=wechat
WECHAT_APP_ID=<appid>
WECHAT_APP_SECRET=<secret>
ALLOW_RUNTIME_API_KEY=false
ALLOWED_ORIGIN=https://your-gateway.example
REQUIRE_HTTPS=true
TRUSTED_PROXY_IPS=127.0.0.1,::1
AI_WORKER_CONCURRENCY=1
KNOWLEDGE_WORKER_CONCURRENCY=1
DATABASE_POOL_SIZE=12
DATABASE_POOL_WAIT_MS=3000
```

后端应放在 HTTPS 反向代理之后。`TRUSTED_PROXY_IPS` 必须填写实际连接后端的代理 IP；只有这些地址提供的 `X-Forwarded-For` 和 `X-Forwarded-Proto` 会被信任。代理应覆盖 `X-Forwarded-Proto`，并正确追加或覆盖 `X-Forwarded-For`。生产配置缺失、布尔值/整数拼写错误、使用 demo 登录或关闭 HTTPS 时，程序会拒绝启动。不要把数据库、模型密钥、微信密钥或 bearer token 写进前端或仓库。

API、身份服务、报告 Worker 和独立的知识草稿 Worker 共享惰性数据库连接池。连接总数受 `DATABASE_POOL_SIZE` 限制；等待超过 `DATABASE_POOL_WAIT_MS` 的请求返回 HTTP 503 `DATABASE_BUSY`。连接池大小必须至少比两个 Worker 池的并发数之和多 2，避免后台任务占满 API 所需连接。

> **如何配置 DeepSeek API Key**：小程序前端不再提供密钥输入框，请统一在 `backend/backend.env` 的 `DEEPSEEK_API_KEY` 中填写（见上面第 5 节步骤 3），保存后重启后端即可生效。没有 Key 时服务也能启动、也能做界面测试，但「开始训练 / 生成报告 / 患者模拟」这类依赖模型的功能不可用（健康检查会返回 503）。

---

## 6. 初始化数据库（执行迁移）

在 `backend/` 目录下，按顺序执行全部迁移。当前已到 `019`：

```powershell
$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
$env:PGCLIENTENCODING='UTF8'

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

说明：
- `DATABASE_URL` 需先在当前 PowerShell 会话设置（或先 `cd backend` 后用 `start-backend.ps1` 方式加载）。
- `003` 会归档重复消息并补可靠任务，`004` 保留历史记录，均不调用模型。
- 迁移可重复执行（幂等）。

---

## 7. 构建后端

需要 Visual Studio 2022 和 CMake，并确保能找到 PostgreSQL 客户端库：

```powershell
cd backend
cmake -S . -B build-msvc -G 'Visual Studio 17 2022' -A x64 `
  -DPostgreSQL_ROOT='C:/Program Files/PostgreSQL/18'
cmake --build build-msvc --config Release
```

构建产物为 `backend\build-msvc\Release\oral_training_backend.exe`。

> 如果 CMake 找不到 PostgreSQL，请确认 `PostgreSQL_ROOT` 指向真实安装目录，并把 `C:\Program Files\PostgreSQL\18\bin` 加入 `PATH`。

---

## 8. 启动后端

### 方式 A：使用启动脚本（推荐）

```powershell
cd backend
.\start-backend.cmd
```

脚本会：
1. 检查 `backend.env`（不存在则从模板创建并提示你填写）。
2. 把 `backend.env` 加载到进程环境 —— 后端**只从环境变量读配置**（`getenv`），不解析该文件，所以别直接双击 exe。
3. 检查并启动 PostgreSQL 服务。
4. 停掉占用端口或同名的旧后端进程 —— 必须在替换 exe **之前**做，否则 Windows 会锁住正在运行的 exe。
5. 用 `build-msvc\Release\oral_training_backend.exe` 覆盖 `backend\oral_training_backend.exe`（**每次启动都覆盖**，避免重建后仍在跑旧二进制），并打印该 exe 的时间戳供核对。
6. 检查 `libpq.dll` 是否在 `backend` 目录。
7. 启动后端，监听 `http://127.0.0.1:8080/api`。

### 方式 B：手动启动

```powershell
cd backend
$env:DATABASE_URL='postgresql://oral_training_app:your_db_password@127.0.0.1:5432/oral_training'
$env:DEEPSEEK_API_KEY='your_deepseek_key_optional'
$env:PRODUCTION='false'
$env:AUTH_MODE='demo'
$env:ALLOW_RUNTIME_API_KEY='true'
$env:BIND_ADDRESS='127.0.0.1'
$env:PORT='8080'
$env:ALLOWED_ORIGIN='*'
$env:REQUIRE_HTTPS='false'
$env:AI_WORKER_CONCURRENCY='1'
$env:PATH='C:\Program Files\PostgreSQL\18\bin;' + $env:PATH
.\build-msvc\Release\oral_training_backend.exe
```

### 健康检查

后端启动后，浏览器或 curl 访问：

```
http://127.0.0.1:8080/api/health
```

应返回 `database`、`workerRunning`、`pendingJobs`、`deadJobs` 等字段，确认数据库连接和 Worker 正常。

---

## 9. 运行小程序前端

1. 打开微信开发者工具，**导入项目**，选择仓库根目录（不是 `backend/`）。
2. AppID 已配置（`project.config.json` 中），可直接用测试号或绑定账号。
3. 保持后端运行，点击「编译」。

开发环境默认通过 `utils/config.js` 访问 `http://127.0.0.1:8080/api`，无需额外配置。

### 测试账号

测试库自带带 `Test` 前缀的主管和学员样本用户，方便验证主管聚合、成员详情等。`AUTH_MODE=demo` 下首次进入会自动使用演示用户。

- 学员视图：首页、训练、结果、我的、历史、话术、错题、成长。
- 主管视图：「数据」Tab 查看团队聚合与成员摘要，可在「我的」切换身份。

---

## 10. 常见问题排查

| 现象 | 可能原因与处理 |
|---|---|
| 后端启动失败，报数据库连接错误 | 检查 `backend.env` 的 `DATABASE_URL` 密码/库名是否正确，PostgreSQL 是否已启动 |
| `libpq.dll` 缺失 | 确认 PostgreSQL 客户端库已安装，并把对应 `bin` 下 DLL 复制到 `backend` 目录或加入 `PATH` |
| CMake 找不到 PostgreSQL | 检查 `PostgreSQL_ROOT` 路径，安装「开发」组件，把 `bin` 加入 `PATH` |
| 小程序请求报「URL 不在白名单」| 开发模式在微信开发者工具勾选「不校验合法域名」，并确认请求的是 `127.0.0.1:8080` |
| 训练/生成报告卡住 | 多为未配置有效 `DEEPSEEK_API_KEY` 或模型不可达；先看后端终端日志 |
| 迁移执行报编码错误 | 执行迁移前先设置 `$env:PGCLIENTENCODING='UTF8'` |
| 改了后端 C++ 代码不生效 | 需要重新 `cmake --build`，并把新的 exe 复制到 `backend` 根目录再启动 |

---

## 11. 验证测试（可选）

```powershell
cd backend
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
.\tests\static_checks.ps1
.\tests\smoke.ps1            # 无模型 API 烟测

# 迁移链路与并发回归（要求一次性测试库，库名须含 test 或 ci）
.\tests\migration_reliability.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
.\tests\session_concurrency.ps1  -DatabaseUrl 'postgresql://.../oral_training_test'
.\tests\concurrency.ps1          -DatabaseUrl 'postgresql://.../oral_training_test'
```

在已迁移的测试库中，可额外验证训练提示、签到幂等、话术收藏、主管聚合看板与成员摘要：

```powershell
$env:ORAL_TRAINING_TEST_DATABASE_URL = 'postgresql://.../oral_training_test'
& '.\backend\build-msvc\Release\database_feature_test.exe'
```

所有离线和无模型检查通过后，只运行一次受控真实模型烟测：

```powershell
.\tests\smoke.ps1 -WithModel
```

> 迁移/并发测试要求一次性测试库（库名须含 `test` 或 `ci`），不要对正式 `oral_training` 库直接运行，以免清理数据。

---

## 12. 安全提醒

- 不要把 `backend.env`、`run-backend.bat`（内含真实密钥）、真实数据库密码、DeepSeek Key、bearer token 提交到仓库或发给无关成员。
- 演示环境禁止输入真实患者姓名、电话、病历等隐私信息。
- 上线请走 HTTPS 反向代理，并将 `AUTH_MODE` 改为 `wechat`、`PRODUCTION=true`、`ALLOW_RUNTIME_API_KEY=false`。
