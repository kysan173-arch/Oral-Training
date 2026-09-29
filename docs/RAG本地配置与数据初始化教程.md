# RAG 本地配置与数据初始化教程

本文说明如何在 Windows 本机从零配置 Oral Training 的最小可用 RAG 环境，包括 PostgreSQL、数据库迁移、DeepSeek、后端构建、演示服务和知识初始化，以及微信小程序中的实际验证。

当前代码已接入两种模式：患者模拟使用有依据的 AI 客服；客服训练使用 AI 患者初始化、固定知识版本核验和 v2 报告。N07 新建开关默认关闭；已有 v2 会话继续按原快照运行。人工标注复核、DevTools 和真实模型验收未完成前，不等同正式发布。

## 1. 环境要求

建议使用以下环境：

- Windows 10 或 Windows 11；
- Visual Studio 2022，安装“使用 C++ 的桌面开发”；
- CMake；
- PostgreSQL 18，包括 `psql.exe`、`pg_dump.exe` 和客户端开发库；
- 微信开发者工具；
- 可用的 DeepSeek API Key。

本文假定：

```text
仓库目录：D:\myproject\Oral-Training-codex-mvp
PostgreSQL：C:\Program Files\PostgreSQL\18
数据库地址：127.0.0.1:5432
后端地址：http://127.0.0.1:8080/api
```

如果本机目录不同，请替换命令中的对应路径。

## 2. 凭据安全

不要把数据库密码、DeepSeek Key、Bearer Token 或本地 `.env` 文件提交到 Git。

后端目前不会自动读取 `.env.example`。可以在仓库外创建本地配置文件：

```text
C:\Users\<你的用户名>\oral-training-local.env
```

内容示例：

```dotenv
POSTGRES_ADMIN_USER=postgres
POSTGRES_ADMIN_PASSWORD=<PostgreSQL 管理员密码>
ORAL_TRAINING_APP_PASSWORD=<应用数据库用户密码>
DEEPSEEK_API_KEY=<DeepSeek API Key>
```

此文件只用于本地开发，不得复制到仓库。后面的命令也可以直接手工设置环境变量。

## 3. 检查 PostgreSQL

打开 PowerShell：

```powershell
Get-Service postgresql-x64-18
Test-NetConnection 127.0.0.1 -Port 5432
Test-Path 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
```

期望结果：

- PostgreSQL 服务状态为 `Running`；
- 5432 端口测试成功；
- `psql.exe` 存在。

如果服务未运行，以管理员身份执行：

```powershell
Start-Service postgresql-x64-18
```

## 4. 创建数据库和应用角色

先连接管理数据库：

```powershell
$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
& $psql -h 127.0.0.1 -p 5432 -U postgres -d postgres
```

`psql` 会提示输入管理员密码。进入交互终端后，可以检查现有对象：

```sql
SELECT datname FROM pg_database WHERE datname = 'oral_training';
SELECT rolname FROM pg_roles WHERE rolname = 'oral_training_app';
```

仅在对象不存在时创建。把示例密码替换成强密码：

```sql
CREATE ROLE oral_training_app LOGIN PASSWORD '<应用数据库用户密码>';
CREATE DATABASE oral_training OWNER oral_training_app;
\q
```

如果数据库已经存在，不要删除或重建。先执行下一节的备份，再补迁移。

## 5. 升级已有数据库前备份

对已有数据的数据库，先创建完整备份：

```powershell
$env:PGPASSWORD = '<PostgreSQL 管理员密码>'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupDir = 'C:\Users\<你的用户名>\oral-training-backups'
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null

& 'C:\Program Files\PostgreSQL\18\bin\pg_dump.exe' `
  -h 127.0.0.1 -p 5432 -U postgres -d oral_training `
  -Fc -f "$backupDir\oral_training-before-rag-$stamp.dump"

Remove-Item Env:PGPASSWORD
```

确认命令退出码为 0，并检查备份文件不是空文件：

```powershell
Get-ChildItem $backupDir | Sort-Object LastWriteTime -Descending | Select-Object -First 1
```

备份保存在仓库外，不要加入 Git。

## 6. 执行数据库迁移

执行迁移前停止正在运行的后端，避免旧二进制在迁移期间继续写数据库。

在仓库根目录打开 PowerShell：

