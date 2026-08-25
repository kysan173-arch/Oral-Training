# Release：口腔客服智能陪练（Experiment 分支）

本说明对应 `backend/dist/oral-training-backend-win-x64.zip` 便携包。

## 版本概要

- 分支：`Experiment`
- 提交：`d896683`
- 后端：Windows x64 便携版（已内置 exe 与所需 DLL，**无需安装 Visual Studio / CMake**）

## 本次主要改动

- **身份体系修复**：`switchRole` 在 Demo 模式下改为切换真实演示账号（主管 `demo-user-001` / 学员），不再篡改用户 role，主管端不再显示学员姓名。
- **患者开场白优化**：重写 `customPatientOpening`，AI 患者开场更自然，不再机械复述用户填写的画像字段。
- **Prompt 增强**：`patientReply` 注入"学员自定义画像背景"指引，禁止占位符，情绪融入语气。
- **启动脚本**：重写 `start-backend.cmd / .ps1`，按端口查找残留进程并容错处理，避免启动卡死。
- **主管端数据**：场景练习分布平均分保留一位小数。
- **组件**：新增 `empty-state` 空状态组件、首页搜索放大镜图标、主管端演示数据种子脚本。

## 一、环境准备

1. **安装 PostgreSQL**（建议 18 或 16），并启动服务。记下超级用户密码。
2. 无需安装 Visual Studio 或 CMake（便携包已内置编译产物）。

## 二、初始化数据库

用 PostgreSQL 超级用户创建应用用户与数据库：

```sql
CREATE USER oral_training_app WITH PASSWORD 'oral_training_pass';
CREATE DATABASE oral_training OWNER oral_training_app ENCODING 'UTF8';
GRANT ALL PRIVILEGES ON DATABASE oral_training TO oral_training_app;
```

解压便携包后，在解压目录用 PowerShell 按顺序执行迁移：

```powershell
$env:PGCLIENTENCODING='UTF8'
$env:DATABASE_URL='postgresql://oral_training_app:oral_training_pass@127.0.0.1:5432/oral_training'
$psql='C:\Program Files\PostgreSQL\18\bin\psql.exe'
& $psql $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\001_initial.sql
# ... 依次执行 002 ~ 009（文件名见 migrations 目录）
```

可选：执行 `migrations\_seed_supervisor_test.sql` 生成演示学员与训练数据，便于查看主管端。

## 三、配置与启动

1. 复制 `backend.env.example` 为 `backend.env`，填写数据库密码与 DeepSeek API Key。
2. **双击 `start-backend.cmd`**。
3. 看到 `Crow/master server is running at http://127.0.0.1:8080` 即成功。
4. 健康检查：浏览器访问 `http://127.0.0.1:8080/api/health`，应看到 `database:true`、`workerRunning:true`。

## 四、前端小程序

1. 用微信开发者工具导入项目根目录（含 `app.*`、`pages/` 的目录）。
2. 开发模式勾选「不校验合法域名」。
3. 前端默认连接 `http://127.0.0.1:8080/api`，无需额外配置。

## 五、测试账号

- `AUTH_MODE=demo` 下首次进入自动使用演示主管用户 `demo-user-001`。
- 「我的」页可切换演示学员账号（`learner-test-001` ~ `004`）。
- 主管端：数据 Tab 查看团队聚合与成员摘要。

## 六、安全提醒

- **不要提交或转发 `backend.env`（含密钥）**。
- 演示环境禁止输入真实患者隐私信息。
- 生产请走 HTTPS 反向代理，并设置 `PRODUCTION=true`、`AUTH_MODE=wechat`、`WECHAT_APP_ID/SECRET`、精确 `ALLOWED_ORIGIN`、`REQUIRE_HTTPS=true`、`ALLOW_RUNTIME_API_KEY=false`。

## 校验

- 便携包：`backend/dist/oral-training-backend-win-x64.zip`
- SHA256：`0D1384990DDDF96630A57BECB3640866345B9F3CE6C036FA76818E5136BC2A2C`
