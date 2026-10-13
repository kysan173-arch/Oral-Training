# RAG 本地配置与数据初始化教程

本文对应支持 EmbeddingGemma 2 和迁移 `036_embeddinggemma_cache.sql` 的版本，更新于 2026-10-12。从仓库根目录执行命令；Windows 使用 PowerShell，Linux 使用 Bash。示例数据库为本机专用开发库 `oral_training`。

## 1. 先了解要启动什么

```text
微信小程序 → Crow 后端（8080）→ PostgreSQL（5432）
                         ├→ EmbeddingGemma 2（8765）：知识向量与语义检索
                         └→ 个人模型网关：患者台词、客服回复、评分和复盘
```

后端、数据库、Embedding 服务分别启动。个人生成模型在小程序「我的 → LiteLLM 模型配置」中设置；Embedding 模型由后端统一配置。后端 `/api/health` 正常，并不证明 Embedding 或个人生成模型可用，需要分别验证。

推荐顺序：数据库与迁移 → 构建和离线检查 → Embedding 服务 → 后端 → 测试数据 → 小程序个人模型 → 两种训练验收。

| 环境 | 要求 |
|---|---|
| Windows 后端 | Windows 10/11、Visual Studio 2022 C++ 桌面开发、CMake 3.20+、PostgreSQL 16/18 客户端及开发库 |
| Linux 后端 | C++17 编译器、CMake 3.20+、OpenSSL/libpq 开发库、PostgreSQL；本文命令按 Ubuntu 24.04 / PostgreSQL 16 编写 |
| 检查与数据脚本 | Python 3.12、Node.js。Windows 的 Python 若缺时区数据库需安装 `tzdata` |
| 检索模型 | 独立 Python 环境与模型权重；安装、启动见 [EmbeddingGemma 教程](embeddinggemma-setup.md) |
| 小程序 | 微信开发者工具；Linux 后端可供另一台开发机通过受控网络入口使用 |
| 生成模型 | 已部署、支持 Chat Completions 和 JSON object 输出的模型网关，以及本账号的访问 Key |

凭据、本地 `backend.env`、数据库备份、模型主密钥及生成文件都不要提交到 Git。

## 2. 创建数据库，保留已有数据

Linux 可安装构建依赖和数据库客户端/服务：

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ libssl-dev libpq-dev postgresql-16 python3 nodejs
sudo -u postgres psql
```

Windows 安装 PostgreSQL 后连接管理数据库：

```powershell
$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
& $psql -h 127.0.0.1 -U postgres -d postgres
```

在 `psql` 内先查询；**仅在不存在时**创建对应对象。用 `\password` 交互设置密码，不把真实密码写进 SQL 文件：

```sql
SELECT datname FROM pg_database WHERE datname = 'oral_training';
SELECT rolname FROM pg_roles WHERE rolname = 'oral_training_app';
-- 仅首次创建时执行下面三行。
CREATE ROLE oral_training_app LOGIN;
\password oral_training_app
CREATE DATABASE oral_training OWNER oral_training_app ENCODING 'UTF8';
\q
```

升级已有库前停止后端并备份。下面的 `pg_dump` 使用密码提示，备份放在仓库外；检查退出码和文件大小后再迁移。

```bash
mkdir -p "$HOME/oral-training-backups"
pg_dump -h 127.0.0.1 -U oral_training_app -W -d oral_training -Fc \
  -f "$HOME/oral-training-backups/before-rag-$(date +%Y%m%d-%H%M%S).dump"
```

```powershell
$backupDir = Join-Path $HOME 'oral-training-backups'
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
& 'C:\Program Files\PostgreSQL\18\bin\pg_dump.exe' `
  -h 127.0.0.1 -U oral_training_app -W -d oral_training -Fc `
  -f "$backupDir\before-rag-$stamp.dump"
