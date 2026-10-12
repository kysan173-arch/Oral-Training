# EmbeddingGemma 2 配置教程

本项目用 EmbeddingGemma 2 将问题和知识片段转换为向量，再做语义检索。它独立于生成患者、客服答复及报告的模型；换生成模型不需要更换检索模型。

本文包含现有 Linux 服务检查，以及新机器可运行的文本 API 示例。模型为 [`google/embeddinggemma-2`](https://huggingface.co/google/embeddinggemma-2)，默认 768 维；查询和文档使用不同的任务前缀。模型能力及维度说明见 [Google 模型卡](https://ai.google.dev/gemma/docs/embeddinggemma/model_card_2)，加载方式见 [官方 Sentence Transformers 示例](https://ai.google.dev/gemma/docs/embeddinggemma/multimodal-embeddinggemma-with-sentence-transformers)。

## 1. 已有本机服务：检查和启动

2026-10-12 核对的开发机已安装：

| 项目 | 当前值 |
|---|---|
| 部署目录 | `$HOME/.local/share/embeddinggemma2` |
| 用户服务 | `embeddinggemma2.service` |
| 健康接口 | `http://127.0.0.1:8765/health` |
| 向量接口 | `http://127.0.0.1:8765/v1/embeddings` |
| API 模型 ID | `embeddinggemma-2` |
| 推理配置 | CPU、BF16、8 计算线程、1 interop 线程、单 worker |
| 默认输出 | 768 维、L2 归一化浮点数 |

在拥有该服务的 Linux 用户下运行：

```bash
systemctl --user status embeddinggemma2.service --no-pager
# 未运行时启动；无需为了健康检查重启。
systemctl --user start embeddinggemma2.service
curl --fail --noproxy '*' http://127.0.0.1:8765/health
journalctl --user -u embeddinggemma2.service -n 50 --no-pager
```

预期健康响应包含 `status=ok`、`model=embeddinggemma-2`、`dimension=768`。`Unit ... not found` 表示本用户未安装该服务，应按下一节部署，不能据此认为权重已安装。

这台机器的部署目录、虚拟环境和权重属于机器本地文件，**不在 Oral Training 仓库中**。已有 `server.py` 还支持多模态；Oral Training 当前只调用文本向量接口。不要覆盖正常运行的部署目录或再启动一个占用 8765 的进程。

## 2. 新机器：安装权重和文本 API

以下命令按 Linux / Python 3.12 编写，安装目录使用独立的 `oral-embedding`，便于与已有服务区分。Windows 后端可以通过本地 SSH 转发使用这个 Linux 服务（见第 6 节）；本文没有验证原生 Windows 的模型依赖安装。

### 2.1 安装依赖与下载权重

下面固定的是本机已使用的核心依赖版本。CPU 模型无需 CUDA；为权重和独立 Python 环境预留至少数 GB 磁盘及可用内存。权重约 1.49 GB，实际内存随精度、批量和输入长度变化。

```bash
sudo apt-get install -y python3-venv
export ORAL_EMBED_DIR="$HOME/.local/share/oral-embedding"
mkdir -p "$ORAL_EMBED_DIR"
python3 -m venv "$ORAL_EMBED_DIR/.venv"
"$ORAL_EMBED_DIR/.venv/bin/python" -m pip install --upgrade pip
"$ORAL_EMBED_DIR/.venv/bin/python" -m pip install \
  'torch==2.14.1+cpu' 'torchvision==0.29.1+cpu' \
  --index-url https://download.pytorch.org/whl/cpu
"$ORAL_EMBED_DIR/.venv/bin/python" -m pip install \
  'sentence-transformers==6.1.0' 'transformers==5.19.0' \
  'fastapi==0.142.4' 'uvicorn==0.54.0' 'huggingface-hub==1.33.0'
"$ORAL_EMBED_DIR/.venv/bin/hf" download google/embeddinggemma-2 \
  --revision 914f7f89142e33e77833254d9c9b90c3cef7303b \
  --local-dir "$ORAL_EMBED_DIR/model"
"$ORAL_EMBED_DIR/.venv/bin/python" -m pip freeze > "$ORAL_EMBED_DIR/requirements.lock.txt"
sha256sum "$ORAL_EMBED_DIR/model/model.safetensors"
```

该固定 revision 的权重 SHA-256 应为：

```text
197a32965d4b1105faf060417baa899e193fb73cd401f42ec9295234d5553d79
```

下载需要联网；下载完成后服务只读本地文件。若依赖仓库不提供对应平台的 wheel，应先核对 Python/CPU 平台，不要直接把旧代 EmbeddingGemma 模型改名冒充这个模型。若修改了核心依赖或权重，重新完成第 3 节和 RAG 用例验证，并保留安装锁定记录。

### 2.2 保存接口适配器

将以下 Python 代码保存为 `$ORAL_EMBED_DIR/server.py`。它只提供本项目需要的文本接口，固定 CPU，使用 `SearchQuery` / `Document` 前缀；显式文本对象确保 URL 或文件路径也按普通文本处理。只在回环地址运行，不提供认证或公网部署能力。

```python
import os
import threading
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Literal

os.environ['HF_HUB_OFFLINE'] = '1'
os.environ['TRANSFORMERS_OFFLINE'] = '1'
os.environ['TOKENIZERS_PARALLELISM'] = 'false'

import numpy as np
import torch
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, ConfigDict, StrictStr
from sentence_transformers import SentenceTransformer
from sentence_transformers.util import batch_to_device

MODEL_ID = 'embeddinggemma-2'
MODEL_PATH = Path(os.environ.get('EMBEDDINGGEMMA_MODEL_PATH', Path(__file__).parent / 'model'))
PROMPTS = {'query': 'SearchQuery', 'document': 'Document'}
DTYPE = os.environ.get('EMBEDDING_DTYPE', 'bfloat16')
lock = threading.Lock()
model = None


@asynccontextmanager
async def lifespan(app):
    global model
    if DTYPE not in ('bfloat16', 'float32'):
        raise ValueError('Use bfloat16 or float32')
    torch.set_num_threads(int(os.environ.get('EMBEDDINGGEMMA_THREADS', '8')))
    torch.set_num_interop_threads(1)
    model = SentenceTransformer(
        str(MODEL_PATH), device='cpu', local_files_only=True, trust_remote_code=False,
        model_kwargs={'torch_dtype': getattr(torch, DTYPE), 'attn_implementation': 'sdpa'},
    )
    model.eval()
    yield
    model = None


app = FastAPI(lifespan=lifespan)


class Request(BaseModel):
    model_config = ConfigDict(extra='forbid')
    model: Literal['embeddinggemma-2']
    input: StrictStr | list[StrictStr]
    task: Literal['query', 'document'] = 'document'
    dimensions: Literal[128, 256, 512, 768] = 768
    encoding_format: Literal['float'] = 'float'


@app.get('/health')
def health():
    if model is None:
        raise HTTPException(503, 'Model not loaded')
    return {'status': 'ok', 'model': MODEL_ID, 'dimension': 768,
            'device': 'cpu', 'dtype': DTYPE}


@app.post('/v1/embeddings')
def embeddings(req: Request):
    items = [req.input] if isinstance(req.input, str) else req.input
    if not 1 <= len(items) <= 16 or any(not text.strip() for text in items):
        raise HTTPException(400, 'Expected 1..16 nonempty texts')
    prompt = model.prompts[PROMPTS[req.task]]
    vectors = []
    with lock, torch.inference_mode():
        for offset in range(0, len(items), 8):
            batch = items[offset:offset + 8]
            for text in batch:
                if len(model.tokenizer(prompt + text, truncation=False)['input_ids']) > 8192:
                    raise HTTPException(400, 'Split text exceeding 8192 tokens')
            features = model.preprocess(
                [{'text': text} for text in batch], prompt=prompt,
                processing_kwargs={'text': {'truncation': False}},
            )
            if int(features['attention_mask'].sum(dim=-1).max()) > 8192:
                raise HTTPException(400, 'Split text exceeding 8192 tokens')
            result = model(batch_to_device(features, model.device))['sentence_embedding']
            vectors.append(result[:, :req.dimensions].float().cpu().numpy())
    values = np.concatenate(vectors, axis=0)
    norms = np.linalg.norm(values, axis=1, keepdims=True)
    if not np.isfinite(values).all() or not np.isfinite(norms).all() or np.any(norms <= 0):
        raise HTTPException(503, 'Invalid embedding')
    values = values / norms
    return {'object': 'list', 'model': MODEL_ID, 'data': [
        {'object': 'embedding', 'index': i, 'embedding': row.tolist()}
        for i, row in enumerate(values)
    ]}
```

### 2.3 启动服务

在模型所在机器运行，保持终端开启：

```bash
export ORAL_EMBED_DIR="$HOME/.local/share/oral-embedding"
cd "$ORAL_EMBED_DIR"
EMBEDDING_DTYPE=bfloat16 EMBEDDINGGEMMA_THREADS=8 \
  .venv/bin/uvicorn server:app --host 127.0.0.1 --port 8765 --workers 1
```

首次加载需要几秒至更长时间，看到 Uvicorn 启动完成后再检查。只能用一个 worker，多个进程会分别占用模型内存。若 CPU 对 BF16 支持不佳，可换成 `float32` 并重新验证；不要使用 `float16`。

需要登录后自动启动时，可将下列内容保存为 `$HOME/.config/systemd/user/oral-embedding.service`（先创建目录），然后运行下面的管理命令。仅在没有其他进程使用 8765 时启用。

```ini
[Unit]
Description=Oral Training local text embeddings
After=network.target

[Service]
WorkingDirectory=%h/.local/share/oral-embedding
Environment=EMBEDDING_DTYPE=bfloat16
Environment=EMBEDDINGGEMMA_THREADS=8
ExecStart=%h/.local/share/oral-embedding/.venv/bin/uvicorn server:app --host 127.0.0.1 --port 8765 --workers 1
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
```

```bash
systemctl --user daemon-reload
systemctl --user enable --now oral-embedding.service
systemctl --user status oral-embedding.service --no-pager
journalctl --user -u oral-embedding.service -n 50 --no-pager
```

用户服务的生命周期取决于登录会话和系统设置，不等于系统开机后无人登录也会常驻。已有部署使用 `embeddinggemma2.service`；上面的新部署使用 `oral-embedding.service`，请操作对应名称。

## 3. 验证真实向量，而不只看进程

从**后端所在机器**运行下面的标准库 Python 检查；不需要生成模型 Key，也不会产生生成模型费用。Linux 用 `python3`，Windows 把代码保存为临时 `.py` 后用 `python` 运行：

```python
import json
import math
import urllib.request

http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
with http.open('http://127.0.0.1:8765/health', timeout=10) as response:
    print(json.load(response))
for task in ('query', 'document'):
    payload = {'model': 'embeddinggemma-2', 'input': ['种植牙报价包含哪些项目？'],
               'task': task, 'dimensions': 768, 'encoding_format': 'float'}
    request = urllib.request.Request(
        'http://127.0.0.1:8765/v1/embeddings',
        data=json.dumps(payload, ensure_ascii=False).encode('utf-8'),
        headers={'Content-Type': 'application/json'},
    )
    with http.open(request, timeout=30) as response:
        body = json.load(response)
    assert body['model'] == 'embeddinggemma-2'
    assert len(body['data']) == 1 and body['data'][0]['index'] == 0
    vector = body['data'][0]['embedding']
    assert len(vector) == 768 and all(math.isfinite(v) for v in vector)
    norm = math.sqrt(sum(v * v for v in vector))
    assert abs(norm - 1.0) < 0.001
    print(task, 'OK: 768 finite normalized values')
```

成功后再检查后端检索预览和真实资料命中。接口返回正确维度，只证明接口和推理可用，不代表业务召回质量已经达标。

## 4. 后端参数

将以下变量加入后端配置，修改后重启后端。完整示例在 `backend/.env.example`。

| 变量 | 默认值 | 说明 |
|---|---|---|
| `RAG_EMBEDDING_BASE_URL` | `http://127.0.0.1:8765/v1` | 后端追加 `/embeddings`；不要填成完整接口地址 |
| `RAG_EMBEDDING_MODEL` | `embeddinggemma-2` | 必须和响应的 `model` 一致，不是权重下载路径 |
| `RAG_EMBEDDING_DIMENSIONS` | `768` | 支持 128/256/512/768，查询和文档必须相同 |
| `RAG_EMBEDDING_REVISION` | `1` | 本项目的缓存版本；同名权重、前缀或推理表示改变时递增 |
| `RAG_EMBEDDING_BATCH_SIZE` | `16` | 每次 HTTP 文档批量 1–16，低内存机器可减小 |
| `RAG_EMBEDDING_TIMEOUT_SECONDS` | `30` | 单次读写超时 1–60 秒，首次缓存填充可能较慢 |
| `RAG_EMBEDDING_MIN_SIMILARITY` | `0.65` | 余弦召回阈值，合法范围 −1 至 1；调整后用业务用例复核 |

后端固定为问题传 `task=query`，知识传 `task=document`，要求 `encoding_format=float`。自建兼容服务须支持这些字段、批量输入和正确的 `index`，不能仅返回一个任意 768 维数组。当前 Embedding 客户端没有独立的 API Key 配置；不要把 Key 放进 URL。

服务不可达、超时、响应模型不匹配、维度错误、非有限值或零向量会报 `RAG_UNAVAILABLE`。系统不会静默退回旧的字面关键词检索。

## 5. 向量缓存与换模型

先执行迁移 036。已发布知识分块仍保存在 `knowledge_chunks`；生成的向量作为派生数据保存在 `knowledge_chunk_embeddings`，JSONB 存储，无需安装 pgvector，当前由应用内精确余弦排序。

旧知识不需要重新发布。首次检索会为候选片段补向量，后续复用缓存；完整测试数据脚本会预热样本缓存。草稿预览不会写入已发布知识的向量缓存。即使文档缓存齐全，新问题仍需请求查询向量，因此 Embedding 服务必须保持可用。

缓存标识包含 Base URL、模型 ID、维度、`RAG_EMBEDDING_REVISION`、文档格式版本，还核对文本 SHA-256。当前文档输入由标题、正文和适用范围组成，并使用 Document 前缀。

换模型/权重时：

1. 在独立端口验证新服务的 query/document 接口和检索质量。
2. 更新后端地址/模型/维度；同名模型原址更换权重、前缀或表示设置时，递增 `RAG_EMBEDDING_REVISION`。
3. 重启后端，让新命名空间按需建立缓存，再完成资料预览和两种训练验收。
4. 保留旧缓存以便回退；不要删除服务/知识 revision，也不要把改维度当作修改旧向量的理由。

资料快照与向量缓存不同：旧会话继续使用其锁定资料，新发布内容应在新会话验证。更换检索器本身仍需回归旧会话的检索行为。

## 6. Windows 后端使用另一台 Linux 的模型

如果 Linux 上已有 SSH 服务、当前账号有访问权限，可在 Windows PowerShell 建立本地转发，替换用户名与主机名：

```powershell
ssh -N -o ExitOnForwardFailure=yes -L 127.0.0.1:8765:127.0.0.1:8765 <用户名>@<Linux主机>
```

保持 SSH 窗口运行，Windows 后端仍配置 `http://127.0.0.1:8765/v1`。然后从 Windows 执行第 3 节检查。若本地 8765 被占用，可把转发左侧端口改为 8766，同时更新检查脚本和后端 Base URL。

远程直连需 HTTPS，且当前客户端不能发送独立 Embedding 鉴权头。已有服务没有认证，不应直接绑定 `0.0.0.0` 暴露公网。WSL、容器和虚拟机各自的 `127.0.0.1` 可能指不同网络空间，以后端进程能否访问为准。

## 7. 常见问题

| 现象 | 处理 |
|---|---|
| `Connection refused` | 检查对应 systemd 用户服务、端口、SSH 转发和模型加载日志 |
| 端口被占用 | 用 `ss -ltnp` 或 Windows `Get-NetTCPConnection -LocalPort 8765` 确认进程；不要启动重复服务 |
| 找不到模型/要求联网 | 检查本地下载是否完整、固定 revision、`EMBEDDINGGEMMA_MODEL_PATH`；服务设计为离线加载 |
| `422` | 检查 `model`、`task`、`dimensions`、额外字段和 JSON 格式 |
| 请求过长/内存不足 | 分块或降低批量，保留单 worker；不能静默截断知识后当作完整依据 |
| 向量成功但 RAG 失败 | 检查 036、表权限、发布范围、后端实际加载的配置和错误日志 |
| 换权重后仍沿用向量 | 递增 `RAG_EMBEDDING_REVISION` 并重启后端 |

返回 [完整本地配置与测试数据教程](RAG本地配置与数据初始化教程.md)。
