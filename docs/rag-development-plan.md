# 服务与专业知识 RAG 后续开发计划

版本：2.0  
日期：2026-09-19  
代码基线：`kysan173-arch/Oral-Training` `master@be64028ad5b121bbe5fc2b7a33d3a7a05380ca9d`  
状态：第一阶段最小闭环已合并；第二阶段“AI 患者 + 知识核验评分”待开发。  
目标读者：继续开发本项目的编码模型、人工开发者和审查者。

## 1. 文档目的

本文件取代旧版“R00—R14 全部待开发”的执行叙述，按当前 `master` 的真实实现重新安排后续工作。

- 架构边界仍以 [RAG 架构设计](rag-architecture-design.md) 为准。
- 已上线接口以 [API 文档](api.md) 为准。
- 本地演示数据准备见 [RAG 本地配置与数据初始化教程](RAG本地配置与数据初始化教程.md)。
- 开发必须遵守根目录 [AGENTS.md](../AGENTS.md)。

本文中的“客服训练”和“角色互换”含义固定如下：

| 名称 | 代码对象 | 学员角色 | AI 角色 | 当前 RAG 状态 |
|---|---|---|---|---|
| 客服训练 | `sessions`、`pages/training` | 客服 | 患者 | 未接入服务快照、AI 患者初始化和知识评分 |
| 角色互换 | `roleplay_sessions`、`pages/roleplay` | 患者 | 标准客服 | 已实现最小 RAG 回答、引用和 trace |

## 2. 当前结论

当前系统已经不是“尚未实现 RAG”，而是完成了第一阶段的轻量、非向量 RAG：

```text
管理员维护服务与知识
        ↓
发布不可变 revision，并生成中文检索块
        ↓
角色互换会话选择服务并锁定 revision manifest
        ↓
结构化服务事实精确读取 + PostgreSQL 中文全文检索
        ↓
DeepSeek 选择 evidenceId
        ↓
后端渲染事实、保存 trace/citation，前端展示依据
```

下一阶段的核心目标不是立即增加向量数据库，而是补齐产品主闭环：

```text
选择诊所服务
  → 生成并锁定 AI 患者画像
  → 学员完成客服对话
  → 提取学员事实陈述
  → 用同一会话证据逐项核验
  → 生成可追溯的五维报告与错题复练
```

## 3. 已完成能力

### 3.1 数据与知识管理

当前已存在并投入使用：

- `010_knowledge_catalog.sql`：服务、服务草稿/版本、知识条目、知识草稿/版本、知识块、生成任务、发布幂等和审计。
- 服务与知识发布版本不可原地修改或删除。
- 管理员可创建、编辑、生成模拟草稿、预览、发布、查看版本和归档。
- 模型生成内容固定为 `synthetic / unverified / demo`，不会自动发布或伪装成已审核资料。
- 草稿保存使用 `draftVersion` 乐观并发；发布与生成任务具有幂等和过期结果防覆盖机制。

### 3.2 检索

当前 `rag_retriever.cpp` 已实现：

- UTF-8 中文处理、全角规整、英文小写化。
- 中文双字切分、ASCII 稳定词项和少量内置同义词。
- 约 450 字目标块、约 60 字重叠，并优先在句末切分。
- PostgreSQL `simple` 配置的 `tsvector / tsquery / ts_rank_cd` 检索。
- manifest、训练范围、通用知识、服务专属知识和 topic 过滤。
- 最多返回 6 个知识块。
- 价格、包含项目、单次时长、总疗程、复诊间隔和预约信息直接读取服务 revision，不让模型自行计算。

### 3.3 角色互换最小 RAG

`011_roleplay_rag_mvp.sql` 和现有代码已经支持：

