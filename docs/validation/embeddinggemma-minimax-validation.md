# EmbeddingGemma / MiniMax RAG 验证（2026-10-08）

基线：上游 master `37830b2`。当前改动在本地工作区，未部署到现有数据库。

## 实现

- 生产 `RagRetriever` 及管理员草稿预览使用 EmbeddingGemma 2；query/document 分别向量化，768 维，余弦排序。
- 检索不要求关键词命中；继续按锁定 manifest、服务、主题、训练范围和有效期筛选，并在返回 6 条前保留冲突检测。
- 迁移 036 添加派生向量缓存。键包含模型地址、ID、权重版本、维度与输入格式；正文摘要不匹配会重算。网络调用不持有数据库事务或连接。
- 服务失败返回 `RAG_UNAVAILABLE`。没有词法兜底，也没有将测试向量作为生产数据来源。
- MiniMax 经现有个人 OpenAI 兼容网关接入。复用生产客服提示词、请求构造、JSON 解析和 `groundedReply`。
- MiniMax 的首次输出预算最低 4096，启用 reasoning_split，追加结构化输出约束。其他模型的请求和预算不变。

## 离线验证

Linux Release 构建成功；7/7 CTest 通过：embedding_client、embedding_rag、rag_contract、evidence_validator、patient_grounding、knowledge_evaluator、rag_retriever。

独立临时 PostgreSQL 16 数据库应用全部 001–036 迁移，重跑 036 后知识原文与缓存行数均保持不变。
数据库测试包含无关键词交集召回、缓存复用、权重版本切换、损坏缓存修复、有效期/主题/未锁定版本过滤、跨服务和训练范围拒绝、预览、故障返回及冲突保留。只有一个连接的池验证模型请求发出时连接已释放。

既有 Windows 生命周期和审计测试显式注入测试向量客户端，知识管理 API 测试启动本地 HTTP fixture，避免离线测试依赖真实模型。新增 Linux CI 运行上述 RAG 检查。

## 真实链路

输入为仓库自带的 12 条**合成种植牙训练知识**；金额、周期等均为虚构演示数据。
EmbeddingGemma：`http://127.0.0.1:8765/v1`，`embeddinggemma-2`；生成：`https://api.minimax.cn/v1`，`MiniMax-M2.7`。
密钥从 `pass show api/minimax` 读入内存，通过子进程环境传递，没有保存到项目配置或日志。

最终一次受控运行 3/3 通过，每轮仅调用一次 MiniMax：

| 问题 | 实际回复 | 引用数 | 整轮耗时 |
|---|---|---:|---:|
| 我那颗后槽牙空了好久了，想弄个固定的，能直接做吗？ | 缺牙时间长通常需要先评估牙槽骨情况，看是否满足直接种植的条件。具体能不能做、要不要先处理其他问题，得医生看了X光或CBCT才能确定。 | 1 | 10.991 秒 |
| 基础档种植体、标准基台、国产全瓷冠加手术和一次CBCT，示例总价是多少？ | 示例总价约8900元/颗。 | 1 | 4.060 秒 |
| 谢谢，我先考虑一下。 | 好的，您慢慢考虑，有需要随时问。 | 0 | 2.188 秒 |

耗时包含检索、首次文档向量计算/缓存读取、MiniMax 请求及校验，不能作为纯 embedding 延迟。
最终成功运行共 11,444 tokens（输入 10,785 / 输出 659）。此前两次诊断请求发现 MiniMax 接受 json_object 后仍返回纯文字，修复输出约束后才通过；这些诊断调用不计入上面的最终运行统计。

完整脱敏结果保存在忽略目录 `backend/build/embedding-live.jsonl`，测试日志为 `backend/build/embedding-checks.log`。重现命令：

```bash
python3 backend/tests/run_embedding_checks.py --live --pass-entry api/minimax
```

## 验证边界

本机为 Linux。已验证生产 RAG 组件和共享生成/校验链路，未在本机编译或启动完整 Windows Crow API、运行 MSVC 专属集成测试或微信真机界面。部署时仍须按后端说明在目标数据库执行 036，并确保后端能访问向量服务。

三例烟测验证集成、证据及报价行为，不代表大规模检索质量评估。默认 0.65 门槛会召回部分弱相关片段，寒暄例也检索到片段，但回复校验保持 conversation 且无引用；生产语料应另行评估门槛和 Recall/Precision。当前使用应用内精确排序，尚无 ANN 索引。

MiniMax 配置依据：[M Plan](https://platform.minimax.cn/docs/m-plan/intro)、[OpenAI 兼容接口](https://platform.minimax.cn/docs/api-reference/text-openai-api)。

后续42轮连续对话、提示词与检索迭代见 [多轮验证记录](rag-multiturn-validation.md)。本页保留首次三轮冒烟的历史结果。
