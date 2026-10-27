# 2026年10月27日测试基线与首轮结果

测试流程、操作卡、知识整理表和固定回放工具已准备。新增离线验收发现6条断言与冻结预期不一致，真实模型回放被门槛拦截；当前不能判定评分、总结或页面性能验收通过。现有数据库未作发布、归档、清理或任务重试。

## 四项交付

| 材料 | 内容 |
|---|---|
| [完整流程与覆盖矩阵](project-acceptance-workflow.md) | 按批准的批次0–8执行，区分自动检查、开发者工具手测、模型回放 |
| [知识整理与归档录入表](knowledge-consolidation.md) | 三组合并候选、真实表单字段、完整正文、前后检索问题、11服务及11异常知识清单 |
| [分批操作卡与差异化输入](acceptance-cards.md) | 30张主流程卡，按每次2–3张分发；31份固定脚本，覆盖D1–D8和稳健性变体 |
| 本次结果及[反馈台账模板](acceptance-results-template.md) | 自动结果、失败样本、指标分母、人工及真机未覆盖项 |

## 环境与数据基线

- 代码HEAD：`579c81aa73fbc59a31d7bab0140ca9e5255a6079`，存在未提交更改。本次回放manifest逐文件记录后端源代码、迁移、工具及二进制哈希，不能仅用HEAD代表实际执行代码。
- 精确数据库：本机`oral_training`，`PRODUCTION=false`、`AUTH_MODE=demo`；37个编号迁移。
- 11服务、61知识、6学员、1主管。正常已发布知识50条；完全相同标题正文的活动重复组0。三组合并全部验证后才可能50→47，未执行前计数保持不变。
- 主测试学员A为`demo-user-001`：个人模型`MiniMax-M3.1-Flash`、revision 2、已保存密钥；未读取或输出密钥。其余5学员和主管没有个人模型配置，不应假设能共享A配置。
- 学员B选`local-test-v1-learner-1`，先做历史/权限隔离。主管`local-test-v1-admin`可做已有授权管理操作，涉及生成的卡在其个人模型就绪后执行。Embedding沿用现有配置，本轮没有替换。
- 两条dead任务均为`patient_initialization`，`last_error=MODEL_NOT_CONFIGURED`，attempts均为1；generation分别为6和2。保留任务与关联会话；该错误说明当次调用没有可用个人模型配置，尚未通过重试验证恢复。

## 备份和恢复

工具：[prepare_acceptance_baseline.py](../../backend/tests/prepare_acceptance_baseline.py)。仅接受明确的本地非生产演示库，备份目录必须在仓库外，源查询使用只读事务。

本机备份目录：`/home/zehan/oral-training-backups/acceptance-20261027-implementation`。`database.dump`已恢复到独立临时PostgreSQL集群，40张public表的行数和完整行多重集摘要逐表一致；备份前后及恢复验证后的源摘要一致。源写入次数0。目录权限0700，文件0600。

备份SHA256：`5b48662c4dee29476c359ab889d30a2cec9b7d9be315469ae9f10e3179e9193a`。恢复校验针对数据表行内容，未将此结果扩大为生产基础设施灾备验收。

```bash
python3 -B backend/tests/prepare_acceptance_baseline.py \
  --database oral_training \
  --pg-bin /usr/lib/postgresql/16/bin \
  --output /home/zehan/oral-training-backups/acceptance-NEW-RUN
```

每次使用新目录；工具拒绝覆盖既有备份。此备份/恢复验证器运行于本地Linux后端，用户手动验收仍在微信开发者工具。

## 自动检查结果

