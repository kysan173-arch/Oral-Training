口腔客服智能陪练后端（Windows x64）

1. 安装并启动 PostgreSQL。
2. 备份现有数据库。
3. 使用 psql 按顺序执行 migrations\001_initial.sql、002_roleplay.sql、
   003_reliability.sql、004_identity.sql、005_learner_insights.sql、
   006_training_experience.sql、007_supervisor_growth.sql。
4. 复制 backend.env.example 为 backend.env，并填写数据库、DeepSeek 和身份配置。
5. 双击 start-backend.cmd。

本机演示可使用 AUTH_MODE=demo；小程序仍通过 /api/auth/wechat 获取 bearer token。
生产必须设置 PRODUCTION=true、AUTH_MODE=wechat、WECHAT_APP_ID、WECHAT_APP_SECRET、
精确 ALLOWED_ORIGIN、REQUIRE_HTTPS=true 和 ALLOW_RUNTIME_API_KEY=false，并通过 HTTPS
反向代理访问。代理需传递 X-Forwarded-Proto: https。

健康检查：GET /api/health。应确认 database=true、workerRunning=true，并监控
pendingJobs/deadJobs。健康接口不会返回任务内容、Prompt 或密钥。

003 迁移会无损归档历史重复轮次并补建 generating 任务，迁移本身不会调用模型。
请勿提交 backend.env，也不要在前端保存模型、数据库或微信密钥。
