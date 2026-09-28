# 服务与专业知识 RAG：详细开发执行计划

版本：1.0  
日期：2026-09-14  
状态：待开发；本文中的任务、测试文件及配置扩展均为计划，不代表已经实现或验收。  
目标读者：接手开发的编码模型、人工开发者和审查者。

## 1. 使用方式与依据

本计划将 [RAG 完整设计](rag-architecture-design.md) 拆成可以独立提交、验收和交接的工作包。设计文档定义产品与架构，本计划定义实施顺序及完成条件。实现时同步维护 [API 文档](api.md)，不要将设计中的拟议接口描述为已上线接口。

已核对的来源：

- 设计文档：`chinesemisaka/Oral-Training` 的 `docs/rag-architecture-design@e120a4c929247f8e7d4f829762d653cd5ae4cc81`。
- 代码基线：`master@317440512c6dc47a477af1a99b085226ed119348`；设计分支沿用此代码。
- 仓库规则：根目录 [AGENTS.md](../AGENTS.md)。
- 实际核对：`backend/src/main.cpp`、`backend/src/reliable_store.h`、`backend/CMakeLists.txt`、`utils/api.js`、Windows PostgreSQL CI 和目录树。
- 上游 `kysan173-arch/Oral-Training` 与个人仓库不是同一个主分支基线。执行模型先确认目标分支，不得把同名旧项目 `oral-training-ai-coach` 当作本项目。

执行规则：

1. 每次接手先读本节、设计对应章节、适用 AGENTS.md 和当前任务涉及代码。
2. 从依赖已完成的第一个任务开始；未经验证的已有实现不能直接标记完成。
3. 一个工作包形成一个聚焦提交；若拆多个提交，每个提交都保持可构建或用明确关闭的功能开关隔离。
4. 每次提交填写第 12 节交接记录，报告实际测试结果、未覆盖风险和下一任务。
5. 发现设计与代码冲突时先记录差异及最小兼容方案；不得自行改成向量微服务、改变评分权重或省略引用校验。
6. 本次仅新增本计划。后续功能开发、上线和真实模型测试按任务门槛执行。

## 2. 不可改变的交付边界

| 项目 | 执行约束 |
|---|---|
| 技术栈 | 原生微信小程序 + Windows C++17/Crow/WinHTTP/libpqxx + 单 PostgreSQL |
| 机构 | 单诊所；learner 与 admin；知识管理权限不等于读取学员明细权限 |
| 知识来源 | 服务结构化事实 + 专业知识；无销售策略 RAG、无病历库 |
| 检索 | 服务 ID/版本精确查询 + 应用侧中文词项 + PostgreSQL simple 全文检索 |
| 模型 | 沿用 DeepSeek 网关，新增契约按 v2 分流；不假设有 embedding 接口 |
| 数据准备 | LLM 生成模拟草稿，经结构校验和发布流程进入 demo；不得自动标记 reviewed |
| AI 患者 | 客服训练 pages/training：学员是客服，AI 是患者 |
| AI 客服 | 角色互换 pages/roleplay：学员是患者，AI 是客服；该模式仍不评分 |
| 会话 | 服务与知识 manifest 固定；画像仅存在所属会话；续练不重生成 |
| 未知/故障 | 无资料正常返回未知；检索失败返回可重试故障，不能伪装无资料 |
| 评分 | 五维权重不变；不可核验不判错；完全无依据时知识分、总分、passed 为 null |
| 兼容 | 旧会话走旧路径；最多 10 轮、续练、异步报告、成长与学习功能保留 |
| 不做 | OCR/PDF 导入、爬虫、多租户、独立问答入口、实时预约交易、Redis/Python 常驻服务 |

## 3. 任务依赖与发布顺序

以下工作量为人工专注开发的粗估，用于控制任务大小，不是模型运行时间或交付承诺。完整实现约 24—36 人日，按验收推进，不因时间不足跳过核心门槛。

| ID | 工作包 | 前置任务 | 预计人日 | 对应设计 |
|---|---|---|---:|---|
| R00 | 基线复核、契约冻结、测试注入点 | 无 | 1 | §2、10、13、16 |
| R01 | v2 报告读取、null 展示与聚合兼容 | R00 | 2—3 | §9.4、15 |
| R02 | 目录/草稿/版本/审计迁移与存储 | R00 | 2 | §4、5 |
| R03 | 管理 API、草稿生成队列和发布 | R02 | 2—3 | §5、10.2、11 |
| R04 | 知识管理页面组 | R03 | 1.5—2 | §5 |
| R05 | 中文切块、词项、索引和确定性检索 | R02 | 2—3 | §6 |
| R06 | 会话上下文、版本快照、服务级幂等 | R02、R05 | 2 | §4.3—4.4、7 |
| R07 | 证据契约、引用校验和事实渲染 | R00、R05、R06 | 2—3 | §6.3、8 |
| R08 | 角色互换 v2 回答与复盘 | R01、R06、R07 | 1.5—2 | §8、11 |
| R09 | 显式任务类型分发与患者初始化 | R01、R06、R07 | 2—3 | §7、12 |
| R10 | 两模式服务选择、初始化及证据 UI | R04、R08、R09 | 1.5—2 | §5、7、8、10 |
| R11 | 陈述提取、证据核验与确定性知识评分 | R05、R06、R07、R09 | 2—3 | §9.1—9.3 |
| R12 | 报告 v2 写入、学习派生和闭环 UI | R01、R10、R11 | 1.5—2 | §9.4 |
| R13 | 端到端离线评测、迁移与并发回归 | R03—R12 | 2—3 | §12、14 |
| R14 | 受控 DeepSeek 联调、分段开启与回退演练 | R13 | 1—2 | §11、14、15 |

建议顺序为 R00 → R01 → R02—R07 → R08 → R09—R10 → R11—R12 → R13—R14。表中依赖不代表允许跳过整个交付链。

发布门槛：

- P0：R01 上线或与后续功能同批发布，使系统先能读取 v2/null。
- P1：R02—R07 完成后开放内容管理；训练 v2 暂不开启。
- P2：R08 和角色互换 UI 完成后先开放角色互换 v2。
- P3：R09—R12 完成并通过核心回归后开放客服训练 v2。不能让用户进入尚不能完成评分闭环的新训练。
- P4：R13—R14 全部验收后正式交付。

同一模型可以顺序执行全部任务。若以后明确采用多个模型同时开发，先冻结 R00 的 DTO/API；分配不重叠文件，`main.cpp`、`reliable_store.h`、迁移编号和 `utils/api.js` 由集成负责人统一合入。

## 4. 开发前冻结的实现契约

### 4.1 公共类型与文件边界

新增 `backend/src/rag_types.h`（本计划新增建议）只放 DTO、枚举与版本常量，不依赖 Crow 路由或具体数据库实现。设计已规划的模块：

