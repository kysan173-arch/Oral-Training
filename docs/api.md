# AI 口腔客服智能陪练系统 API 契约

> 版本：v2.0 MVP
>
> 本契约以《口腔医疗客服智能陪练系统 MVP 产品设计文档》为唯一产品依据，服务于一天内可演示的最小闭环：场景选择 → 创建/恢复会话 → 多轮对练 → 自动评分 → 查看报告 → 历史回顾 → 演示数据。

## 1. 设计边界

### 1.1 MVP 范围

- 固定演示用户，不提供微信授权、手机号登录和正式权限体系。
- 固定 4 个训练场景：种植牙基础咨询、正畸基础咨询、与其他诊所比价、术后不适咨询。
- 只支持文字对话。
- 训练轮数为 5—10 轮；用户完成至少 1 轮后可以主动结束，第 10 轮 AI 回复完成后自动结束。
- 每轮消息提交后由服务端持久化；中途退出不需要额外保存接口，下一次通过会话详情恢复。
- 评分由独立评分服务生成，结果包含五维得分、总结、优势、改进、违规说明和关键轮次点评。
- 统计页只展示演示数据，不包含团队成员、权限、排行榜和导出。

### 1.2 暂不提供的能力

- 登录、刷新 token、角色权限。
- 语音、ASR、黄金话术、快捷回复、实时评分。
- 打卡、积分、等级、排行榜、错题本和主管后台。
- 真实患者信息、诊疗记录和医疗建议。

## 2. 服务约定

### 2.1 Base URL

```text
https://<host>/api
```

开发环境可以使用局域网地址，但生产环境必须使用 HTTPS。前端只保存 Base URL，不保存任何模型 API Key。

### 2.2 演示用户

MVP 不做登录。服务端默认使用固定用户：

```text
demo-user-001
```

如需在开发环境区分多个演示数据集，可增加请求头；正式 MVP 不由用户填写或修改：

```http
X-Demo-User-Id: demo-user-001
```

### 2.3 请求头

```http
Content-Type: application/json
X-Demo-User-Id: demo-user-001
```

### 2.4 统一响应结构

