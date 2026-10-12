# N07 分段开关、受控联调与发布记录

日期：2026-09-22。基于 N06，工作分支 `fix/rag-n07`。当前状态：代码实现与无模型自动回归已通过；真实模型联调和正式发布尚未完成。

## 已准备的代码

- 新建开关默认全关；客服训练依赖患者与 v2 评分两个开关，角色互换独立。配置变更需重启，不是热开关。
- 关闭只限制新 v2 会话及角色互换重新开始；相同创建标识重放、旧会话消息、初始化恢复、评分、复盘、画像、null 报告与旧证据保持原快照。
- MODEL_CALL_LIMIT 对单进程全部 HTTP 尝试施加并发安全上限，包括 JSON 修复重试和 Worker 重试；0 不限，正数用作独占受控批次上限。耗尽后不自动重试。此上限不是跨重启的财务预算。
- 审计记录 actualModel、提示词版本、usage、超时/失败代码、耗时、尝试序号；不记录正文或密钥。HTTP/JSON 成功不代表领域验证成功，需同时查看 Worker 失败日志。
- `backend/tests/rag_controlled_smoke.ps1` 只在显式手动调用时执行。CI 仅解析其 PowerShell 语法。脚本要求已复核输入、两个本地 token、全新的独占后端、匹配的调用上限、空任务队列及新输出路径；依次覆盖两种草稿、患者初始化、两模式消息、评分与复盘。失败即停止，不显式重试、不自动发布草稿。

## 拟议单批次（尚未执行）

1. 在 Windows 测试机启动与本分支一致的后端和一次性演示数据库；配置 DeepSeek Key，只供该进程读取，不通过聊天或仓库提交。
2. 使用人工复核的 synthetic/demo 服务与知识。另准备两个可被生成任务更新的测试草稿 ID，生成结果不发布；禁止拿真实门店报价冒充合成资料。
3. 拟议上限：16 次 HTTP 尝试。通常为 7 次逻辑调用：2 次草稿、初始化、患者回复、陈述提取、沟通评分、标准客服回复；每次可有既有修复重试，其他 Worker 重试也消耗同一上限。v2 复盘不调用模型。输出 token 理论上限为 16 × 8192；输入 token 和最终费用另计，真实费用预算尚未指定。
4. 将已确认上限写入后端 MODEL_CALL_LIMIT，开启三个新建开关，保存 stderr；设置脚本进程的 RAG_SMOKE_ADMIN_TOKEN、RAG_SMOKE_LEARNER_TOKEN。确认没有其他访问方或后台存量任务消耗预算。
5. 命令模板（变量先在本机设置，令牌不要写入命令参数）：

```powershell
.\backend\tests\rag_controlled_smoke.ps1 `
  -ServiceId $reviewedServiceId -ScenarioId $scenarioId `
  -ServiceDraftId $testServiceDraftId -KnowledgeDraftId $testKnowledgeDraftId `
  -LearnerAnswer $reviewedAnswer -MaxHttpCalls 16 `
  -OutputPath 'C:\rag-evidence\n07-batch.json' -ReviewedInputs
```

输出目录需预先存在。脚本会创建训练会话并更新指定测试草稿；失败时已创建的任务仍可能完成，不可直接再次执行或重启清零。先收集 batch 文件、后端 model_call 记录和 Worker 日志，核对总次数、actualModel、usage、错误及调用上限，再决定是否另批次。

## 回退/恢复演练清单

| 场景 | 操作 | 应核验 |
|---|---|---|
| 暂停新建 | 将三个开关关闭并重启 | 新客户端标识得到 503；旧标识重放原 ID；角色互换重开失败且原会话未被放弃 |
| 继续旧训练 | 在关闭状态续发消息、初始化重试、完成并读取报告 | 仍为 v2，固定 manifest，私有画像不泄漏，null 不变 0，公开引用仍属于原报告 |
| Worker 故障 | 仅在隔离测试库中终止后端，等待租约过期再恢复 | 旧 attempt 不可提交；任务被重领或明确失败；不重复消息/报告/trace |
| 恢复新建 | 恢复开关并重启 | 新会话使用当前 revision；旧会话保持旧 revision |

这些操作不能在真实联调批次中随意重启来重复消耗上限；故障演练用独立的无模型/假网关测试，再记录真实环境验证结果。

## 验证与剩余条件

本地 `git diff --check`、客户端恢复、患者交互、知识报告组件检查均退出 0；页面数据检查 53 项通过。

最终 [Windows/PostgreSQL CI 35753541244](https://github.com/chinesemisaka/Oral-Training/actions/runs/35753541244) 全部成功，验证代码 `9d0b0585af4d28501a3af0e2eb4b512aa619f8e7`。MSVC Release、CTest、静态检查、空库/历史库/重跑/回滚迁移、知识管理 API、初始化及报告数据库测试、无模型 API、状态机和每模式 20 请求并发全部退出 0。CTest 13 项中 11 Passed、2 项数据库测试因未设置 URL 按设计 Skipped；后续独立数据库步骤执行通过。受控联调脚本在 PowerShell 7 与 Windows PowerShell 5.1 均通过语法解析，未执行实际批次。

首轮 CI 35752700289 曾因 Windows PowerShell 5.1 对无 BOM 中文脚本的解析失败而失败；已为脚本添加 UTF-8 BOM 并补充解析诊断，最终 CI 验证修复。未把失败轮次算成通过。

自动测试新增：默认开关与非法配置、16 线程竞争 5 个调用额度、超时审计序列化、暂停后的创建/重开拒绝、幂等重放、原快照/null 报告/旧消息保留。原迁移、并发、租约、报告、权限回归全部通过。CI 日志确认 N07 新建/重开阻断、重放/快照/null 历史/旧消息保留，恢复后新会话仍为 v2；N06 Recall@6 20/20，越权成功 0，双提交胜出 1/2，stale 可见写入 0。

真实模型批次：Skipped，调用次数 0、usage/真实模型 ID/实际失败率均未测量。当前运行环境无 DeepSeek Key、PowerShell 和 Windows 后端；未提供受控批次费用预算、已复核演示资料 ID，N06 人工案例复核与 DevTools 验收仍未完成。

《rag-development-plan.md》的 N07 前置要求为 N06 全部门槛通过；本次按用户“进入 n07”推进准备工作，不把尚未完成的人工门槛改写为已通过。待上述条件具备后才运行单次受控批次并补录结果，不认定 N07 全部验收或正式发布完成。