```powershell
Set-Location 'D:\myproject\Oral-Training-codex-mvp'
$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
$env:PGPASSWORD = '<PostgreSQL 管理员密码>'

Get-ChildItem 'backend\migrations\[0-9][0-9][0-9]_*.sql' |
  Sort-Object Name |
  ForEach-Object {
    Write-Host "Applying $($_.Name)"
    & $psql -h 127.0.0.1 -p 5432 -U postgres `
      -d oral_training -v ON_ERROR_STOP=1 -f $_.FullName
    if ($LASTEXITCODE -ne 0) {
      throw "Migration failed: $($_.Name)"
    }
  }

Remove-Item Env:PGPASSWORD
```

必须按编号执行到：

- `010_knowledge_catalog.sql`：服务目录、知识目录、版本、检索块、管理任务与审计；
- `011_roleplay_rag_mvp.sql`：患者模拟的 RAG 上下文、证据 trace 和版本快照。

`preflight_reliability.sql` 是检查脚本，不属于编号迁移，不要把它当作新 schema 迁移。

### 6.1 授予应用角色权限

如果迁移由 `postgres` 管理员执行，新建表默认属于管理员，需要为应用角色授权：

```powershell
$env:PGPASSWORD = '<PostgreSQL 管理员密码>'

@'
GRANT CONNECT ON DATABASE oral_training TO oral_training_app;
GRANT USAGE ON SCHEMA public TO oral_training_app;
GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO oral_training_app;
GRANT USAGE, SELECT, UPDATE ON ALL SEQUENCES IN SCHEMA public TO oral_training_app;
GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA public TO oral_training_app;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
  GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO oral_training_app;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
  GRANT USAGE, SELECT, UPDATE ON SEQUENCES TO oral_training_app;
ALTER DEFAULT PRIVILEGES IN SCHEMA public
  GRANT EXECUTE ON FUNCTIONS TO oral_training_app;
'@ | & $psql -h 127.0.0.1 -p 5432 -U postgres `
  -d oral_training -v ON_ERROR_STOP=1

Remove-Item Env:PGPASSWORD
```

检查关键表：

```powershell
& $psql -h 127.0.0.1 -p 5432 -U postgres -d oral_training `
  -c "SELECT table_name FROM information_schema.tables WHERE table_schema='public' ORDER BY table_name;"
```

应能看到 `clinic_services`、`service_revisions`、`knowledge_entries`、`knowledge_revisions`、`knowledge_chunks`、`training_contexts` 和 `rag_traces` 等表。

## 7. 编译和测试后端

在仓库根目录执行：

```powershell
cmake -S backend -B backend\build-msvc `
  -G 'Visual Studio 17 2022' -A x64 `
  -DPostgreSQL_ROOT='C:/Program Files/PostgreSQL/18'

cmake --build backend\build-msvc --config Release
```

构建成功后，可执行文件位于：

```text
backend\build-msvc\Release\oral_training_backend.exe
```

执行离线测试：

```powershell
ctest --test-dir backend\build-msvc -C Release --output-on-failure
```

默认没有配置专用测试数据库时，`database_feature` 可能显示 `Skipped`。不要为了运行数据库测试而清理或复用正式开发数据库。

## 8. 设置运行环境并启动后端

数据库密码包含 `@`、`:`、`/`、`#` 等 URI 特殊字符时必须编码。以下命令会自动编码密码：

```powershell
$appPassword = '<应用数据库用户密码>'
$encodedPassword = [Uri]::EscapeDataString($appPassword)

$env:DATABASE_URL = "postgresql://oral_training_app:$encodedPassword@127.0.0.1:5432/oral_training"
$env:DEEPSEEK_API_KEY = '<DeepSeek API Key>'
$env:DEEPSEEK_MODEL = 'deepseek-v4-flash'
$env:RAG_ROLEPLAY_ENABLED = 'false'
$env:RAG_PATIENT_ENABLED = 'false'
$env:RAG_EVALUATION_V2_ENABLED = 'false'
$env:MODEL_CALL_LIMIT = '0'
$env:AUTH_MODE = 'demo'
$env:PRODUCTION = 'false'
$env:ALLOW_RUNTIME_API_KEY = 'false'
$env:BIND_ADDRESS = '127.0.0.1'
$env:PORT = '8080'
$env:DATABASE_POOL_SIZE = '12'
$env:AI_WORKER_CONCURRENCY = '1'
$env:KNOWLEDGE_WORKER_CONCURRENCY = '1'
$env:PATH = 'C:\Program Files\PostgreSQL\18\bin;' + $env:PATH

& '.\backend\build-msvc\Release\oral_training_backend.exe'
```

