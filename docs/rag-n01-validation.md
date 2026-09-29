# N01 验证记录

日期：2026-09-20。上游基线：`a1ba5fb1ea028d74b64f4a1ca5917d8e05e8d740`。
分支：`fix/rag-n01`。先将个人 fork master 从 `3174405` 快进同步上游（24 个提交）。最新迁移为 `019_roleplay_free_template.sql`，本次未改迁移。

**当前结果：N01 实现与仓库完整自动 CI 已通过；微信模拟器/真机未验证，真实模型未调用。上游 PR 创建因集成权限不足未成功，尚未合并或部署。**

## 实施范围

共享 SHA-256、规范化 manifest、EvidenceValidator、结构化事实完整渲染、无合法选择时 unknown、固定沟通文本、确定性有据复盘、胜出事务校验、复盘引用展示。N02—N07 未开始。

实现选择：校验器采用 header-only，使纯离线测试无需依赖 Crow/WinHTTP/数据库；Windows 使用现有 BCrypt SHA-256，离线 Linux 测试使用 OpenSSL EVP。v2 复盘直接复用已公开且已经引用的证据，不新增模型契约或调用。原始旧 hash/trace 不改写，读取时验证并规范化；缺少服务范围元数据的旧 passage 不在新复盘复用。

## 已运行

环境：Linux、GCC 13、Node.js；nlohmann JSON v3.11.3，OpenSSL。下面的 `$JSON_INCLUDE` 指向包含 `nlohmann/json.hpp` 的依赖目录。

| 命令 | 结果 |
|---|---|
| `g++ -std=c++17 -Wall -Wextra -Werror -I "$JSON_INCLUDE" backend/tests/evidence_validator_test.cpp -lcrypto -o /tmp/evidence_validator_test && /tmp/evidence_validator_test` | Passed，退出码 0，58 项断言 |
| `g++ -std=c++17 -I "$JSON_INCLUDE" backend/tests/rag_contract_test.cpp -o /tmp/rag_contract_test && /tmp/rag_contract_test` | Passed，退出码 0 |
| `node backend/tests/roleplay_evidence_test.js` | Passed，退出码 0；正确引用、版本/ID/revision 不匹配、空会话 |
| `node backend/tests/client_recovery_test.js` | Passed，退出码 0 |
| 对仓库所有 JS 运行 `node --check`，对 JSON 执行解析 | Passed，50 个 JS、37 个 JSON，退出码 0 |
| `git diff --check` | Passed，退出码 0 |

SHA 标准向量包括空串、abc、一百万个 a。证据测试覆盖跨 trace 的同名 E1、跨服务 revision、未知 ID、重复 ID、未公开证据、manifest 顺序/去重、空知识快照、金额小数/范围/单位/起价/有效期、时长条件、无证据 unknown、部分缺失、冲突、不截断长证据、模型自由文本事实注入，以及复盘引用的哈希一致性。

初次以 `-Werror` 编译现有 rag_contract 时，原有 model_gateway.h 的未使用参数触发警告；按原项目非 Werror 设置编译后通过，未为此改动无关模型接口。

## 初次本地验证的限制（后续 CI 结果见下文）

- Windows/MSVC 完整构建、CTest：本地 Not run，当前环境无 Windows 编译工具。新证据测试和 UI 引用测试已经注册 CTest；待仓库 CI 验证。
- PostgreSQL 集成、历史 MD5 投影、任务并发/权限的数据库回归及无模型 HTTP smoke：本地 Not run，当前环境无 PostgreSQL。
- 新增 retriever 摘要不匹配测试已写入，但依赖 libpqxx 的 retriever 测试本地 Not run。
- 微信开发者工具模拟器/真机视觉与交互：Not run；仅完成脚本、JSON 和引用行为测试。
- 真实 DeepSeek：Not run，遵循 N07 前不调用真实模型的边界。

当时 N01 状态为“实现完成，离线核心验证通过，Windows/数据库集成验收待确认”，不将核心单元测试等同于完整上线验收。需要 Windows CI/数据库回归通过后再开放发布。

## 远端 Windows CI 第一次运行

