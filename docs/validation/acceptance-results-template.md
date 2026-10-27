# 验收结果记录模板

本文件为空白记录模板；所有人工结果初始为 **pending**。复制为单批记录时保留原始输入与预期，修订预期需另记版本和理由，不覆盖失败结果。

## 批次信息

- 批次ID / 日期 / 操作者：pending
- Git提交或工作区快照 / fixture SHA256：pending
- 环境 / 数据库精确范围 / 开发者工具版本：pending
- 测试账号代号与角色（不记token、密钥）：pending
- 模型保存配置 / 实际返回模型 / 输出限制 / thinking / 超时：pending
- 服务ID与revision / 知识manifest / 场景与难度：pending
- 本批HTTP上限 / 实际次数 / 重试次数：pending
- 基线计数差异与2个dead job状态：pending

## 逐卡结果

| 卡ID | 状态 | 会话ID/轮次 | 实际操作与输入分支 | 实际结果及证据路径 | 预期差异/问题ID |
|---|---|---|---|---|---|
| 0A | pending | — | — | — | — |
| 0B | pending | — | — | — | — |
| 0C | pending | — | — | — | — |

后续按操作卡ID新增行。批次2拆2a/2b，批次3拆3a/3b/3c（标准档/进阶档分行），批次6拆6a/6b，每次仅2–3张。状态仅用pending/pass/fail/blocked；未执行不得写pass。截图脱敏并标明对应轮次，正文只保存本次合成测试内容。

## 原子检查

- 功能状态、轮次、幂等、恢复：pending
- 事实/条件/引用支持：pending
- 四维沟通评价与实际行为相符：pending
- 总结事实、角色归因、纠错状态、未解决事项：pending
- 权限范围：pending
- 旧会话revision/manifest及证据保持：pending
- UI结果与后台只读证据对应：pending

每个失败项附：预期、实际、最短复现、requestId（如有）、原始证据、严重性、复测关联。修复后新增复测结果，不删除原失败。

## 批次4差异化回放记录

当前suiteId为oral-scoring-acceptance-v3；v1/v2产物保留，不与v3混算。回放保存results.json及运行manifest；期望键采用`caseId + repetition + mode`，缺失运行明确标未完成。记录runMode与limits；offline_only不进入真实模型成功分母。原始和校验后输出都保留，以区分模型错误与后端拦截。

| 指标 | 分子/分母 | 结果 | 说明 |
|---|---|---|---|
| 知识断言与先验标签一致 | pending | pending | 同轮field/topic与原句/抽取子句匹配；正向断言漏输出算错；not_applicable正确排除可不生成核验项 |
| 知识错误漏检 | pending | pending | 错误被接受或未报告 |
| 正确/否定/转述/预算误扣 | pending | pending | 按对应样本逐条说明 |
| 知识成对方向一致 knowledgePairConsistency | pending | pending | 仅knowledgeAccuracy，不混入沟通维度 |
| 四维成对方向一致 pairConsistency | pending | pending | 小样本对照一致性，保留每次分数，不宣称泛化准确率 |
| 无证据/冲突无法评定 | pending | pending | 真实报告totalScore=null，不按0分比较；offline无report标pending不计展示错误 |
| 总结事实正确 | pending | pending | 人工原子断言审核后计算 |
| 发言者归因正确 | pending | pending | 区分患者、客服、他人转述；not_applicable自动排除不替代人工归因审核 |
| 纠错状态正确 | pending | pending | 首次错误不能被纠正记录抹除 |
| 未解决事项准确 | pending | pending | 未预约/未决定等状态 |

review-template.json中的人工判断保持pending直到审阅，使用评估器支持的枚举填写，不自行臆造字段。每个判断附输出原句和理由；关键词匹配不替代语义审核。角色互换仅审核复盘，无分数指标。

## 批次进度（固定顺序）

| 批次 | 内容 | 状态 |
|---|---|---|
| 0 | 环境 | pending |
| 1 | 正常知识整理，异常保持原状 | pending |
| 2 | 患者模拟 | pending |
| 3 | 客服训练 | pending |
| 4 | 差异化评分与总结 | pending |
| 5 | 恢复隔离 | pending |
| 6 | 学习管理 | pending |
| 7 | 边界专项，之后异常归档 | pending |
| 8 | 性能收尾 | pending |

## 批次8性能记录

固定路径：主管首页→待辅导成员→成员学习详情member-detail。主管代号 / 成员ID / 同一数据快照 / 开发者工具版本 / 模拟机型 / 网络 / 后端状态：pending。冷进入定义：关闭运行实例再打开，经首页点同成员，缓存条件另记；返回重进定义：从该成员详情返回首页再点同成员。不得换测报告页，不触发模型。

| 样本 | 缓存状态 | 请求起止/耗时ms | 点击成员到主体可交互ms | 点击成员到雷达完成ms | 点击成员到成长图完成ms | >50ms主线程任务数/最长ms | 轨迹/错误 |
|---|---|---|---|---|---|---|---|
| cold-1 | pending | pending | pending | pending | pending | pending  pending |
| cold-2 | pending | pending | pending | pending | pending | pending  pending |
| cold-3 | pending | pending | pending | pending | pending | pending  pending |
| return-1 | pending | pending | pending | pending | pending | pending  pending |
| return-2 | pending | pending | pending | pending | pending | pending  pending |
| return-3 | pending | pending | pending | pending | pending | pending  pending |
| return-4 | pending | pending | pending | pending | pending | pending  pending |
| return-5 | pending | pending | pending | pending | pending | pending  pending |
| return-6 | pending | pending | pending | pending | pending | pending  pending |
| return-7 | pending | pending | pending | pending | pending | pending  pending |
| return-8 | pending | pending | pending | pending | pending | pending  pending |
| return-9 | pending | pending | pending | pending | pending | pending  pending |
| return-10 | pending | pending | pending | pending | pending | pending  pending |


冷/返回样本分别汇总中位数和最大值；请求、主体、雷达图、成长图各自统计，不相加。缺少某图表标不适用并说明数据条件；异常样本保留，错误/超时单列，不拿空值作0。>50ms任务附调用位置和轨迹截图。未批准阈值的项目只记录观察与慢点，不宣称性能达标；真机性能未验收。

## 数据整理审计

| 对象ID | 原状态/版本 | 操作及理由 | 新状态/版本 | 历史引用核验 | 结果 |
|---|---|---|---|---|---|
| pending | pending | pending | pending | pending | pending |

不记录凭据或无关个人训练正文。不删除旧版本、会话、任务。归档后重新发布会创建新版本，记录为发布操作，不称“原样撤销”。

## 本批结论

- 已通过 / 失败 / 阻断 / 未执行数量：pending
- P0阻断项及是否可进入下一批：pending
- 模型调用实际费用/用量（可获取时）：pending
- 未覆盖：真机、真实微信登录、设备网络；组件回放不覆盖完整检索/worker落库/API/UI。
- 下一批2–3张卡：pending

依据：[完整流程](project-acceptance-workflow.md)、[输入与操作卡](acceptance-cards.md)、[冻结fixture](../../backend/tests/fixtures/scoring_acceptance.json)。