成功响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {}
}
```

失败响应：

```json
{
  "code": "SESSION_FINISHED",
  "message": "训练已结束，不能继续发送消息",
  "data": null
}
```

前端请求封装只在 HTTP 状态码成功且 `code === 0` 时返回 `data`；其他情况统一进入错误处理。

### 2.5 时间、分数和分页

- 时间统一使用 ISO 8601，带时区，例如 `2026-07-15T14:30:00+08:00`。
- 分数统一为 0—100 的整数；平均分可以保留 1 位小数。
- 历史记录默认返回最近 50 条，MVP 不强制分页。
- 所有 ID 使用字符串，避免前端因 JavaScript 数字精度导致会话 ID 变化。

## 3. 核心数据对象

### 3.1 Scenario 场景

```json
{
  "id": "implant-basic",
  "name": "种植牙基础咨询",
  "summary": "患者咨询种植牙流程、疼痛和治疗周期",
  "difficulty": "basic",
  "focus": ["基础信息解释", "需求挖掘", "回应担忧", "医疗边界"],
  "patientProfile": {
    "age": 52,
    "gender": "unknown",
    "description": "缺失一颗后牙，对种植牙了解较少，初始情绪平静但谨慎"
  },
  "maxRounds": 10,
  "bestScore": 0,
  "activeSession": null
}
```

`patientProfile` 只包含用户可见信息。隐藏顾虑、风险信号、情绪和信任度只保存在服务端，不得通过接口返回给小程序。

### 3.2 Session 会话

```json
{
  "id": "sess_01JZ...",
  "scenarioId": "implant-basic",
  "scenarioName": "种植牙基础咨询",
  "status": "in_progress",
  "currentRound": 2,
  "maxRounds": 10,
  "startedAt": "2026-07-15T14:30:00+08:00",
  "updatedAt": "2026-07-15T14:32:10+08:00",
  "finishedAt": null,
  "totalScore": null,
  "evaluationStatus": "not_started"
}
```

状态：

| 状态 | 含义 | 允许操作 |
|---|---|---|
| `in_progress` | 训练进行中或中途退出 | 查看、继续、发送、结束、重新开始 |
| `completed` | 对话结束，评分已生成或正在生成 | 查看会话、查看报告、重新训练 |
| `abandoned` | 用户选择重新开始后，旧会话被放弃 | 只读查看 |

评分状态：

| 状态 | 含义 |
|---|---|
| `not_started` | 尚未结束训练 |
| `generating` | 已结束，评分生成中 |
| `ready` | 评分已生成 |
| `failed` | 评分失败，可重试 |

### 3.3 Message 消息

```json
{
  "id": "msg_01JZ...",
  "role": "patient",
  "content": "我主要担心手术会不会很疼。",
  "round": 2,
  "createdAt": "2026-07-15T14:32:08+08:00"
}
```

`role` 只能为 `patient` 或 `user`。患者消息由服务端生成，用户消息由前端提交。

### 3.4 Evaluation 评分报告

```json
{
  "sessionId": "sess_01JZ...",
  "status": "ready",
  "totalScore": 82,
  "dimensionScores": {
    "knowledgeAccuracy": 80,
    "medicalCompliance": 90,
    "empathy": 88,
    "needsDiscovery": 75,
    "serviceEtiquette": 86
  },
  "summary": "能够先回应患者担忧，但对预算和疼痛的追问还不够完整。",
  "strengths": [
    {
      "round": 1,
      "evidence": "先回应了患者对疼痛的担心",
      "content": "表达了基本的同理和安抚"
    }
  ],
  "improvements": [
    {
      "round": 2,
      "content": "继续询问疼痛担忧、预算和时间安排，再介绍检查流程。"
    }
  ],
  "violations": [
    {
      "round": 3,
      "originalQuote": "我们保证种植一定成功。",
      "type": "疗效保证",
      "reason": "客服不能对个体治疗效果作绝对承诺。",
      "deduction": 30,
      "recommendedRewrite": "具体效果需要医生检查后结合您的情况评估。"
    }
  ],
  "roundComments": [
    {
      "round": 2,
      "userMessage": "......",
      "comment": "回应了情绪，但没有追问患者最主要的顾虑。",
      "recommendedRewrite": "我理解您担心疼痛，您更担心手术过程，还是术后恢复呢？"
    }
  ],
  "modelVersion": "provider-model-v1",
  "promptVersion": "score-prompt-v1",
  "generatedAt": "2026-07-15T14:32:30+08:00"
}
```

五维权重固定为：

| 字段 | 中文名称 | 权重 |
|---|---|---:|
| `knowledgeAccuracy` | 口腔知识准确性 | 25% |
| `medicalCompliance` | 医疗合规 | 25% |
| `empathy` | 情绪识别与同理心 | 20% |
| `needsDiscovery` | 需求挖掘 | 20% |
| `serviceEtiquette` | 服务礼仪 | 10% |

## 4. 接口清单

| 方法 | 路径 | 用途 |
|---|---|---|
| `GET` | `/scenarios` | 获取 4 个场景及当前用户的进行中会话 |
| `POST` | `/sessions` | 创建新训练会话 |
| `POST` | `/sessions/{sessionId}/restart` | 放弃旧会话并重新创建同场景会话 |
| `GET` | `/sessions/{sessionId}` | 恢复会话、消息和当前轮次 |
| `POST` | `/sessions/{sessionId}/messages` | 提交用户消息并获取患者回复 |
| `POST` | `/sessions/{sessionId}/finish` | 主动或自动结束训练并触发评分 |
| `GET` | `/sessions/{sessionId}/evaluation` | 获取评分状态或完整评分报告 |
| `POST` | `/sessions/{sessionId}/evaluation/retry` | 评分失败后重新评分 |
| `GET` | `/sessions` | 获取历史训练记录 |
| `GET` | `/dashboard/summary` | 获取演示数据汇总 |

## 5. 场景接口

### 5.1 获取场景列表

```http
GET /api/scenarios
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "items": [
      {
        "id": "implant-basic",
        "name": "种植牙基础咨询",
        "summary": "咨询种植牙流程、疼痛和时间",
        "difficulty": "basic",
        "focus": ["基础信息解释", "需求挖掘", "医疗边界"],
        "patientProfile": {
          "age": 52,
          "gender": "unknown",
          "description": "缺失一颗后牙，对种植牙了解较少，初始情绪平静但谨慎"
        },
        "maxRounds": 10,
        "bestScore": 0,
        "activeSession": null
      }
    ]
  }
}
```

`activeSession` 不为空时，场景页显示“继续训练”；为空时显示“开始训练”。

四个固定场景 ID：

| ID | 名称 |
|---|---|
| `implant-basic` | 种植牙基础咨询 |
| `orthodontic-basic` | 正畸基础咨询 |
| `price-comparison` | 与其他诊所比价 |
| `post-treatment-discomfort` | 术后不适咨询 |

## 6. 会话与对练接口

### 6.1 创建会话

```http
POST /api/sessions
Content-Type: application/json
```

请求：

```json
{
  "scenarioId": "implant-basic"
}
```

成功响应使用 `201`：

```json
{
  "code": 0,
  "message": "created",
  "data": {
    "session": {
      "id": "sess_01JZ...",
      "scenarioId": "implant-basic",
      "status": "in_progress",
      "currentRound": 0,
      "maxRounds": 10,
      "evaluationStatus": "not_started"
    },
    "messages": [
      {
        "id": "msg_01JZ...",
        "role": "patient",
        "content": "您好，我想了解一下种植牙，大概需要多久？",
        "round": 0,
        "createdAt": "2026-07-15T14:30:00+08:00"
      }
    ]
  }
}
```

如果该场景已有 `in_progress` 会话，返回 `409 SESSION_IN_PROGRESS`，前端应提示用户选择继续或调用重新开始接口。

为便于前端直接恢复会话，建议错误响应携带当前会话 ID：

```json
{
  "code": "SESSION_IN_PROGRESS",
  "message": "该场景已有进行中的训练",
  "data": {
    "sessionId": "sess_01JZ..."
  }
}
```

### 6.2 重新开始会话

```http
POST /api/sessions/{sessionId}/restart
```

服务端执行以下原子操作：

1. 将原 `in_progress` 会话标记为 `abandoned`。
2. 创建同一场景的新会话。
3. 返回新会话及患者开场消息。

响应结构与“创建会话”一致。已完成或已放弃的会话不能再次重开，应从场景页创建新会话。

### 6.3 获取会话详情

```http
GET /api/sessions/{sessionId}
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "session": {
      "id": "sess_01JZ...",
      "scenarioId": "implant-basic",
      "scenarioName": "种植牙基础咨询",
      "status": "in_progress",
      "currentRound": 2,
      "maxRounds": 10,
      "startedAt": "2026-07-15T14:30:00+08:00",
      "updatedAt": "2026-07-15T14:32:10+08:00",
      "finishedAt": null,
      "totalScore": null,
      "evaluationStatus": "not_started"
    },
    "messages": [],
    "pendingMessage": null
  }
}
```

接口返回完整消息，供“继续训练”和历史详情使用。模型超时且用户消息已经保存时，`pendingMessage` 返回 `{ clientMessageId, content, round }`，前端必须使用相同的 `clientMessageId` 和内容重试；没有待回复消息时为 `null`。不要返回隐藏信息、Prompt、情绪内部状态或模型原始响应。

### 6.4 发送用户消息

```http
POST /api/sessions/{sessionId}/messages
Content-Type: application/json
```

请求：

```json
{
  "clientMessageId": "client-msg-0002",
  "content": "我理解您的担心，可以先了解一下您最在意疼痛还是恢复时间吗？"
}
```

约束：

- `content` 去除首尾空白后长度为 1—1000 个字符。
- 会话必须为 `in_progress`。
- 当前轮数达到 10 后拒绝发送。
- `clientMessageId` 用于幂等，网络重试不能生成重复用户消息或重复患者回复。

成功响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "userMessage": {
      "id": "msg_user_0002",
      "role": "user",
      "content": "我理解您的担心，可以先了解一下您最在意疼痛还是恢复时间吗？",
      "round": 1,
      "createdAt": "2026-07-15T14:32:00+08:00"
    },
    "patientMessage": {
      "id": "msg_patient_0002",
      "role": "patient",
      "content": "我最担心的是手术疼不疼，另外预算也有限。",
      "round": 1,
      "createdAt": "2026-07-15T14:32:08+08:00"
    },
    "session": {
      "currentRound": 1,
      "remainingRounds": 9,
      "status": "in_progress",
      "shouldFinish": false
    }
  }
}
```

