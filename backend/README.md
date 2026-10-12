# Oral Training Backend

首次部署见 [完整本地配置与数据初始化教程](../docs/setup/RAG本地配置与数据初始化教程.md)；独立检索模型的安装、启动与诊断见 [EmbeddingGemma 2 配置教程](../docs/setup/embeddinggemma-setup.md)。

单个 Windows / Linux 程序同时运行 Crow API 和可恢复 AI Worker。PostgreSQL 保存用户隔离的会话、消息租约、评分/复盘和任务尝试；生成模型按账号路由，独立 EmbeddingGemma 服务负责知识向量化。

## 本地启动

1. 设置环境变量（程序不会自动加载 `.env`）。
2. 备份已有数据库，并按顺序执行迁移：

   ```powershell
   $psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
   & .\migrate.ps1 -DatabaseUrl $env:DATABASE_URL -PsqlPath $psql
   ```

   `003` 会完整归档历史重复轮次后建立唯一索引，回填回复状态，并为已有 `generating` 记录补任务。`004` 保留所有旧记录并归属到 `demo-user-001`。`005` 按“最新回复 + 其之前最近一次输入”修复被拆开的历史问答，并补建完成会话缺失的报告或任务；被替换的消息、报告和任务状态都会归档。迁移本身不会调用模型，执行 `005` 至最新迁移 期间必须保持后端停止，全部迁移完成后再启动。`010` 增加服务、知识、不可变版本、发布审计及独立草稿生成队列；`011` 增加角色互换 RAG 快照、证据 trace 和消息引用。`012` 至 `019` 依次为自定义患者画像、推荐场景、培训计划与指派、主管团队归属、消息情绪、轮次内提示唯一键、场景反应规则与自由模拟模板。

3. 构建并启动：

   迁移入口自动按编号执行所有 SQL（包含 `032` 患者初始化、`033` 归档完整性及 `034–035` 个人模型配置与 `036` 向量缓存），记录文件 SHA-256，重跑跳过已完成文件。升级前停止后端并备份数据库。已有数据库首次采用此入口时，必须核实最后成功应用的编号，再增加 `-BaselineThrough <编号>`；例如核实上游 001–031 全部完成后可使用 `-BaselineThrough 31`。该参数仅登记已应用迁移，不能用于跳过失败步骤。

   已发布的 005 文件保持原样。入口在执行 005 前安装 `migration-support/005_archive_guard.sql`，通过 BEFORE INSERT 捕获完整源行，避免同名 summary 列导致空值/不完整归档。升级尚未应用 005 的旧库时，若需手工执行该步骤，也必须先安装此保护；不要在已升级的数据库重放旧版状态修复，应使用迁移账本跳过已完成步骤。033 保留旧归档原文并将无法证明完整的历史归档标为 `source_row_complete=false`；无法从现存数据推断曾丢失的字段，应查找备份补证，不能用当前行冒充旧行。

   ```powershell
   cmake -S . -B build-msvc -G 'Visual Studio 17 2022' -A x64
   cmake --build build-msvc --config Release
   $env:PATH='C:\Program Files\PostgreSQL\18\bin;' + $env:PATH
   .\build-msvc\Release\oral_training_backend.exe
   ```

## Linux 构建、迁移与启动

需要 CMake 3.20+、C++17 编译器、OpenSSL/libpq 开发库、Python 3、Node.js（客户端检查）和 PostgreSQL。
Ubuntu 24.04 可安装 `cmake g++ libssl-dev libpq-dev postgresql-16`。首次 CMake 配置需要获取已固定版本的依赖。

```bash
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build -j 2
ctest --test-dir backend/build --output-on-failure
python3 backend/tests/litellm_integration_test.py --pg-bin /usr/lib/postgresql/16/bin
```

