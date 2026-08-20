# 口腔客服智能陪练 API 契约

版本：v3.2（训练体验、主管摘要与个人成长）

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
- `admin`：可读取当前单一机构的聚合看板及成员学习摘要（完成次数、分数均值、五维均值和训练趋势），但不返回原始对话、报告原文、话术或错题；不能使用训练和个人成长接口。

主管角色只能通过受控的服务端数据库运维流程授予已验证的用户，客户端没有自助提权接口。

本轮不提供多机构租户、排行榜或团队运营接口。

## 3. 健康检查

### `GET /health`

无需登录。响应不包含任务内容、Prompt 或密钥：

```json
{
  "database":true,
  "modelConfigured":true,
  "workerRunning":true,
  "pendingJobs":0,
  "deadJobs":0,
  "runtimeApiKeyAllowed":false,
  "authMode":"wechat",
  "production":true
}
```

## 4. 客服训练

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/scenarios` | 场景、本人最佳分和本人进行中会话 |
| `POST` | `/sessions` | 创建会话，body 为 `{"scenarioId":"implant-basic"}` |
| `GET` | `/sessions` | 本人历史；支持 `status`、`scenarioId`、`limit` |
| `GET` | `/sessions/{id}` | 会话、完整消息和待恢复输入 |
| `POST` | `/sessions/{id}/restart` | 放弃进行中会话并创建新会话 |
| `POST` | `/sessions/{id}/messages` | 提交客服输入并获取模拟患者回复 |
| `POST` | `/sessions/{id}/hint` | 获取本次训练的合规沟通提示，最多 3 条 |
| `POST` | `/sessions/{id}/finish` | 结束会话并可靠入队评分任务 |
| `GET` | `/sessions/{id}/evaluation` | 获取 `not_started/generating/ready/failed` |
| `POST` | `/sessions/{id}/evaluation/retry` | 仅对失败评分人工重试 |

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

最后一轮的患者回复、输入状态 `ready`、会话 `completed`、评分 `generating` 和任务入队在同一数据库事务内提交。

### 结束与重试

- 零轮会话返回 `422 MIN_ROUNDS_NOT_REACHED`。
- `abandoned` 永远不能恢复为 `completed`，返回 `409 SESSION_ABANDONED`。
- 重复结束已完成会话只返回当前状态，不新建任务，也不隐式重试失败任务。
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

总分只使用一次固定五维加权，不再按违规二次扣减：

| 字段 | 权重 |
|---|---:|
| `knowledgeAccuracy` | 25% |
| `medicalCompliance` | 25% |
| `empathy` | 20% |
| `needsDiscovery` | 20% |
| `serviceEtiquette` | 10% |

单项违规 `deduction` 归一到 0—50，仅用于解释。如果存在 `deduction >= 30` 的严重违规且 `medicalCompliance > 60`，该次结果判定为 `MODEL_SCORE_INCONSISTENT`，由可靠任务机制按策略重试。

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
| `GET` | `/learning/profile` | 本人完成次数、平均分、首末分差、五维均值、最近 12 条趋势、练习重点和错题掌握数 |
| `GET` | `/learning/mine` | 本人签到积分、当月签到日历、连续天数、训练摘要和话术收藏数 |
| `POST` | `/learning/checkins` | 每个中国时区自然日签到一次，固定奖励 +10 积分 |

掌握状态请求：

```json
{"mastered":true}
```

服务端先验证该 `sessionId/mistakeKey` 是当前用户已完成报告的真实派生项，再写入 `learner_mistake_progress`。取消掌握不会删除报告或错题来源，只会将 `mastered_at` 置空并保留更新时间。

训练提示不调用模型；服务端将每条提示写入 `session_hints`，并在读取会话时返回。提示只给出沟通步骤与医疗合规边界，不给出诊断、用药、疗效、固定价格或疗程结论。话术收藏同样先验证 `sessionId/phraseKey` 来自当前用户的已完成报告，再写入偏好记录。

积分仅来自每日签到，固定为 +10；不提供训练奖励、兑换、排行榜或其他积分来源。

## 8. 可靠 AI Worker

API 和 Worker 运行在同一个便携程序中。Worker 默认并发 1，可配置到 4；使用 `FOR UPDATE SKIP LOCKED` 领取任务，租约 180 秒。去重键为：

- `evaluation:{sessionId}`
- `roleplay-summary:{sessionId}`

瞬时错误最多尝试 3 次，第一次失败后等待 5 秒，第二次失败后等待 30 秒。未配置模型、鉴权失败、内容过滤或不安全输出等非瞬时错误直接进入 `dead` 并把业务状态置为 `failed`。Worker 会回收过期租约；数据库中断时在进程内退避，异常不会逃出线程。

## 9. 看板与主管成员摘要

### `GET /dashboard/summary`

`scope` 为 `personal` 或 `institution`。学员收到个人统计和最近 5 条本人会话；管理员收到单机构聚合，`recentSessions` 为空，避免泄露个人会话。

仅 `admin` 可以调用：

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/supervisor/dashboard?range=week|month|quarter|all` | 机构学员数、训练量、达标率、场景聚合、五维均值和趋势 |
| `GET` | `/supervisor/members?limit=1..100` | 成员学习摘要，按姓名展示，不按成绩排序 |
| `GET` | `/supervisor/members/{memberId}` | 单个成员的五维均值、弱项建议和最多 12 条训练分数趋势 |