| 文件 | 输入与输出 | 责任边界 |
|---|---|---|
| knowledge_store.h/.cpp | 草稿命令 → 草稿/不可变修订；发布命令 → revision ID | admin 存储、并发版本、审计、目录 |
| rag_retriever.h/.cpp | RetrievalRequest → EvidenceBundle | 中文索引、固定 manifest 过滤、事实查询、召回 |
| training_context.h/.cpp | 会话/服务选择 → TrainingContext；初始化结果 → 原子落库 | 生命周期、快照、画像、公开投影 |
| evidence_validator.h/.cpp | 结构化输出 + EvidenceBundle → 校验结果/渲染答复 | 引用、单位、条件、版本、输出各表面 |
| knowledge_evaluator.h/.cpp | 学员陈述 + 问题 + 证据 → 核验单元/知识分 | 去重、纠正、确定性计分 |
| main.cpp | 请求/任务 → 调用上述模块 | 最小编排改动和 ModelGateway v2 分流 |
| reliable_store.h | 消息/报告/队列 → 可靠事务 | 复用消息租约、generation、结果写入和用户隔离 |

必须先解决现有匿名 namespace 中最小共享类型的可见性，禁止新模块反向 include main.cpp。可让测试使用 FakeModelGateway 或注入 completion 函数；不为测试重构整个服务。

DTO 最低字段：

- TrainingContext：contextId、contextVersion=2、serviceRevisionId、knowledgeAsOf、manifest、manifestHash、patientProfile、initialization。
- RetrievalRequest：contextId、purpose、currentQuestion、最近两组完整问答、待核验 topic/field；serviceId 由服务端会话确定。
- EvidenceBundle：沿设计 §6.3；额外固定 manifestHash、tokenizerVersion；区分 facts/passages/missingFields/conflicts/retrievalStatus。
- CitationRef：traceId + evidenceId；revisionId、原文、来源由服务端展开，模型不填写可任意访问的引用 URL。
- GroundedResult：reply、answerStatus、citations、learningPoints、complianceBoundary、shouldEnd。
- KnowledgeCheck：沿设计 §9.2；originalQuote 必须对应真实学员消息及轮次。
- ReportV2：schemaVersion=2、原有报告字段、knowledgeAssessment、knowledgeChecks、knowledgeManifestHash；可空数字采用显式 optional/null。

`contextVersion` 为 API 字段，`context_version` 为数据库列；`schemaVersion` 为报告 JSON 字段。不要混用版本检测方式。

### 4.2 数据与迁移约定

从实际最新迁移号顺延；基线为 001—009，拟采用：

1. 010_knowledge_catalog.sql：设计 §4.3 的目录、草稿、修订、块、关联、管理任务和审计。
2. 011_training_rag_context.sql：两类 session 的 service_id/context_version/client_session_id，training_contexts、rag_traces、消息引用和 active 部分唯一索引。
3. 012_patient_initialization_jobs.sql：初始化状态和 ai_jobs 类型约束/索引。
4. 013 仅在确需关系字段时创建，不为纯 JSONB 扩展创建空迁移。

补充实施规则：

- training_contexts 两种会话 FK 恰好一个非空，各自唯一；revision 被引用后禁止物理删除。
- service_id + service_revision_id 的归属关系由数据库约束或事务显式校验；不能把 B 服务 revision 绑定 A。
- manifest 内 revision 必须在创建事务的一致快照内读取；可以使用 REPEATABLE READ，不能多次 READ COMMITTED 查询混成并不存在的版本组合。
- 服务与知识发布事务写 revision、chunks、current 指针、audit，全部成功才可见。
- 草稿保存及发布都检查 draftVersion；幂等键需与规范化请求摘要一起存储。同键同参返回原结果，同键异参 409。
- 两模式各按 (user_id, client_session_id) 幂等；新旧 active 会话分别使用 service_id IS NOT NULL 与 IS NULL 的部分唯一索引。
- 重复创建同组合但不同幂等键时不得制造第二个 active 会话；建议返回 409 ACTIVE_SESSION_EXISTS 和本人已有会话 ID，由客户端进入续练。
- 客服训练初始化状态与 evaluation_status 分离；初始化 ready 不能误标评分 ready。
- 迁移不联网、不调用 LLM、不生成服务样例、不修改已发布迁移。
- FK 级联规则不得形成删除会话 → 删除公共知识的反向链路。

### 4.3 API 与错误约定

所有接口完整列表沿设计 §10，R00 在 docs/api.md 的“待实现 RAG v2”区登记，任务验收后迁入正式章节。保留 {code,message,data} 与 bearer 身份。

| 场景 | 本计划冻结的响应规则 |
|---|---|
| 新建客服训练 v2 | 202；data 含 sessionId、contextVersion、serviceSummary、initialization.status=pending |
| GET 会话初始化中 | 200 返回 pending/generating/failed/ready，不靠 HTTP 500 触发页面等待 |
| 角色互换 v2 创建 | 201；相同幂等重放返回原会话，不重新锁版本 |
| 初始化前发消息/结束/提示 | 409 PATIENT_INITIALIZATION_PENDING；失败态使用 PATIENT_INITIALIZATION_FAILED |
| 服务不可选/场景不兼容 | 409 SERVICE_NOT_AVAILABLE / SERVICE_SCENARIO_MISMATCH |
| 草稿冲突/发布参数冲突 | 409 DRAFT_VERSION_CONFLICT 或统一幂等冲突码 |
| 输入 schema 错误 | 400 INVALID_ARGUMENT；附字段级问题，不回显密钥 |
| 未登录/无管理权限 | 401 / 403，沿用当前身份错误码 |
| 检索或模型故障 | 503 及可重试业务码；永久非法模型输出按现有体系区分 |
| 无资料/部分资料/冲突 | 200 + answerStatus=unknown/partial/conflicted |
| 证据读取 | 验证本人会话、trace 属于会话、证据已经公开；不可访问统一无资源响应，避免枚举 |

新增 HTTP 细节属于本计划的实现决定；若现有 API 错误体系要求不同状态，R00 统一调整并写明理由，不由各任务分别猜测。

`utils/api.js` 现有 createSession(scenarioId) 等调用要保留旧签名或提供兼容包装。新增对象参数必须在所有调用点完成迁移，禁止悄悄把字符串序列化成错误请求体。clientSessionId 创建一次并在网络超时、鉴权重试、页面恢复时复用；只有明确新练习/重启才更换。

### 4.4 状态、开关与预算

患者初始化：pending → generating → ready；失败 → failed；显式 retry 推进 generation 后回到 pending。每个 generation 只允许一个有效画像和 round=0 开场。stale attempt 不可更新新状态。

基础配置沿设计 §12.2。为分段发布建议新增 `RAG_ROLEPLAY_ENABLED`、`RAG_PATIENT_ENABLED` 和 `RAG_EVALUATION_V2_ENABLED`；均为本计划拟新增项，默认关闭。开启 RAG_PATIENT_ENABLED 必须同时满足报告读取与评分写入能力就绪，配置不合法启动时拒绝启用该路径。

RAG_ENABLED=false 阻止新 v2 会话，保留 v2 读取；紧急暂停已存在 v2 生成时返回明确停用状态，不能回落旧无证据回答。知识生成开关独立，停用生成不影响已发布内容。

模型预算按设计 §11.3；底层重试器保持唯一。跨 Worker 尝试累计 actualCallCount/usage，业务修复最多一次；超限失败，不层层重新获得完整预算。

## 5. 工作包执行说明

### R00 — 基线复核、契约冻结与可测试接口