集成测试自行建立临时数据库集群，验证完整 HTTP API、两种训练模式、并发创建、消息幂等/租约、
后台报告失败/重试、用户隔离、加密保存/进程重启、知识管理和患者初始化。默认不会访问现有数据库或真实模型。
显式增加 `--live-pass-entry api/deepseek` 会在全部离线检查通过后，在临时数据库中对两种角色各发起一次真实 DeepSeek 调用，进程最多 4 次模型 HTTP 尝试；结束时清除临时个人配置与测试主密钥，不改动实际账号配置。
CTest 未配置测试数据库时会跳过数据库用例；集成入口会分别在隔离 schema 中执行这些用例。
`run_embedding_checks.py` 继续只运行语义检索相关检查，不代替完整 API 集成验证。

准备一个独立数据库和应用账号（生产账号不应为超级用户），停止旧后端并备份已有数据，再执行迁移。
Python 入口与 `migrate.ps1` 共用同一 SHA-256 迁移账本、005 完整归档保护及已存在数据库 baseline 规则。
迁移不会自动在启动时运行，也不会自动把已有数据库登记为完成迁移。

```bash
# 数据库连接可通过环境提供；不要把真实密码放进版本控制。
export DATABASE_URL='postgresql://oral_training_app@127.0.0.1:5432/oral_training'
python3 backend/migrate.py --psql /usr/lib/postgresql/16/bin/psql
python3 backend/linux_runtime.py --init-key "$HOME/.config/oral-training/model.key"
cp backend/.env.example backend/backend.env
chmod 600 backend/backend.env
# 编辑 backend/backend.env：设置 DATABASE_URL 和 MODEL_KEY_FILE 的绝对路径。
./backend/start-backend.sh
# 另一个终端确认 ready/database/workerRunning 均为 true：
curl --fail http://127.0.0.1:8080/api/health
```

启动器按 KEY=VALUE 读取配置，不执行 shell 替换。主密钥是 **32 字节原始随机数据**，只允许运行账号读取
（0600），拒绝软链接、其他账号所属或长度错误的文件；缺失时后端拒绝启动。初始化命令拒绝覆盖既有文件。
请独立备份主密钥；不要在服务运行中替换它或每次启动重新生成。密文带版本标记和随机 nonce，并验证 GCM 标签。
Windows DPAPI 原文保留，无法解密时返回 `MODEL_CONFIG_UNREADABLE`，用户可在个人页面重新保存 Key。

默认仅监听 127.0.0.1:8080，可供本机微信开发者工具连接；EmbeddingGemma 仍需单独运行在 8765 端口。
真实手机需要可访问的 HTTPS 反向代理地址。运行日志输出到 stdout/stderr，包含请求 ID，不包含 API key。
长期部署可参考 `systemd/oral-training.service`：先建立独立服务账号，将代码/程序部署到 `/opt/oral-training`，
配置与主密钥放在 `/etc/oral-training` 并交给该账号、设为 0600，再安装并启用 unit；日志用
`journalctl -u oral-training` 查看。该模板不会自动安装、修改系统服务或开放公网端口。

## 本地完整测试数据

运行中的本地后端使用 `PRODUCTION=false`、`AUTH_MODE=demo` 时，可为 `backend/backend.env`
指定的数据库增加合成测试数据。先只读预检，再显式执行导入；数据库名必须与配置完全一致：

```bash
python3 -B backend/seed_local_test_data.py --database oral_training
python3 -B backend/seed_local_test_data.py --database oral_training --as-of 2026-10-12 --apply
```

`--as-of` 决定历史、签到和计划的业务日期，省略时使用上海当天日期。此脚本只允许回环数据库和
非生产 demo 后端，沿用现有迁移结构，不执行清库或旧迁移。重复执行保留已存在的样本及其后续
训练进度；服务和知识通过正常发布接口生成不可变版本、分块和审计记录。当前日期下的重复导入
不会增加重复记录。结果与检查计数写入忽略目录 `backend/build/local-test-seed-result.json`。

样本包括：