必须在设置变量的同一个 PowerShell 窗口中启动后端。看到以下信息表示 HTTP 服务已监听：

```text
Oral training API listening at http://127.0.0.1:8080/api
```

不要关闭这个终端。按 `Ctrl+C` 可以停止后端。

## 9. 检查健康状态

另开一个 PowerShell：

```powershell
$health = Invoke-RestMethod 'http://127.0.0.1:8080/api/health'
$health.data | Format-List
```

正常状态至少应满足：

```text
status                          healthy
ready                           True
database                        True
modelConfigured                 True
workerRunning                   True
knowledgeWorkerThreads          1
workersInDatabaseBackoff        0
knowledgeWorkersInDatabaseBackoff 0
```

如果后端监听成功但健康检查返回 HTTP 503，应查看响应正文和后端日志。常见原因包括 DeepSeek Key 缺失、数据库连接失败、应用角色没有新表权限，或 Worker 正在数据库退避。

## 10. 启动微信小程序

使用微信开发者工具打开仓库根目录。开发环境 API 已配置为：

```text
http://127.0.0.1:8080/api
```

配置位置是 `utils/config.js`。本地调试时，如开发者工具阻止本地 HTTP 请求，可以开启“不校验合法域名、web-view、TLS 版本及 HTTPS 证书”。

小程序启动后会调用 `wx.login`，然后通过 `/api/auth/wechat` 获取服务端 Bearer Token。`AUTH_MODE=demo` 时不会访问真实微信登录服务，而是登录保留的 demo 用户。

## 11. 将 demo 用户切换为管理员

知识管理接口只允许 `admin`。本地初始化前，将 demo 用户临时切换为管理员：

```powershell
$env:PGPASSWORD = '<PostgreSQL 管理员密码>'

& $psql -h 127.0.0.1 -p 5432 -U postgres -d oral_training `
  -v ON_ERROR_STOP=1 `
  -c "UPDATE users SET role='admin' WHERE id='demo-user-001' AND status='active';"

Remove-Item Env:PGPASSWORD
```

重新编译或重新进入小程序，使客户端获得包含新角色的新 Token。进入：

```text
我的 → 管理后台 → 知识管理
```

管理员不能使用学员训练接口。数据初始化完成后必须按第 14 节恢复为学员。

## 12. 创建并发布演示服务

在“知识管理”中选择新建服务。推荐使用一套自洽的演示参数，例如：

```text
服务名称：种植牙演示服务
分类：implant
资料来源：synthetic
价格类型：range
最低价格：890000 分
最高价格：1230000 分
价格单位：per_tooth
单次到诊：30～90 minute
完整疗程：3～8 month
早期复诊间隔：10～14 day
```

金额字段使用人民币“分”：

```text
890000 = 8900 元
1230000 = 12300 元
```

可以配置：

- 包含：种植体、基台、牙冠、种植手术、局部麻醉、基础口腔检查；
- 不包含：CBCT、植骨或骨增量、临时修复、术后药品；
- 场景：`implant-basic`、`post-treatment-discomfort`、`price-comparison`；
- 预约：使用 `consultation_hours`，明确具体时段需人工确认；
- `isLiveAvailability`：必须为 `false`，不能把演示数据描述成实时号源。

保存草稿后点击“发布不可变版本”。只有发布后的服务才能被新训练会话选择。发布版本不可原地修改；后续修改会生成新版本。

记下页面或接口返回的服务 ID，格式类似：

```text
svc-xxxxxxxxxxxxxxxxxxxxxxxx
```

## 13. 初始化专业知识

仓库提供测试资料：

```text
docs\rag-data\患者模拟_RAG测试知识库_具体参数版.json
```

该文件包含12条 `synthetic/unverified/demo` 种植牙知识。当前管理页面不支持直接上传 JSON 批量导入，因此有两种方式：

1. 在知识编辑页面逐条复制；
2. 由开发脚本逐条调用管理 API 创建和发布。

每条知识的核心字段是：

```json
{
  "topic": "implant-indications",
  "scope": "service",
  "serviceId": "svc-xxxxxxxxxxxxxxxxxxxxxxxx",
  "title": "种植牙适用情况",
  "body": "知识正文",
  "metadata": {
    "origin": "synthetic",
    "verification": "unverified",
    "sourceTitle": "",
    "sourceUrl": null,
    "sourceLocator": "",
    "applicability": "适用说明",
    "trainingScope": "demo",
    "aliases": ["患者常见问法"]
  }
}
```

