# N06 验收记录

日期：2026-09-22。分支：`chinesemisaka/Oral-Training:fix/rag-n06`。基线：N05 `d3dd233aaac80b95d131be9730a9e43b265af8c7`。

## 实现范围

- evaluation Worker 按 contextVersion 分流：v1 原路径；v2 固定 manifest 核验知识、单独评价沟通四维。模型知识分与总分不能覆盖服务器结果。
- 胜出事务重新回放知识核验，分配真实 trace ID，重写所有引用，原子写入公开 trace、报告、SQL 总分、session 评分状态、job/attempt 完成状态。异常整体回滚。
- v2 null 报告保持 ready；历史、聚合和雷达不将 null 转成 0。
- 结果页、历史摘要与历史详情共享知识核验组件，显示原句、结论、证据、表达参考、覆盖率和空分原因；证据端点验证用户、会话、公开目的与当前报告引用成员资格。
- 错题仅收录仍生效的已核验矛盾；未知、资料冲突、已纠正项不入错题。复练读取提交时当前版本，返回版本变化，原报告及其证据不变。

## 环境与命令

本地 Linux / GCC C++17 / OpenSSL / Node；Windows 2022 GitHub Actions / Visual Studio 2022 x64 Release / PostgreSQL 14 / Node 20。全部数据库写测试仅使用 CI 一次性数据库或明确前缀隔离 schema。

| 命令或验证 | 退出码 | 状态 | 结果 |
|---|---:|---|---|
| `g++ -std=c++17 -I../deps backend/tests/knowledge_evaluator_test.cpp -lcrypto -o /tmp/knowledge_n06`，运行该程序 | 0 | Passed | 82/82 固定案例；59 项回归断言 |
| `node backend/tests/client_recovery_test.js` | 0 | Passed | 恢复与轮询 |
| `node backend/tests/patient_client_test.js` | 0 | Passed | 初始化、稳定请求标识、隔离与旧会话 |
| `node backend/tests/knowledge_report_client_test.js` | 0 | Passed | 覆盖率/未知结论/会话限定证据读取 |
| `node backend/tests/page_render_check.js` | 0 | Passed | 53 项页面数据检查；不是 DevTools 视觉验收 |
| Node `--check` 与 JSON parse | 0 | Passed | 54 JS / 38 JSON |
| `git diff --check` | 0 | Passed | 无空白错误 |
| `cmake -S backend -B backend\build-msvc -G "Visual Studio 17 2022" -A x64`，`cmake --build backend\build-msvc --config Release` | 0 | Passed | 最终 CI 编译成功 |
| `ctest --test-dir backend\build-msvc -C Release --output-on-failure` | 0 | Passed / Skipped | 13 项中 11 Passed、2 因未设置 DB URL 跳过；后续专门 DB 步骤执行并通过这两项 |
| `static_checks.ps1` | 0 | Passed | 包含新增报告组件测试 |
| `migration_reliability.ps1`、`knowledge_catalog_migration.ps1` | 0 | Passed | 空库、历史库、重跑、失败回滚 |
| `knowledge_store_database.ps1`、`knowledge_admin_api.ps1` | 0 | Passed | 知识存储/管理 API |
| `patient_initialization.ps1` | 0 | Passed | 初始化、末轮、租约、报告、引用、版本固定、复练；真实 PostgreSQL 检索 |
| `database_feature_test.exe` | 0 | Passed | 含 v1/v2/null 混合读取与聚合 |
| `smoke.ps1`（不带 WithModel）、`state_machine.ps1`、`session_concurrency.ps1` | 0 | Passed | 无真实模型；两种模式各 20 并发请求 |
| 微信开发者工具/真机交互验收 | — | Skipped | 环境无 DevTools；不能用 Node 测试替代 |
| 真实 DeepSeek 联调 | — | Skipped | 属于 N07，未发起 |

首轮完整 CI：[35700995862](https://github.com/chinesemisaka/Oral-Training/actions/runs/35700995862)，代码 `e44b87657494e5e2ce3676f4aceb66d7d24c63ad`。最终完整 CI：[35701976582](https://github.com/chinesemisaka/Oral-Training/actions/runs/35701976582)，验证代码提交 `d2a3bfed82f4b5f447931be93fe2fa387abbe2cc`，全部步骤 success。该提交包括正式 Worker 入口、并发竞争、复练空判定及历史详情空分修正。最终日志记录 Recall@6 20/20、拒绝证据读取 7 次、越权成功 0、并发胜出 1/2、stale report 可见写入 0。后续文档提交使用 `[skip ci]`，不改动已验证代码。

## 指标口径

固定集位于 `backend/tests/fixtures/knowledge_cases.h`，82 条预期由固定规则独立编写，不读取或复制模型输出。覆盖起价、值/单位/范围、疗程、预约、包含项目、否定/转述/纠正、缺失、冲突、跨服务和提示注入。专业同义表达无法精确证明时明确 unknown。额外单元断言覆盖有效价格范围、小时/分钟换算、重复计分与后续纠正。

| 指标 | 自动测试结果 | 范围与限制 |
|---|---|---|
| 固定判定准确率 | 82/82 | 含非适用、未知、冲突及拒绝；不是临床泛化准确率 |
| 结构化可判定值/单位/范围/条件 | 52/52（结构化 supported/contradicted/incomplete 子集） | 另有范围与小时换算断言；中文金额等不支持语义按未知处理 |
| 跨服务污染 | 0 | 固定跨服务 case 拒绝；真实检索逐条验证服务归属 |
| 公开引用属于 trace 与 manifest | 1/1 持久化报告引用往返一致 | 事务回放及 EvidenceValidator；持久化引用往返对比；不公开私有/未引用 trace |
| 固定未知案例编造 | 0/10 | evidence_missing 保持未知，不给知识分/错题；不等同真实模型建议文案验证 |
| 专业 Recall@6 | 20/20 = 100% | PostgreSQL，10 篇固定合成资料、每篇 2 个预先指定查询；不能代表人工真实语料评测 |
| 越权/不可公开证据读取成功 | 0/7 | 跨用户、跨会话、私有 trace、未引用 trace |
| stale report 可见写入 | 0/4（错误 attempt、过期、完成后重放、并发败方） | 过期租约、错误 attempt、已完成任务重放；插入 trace 后失败验证回滚；两个并发提交仅一个胜出 |

## 尚未满足的人工门槛

82 条 case 是规则编写的固定预期，**尚无业务/医疗人员逐条人工标注复核**；20 个检索查询来自合成资料。不能将它们声称为完成了 N06 要求的人工标注与代表性专业语料验收。

微信 DevTools 尚需覆盖：两种模式、续练、断网恢复、历史摘要与空分报告、公开依据展开、知识管理、旧错题跨版本复练。真实模型未调用，其沟通质量及自由文本建议仍需 N07 受控联调。

因此本记录区分“代码及离线自动回归完成”和“全部发布硬门槛通过”。人工复核及 DevTools 未通过前，不认定 N06 全门槛完成，也不启动 N07 或合并上游。