- 角色互换会话选择具体服务。
- `clientSessionId` 创建幂等。
- 会话锁定 `service_revision_id`、`knowledge_as_of` 和知识 revision manifest。
- 每轮从固定上下文检索证据。
- DeepSeek 只选择 `evidenceId`，服务端渲染价格、时间和原文摘述。
- 回答包含 `answered / partial / unknown / conflicted` 状态。
- 回答、引用、trace 和消息在胜出事务中保存，失败尝试不会公开。
- 学员只能读取本人会话的公开 evidence。
- 小程序可查看标准客服回答依据。
- 无 `serviceId` 的旧会话继续使用 v1 路径。

### 3.4 兼容与测试基础

- `rag_types.h` 已冻结 context/report、evidence、citation、knowledge check 等 v2 DTO。
- 报告读取端已经支持 `schemaVersion=2` 和合法 `null` 分数。
- 聚合、结果页、历史页、能力画像不会把“证据不足”误显示为 0 分。
- 已有 RAG contract、retriever、knowledge store、数据库迁移、管理 API 和客户端恢复测试。
- PR #7 记录过一次受控真实角色互换 RAG 烟测；该记录不等于完整第二阶段已经验收。

## 4. 原计划状态映射

| 原任务 | 当前状态 | 说明 |
|---|---|---|
| R00 契约冻结 | 已完成 | v2 DTO、模型注入点和 API 草案已存在 |
| R01 v2/null 读取兼容 | 已完成 | 读端、聚合和页面已兼容空分 |
| R02 目录与不可变版本 | 已完成 | 迁移 010 和 `KnowledgeStore` 已落地 |
| R03 管理 API 与生成队列 | 已完成主体 | API、生成队列和可信预览已存在 |
| R04 管理页面 | 已完成主体 | 尚需微信开发者工具最终视觉验收 |
| R05 中文检索 | 已完成 MVP | 缺正式人工标注 Recall@6 评测 |
| R06 会话快照 | 部分完成 | 仅角色互换接入；客服训练未接入 |
| R07 证据校验 | 部分完成 | 有安全渲染，但没有独立完整 `EvidenceValidator` |
| R08 角色互换 RAG | 部分完成 | 逐轮回答已接入；复盘仍未复用证据 |
| R09 AI 患者初始化 | 未开始 | 没有 migration 012、初始化任务或患者画像落库 |
| R10 两模式交互 | 部分完成 | 角色互换已完成；客服训练仍是旧入口 |
| R11 知识核验评分 | 未开始 | DTO 存在，执行模块不存在 |
| R12 报告 v2 写入闭环 | 未开始 | 目前只有读端兼容，评分仍写 v1 |
| R13 总体验收 | 未完成 | 缺固定评测集、初始化/评分并发回归和完整前端验收 |
| R14 发布联调 | 部分完成 | 仅角色互换做过一次真实模型烟测 |

## 5. 当前必须先处理的技术缺口

这些问题应在扩展客服训练前先修复，不能把它们带入第二条 RAG 链路。

### 5.1 manifest hash 不一致

当前角色互换上下文将 manifest 保存为 `md5:<hash>`，而 `RagRetriever::retrieve()` 在 `EvidenceBundle` 中使用 revision ID 拼接值。两者不是同一个 hash，无法可靠证明一次检索使用了哪个快照。

目标：

- 对规范化 manifest JSON 使用同一 SHA-256 算法。
- `training_contexts.manifest_hash`、`EvidenceBundle.manifestHash`、`rag_traces` 和报告中的 hash 必须完全相同。
- 检索器应接收已锁定的 manifest hash，不得自行产生另一种表示。

### 5.2 证据选择仍有过度回答风险

当前模型没有选择有效 evidenceId 时，规范化代码会自动取前两个可渲染证据。这可能把“检索到了但与问题不相关”的内容当成答案。

目标：

- 模型未选择合法证据时默认返回 `unknown`，不自动补选。
- 引用必须属于本次 trace、固定 manifest 和允许的服务范围。
- 数值事实继续由后端模板渲染。
- 自由文本中出现无依据价格、日期、时长、折扣或确定性疗效时，拒绝该输出或安全降级。

### 5.3 角色互换复盘尚未 grounded