若模型调用超时，返回 `503 MODEL_TIMEOUT`。服务端不得删除已保存的用户消息，前端可以使用同一个 `clientMessageId` 重试。

### 6.5 结束训练

```http
POST /api/sessions/{sessionId}/finish
Content-Type: application/json
```

请求：

```json
{
  "reason": "manual"
}
```

`reason` 可选值：`manual`、`max_rounds`、`model_error`。

规则：

- `currentRound === 0` 时返回 `422 MIN_ROUNDS_NOT_REACHED`。
- 结束操作必须幂等；已结束的会话再次调用时返回当前会话状态，不重复创建评分任务。
- 会话立即变为 `completed`，评分状态变为 `generating` 或 `ready`。

响应：

```json
{
  "code": 0,
  "message": "accepted",
  "data": {
    "sessionId": "sess_01JZ...",
    "status": "completed",
    "evaluationStatus": "generating"
  }
}
```

HTTP 状态使用 `202` 表示评分仍在生成。前端跳转结果页后轮询评分接口，不要把分数拼到 URL 中。

## 7. 评分接口

### 7.1 获取评分报告

```http
GET /api/sessions/{sessionId}/evaluation
```

评分生成中：

```json
{
  "code": 0,
  "message": "evaluation_generating",
  "data": {
    "sessionId": "sess_01JZ...",
    "status": "generating",
    "retryable": false,
    "evaluation": null
  }
}
```