导入前检查：

- 所有 `scope=service` 的条目必须使用当前数据库中真实存在的服务 ID；
- 不要直接沿用另一台机器或另一数据库生成的服务 ID；
- `synthetic` 内容只能标记为 `unverified/demo`；
- 模拟内容不得伪造来源标题、章节或 URL；
- 价格、时长、包含项目应与已发布服务事实一致；
- 服务专属的术后知识不应错误设为 `general`。

逐条录入时，先保存草稿，在“检索预览”中输入测试问题，确认能够命中，再点击“发布不可变版本”。发布时系统会生成 `zh-bigram-v1` 检索块。

### 13.1 推荐预览问题

```text
种一颗牙多少钱，包含哪些项目？
我这种情况能不能做种植牙？
做种植牙需要多长时间？
是不是每个人都需要植骨？
种植以后疼痛和出血正常吗？
做完以后多久需要复诊？
```

预览结果应返回 `retrievalStatus=ok`，并至少包含一个 evidence。

### 13.2 数据库验收

```sql
SELECT COUNT(*) AS published_services
FROM clinic_services
WHERE current_revision_id IS NOT NULL;

SELECT COUNT(*) AS published_knowledge
FROM knowledge_entries
WHERE current_revision_id IS NOT NULL;

SELECT COUNT(*) AS knowledge_chunks
FROM knowledge_chunks;
```

使用本文12条测试知识时，正常结果应至少为：

```text
published_services >= 1
published_knowledge >= 12
knowledge_chunks >= 12
```

正文长度超过单块目标时，一条知识会生成多个 chunk，因此 chunk 数通常高于知识条目数。

## 14. 恢复 demo 学员角色

完成管理操作后执行：

```powershell
$env:PGPASSWORD = '<PostgreSQL 管理员密码>'

& $psql -h 127.0.0.1 -p 5432 -U postgres -d oral_training `
  -v ON_ERROR_STOP=1 `
  -c "UPDATE users SET role='learner' WHERE id='demo-user-001' AND status='active';"

Remove-Item Env:PGPASSWORD
```

重新进入小程序，确保获取新的学员 Token。

## 15. 执行一次完整 RAG 验证

在小程序中：

演示新建前须完成文末 N07 的人工门槛与预算配置，并在独占测试后端开启相应开关；关闭状态会返回 RAG_NEW_SESSIONS_PAUSED。

1. 打开训练场景；
2. 切换为“患者模拟”；
3. 选择已发布的“种植牙演示服务”；
4. 选择“种植牙基础咨询”；
5. 开始模拟；
6. 输入“种一颗牙多少钱，费用里包含哪些项目？”；
7. 检查 AI 回复是否保留价格范围、单位、包含项目和附加费用条件；
8. 点击“查看依据”，确认引用来自当前服务版本和种植牙知识；
9. 点击“结束并复盘”；
10. 在“历史记录 → 患者模拟”中检查会话和复盘。

成功回复通常应满足：

- `answerStatus=answered` 或资料不完整时为 `partial`；
- 至少有一个 citation；
- citation 中的 `traceId` 能读取 evidence；
- 不引用其他服务的资料；
- 资料未知时明确说明未知，不自由编造；
- 数据库或检索故障返回可重试错误，而不是伪装成“没有资料”。

## 16. 版本和生效规则

服务与知识发布后只影响新创建的 RAG 会话。会话创建时会锁定：

- 服务 revision；
- `knowledgeAsOf`；
- 完整知识 revision manifest；
- tokenizer 和提示词版本。

已经开始的患者模拟不会自动切换到新发布的资料。修改并发布知识后，应新建一场患者模拟验证新版本。这样可以保证同一场训练前后口径一致，并让历史证据可追溯。

## 17. 常见问题

### 17.1 后端没有监听 8080

```powershell
Test-NetConnection 127.0.0.1 -Port 8080
Get-Process oral_training_backend -ErrorAction SilentlyContinue
```

检查启动终端中的首个错误。常见原因是 `DATABASE_URL` 无效、端口被占用或运行库路径缺失。

### 17.2 健康检查为 503

检查 `/api/health` 返回内容：

- `modelConfigured=false`：未在当前后端进程中设置 DeepSeek Key；
- `database=false`：连接字符串、密码或数据库服务异常；
- Worker database backoff 大于 0：应用角色通常缺少表权限；
- `knowledgeWorkerThreads=0`：知识 Worker 没有成功启动。