**输入**：AGENTS.md、本计划、完整设计、当前目标分支。  
**改动**：rag_types.h、docs/api.md 待实现区、测试注入的最小接口。

步骤：

1. 记录工作分支 SHA、未提交文件、最新迁移号；比较设计基线后的相关变更，不覆盖他人修改。
2. 定位 ModelGateway 四类方法、Service::processJob、队列锁/失败/修复分支，以及全部 null 分消费点。
3. 固定 §4 DTO、JSON 示例、错误码、幂等摘要、枚举；新增字段默认值保证旧路径不变。
4. 规划 FakeModelGateway 对空 JSON、截断、超时、无效引用、次数超限的可控响应。
5. 建立任务进度与测试记录；核对 Windows CI 的入口和本机 PostgreSQL 版本。

**验收**：现有构建与 CTest 基线记录完整；新 DTO 可以独立 include；无模型请求参数/旧提示词变更。  
**交接**：冻结类型、基线差异、最终迁移编号和测试命令。

### R01 — 先完成报告读取与 null 全链路兼容

**改动**：main.cpp 的 normalizeReport/序列化/维度累计；reliable_store.h 的 repairEvaluationState、dashboard、learningProfile、learningMine、supervisorDashboard；utils/api.js 的 formatScore；结果/历史/画像/主管页面和雷达组件。

步骤：

1. 按 schemaVersion 显式分流，缺省旧报告仍走 v1。合法 v2 insufficient_evidence 不修成 0、不自动重评。
2. 数据库 row 读取先判空，消除 v2 路径上直接 as<int>()、Number(null)、缺省零分。
3. 总分统计过滤 total_score IS NOT NULL；达标率分母改为 scoredCount，completedCount 保留全部完成会话；维度逐维计算非空均值。
4. 无评分样本时平均分/最佳分显示“暂无评分”，不画零分点；v1 UI 仍按旧含义显示。
5. 高分成就和按分数奖励不从 null 推导；完成次数、签到保持原规则。
6. 用固定 v1/v2 报告 fixture 验证旧损坏修复与新合法空分互不干扰。

**验收**：混合报告 80、null、60 的均分 70，已评分 2、未评分 1；重复读取 null 报告不新入队；缺失维度不显示为能力为零。  
**禁止**：此阶段不得开始生产 v2 报告，只建立读取能力。

### R02 — 目录与不可变版本存储

**改动**：010 迁移、knowledge_store.h/.cpp、CMake、数据库测试。

步骤：

1. 按设计建表、FK、唯一约束和索引；ID 与用户类型沿现有数据库。
2. 完成 service/knowledge 草稿 schema；金额整数、范围顺序、单位条件、日期、unknown 都在服务端校验。
3. 存储读取与发布支持 demo/verified；generated 内容只能 synthetic/unverified，verified 不自动收纳。
4. 实现版本内容 hash、公开投影、审计；价格说明由字段渲染，正文不能覆盖报价。
5. 对旧库、新库、迁移重跑验证；失败时回滚事务，旧 current_revision_id 不变。

**验收**：重复 version 被拒绝；被引用 revision 不可删；非法价格/时长不发布；发布失败看不到半份版本。

### R03 — 管理 API 与可靠模拟草稿生成

**改动**：main.cpp 管理路由、knowledge_store、knowledge_admin_jobs 执行器、新版本化提示词。

步骤：

1. 实现设计 §10.2 的全部管理端点，每个请求校验 admin。
2. PUT 草稿采用 draftVersion；publish 使用草稿版本、幂等键和主行锁；archive 只影响新会话选择。
3. 单独管理队列实现 claim/lease/heartbeat/retry/fail，generation 与 attempt 限制胜出结果；并发初始为 1。
4. DeepSeek 仅生成 schema 合法草稿，不直接发布；生成期间管理员编辑不能被旧结果覆盖，保留候选草稿或返回冲突。
5. 管理生成数量设服务端上限；空响应、截断、超时、部分非法批次有可见结果，不能冒充全部成功。
6. preview 明确指定 draftVersion，在临时视图中使用 R05/R07 能力；未完成检索前可先关闭预览入口，不以自由模型问答替代。

**验收**：learner 全部写接口 403；并发保存一个成功一个 409；同键发布不重复版本；过期任务不覆盖新草稿；生成内容没有伪造来源与 reviewed 标记。  
**完成条件**：预览在 R05/R07 后补齐才算 R03 完成。

### R04 — 管理页面组

**改动**：pages/knowledge-admin、pages/service-editor、pages/knowledge-editor、pages/mine、app.json、utils/api.js。

步骤：

1. 新建服务/知识/任务三分区；不增加底部 Tab。
2. 表单分离价格、包含项目、单次时长、全程周期、复诊及预约说明；未知为显式选项。
3. 显示 draftVersion 冲突并允许重新加载，禁止用旧内容自动覆盖。
4. 生成任务显示排队/执行/失败/重试；页面 hide/unload 停止轮询，重入恢复任务状态。
5. 发布前展示字段差异、模拟标识及有效期；版本详情只读。
6. 普通用户无管理入口；接口 403 时给出正常错误状态。

**验收**：可完成生成 → 编辑 → 预览 → 发布 → 版本比较 → 归档；服务清单即时反映发布状态，失败时保留输入。

### R05 — 确定性检索与中文索引

**改动**：rag_retriever.h/.cpp、knowledge_chunks 发布接入、固定检索 fixture 与离线评测入口。

步骤：

1. 实现 Unicode 码点处理、全角规整、别名字典、中文双字片段、稳定 ASCII 编码与 tokenizerVersion。
2. 按标题/问答切块；保留原文位置、否定词、适用条件、来源；200—500 字目标、800 字上限。
3. 索引与查询使用同一预处理；参数绑定 tsquery；标题/术语/正文权重固定并可版本追踪。
4. 先过滤 manifest/scope/service/general/主题，再候选最多 20、最终最多 6；固定 tie-break 便于重跑。
5. 服务字段精确读，包含价格/项目/全部时间族；“多久”同时检索单次与全程；省略问题仅用当前服务及最近两组完整问答补全。
6. 区分 no_hit、明确 unknown、conflict 与故障；低信息词不能凑够 Top-K。
7. 接入发布时索引生成与 preview 临时索引；正常训练只查已发布快照。

**验收**：同版本重复查询结果稳定；另一服务价格永不进入；中文同义词与否定句覆盖；恶意 SQL 字符只作为查询数据。  
**门槛**：R13 固定集 Recall@6 ≥90%；此阶段提交初步召回报告及未命中原因，不以调高 K 隐藏问题。

### R06 — 会话上下文、快照与幂等

**改动**：011 迁移、training_context、两类 ReliableDatabase 创建/重启/列表/续练查询、API。

步骤：

1. 服务选择校验已发布、未归档、有效期、兼容场景与运行范围。
2. 短事务创建 session/context、固定 service revision、knowledgeAsOf 和完整知识 manifest；新旧路径分开。
3. 实现 clientSessionId 幂等及 active 部分索引；所有场景续练/最高分查询同步带 serviceId。
4. GET 会话只返回公开服务与公开画像，隐藏信息不进入 JSON。
5. 重启一次事务中 abandoned 旧会话、创建新上下文、入初始化队列；失败可重试新会话，不能两个 active。
6. 缓存键必须含 revision/manifest/purpose/tokenizerVersion，用户历史派生查询不得跨用户复用。
7. 新知识发布或服务修改只影响新会话；旧证据无法读取时报错，不偷偷换 current。