if ($LASTEXITCODE -ne 0) { throw 'Backup failed' }
```

如旧库表属于其他账号，改用具备完整读取权限的备份账号。不要删除或重建已有库。

## 3. 配置后端并执行迁移

首次复制模板；已有配置直接编辑，不覆盖：

```bash
test -f backend/backend.env || cp backend/.env.example backend/backend.env
chmod 600 backend/backend.env
```

```powershell
if (-not (Test-Path backend/backend.env)) {
  Copy-Item backend/.env.example backend/backend.env
}
```

编辑 `backend/backend.env`，将占位符替换为本机配置：

```dotenv
DATABASE_URL=postgresql://oral_training_app:<URL编码后的密码>@127.0.0.1:5432/oral_training
PRODUCTION=false
AUTH_MODE=demo
BIND_ADDRESS=127.0.0.1
PORT=8080
DATABASE_POOL_SIZE=12
DATABASE_POOL_WAIT_MS=3000
AI_WORKER_CONCURRENCY=1
KNOWLEDGE_WORKER_CONCURRENCY=1
MODEL_CALL_LIMIT=0

RAG_EMBEDDING_BASE_URL=http://127.0.0.1:8765/v1
RAG_EMBEDDING_MODEL=embeddinggemma-2
RAG_EMBEDDING_DIMENSIONS=768
RAG_EMBEDDING_REVISION=1
RAG_EMBEDDING_BATCH_SIZE=16
RAG_EMBEDDING_TIMEOUT_SECONDS=30
RAG_EMBEDDING_MIN_SIMILARITY=0.65
```

密码中的 `@`、`:`、`/`、`#`、`%` 等必须做 URI 编码，可用 Python `urllib.parse.quote(password, safe='')` 或 PowerShell `[Uri]::EscapeDataString($password)`；不要在终端输出实际连接字符串。文件只接受 `KEY=VALUE`，不执行 `$HOME`、`~` 或命令替换，路径应写绝对路径。

### 3.1 Linux 主密钥与迁移

第一次部署创建个人模型 Key 的加密主密钥；已有主密钥直接使用，恢复机器时应恢复原文件：

```bash
python3 backend/linux_runtime.py --init-key "$HOME/.config/oral-training/model.key"
```

在 `backend/backend.env` 增加 `MODEL_KEY_FILE=/home/<用户名>/.config/oral-training/model.key`，替换用户名。文件必须是运行账号所属、权限 `0600` 的 32 字节普通文件；单独备份，不能在每次启动时重新生成。

从配置文件加载连接再迁移，避免把含密码的 URL 放到命令参数中：

```bash
python3 -B - <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, 'backend')
from linux_runtime import load_env
from migrate import migrate
env = load_env(Path('backend/backend.env'))
migrate(env['DATABASE_URL'], psql='/usr/lib/postgresql/16/bin/psql')
PY
```

### 3.2 Windows 加载配置与迁移

在仓库根目录的 PowerShell 中加载配置；后续启动也使用同一窗口：

```powershell
foreach ($line in Get-Content backend/backend.env -Encoding UTF8) {
  $line = $line.Trim()
  if (-not $line -or $line.StartsWith('#')) { continue }
  $parts = $line.Split('=', 2)
  if ($parts.Count -ne 2) { throw 'Invalid backend.env line' }
  Set-Item -Path "Env:$($parts[0].Trim())" -Value $parts[1].Trim()
}
$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
& backend/migrate.ps1 -DatabaseUrl $env:DATABASE_URL -PsqlPath $psql
if ($LASTEXITCODE -ne 0) { throw 'Migration failed' }
```

Windows 使用 DPAPI 加密个人模型 Key，不需要 `MODEL_KEY_FILE`。密文绑定运行账号和机器；更换机器/账号或从 Windows 转到 Linux 后，应在个人配置页重新保存 Key。

### 3.3 迁移核对与权限

统一迁移入口按编号执行全部 SQL、记录 SHA-256，重跑跳过已完成文件，并为旧迁移 005 安装完整源行归档保护。不要再次用循环直接重放全部 SQL，也不要修改已经发布的迁移。

旧库没有 `schema_migrations` 时，先核实最后成功的迁移编号，再使用 Python `migrate(..., baseline=N)` 或 PowerShell `-BaselineThrough N`。例如**仅在确认 001–035 全部成功应用后**才能用 35；该参数只是登记历史，不能跳过缺失或失败的步骤。

通过 `psql` 检查：

```sql
SELECT name, baselined FROM schema_migrations ORDER BY name;
SELECT to_regclass('knowledge_chunk_embeddings');
```

应至少完成到 `036_embeddinggemma_cache.sql`。它新增向量缓存表，**不需要 pgvector**。034–035 对应个人模型配置，032–033 对应患者初始化和归档完整性。