- 1名测试主管、3名有历史的测试学员、1名无历史学员；原演示用户也获得测试历史，保留原角色和模型配置。
- 6个服务：种植牙、正畸、洁牙、资料待完善，以及未发布草稿和已下架样本。现有19个场景均有可用服务映射。
- 20篇已发布合成知识、1篇未发布草稿及32个正文分块；自动填充本机 `embeddinggemma-2` 向量缓存。
- 新增40段客服训练、16段患者模拟、424条成对对话/开场消息、32份评分状态记录、8份复盘状态记录。
  包含进行中、完成、放弃、失败可重试和v2证据不足；患者模拟带真实服务版本、锁定资料清单及可查看的引用。
- 5份计划（有效、到期、草稿、已忽略）、12条指派、28次签到、8条话术收藏和8条错题掌握记录。

小程序刷新后，在「我的 → 切换演示账号」选择 `Test 主管` 或 `Test 学员·…`；当前演示账号
可直接查看新增历史。新账号不复制其他人的模型密钥，继续实时训练前需在本账号的模型配置页填写设置。
历史报告明确标为合成样本，所有价格及资料均为虚构测试内容。导入不发起生成模型调用，也不创建
自动消费模型的待执行任务；失败样本仅在用户主动重试后进入正常任务流程。

脚本检查历史/恢复、评分/复盘、引用、患者初始化、学习页、主管页、账号隔离及两个实际向量检索示例。
保留已有非样本会话和模型配置的哈希校验，仅撤销脚本自己产生的临时登录令牌。

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

API Key 加密后保存在 PostgreSQL。Windows 使用 DPAPI，与后端运行账号和机器绑定；Linux 使用 AES-256-GCM，与仓库外的 `MODEL_KEY_FILE` 主密钥绑定。Windows 密文不能在 Linux 解密，跨平台迁移后用户需重新输入自己的 Key。Linux 同平台恢复须同时恢复数据库和原主密钥，丢失密钥时不能恢复个人 API Key。已有请求使用开始时的配置快照，保存和清除只影响本人后续调用。页面不会回显密钥，也不会把它写入小程序本地存储。生产配置接口继续强制鉴权和 HTTPS。