**验收**：相同幂等键并发只有一会话；同键异参 409；A/B 服务同场景可各自续练；新旧服务版本、知识新增、归档、报价到期用例全部通过。

### R07 — 证据校验与服务端渲染

**改动**：evidence_validator、GroundedResult DTO、引用读取 API、trace 写入接口。

步骤：

1. 实现 text/fact/unknown/knowledge 段 schema；fact 只可选择本包字段与 evidenceId。
2. 服务端模板渲染金额、起价、范围、单位、条件、有效期；时间区分单次/全程/咨询时间/非实时预约。
3. 自由文本禁止夹带无依据数字、中文金额、折扣/免费、日期和确定承诺；保留既有诊断与疗效边界。
4. 校验 evidenceId、revision、manifest、适用条件、冲突状态，语义不确定的知识解释降级原文摘述或未知。
5. learningPoints、complianceBoundary、复盘、点评、推荐话术都经同一入口，不只校验 reply。
6. 校验失败最多一次语义修复；仍失败用可验证事实模板；数据库故障不按未知模板成功返回。
7. trace 尝试可审计，但仅胜出消息/报告的公开引用可通过 evidence API 读取。

**验收**：“3980 元起/颗”不可变成总价；“5000 预算”不是报价；不同 trace 的 E1 不混用；其他学员及管理员越权读取引用均拒绝。

### R08 — 角色互换回答与复盘

**改动**：ModelGateway::standardServiceReply / roleplaySummary 的 v2 分支、Service::sendRoleplayMessage、saveStandardCustomerReply/saveSummary。

步骤：

1. v2 对话流程串接租约 → 固定上下文 → 检索 → 模型 → 校验 → 原子提交。
2. 模型调用前释放连接，提交校验 attempt/generation；答复、轮次、trace、结束与 summary 入队保持原子性。
3. reply 字符串兼容现有客户端，新增 citations 和 answerStatus。
4. summary 重用该会话证据；新增专业解释先检索并核验，不能复盘阶段补造服务事实。
5. 未知与部分可答分别说明；跨服务请求提示另开训练，不切换服务。
6. 按任务配置 token 上限，沿用底层请求和网络重试；旧模式调用契约不变。

**验收**：已知价格通过新校验、未知疗程明确说明、无实时号源不确认预约；断网重发不增加轮次；失效租约结果不能成为可见引用；角色互换无评分字段。

### R09 — 显式任务分发与 AI 患者初始化

**改动**：012 迁移、AiJob/队列/Service::processJob、training_context、PatientFactory 对应编排、新提示词。

步骤：

1. 将全部“evaluation 否则 roleplay”改成显式 evaluation/roleplay_summary/patient_initialization 分发；未知类型拒绝。**（已完成：`AiJobKind` + `parseAiJobKind`，覆盖 `lockAiJobTarget` / `enqueueAiJob` / `ensureAiJob` / `markTargetFailed` / worker 失败码；knowledge 队列另有 `GenerationTarget` + `parseGenerationTarget` 覆盖 create/retry/succeed/validateGeneratedDraft/模型提示词）**
2. 一并覆盖 lockAiJobTarget、去重键、claim、renew、fail、过期恢复、目标失败标记、stats、readiness 和终止流程。
3. 初始化事务保存会话/context/job；Worker 事务外生成画像和开场；提交锁序 session → context → job/result。
4. profile 包括稳定身份、需求、预算、顾虑、有限已知内容、披露计划；不创建独立患者表。
5. 输出检查虚构身份、服务一致性、有限患者认知；round=0 不占用户 10 轮配额。
6. 每轮复用画像，情绪/披露作为可变状态；续练不重新调用 PatientFactory。
7. 初始化失败只重试当前会话；pending/generating 禁止消息/结束/提示，前后端都校验。

**验收**：重复 job/租约过期/进程重启最多一个有效画像与开场；未知任务不触碰任何 session 表；现有报告并发与锁序回归通过。

### R10 — 学员服务选择与训练交互

**改动**：pages/index、training、roleplay、roleplay-result、home/mine 必要入口、utils/api.js、utils/request-policy.js、引用展示组件（如有）。

步骤：

1. 新 UI 先选服务再选场景/难度；空服务目录显示可操作空态，不随机使用服务。
2. 两模式创建与 restart 持有稳定 clientSessionId；服务维度区分续练、最佳分和链接参数。
3. 客服训练展示初始化等待、失败重试和公开画像；重复进入恢复原状态，ready 前输入禁用。
4. 显示本次服务版本、模拟资料标识；引用展开字段/原文/适用条件/来源，unknown 和 partial 有清晰呈现。
5. hide/unload 停止轮询；onShow 恢复；防止旧请求返回覆盖新会话；引用请求同样检查当前 session。
6. 开关关闭、服务归档或权限变化显示对应提示；旧会话仍可正常查看与续练其原逻辑。

**验收**：超时后重试、切页恢复、登录续期、重复点击、旧版无 serviceId、两模式来回切换均不串服务或会话。

### R11 — 陈述提取与知识评分

**改动**：knowledge_evaluator、claim-extract-v1/score-rag-v1 提示词、评分用证据 trace、离线核验 fixture。

步骤：

1. 提取学员原句、轮次、数值位置、否定/假设/转述/纠正类型，以及患者实际询问的知识点。
2. 后端验证原句真实子串与说话者；不存在的原句不进入判错或评分。
3. 对每个 topic/field 分别检索固定 manifest；结构事实确定比较，专业语义使用带证据判定再验证枚举/引用。
4. 去重评分单元；重复错误不重复扣分，保留每轮证据；后续自我纠正更新最终判定并保留过程。
5. 按 knowledge-rubric-v1 设置权重 2/1、单元值 1/0.5/0；不允许模型决定 scoreImpact。
6. evidence_missing/conflicted 不进分母；not_applicable 不进入可判定单元；coverage 明确定义为可判定单元数 /（可判定 + 不可判定单元数），零分母返回 null。
7. 无可判定单元时知识分/总分/passed 为 null；其余四维正常点评，不重分配权重。
8. 合规扣分沿原规则；若同一事件在两维体现，分别给出事实错误与违规承诺理由。

**验收**：转述别家价格不当本店报价；单位错误识别；主动纠正生效；冲突不选边；库中可答但始终回避可判 0。  
**确定性算例**：价格权重 2 得 0，普通时间权重 1 得 1，知识分 round(100/3)=33；全部 evidence_missing 时为 null。

### R12 — 报告写入、学习派生与 UI 闭环

**改动**：evaluation job v2、normalizeReport v2、saveEvaluation、结果/历史页、phrases/mistakes、报告派生逻辑。

步骤：