当前逐轮回答使用 RAG，但 `roleplay_summary` 仍直接根据对话生成。复盘阶段可能重新组织出没有证据的专业事实。

目标：

- 复盘只复用本会话已公开 trace，或按同一 manifest 新建 `roleplay_summary` 检索 trace。
- 新增的专业解释必须有 citation；否则仅总结沟通过程，不补充事实。

### 5.4 AI 任务分发仍是二选一逻辑

`lockAiJobTarget()`、dedupe key、失败回写等位置仍按“evaluation，否则 roleplay_summary”处理。直接增加 `patient_initialization` 会把任务写到错误目标表。

目标：先改为显式枚举分发，未知任务类型立即拒绝，再增加患者初始化任务。

### 5.5 检索质量尚未量化

`zh-bigram-v1` 已可用，但同义词和 Top-K 固定在代码中，没有正式固定评测集，也没有真正产出 `conflicted` 的完整判定链。

目标：先建立指标，再决定是否需要混合向量检索；不得以架构升级代替评测。

## 6. 后续工作包

剩余工作建议拆成 N01—N07。每个工作包形成独立、可验证的 Conventional Commit；不得一次性改完所有阶段。

### N01 — 加固当前角色互换 RAG

对应原任务：R05、R07、R08 的剩余部分。  
预计：1—2 人日。  
前置：无，下一步立即执行。

主要改动：

1. 新增共享 SHA-256 工具，统一 manifest 规范化与 hash。
2. 修改 `RagRetriever::retrieve()`，显式传入并返回锁定的 manifest hash。
3. 提取最小 `EvidenceValidator`，至少校验 evidenceId、revision、manifest、服务范围和可渲染字段。
4. 删除“模型未选证据时自动取前两个”的行为。
5. 对无依据数字、金额、日期、时长、折扣和绝对化承诺实施安全降级。
6. 让角色互换复盘复用本会话证据，不再自由补充服务事实。
7. 为上述行为增加纯离线测试，不调用真实模型。

建议文件：

- `backend/src/evidence_validator.h/.cpp`
- `backend/src/rag_retriever.h/.cpp`
- `backend/src/main.cpp`
- `backend/src/reliable_store.h`
- `backend/tests/evidence_validator_test.cpp`
- `backend/tests/rag_retriever_test.cpp`
- `docs/api.md`

验收：

- 同一会话的 context、trace、citation 和 summary 使用同一个 SHA-256 manifest hash。
- 无合法 evidenceId 时回答为 `unknown`，不会自动引用任意命中块。
- `3980 元起/颗` 不会被渲染为 `3980 元总价`。
- 其他 trace 的 `E1`、其他服务 revision 和未公开 trace 均不可引用。
- 角色互换复盘新增的专业事实全部可追溯。

### N02 — 客服训练上下文与患者初始化基础设施

对应原任务：R06、R09。  
预计：2—3 人日。  
前置：N01。

新增迁移 `012_patient_initialization_jobs.sql`，不得修改已发布的 010/011。迁移至少需要：

- 为 `sessions` 增加可空的 `service_id`、`service_revision_id`、`client_session_id` 和 `context_version`。
- 扩展 `training_contexts`，保存初始化状态、generation、错误、私有画像、公开画像和可变患者状态。
- 扩展 `ai_jobs.job_type` 约束与索引，支持 `patient_initialization`。
- 为新旧客服训练分别建立正确的 active/idempotency 索引。

后端工作：

1. 将 AI 任务锁定、dedupe、claim、renew、fail、过期回收和目标失败回写改为显式任务类型分发。
2. `POST /sessions` 接受 `scenarioId + serviceId + clientSessionId`；有服务的新路径返回 `202` 和初始化状态，旧签名继续兼容。
3. 创建会话时在同一短事务内锁定服务 revision、knowledgeAsOf、完整 manifest 和 SHA-256 hash，并入队初始化任务。
4. 提供初始化读取与显式重试接口；`pending/generating` 时禁止发送消息、请求提示和结束训练。
5. 相同 `clientSessionId` 同参返回原会话，异参返回 `409 IDEMPOTENCY_CONFLICT`。

