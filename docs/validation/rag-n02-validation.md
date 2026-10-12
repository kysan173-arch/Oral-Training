# N02 验证记录

**结果：N02 基础设施与完整 Windows/PostgreSQL 自动 CI 通过；未合并或部署。**

基线：N01 分支 `8cba0f1abc7d45ea0dfbc912cdff66b176634a54`；上游仍为 `a1ba5fb1ea028d74b64f4a1ca5917d8e05e8d740`。N01 尚未合并，因此 `fix/rag-n02` 包含 N01 提交。当前迁移新增 020，已发布 001—019 未改写。

## 实现

- 服务训练原子创建 session、固定 revision/知识 manifest 与 SHA-256 context、patient_initialization 任务。
- 用户级创建锁与两种 active 唯一索引；同 clientSessionId 同参重放、异参冲突。
- 显式 AI 任务类型分发、目标锁、dedupe、claim、renew、失败回写及过期回收；未知类型不会映射到角色互换表。
- GET 初始化状态、POST 显式重试；generation/attempt/有效 lease 共同控制胜出提交。
- 初始化成功一次性提交公开画像、私有画像、状态、round 0 开场与任务成功；公开画像投影白名单。
- 初始化未完成时保护消息/提示/结束；旧无服务路径维持 201 和既有行为。
- 默认模型网关不支持初始化时明确失败；初始化模型接口可注入，用离线 fake gateway 验证真实 worker。真实生成和 grounded 逐轮对话属于 N03，小程序服务入口属于 N04。

## 验证方法

`patient_initialization.ps1` 仅接受 test/ci 数据库，创建随机 patient_init_ schema，按序运行所有迁移，保留旧会话与评分任务 fixture，重复执行 020，并运行 `patient_initialization_test.exe`。完成后比较全量 context 摘要，确认再次迁移不改写历史数据，最后删除本次随机 schema。

专门数据库回归覆盖：
- 12 个并发相同请求只创建一个会话/context/generation；
- 同参重放、异参冲突、另一个请求的 active 冲突、不同用户权限；
- 无服务旧会话与服务会话并存；
- 初始化中/失败时消息、提示和结束保护；
- 发布新服务与新知识后旧上下文不变，新会话使用新快照；
- 过期 lease 不可续租/失败回写，重领 attempt、显式重试 generation、stale save/fail；
- 任务类型错配、瞬时失败 retry_wait、耗尽租约失败；
- 非法输出拒绝、公开/私有画像隔离、round 0 不占学员轮次、重复保存拒绝；
- 注入 fake gateway 的真实 worker 成功提交；
- 放弃后拒绝迟到结果与重试。

`knowledge_admin_api.ps1` 同时覆盖新 202 创建、重放、冲突、轮询、默认初始化器明确失败、三类动作保护、显式重试与放弃。

本地客户端恢复与证据引用测试、git diff --check 已通过。Windows 完整结果见后续记录。

## CI 修复记录

首轮 [Run 35544531114](https://github.com/chinesemisaka/Oral-Training/actions/runs/35544531114) 编译报 C1128：主翻译单元节数超出 MSVC 默认目标文件限制。为包含 main.cpp 的后端/测试目标启用 /bigobj 后重新验证；未更改优化级别或跳过测试。

[Run 35544829675](https://github.com/chinesemisaka/Oral-Training/actions/runs/35544829675)：MSVC 构建和 CTest 通过（9 Passed / 2 Skipped，数据库测试随后单独运行）。HTTP 测试中创建、重放、冲突、轮询、失败保护、重试均通过；放弃会话的新增断言误期待 200，而现有接口是 202。仅修正测试状态码，没有修改现有接口。

## 未运行

真实 DeepSeek、微信开发者工具/真机、生产迁移和部署均未运行。N02 不声明 N03 的真实患者生成或逐轮 grounded 对话已完成。

## 最终结果

[Run 35545153904](https://github.com/chinesemisaka/Oral-Training/actions/runs/35545153904)，实际通过的代码提交：`e0f30be8647282b1e4de3589dd2e659e81d4b448`，工作流结论 **success**。

- Windows/MSVC Release 全量构建通过，C1128 已解除。
- CTest：9 Passed / 0 Failed / 2 Skipped。database_feature 与 patient_initialization 因 CTest 未配置测试库变量跳过，随后在专用数据库步骤实际运行并通过。
- 静态检查：50 个 JS、37 个 JSON 通过；本地客户端恢复和证据引用测试通过。
- 空库/历史迁移、知识目录迁移、知识存储数据库测试通过。
- 知识管理与 N02 初始化 HTTP 契约通过。
- N02 隔离数据库测试通过：12 请求并发幂等、用户/任务类型隔离、版本快照、lease/generation/attempt 保护、显式/自动重试、耗尽回收、fake gateway 驱动 worker、公开画像投影。
- 有历史数据时迁移和重复迁移通过；初始化结果写入后再次运行 020，全量 context 摘要保持一致。
- 原有 database_feature、无模型 API smoke、状态机、两种训练模式各 20 请求并发测试全部通过。

真实 DeepSeek 未调用。当前 DeepSeek 网关对初始化明确报告不可用；服务患者的真实画像和 grounded 对话仍需 N03 实现。初始化成功流程由确定性测试网关覆盖，不能据此宣称真实模型质量已经验收。

最终文档提交只更新验证记录与开发状态，使用 [skip ci]；实际测试代码 SHA 如上。分支以 N01 为基线，N01/N02 均尚未合并上游。上游 PR 创建权限此前返回 403，本次没有绕过该限制。
