# Oral Training Backend

单个 Windows x64 程序同时运行 Crow API 和可恢复 AI Worker。PostgreSQL 保存用户隔离的会话、消息租约、评分/复盘和任务尝试；DeepSeek 网关保持既有请求、Prompt、解析及调用内重试逻辑。

## 本地启动

1. 设置环境变量（程序不会自动加载 `.env`）。
2. 备份已有数据库，并按顺序执行迁移：

   ```powershell
   $psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
   & .\migrate.ps1 -DatabaseUrl $env:DATABASE_URL -PsqlPath $psql
   ```

   `003` 会完整归档历史重复轮次后建立唯一索引，回填回复状态，并为已有 `generating` 记录补任务。`004` 保留所有旧记录并归属到 `demo-user-001`。`005` 按“最新回复 + 其之前最近一次输入”修复被拆开的历史问答，并补建完成会话缺失的报告或任务；被替换的消息、报告和任务状态都会归档。迁移本身不会调用模型，执行 `005` 至最新迁移 期间必须保持后端停止，全部迁移完成后再启动。`010` 增加服务、知识、不可变版本、发布审计及独立草稿生成队列；`011` 增加角色互换 RAG 快照、证据 trace 和消息引用。`012` 至 `019` 依次为自定义患者画像、推荐场景、培训计划与指派、主管团队归属、消息情绪、轮次内提示唯一键、场景反应规则与自由模拟模板。