| 检查 | 通过 | 失败 | 跳过或未执行 | 证据及范围 |
|---|---:|---:|---:|---|
| 全量CTest | 23 | 0 | 5 skipped | 28项；静态、页面渲染、报告数学规则、引用等原有检查，加回放执行及指标评估器 |
| 指标评估器单测 | 13 | 0 | 0 | 覆盖漏项、错误分母、非本方断言、缺轮、重复键、3次完整性、分开知识/沟通对照、人工审核及哈希绑定；更新后单独复跑CTest通过 |
| 自建临时DB集成工具 | 1套 | 0 | 0 | 两模式、20并发创建/模式、身份隔离、幂等、租约、重试、历史恢复、迁移/历史修复/重跑、知识存储、检索冲突和Embedding缓存等 |
| 固定回放执行 | 47 | 0 | 47条均为offline_only | 无模型评分，不把执行成功当语义通过 |
| 冻结知识断言验收 | 17 | 6 | 0 | 共23条预标注断言；非通用准确率 |
| 真实模型回放 | 0 | 0 | 全部未执行 | 完整fixture离线验收不通过时，即使只选择一个角色互换样本也会阻止调用 |
| 主流程人工卡 | 0 | 0 | 30 pending | 等用户按批次反馈；服务/知识实际发布归档未执行 |
| 成员详情性能 | 0 | 0 | 3冷进入+10返回重进待测 | 同成员、同数据、同开发者工具；请求/主体/雷达/成长图分别计时 |
| Windows MSVC及真机 | 0 | 0 | 未执行 | 本次编译/自动运行环境为Linux，不冒充Windows或真机结果 |

CTest跳过的是`embedding_rag`、`database_feature`、`patient_initialization`、`audit_regression`、`scenario_draft`，原因是没有直接传测试数据库环境。随后独立集成工具在它自建的临时集群中执行了这些数据库分支，返回成功；保留两阶段原始结果，不改写第一次Skipped。

已有`knowledge_evaluator_test`覆盖重复错误只计一次、纠正后原证据保留、缺证据总分/通过状态null、沟通参考分和固定权重；`report_validation_test`覆盖合规扣分上限及不一致分数拒绝。这些规则检查通过不代表新增自然语言样本已通过。

## 差异化评分首轮结果

冻结输入为[scoring_acceptance.json](../../backend/tests/fixtures/scoring_acceptance.json)，suiteId=`oral-scoring-acceptance-v3`。31例、47次回放，10组方向对照中6组为知识、4组为沟通维度；沟通对照每变体运行3次。SHA256：`e61464d6161af3c38d0e999a0cd7387593c3f844dc48087e40162f92e6a303fa`。v1/v2原始产物保留，结果不混算。

| 指标 | 分子/分母 | 当前结果 |
|---|---|---|
| 知识标签一致 | 17/23 | 73.9%，仅本冻结集的确定性离线核验 |
| 错误漏检 | 1/12 | 8.3%；错误包含植骨被标为无法判定，未识别为矛盾 |
| 正确/非本方报价误扣 | 4/7 | 57.1%；当前已标注知识表达范围，不包含尚未生成的D3医疗合规评分 |
| 四维沟通对照一致性 | 0/0 | 未执行，不能写0%或100% |
| 知识得分对照一致性 | 0/0 | 尚无模型报告，单列于沟通指标之外 |
| 总结事实准确性及关键事件覆盖率 | 0/0 | 无真实总结及人工审核，均pending |
| 无证据/冲突展示 | 0/0；2例pending | 离线没有report；需真实报告检查totalScore/passed=null及沟通参考分展示 |
| 发言者归因、纠正状态、虚构已解决 | 未审核 | 不把未执行计成“0次错误”验收通过 |

同轮topic/field与原句或唯一包含的抽取子句匹配；正向事实漏掉算错。转述/预算的`not_applicable`允许正确过滤、不生成事实核验项，但报告归因仍需人工审核。该匹配规则只对齐证据位置，不由当前判定反推标签。

### 六条失败样本

