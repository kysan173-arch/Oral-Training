# 口腔客服智能陪练 API 契约

版本：v3.6（训练体验、主管团队与个人成长 · RAG v2 报告兼容、知识管理与检索预览）

> RAG v2 仍在分阶段开发。知识发布已生成中文检索块，管理端可预览确定性召回，角色互换已接入服务快照和回答依据；学员扮演客服的 AI 患者初始化与知识核验评分仍待实现。

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
  "modelConfigured":true,
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

数据库、任务队列、模型或 Worker 不可用时返回 HTTP 503，`ready=false` 且 `status=unhealthy`。Worker 进入数据库错误退避期间不会再被报告为健康。小程序仍会读取健康响应中的 `runtimeApiKeyAllowed`，因此本地演示可在模型未配置时打开密钥配置入口；负载均衡器则会正确识别该实例尚未就绪。连接池等待超时时，普通接口返回 HTTP 503 `DATABASE_BUSY`。

## 4. 客服训练

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/scenarios` | 场景、本人最佳分和本人进行中会话 |
| `POST` | `/sessions` | 创建会话，body 为 `{"scenarioId":"implant-basic","customPatientProfile":{"gender":"女"}}` |
| `GET` | `/sessions` | 本人历史；支持 `status`、`scenarioId`、`limit` |
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
| `POST` / `GET` | `/roleplay/sessions` | 创建或查询本人会话 |
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

仅 `admin` 可以调用。**所有聚合口径都限定在「我的团队」范围内**（`supervisor_team_members`），未加入团队的学员不会出现在看板、成员列表、报表与排行榜中：

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/supervisor/dashboard?range=week\|month\|quarter\|all` | 本团队学员数、训练量、达标率、场景聚合、五维均值和趋势 |
| `GET` | `/supervisor/members?limit=1..100` | 本团队成员学习摘要，按姓名展示，不按成绩排序；附 `totalTeamMembers`（不受 `limit` 影响的团队成员总数，用于判断列表是否被截断）与 `totalLearners`（全部在职学员数，含已归属其他主管的人） |
| `GET` | `/supervisor/members/{memberId}` | 本团队单个成员的五维均值、弱项建议和最多 12 条训练分数趋势；非本团队成员返回 `MEMBER_NOT_FOUND` |
| `GET` | `/supervisor/scenarios` | 场景目录（`id`、`name`、`category`、`difficulty`），供发布培训计划时选择适用场景 |

主管接口不会返回消息、原始患者内容、报告全文、错题或话术；不包含任务指派（培训计划见第 11 节）。

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
| `POST` | `/supervisor/training-plans` | `admin` | 发布计划；请求体 `title`(1-100)、`period`(`week`/`month`)、`dueAt`、`requiredCount`(1-20)、`requiredPassRate`(0-100)、`description`(≤500)、`scenarioIds`（空数组=全部场景）、`targetUserIds`（空数组=全团队成员，最多 500 个去重 id）；返回新建计划、`assignmentCount`、`targeted`、`requestedCount`、`skippedCount` |
| `GET` | `/supervisor/training-plans?status=all\|active\|expired` | `admin` | 本人发布的计划列表，附 `assignmentCount`、`doneCount`、`avgScore`（统计时按当前团队过滤一次，与成员列表口径一致） |
| `GET` | `/supervisor/training-plans/{planId}` | `admin` | 计划详情与逐学员进度（`completedCount`、`avgScore`、`lastTrainingDate`、`done`） |
| `POST` | `/supervisor/training-plans/{planId}/notify` | `admin` | 标记已提醒并回传未完成名单，供前端复制；当前无订阅消息通道 |
| `GET` | `/learning/training-plans` | `learner` | 本人被指派计划的进度，附 `completedCount`、`avgScore`、`status`(`pending`/`done`/`expired`) 与 `pendingCount` |

进度口径：只统计**客服训练**（`sessions`）的完成次数与平均分，**不含患者模拟**（`roleplay_sessions` 无评分，无法参与「最低平均分」判定）。`scenarioIds` 非空时按 `scenario_id` 过滤训练记录。

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

现有成功响应数据结构和全部业务路径保持兼容。学员洞察字段在服务端对已规范化报告进行派生；DeepSeek 请求地址、请求参数、响应解析与模型调用内部重试逻辑未改变，评分 Prompt 仅新增累计违规与医疗合规分的一致性约束并记录为 `score-prompt-v3`。生产环境必须设置 `PRODUCTION=true`、`AUTH_MODE=wechat`、HTTPS `ALLOWED_ORIGIN`、`REQUIRE_HTTPS=true` 和非空 `TRUSTED_PROXY_IPS`，并在 HTTPS 反向代理后运行；运行时密钥上传会自动关闭。程序只信任列表内代理提供的 `X-Forwarded-For` 和 `X-Forwarded-Proto`，配置或代理头无效时采用拒绝策略。

