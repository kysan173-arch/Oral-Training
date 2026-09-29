# 口腔客服智能陪练 API 契约

版本：v3.8（每个账号独立管理 LiteLLM 模型配置）

> RAG v2 仍在分阶段开发。知识发布已生成中文检索块，管理端可预览确定性召回，角色互换已接入服务快照和回答依据；客服训练患者初始化与逐轮回复已接入锁定证据，知识核验评分仍待实现。

Base URL 为 `https://<host>/api`。本机开发可使用 `http://127.0.0.1:8080/api`；体验版和正式版必须使用 HTTPS。

## 1. 通用约定

除健康检查和登录外，所有接口必须携带服务端会话令牌：

```http
Authorization: Bearer <accessToken>
Content-Type: application/json
```

服务端不读取或信任 `X-Demo-User-Id`。成功与失败结构保持统一：

```json
{"code":0,"message":"ok","data":{}}
```

```json
{"code":"SESSION_ABANDONED","message":"已放弃的训练不能结束或恢复","data":null}
```

每个响应包含 `X-Request-Id`。时间为带时区的 ISO 8601 字符串；业务 ID 均为字符串。所有用户输入先去除首尾空白，再按 UTF-8 字符数校验，消息长度为 1—1000 个字符。

## 2. 登录、角色与数据范围

### `POST /auth/wechat`

请求：

```json
{"code":"wx.login 返回的临时 code"}
```

服务端在 `AUTH_MODE=wechat` 时通过微信 `jscode2session` 换取 `openid`，创建或读取本地用户，并返回不透明会话令牌。`AUTH_MODE=demo` 仅供本机和受控局域网使用，同一路径会登录保留的演示用户。

```json
{
  "accessToken":"仅此处返回的令牌",
  "expiresIn":604800,
  "user":{"id":"wx_...","role":"learner","displayName":"微信用户"}
}
```

角色只有：

- `learner`：只能访问自己的场景进度、会话、消息、历史和个人看板。
- `admin`：可读取**自己的团队**的聚合看板及成员学习摘要（完成次数、分数均值、五维均值和训练趋势），但不返回原始对话、报告原文、话术或错题；不能使用训练和个人成长接口。

主管角色只能通过受控的服务端数据库运维流程授予已验证的用户，客户端没有自助提权接口。

本轮不提供多机构租户、排行榜或团队运营接口。

## 3. 健康检查

### `GET /health`

无需登录。响应不包含任务内容、Prompt 或密钥：

```json
{
  "status":"healthy",
  "ready":true,
  "database":true,
  "modelConfigured":null,
  "modelConfigurationScope":"personal",
  "workerRunning":true,
  "workerThreads":1,
  "knowledgeWorkerThreads":1,
  "workersInDatabaseBackoff":0,
  "knowledgeWorkersInDatabaseBackoff":0,
  "pendingJobs":0,
  "deadJobs":0,
  "knowledgePendingJobs":0,
  "knowledgeDeadJobs":0,
  "databasePool":{
    "maximum":12,
    "open":3,
    "idle":3,
    "inUse":0,
    "waiting":0
  },
  "runtimeApiKeyAllowed":false,
  "authMode":"wechat",
  "production":true
}
```

数据库、任务队列或 Worker 不可用时返回 HTTP 503，`ready=false` 且 `status=unhealthy`。个人模型配置不参与公共 readiness：`modelConfigured=null`、`modelConfigurationScope=personal`，个人未配置不会影响其他用户。Worker 进入数据库错误退避期间不会再被报告为健康。`runtimeApiKeyAllowed` 固定为 false，旧内存密钥入口已退役；所有已登录用户都可进入「我的 → LiteLLM 模型配置」。连接池等待超时时，普通接口返回 HTTP 503 `DATABASE_BUSY`。

### LiteLLM 个人模型配置（迁移 023）

`GET /config/litellm`：所有已登录用户仅读取自己的配置，返回：

```json
{"provider":"litellm","scope":"personal","configured":true,"canEdit":true,"baseUrl":"https://llm.example.com/v1","model":"ds-primary","hasApiKey":true,"revision":1}
```

学员和主管都收到 `canEdit=true`；首次未配置时 revision=0。任何响应均不含明文 Key、密文或 Key 尾号。configured 只表示已保存配置，不表示上游连通性已验证。个人配置范围始终来自服务端 bearer 会话，忽略请求体和查询参数中的 userId。

`PUT /config/litellm`：已登录用户修改自己的配置，生产环境继续要求可信 HTTPS。请求：

```json
{"baseUrl":"https://llm.example.com/v1","model":"ds-primary","apiKey":"<LiteLLM 网关 Key>","revision":0}
```

首次保存和修改 Base URL 必须输入 Key；地址不变时省略或留空 apiKey 保留原密钥。revision 使用 GET 获得的个人版本，成功递增；过期版本返回 409 `MODEL_CONFIG_CONFLICT`。字段非法返回 400 `INVALID_ARGUMENT`，未登录返回 401。保存不发起模型请求，不表示连接验证通过。

`DELETE /config/litellm`：清除本人的配置；请求 `{"revision":1}`，递增个人版本并保留配置变更审计，不影响其他账号。本账号后续生成返回 `MODEL_NOT_CONFIGURED`，已有模型调用完成其原配置快照。不会回退到系统变量或其他人的 Key。

配置和不含密钥的变更审计按 user_id 隔离，在同一事务内保存。凭据由 Windows DPAPI 按后端运行账号加密，重启仍有效；迁移机器/运行账号后无法解密时返回 `MODEL_CONFIG_UNREADABLE`，需本人重新保存 Key。旧机构配置及审计原样保留，迁移 023 只向最后保存者复制配置，不向其他用户分发 Key。后台模型任务从数据库中的会话所有者或知识生成任务创建者确定用户。后端不再读取 DEEPSEEK_API_KEY、DEEPSEEK_MODEL、ALLOW_RUNTIME_API_KEY。`POST /config/deepseek-key` 在鉴权后返回 410 `MODEL_CONFIG_MOVED`。

后端调用 `{baseUrl}/chat/completions`（无路径时补 `/v1`），保留公共 Chat Completions 参数和业务 JSON 修复流程，不再发送 DeepSeek 专属 thinking 或固定 user_id。HTTP 仅限回环网关，其他地址要求 HTTPS，禁止携带 URL 用户凭据/查询参数和自动重定向。模型日志含 provider、requestedModel、actualModel、configRevision、usage；modelVersion 记录 `litellm:<模型别名>@<配置版本>`。上游推理参数由 LiteLLM 管理。