| 用例 | 冻结预期 | 实际判定 | 已核验的原因/影响 |
|---|---|---|---|
| D1-correct | supported | incomplete | 报价后半句有另计条件；分号切句及条件全文匹配使完整表达被误扣 |
| D2-complete | supported | incomplete | 同一完整条件表达复现，不能与真正删除检查条件的变体混为一类 |
| D5-corrected 第2轮 | supported | incomplete | 已明确更正为8900，后续总结是否遗漏纠正仍待模型/人工验证 |
| D1-included-correct | supported | evidence_missing | 当前包含项解析不能认证带“基础档组合”“及”的完整正确列表 |
| D1-included-wrong | contradicted | evidence_missing | 增加“不包含的植骨”未被识别为具体包含项错误 |
| D1-duration-correct | supported | contradicted | 时间范围解析未支持全角“～”，将3～8个月读成单一端点 |

涉及生产知识核验代码`claimSpans`/`compareKnowledgeFact`。本轮保持失败证据，没有通过改输入、改gold、改评分提示词或删除样本取得通过。后续应先修复并重跑完整冻结集，再进入真实模型评分和总结验收。

## 工具与原始证据

```bash
# 离线：不调用模型、不连接已有数据库；有gold失败退出1且保留全部结果
python3 -B backend/tests/run_scoring_replay.py --run-dir /tmp/acceptance-NEW-RUN
python3 -B backend/tests/evaluate_scoring_acceptance.py /tmp/acceptance-NEW-RUN

# 离线门槛通过后，由协作者分批运行。HTTP预算包含已有生产重试。
python3 -B backend/tests/run_scoring_replay.py \
  --run-dir /tmp/acceptance-LIVE-NEW-RUN --live \
  --groups D1,D3 --max-calls 32 --pass-entry api/minimax

# 将review-template.json复制为review.json，人审完成后重新评估，原结果不覆盖
python3 -B backend/tests/evaluate_scoring_acceptance.py /tmp/acceptance-LIVE-NEW-RUN \
  --review /tmp/acceptance-LIVE-NEW-RUN/review.json \
  --output /tmp/acceptance-LIVE-NEW-RUN/metrics-reviewed.json --require-review
```

Windows运行回放时显式提供`--binary backend/build-msvc/Release/scoring_replay_probe.exe`和本机`--pg-bin`。真实回放自建临时集群及UUID数据库，不继承现有PG连接信息；必须显式选择用例和HTTP预算（最多96次），输入、输出目录不可覆盖。

本次全部原始结果已私有保留于`/home/zehan/oral-training-backups/acceptance-results-20261027-v3`，包含`input.json`、`manifest.json`、`offline-input.json`、`offline.json`、`offline-metrics.json`、`results.json`、`metrics.json`、`review-template.json`及`ctest.log`。数据库集成产物目录为`/tmp/litellm-validation-g2nvj4ry`。

回放调用生产知识核验、评分归一化和总结函数；角色互换只使用明确标注、已由标准客服说出的公开trace，再经生产`groundedSummary`过滤。模型失败保留生产确定性兜底，同时标为error。组件回放不覆盖真实检索排名、数据库trace加载、worker落库、HTTP交互、随机患者、患者状态权重或可选答案比较；这些范围由现有集成工具及人工卡分别验证。

结果中的model为配置/请求的模型名；现有gateway未透传上游响应的model字段，实际返回模型身份保持未核验。本次HTTP模型调用为0。

人工总结必须逐句拆出全部事实性断言，标注出处轮次，再核对预先冻结的必述/禁述事件。争议保留pending；无人工审核不能报告总结准确率。知识与权限等为硬门槛，沟通逆序/持平单列`needs_review`，不以综合分掩盖。

## 现在执行的三张卡

按[首批0A–0C](acceptance-cards.md)完成身份、模型状态和四类入口核对；暂不新建训练。回传：`用例编号｜实际名称或截图｜异常现象`。收到反馈后核验账号、版本和状态，再交付下一组2–3张卡。生成操作、归档操作及13次性能样本均保持待验收。