## 附录 A：RAG v2 分阶段契约

本附录冻结 `contextVersion=2`、`schemaVersion=2` 的目标契约。旧客户端未提交 `serviceId` 时继续走 v1；服务端不得替旧请求随机选择服务。当前已实现 `/services`、按服务筛选的角色互换场景、角色互换 v2 会话/消息及其 evidence 读取；客服训练 v2 初始化与知识核验评分仍待后续阶段完成。

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

带依据回复保留旧页面使用的 `reply`：

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

### A.2 管理接口（R02—R04 主体已实现）

| 方法 | 路径 | 目标行为 |
|---|---|---|
| `GET/POST` | `/admin/services` | 服务列表与新建服务草稿 |
| `GET/PUT` | `/admin/services/{id}/draft` | 按 `draftVersion` 读取和保存服务草稿 |
| `POST` | `/admin/services/{id}/publish` | 按草稿版本与幂等键原子发布 |
| `POST` | `/admin/services/{id}/archive` | 停止新会话选择，不删除历史版本 |
| `GET` | `/admin/services/{id}/revisions` | 读取不可变版本及字段差异 |
| `GET/POST` | `/admin/knowledge` | 专业知识列表与新建条目 |
| `GET/PUT` | `/admin/knowledge/{id}/draft` | 按 `draftVersion` 编辑知识草稿 |
| `POST` | `/admin/knowledge/{id}/publish` | 发布不可变版本并生成 `zh-bigram-v1` 检索块 |
| `POST` | `/admin/knowledge/{id}/archive` | 停止新上下文纳入该条目 |
| `GET` | `/admin/knowledge/{id}/revisions` | 读取知识版本历史 |
| `POST` | `/admin/knowledge/generation-jobs` | 创建模拟资料草稿生成任务，返回 `202` |
| `GET` | `/admin/knowledge/generation-jobs/{id}` | 读取排队、执行、失败或草稿结果 |
| `POST` | `/admin/knowledge/generation-jobs/{id}/retry` | 对失败任务显式重试 |
| `POST` | `/admin/knowledge/preview` | 使用指定已保存草稿版本和问题执行临时检索预览 |

全部管理端点逐个校验 `admin`。知识管理权限不授予学员对话、个人报告、话术或错题读取权限。生成任务只保存 `synthetic/unverified` 草稿，不能直接发布或自行标记 `reviewed`。

服务创建提交 `{"payload": ServiceDraft}`；保存提交 `{"draftVersion":2,"payload":ServiceDraft}`。知识创建提交 `topic/scope/serviceId/title/body/metadata`，保存提交 `draftVersion/title/body/metadata`。服务与知识发布都必须携带 `Idempotency-Key` 请求头以及正文中的 `draftVersion`；同键同参返回原 revision，同键异参返回 `409 IDEMPOTENCY_CONFLICT`。

金额使用整数分。已知价格必须保留类型、CNY、单位和适用条件；范围价格的下界不得大于上界。价格、单次时长、全程周期、复诊间隔和预约资料都可显式使用 `{"status":"unknown","reason":"..."}`，不得用零冒充未知。发布在一个事务内追加不可变 revision、切换 current pointer、更新场景关联并写审计，归档不删除历史版本。

生成任务请求示例：

```json
{
  "kind":"knowledge_draft",
  "draftId":"knowledge-draft-001",
  "brief":"生成一份仅用于演示训练的候选正文",
  "count":1
}
```

`count` 当前固定为 1。任务状态为 `pending/running/retry_wait/succeeded/dead`，响应包含 `generation`、`attempts`、`maxAttempts`、`promptVersion`、`modelVersion`、`resultApplied` 和错误字段。Worker 使用独立队列、租约与 attempt token；生成开始后如管理员保存了新草稿，旧结果只保存在 `result.candidate` 且 `resultApplied=false`，不会覆盖人工编辑。失败任务只能通过 retry 端点开始新 generation；重试会以当时的最新草稿重新构造模型输入。

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
| 503 | `RAG_UNAVAILABLE` | RAG 被停用或检索基础设施不可用 |
| 503 | `EVIDENCE_VALIDATION_FAILED` | 模型输出经过一次修复后仍不能由证据支持 |

证据读取必须同时验证当前用户拥有会话、`traceId` 属于该会话且证据已被胜出消息或报告公开。任一条件不满足时统一返回无资源响应，避免枚举其他用户或失败尝试的 trace。