[Run 35498088974](https://github.com/chinesemisaka/Oral-Training/actions/runs/35498088974)，提交 `ee18c85`：MSVC Release 全量构建通过；CTest 9 Passed、0 Failed、database_feature 1 Skipped（CTest 阶段未设置测试库变量）。证据校验、retriever、报告/安全配置、客户端恢复和静态检查全部通过；knowledge_store_database_test 通过。

后续 knowledge_admin_api.ps1 失败：测试库仅执行 001—011，而当前后端已经依赖 019 的 free_description 字段，创建角色互换会话返回 500。无模型 API/状态机/并发测试因此跳过。该失败是同步上游后的测试初始化与当前 schema 不一致。

修复测试基础设施：当前后端 API smoke 和 workflow 的当前库初始化按名称顺序应用所有三位数字编号 SQL（当前 001—019），明确排除 `_seed_supervisor_test.sql`；专门的历史迁移 fixtures 保持原范围，已发布迁移没有修改。此修复为完成 N01 集成验证所需，未扩展到 N02。

## 远端 Windows CI 第二次运行

[Run 35498367274](https://github.com/chinesemisaka/Oral-Training/actions/runs/35498367274)，提交 `0cf18e7`：MSVC、CTest、迁移/知识存储及 knowledge_admin_api 全部通过，修复了测试库缺少 019 字段的问题。

数据库功能测试随后在原有“话术分类筛选”断言失败：fixture 同时为当前报告与 400 天前的历史报告写入相同分类话术，但断言只期待一条。该查询没有时间窗口，应该返回两份；修正为精确核对两份会话 ID、phraseKey 和分类，保留其他分类必须为空的断言，不修改业务代码。此处之前的失败阻止了无模型 HTTP smoke/状态机/并发测试继续执行。

## 远端 Windows CI 第三次运行及当时阻塞

[Run 35498662976](https://github.com/chinesemisaka/Oral-Training/actions/runs/35498662976)，代码提交 `60ddf18e560cb487b9a67befeefd4c9580e2dccb`：

- Passed：Windows/MSVC Release 全量构建；CTest 9 Passed / 0 Failed / 1 Skipped；空库/历史迁移脚本、知识目录迁移、知识存储数据库测试、知识管理 API smoke。
- 分类话术断言已通过，执行继续到主管看板测试。
- Failed：`database_feature_test` 报 `supervisor aggregate did not cover every scenario`。当前场景目录过滤 `is_active AND NOT is_template`，主管看板场景统计口径不同；019 增加自由模拟模板后暴露该差异。这两处业务查询均来自上游，本次未修改。需要在主管看板任务中明确统计范围并修复/验证，不能直接放宽断言。
- Not run：后续通用无模型 HTTP smoke、状态机和会话并发脚本（被前述失败阻断）；微信模拟器/真机；真实 DeepSeek。

因此：**N01 实现已提交，核心离线与 Windows 构建/单元/知识管理集成验证通过；仓库总 CI 仍失败，不能宣布全量验收通过或直接合并。** 本次不延伸修改主管看板产品逻辑。原始失败日志与之前两处测试修复均保留。

上游草稿 PR 创建被 GitHub 403 `Resource not accessible by integration` 拒绝；开发代码已保存到个人 fork `fix/rag-n01`，未合并 master。

## 后续继续修复

用户确认继续后，修复主管与学员看板的场景列表：与训练目录一致，仅展示启用且非模板的场景，保留零训练次数行；团队、用户及时间筛选保持不变，总览历史计数不删减。场景测试改为核对精确 ID 集合，smoke 不再写死 4 个场景。

[Run 35519678999](https://github.com/chinesemisaka/Oral-Training/actions/runs/35519678999)，提交 `32fd3f7`：场景断言通过，后续周统计断言失败。测试预期查询误包含团队外学员的混合评分 fixture；改为仅查询已归属的两名测试学员，保留完成时间边界检查。后端团队隔离行为未放宽。

上游草稿 PR 再次创建仍返回 403 `Resource not accessible by integration`，没有创建或合并 PR。

[Run 35519989443](https://github.com/chinesemisaka/Oral-Training/actions/runs/35519989443)（`93e967b`）和 [Run 35520052908](https://github.com/chinesemisaka/Oral-Training/actions/runs/35520052908)（`5bc0cf9`）：主管周统计及团队成员测试通过，随后在“200 份报告之后的历史收藏”回归失败。收藏话术和未掌握错题查询在解析筛选前固定截断最近 200 份报告，导致旧数据不可见。改为按完成时间、会话 ID 稳定排序，每批 200 份继续读取，直到满足接口条数上限或历史耗尽；仍保留用户隔离、分类/场景筛选及原有历史数据断言。

## 最终自动验证结果

[Run 35520367602](https://github.com/chinesemisaka/Oral-Training/actions/runs/35520367602)，代码提交 `6df450746dbd2a72cf5fff836dada931c346f970`，整个 Windows PostgreSQL CI **success**：

- MSVC Release 全量构建通过；CTest 9 Passed / 0 Failed / 1 Skipped。database_feature 在 CTest 阶段因未配置测试库变量跳过，随后在专门数据库步骤实际运行并通过。
- 50 个 JS、37 个 JSON 静态检查通过；空库与历史迁移、知识目录迁移、知识存储数据库、知识管理 API 全部通过。
- database_feature_test 通过：场景目录、团队统计、历史收藏/错题分页、旧报告兼容、任务锁顺序等回归通过。
- 无模型 API smoke 通过：客服训练与角色互换目录均为 5 个有效场景。
- 状态机通过：幂等冲突、等待回复、generation、自愈与已放弃会话保护。
- 会话并发通过：两种模式各 20 请求，连接池上限 12、最终打开 2。
- 真实模型：未调用（ModelConfigured=false、ModelTest=skipped）；微信开发者工具/真机：未运行。

N01 自动验收阻塞已解除。未开始 N02—N07，未合并 master 或部署生产。最终文档提交仅更新记录并使用 [skip ci]；上述代码 SHA 是实际通过整套 CI 的版本。