## 4. 客服训练

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/scenarios` | 场景、本人最佳分、本人进行中会话、`dimensionFocus`（该场景主要练的维度，按权重降序、带中文名）与 `difficultyTiers`（可选难度档位；空对象 = 该场景没有进阶档，前端就不展示档位入口） |
| `POST` | `/sessions` | 创建会话，body 为 `{"scenarioId":"implant-basic","customPatientProfile":{"gender":"女"},"tier":"advanced"}`；`tier` 选填，缺省 `standard` |
| `GET` | `/sessions` | 本人历史；支持 `status`、`scenarioId`、`category`、`limit` |
| `GET` | `/sessions/{id}` | 会话、完整消息和待恢复输入 |
| `POST` | `/sessions/{id}/restart` | 放弃进行中会话并创建新会话 |
| `POST` | `/sessions/{id}/messages` | 提交客服输入并获取模拟患者回复 |
| `POST` | `/sessions/{id}/hint` | 获取针对当前患者发言的实时提示；每场共 3 条，每轮最多 1 条 |
| `POST` | `/sessions/{id}/finish` | 结束会话并可靠入队评分任务 |
| `GET` | `/sessions/{id}/evaluation` | 获取 `not_started/generating/ready/failed` |
| `POST` | `/sessions/{id}/evaluation/retry` | 仅对失败评分人工重试 |

`GET /scenarios` 返回场景默认 `patientProfile`（`age`、`gender`、`description`），其中 `gender` 默认为 `unknown`，表示场景未限定性别。

### 自定义患者画像

`POST /sessions` 可选传入 `customPatientProfile`，只作用于本次会话（存放在 `sessions.custom_patient_profile`），不会修改共享的场景模板。全部字段选填，白名单外的键一律丢弃。

| 字段 | 类型 | 约束 | 作用 |
|---|---|---|---|
| `description` | string | ≤ 60 字符，超长截断 | 拼进患者开场白「您好，我最近……」，同时是主要诉求来源 |
| `age` | number \| string | 数字或纯数字字符串，归一为数字，1—120 | 覆盖 `patientProfile.age`，并进入模型背景 |
| `emotion` | string | ≤ 8 字符 | 覆盖 `hidden.initialState.emotion`；命中「焦虑/担心/害怕/紧张/犹豫/不安」时开场白走「心里挺X的」句式，平静类中性词不参与拼接 |
| `gender` | string | 仅 `男` / `女` | 覆盖 `patientProfile.gender`；只用于稳住模型人设，不写进开场白文本 |

### 消息情绪标签

`GET /sessions/{id}` 返回的每条 `messages` 项含 `emotion`（string \| null），落在 `messages.emotion`（迁移 `016_message_emotion.sql`）：

- 仅 **AI 患者回复** 有值（`role = "patient"`，含第 0 轮开场白）；学员消息与本次迁移前的历史行均为 `null`。
- 模型生成的回复取值限于 `平静` / `犹豫` / `焦虑` / `缓和`（`normalizePatientReply` 白名单）；开场白可能携带 `customPatientProfile.emotion` 的自由文本（如「烦躁」），故该列为**无 CHECK 约束的自由文本**。
- 前端据此展示逐轮情绪标签：`平静` 中性、`犹豫` 警示、`焦虑` 负向、`缓和` 正向；白名单外的值按中性色**原样展示**，不改写字面。该字段是**逐轮快照**，不随后续轮次变化。

- 净化后没有任何有效字段时按「未提供画像」处理，会话沿用场景默认画像（`custom_patient_profile` 存为 `{}`，`session.customPatientProfile` 回传 `{}`）。
- 开场白在创建会话时生成一次；`POST /sessions/{id}/restart` 会沿用原会话的自定义画像重新生成开场白。
- `GET /sessions/{id}` 的 `session.customPatientProfile` 回传净化后的画像，与提交时一致，前端可直接用于展示。

### 历史记录筛选（`category`）

`GET /sessions` 与 `GET /roleplay/sessions` 均支持 `category`，按**训练大类**筛选本人历史。取值与场景分类（`migrations/007` 的 CHECK 约束）一一对应：

| 值 | 中文 |
|---|---|
| `consultation` | 咨询解答 |
| `price_negotiation` | 价格异议 |
| `complaint_handling` | 投诉安抚 |
| `recommendation` | 项目推荐 |

- 留空 = 不筛选，返回全部；与 `scenarioId` 可叠加，两者同时给出时取交集。
- **非法值返回 400 `INVALID_ARGUMENT`，不静默返回空列表**——静默空结果看起来就像「这段时间没有训练记录」，学员会以为历史丢了，而实际只是筛选值写错了。
- 分类挂在 `scenarios` 表上、会话只存 `scenario_id`，所以服务端用 `scenario_id IN (SELECT id FROM scenarios WHERE category = $1)` 实现（参数化，不是拼字符串）。
- 两类会话共用同一套分类，学员在「客服训练 / 患者模拟」之间切换时筛选行为一致。
- 别与话术锦囊的 `sceneCategory` 混淆：那是同一套分类在**另一个端点**（`/learning/phrases`）上的参数名，两处的取值相同但参数名不同，不要互换。
- 分类中文名的唯一来源是 `reliable_store.h` 的 `sceneCategories()`；前端展示取自 `utils/scenario.js` 的 `CATEGORY_CONFIG`，两边必须同措辞。

### 难度档位（迁移 `026_difficulty_tiers.sql` / `027_advanced_tier_openings.sql`）

`POST /sessions` 可选传入 `tier`：`standard`（缺省）或 `advanced`。会话所用档位由 `GET /sessions/{id}` 与 `GET /sessions` 的 `difficultyTier` 一并返回。

- **档位由学员显式选择，系统不做自动升级。** 悄悄调难度会让分数失去可解释性（这轮 70 分，是我退步了还是患者更凶了？），失败也会被归因成「我是不是变差了」——这是训练系统最不该制造的情绪。
- 覆盖优先级：**场景默认 → 档位 `difficulty_tiers[tier]` → `customPatientProfile`**。学员显式填的画像始终最高，不会被档位改掉。
- 档位覆盖**两样东西**：`initialState`（情绪 / 情绪强度 / 信任度，迁移 `026`）与**开场白 `openings`**（迁移 `027`）。只覆盖前者的话，患者内部状态确实更紧张了、开口说的却还是标准档那句原话——学员听不出难度差别，档位等于只做了一半。
- **`openings` 是数组，每条场景至少 2 句**，由服务端按**会话 id 确定性地**挑一条：同一会话永远同一条（可复现、便于排查），不同会话会看到不同变体。固定单句在第 3 次训练就被背下来了，这个分也就不再代表能力。
- `scenarios.difficulty_tiers` 只存**非默认档**：空对象 = 这条场景没有进阶档，前端就不展示档位入口。场景没有该档定义时传 `tier` **不报错**，按 standard 处理（「没有进阶档」是正常状态，不是学员的错）。
- 非法 `tier` 值返回 400 `INVALID_ARGUMENT`，**不静默降级**——降级会让学员以为自己在挑战进阶档、实际跑的是标准档。
- **主管端可编辑档位**（`POST/PUT /supervisor/scenarios` 的 `difficultyTiers`）：`{advanced: {summary, initialState{emotion, emotionLevel, trustLevel}, openings[0..3]}}`。校验口径与标准档的 `hiddenConfig` 完全一致（同一情绪词表、同一强度/信任度区间、开场白 5-200 字）。`openings` 允许留空（沿用标准档开场白），但保存响应会带 `ADVANCED_OPENING_MISSING` 提醒——台词照旧会让学员以为档位没生效；某句与标准档开场白完全相同则提醒 `ADVANCED_OPENING_DUPLICATE`。
- ⚠️ **进阶档不计入培训计划达标**（`planProgressJoin` 只统计 `difficulty_tier = 'standard'`），只进成长趋势。计划是主管给的硬要求，学员额外挑战不该反噬他。
- **学员端 `GET /scenarios` 下发的 `difficultyTiers` 只含 `summary`**，不含 `openings` 与 `initialState`：前者是患者的台词，把全部变体提前发给学员等于把答案给了他（与「多句变体防背答案」的初衷正好相反）；后者是内部难度参数，前端展示用不到。前端只需要「有没有进阶档」+「它是什么档」这两件事。

### 同场景变体池（迁移 `028_scenario_variants.sql` / `029_scenario_variants_bulk.sql`，零 schema 迁移）

`hidden_config.variants` 是可选的数组，每个元素 `{ "hidden": [...], "instructions": "..." }` 是**部分覆盖**：`hidden`（隐藏顾虑组）与 `instructions`（披露节奏）若给出则覆盖主值，未写字段沿用主值。

- **目的**：同一学员复练同一场景时，患者「藏着什么顾虑、什么条件下才松口」每次不同，背答案就失效——这正是比「难度不够」更该防的分数通胀。
- **选取是 `session_id` 的纯函数**（FNV-1a，与档位开场白同源）：同一会话每轮重算结果一致（患者人设不漂移）、不同会话看到不同变体；不落库、不依赖随机数。
- **作用点只在 `POST /messages` 的患者回复**：隐藏顾虑与披露节奏只进患者提示词，不进任何落库字段，所以没有对应的响应字段。
- 变体池**只存在于数据层**（`hidden_config.variants`），不需要改场景骨架。028 覆盖 2 条示范场景，029 铺满其余 8 条——**当前 10 条上架场景全部各有 2 组**。
- 主管端可见性：`GET /supervisor/scenarios/manage` 的 `items` 附 `hasVariantPool`（布尔，`variants` ≥ 2 组才为 true），管理列表据此标注「未加变体池」；`POST`/`PUT /supervisor/scenarios` 的响应里，变体不足 2 组会带 `NO_VARIANT_POOL` 提醒（只提醒不拦——新场景先上架、后补变体是合理顺序）。
- ⚠️ **编辑场景不会清空变体池**：`hiddenConfig` 在校验里是白名单重建的，`variants` 必须显式透传，且 `updateScenario` 会在请求体没带该键时从库里兜底补回（否则主管改个简介就会把变体静默抹掉）。
- **主管端变体编辑器已做**（`pages/admin-scenarios` 表单的「复练变体池」区，最多 5 组）：每组逐条添加隐藏顾虑 + 可选披露节奏，提交时**显式下发** `hiddenConfig.variants`（含空数组——空数组 = 主管清空了变体池，语义明确；不带该键才是「保持不变」，那是给旧客户端的兼容路径）。结构校验走 `normalizedVariants`：每组 `hidden` 非空、`instructions` 可选（5-400 字）。
- 新增两条保存提醒（只提醒不拦）：`VARIANT_DUPLICATE`（某组变体的隐藏顾虑与主值一字不差 = 等于没加，却让 `NO_VARIANT_POOL` 消失，比没有变体更糟）、`NO_DIMENSION_WEIGHTS`（见下）。

### 维度侧重与编辑器的完整契约（迁移 `024_scenario_dimension_weights.sql`）

`dimension_weights` 决定「学员按弱项复练时这条场景会不会被选中」：`retrainCandidates` 用 `dimension_weights ? $2` 过滤，**未标注的场景是直接消失，不是排在最后**——漏填不报错、页面上也看不出差别。

- `GET /supervisor/plan-dimensions` 下发五维目录（`[{id, name}]`），中文名唯一来源是后端 `planDimensions()`；场景编辑页据此渲染五行权重输入，前端不另写映射副本。
- 表单提交的是主管填的原始数值（填 `3` 与填 `0.6` 等价），归一化在服务端做，并在表单里实时显示折算百分比。
- **维度目录加载失败时前端不下发该键**：发空对象会被归一化成「未标注」，而不发送走的是「保留原值」——两者恰好是这条场景还能不能被推荐选中的差别。

### AI 生成场景骨架（迁移 `030_scenario_ai_draft.sql`）

主管只提供「分类 + 想覆盖的顾虑 + 难度」，模型产出**教学骨架**。分工是这套接口的全部设计：

| 谁写 | 写什么 |
| --- | --- |
| 模型 | 表面诉求与真实顾虑的落差、三条性质错开的隐藏顾虑、可判定的缓和条件、升级条件、施压式开场白、维度侧重 |
| 主管 | **机构事实**：能不能退费、转交谁、多久答复（= `roleplayConfig.serviceGuidance`） |

模型不知道本机构的真实流程，编出来的红线一旦被学员练成肌肉记忆，比没有红线更危险。所以提示词禁止模型填服务要点，`validateGeneratedDraft` 还会**再拒一次**非空的服务要点。

- 走的是与知识库同一套生成队列（`knowledge_admin_jobs`，kind=`scenario_draft`）：幂等键 + 租约 + 重试 + 死信，不新开表也不新开进程。「候选结果隔离」由 `scenarios.generation_id` 承担（非空 = 该任务独占这行）。
- **目标行在提交任务时预建**（`scenarios` 占位行，`is_active = FALSE`、`ai_draft = TRUE`）：任务表要求 `draft_id` 非空且稳定；生成失败（模型连错 3 次）时留下一条已下线空壳，主管一眼看得出「这条没生成出来」。`sort_order` 由服务端按 `MAX(非模板) + 1` 动态分配。
- **生成成功一律 `is_active = FALSE`**：AI 产出未经主管审阅绝不能自动上架。`ai_draft` 是**来源标记、永久保留**（上架后仍为真）——它不表示「未审阅」，未审阅由 `is_active` 表达。
- **生成中的场景不可编辑**（`PUT` 返回 409 `SCENARIO_GENERATING`）：占位内容本来就不满足校验，且放行会让主管刚改的内容被随后写入的 AI 结果覆盖。数据库侧还有 `scenarios_generating_not_active_check` 兜底（`generation_id IS NOT NULL` 的行不许 `is_active = TRUE`）。
- **重新生成必须以当前草稿为输入**（`retry` 从 `scenarios` 行刷新 `currentDraft`），否则主管在编辑器里改过的内容会被一次重试悄悄丢掉；已上架的场景**拒绝重试**（409 `SCENARIO_PUBLISHED`），因为重试成功会强制下线。
- 落库时走 `ReliableDatabase::validateScenarioPayload`——**与主管手工建场景同一套约束**，AI 不是绕过校验的例外通道。校验不过 → 事务回滚 → 任务记为失败并可重试。质量提醒（如 `NO_SERVICE_GUIDANCE`、`NO_VARIANT_POOL`）随 `result.warnings` 一并存档。
- 未知 `kind` 一律拒绝执行：分发是显式枚举（`GenerationTarget` + `parseGenerationTarget`），不存在「不是 A 就是 B」的兜底（RAG 计划 §7.3 / 风险表 R09）。

### 消息幂等与回复租约

请求：

```json
{"clientMessageId":"client-msg-123","content":"我理解您的担忧……"}
```

- 相同 `clientMessageId` 和相同清理后内容返回原消息对。
- 相同 ID 与不同内容返回 `409 IDEMPOTENCY_CONFLICT`。
- 首个请求领取 180 秒回复生成租约。租约有效时，并发请求返回 `409 SESSION_RESPONSE_PENDING`，不会发起第二次模型调用。
- 模型失败或租约过期后，只有相同 ID 和内容可以重新领取。
- `GET /sessions/{id}` 的 `pendingMessage` 包含 `clientMessageId`、`content`、`round`、`replyStatus`；前端应轮询会话，并在超时后保留原 ID 和输入。
- 小程序普通请求超时为 30 秒；两类逐轮模型消息请求单独使用 120 秒，覆盖后端最多两次分阶段模型调用。请求中断后的恢复仍使用原 `clientMessageId`。

最后一轮的患者回复、输入状态 `ready`、会话 `completed`、评分 `generating` 和任务入队在同一数据库事务内提交。

### 结束与重试

- 零轮会话返回 `422 MIN_ROUNDS_NOT_REACHED`。
- `abandoned` 永远不能恢复为 `completed`，返回 `409 SESSION_ABANDONED`。
- 重复结束已完成会话通常只返回当前状态；若报告行或任务行缺失，则原子补建并恢复生成。明确失败的任务不会被隐式重试。
- 失败评分只能调用 `/evaluation/retry` 重新入队。

## 5. 患者模拟（角色互换）

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/roleplay/scenarios` | 场景、建议问题和本人进行中会话 |
| `POST` / `GET` | `/roleplay/sessions` | 创建或查询本人会话；`GET` 支持 `status`、`scenarioId`、`category`、`limit` |
| `GET` | `/roleplay/sessions/{id}` | 会话、消息及待恢复问题 |
| `POST` | `/roleplay/sessions/{id}/restart` | 放弃并重新创建 |
| `POST` | `/roleplay/sessions/{id}/messages` | 提交患者问题，获取标准客服答复 |
| `POST` | `/roleplay/sessions/{id}/finish` | 结束并可靠入队复盘任务 |
| `GET` | `/roleplay/sessions/{id}/summary` | 获取复盘状态或内容 |
| `POST` | `/roleplay/sessions/{id}/summary/retry` | 仅对失败复盘人工重试 |