评分完成：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "sessionId": "sess_01JZ...",
    "status": "ready",
    "retryable": false,
    "evaluation": {}
  }
}
```

评分失败：

```json
{
  "code": 0,
  "message": "evaluation_failed",
  "data": {
    "sessionId": "sess_01JZ...",
    "status": "failed",
    "retryable": true,
    "evaluation": null
  }
}
```

### 7.2 重试评分

```http
POST /api/sessions/{sessionId}/evaluation/retry
```

只允许对 `evaluationStatus = failed` 的已完成会话调用。成功返回 `202`，响应结构与评分生成中一致。

评分服务要求：

- 一次性读取完整对话，而不是只评价最后一轮。
- 使用固定评分 Prompt 和 JSON Schema。
- 识别严重医疗错误、疗效保证、风险处理不当和贬低其他机构等问题。
- JSON 解析失败最多自动重试一次；仍失败则将状态设为 `failed`。
- 评分建议不得写成医学诊断或治疗指令。

## 8. 历史记录接口

### 8.1 获取训练历史

```http
GET /api/sessions?status=all&scenarioId=&limit=50
```

查询参数：

| 参数 | 必填 | 说明 |
|---|---|---|
| `status` | 否 | `all`、`in_progress`、`completed`、`abandoned`，默认 `all` |
| `scenarioId` | 否 | 按场景筛选 |
| `limit` | 否 | 1—50，默认 50 |

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "items": [
      {
        "id": "sess_01JZ...",
        "scenarioId": "implant-basic",
        "scenarioName": "种植牙基础咨询",
        "status": "completed",
        "currentRound": 6,
        "maxRounds": 10,
        "totalScore": 82,
        "evaluationStatus": "ready",
        "startedAt": "2026-07-15T14:30:00+08:00",
        "updatedAt": "2026-07-15T14:36:00+08:00"
      }
    ],
    "total": 1
  }
}
```