新库推荐由数据库所有者执行迁移。若历史表由管理员创建，需要由对象所有者或管理员给应用角色授权：

```sql
GRANT CONNECT ON DATABASE oral_training TO oral_training_app;
GRANT USAGE ON SCHEMA public TO oral_training_app;
GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO oral_training_app;
GRANT USAGE, SELECT, UPDATE ON ALL SEQUENCES IN SCHEMA public TO oral_training_app;
GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA public TO oral_training_app;
```

之后统一迁移执行账号，或为实际建表账号设置相应 `ALTER DEFAULT PRIVILEGES`。只授予数据库 CONNECT 并不会授予新表访问权限。

## 4. 构建与离线检查

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
cmake -S backend -B backend/build-msvc -G 'Visual Studio 17 2022' -A x64 `
  -DPostgreSQL_ROOT='C:/Program Files/PostgreSQL/18'
cmake --build backend/build-msvc --config Release
ctest --test-dir backend/build-msvc -C Release --output-on-failure
& backend/tests/static_checks.ps1
python backend/tests/litellm_integration_test.py --pg-bin 'C:/Program Files/PostgreSQL/18/bin'
```

首次 CMake 配置需联网获取固定版本的依赖。未设置测试数据库时部分 CTest 数据库用例会跳过；上述集成入口会建立临时 PostgreSQL 集群和模拟网关，覆盖数据库与 HTTP 流程，不调用真实生成模型，也不清理开发库。

## 5. 启动并验证 EmbeddingGemma 2

按照 [EmbeddingGemma 2 配置教程](embeddinggemma-setup.md) 完成安装或复用已有服务，至少验证：

1. `GET http://127.0.0.1:8765/health` 成功。
2. `POST /v1/embeddings` 的 `task=query` 和 `task=document` 都返回 `model=embeddinggemma-2`，每条向量为 768 维有限数值。
3. 后端配置的 Base URL 是 `http://127.0.0.1:8765/v1`，不包含末尾 `/embeddings`。

8765 是**后端所在机器**的地址。Windows 后端访问 Linux 上的服务时，不能直接把 Linux 的局域网 HTTP 地址填进去：后端只允许回环 HTTP 或远程 HTTPS。可建立受控 SSH 本地转发，再使用上述回环 URL；详见模型教程。

## 6. 启动后端并检查健康状态

Linux 启动器自动读取 `backend/backend.env`：

```bash
./backend/start-backend.sh
```

Windows 在第 3.2 节已加载配置的窗口启动：

```powershell
$env:PATH = 'C:\Program Files\PostgreSQL\18\bin;' + $env:PATH
& backend/build-msvc/Release/oral_training_backend.exe
```

直接运行二进制不会读取 `.env` 文件。另开终端检查：

```bash
curl --fail --noproxy '*' http://127.0.0.1:8080/api/health
```

```powershell
(Invoke-RestMethod 'http://127.0.0.1:8080/api/health').data | Format-List
```

预期 `ready=true`、`database=true`、`workerRunning=true`，两个 Worker 的数据库退避计数为 0。`modelConfigured=null`、`modelConfigurationScope=personal` 是正常值；个人模型未配置不会让整个后端不就绪。

当前版本两种服务训练默认启用，旧 `RAG_ROLEPLAY_ENABLED`、`RAG_PATIENT_ENABLED`、`RAG_EVALUATION_V2_ENABLED` 不再读取。`MODEL_CALL_LIMIT=0` 表示不限制生成模型调用次数；受控真实测试应设置正数上限，重试也计入，上限随进程重启归零。达到上限时返回 `MODEL_CALL_BUDGET_EXHAUSTED`。

## 7. 导入完整本地测试数据

先保持数据库、后端、Embedding 服务运行，确认配置为 `PRODUCTION=false`、`AUTH_MODE=demo`。`psql` 应在 PATH 中。脚本读取同一份后端配置，数据库必须是回环地址，并与 `--database` 完全一致。

Linux 从仓库根目录执行：

```bash
# 只读预检，不写入。
python3 -B backend/seed_local_test_data.py --database oral_training
# 确认预检显示的数据库无误后导入。
python3 -B backend/seed_local_test_data.py --database oral_training --apply
```

