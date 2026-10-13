# 文档导航

文档按用途分类；原文件名和历史记录保留。当前接口以 [API 契约](architecture/api.md) 为准，部署从 [完整本地配置教程](setup/RAG本地配置与数据初始化教程.md) 开始。

设计草案、旧模型说明和验证记录保留各自原有的日期与状态，使用时请以文档中的适用范围为准。

| 目录 | 内容 |
|---|---|
| [product/](product/) | 产品与规划 |
| [setup/](setup/) | 部署与配置 |
| [architecture/](architecture/) | 接口与技术设计 |
| [models/](models/) | 模型与提示词 |
| [training/](training/) | 训练场景与语料规范 |
| [validation/](validation/) | 验证与修复记录 |
| [reports/](reports/) | 成果汇报 |
| [rag-data/](rag-data/) | 合成 RAG 演示知识，保留路径供初始化脚本与测试使用 |

## 产品与规划

- [口腔医疗客服智能陪练系统 · 技术说明文档（前端聚焦版）](product/prd.md)
- [口腔医疗客服智能陪练系统](product/口腔医疗客服智能陪练系统_产品设计文档.md)
- [模块 3 培训运营 + 模块 4 数据报表 实现计划](product/主管端培训运营与数据报表方案.md)
- [项目计划书（Word）](product/项目计划书.docx)

## 部署与配置

- [本地配置教程入口](setup/setup.md)
- [RAG 本地配置与数据初始化教程](setup/RAG本地配置与数据初始化教程.md)
- [EmbeddingGemma 2 配置教程](setup/embeddinggemma-setup.md)

## 接口与技术设计

- [口腔客服智能陪练 API 契约](architecture/api.md)
- [后端可靠性与生产架构](architecture/后端构建方案.md)
- [口腔客服智能陪练：服务与专业知识 RAG 完整设计](architecture/rag-architecture-design.md)
- [服务与专业知识 RAG 后续开发计划](architecture/rag-development-plan.md)

## 模型与提示词

- [模型接入指南：如何用自己的模型跑这个小程序](models/模型接入指南.md)
- [模型提示词清单与输出契约](models/模型提示词清单.md)

## 训练场景与语料规范

- [训练场景设计规范](training/训练场景设计规范.md)
- [语料反推场景草案（P2-3）](training/语料反推场景草案.md)
- [口腔医疗客服语料筛选规则文档_v0.1（Word）](training/口腔医疗客服语料筛选规则文档_v0.1.docx)

## 验证与修复记录

- [完整手动测试知识库清单](validation/manual-knowledge-checklist.md)
- [DeepSeek 双角色 RAG 验证](validation/deepseek-dual-role-validation.md)
- [EmbeddingGemma / MiniMax RAG 验证（2026-10-08）](validation/embeddinggemma-minimax-validation.md)
- [项目审核问题修复记录 · 2026-09-29](validation/project-audit-fixes-2026-09-29.md)
- [RAG 对话质量修复验证](validation/rag-conversation-quality-validation.md)
- [RAG 与个人模型路由增量核对](validation/rag-model-increment-review.md)
- [EmbeddingGemma / MiniMax 多轮 RAG 迭代](validation/rag-multiturn-validation.md)
- [N01 验证记录](validation/rag-n01-validation.md)
- [N02 验证记录](validation/rag-n02-validation.md)
- [N03 验证记录](validation/rag-n03-validation.md)
- [N04 验证记录](validation/rag-n04-validation.md)
- [N05 知识核验器与确定性评分](validation/rag-n05-validation.md)
- [N06 验收记录](validation/rag-validation-report.md)
- [N07 分段开关、受控联调与发布记录](validation/rag-n07-validation.md)
- [标准客服自然表达实测（2026-09-29）](validation/service-reply-evaluation-2026-09-29.md)

## 成果汇报

- [训练模块改进 · 成果汇报稿](reports/训练模块改进_成果汇报稿.md)

## RAG 演示数据

- [患者模拟 RAG 测试知识库（具体参数版）](rag-data/患者模拟_RAG测试知识库_具体参数版.json)
- [完整手测补充知识与提问](rag-data/manual-test-knowledge.json)

演示知识中的价格、时长与机构参数为合成数据。