列表只返回摘要；点击记录后调用 `GET /sessions/{sessionId}` 获取完整对话，再调用评分接口获取报告。

## 9. 演示数据接口

### 9.1 获取汇总数据

```http
GET /api/dashboard/summary
```

响应：

```json
{
  "code": 0,
  "message": "ok",
  "data": {
    "totalSessions": 12,
    "completedSessions": 10,
    "averageScore": 82.5,
    "scenarioStats": [
      {
        "scenarioId": "implant-basic",
        "scenarioName": "种植牙基础咨询",
        "trainingCount": 4
      }
    ],
    "dimensionAverages": {
      "knowledgeAccuracy": 80,
      "medicalCompliance": 88,
      "empathy": 84,
      "needsDiscovery": 76,
      "serviceEtiquette": 86
    },
    "recentSessions": []
  }
}
```

统计规则：

- `totalSessions` 不统计 `abandoned` 会话。
- `completedSessions` 只统计状态为 `completed` 且评分状态为 `ready` 的会话。
- `averageScore` 只计算已有评分的完成会话。
- `scenarioStats` 只返回四个固定场景，可返回 0 次。
- `recentSessions` 返回最近 5 条训练摘要。

## 10. 错误码

| HTTP | code | 场景 |
|---:|---|---|
| 400 | `INVALID_ARGUMENT` | 参数缺失、格式错误或消息超长 |
| 404 | `SCENARIO_NOT_FOUND` | 场景不存在 |
| 404 | `SESSION_NOT_FOUND` | 会话不存在 |
| 409 | `SESSION_IN_PROGRESS` | 同一场景已有进行中会话 |
| 409 | `SESSION_FINISHED` | 已结束会话不能继续发送消息 |
| 409 | `SESSION_ABANDONED` | 已放弃会话不能继续操作 |
| 422 | `MIN_ROUNDS_NOT_REACHED` | 未完成至少 1 轮，不能评分 |
| 422 | `SCENARIO_NOT_ALLOWED` | 场景不在 MVP 固定范围内 |
| 429 | `RATE_LIMITED` | 请求过于频繁 |
| 503 | `MODEL_TIMEOUT` | 患者模型调用超时 |
| 503 | `MODEL_OUTPUT_INVALID` | 患者模型返回无法解析的结果 |
| 500 | `EVALUATION_FAILED` | 评分生成失败，可调用评分重试接口 |

## 11. 前端调用顺序

### 11.1 正常训练

```text
GET /scenarios
    ↓ 点击“开始训练”
POST /sessions
    ↓
POST /sessions/{id}/messages  × N
    ↓ 用户主动结束或第 10 轮
POST /sessions/{id}/finish
    ↓
GET /sessions/{id}/evaluation 轮询
    ↓ status = ready
展示结果报告
```

### 11.2 续练

```text
GET /scenarios
    ↓ 发现 activeSession
GET /sessions/{id}
    ↓
恢复完整消息后继续 POST /messages
```

### 11.3 重新开始

```text
POST /sessions/{oldId}/restart
    ↓ oldId = abandoned
    ↓ 返回 newId
使用 newId 进入新的训练会话
```

### 11.4 结果页评分失败

```text
POST /sessions/{id}/finish
    ↓
GET /sessions/{id}/evaluation
    ├─ ready：展示报告
    ├─ generating：间隔 2 秒重试，最多 30 秒
    └─ failed：展示“重新生成评分”按钮
        ↓
POST /sessions/{id}/evaluation/retry
```

## 12. 与当前小程序代码的迁移映射