Windows 对应使用 `python`；先确保 PostgreSQL 的 bin 目录在 PATH 中。若遇到 `ZoneInfoNotFoundError`，执行 `python -m pip install tzdata` 后重试。数据脚本本次实测平台为 Linux。

`--as-of YYYY-MM-DD` 可固定历史和计划日期，省略时使用上海当天日期。可通过 `--env-file <配置文件>` 指定配置；已有后端必须使用同一数据库和端口。

| 样本 | 内容 |
|---|---|
| 账号 | 1 名测试主管、3 名有历史学员、1 名空历史学员；原演示账号获得测试历史，原角色和模型配置保留 |
| 服务 | 6 个：4 个可用已发布服务、1 个草稿、1 个下架样本；基础种子含模板映射；完整手测扩展会修正为实际可用场景 |
| 知识 | 20 篇已发布合成知识、1 篇草稿、32 个正文分块，并预热实际向量缓存 |
| 两种训练 | 新增 40 段客服训练、16 段患者模拟；进行中、完成、放弃、失败重试、证据不足等状态 |
| 报告与引用 | 32 份评分状态记录、8 份复盘状态记录；服务版本、锁定资料、可查看的引用 |
| 学习与主管页 | 5 份计划、12 条指派、28 次签到、8 条收藏、8 条错题掌握记录 |

脚本可重复运行，保留已有样本和后续训练进度，不清库、不复制个人模型密钥。重新指定日期也不会重写已经存在的历史。服务和知识走正常发布 API，保留不可变版本和审计记录。导入会调用本地 Embedding，但不会调用生成模型，也不创建待自动消费模型的任务。失败样本由用户主动重试后才进入正常任务流程。

输出保存在 `backend/build/local-test-seed-result.json`。查看 `existingDataPreserved`、`apiChecks`、`embeddings` 和计数；在没有其他用户同时发起生成请求时，`modelCallsBefore` 与 `modelCallsAfter` 应一致。脚本只撤销自己生成的临时登录令牌。中途失败时先解决日志中的原因再重跑，无需清理整库。

所有价格、患者对话和报告均为合成测试资料，报告标记为 `synthetic-local-fixture-v1`，不能当作真实模型质量评估或真实诊所资料。

### 完整手动测试的知识补充

基础样本之外，投诉退费、候诊改约、正畸方案对比、洁牙边界和发布时效测试可继续运行：

```bash
python3 -B backend/prepare_manual_knowledge.py --database oral_training
python3 -B backend/prepare_manual_knowledge.py --database oral_training --apply
```

该扩展修正服务映射，覆盖10个实际启用的非模板场景，并提供独立的知识冲突和时效样本。问题、预期结果与账号选择见 [完整手动测试知识库清单](../validation/manual-knowledge-checklist.md)。旧会话仍使用原快照，新增资料请用新会话测试。

## 8. 小程序和个人生成模型

用微信开发者工具打开仓库根目录。开发 API 地址在 `utils/config.js`，本机默认为 `http://127.0.0.1:8080/api`。模拟器本地调试可关闭合法域名校验；手机上的 `127.0.0.1` 指手机本身，真机需配置可访问的 HTTPS 入口。

`AUTH_MODE=demo` 仍由后端签发 Bearer Token，不访问真实微信登录服务。导入数据后，在「我的 → 切换演示账号」选择 `Test 主管` 或 `Test 学员·…`。主管用于知识管理和聚合看板，学员用于训练；不需要反复修改原 demo 用户的角色。

需要实时对话的每个账号分别进入「我的 → LiteLLM 模型配置」并保存：

| 字段 | 填写方式 |
|---|---|
| Base URL | 自己已部署的兼容网关，如 `https://llm.example.com/v1`；后端追加 `/chat/completions` |
| 模型 | 网关中已配置的 `model_name`，如 `ds-primary`；必须是该网关实际提供的别名 |
| API Key | 该网关给本账号的 Key；不要把 Embedding 地址或其他人的 Key 填进来 |

保存后无需重启后端。后台报告/复盘使用会话所属账号的配置；知识草稿生成使用任务创建者的配置。新测试账号没有复制别人的密钥，阅读合成历史无需 Key，继续实时训练需自行配置。