角色互换使用相同的幂等规则，生成中错误码为 `ROLEPLAY_RESPONSE_PENDING`，放弃错误码为 `ROLEPLAY_SESSION_ABANDONED`。标准客服消息额外包含：

```json
{
  "learningPoints":["学习要点"],
  "complianceBoundary":"具体诊疗判断需由医生结合检查评估。"
}
```

复盘不含数值评分，结构为 `summary`、`coveredTopics`、`keyPrinciples` 和 `nextPracticeSuggestions`。

## 6. 评分规则

每个维度都是 **0—100 的整数**（不接受小数、百分数或 0—1 比值）。`dimensionScores` 必须同时包含全部五个键，键名不可改写、不可嵌套。

总分只使用一次固定五维加权，不再按违规二次扣减：

| 字段 | 权重 |
|---|---:|
| `knowledgeAccuracy` | 25% |
| `medicalCompliance` | 25% |
| `empathy` | 20% |
| `needsDiscovery` | 20% |
| `serviceEtiquette` | 10% |

单项违规 `deduction` 归一到 0—50，仅用于解释。若存在单项扣分达到 30，或全部违规累计扣分达到 30，`medicalCompliance` 不得高于 60；累计扣分达到 60 时不得高于 50。违反该一致性规则的结果会被判定为 `MODEL_SCORE_INCONSISTENT`，由可靠任务机制按策略重试。

## 7. 学员洞察：报告、话术、错题与成长

以下接口仅限 `learner`，且始终按服务端登录用户过滤。管理员不能读取个人话术、错题、成长趋势或会话明细。

评分任务完成时，服务端在同一事务中把两个派生字段写入 `evaluations.report`：

```json
{
  "recommendedPhrases":[{
    "phraseKey":"phrase-1-1",
    "round":1,
    "patientSays":"患者在该轮前提出的关切",
    "csReply":"基于已验证点评的推荐表达",
    "reason":"该表达的练习原因"
  }],
  "learningMistakes":[{
    "mistakeKey":"improvement-1-1",
    "kind":"improvement",
    "priority":"practice",
    "round":1,
    "originalQuote":"学员当时表达",
    "reason":"需要调整的原因",
    "recommendedRewrite":"建议改写"
  }]
}
```

`recommendedPhrases` 从逐轮点评和违规项的已验证改写派生；`learningMistakes` 从违规项和未被违规项覆盖的改进项派生。它们不接受客户端写入，也不触发额外模型调用。已有历史报告在读取时兼容地从原有点评/违规字段提取可用项。

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/learning/phrases` | 本人话术锦囊；支持 `search`、`scenarioId`、`favoritesOnly`、`limit`（1—50） |
| `PUT` | `/learning/phrases/{sessionId}/{phraseKey}/favorite` | 收藏或取消收藏本人报告中的真实话术 |
| `GET` | `/learning/mistakes` | 本人错题；支持 `scenarioId`、`includeMastered=true|false`、`limit`（1—50） |
| `PUT` | `/learning/mistakes/{sessionId}/{mistakeKey}` | 标记或取消标记掌握状态 |
| `GET` | `/learning/profile` | 本人完成次数、`scoredCount/unscoredCount`、平均分、首末分差、逐维非空均值、最近 12 条有分趋势、练习重点和错题掌握数 |
| `GET` | `/learning/mine` | 本人签到积分、当月签到日历、连续天数、含已评分/未评分计数的训练摘要和话术收藏数 |
| `POST` | `/learning/checkins` | 每个中国时区自然日签到一次，固定奖励 +10 积分 |

掌握状态请求：

```json
{"mastered":true}
```

维度分契约不成立时判定为 `MODEL_SCORE_INVALID`，同样交由可靠任务重试，**不会写入 0 分**：

- `dimensionScores` 缺失或不是对象；
- 五个键中任一缺失或不是数字；
- 任一取值超出 0—100；
- 五个维度全为 0；
- 五个维度都落在 0—1 区间（疑似使用了错误量纲）。

重试用尽后 `sessions.evaluation_status` 置为 `failed`，`evaluations.error_type` 记录具体原因，客户端按「报告生成失败，可重新评分」处理。`evaluations.prompt_version` 当前为 `score-prompt-v3`。

`recommendedRewrite` 的形态要求：必须是客服能直接对患者说出口的完整话术原句（20—100 个中文字符，带称谓、句子完整），不得写成「先安抚，再追问主诉」这类要点、提纲或动作说明。该字段经 `recommendedPhrases` 派生为结果页的「本次可收录话术」和话术锦囊条目，因此形态直接决定锦囊的可用性。

## 7. 学员洞察：报告、话术、错题与成长

以下接口仅限 `learner`，且始终按服务端登录用户过滤。管理员不能读取个人话术、错题、成长趋势或会话明细。

评分任务完成时，服务端在同一事务中把两个派生字段写入 `evaluations.report`：

```json
{
  "recommendedPhrases":[{
    "phraseKey":"phrase-1-1",
    "round":1,
    "patientSays":"患者在该轮前提出的关切",
    "csReply":"基于已验证点评的推荐表达",
    "reason":"该表达的练习原因"
  }],
  "learningMistakes":[{
    "mistakeKey":"improvement-1-1",
    "kind":"improvement",
    "priority":"practice",
    "round":1,
    "originalQuote":"学员当时表达",
    "reason":"需要调整的原因",
    "recommendedRewrite":"建议改写"
  }]
}
```

`recommendedPhrases` 从逐轮点评和违规项的已验证改写派生；`learningMistakes` 从违规项和未被违规项覆盖的改进项派生。它们不接受客户端写入，也不触发额外模型调用。已有历史报告在读取时兼容地从原有点评/违规字段提取可用项。

分类筛选（`sceneCategory`）在 SQL 里、在 `limit` 截断之前生效。若改成前端按每条话术的 `category` 字段筛，会退化为「先截断再筛选」：训练量大的学员，50 条上限被单一分类占满后，其他分类会显示出少于实际的结果。

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/learning/phrases` | 本人话术锦囊；支持 `search`、`scenarioId`、`sceneCategory`（`consultation`/`price_negotiation`/`complaint_handling`/`recommendation`，非法值 400 `INVALID_ARGUMENT`）、`favoritesOnly`、`limit`（1—50）。响应附 `sceneCategory`（生效值）与 `sceneCategories`（`{id,name}` 分类目录，前端中文名以此为准） |
| `PUT` | `/learning/phrases/{sessionId}/{phraseKey}/favorite` | 收藏或取消收藏本人报告中的真实话术 |
| `GET` | `/learning/mistakes` | 本人错题；支持 `scenarioId`、`includeMastered=true|false`、`limit`（1—50） |
| `PUT` | `/learning/mistakes/{sessionId}/{mistakeKey}` | 标记或取消标记掌握状态 |
| `GET` | `/learning/mistakes/{sessionId}/{mistakeKey}/context` | 错题「复现原回合」上下文：错题详情（含 `mastered`）+ 患者该回合提问原话（`patientQuestion`，取自 `messages` 表）+ 学员当时发言（`originalAnswer`）+ 场景画像与自定义画像。错题必须存在于该会话报告，否则 404 `LEARNING_MISTAKE_NOT_FOUND`；回合消息缺失 404 `MISTAKE_ROUND_NOT_FOUND` |
| `POST` | `/learning/mistakes/{sessionId}/{mistakeKey}/retrain` | 单回合复练点评：`{answer}`（1—1000 字）→ 同步调用模型返回 `{passed, comment, recommendedRewrite}`。`passed` 缺失或非法按 `false` 处理；不给五维分数（单回合覆盖不了五维，避免偏离整场权重口径） |
| `GET` | `/learning/profile` | 本人完成次数、平均分、首末分差、五维均值、最近 12 条趋势、练习重点和错题掌握数 |
| `GET` | `/learning/retrain-candidates?dimension={五维key}` | 弱项定向复练的场景候选。**只返回该维度权重 > 0 的场景**（未标注的场景被排除，而不是排在最后——排进候选等于告诉学员「这条也能练」），按 `dimension_weights->>dimension` 降序、同权重按 `sort_order`。每项附 `dimensionWeight`、`dimensionFocus`（该场景整体练哪些维度，用于解释「为什么推荐它」）、`completedCount`、`avgDimensionScore`、`activeSession`。`dimension` 缺失或非法一律 400（静默返回空列表会把「调用方写错 key」掩盖成「系统没数据」） |