验收：

- 旧客服训练不受影响。
- 相同请求并发创建只产生一个会话、一个 context 和一个有效初始化 generation。
- 未知 job type 不会落到 `roleplay_sessions`。
- 进程重启、租约过期和 stale attempt 不会覆盖新结果。
- 新发布知识只影响新会话，续练始终沿用原 manifest。

### N03 — RAG 驱动的 AI 患者与逐轮状态

对应原任务：R09。  
预计：2—3 人日。  
前置：N02。

会话初始化输出必须是结构化画像，至少包括：

```json
{
  "publicProfile": {
    "displayName": "咨询中的李女士",
    "ageRange": "30-39",
    "initialEmotion": "犹豫"
  },
  "privateProfile": {
    "consultationGoal": "咨询单颗种植牙",
    "budget": "5000元以内",
    "concerns": ["疼痛", "价格", "恢复时间"],
    "hiddenInformation": ["正在比较另外两家诊所"],
    "objections": ["为什么你们更贵"],
    "revealPolicy": {
      "budget": "被主动询问后透露",
      "competitor": "第三轮后或被追问时透露"
    }
  }
}
```

实现要求：

1. 为 `IModelGateway` 增加患者初始化和 grounded patient reply 契约，沿用单一 DeepSeek 网关与底层重试器。
2. 初始化使用 `PatientInitialization` 证据包；画像只能基于锁定服务和知识，不得生成不存在的诊所能力、优惠或实时号源。
3. 每轮根据当前学员回复，以 `PatientReply` 目的检索相关事实和知识，再结合固定画像生成患者反应。
4. 私有画像永不返回前端；前端只能看到公开画像、当前情绪和患者说出的内容。
5. 患者状态至少包含情绪、信任度、已经披露的信息、已经触发的异议和结束原因，并在消息事务中推进。
6. 续练复用原画像和状态，不重新生成患者。
7. round 0 开场不占学员最多 10 轮配额。

验收：

- 同一会话重进、断网恢复或换页后，患者身份、预算和隐藏信息保持一致。
- 未达到披露条件时，私有预算和竞品信息不会泄露。
- 学员报错价格或作出绝对化承诺时，患者会基于证据提出合理质疑，而不是接受错误事实。
- 无资料时患者可以追问或表示需要确认，但不会替诊所补造答案。

### N04 — 客服训练服务选择与初始化交互

对应原任务：R10。  
预计：1.5—2 人日。  
前置：N02、N03。

前端工作：

1. 客服训练与角色互换共用已发布服务选择入口，再按服务过滤兼容场景。
2. 保存稳定 `clientSessionId`，网络重试、鉴权刷新和页面恢复期间不得重新生成。
3. 展示初始化 `pending / generating / failed / ready`；失败态提供显式重试。
4. `ready` 前禁用输入、提示和结束按钮。
5. 聊天中只展示公开患者画像和情绪，不展示证据答案；证据在训练报告生成后开放。
6. 新旧会话按 `contextVersion` 分流，旧会话继续原有行为。
7. 页面 hide/unload 停止轮询，onShow 根据 sessionId 恢复；迟到响应不得覆盖新会话。

验收：

- 空服务目录、服务归档、场景不兼容、初始化失败和登录过期均有可操作状态。
- 重复点击开始、切换两种模式和恢复旧会话不会串 serviceId 或 sessionId。
- 同一场景的不同服务分别保存续练状态和历史记录。

### N05 — 学员陈述核验与确定性知识评分

对应原任务：R11。  
预计：3—4 人日。  
前置：N01、N03。

建议新增 `knowledge_evaluator.h/.cpp`，处理流程：

```text
学员消息
  → 提取事实陈述及原句位置
  → 服务端验证原句确实存在且说话者正确
  → 按 topic/field 从固定 manifest 检索
  → 结构化字段确定性比较，专业解释受证据约束判断
  → 去重、处理后续纠正
  → 后端按固定 rubric 计算知识分
```