1. 评分提取 + 证据判定/报告生成按预算执行；生成失败不保存部分报告，重试仍用相同 manifest。
2. 后端计算五维总分，沿现有舍入；v2 合法 null 存 JSON null 与 SQL NULL，状态仍 ready。
3. 报告/trace/总分/队列完成原子提交，stale worker 不可覆盖新 generation。
4. 页面显示逐项原句、核验结论、依据、覆盖率、知识分为空原因。
5. phrases/mistakes 从已校验报告派生，携带 serviceRevisionId/knowledgeRefs；证据缺失不是错题。
6. 从旧错题复练采用当前服务版本并提示更新；原报告引用仍使用旧 revision。
7. 用 R01 混合版本统计测试再验证写入后的真实读取路径。

**验收**：完整跑通选服务 → 初始化 → 训练 → 报告 → 引用 → 错题复练；null 报告可读且不无限重评；角色互换仍只生成复盘。

### R13 — 固定评测、迁移和可靠性总验收

**改动**：backend/tests 新测试与 fixture、CMake/CI、docs/rag-validation-report.md（实施时新增）。

步骤：

1. 完成第 6 节离线测试集与指标输出；expected 由固定人工规则/标注定义，不用当前模型输出自证。
2. 空库 → 最新、历史库 → 最新、迁移重跑、失败回滚、混合 v1/v2、旧 active 会话并存全覆盖。
3. 执行初始化/发布/消息/末轮/报告竞态与 stale token 故障注入。
4. 运行现有全部 CTest、静态检查、迁移/并发/状态机和不带 WithModel 的 smoke。
5. 核对 Windows CI 实際触发：当前 push 仅 fix/**、integration/**，PR 目标 master；新增分支需 PR 或 workflow_dispatch 才触发，不把未运行当通过。
6. 记录 DB/检索/模型故障区分、权限隔离及前端恢复结果。
7. 保存每项命令、环境、退出码、失败修复、指标分母和剩余限制。

**验收**：无跳过核心数据库测试；已有功能无回归；第 6 节硬门槛通过才进入 R14。

### R14 — DeepSeek 联调、发布与回退

步骤：

1. 在离线门槛通过后，核对官方模型列表与账号可用 ID、JSON 输出；不未经验证更改原默认别名。
2. 先登记调用预算上限、任务上限和预期路径。用一次受控联调批次覆盖资料草稿、患者初始化、两模式消息、评分及复盘；故障修复后只重跑受影响路径。
3. 校验真实响应空/截断/usage 解析与任务级 max_tokens，实测模型超时、代理/客户端超时和租约是否匹配。
4. 准备约 6—10 个模拟服务、40—80 条知识的 demo 资料，记录生成/校验/发布；不当作真实报价或已核验医学资料。
5. 按 P0—P4 逐段打开开关；已有 v2 会话始终用固定版本。
6. 演练关闭新建 RAG、暂停已存在会话生成、继续读取报告、恢复后续练；不删除新表、不回旧不兼容二进制。
7. 完成 API 文档、配置说明、验收报告和交接记录，列出实际模型 ID/提示词版本/数据库迁移版本。

**验收**：端到端结果与固定事实一致；调用次数与费用可追踪；回退不触发 v2 null 修复、不丢画像/引用/历史版本。

## 6. 测试数据、矩阵与门槛

### 6.1 固定数据布局（待实施）

建议新增：

- backend/tests/fixtures/rag/services.json：A/B 不同价格，起价、范围价、未知、过期、非实时预约。
- backend/tests/fixtures/rag/knowledge.json：general/service、synthetic/reviewed、冲突、同义词、否定及适用条件。
- backend/tests/fixtures/rag/cases.jsonl：不少于 80 条，含期望 facts、gold passage IDs、引用、核验标签。
- backend/tests/fixtures/rag/reports.json：v1 正常/损坏，v2 全评分/部分覆盖/完全不足。
- backend/tests/rag_retrieval_test.cpp、evidence_validator_test.cpp、knowledge_evaluator_test.cpp。
- backend/tests/rag_database_test.cpp、rag_client_recovery_test.js。
- backend/tests/rag_eval_report.json 为运行产物，是否提交按仓库产物规则；正式结论写 docs/rag-validation-report.md。

单条 case 至少包含 caseId、purpose、serviceRevisionId、manifest、question/history、expectedFactKeys、relevantChunkIds、expectedStatus、expectedChecks、category。涉及排名指标的测试单独有人工标注 relevantChunkIds，未知问题不混入 Recall 分母。

### 6.2 80 条最低分配

| 类别 | 数量 | 主要断言 |
|---|---:|---|
| 价格/单位/范围/条件/包含关系 | 16 | 后端原值渲染，不能将每颗变成总价 |
| 时间/有效期/预约 | 10 | 单次与全程区分，未来日期报价不误承诺 |
| 中文术语/同义/省略/多主题 | 16 | 正确召回，低信息词不凑命中 |
| 未知/冲突/适用范围 | 12 | 未知不造事实，冲突不判错 |
| 跨服务/版本变化 | 8 | 同服务旧版固定，新知识不漂移 |
| 陈述否定/转述/纠正/重复 | 10 | 说话者、原句与评分单元正确 |
| 注入/错误引用/隐藏资料 | 8 | 引用与权限过滤，不执行正文指令 |

另设数据库与前端集成用例，不用上述 80 条代替并发验收。

### 6.3 必须通过的集成矩阵

| 场景 | 预期 |
|---|---|
| 同键并发创建/重启 | 一个结果；异参冲突；无双 active |
| 并发保存/发布与事务中途失败 | 不覆盖新草稿，不产生半发布版本 |
| 初始化任务过期 + 新 generation 完成 | 仅新结果可见，唯一 round=0 |
| 第 10 轮重发/失联 | 一对消息，单次结束与报告入队 |
| 模型成功但落库前租约失效 | stale 输出和引用不可见 |
| manifest 版本读取故障 | 可重试失败，不能用最新内容替代 |
| v2 null 报告多次读取 | ready 保持，不修复零分或新入队 |
| admin 猜 learner trace ID | 不因 admin 内容权限泄露学员对话 |
| verified 范围只有 synthetic 内容 | 不放宽到 demo，返回未知或不具备开练资料 |
| 页面退出/登录过期/请求迟到 | 停止轮询，重入恢复，不串会话 |
| 关闭 RAG/恢复 | 旧版可用，v2 可读，恢复后沿原版本续练 |

### 6.4 指标定义

- 固定字段准确率 100%：有明确可答字段的 case 中，值、单位、范围和条件均正确。
- 跨服务污染 0：A 会话的最终证据和输出无 B 专属事实。
- 引用存在与版本正确率 100%：每个公开引用属于本次 trace 与锁定 manifest。
- 明确未知 case 编造数 0。
- 专业知识 Recall@6 ≥90%：对存在人工标注相关块的查询，计算 top6 命中的相关块数 / 全部标注相关块数，再取查询宏平均；另记录 Hit@6，二者不可混用。
- 权限越权成功数 0；stale attempt 可见写入数 0。
- 本地检索 p95 <200ms，额外应用开销 p95 <500ms 为设计性能目标：报告数据量、机器、样本数、冷/热缓存条件，不把小样本结果当生产保证。
- 真实医学解释由人工抽查，语义质量与确定性字段测试分开记录；模型自评不替代验收。

## 7. 构建与验证命令

在 Windows 项目根目录运行现有受支持流程；PostgreSQL 安装路径按本机设置，CI 当前使用 PostgreSQL 14，不能依赖仅在本机较新版本才可用的语法。

~~~powershell
cmake -S backend -B backend\build-msvc -G 'Visual Studio 17 2022' -A x64
cmake --build backend\build-msvc --config Release
ctest --test-dir backend\build-msvc -C Release --output-on-failure
.\backend\tests\static_checks.ps1
git diff --check
~~~

其余现有脚本：migration_reliability.ps1、concurrency.ps1、session_concurrency.ps1、state_machine.ps1、smoke.ps1。执行前读取各脚本 param 与 CI 中的调用，使用所需参数；不得编造统一参数名。默认 smoke 不带 -WithModel。

数据库测试使用明确创建的可丢弃测试数据库；先确认连接目标再清理。database_feature 的退出码 77 表示跳过，不能写成数据库测试通过。记录 Passed / Failed / Skipped / Not run，无法运行 Windows 测试时如实交接，不用 Linux 语法检查冒充构建通过。

每个工作包跑受影响测试及必要回归；R13 跑完整门槛。文档任务只校验 Markdown、引用、路径与差异，不消耗真实模型调用。

## 8. 风险与实施防护

| 风险 | 对应任务 | 防护 |
|---|---|---|
| 队列新增类型落到错误目标表 | R09 | 全量显式分发，未知类型拒绝，锁序测试 |
| 报告 ready 被误解为总分必有值 | R01/R12 | schemaVersion 分流，逐层 nullable，读前于写 |
| RAG 输出被旧数字拦截清空 | R07/R08 | v2 专用证据校验，v1 保持原规则 |
| 仅锁首轮资料，后续版本漂移 | R06 | 全 manifest 一致快照，不只存命中块 |
| 一用户一场景索引阻止多服务 | R06 | 新旧部分索引及列表查询同步调整 |
| 草稿生成与人工编辑相互覆盖 | R03 | generation、draftVersion、候选结果隔离 |
| 评分把转述/预算视为报价 | R11 | 说话者与原句校验、陈述类型、服务证据独立 |
| 提示/学习要点绕过正文校验 | R07/R12 | 全输出表面校验；训练提示若含专业事实同样约束 |
| 多层重试导致费用失控 | R03/R08/R09/R11 | 唯一网络重试器，累计调用预算，最多一次语义修复 |
| 大 main.cpp 使模块无法独立测试 | R00 | 提取最小 DTO 和注入接口，不先整体重构 |
| 切换旧二进制误修复新报告 | R14 | 只回退开关，保留 v2 读取能力 |

## 9. 最终完成定义

所有项满足才称“RAG 功能完成”：

- 管理员可生成模拟草稿、编辑、预览、发布、归档、查版本。
- 学员新 UI 选择服务，两模式围绕同一锁定版本训练。
- 患者画像初始化可靠、续练稳定、隐藏信息不泄露。
- AI 客服准确表达价格/时间/条件，无资料明确未知，引用可查看。
- 核验评分可指出真实原句与证据；未知/冲突不误判；完全无依据时总分为空。
- 前端、后端、历史修复、成长统计、话术错题全部兼容 v1/v2/null。
- 数据迁移、并发、权限、离线指标、旧功能回归和受控模型联调有实际记录。
- 文档与实现接口一致；配置默认安全关闭，开启与回退操作可执行。

不能以“接口返回 200”“页面有引用按钮”“把文档拼进 prompt”代替上述验收。

## 10. 可直接交给编码模型的任务提示词

~~~text
请在 Oral-Training 仓库执行 RAG 开发任务 {Rxx}。

先读根目录及路径适用的 AGENTS.md、docs/rag-architecture-design.md、
docs/rag-development-plan.md 的第 1—4 节、对应任务与最近交接记录。
确认当前分支 SHA、最新迁移号和前置任务实际完成情况。

依据现有 C++/Crow/WinHTTP/PostgreSQL 与微信小程序实现，
只完成该工作包及必要兼容修复；不扩张架构，不跳过验收，不覆盖他人改动。
新旧会话按 contextVersion 分流，报告按 schemaVersion 分流。
模型调用在事务外，消息/引用/轮次/结束入队原子提交。
服务事实只能来自锁定证据；无资料与系统故障分别处理。

先检查代码，落实该任务的步骤、错误处理、测试与 API 文档。
离线测试用固定 fixture/FakeModelGateway；R14 前不运行真实模型验收。
缺少运行环境就记录未运行与原因，不能声称通过。
每个聚焦提交采用中文说明的 Conventional Commit。

完成后更新交接记录：
任务状态、提交 SHA、涉及文件、API/schema 变化、迁移与配置、
实际测试命令/结果、已知限制、下一可执行任务。
未完成项明确列出，不用空实现/固定成功响应掩盖。
~~~

## 11. 需求到任务追踪表

| 设计要求 | 实施任务 | 验收证据 |
|---|---|---|
| 服务及知识维护、新页面 | R02—R04 | 草稿/发布/归档完整操作 |
| 无资料时未知、具体价格时间准确 | R05、R07、R08 | 字段/时间/未知测试集 |
| 服务驱动随机患者 | R06、R09、R10 | 初始化并发、画像复用、0 轮开场 |
| 角色互换依据服务回答 | R08、R10 | reply/引用/复盘端到端 |
| 会话版本与患者信息保存 | R06、R09 | 修改资料后的旧会话与续练验证 |
| 知识核验与评分 | R11、R12 | 原句核验、计分、覆盖率/空分 |
| null 分及历史数据兼容 | R01、R12、R13 | 混合版本读取与聚合 |
| DeepSeek 单网关及调用预算 | R00、R03、R08、R09、R11、R14 | Fake 注入与受控 usage 记录 |
| 可靠性/权限/回退 | R06—R09、R13、R14 | 并发矩阵、越权测试、回退演练 |

## 12. 进度与模型交接记录

初始状态：R00—R14 全部待开发。本文件不改变原设计状态。

每个任务追加一条记录，不用覆盖旧记录掩盖失败：

~~~markdown
### Rxx — 任务名称
- 状态：待开发 / 进行中 / 阻塞 / 已验收
- 基线与提交：
- 前置任务证据：
- 实际修改文件：
- API / DTO / 数据库变化：
- 新配置及默认值：
- 验证：命令、环境、退出码、Passed/Failed/Skipped/Not run
- 测试覆盖与未覆盖风险：
- 与设计偏差及理由：
- 遗留事项：
- 下一任务与接手必读：
~~~

### R00 — 基线复核、契约冻结与可测试接口

- 状态：已验收（R00 不含数据库变更；现有数据库测试因未配置测试库记为 Skipped）
- 基线与提交：`codex/RAG@8f9d643`；工作区原有未跟踪目录 `tmp/` 保持不动；尚未提交
- 前置任务证据：无；设计基线 `3174405` 之后仅合入 RAG 设计/计划文档，生产 RAG 功能仍未实现
- 实际修改文件：`backend/src/rag_types.h`、`backend/src/model_gateway.h`、`backend/src/main.cpp`、`backend/tests/rag_contract_test.cpp`、`backend/CMakeLists.txt`、`docs/api.md`、本文
- API/schema 变化：仅在 `docs/api.md` 附录登记待实现 RAG v2 契约；冻结 `contextVersion=2`、`schemaVersion=2`、DTO 枚举、提示词版本、错误码和幂等摘要结构；未注册新路由、未写 v2 数据
- 迁移与配置：确认最新迁移为 `009_legacy_report_totals.sql`，后续从 `010` 开始；本任务无迁移、无新运行配置
- 注入点：新增 `IModelGateway`，`Service` 默认仍构造原 `ModelGateway`，测试可注入 fake；四类现有模型方法、请求参数、Prompt、解析和底层重试未改
- 实际测试（2026-09-14）：MSVC Release 构建通过；CTest 6 项中 5 Passed、`database_feature` 因未配置测试数据库 Skipped；新增 `rag_contract` Passed；`static_checks.ps1` Passed（19 个 JS、40 个 JSON）；`git diff --check` 通过；本机 `psql` 18.4
- 已知限制：R01 前仍不支持 v2/null 报告读取；~~`reliable_store.h` 的任务类型二选一分发须在 R09 改成显式枚举~~（**已改，2026-09-27**：`AiJobKind` / `GenerationTarget` 两张显式映射表，未知类型拒绝执行且在 DB 侧不再猜目标表）；CI PostgreSQL 14 兼容性需在迁移实现时验证
- 下一可执行任务：R01 报告读取、null 展示与聚合兼容；验收后才能生产任何 v2 报告

### R01 — 报告读取、null 展示与聚合兼容

- 状态：已验收
- 基线与提交：承接 R00 工作区；尚未提交
- 前置任务证据：R00 DTO、模型注入接口、契约测试及待实现 API 附录均已通过 MSVC/CTest
- 实际修改文件：`backend/src/main.cpp`、`backend/src/reliable_store.h`、`backend/tests/report_validation_test.cpp`、`backend/tests/database_feature_test.cpp`、`backend/tests/client_recovery_test.js`、`utils/api.js`、`pages/result`、`pages/report`、`pages/profile`、`pages/admin`、`pages/mine`、`docs/api.md`、本文
- API/schema 变化：报告读取按 `schemaVersion` 分流；聚合增加 `scoredSessions/unscoredSessions` 或 `scoredCount/unscoredCount`；无已评分样本时平均分、达标率及无样本维度返回 JSON null
- 兼容行为：缺省版本仍按 v1 修复；v2 `insufficient_evidence` 的合法 null 总分保持 ready、同步 SQL NULL，重复读取不入队；R01 不生产 v2 报告
- 统计行为：总分平均、达标率、最佳分、趋势仅使用非空总分；维度均值使用各维度自己的非空样本数；完成次数保留全部 ready 报告
- 页面行为：null 显示“暂无评分”或“知识依据不足”，不补算 v2 综合分，不画缺失维度的零分点，不从空分生成薄弱项
- 实际测试（2026-09-15）：MSVC Release 构建通过；CTest 5 Passed、0 Failed、默认环境下 `database_feature` Skipped；使用 `backend/.env` 中经测试程序安全校验为 test/ci 范围的数据库单独运行 `database_feature_test.exe` Passed；混合 80/null/60 均分 70、2 已评分/1 未评分、逐维非空均值及重复读取不入队均已覆盖；静态检查和 `git diff --check` 通过
- 已知限制：R12 前不会写入 v2 报告；微信开发者工具的视觉验收留在 R13，总分/达标率新字段需保持文档兼容
- 下一可执行任务：R02 目录与不可变版本存储

### R02 — 目录与不可变版本存储

- 状态：已验收
- 基线与提交：承接 R00—R01 工作区；尚未提交
- 前置任务证据：R00 的 DTO/模型注入契约与 R01 的 v2/null 读取链路均已通过 MSVC/CTest
- 实际修改文件：`backend/migrations/010_knowledge_catalog.sql`、`backend/src/knowledge_store.h`、`backend/src/knowledge_store.cpp`、`backend/tests/knowledge_store_test.cpp`、`backend/tests/knowledge_store_database_test.cpp`、`backend/tests/knowledge_catalog_migration.ps1`、`backend/tests/knowledge_store_database.ps1`、`backend/tests/migration_reliability.ps1`、`backend/CMakeLists.txt`、Windows PostgreSQL CI、本文
- API / DTO / 数据库变化：新增服务、服务草稿/不可变版本/场景关联、知识条目、知识草稿/不可变版本/检索块、管理生成任务、发布幂等请求和审计事件表；新增 `KnowledgeStore`，支持 admin 作用域下的创建、乐观保存、幂等发布、归档和版本读取
- 数据约束：金额使用整数分，价格范围、单位/条件、有效期、时长、预约与 unknown 均由服务端校验；synthetic 内容只能是 unverified，不能伪造来源或 reviewed；公开投影移除内部字段并由结构化价格字段渲染说明
- 事务与审计：发布对服务/草稿主行加锁，在同一事务中追加 revision、切换 current pointer、更新场景关联、登记幂等结果并写审计；发布失败整体回滚；已发布 revision 由数据库触发器禁止更新或删除
- 实际测试（2026-09-15）：MSVC Release 构建通过；CTest 7 项中 6 Passed、默认 `database_feature` Skipped；`knowledge_store` Passed；在名称含 test/ci 且每次创建一次性 schema 的 PostgreSQL 18 环境中，`knowledge_catalog_migration.ps1` 和 `knowledge_store_database.ps1` Passed；覆盖空库、历史库、迁移重跑、并发保存一个成功一个 409、learner 403、同键发布重放/异参冲突、外键失败不产生半版本、revision 不可变、归档保留历史与审计
- 已知限制：完整 `migration_reliability.ps1` 在本机 PostgreSQL 18 的既有 005 历史 fixture 上触发 `generation_state_repair_archive.source_row` 非空约束，失败点早于 010 且未修改已发布迁移；010 已由独立空库/历史/重跑脚本验证，CI PostgreSQL 14 仍作为最终兼容入口
- 下一可执行任务：R03 管理 API 与可靠模拟草稿生成；R05/R07 完成前 preview 按计划保持关闭，不能用无证据模型回答代替

### R03 — 管理 API 与可靠模拟草稿生成

- 状态：阶段 2 主体已验收；按本任务完成条件，可信 preview 仍须在 R05/R07 后补齐，R03 最终状态保持进行中
- 基线与提交：承接 R00—R02 工作区；尚未提交
- 前置任务证据：R02 的 010 迁移、不可变 revision、乐观草稿保存、幂等发布及一次性 schema 数据库测试均已通过
- 实际修改文件：`backend/src/main.cpp`、`backend/src/model_gateway.h`、`backend/src/knowledge_admin_queue.h/.cpp`、`backend/migrations/010_knowledge_catalog.sql`、`backend/tests/knowledge_store_database_test.cpp`、`backend/tests/knowledge_admin_api.ps1`、`backend/CMakeLists.txt`、`.github/workflows/windows-postgresql-ci.yml`、`backend/.env.example`、`backend/README.md`、`README.md`、`docs/api.md`、本文
- API 变化：实现设计 §10.2 的服务、知识、版本、发布、归档和生成任务端点；发布和生成创建接受 `Idempotency-Key`；preview 已注册但在 R05/R07 前明确返回 `409 PREVIEW_NOT_AVAILABLE`，没有用自由模型问答冒充预览
- 生成可靠性：使用独立 `knowledge_admin_jobs` 队列和独立 Worker，并记录 `base_draft_version/generation/attempt_token/prompt_version/model_version/result_applied`；支持 claim、租约续期、过期重领、自动重试、dead 与显式新 generation；人工保存会清除 draft 的 generation 归属，旧结果只保留为候选而不覆盖
- 模型边界：新增 `service-draft-v1` 与 `knowledge-draft-v1` 草稿方法，复用唯一 DeepSeek 网关和既有底层请求/解析重试器；只允许 schema 合法的 `synthetic/unverified/demo` 候选，虚假 source title/URL/locator、manual origin 或 reviewed/verified 均被服务端拒绝；模型不能直接发布
- 配置：新增 `KNOWLEDGE_WORKER_CONCURRENCY`，默认 1、限制 1—2；连接池下限改为两个 Worker 池并发之和加 2；健康检查增加知识 Worker、待处理/失败任务及数据库退避状态
- 实际测试（2026-09-15）：MSVC Release 构建 Passed；CTest 6 Passed、默认数据库测试 Skipped；一次性 schema 的 `knowledge_store_database.ps1` Passed，覆盖任务幂等、旧生成不覆盖人工编辑、失败可见、重试刷新最新输入、租约失效 attempt 不可写及新 attempt 重领；`knowledge_admin_api.ps1` Passed，覆盖 learner 403、服务 CRUD、旧草稿 409、发布重放、知识发布、生成 202/轮询及 preview 明确关闭
- 未覆盖风险：依照计划未运行真实模型；空响应、截断与非法候选由相同校验/重试路径覆盖但真实输出质量留到 R14 受控联调；R05/R07 前不能完成可信 preview
- 下一可执行任务：R05 中文切块、索引和确定性检索；R07 后回补 preview 并将 R03 标记为最终已验收

### R04 — 知识管理页面组

- 状态：阶段 2 主体已验收；可信 preview 与微信开发者工具视觉验收未完成，R04 最终状态保持进行中
- 基线与提交：承接 R00—R03 工作区；尚未提交
- 实际修改文件：`pages/knowledge-admin/*`、`pages/service-editor/*`、`pages/knowledge-editor/*`、`pages/admin/admin.js/.wxml/.wxss`、`pages/mine/mine.js/.wxml/.wxss`、`app.json`、`utils/api.js`、`backend/tests/client_recovery_test.js`、本文
- 页面行为：管理首页提供服务、知识和最近生成任务三分区；管理员可从“我的”或主管看板进入；普通用户无入口且后端仍逐接口 403；服务表单分开价格、包含/不含、单次时长、全程周期、复诊间隔和预约说明，所有未知值均显式填写原因
- 并发与恢复：保存携带 `draftVersion`；409 时保留当前输入并由管理员选择是否重新加载；生成任务显示排队/执行/等待重试/完成/失败，hide/unload 停止轮询，重入根据 draft 的 `generationId` 恢复；旧候选未应用时给出明确提示
- 发布与历史：发布前确认 synthetic 标识、价格单位/条件/有效期或知识适用范围；版本列表只读并展示 hash/正文摘要；归档说明不会删除历史；preview 按后端显式提示尚不可用
- 实际测试（2026-09-15）：`static_checks.ps1` Passed（22 个 JS、43 个 JSON）；`client_recovery_test.js` 覆盖管理 API 幂等请求头、unknown 不被转成零值、固定 synthetic/unverified/demo 元数据及页面退出停止轮询；`git diff --check` Passed；真实 HTTP 管理闭环由 `knowledge_admin_api.ps1` Passed
- 未覆盖风险：当前机器未安装微信开发者工具，未执行模拟器/真机视觉与交互验收；真实模型生成按钮未做受控联调；R05/R07 前 preview 只显示明确不可用信息
- 下一可执行任务：R05；完成 R07 后回补并验收 preview，再在 R13 使用微信开发者工具覆盖生成 → 编辑 → 预览 → 发布 → 版本对照 → 归档完整流程

首次开发从 R00 开始；不需要重新询问已在设计中确认的单诊所、模拟资料、DeepSeek、会话画像和知识核验边界。

### R05 — 确定性检索与中文索引（MVP 主体）

- 状态：MVP 主体已实现；正式 Recall@6 固定集评测留到 R13
- 实际修改文件：`backend/src/rag_retriever.h/.cpp`、`backend/src/knowledge_store.cpp`、`backend/tests/rag_retriever_test.cpp`、`backend/tests/knowledge_admin_api.ps1`、管理端知识预览页面、`docs/api.md`
- 已实现：Unicode/全角规整、中文双字片段、固定同义词、稳定 ASCII 词项、450 字切块与 60 字重叠；知识发布在同一事务写 `knowledge_chunks/search_vector`；查询先锁定 revision manifest，再按 scope/service/topic 过滤并稳定取前 6；价格、包含项目、单次/全程时长、复诊和预约走服务 revision 精确字段路径
- 预览：管理端可输入问题，对指定已保存草稿版本执行临时检索并展示命中原文，不调用自由模型、不污染正式索引
- 验证（2026-09-15）：MSVC Release 构建 Passed；`rag_retriever_test` Passed；知识数据库测试 Passed，发布路径成功写检索块；PostgreSQL 18 空库 001—011 迁移 Passed；CTest 7 Passed、1 数据库特性测试因默认无 URL Skipped；`git diff --check` Passed
- 未覆盖：尚未准备 80 条人工标注召回集，未宣称 Recall@6 ≥90%；未运行真实模型

### R06—R08、R10 — 角色互换最小 RAG 闭环

- 状态：最小 MVP 已实现；各任务的完整验收项仍在后续阶段继续
- 数据库/API：新增 `011_roleplay_rag_mvp.sql`，角色互换会话保存 service/revision/clientSessionId/contextVersion；`training_contexts` 固定知识 manifest；`rag_traces` 与消息 citations/answerStatus 原子公开；新增 `GET /services` 和本人 evidence 读取
- 对话：v2 会话每轮使用锁定 service revision 和 manifest 检索；DeepSeek 只选择 evidenceId，价格、时长、项目范围和未知字段由服务端渲染；旧无 serviceId 会话继续 v1 契约
- 页面：患者模拟先选已发布服务，仅展示兼容场景；标准客服消息可展开本轮依据；服务摘要保留版本与正确的价格单位/起价条件
- 最小冒烟（2026-09-15）：隔离 PostgreSQL 18 + 本地后端验证服务发布后学员可选、`3980 元起/颗` 未丢失限定词、创建会话返回 `contextVersion=2` 并锁定正确 service revision、同 `clientSessionId` 重放返回原会话
- 未覆盖：本机未配置 DeepSeek Key，未做真实模型消息；角色互换 summary 尚未复用 evidence；客服训练的 AI 患者初始化、评分 v2 和两模式完整上线仍分别属于 R09、R11—R12；微信开发者工具视觉验收未运行
- 下一步：用受控 DeepSeek Key 做 1 次角色互换消息联调；随后按最短产品路径实现 R09 患者初始化，再补 R11—R12 知识核验评分