### 17.3 日志显示“对表 ai_jobs 权限不够”

重新执行第 6.1 节的授权。迁移由管理员执行时，不能只修改应用账号密码而忽略新表权限。

### 17.4 管理入口不显示

确认数据库中的 demo 用户角色为 `admin`，然后重新登录以获取新 Token。旧 Token 中仍可能携带旧角色信息。

### 17.5 管理员不能开始患者模拟

这是权限隔离的预期行为。按照第 14 节将 demo 用户恢复为 `learner` 并重新登录。

### 17.6 看不到服务或场景

检查：

- 服务是否已经发布；
- 服务是否处于 `active`；
- `scenarioIds` 是否包含目标场景；
- 当前是否使用 learner Token；
- 数据库是否已执行迁移 010 和 011。

### 17.7 知识预览没有命中

检查：

- 草稿是否已保存；
- 问题关键词是否出现在标题、正文、适用范围或 aliases；
- `scope/serviceId` 是否匹配；
- `trainingScope` 是否为当前演示使用的 `demo`；
- 发布后是否创建了 `knowledge_chunks`。

### 17.8 更新知识后旧会话仍使用旧内容

这是会话快照机制，不是缓存错误。结束旧会话并创建新会话。

### 17.9 回复出现价格冲突

结构化服务事实是价格和时间的主来源。检查知识正文是否复制了另一版价格，或是否错误关联了其他服务 ID。修订后发布新版本并新建会话，不要修改已发布 revision。

## 18. 停止和重新启动

启动终端中按 `Ctrl+C` 可正常停止后端。也可以查找进程：

```powershell
Get-NetTCPConnection -LocalPort 8080 -State Listen |
  Select-Object LocalAddress, LocalPort, OwningProcess
```

只在确认 PID 属于 `oral_training_backend` 后停止：

```powershell
Get-Process -Id <PID>
Stop-Process -Id <PID>
```

重新启动时，需要重新设置该 PowerShell 进程的 `DATABASE_URL`、`DEEPSEEK_API_KEY` 等环境变量，然后运行 Release 可执行文件。

## 19. 上线前边界

本文配置仅适用于本地 demo。生产环境至少还需要：

- `AUTH_MODE=wechat`；
- 正确的微信 App ID 和 Secret；
- HTTPS 反向代理；
- 精确 CORS Origin；
- 禁止运行时上传 API Key；
- 可信代理 IP；
- 请求 ID 和结构化日志；
- 经人工审核的 `reviewed/verified` 知识；
- 正式数据备份、迁移演练和回滚方案。

不得把 `synthetic/unverified/demo` 数据描述为真实诊所价格、真实号源或正式医疗指南，也不得用演示知识替代医生诊断。


## N07 分段开启与暂停

后端启动时读取以下变量，修改后需重启。先完成 N06 人工复核及微信 DevTools 验收，再选择性开启：

```dotenv
RAG_ROLEPLAY_ENABLED=false
RAG_PATIENT_ENABLED=false
RAG_EVALUATION_V2_ENABLED=false
MODEL_CALL_LIMIT=0
```

患者模拟只依赖 ROLEPLAY；客服训练的新建同时要求 PATIENT 和 EVALUATION_V2 为 true。只关闭 EVALUATION_V2 也会暂停新客服训练，不会停止旧会话的 v2 评分。旧消息、画像、null 报告与引用继续可读，旧会话不降级至 v1；关闭期间相同 clientSessionId 的已成功请求可以幂等重放，重新开始属于新建，返回 503 RAG_NEW_SESSIONS_PAUSED。无 serviceId 的旧 v1 入口保持兼容。

受控联调用独占测试后端，设置明确的正数 MODEL_CALL_LIMIT（例如拟议的 16 次 HTTP 尝试，尚需确定批次预算），把 stderr 保留为审计文件。0 表示不限制，仅适用于正常运行配置；负数或非法数字导致启动失败。并发与重试共用上限，重启会清零，因此不得通过自动重启重复批次。达到上限返回 MODEL_CALL_BUDGET_EXHAUSTED，不自动发起队列重试。

真实联调步骤、参数和回退演练见 [N07 联调与发布记录](rag-n07-validation.md)。不要用原 smoke.ps1 -WithModel 代替受控脚本：原脚本会遍历多个场景且不验证本次预算。
