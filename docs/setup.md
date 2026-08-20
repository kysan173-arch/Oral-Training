# 本地运行与测试部署文档

本文档指导团队成员在各自 Windows 电脑上把「口腔客服智能陪练」小程序跑起来，包括后端、数据库和微信开发者工具三个部分。

> 仅用于开发测试。请勿提交真实密钥到仓库，也不要在演示数据中输入真实患者隐私信息。

---

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

3. 编辑 `backend.env`，至少修改数据库密码（也可填 DeepSeek Key）：

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
```

> 没有 DeepSeek Key 时也可以先启动并做大部分界面测试，但「开始训练/生成报告/患者模拟」这类依赖模型的功能需要有效 Key。

---

## 6. 初始化数据库（执行迁移）

在 `backend/` 目录下，按顺序执行全部迁移。当前已到 `009`：

```powershell
$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
$env:PGCLIENTENCODING='UTF8'

& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\001_initial.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\002_roleplay.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\003_reliability.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\004_identity.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\005_learner_insights.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\006_training_experience.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\007_supervisor_growth.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\008_custom_patient_profile.sql
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\009_recommendation_scenario.sql
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
1. 自动从 `backend.env` 加载环境变量（不存在则创建模板并提示你填写）。
2. 检查并启动 PostgreSQL 服务。
3. 检查 exe（若不存在会从 `build-msvc\Release` 复制）。
4. 检查 `libpq.dll` 是否在 `backend` 目录。
5. 启动后端，监听 `http://127.0.0.1:8080/api`。

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
```

> 迁移/并发测试要求一次性测试库（库名须含 `test` 或 `ci`），不要对正式 `oral_training` 库直接运行，以免清理数据。

---

## 12. 安全提醒

- 不要把 `backend.env`、`run-backend.bat`（内含真实密钥）、真实数据库密码、DeepSeek Key、bearer token 提交到仓库或发给无关成员。
- 演示环境禁止输入真实患者姓名、电话、病历等隐私信息。
- 上线请走 HTTPS 反向代理，并将 `AUTH_MODE` 改为 `wechat`、`PRODUCTION=true`、`ALLOW_RUNTIME_API_KEY=false`。