3. 构建并启动：

   迁移入口自动按编号执行所有 SQL（包含 `032` 患者初始化、`033` 归档完整性及 `034–035` 个人模型配置），记录文件 SHA-256，重跑跳过已完成文件。升级前停止后端并备份数据库。已有数据库首次采用此入口时，必须核实最后成功应用的编号，再增加 `-BaselineThrough <编号>`；例如核实上游 001–031 全部完成后可使用 `-BaselineThrough 31`。该参数仅登记已应用迁移，不能用于跳过失败步骤。

   已发布的 005 文件保持原样。入口在执行 005 前安装 `migration-support/005_archive_guard.sql`，通过 BEFORE INSERT 捕获完整源行，避免同名 summary 列导致空值/不完整归档。升级尚未应用 005 的旧库时，若需手工执行该步骤，也必须先安装此保护；不要在已升级的数据库重放旧版状态修复，应使用迁移账本跳过已完成步骤。033 保留旧归档原文并将无法证明完整的历史归档标为 `source_row_complete=false`；无法从现存数据推断曾丢失的字段，应查找备份补证，不能用当前行冒充旧行。

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
AI_WORKER_CONCURRENCY=1
KNOWLEDGE_WORKER_CONCURRENCY=1
DATABASE_POOL_SIZE=12
DATABASE_POOL_WAIT_MS=3000
```

TLS 在反向代理终止。`TRUSTED_PROXY_IPS` 是以逗号分隔的精确代理 IP 列表，必须包含实际连接后端的每一层可信代理；程序只接受这些代理提供的 `X-Forwarded-Proto`，并从 `X-Forwarded-For` 右侧逐层剥离可信代理后确定限流客户端。代理应覆盖协议头并正确追加或覆盖客户端地址头。

生产模式要求微信登录、HTTPS、HTTPS Origin 和非空可信代理列表。布尔值只接受 `true/false`、`1/0`、`yes/no`、`on/off`（忽略大小写），整数必须完整合法；任何无效或降级配置都会让程序拒绝启动。学员报告 Worker 并发默认 1、最大 4，知识草稿 Worker 由 `KNOWLEDGE_WORKER_CONCURRENCY` 控制，默认 1、最大 2。API、身份服务和两个 Worker 池共享惰性连接池，默认最多 12 个连接、最长等待 3 秒；连接池大小必须至少为两个 Worker 并发数之和加 2，池耗尽时 API 返回 503 `DATABASE_BUSY`。

按顺序执行到迁移 `035_personal_litellm_settings.sql` 后，学员和主管均在小程序「我的 → LiteLLM 模型配置」填写自己的网关 Base URL、模型别名和网关 API Key。保存后无需重启，仅本账号后续调用使用配置。所有读写按 bearer 会话中的用户 ID 隔离，忽略客户端 userId。后台初始化、报告和复盘按会话所属账号选配置，知识草稿生成按任务创建者选配置；不借用主管或其他账号的 Key。原机构配置和审计完整保留，只给最后保存者复制个人配置，迁移重跑不会覆盖或恢复已清除的个人配置。后端不再读取 `DEEPSEEK_API_KEY`、`DEEPSEEK_MODEL` 或 `ALLOW_RUNTIME_API_KEY`，旧密钥接口返回 410。

公共健康检查只判断数据库、队列和 Worker，返回 `modelConfigured=null`、`modelConfigurationScope=personal`。个人是否已配置通过鉴权的配置接口查询；未配置的个人模型调用返回 `MODEL_NOT_CONFIGURED`，不会使所有用户的服务一起变成未就绪。

API Key 用 Windows DPAPI 加密后保存在 PostgreSQL，与后端运行的 Windows 账号和机器绑定；迁移到其他机器或服务账号后，用户需重新输入自己的 Key。已有请求使用开始时的配置快照，保存和清除只影响本人后续调用。页面不会回显密钥，也不会把它写入小程序本地存储。生产配置接口继续强制鉴权和 HTTPS。

LiteLLM 网关需要单独部署并配置上游模型。页面输入的是网关颁发的 Key 和 `model_name`，不是上游供应商 Key。后端向 Base URL 追加 `/chat/completions`，空路径自动使用 `/v1`；HTTP 仅允许回环地址，远程网关必须 HTTPS，禁止自动跟随重定向。网关建议关闭额外重试、配置适合所选模型的推理模式，并支持 JSON object 输出；业务 Prompt、JSON 校验及两次修复尝试仍在后端执行。参考 [LiteLLM 配置文档](https://docs.litellm.ai/docs/proxy/configs)。

模型配置离线集成测试：从仓库根目录运行 `python backend/tests/litellm_integration_test.py`，或用 `--bin-dir` 指定新版可执行文件目录。脚本创建独立的临时 PostgreSQL 集群和本地模拟网关，验证迁移重跑、权限、加密持久化/重启、配置清除、忽略旧环境变量和两种训练调用的路由；随后运行普通 smoke、状态机及并发检查。测试不使用真实供应商，不验证真实模型质量；结束后关闭测试进程，保留临时日志。

客服答复使用 `service-reply-v3` / `service-reply-rag-v3`。带资料模式直接显示模型消化后的简短答复，引用单独保留，后端不再往聊天正文追加资料全文。`evidence_validator_test` 覆盖正文长度、引用归属、数字范围/单位、起价条件和资料缺失等边界。离线检查通过后可手动运行 `service_reply_probe.exe --live`：设置 `DATABASE_URL`、`REPLY_PROBE_USER_ID` 和该用户已有 v2 会话的 `REPLY_PROBE_SESSION_ID`。它只读该会话的资料快照，使用用户已保存的个人 Key，执行 6 个带资料问题和 1 个普通客服问题，最多 14 次 HTTP 尝试；不会改写用户对话，也不会自动加入 CTest。`--live-current` 则只针对现有对话历史追问一次总疗程，最多 2 次尝试，检查旧长回复不会被继续模仿。输出只包含问题、答复及非敏感质量指标。

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

知识目录与存储 API 使用一次性 schema 验证，不会清理未核对范围的数据库：

```powershell
.\tests\knowledge_catalog_migration.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
.\tests\knowledge_store_database.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
.\tests\knowledge_admin_api.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
.\tests\audit_regression.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
.\tests\offline_api_regression.ps1 -DatabaseUrl 'postgresql://.../oral_training_test'
```

`audit_regression.ps1` 使用统一迁移入口创建隔离 schema，验证审核修复及有历史数据时迁移重跑不变；`offline_api_regression.ps1` 在 loopback 随机端口启动无密钥测试后端，运行常规 smoke、状态机与两模式并发创建，结束后停止进程并清理该测试 schema。两者不会调用真实模型。

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

对话自然度用例：运行 service_reply_probe.exe --cases backend/tests/service_reply_cases.json。每批最多 12 个经过检查的模拟问题、每题最多两次 HTTP 尝试，保留原始回复和最终回复供人工逐项评估；字符数达标不等于自然度合格。