判定枚举沿用 DTO：

- `supported`
- `contradicted`
- `incomplete`
- `evidence_missing`
- `conflicted`
- `not_applicable`

规则：

1. 模型只提取候选陈述和选择证据，不直接决定最终分数或 `scoreImpact`。
2. 金额、单位、范围、起价条件、时间和包含项目由后端确定性比较。
3. `originalQuote` 必须是对应学员消息的真实子串；虚构原句不得进入报告。
4. 患者报价、竞品价格、假设句、疑问句和转述不得误判为本诊所承诺。
5. 同一事实重复错误只形成一个评分单元；后续主动纠正应保留过程并更新最终状态。
6. `evidence_missing / conflicted / not_applicable` 不进入知识准确率分母。
7. 可判定单元为 0 时，`knowledgeAccuracy`、`totalScore` 和 `passed` 均为 `null`，不把其他四维重新加权。
8. 事实错误与医疗合规违规可分别影响不同维度，但必须分别说明原因。

确定性示例：

| 资料 | 学员表达 | 判定 |
|---|---|---|
| `3980 元起/颗` | “就是 3980 元” | `incomplete`，缺少起价和单位条件 |
| 疗程因骨量而异 | “三个月一定完成” | `contradicted`，并触发合规检查 |
| 无实时号源 | “我现在给您锁定明天下午” | `contradicted` |
| 资料没有成功率 | “具体效果需要医生检查后评估” | 不判错，可视为合规表达 |

验收：

- 转述、否定、假设、单位错误、重复错误、主动纠正和证据冲突都有固定测试。
- 知识分由后端相同输入稳定计算，模型不能改变权重。
- 每个可见核验项都能展开到原句和证据。

### N06 — 报告 v2、错题复练与完整测试

对应原任务：R12、R13。  
预计：3—4 人日。  
前置：N04、N05。

报告工作：

1. evaluation job 按 `contextVersion` 选择 v1/v2；v2 使用固定 manifest 完成陈述核验和沟通维度评价。
2. 后端生成 `knowledgeAssessment`、`knowledgeChecks` 和 `knowledgeManifestHash`，并计算最终五维总分。
3. 报告、公开 trace、SQL 总分、任务完成和 session 状态在胜出事务中提交。
4. 结果页和历史页显示原句、结论、依据、正确表达、覆盖率及空分原因。
5. 错题本只收录已核验错误；证据缺失不是错题。
6. 从旧错题进入复练时使用当前服务版本并提示版本变化；原报告仍读取旧 revision。

固定评测集：

- 至少 80 条人工标注 case。
- 覆盖价格/单位/范围、时间、预约、同义表达、未知、冲突、跨服务、否定/转述/纠正和提示词注入。
- expected 由人工规则和固定 fixture 给出，不能用当前模型输出自证。

硬门槛：

| 指标 | 门槛 |
|---|---:|
| 结构化字段值、单位、范围和条件准确率 | 100% |
| 跨服务证据污染 | 0 |
| 公开引用属于当前 trace 与 manifest | 100% |
| 明确未知 case 的编造数量 | 0 |
| 专业知识 Recall@6 | ≥ 90% |
| 越权读取成功数 | 0 |
| stale attempt 可见写入数 | 0 |

完整回归：

- 空库升级、历史库升级、迁移重跑和失败回滚。
- 初始化、消息末轮、报告和 Worker 租约并发。
- v1/v2/null 报告混合读取与聚合。
- CTest、静态检查、迁移脚本、并发脚本、状态机和不带真实模型的 smoke。
- 微信开发者工具覆盖两种模式、续练、断网恢复、历史、结果、知识管理和错题复练。

实施时新增 `docs/rag-validation-report.md`，记录实际命令、环境、退出码、Passed/Failed/Skipped、指标分母和剩余限制。

### N07 — 受控 DeepSeek 联调与分段发布