| 当前代码 | 新接口 | 调整要求 |
|---|---|---|
| `index.js loadMockData` | `GET /scenarios` | 删除旧的四分类和 15 个关卡 mock |
| `training.js startTraining` | `POST /sessions` 或 `GET /sessions/{id}` | 不再用 `getLevelConfig`，保存 `sessionId` |
| `training.js getAIResponse` | `POST /sessions/{id}/messages` | 删除固定回复和随机延迟 |
| `training.js endTraining` | `POST /sessions/{id}/finish` | 删除随机分数，不把分数放进 URL |
| `result.js loadResult` | `GET /sessions/{id}/evaluation` | 按 `status` 处理生成中、完成、失败 |
| `report.js` 历史列表 | `GET /sessions` | 只保留历史记录和完整详情入口 |
| 报告/数据页 | `GET /dashboard/summary` | 只展示 MVP 统计，不展示排行榜和错题本 |
| `static/api/request.js` | 保留 | 将 Base URL 改为部署地址，错误结构按本契约处理 |

## 13. 服务端最小落库对象

MVP 至少需要四张表或等价的数据对象：

| 对象 | 必要字段 |
|---|---|
| `scenarios` | 场景公开信息、隐藏配置、训练重点、最大轮数 |
| `sessions` | 用户、场景、状态、轮数、开始/更新时间、评分状态、总分 |
| `messages` | 会话、角色、内容、轮次、时间、幂等 ID |
| `evaluations` | 五维得分、总分、总结、优势、改进、违规、逐轮点评、模型版本 |

服务端必须保存患者内部状态：`emotion`、`emotionLevel`、`trustLevel`、`revealedInformation`、`riskTriggered`、`currentRound`。这些字段仅供患者 Prompt 和续练使用，不返回给用户。

## 14. 患者模拟（角色互换）接口

患者模拟与客服训练完全隔离：学员扮演患者，模型扮演标准客服。它复用四个场景，但不写入 `sessions`、`evaluations` 或数据看板，也不生成五维分数。

| 方法 | 路径 | 用途 |
|---|---|---|
| `GET` | `/roleplay/scenarios` | 获取场景、建议患者提问和角色互换进行中会话 |
| `POST` / `GET` | `/roleplay/sessions` | 创建或查询患者模拟历史 |
| `GET` | `/roleplay/sessions/{id}` | 获取会话、完整问答和待重试消息 |
| `POST` | `/roleplay/sessions/{id}/restart` | 放弃未完成会话并重新开始 |
| `POST` | `/roleplay/sessions/{id}/messages` | 提交患者问题并获取标准客服答复 |
| `POST` | `/roleplay/sessions/{id}/finish` | 结束练习并异步生成复盘 |
| `GET` | `/roleplay/sessions/{id}/summary` | 获取复盘状态或内容 |
| `POST` | `/roleplay/sessions/{id}/summary/retry` | 复盘失败后重试 |

角色互换的消息 `role` 只能为 `learner_patient` 或 `standard_customer`。标准客服消息在 `content` 外还返回：

```json
{
  "learningPoints": ["先回应患者的核心担忧，再说明可提供的服务协助。"],
  "complianceBoundary": "客服仅提供流程与预约协助，具体诊疗判断需由医生结合检查评估。"
}
```

每次会话最多 10 轮，至少完成 1 轮才能结束。提交消息继续使用 `clientMessageId` 保证幂等；模型失败时，已保存的患者问题通过 `pendingMessage` 返回，前端必须使用相同 ID 和内容重试。

复盘状态为 `generating`、`ready` 或 `failed`。`ready` 时返回以下无分数结构：

```json
{
  "summary": "整体接待总结",
  "coveredTopics": ["已覆盖问题"],
  "keyPrinciples": ["关键服务原则"],
  "nextPracticeSuggestions": ["后续练习建议"]
}
```

对应 PostgreSQL 表为 `roleplay_sessions`、`roleplay_messages` 和 `roleplay_summaries`；`scenarios.roleplay_config` 中的 `serviceGuidance` 仅供后端 Prompt 使用，接口只公开 `suggestedQuestions`。

— API 契约结束 —