**为什么这个排序不看学员的历史分**：候选顺序只由「该场景练这个维度的比重」决定。若把学员在该场景的历史分也掺进排序，同一份弱项对不同学员会给出不同顺序，主管和学员都问不出「为什么这条排前面」；而「这个场景重点练这个维度」是场景自身的属性，不该因谁在练而变。历史分只作展示：`avgDimensionScore` 为 `null` 表示**还没练过**，前端必须如实显示「还没练过」，不能显示 0 分（0 分会被读成「练过但很差」）。`activeSession` 非空时前端应引导「继续训练」而不是新建——同一场景的进行中会话有唯一约束，直接新建会被拒。

未标注维度权重（`{}`）的场景不会出现在候选里，这是刻意的：无法判断它练不练该维度，就不能推荐。所以**新建场景若不填 `dimensionWeights`，将永远不会被「按弱项推荐」选中**（见上文场景 ↔ 维度映射）。
| `GET` | `/learning/mine` | 本人签到积分、当月签到日历、连续天数、训练摘要和话术收藏数 |
| `POST` | `/learning/checkins` | 每个中国时区自然日签到一次，固定奖励 +10 积分 |

掌握状态请求：

```json
{"mastered":true}
```

服务端先验证该 `sessionId/mistakeKey` 是当前用户已完成报告的真实派生项，再写入 `learner_mistake_progress`。取消掌握不会删除报告或错题来源，只会将 `mastered_at` 置空并保留更新时间。

提示调用模型生成，输入是场景公开信息、患者当前状态、完整对话、**患者当前这一轮的发言原话**和学员上一轮回答，因此每一轮的提示都随对话内容变化，不是固定话术。总量上限为每场 3 条、每轮最多 1 条，两个上限都在写入 `session_hints` 的事务内裁定：唯一键 `(session_id, round)` 保证同一轮不可能落库第二条。第 0 轮（尚未回复患者开场白）不允许取提示，先返回 409 `HINT_ROUND_NOT_READY`。模型输出经与评分建议同一套合规校验，命中未经验证的医疗或价格信息时替换为合规兜底话术。提示只给出沟通步骤与医疗合规边界，不给出诊断、用药、疗效、固定价格或疗程结论。话术收藏同样先验证 `sessionId/phraseKey` 来自当前用户的已完成报告，再写入偏好记录。

读取会话时同时返回提示状态：`hintRemaining`（总剩余，初始 3）、`hintRound`（当前轮次）、`hintRoundLimit`（每轮上限 1）、`hintRemainingThisRound`（本轮还剩几条）。前端据此判断按钮可用性，无需自行推算。

积分仅来自每日签到，固定为 +10；不提供训练奖励、兑换、排行榜或其他积分来源。

## 8. 可靠 AI Worker

API 和 Worker 运行在同一个便携程序中。Worker 默认并发 1，可配置到 4；使用 `FOR UPDATE SKIP LOCKED` 领取任务，租约 180 秒。去重键为：

- `evaluation:{sessionId}`
- `roleplay-summary:{sessionId}`

瞬时错误最多尝试 3 次，第一次失败后等待 5 秒，第二次失败后等待 30 秒。未配置模型、鉴权失败、内容过滤或不安全输出等非瞬时错误直接进入 `dead` 并把业务状态置为 `failed`。Worker 会回收过期租约；数据库中断时在进程内退避，异常不会逃出线程。

已完成会话的结束接口和报告轮询都会核对业务状态、报告行与任务行。缺失状态会在同一事务中补建，终态但无报告的任务会开启新 generation；达到 generation 上限时返回明确失败状态，不会永久停留在 `generating`。

小程序收到 `not_started` 时不会无限轮询：已完成会话会重新调用幂等结束接口恢复任务，进行中会话返回训练页，已放弃会话返回历史记录。训练、患者模拟及两个结果页缺少 `sessionId` 时都会明确提示并安全导航。

## 9. 看板、主管聚合与成员摘要

### `GET /dashboard/summary`

`scope` 为 `personal` 或 `institution`。学员收到个人统计和最近 5 条本人会话；管理员收到单机构聚合，`recentSessions` 为空，避免泄露个人会话。

完成报告允许没有综合分。聚合响应以 `completedSessions` 统计所有已完成且报告为 `ready` 的训练，并同时返回 `scoredSessions` 与 `unscoredSessions`。平均分、达标率、最佳分和趋势仅使用 `totalScore` 非空的报告；没有已评分样本时平均分和达标率返回 JSON `null`。`dimensionAverages` 的每个维度独立排除该维度的 `null`，没有样本的维度返回 `null`。

`scenarioStats` 逐场景下发 `{scenarioId, scenarioName, category, trainingCount}`；`category` 取自 `scenarios.category`（`consultation` / `price_negotiation` / `complaint_handling` / `recommendation`），供学员数据页把次数归并到四大分类。