对应原任务：R14。  
预计：1—2 人日。  
前置：N06 全部门槛通过。

1. 增加并验证 `RAG_ROLEPLAY_ENABLED`、`RAG_PATIENT_ENABLED`、`RAG_EVALUATION_V2_ENABLED`；默认状态和依赖关系写入配置文档。
2. 开关关闭时阻止新 v2 会话，但保留已有 v2 会话、历史报告和证据读取，不回退到无证据回答。
3. 在预算明确的单次受控批次中覆盖草稿生成、患者初始化、两模式消息、评分和复盘。
4. 记录真实模型 ID、提示词版本、调用次数、usage、超时、重试和失败类型。
5. 使用人工复核后的演示服务和知识完成端到端演示；模拟资料继续明确标记，不描述为真实诊所报价或医疗指南。
6. 演练暂停新建、Worker 故障、恢复服务和继续旧会话。

验收：

- 两种训练模式均使用固定服务和知识版本。
- AI 患者、AI 客服、评分和复盘的专业事实均可追溯。
- 关闭与恢复开关不破坏 v2 null 报告、患者画像、引用或旧版本历史。
- API 文档、本地教程、环境变量和最终验收报告与代码一致。

## 7. 推荐实施顺序与发布门槛

| 阶段 | 工作包 | 可发布能力 | 门槛 |
|---|---|---|---|
| S0 | N01 | 稳定的角色互换 RAG | hash、引用与复盘校验通过 |
| S1 | N02—N03 | 后端 AI 患者闭环 | 初始化并发、隐私和续练测试通过 |
| S2 | N04 | 客服训练内测入口 | 前端恢复与兼容测试通过 |
| S3 | N05—N06 | 有证据的评分和复练闭环 | 80 条评测与全部离线回归通过 |
| S4 | N07 | 正式演示/发布 | 受控真实模型联调和回退演练通过 |

客服训练 RAG 不得在 N05—N06 完成前对普通学员默认开放，避免出现“能对话但不能可信评分”的半成品路径。

## 8. 数据库与 API 约束

### 8.1 迁移

- 已发布的 001—011 永不修改。
- 下一迁移固定使用 `012_patient_initialization_jobs.sql`。
- 如报告确实需要关系字段，再新增 013；纯 JSONB 字段不创建空迁移。
- 所有迁移不得联网、调用模型、自动生成知识或清理未确认范围的数据。
- 在空库、代表性历史库和重复执行三种场景验证。

### 8.2 版本与快照

- 新会话锁定当前已发布服务 revision 和完整知识 manifest。
- 已开始会话不随新发布内容漂移。
- 缓存键必须包括 service revision、manifest hash、purpose 和 tokenizer version。
- 找不到旧 revision 时返回可重试/可诊断错误，不能悄悄替换为 current revision。

### 8.3 未知、冲突与故障

| 情况 | 正确行为 |
|---|---|
| 已发布资料明确未知 | 业务成功，返回 `unknown` |
| 部分字段已知 | 返回 `partial`，只回答有证据部分 |
| 证据冲突 | 返回 `conflicted`，不自行选边 |
| 数据库、检索器或模型故障 | 5xx/可重试错误，不伪装成“资料没有提供” |
| 完全无法核验学员知识 | 报告 ready，但知识分、总分和 passed 为 null |

## 9. 暂不实施的复杂化

在 N06 的固定评测完成前，不实施：

- 独立向量数据库。
- 外部 Embedding 微服务。
- reranker 服务。
- 知识图谱。
- 多诊所租户。
- OCR/PDF 自动导入、网页爬虫或实时医疗资料同步。
- 实时预约交易或真实患者病历接入。

只有专业知识 `Recall@6 < 90%`，且误差分析证明是语义召回问题而非知识缺失、分块、同义词或范围过滤问题时，才进入混合检索设计：

```text
PostgreSQL 全文检索 + pgvector 语义检索
                ↓
              RRF 合并
                ↓
       可选轻量 reranker
```