`DEEPSEEK_API_KEY`、`DEEPSEEK_MODEL`、`ALLOW_RUNTIME_API_KEY` 已停用，旧上传密钥接口返回 410。网关部署由其管理员完成，更多说明见 [后端个人模型配置](../../backend/README.md#环境变量)。

## 9. 发布知识与验收 RAG

测试数据已经提供服务和知识。手工增加资料时，使用主管账号进入「管理后台 → 知识管理」：

1. 创建服务，录入结构化价格、包含/不包含项目、到诊时长、疗程、复诊和适用场景；金额单位是分。
2. 保存并发布服务版本，保持服务为 `active`。草稿或下架服务不会出现在新训练选择中。
3. 创建知识草稿，绑定本数据库的正确 `serviceId`；通用知识使用相应通用范围。
4. 填写来源和适用条件，合成内容保持 `synthetic/unverified/demo`，不伪造真实出处。
5. 保存草稿，检索预览通过后发布不可变版本。长正文可产生多个分块。

仓库的 [12 条种植牙测试知识](../rag-data/患者模拟_RAG测试知识库_具体参数版.json) 可作为手工输入参考；管理页面不提供 JSON 批量上传。服务价格和知识里的价格须自洽，不能混用另一套示例参数。

先预览「种植牙报价包含哪些项目？」和「隐形矫正需要多久？」，检查正确服务下的依据。没有相关资料可返回空结果；模型断连应报 `RAG_UNAVAILABLE`，不能伪装成正常无命中。

随后覆盖以下流程，真实模型测试只在离线检查通过后受控执行：

- **患者模拟**：选已发布服务，追问费用、包含项目、疗程和预约，查看依据，结束并复盘。
- **客服训练**：创建患者并完成初始化，多轮回答患者问题，结束后轮询评分报告，确认依据和证据不足状态。
- **恢复与历史**：离开进行中会话后恢复；查看两种历史、失败样本、旧报告及引用。
- **边界与隔离**：学员不能管理知识，也看不到其他学员私有记录；资料未知时不编造价格、实时号源或诊断。

新会话锁定服务 revision、知识时间点和 revision manifest。发布新知识后，用新会话验证；旧会话继续使用原快照。向量缓存按模型、地址、维度、版本等隔离；更换同名权重或服务侧提示前缀时应更新 `RAG_EMBEDDING_REVISION`，见 [缓存说明](embeddinggemma-setup.md#5-向量缓存与换模型)。

## 10. 常见问题

| 现象 | 优先检查 |
|---|---|
| 8080 连接失败 | 构建是否成功、启动日志、端口占用、是否正确加载配置；Linux 主密钥路径/权限 |
| `/api/health` 为 503 | `database`、Worker 退避计数、连接池、应用账号表权限 |
| `modelConfigured=null` | 正常，公共健康接口不泄露个人配置；去当前账号模型页面检查 |
| `MODEL_NOT_CONFIGURED` | 当前账号未保存自己的生成模型配置 |
| `MODEL_CONFIG_UNREADABLE` | DPAPI 运行身份改变或 Linux 主密钥丢失/更换；恢复原密钥或重新保存个人 Key |
| `RAG_UNAVAILABLE` | 8765 是否运行、Base URL 是否误填完整接口、模型 ID/维度是否匹配、036 是否应用 |
| 预览为空或串服务 | 发布状态、`scope/serviceId`、资料适用期与会话快照；检查语义内容及阈值，不能靠无限降阈值通过测试 |
| 没有管理入口 | 当前是否主管账号；换账号后重新取得 Token |
| 没有服务 | 服务是否 active/已发布、是否映射目标场景，是否使用学员账号 |
| 导入脚本拒绝运行 | 非生产/demo 配置、准确数据库名、回环 URI、迁移账本和健康状态；不得绕过检查向生产库导入 |
| 修改 env 不生效 | 重启后端；Embedding 的 `service.env` 改动还需单独重启 Embedding 服务 |

## 11. 重启与部署边界

前台后端按 `Ctrl+C` 停止，再运行对应启动步骤。Linux Embedding 的启停/日志见模型教程；长期后端部署可参考 `backend/systemd/oral-training.service`，配置与主密钥放仓库外。

本教程使用本地 demo。生产部署还需真实微信登录、HTTPS 反向代理、精确 CORS、可信代理地址、独立受限数据库账号、备份恢复演练和经审核资料。不要把测试样本导入生产库。