主管接口不会返回消息、原始患者内容、报告全文、错题或话术；不包含排行榜、培训计划、任务分配或团队运营操作。

## 10. 错误码

| HTTP | code | 含义 |
|---:|---|---|
| 400 | `INVALID_ARGUMENT` | 参数、JSON、UTF-8 或字符长度无效 |
| 400 | `HTTPS_REQUIRED` | 生产入口未通过 HTTPS 代理 |
| 401 | `AUTH_REQUIRED` / `AUTH_INVALID` / `AUTH_EXPIRED` | 缺少、无效或过期令牌 |
| 401 | `WECHAT_LOGIN_FAILED` | 微信 code 无效或过期 |
| 403 | `ROLE_FORBIDDEN` | 角色无权访问该类接口 |
| 403 | `ORIGIN_FORBIDDEN` | Origin 不在精确允许列表 |
| 404 | `SCENARIO_NOT_FOUND` | 场景不存在 |
| 404 | `SESSION_NOT_FOUND` / `ROLEPLAY_SESSION_NOT_FOUND` | 会话不存在或不属于本人 |
| 404 | `LEARNING_MISTAKE_NOT_FOUND` | 错题不存在、不属于本人或不再是当前报告的派生项 |
| 404 | `LEARNING_PHRASE_NOT_FOUND` / `MEMBER_NOT_FOUND` | 话术或成员不存在、无权访问 |
| 409 | `IDEMPOTENCY_CONFLICT` | 同一幂等 ID 对应不同内容 |
| 409 | `SESSION_RESPONSE_PENDING` | 模拟患者回复租约有效 |
| 409 | `ROLEPLAY_RESPONSE_PENDING` | 标准客服回复租约有效 |
| 409 | `SESSION_ABANDONED` | 客服训练会话已放弃 |
| 409 | `ROLEPLAY_SESSION_ABANDONED` | 患者模拟会话已放弃 |
| 409 | `SESSION_IN_PROGRESS` / `ROLEPLAY_SESSION_IN_PROGRESS` | 同场景已有进行中会话 |
| 409 | `EVALUATION_NOT_RETRYABLE` / `ROLEPLAY_SUMMARY_NOT_RETRYABLE` | 当前任务不可人工重试 |
| 409 | `HINT_LIMIT_REACHED` | 本次训练的三条提示已经用完 |
| 422 | `MIN_ROUNDS_NOT_REACHED` | 尚未完成一轮 |
| 429 | `RATE_LIMITED` | 用户/IP 速率超限 |
| 503 | `MODEL_NOT_CONFIGURED` / `MODEL_AUTH_FAILED` | 模型配置不可用 |
| 503 | `MODEL_TIMEOUT` / `MODEL_RATE_LIMITED` / `MODEL_ERROR` | 模型瞬时错误 |
| 503 | `MODEL_INVALID_RESPONSE` / `MODEL_SCORE_INCONSISTENT` | 模型结果无效或评分矛盾 |
| 503 | `REPORT_INVALID` | 已存储评分报告格式无效，无法生成学习洞察 |

## 11. 兼容与安全边界

现有成功响应数据结构和全部业务路径保持兼容。学员洞察字段在服务端对已规范化报告进行派生，DeepSeek 请求地址、请求参数、Prompt、响应解析与模型调用内部重试逻辑未改变。生产环境必须设置 `PRODUCTION=true`、`AUTH_MODE=wechat`、精确 `ALLOWED_ORIGIN`、`REQUIRE_HTTPS=true`，并在 HTTPS 反向代理后运行；运行时密钥上传会自动关闭。