即使升级检索，价格、时间、项目范围和预约字段仍走结构化精确路径。

## 10. 每个工作包的执行规则

1. 开始前读取 `AGENTS.md`、本计划、相关设计章节和当前目标文件。
2. 记录分支、基线 SHA、最新迁移号和工作区已有修改。
3. 不改变现有 DeepSeek URL、底层请求参数、解析和重试机制，除非任务明确要求契约变更。
4. 模型网络调用必须在数据库事务外；消息、状态、trace、引用和任务完成必须在胜出事务中原子提交。
5. 离线测试优先使用固定 fixture/FakeModelGateway；N07 前不重复消耗真实模型调用。
6. 数据库测试只能使用名称明确包含 `test` 或 `ci` 的可丢弃数据库。
7. 无法运行 Windows 构建或数据库测试时，标记 `Not run` 或 `Skipped`，不得声称通过。
8. 每个提交同步更新 `docs/api.md` 和本文件状态；使用聚焦 Conventional Commit。

## 11. 下一任务可直接使用的提示词

```text
请在 kysan173-arch/Oral-Training 的最新 master 上执行
docs/rag-development-plan.md 中的 N01“加固当前角色互换 RAG”。

先读 AGENTS.md、rag-architecture-design.md、rag-development-plan.md、
rag_types.h、rag_retriever.*、main.cpp 中 grounded roleplay 流程、
reliable_store.h 中 training_contexts/rag_traces/roleplay summary 逻辑。

本任务只完成：
1. manifest 规范化 SHA-256 全链路一致；
2. 最小 EvidenceValidator；
3. 删除无合法选择时自动引用前两个证据；
4. 角色互换复盘证据约束；
5. 对应离线测试和 API 文档。

不得开始 migration 012、AI 患者或评分开发；不得改变旧 v1 会话行为；
不得让模型自由渲染价格、时间、项目范围或预约信息。

完成后报告实际修改文件、测试命令与结果、未覆盖风险和下一可执行任务。
```

## 12. 最终完成定义

只有同时满足以下条件，才可以宣布 RAG 开发完成：

- 管理员能生成模拟草稿、人工核对、预览、发布、归档和追溯版本。
- 两种训练模式均先选择具体服务，并锁定相同语义的服务/知识快照。
- AI 患者画像稳定、隐藏信息不泄露、续练不重生成。
- AI 客服的价格、时间、项目范围和专业解释可追溯，无资料时明确未知。
- 学员每个知识错误都能定位到真实原句、核验结论和证据。
- 五维评分由固定规则合成；证据不足时不会伪造 0 分或 100 分。
- 报告、历史、能力画像、话术和错题本兼容 v1/v2/null。
- 迁移、并发、权限、离线指标、前端恢复和受控模型联调均有实际记录。
- 功能可以分段关闭和恢复，同时保留历史画像、引用和报告可读。

不能以“接口返回 200”“页面有引用按钮”或“把全文塞进 Prompt”代替以上验收。

## 13. 交接记录模板

每完成一个工作包，在本节追加记录：

```markdown
### Nxx — 工作包名称
- 状态：待开发 / 进行中 / 阻塞 / 已验收
- 基线与提交：
- 实际修改文件：
- API / DTO / 数据库变化：
- 新配置及默认值：
- 验证：命令、环境、退出码、Passed/Failed/Skipped/Not run
- 指标与测试覆盖：
- 与计划偏差及理由：
- 已知限制：
- 下一任务：
```

### 当前交接

- 状态：N01 待开发；N02—N07 依赖顺序待开发。
- 当前基线：`master@be64028ad5b121bbe5fc2b7a33d3a7a05380ca9d`。
- 最近 RAG 功能提交：PR #7，`377464287c935c585ea4e3464a66688bf7dfc63d`，已合并。
- 最近文档提交：`112a3f52206a3c61857303efb75710d339584e9e`，已合并。
- 下一步：从 N01 开始，不要直接跳到向量检索或评分页面。