LiteLLM 网关需要单独部署并配置上游模型。页面输入的是网关颁发的 Key 和 `model_name`，不是上游供应商 Key。后端向 Base URL 追加 `/chat/completions`，空路径自动使用 `/v1`；HTTP 仅允许回环地址，远程网关必须 HTTPS，禁止自动跟随重定向。网关建议关闭额外重试、配置适合所选模型的推理模式，并支持 JSON object 输出；业务 Prompt、JSON 校验及两次修复尝试仍在后端执行。参考 [LiteLLM 配置文档](https://docs.litellm.ai/docs/proxy/configs)。

模型配置离线集成测试：从仓库根目录运行 `python backend/tests/litellm_integration_test.py`，或用 `--bin-dir` 指定新版可执行文件目录。脚本创建独立的临时 PostgreSQL 集群和本地模拟网关，验证迁移重跑、权限、加密持久化/重启、配置清除、忽略旧环境变量和两种训练调用的路由；随后运行普通 smoke、状态机及并发检查。测试不使用真实供应商，不验证真实模型质量；结束后关闭测试进程，保留临时日志。

客服答复使用 `service-reply-v3` / `service-reply-rag-v8`。带资料模式直接显示模型消化后的简短答复，引用单独保留，后端不再往聊天正文追加资料全文。`evidence_validator_test` 覆盖正文长度、引用归属、数字范围/单位、起价条件和资料缺失等边界。离线检查通过后可手动运行 `service_reply_probe.exe --live`：设置 `DATABASE_URL`、`REPLY_PROBE_USER_ID` 和该用户已有 v2 会话的 `REPLY_PROBE_SESSION_ID`。它只读该会话的资料快照，使用用户已保存的个人 Key，执行 6 个带资料问题和 1 个普通客服问题，最多 14 次 HTTP 尝试；不会改写用户对话，也不会自动加入 CTest。`--live-current` 则只针对现有对话历史追问一次总疗程，最多 2 次尝试，检查旧长回复不会被继续模仿。输出只包含问题、答复及非敏感质量指标。

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

接口和错误码见 `../docs/architecture/api.md`。

对话自然度用例：运行 service_reply_probe.exe --cases backend/tests/service_reply_cases.json。每批最多 12 个经过检查的模拟问题、每题最多两次 HTTP 尝试，保留原始回复和最终回复供人工逐项评估；字符数达标不等于自然度合格。


## EmbeddingGemma 语义检索与 MiniMax 验证

RAG 默认调用 EmbeddingGemma 2：`http://127.0.0.1:8765/v1/embeddings`，模型 ID 为
`embeddinggemma-2`，默认 768 维。问题使用 `task=query`，知识片段使用 `task=document`。
管理员草稿预览和训练中的检索共用此配置，患者初始化、逐轮回复、知识评分也通过同一检索器。
结构化价格、疗程等字段仍按原有规则读取；知识召回不再要求中文字面词项命中。

先用统一 `migrate.ps1` / `migrate.py` 入口执行新增迁移 `036_embeddinggemma_cache.sql`，再启动后端。
旧的知识片段无需重新发布：首次检索会为会话锁定、主题匹配且在有效期内的片段生成向量。
向量存入 PostgreSQL 的 `knowledge_chunk_embeddings` 表，不要求安装 pgvector；当前知识规模
使用应用内精确余弦排序。缓存命名空间包含地址、模型、维度、权重版本和文档格式版本，且校验
输入 SHA-256。更换同名模型权重时增加 `RAG_EMBEDDING_REVISION`。草稿预览不写发布知识缓存。

配置项见 `.env.example`。支持 128/256/512/768 维，每批最多 16 条。默认相似度门槛为 0.65，
需要结合自己的语料评估；余弦值不是置信度。最多返回 6 个片段，先检测候选冲突再截断，保留
原有 manifest、服务范围、有效期、证据编号和引用校验。缓存未命中的向量计算在数据库事务和
连接释放后进行，只有缓存写入使用短事务。服务超时、异常 HTTP、维度错误或无效向量返回
`RAG_UNAVAILABLE`，不会静默改用关键词检索。首次检索延迟与未缓存片段数量有关；大型知识库
应另行引入后台预计算和向量索引。

EmbeddingGemma 应与后端运行在同一台机器上；`127.0.0.1` 指后端所在主机。使用远程服务时
配置 HTTPS 地址；当前客户端适合可信网络内无鉴权的 embedding 服务，不携带 MiniMax 密钥。

MiniMax 生成模型使用既有个人 OpenAI 兼容网关配置（页面仍名为“LiteLLM 模型配置”）：

- Base URL：`https://api.minimax.cn/v1`
- 模型：`MiniMax-M2.7`（也可填写该账号可用的其他模型）
- API Key：个人 MiniMax M Plan Key；仍沿用现有加密保存和账号隔离，不能填入 embedding 配置。

参考 [M Plan 介绍](https://platform.minimax.cn/docs/m-plan/intro) 和
[OpenAI 兼容接口](https://platform.minimax.cn/docs/api-reference/text-openai-api)。MiniMax 的思考
内容也消耗输出预算，因此 `MiniMax-` 开头模型的首次预算最低 4096 tokens；其他模型的预算和
现有两次尝试上限不变。MiniMax 请求启用 `reasoning_split` 并在消息末尾再次要求使用系统指定的
JSON 结构，避免长证据输入后只返回纯文字。现有 JSON 解析仍兼容 `<think>` 块，客户端不展示推理内容。

### Linux 上的实际 RAG 验证

Linux CMake 现在同时构建完整 Crow API 服务器、生产检索器、向量客户端、证据校验和对应探针。
Linux 的模型与微信登录 HTTP 使用 cpp-httplib/OpenSSL，个人密钥使用 AES-256-GCM；Windows 保留 WinHTTP/DPAPI。
需要 CMake、C++17 编译器、OpenSSL/libpq 开发库、PostgreSQL 16 工具和 Python 3。

```bash
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build -j 4
python3 backend/tests/run_embedding_checks.py
# 明确执行真实模型测试；pass 只在内存中解密，且离线检查必须先通过。
python3 backend/tests/run_embedding_checks.py --live --pass-entry api/minimax
# 多轮开发用例：34轮；首次运行先保留开发/保留集的区分。
python3 backend/tests/run_embedding_checks.py --live --conversations development --run-name rag-dev-01
# 额外8轮变体；也可用 all 一次执行全部42轮。
python3 backend/tests/run_embedding_checks.py --live --conversations holdout --run-name rag-held-01
```

脚本只使用自己新建的临时 PostgreSQL 集群，应用全部编号迁移，运行离线检查并检验有缓存数据时
重跑 036 不改变数据。`--live` 额外使用仓库的合成知识库调用本机 EmbeddingGemma 和 MiniMax，
最多三次生成请求，验证缺牙咨询、具体示例报价与寒暄。复用生产提示词、请求体构造、解析和
`groundedReply` 引用校验，不改任何现有训练会话或个人模型设置。脱敏结果在
`backend/build/embedding-live.jsonl`，构建目录已忽略。Windows 继续使用原有 MSVC 构建命令；
新增向量客户端使用 WinHTTP，不新增 OpenSSL 依赖。

多轮探针从 `tests/fixtures/rag_conversations.json` 读取9组连续对话，每轮把实际校验后的客服
答复放入下一轮历史；新对话重置历史，避免互相污染。覆盖价格包含项、代词追问、预算、改问
初诊/总疗程、拆线复查与长期维护区别、症状升级、用药边界、预约限制、情绪交流、指令注入、
冲突和空知识。每轮只做一次真实生成，不用自动重试掩盖首轮失败。

`--run-name` 指定结果名，已存在时拒绝覆盖。构建目录中保存原始/校验后的回答、每轮检索问题、
证据和引用、耗时与 token 用量，以及源码/测试集/二进制 SHA-256 清单。评分脚本生成
`.summary.json`，分别检查模型结构、相关资料召回、引用来源、关键概念、禁用说法、长度和意图；
通用“无法确认”不能作为有依据问题的正确答案。任何失败均返回非零退出码。

```bash
python3 -B backend/tests/evaluate_rag_conversations.py backend/build/rag-dev-01.jsonl --cohort development
```

当前客服提示词使用 `service-reply-rag-v8`，历史消息以带角色的 JSON 数据传入，避免模型模仿
旧客服纯文字回复。检索使用 `embeddinggemma-context-cosine-v4`，只用患者问题补全追问上下文，
不把历史客服输出当作依据。详见 [多轮迭代记录](../docs/validation/rag-multiturn-validation.md)。

### DeepSeek 双角色回归

测试脚本支持 `--provider deepseek`，默认从 `pass` 的 `api/deepseek` 读取密钥，使用
`https://api.deepseek.com` 和 `deepseek-flash`；可以用 `--model` 显式指定账号实际支持的模型。
只有带 `--live` 才会解密和调用模型，离线检查仍然是先决条件。

```bash
python3 backend/tests/run_embedding_checks.py --live --provider deepseek \
  --conversations all --suite backend/tests/fixtures/dual_role_conversations.json \
  --run-name ds-dual-new-run
```

双角色集共75项：标准客服42轮、模拟患者27轮、患者画像初始化6次。患者侧沿用真实
`patientView` 和 `groundedPatientReply`，检查隐藏信息的透露时机、预算与机构报价的区分、
转述/否定承诺、信任及风险状态、角色切换和自然告别。会话实际结束后停止生成，预期中的后续
用例若未到达会记为缺失，不伪造继续对话。探针只复用相同不可变资料的向量缓存，历史、画像、
状态和 contextId 都按对话重置。

每次运行额外保存 `.suite.json` 测试集快照，生成和评分使用同一份快照，避免运行中编辑测试集
改变评分依据。当前患者提示词为 `patient-reply-rag-v4`，检索为
`embeddinggemma-context-cosine-v4`（增加陈述式初诊时长的字段召回）。实测结果及限制见
[DeepSeek 双角色验证](../docs/validation/deepseek-dual-role-validation.md)。