仅 `admin` 可以调用。**所有聚合口径都限定在「我的团队」范围内**（`supervisor_team_members`），未加入团队的学员不会出现在看板、成员列表、报表与排行榜中：

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/supervisor/dashboard?range=week\|month\|quarter\|all` | 本团队学员数、训练量、达标率、场景聚合、五维均值和趋势。`scenarioStats` 逐场景下发（每项带 `category`），主管端展示层据此归并到四大分类 |
| `GET` | `/supervisor/members?limit=1..100` | `admin` | 本团队成员学习摘要，按姓名展示，不按成绩排序；附 `totalTeamMembers`（不受 `limit` 影响的团队成员总数，用于判断列表是否被截断）与 `totalLearners`（全部在职学员数，含已归属其他主管的人）。分档字段（迁移 026）：`standardAvgScore` / `advancedAvgScore`（该档**没练过时为 `null`，前端须显示「未挑战」而不是 0**）与 `advancedCount` |
| `GET` | `/supervisor/members/{memberId}` | `admin` | 本团队单个成员的五维均值、弱项建议和最多 12 条训练分数趋势；非本团队成员返回 `MEMBER_NOT_FOUND`。分档字段同上，另附 `advancedChallengeRate`（进阶完成数 / 已完成报告数，百分之一位；无已评分训练时为 `null`）；`trend` 每点带 `difficultyTier`（`standard`/`advanced`）——**混档趋势不可比，前端默认只画标准档** |
| `GET` | `/supervisor/scenarios` | 场景目录（`id`、`name`、`category`、`difficulty`），供发布培训计划时选择适用场景 |
| `GET` | `/supervisor/plan-dimensions` | `admin` | 训练计划可指定的目标维度目录（`id` 五维 key + `name` 中文名）。中文名的唯一来源是 `reliable_store.h` 的 `planDimensions()`，前端不得另建副本 |
| `GET` | `/supervisor/scenarios/manage` | `admin` | 场景管理全量目录。`items` = 可运营场景（含已下架，便于重新上架；**不含任何模板行**），每项附 `dimensionWeights`（原始权重对象，供编辑表单回填）、`dimensionFocus`（同一份数据的展示形态 `[{key, name, weight}]`，已带中文名并按权重降序）与 `difficultyTiers`（**完整档位含 `openings`**，供编辑；学员端走 `/scenarios` 的 `tierSummaryForLearner` 只拿 `summary`，两处不要混用）；`templates` = 骨架模板（迁移 025），**返回完整字段**供前端预填整张表单，与 `items` 是两段互不重叠的数据 |
| `POST` | `/supervisor/scenarios` | `admin` | 新建场景；`id` 选填（留空自动生成 `sc-<毫秒时间戳>`）。可选 `fromTemplateId`：指定骨架模板时先以模板字段为基底、再由请求体逐键覆盖，**模板的 `id` 与 `sortOrder` 一律丢弃**（否则会撞 id 与唯一索引）；模板不存在返回 `TEMPLATE_NOT_FOUND`。可选 `dimensionWeights`：只接受五维 key，非数字/零/负数丢弃，其余**归一化到 1.0** 后落库；全零或无有效项存为 `{}`（= 未标注）。可选 `difficultyTiers`（见下方「难度档位」）。响应含 `warnings` |
| `PUT` | `/supervisor/scenarios/{id}` | `admin` | 编辑或上下架场景，只覆盖请求体里出现的键。`dimensionWeights`、`difficultyTiers` 不提交即保留原值；**`difficultyTiers` 提交 `{}` 是显式清掉进阶档**。`hiddenConfig.variants` 显式提交（空数组 = 清空变体池）。响应含 `warnings`（档位：`ADVANCED_OPENING_MISSING` / `ADVANCED_OPENING_DUPLICATE`；变体：`NO_VARIANT_POOL` / `VARIANT_DUPLICATE`；维度：`NO_DIMENSION_WEIGHTS`）。AI 生成中的场景返回 409 `SCENARIO_GENERATING` |
| `POST` | `/supervisor/scenarios/ai-draft` | `admin` | 提交 AI 骨架生成任务（202）。请求体 `{category, difficulty, concerns[1..5], name?, brief?}` + `Idempotency-Key`（或 body 内 `idempotencyKey`）；返回任务对象，`draftId` 即预建的占位场景 id。未知 kind、无效分类/难度、空顾虑返回 400 |
| `GET` | `/supervisor/scenarios/ai-draft/{jobId}` | `admin` | 轮询任务状态（`pending` / `running` / `retry_wait` / `succeeded` / `dead`）。`result` 含 `candidate`、`applied`、`scenarioId`、`warnings` |
| `POST` | `/supervisor/scenarios/ai-draft/{jobId}/retry` | `admin` | 仅 `dead` 任务可重试（202）。**已上架的骨架返回 409 `SCENARIO_PUBLISHED`**——重试成功会强制下线，那等于把已发布场景撤下来 |
| `GET` | `/supervisor/reports/forbidden-phrases?range=week\|month\|quarter\|all&sceneCategory=…&limit=…` | `admin` | 违规表达按类型聚合。`phrases` **只含有命中的分类**，每项带 `category`、`categoryLabel`、`count`、`memberCount`；`countedViolations` 是全量次数；`violationCategories` 是完整分类目录（前端据此渲染，不得另建中文名副本）；`sceneCategories` 是另一条**正交**的筛选轴 |
| `GET` | `/supervisor/reports/forbidden-phrases/{category}?range=…&limit=1..200` | `admin` | 该分类下涉及的成员，按违规次数降序。每名成员带 `entries`：**最多 5 条**违规明细（`round` / `originalQuote` / `reason` / `recommendedRewrite` / `deduction` / `scenarioName` / `date`），按扣分降序。`count` 是该分类下的**完整**违规条数——超出 `entries.length` 时前端必须如实标注「仅展示扣分最高的 N 条」，不要把它读成全部 |

**违规分类口径（重要）**：分类**预设为固定 5 类且不落库**——评分模型在 `violations[].type` 里填写的是**自由文本**，查询时由 `violationCategorySql()`（`reliable_store.h`）用关键词正则归并到 `efficacy_guarantee` / `overreach_judgement` / `risk_mishandling` / `peer_disparagement` / `other`。因此：

- **改词表会即时重算历史统计**（每次查询现算，没有口径快照）；`CASE` 首命中优先，**调 WHEN 顺序等于调口径**。
- `other` 是兜底类，**没有占比监控**：模型换一种词表里没有的措辞（或直接吐出维度 key，如 `knowledgeAccuracy`）就整批掉进去，统计会静默失真。改动前先量一下 `other` 占比。

**场景 ↔ 维度映射（迁移 024）**：`scenarios.dimension_weights` 记录「该场景主要练哪几个维度」，是「按弱项推荐场景」的前提——此前只有自由文本的 `focus`（如 `["需求挖掘","引导专业检查"]`），与五维 key 之间没有任何映射，任何形式的推荐都只能靠拍脑袋。

- key 与 `planDimensions()` 同一套；中文名由后端经 `dimensionFocus[].name` 下发，**前端不得再维护一份 key→中文名映射**。
- 权重表示「该场景在多大程度上练这个维度」，同一场景各值之和为 1.0，因此可跨场景比较（「这条场景 45% 在练合规」）。
- **只写非零维度，不写 0、不补零。** 「空对象 `{}`」与「某维度权重为 0」是两种不同语义：前者是**尚未标注**，后者是**不练**。推荐逻辑必须**跳过** `{}`，既不能把未标注当成全维度均等，也不能把它排在最后充数。

**场景质量软校验（迁移 025 一并落地）**：创建与编辑场景的响应带 `warnings: [{code, message}]`，内容是《训练场景设计规范》六条判据里**机器能判**的部分——`HIDDEN_COUNT`、`OPENING_IS_QUESTION`、`NO_RELIEF_CONDITION`、`NO_ESCALATION_CONDITION`、`NO_EVASION_RULE`、`NO_SERVICE_GUIDANCE`、`NO_RED_LINE`、`PLACEHOLDER_NOT_REPLACED`（实现在 `reliable_store.h` 的 `scenarioQualityWarnings()`）。

- **只提醒、不拦截**：保存照常成功。判据 1（诉求与顾虑的落差）和判据 2 的「性质错开」需要语义理解，硬拦会把好场景挡在门外。
- 前端**不要用 `showModal` 展示**：单条 message 有 60-100 字，弹窗会截断，而它们恰恰是最该被读完的内容。主管端改用列表页顶部的可关闭提示条。
- `NO_RED_LINE` 是判据 6 的替代指标——机器判不出「有没有那个诱人的坑」，但**没写红线的场景必然没有坑**。
- 回填范围：只回填 `is_active` 且非模板的场景；已下架场景与 `is_template` 模板行保持 `{}`（模板的维度倾向由主管「以此新建」时决定）。
- 服务端归一化的原因：让「权重可跨场景比较」这个不变式不依赖调用方守规矩——主管填 `3/1/1` 与填 `0.6/0.2/0.2` 应当存成同一个东西。


看板的 `scenarioStats` 与训练场景目录一致，仅列出启用且非模板的场景，保留训练次数为零的场景；总览计数仍包含所选时间范围内的团队历史训练，因此不要求等于场景行之和。

主管接口不会返回消息、原始患者内容、报告全文、错题或话术；不包含任务指派（培训计划见第 11 节）。

旧客户端的 `GET /api/supervisor/members/:memberId/sessions/:sessionId` 始终拒绝管理员读取，返回 HTTP 403 / `LEARNER_CONTENT_PRIVATE`，不查询会话是否存在。成员摘要不再提供 `inspectSessions`。成员均分、通过率和每维均值无有效样本时为 null，返回 `scoredSessions` / `unscoredSessions` 区分有无总分；趋势仅纳入有总分的记录。

## 10. 我的团队

主管与学员的归属关系存放在 `supervisor_team_members`（迁移 `015_supervisor_team.sql`）。**`learner_id` 是主键，因此一名学员最多隶属一名主管**；候选人只包含「在职且尚无归属」的学员，已在其他主管团队里的学员不会出现在候选名单中。

移出团队**只解除归属**：学员账号、全部训练记录与历史报告一律保留，可被重新加入；同时在同一事务内删除其在本主管**未到期**计划中的指派行（已到期计划保留指派行以便回溯）。团队成员变动后，看板与培训计划口径随之下一次读取即时生效，无需重启。

| 方法 | 路径 | 角色 | 说明 |
|---|---|---|---|
| `GET` | `/supervisor/team/members?limit=1..100` | `admin` | 团队成员列表（与 `/supervisor/members` 同源），附 `totalTeamMembers`、`totalLearners` |
| `GET` | `/supervisor/team/candidates?limit=1..100` | `admin` | 可添加学员（在职且无归属），附 `totalCandidates`、`totalTeamMembers` |
| `POST` | `/supervisor/team/members` | `admin` | 添加成员；请求体 `learnerIds`（字符串数组，去重，最多 500）。只插入在职且当前无归属的学员；**一名都没插入时返回 400 `TEAM_MEMBER_UNAVAILABLE`**（而非 `addedCount=0` 的伪成功）。成功返回 `addedCount`、`skippedCount` |
| `POST` | `/supervisor/team/members/{learnerId}/remove` | `admin` | 移出成员；返回 `removed`、`clearedAssignments`（同事务删除的未到期计划指派数）。该学员不属于当前主管时返回 404 `TEAM_MEMBER_NOT_FOUND` |

## 11. 培训运营

培训计划由主管发布。**发布对象是主管自己的团队成员**（`supervisor_team_members` 中 `supervisor_id = 当前主管` 的在职学员）；请求体带非空 `targetUserIds` 时只指派给这些学员，不在本团队或已停用的 id 会被过滤，若过滤后一个可用学员都不剩则返回 400 拒绝发布；团队成员为空且未指定学员时返回 400 `TEAM_EMPTY`，避免「发布成功但零指派」。进度**不落计数器**，每次读取时按 `[created_at, due_at]` 窗口实时聚合已完成且评分就绪的 `sessions`，因此发布后新加入团队的学员会自动出现在名单里，也不存在计数漂移。到期与否由 `due_at <= NOW()` 派生，没有 `status` 列。

| 方法 | 路径 | 角色 | 说明 |
|---|---|---|---|
| `POST` | `/supervisor/training-plans` | `admin` | 发布计划；请求体 `title`(1-100)、`period`(`week`/`month`)、`dueAt`、`requiredCount`(1-20)、`requiredPassRate`(0-100)、`description`(≤500)、`scenarioIds`（空数组=全部场景）、`targetUserIds`（空数组=全团队成员，最多 500 个去重 id）、`focusDimension`（选填，目标维度 key，空串=不限）、`maxPerScenario`（选填，0-10，**0=不限**，见下）、`requireEachPass`（选填，布尔，**默认 false**，见下「逐次达标」）；返回新建计划、`assignmentCount`、`targeted`、`requestedCount`、`skippedCount` |
| `GET` | `/supervisor/training-plans?status=all\|active\|expired` | `admin` | 本人发布的计划列表，附 `assignmentCount`、`doneCount`、`avgScore`（统计时按当前团队过滤一次，与成员列表口径一致）、`focusDimension`/`focusDimensionLabel`、`focusAvgScore`、`maxPerScenario`、`requireEachPass`、`scoreBasis`（`dimension`/`total`） |
| `GET` | `/supervisor/training-plans/{planId}` | `admin` | 计划详情与逐学员进度（`completedCount`、`avgScore`（综合分）、`focusAvgScore`、`score`（判定实际用的分）、`scoreBasis`、`lastTrainingDate`、`done`、`minScore`/`minFocusScore`/`lastScore`（诊断信号，`null`=窗口内无有效训练））；`plan` 内附 `focusDimension`/`focusDimensionLabel`、`maxPerScenario` 与 `requireEachPass`——页面必须据此说明判定依据，不能拿 `avgScore` 去核对结论 |
| `POST` | `/supervisor/training-plans/{planId}/notify` | `admin` | 标记已提醒并回传未完成名单，供前端复制；当前无订阅消息通道 |
| `POST` | `/supervisor/training-plans/suggest` | `admin` | 按学员五维薄弱项让模型生成计划**草稿**（`status='draft'`，不写指派行、学员完全不可见）；请求体可选 `learnerIds`（空=全团队成员，单次上限 10 人）；返回 `generatedCount` 与 `skipped[{learnerId,reason}]`（单人失败不拖垮整批）。候选池喂 `id`/`name`/`category`/`summary`/`difficulty`/`focus`/`dimensionFocus`（**不含** `hidden_config` 患者剧本与 `roleplay_config` 参考答案）。**模型返回的 `focusDimension` 必须能在它所选的场景里练到**，否则该学员记入 `skipped` 并不产出草稿（见下） |
| `GET` | `/supervisor/training-plan-drafts` | `admin` | 本人待审核的 AI 草稿列表，附 `focusDimension`/`focusDimensionLabel`、`rationale`、`scenarioIds`、`learnerName`、`dueAt` |
| `POST` | `/supervisor/training-plans/{planId}/publish` | `admin` | 采纳草稿：`draft` → `published` 并写入指派行。请求体可带 `title`/`description`/`scenarioIds`/`requiredCount`/`requiredPassRate`/`dueAt`/`focusDimension`/`maxPerScenario`/`requireEachPass` 覆盖字段，即「编辑后发布」；空对象即原样采纳 |
| `POST` | `/supervisor/training-plans/{planId}/dismiss` | `admin` | 丢弃草稿：`draft` → `dismissed`（软删，保留采纳率审计） |
| `GET` | `/learning/training-plans` | `learner` | 本人被指派计划的进度，附 `completedCount`、`avgScore`（综合分）、`focusDimension`/`focusDimensionLabel`、`focusAvgScore`、`score`（判定实际用的分）、`scoreBasis`、`maxPerScenario`、`requireEachPass`、`minScore`/`minFocusScore`/`lastScore`、`status`(`pending`/`done`/`expired`) 与 `pendingCount` |

进度口径：只统计**客服训练**（`sessions`）的完成次数与平均分，**不含患者模拟**（`roleplay_sessions` 无评分，无法参与「最低平均分」判定）。`scenarioIds` 非空时按 `scenario_id` 过滤训练记录。

**达标判定按目标维度分流**：计划未指定 `focusDimension`（空串）时，用综合均分 `AVG(sessions.total_score)` 与 `requiredPassRate` 比较，与历史计划语义一致；指定了目标维度时，改用该维度均分 `AVG(evaluations.report->'dimensionScores'->>focusDimension)` 比较——目的是让「针对弱项的计划」真的按弱项达标，避免学员靠其他维度拉高加权总分蒙混过关。该维度**没有任何有效评分**（历史报告缺键等）时回退综合分，**绝不把缺失当 0 分**——否则会凭空造出不达标。四处判定（主管列表、计划详情、CSV 导出、学员端列表）共用同一段 SQL 与同一个分数选择函数，任一处单独改都会出现「主管看到达标、学员看到未达标」。响应里 `avgScore` 恒为综合分，判定实际用的分在 `score` 字段，前端展示与比较都必须用 `score`。

**回退必须对主管可见**：四处响应都带 `scoreBasis`——`dimension` = 按目标维度判定；`total` = 该维度无有效评分、已回退综合分。回退本身是有意设计（缺失不当 0 分），但它不能让主管在「以为按 A 考、实际按 B 判」的情况下看到一切正常，因此主管列表、计划详情、CSV 导出与学员端四处**都必须下发该字段**，前端在 `total` 且计划指定了维度时要显式写出判定依据。

**AI 草稿的目标维度必须与所选场景自洽**（堵住又一处静默回退）。`focusDimension` 决定达标判定口径；如果计划指定的维度在所选场景里根本练不到，该维度就永远不会有有效评分，判定会静默回退综合分——主管以为在考核目标维度，实际按综合分判。因此：

- 候选池向模型下发每个场景的 `dimensionFocus`（`[{key,name,weight}]`），模型据此挑场景，不再从自由文本 `focus` 里猜「这个场景练不练某个维度」。
- 模型返回后由服务端**校验**：非空的 `focusDimension` 必须出现在所选场景的 `dimensionFocus` 中（权重 > 0）。不满足时该学员记入 `skipped`（`reason` 写明该维度不可练、以及所选场景实际可练的维度），**不产出草稿**——既不静默改写维度、也不静默清空维度。
- 校验放在**落库边界**（`createAiPlanDraft`，错误码 `PLAN_DIMENSION_NOT_TRAINABLE`）而不是模型网关内部：这样任何模型网关（含将来接入的微调模型）都绕不过去。写在某个网关实现里只会保护那一条路径。
- 空的 `focusDimension` 是**合法降级**（= 不限维度、按综合分判定），不拦。提示词里明确告知模型这是允许的出路，否则它会为了满足约束而硬填一个练不到的维度。
- 所选场景**尚未标注维度权重**（`{}`）时同样拒绝，`reason` 提示先去场景管理里标注——无法验证就不能假装已验证。
- 这条约束以前只写在提示词里、**没有任何机制检查**，模型违反了也无人知晓；现在是服务端强制。

**防刷分：同一场景最多计入 N 次**（`maxPerScenario`，迁移 023）。计划可以不指定场景、只给「目标维度 + 分数 + 次数」，学员因此可以反复练最容易的那一个场景，把完成次数与维度均分一起刷上去——计划看着闭环，训练量却没铺开。`maxPerScenario` 非 0 时，同一 `scenario_id` 只按**最早**的 N 次计入完成数与均分，其余次数必须靠其他场景补齐；`0` = 不限（存量计划全部为 0，语义不变）。取「最早」而不是「最近」是刻意的：取最近 N 次时，学员在已达标的场景上多练一次（且这次分数更低）就会把原来的好成绩顶出统计，计划可能由达标翻回未达标，等于惩罚额外训练；取最早 N 次则多练只是不计入，永不倒扣。该约束与达标判定同在四处共用的 `planProgressJoin()` 里实现，改一处即四处生效——但也因此必须逐处回归。CSV 导出（`scope=plan_members`）附「目标维度 / 判定分 / 判定依据 / 同场景次数上限 / 综合均分」列，判定分与「是否达标」同源。

**逐次达标：窗口内最低分 ≥ 达标线**（`requireEachPass`，迁移 031）。均分会掩盖尾部风险——真实数据里出现过 `75 + 52`（均分 63.5）按 60 分线算「达标」，而该学员第二次只有 52 分。计划开启 `requireEachPass` 后，判定分改取**窗口内最低分**（`MIN`）而不是均分，要求每一次都过线；`false`（默认）保持原均分口径，存量计划语义一字不变。回退规则与均分口径完全一致：目标维度无有效评分时退到综合最低分，缺失绝不当 0 分。

**为什么用「最低分」而不是「最后一次」**：判定与 `maxPerScenario` 共用同一批行（先按场景截取**最早** N 次再聚合），所以多练不会把已有成绩顶出统计、永不倒扣。若改看「最后一次」，学员在已达标的场景上多练一次失手就会翻回未达标——等于因为多练而受罚。真实数据也印证了这点：某学员 `91/0/5/79` 均分 43.8（不达标）但最后一次 79（达标），「看最后一次」反而会**放松**标准。开关由主管显式选择，系统不偷偷改判定口径（与 D1「不做自动升级」同一条原则）。

场景分类 `category` 的取值与中文名为 `consultation` 咨询解答、`price_negotiation` 价格异议、`complaint_handling` 投诉安抚、`recommendation` 项目推荐，与 `migrations/007` 的 CHECK 约束一致，也与学员端训练页展示的分类名一致。

## 12. 错误码

| HTTP | code | 含义 |
|---:|---|---|
| 400 | `INVALID_ARGUMENT` | 参数、JSON、UTF-8 或字符长度无效 |
| 400 | `HTTPS_REQUIRED` | 生产入口未通过 HTTPS 代理 |
| 400 | `TEAM_MEMBER_UNAVAILABLE` | 添加成员时一名都没插入成功（均已停用或已属于其他主管） |
| 400 | `TEAM_EMPTY` | 团队暂无成员，无法按全团队发布培训计划 |
| 401 | `AUTH_REQUIRED` / `AUTH_INVALID` / `AUTH_EXPIRED` | 缺少、无效或过期令牌 |
| 401 | `WECHAT_LOGIN_FAILED` | 微信 code 无效或过期 |
| 403 | `ROLE_FORBIDDEN` | 角色无权访问该类接口 |
| 403 | `ORIGIN_FORBIDDEN` | Origin 不在精确允许列表 |
| 400 | `HTTPS_REQUIRED` / `FORWARDED_HEADER_INVALID` | 请求未由可信 HTTPS 代理转发，或代理头格式无效 |
| 404 | `SCENARIO_NOT_FOUND` | 场景不存在 |
| 404 | `SESSION_NOT_FOUND` / `ROLEPLAY_SESSION_NOT_FOUND` | 会话不存在或不属于本人 |
| 404 | `LEARNING_MISTAKE_NOT_FOUND` | 错题不存在、不属于本人或不再是当前报告的派生项 |
| 404 | `LEARNING_PHRASE_NOT_FOUND` / `MEMBER_NOT_FOUND` | 话术或成员不存在、无权访问（成员详情还要求属于当前主管团队） |
| 404 | `TEAM_MEMBER_NOT_FOUND` | 该学员不在你的团队中，无法移出 |
| 404 | `TRAINING_PLAN_NOT_FOUND` | 培训计划不存在 |
| 409 | `IDEMPOTENCY_CONFLICT` | 同一幂等 ID 对应不同内容 |
| 409 | `SESSION_RESPONSE_PENDING` | 模拟患者回复租约有效 |
| 409 | `ROLEPLAY_RESPONSE_PENDING` | 标准客服回复租约有效 |
| 409 | `SESSION_ABANDONED` | 客服训练会话已放弃 |
| 409 | `ROLEPLAY_SESSION_ABANDONED` | 患者模拟会话已放弃 |
| 409 | `SESSION_IN_PROGRESS` / `ROLEPLAY_SESSION_IN_PROGRESS` | 同场景已有进行中会话 |
| 409 | `EVALUATION_NOT_RETRYABLE` / `ROLEPLAY_SUMMARY_NOT_RETRYABLE` | 当前任务不可人工重试 |
| 409 | `AI_JOB_GENERATION_EXHAUSTED` | 任务 generation 已达到人工重试上限 |
| 409 | `HINT_LIMIT_REACHED` | 本次训练的三条提示已经用完 |
| 409 | `HINT_ROUND_LIMIT_REACHED` | 本轮已经获取过提示，回复患者后可在下一轮继续 |
| 409 | `HINT_ROUND_NOT_READY` | 尚未回复患者，没有可针对的当前轮次 |
| 422 | `MIN_ROUNDS_NOT_REACHED` | 尚未完成一轮 |
| 429 | `RATE_LIMITED` | 用户/IP 速率超限 |
| 503 | `DATABASE_BUSY` | 数据库连接池已满且等待超时 |
| 503 | `MODEL_NOT_CONFIGURED` / `MODEL_AUTH_FAILED` | 模型配置不可用 |
| 503 | `MODEL_TIMEOUT` / `MODEL_RATE_LIMITED` / `MODEL_ERROR` | 模型瞬时错误 |
| 503 | `MODEL_INVALID_RESPONSE` / `MODEL_SCORE_INCONSISTENT` | 模型结果无效或评分矛盾 |
| 503 | `REPORT_INVALID` | 已存储评分报告格式无效，无法生成学习洞察 |

## 13. 兼容与安全边界

现有训练成功响应结构保持兼容。学员洞察字段在服务端对已规范化报告进行派生；评分 Prompt 记录为 `score-prompt-v3`。v3.8 使用个人 LiteLLM 配置，具体配置及参数边界见第 3 节，业务响应解析与模型内部修复重试保留。生产环境必须设置 `PRODUCTION=true`、`AUTH_MODE=wechat`、HTTPS `ALLOWED_ORIGIN`、`REQUIRE_HTTPS=true` 和非空 `TRUSTED_PROXY_IPS`，并在 HTTPS 反向代理后运行；旧运行时密钥上传接口已退役，新配置接口允许已认证用户管理自己的配置。程序只信任列表内代理提供的 `X-Forwarded-For` 和 `X-Forwarded-Proto`，配置或代理头无效时采用拒绝策略。

## 附录 A：RAG v2 分阶段契约

本附录冻结 `contextVersion=2`、`schemaVersion=2` 的目标契约。旧客户端未提交 `serviceId` 时继续走 v1；服务端不得替旧请求随机选择服务。当前已实现 `/services`、按服务筛选的角色互换场景、角色互换 v2 会话/消息及其 evidence 读取；客服训练 v2 初始化、逐轮回复、固定知识核验评分与报告已实现；N07 新建开关默认关闭，真实联调与人工验收状态另见 N07 记录。

### A.1 学员接口

| 方法 | 路径 | 目标行为 |
|---|---|---|
| `GET` | `/services` | 返回当前可选的已发布服务摘要和适用场景 |
| `GET` | `/services/{id}` | 返回服务公开字段，不返回管理备注或患者隐藏画像 |
| `GET` | `/scenarios?serviceId=...` | 按服务返回兼容场景、本人续练会话和最佳分 |
| `GET` | `/roleplay/scenarios?serviceId=...` | 按服务返回角色互换场景和本人续练会话 |
| `POST` | `/sessions` | 接收 `serviceId`、`scenarioId`、`clientSessionId`，创建患者初始化任务 |
| `GET` | `/sessions/{id}` | 扩展公开服务摘要、知识范围、患者公开画像与初始化状态 |
| `POST` | `/sessions/{id}/initialization/retry` | 仅本人对失败的初始化显式重试 |
| `POST` | `/sessions/{id}/restart` | 接收新的 `clientSessionId`，以当前发布版本开始新会话 |
| `POST` | `/roleplay/sessions` | 接收服务、场景及客户端幂等 ID，锁定 RAG 上下文 |
| `POST` | `/roleplay/sessions/{id}/restart` | 使用新幂等 ID 和当前服务版本重开 |
| `POST` | 两类现有 `/messages` | 保留 `clientMessageId`，响应增加 `answerStatus` 和 `citations` |
| `GET` | 两类现有报告/复盘接口 | 返回版本字段、知识核验或带依据复盘；角色互换不评分 |
| `GET` | `/sessions/{id}/evidence/{traceId}` | 本人读取该客服训练会话已经公开的证据 |
| `GET` | `/roleplay/sessions/{id}/evidence/{traceId}` | 本人读取该角色互换会话已经公开的证据 |

创建客服训练 v2 的目标响应为 `202`：

```json
{
  "code":0,
  "message":"accepted",
  "data":{
    "sessionId":"session-001",
    "contextVersion":2,
    "serviceSummary":{"serviceId":"svc-001","revisionId":"srv-rev-001","name":"演示服务 A"},
    "initialization":{"status":"pending","errorCode":null,"retryable":false}
  }
}
```

角色互换 v2 创建成功通常返回 `201`。相同 `clientSessionId` 和相同规范化请求重放返回原会话；相同键对应不同参数返回 `409 IDEMPOTENCY_CONFLICT`。规范化摘要至少覆盖用户身份、模式、`serviceId`、`scenarioId` 及会影响上下文的创建参数。

初始化状态通过 `GET /sessions/{id}` 以 `pending / generating / failed / ready` 正常返回。`pending` 或 `generating` 时发送消息、结束或请求提示返回 `409 PATIENT_INITIALIZATION_PENDING`；失败时返回 `409 PATIENT_INITIALIZATION_FAILED`，不能回落到无证据的旧患者生成。

带依据回复保留旧页面使用的 `reply`。`service-reply-rag-v3` 让模型基于本轮有效证据写简短客服答复（通常 1—2 句、20—80 字，明确要求详细时最多 180 字），首句回答当前问题，仅补充必要条件。结构化字段足以回答时不再追加知识段落的分支信息。后端校验引用归属、数字范围和单位及起价条件，不再把资料原文拼入聊天气泡；完整原文保留在 trace 和「查看依据」中。无有效引用、格式超限或校验不通过时返回简短待确认答复，不截断条件或输出未经核实的数字。既有历史消息不重写。v3 将寒暄、感谢、拒绝预约、共情等无服务事实的聊天与事实问答区分：通过校验的普通聊天可没有引用，返回空 learningPoints/complianceBoundary，页面不显示空依据入口或教学卡片。“一句话”等追问使用上一条实质患者问题检索，不把旧模型回答当作知识来源。

```json
{
  "reply":"演示服务 A 为 3980 元起/颗；具体按资料所列条件确认。",
  "answerStatus":"answered",
  "citations":[{"traceId":"trace-001","evidenceId":"E1"}],
  "learningPoints":[{"text":"报价需保留起价和计价单位。","evidenceIds":["E1"]}],
  "complianceBoundary":"具体诊疗安排需要医生结合检查评估。",
  "shouldEnd":false
}
```

`answerStatus` 只能为 `answered / partial / unknown / conflicted`。资料明确未知、无命中和资料冲突属于正常业务结果并返回 `200`；数据库或检索故障返回可重试 `503`，不能伪装成“资料未知”。

报告 v2 的最小可空示例：

```json
{
  "schemaVersion":2,
  "dimensionScores":{
    "knowledgeAccuracy":null,
    "medicalCompliance":90,
    "empathy":85,
    "needsDiscovery":80,
    "serviceEtiquette":90
  },
  "totalScore":null,
  "passed":null,
  "knowledgeAssessment":{
    "status":"insufficient_evidence",
    "knowledgeAccuracy":null,
    "assessableCount":0,
    "unassessableCount":2,
    "coverage":0,
    "rubricVersion":"knowledge-rubric-v1"
  },
  "knowledgeChecks":[],
  "knowledgeManifestHash":"sha256:..."
}
```

`ready` 只表示报告生成完成，不保证 `totalScore` 非空。只有有总分的报告进入平均分、达标率分母、最佳分、趋势及高分成就；完成次数仍包括无综合分报告。

### A.3 新增错误码

| HTTP | code | 含义 |
|---:|---|---|
| 409 | `SERVICE_NOT_AVAILABLE` | 服务未发布、已归档、已过期或不在当前运行范围 |
| 409 | `SERVICE_SCENARIO_MISMATCH` | 服务与训练场景不兼容 |
| 409 | `DRAFT_VERSION_CONFLICT` | 草稿已被其他编辑覆盖，客户端需重新加载 |
| 409 | `GENERATION_JOB_STATE_CONFLICT` | 生成任务当前状态不允许重试 |
| 409 | `GENERATION_EXHAUSTED` | 管理生成任务已达到 generation 上限 |
| 409 | `PATIENT_INITIALIZATION_PENDING` | 患者画像或开场仍在生成 |
| 409 | `PATIENT_INITIALIZATION_FAILED` | 患者初始化失败，需要显式重试 |
| 503 | `KNOWLEDGE_NOT_READY` | 当前运行范围没有满足开练条件的已发布资料 |
| 503 | `RAG_UNAVAILABLE` | 固定知识上下文缺失或检索基础设施不可用 |
| 503 | `EVIDENCE_VALIDATION_FAILED` | 模型输出经过一次修复后仍不能由证据支持 |

新服务训练按 Asia/Shanghai 日历日期检查报价 `validFrom/validUntil`，起止日均包含在有效范围内；无边界表示该方向不限期。过期或未生效报价不进入学员服务目录，也不能创建新训练。创建时的服务/场景联合查询可能返回 `SERVICE_SCENARIO_MISMATCH`；重开时返回 `SERVICE_NOT_AVAILABLE`。知识版本的 `effectiveFrom/effectiveUntil` 同样在新快照中筛选；检索再次按会话锁定的 `knowledgeAsOf` 检查，因此旧会话不会因今天的日期变化而丢失当时有效的依据。

同主题、同范围及同适用条件的命中段落，在截取前六条之前检测相反陈述与同一陈述的数值差异，冲突组进入 `conflicts`，回复/核验沿用 conflicted 处理。检测使用确定性文本规则，不增加模型调用；任意语义改写或复杂条件冲突仍需要发布者人工核对，不能将空 conflicts 当成资料完全一致的证明。

证据读取必须同时验证当前用户拥有会话、`traceId` 属于该会话且证据已被胜出消息或报告公开。任一条件不满足时统一返回无资源响应，避免枚举其他用户或失败尝试的 trace。

### N01：角色互换 RAG 证据加固（2026-09-20）

适用 `contextVersion >= 2` 的角色互换会话；无服务的 v1 会话保持原路径。不新增接口、环境变量或数据库迁移。

- 新上下文的 `manifestHash` 为 `sha256:` + 64 位小写十六进制。规范化对象为 `{version:1, serviceRevisionId, knowledgeRevisionIds, trainingScope}`；知识 revision ID 排序去重，以 nlohmann JSON 默认键排序、紧凑 UTF-8 序列化计算 SHA-256。服务 revision 和训练范围也参与摘要。
- 检索显式接受锁定摘要，返回同一摘要；检索前核对完整锁定 revision 集。摘要不一致、revision 丢失或越过服务范围属于系统错误，不伪装为资料未知。
- evidence bundle 新增 `serviceId`、`trainingScope`；passage 新增 `scope`、`serviceId`、`trainingScope`。后端验证本轮 trace、context、manifest、revision、服务范围及可渲染字段。新 citation 保留 `traceId/evidenceId`，增加 `manifestHash/revisionId`。
- 模型没有选择合法 evidenceId 时返回 `answerStatus=unknown`、空 citations；不再自动选择命中块。部分缺失返回 `partial`，有冲突时返回 `conflicted` 且不选边。
- 服务事实从结构化值重新渲染，不信任 displayText 或模型文本。保留起价、单位、范围、有效期、阶段和条件；预约资料明确为非实时号源。完整证据放不下时略过该条，不截断原文或价格限定词。
- N01 使用固定 intro、learningPoints 和 complianceBoundary；模型自由文本不进入这些展示字段，因此阿拉伯数字、中文数字、日期、折扣和无依据承诺不能从这些字段绕过引用。模型仍可选择本轮证据并返回布尔 shouldEnd。

`GET /roleplay/sessions/{id}/summary` 的 ready summary 对 v2 增加：

```json
{
  "schemaVersion": 2,
  "knowledgeManifestHash": "sha256:<64 hex characters>",
  "groundedFacts": [
    {
      "text": "3980 元起/颗；需检查后确认",
      "citation": {
        "traceId": "trace-example",
        "evidenceId": "E1",
        "revisionId": "service-revision-example",
        "manifestHash": "sha256:<64 hex characters>"
      }
    }
  ],
  "citations": [],
  "modelVersion": "deterministic-evidence-v1",
  "promptVersion": "roleplay-summary-evidence-v2"
}
```

示例省略原有 summary、coveredTopics、keyPrinciples、nextPracticeSuggestions 字段；实际 citations 为 groundedFacts 中引用的同一列表。最多复用六条已公开且已被标准客服消息引用的依据；未公开 trace、其他会话/服务引用和未选中的命中块不进入复盘。没有可复用依据时 groundedFacts/citations 为空，仅返回沟通原则。v2 复盘由后端确定性生成，不调用模型；完成任务前在持有任务租约的事务中再次校验。结果页和历史详情页可逐项展开原始依据。

**历史兼容**：不重写原始历史数据。旧 MD5 上下文须先校验原摘要，然后只在读取投影中按原服务 revision/manifest 计算规范化 SHA-256。旧 trace 的版本拼接串须与同一上下文相符，投影保留 `legacyManifestHash` 并返回规范化摘要。旧消息中仅有 traceId/evidenceId 的 citation 仍可读取；新回复、复盘及其引用使用规范化 SHA-256。没有服务范围元数据的旧 passage 不纳入新复盘，旧结构化事实经校验后可复用。旧 v2 自由文本复盘通过安全投影读取；v1 复盘不变。

验证范围与未运行项见 [N01 验证记录](rag-n01-validation.md)。

## N02：客服训练异步患者初始化

迁移：`032_patient_initialization_jobs.sql`。未提供服务的旧 `POST /sessions` 保持 201；提供服务时：
`{"scenarioId":"implant-basic","serviceId":"svc-...","clientSessionId":"客户端稳定唯一 ID"}` 返回 202，包含 `session`、`initialization`、空 `messages`。此路径暂不接受 `customPatientProfile`。

- 同一用户同一 clientSessionId、同参重放返回原会话（即使已放弃）；异参返回 409 IDEMPOTENCY_CONFLICT。
- 活跃唯一性按用户、场景、服务隔离；旧无服务会话有独立的唯一索引。
- 服务版本、完整知识 revision 清单、knowledgeAsOf、trainingScope=demo 和 SHA-256 manifest 在创建事务中固定，重试和续练不重新选取。
- `GET /sessions/{id}/initialization`：返回 status（pending/generating/ready/failed）、generation、retryable、errorType、manifestHash 和 publicProfile。只允许本人读取，不返回 privateProfile 或内部患者状态。
- `POST /sessions/{id}/initialization/retry`：仅失败且仍在进行中的会话可重试，返回 202；generation 增加，manifest 不变。重复重试返回 409 INITIALIZATION_NOT_RETRYABLE。
- 初始化未就绪时，消息、提示、结束请求返回 409 PATIENT_INITIALIZATION_PENDING 或 PATIENT_INITIALIZATION_FAILED；可显式放弃。
- 初始化成功时事务提交公开/私有画像、患者状态、round 0 开场和任务成功状态；开场不占学员轮数。旧 attempt、过期 lease、旧 generation 或已放弃会话的结果不能提交。
- 服务会话的旧 restart 接口返回 409 SERVICE_SESSION_RESTART_REQUIRES_CREATE：先放弃，再携带新 clientSessionId 创建。
- 会话详情及历史增加 contextVersion、serviceId、serviceRevisionId，详情增加 initializationStatus、publicProfile。

**阶段边界：** N02 完成基础设施和可注入的初始化网关契约；默认 DeepSeek 网关尚不支持患者初始化，任务会明确失败为 PATIENT_INITIALIZATION_UNAVAILABLE。真实 grounded 画像/逐轮回复在 N03 实现；N02 不将服务训练降级到旧患者模板。即使测试网关将初始化推进 ready，服务对话与提示仍返回 503 PATIENT_TRAINING_UNAVAILABLE。小程序选择服务与轮询界面留在 N04。本次不调用真实模型。


## N03：证据约束患者与逐轮状态

N03 替代上述 N02 阶段限制：默认 DeepSeek 网关支持患者初始化和逐轮回复；缺少 API key 时明确失败为 `MODEL_NOT_CONFIGURED`，不回落到旧模板。模型只选择画像枚举、回应意图和证据 ID，服务端验证锁定快照并组织患者话语；价格等事实保留原证据的条件和单位，资料缺失或冲突时要求确认。

私有画像固定存储；每轮模型只接收公开画像、关注点和已达到披露条件的信息。预算在明确询问后披露，比较其他诊所的信息在明确询问或第三轮起披露；个人预算不作为诊所报价。v2 会话的公开 `patientState` 仅含 `emotion`，不暴露信任度、隐藏信息及触发规则。训练提示使用不包含私有画像的固定指导文本。

每轮患者消息、情绪/信任度/披露状态/异议及结束原因、私有检索轨迹在获胜回复事务内保存。过期 lease 和重放不能覆盖状态。round 0 开场不消耗学员轮数；达到轮数上限或有效结束意图时与评分任务原子提交，手动结束与放弃同样记录原因。评分仍走现有流程，知识核验评分留待 N05/N06；小程序 v2 入口留待 N04。


## N04：服务选择与初始化交互

`GET /scenarios?serviceId=...` 和 `GET /roleplay/scenarios?serviceId=...` 仅返回该已发布、未归档服务的兼容场景，进行中会话按服务隔离；客服训练最佳成绩同样按服务隔离。省略 serviceId 为旧版通用场景及无服务会话。归档服务不能新建，但已有会话按 sessionId 读取仍有效。

客服训练历史列表新增可选 `serviceName`，取会话锁定的服务版本名称。小程序默认按服务训练，保留显式旧版入口；创建 v2 时不发送 customPatientProfile。创建请求 ID 在用户/模式/服务/场景范围内持久化，直到成功进入会话。初始化状态来自会话详情的 initializationStatus，显式重试使用已有 initialization/retry 接口。

### N06：服务训练评分报告 v2

`GET /api/sessions/:id/evaluation` 对 `contextVersion=2` 的会话返回 `schemaVersion=2` 报告。知识核验固定使用会话 manifest，沟通四维单独评价；总分固定权重为知识 25%、合规 25%、共情 20%、需求 20%、礼仪 10%。知识无法核验时 `knowledgeAccuracy/totalScore/passed` 为 `null`，不重分配权重；`knowledgeAssessment.nullReason` 解释原因。

新增报告字段：`serviceRevisionId`、`knowledgeManifestHash`、`knowledgeAssessment`（status、knowledgeAccuracy、assessableCount、unassessableCount、coverage、rubricVersion、nullReason）、`knowledgeChecks`。每条核验包含原始轮次和原句、verdict、reason、evidenceRefs、evidenceTexts、recommendedRewrite、scoringUnit、correctedInLaterRound。未回答项单独保留患者问题，不伪造客服原句。`learningMistakes` 只包含有引用、仍计分且未被后续纠正的 contradicted 项。

`GET /api/sessions/:id/evidence/:traceId` 需要会话所有者身份，且会话已完成、报告 ready。仅返回当前报告实际引用的公开 claim_verification trace，响应为 `{traceId, manifestHash, citations:[{text,citation}]}`。不存在、其他用户、其他会话、初始化/患者回复私有 trace、未被当前报告引用的 trace 均返回 `404 EVIDENCE_NOT_FOUND`。

错题复练上下文的 session 新增 `contextVersion/serviceId/originalRevisionId/currentRevisionId/versionChanged`。v2 单轮提交在提交时读取当前已发布服务与知识快照，返回实际 `currentRevisionId/versionChanged/assessmentStatus`。依据不足时 `passed=null`，明确已核实错误或缺漏为 false，目标知识确认且无其他错误/未知为 true。该操作不改写原报告，也不把复练快照当作原报告公开证据。

v1 报告和单轮复练契约保持兼容。N06 不增加数据迁移，不调用真实模型进行验收。

### N07：分段新建开关与受控调用

三个 `RAG_*_ENABLED` 环境变量默认 false，启动时生效。`RAG_ROLEPLAY_ENABLED` 控制新建服务患者模拟；新建服务客服训练同时要求 `RAG_PATIENT_ENABLED && RAG_EVALUATION_V2_ENABLED`。关闭时新建/角色互换重新开始返回 `503 RAG_NEW_SESSIONS_PAUSED`；已成功 clientSessionId 的重放、已有会话消息/初始化重试/评分/复盘/历史/证据继续原 v2 路径，绝不降级。无服务 v1 路径保持兼容。

health 新增 `rag.roleplayNewSessions/patientNewSessions/evaluationV2Enabled`、`modelCallLimit/modelCallCount`。`MODEL_CALL_LIMIT` 为非负整数，默认 0（不限）；正值按后端进程限制全部模型 HTTP 尝试，重试也计数，并发不能超额。耗尽返回 `503 MODEL_CALL_BUDGET_EXHAUSTED`，队列不自动重试；重启清零，所以不能用作跨进程/跨重启的账单额度。

每次实际 HTTP 尝试输出一个 `event=model_call` JSON 审计记录：logicalCallId、attempt、callNumber、requestedModel、actualModel、promptVersion、maxOutputTokens、usage、httpStatus、finishReason、latencyMs、retry、errorType。缺失的提供商 usage 保持缺失，不能当 0；无返回时 actualModel 为 null。日志不记录密钥、对话、资料正文或完整模型响应。该记录反映传输/JSON 解析结果，后续领域验证失败还需关联 Worker 失败日志。v2 复盘为确定性证据汇总，本身不增加模型调用。
