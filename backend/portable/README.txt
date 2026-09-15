口腔客服智能陪练后端（Windows x64 便携版）

本目录为绿色便携包：exe 与所需 DLL 已内置，无需安装 Visual Studio 或 CMake。
你只需安装 PostgreSQL、初始化数据库，然后双击 start-backend.cmd 即可运行。

1. 安装并启动 PostgreSQL。
2. 备份现有数据库。
3. 使用 psql 按顺序执行 migrations\001_initial.sql、002_roleplay.sql、
   003_reliability.sql、004_identity.sql、005_pair_and_state_repair.sql、
   006_learner_insights.sql、007_training_experience.sql、008_supervisor_growth.sql、
   009_legacy_report_totals.sql、010_knowledge_catalog.sql、011_roleplay_rag_mvp.sql、
   012_custom_patient_profile.sql、013_recommendation_scenario.sql、014_training_plans.sql、
   015_supervisor_team.sql、016_message_emotion.sql、017_hint_per_round.sql、
   018_scenario_reaction_rules.sql、019_roleplay_free_template.sql。
4. 复制 backend.env.example 为 backend.env，并填写数据库、DeepSeek 和身份配置。
5. 双击 start-backend.cmd。

==========================================================
一、环境准备
==========================================================
1. 安装并启动 PostgreSQL（建议 18 或 16）。
   记下超级用户（postgres）密码；把 bin 目录（如 C:\Program Files\PostgreSQL\18\bin）
   加入系统 PATH，或用完整路径调用 psql.exe。

本机演示可使用 AUTH_MODE=demo；小程序仍通过 /api/auth/wechat 获取 bearer token。
生产必须设置 PRODUCTION=true、AUTH_MODE=wechat、WECHAT_APP_ID、WECHAT_APP_SECRET、
HTTPS ALLOWED_ORIGIN、REQUIRE_HTTPS=true、非空 TRUSTED_PROXY_IPS 和
ALLOW_RUNTIME_API_KEY=false，并通过 HTTPS 反向代理访问。只有可信代理提供的
X-Forwarded-For 和 X-Forwarded-Proto 会被接受；无效或降级配置会导致程序拒绝启动。

2. 创建应用用户和数据库（用超级用户执行）：
   psql -U postgres -h 127.0.0.1
     CREATE USER oral_training_app WITH PASSWORD 'oral_training_pass';
     CREATE DATABASE oral_training OWNER oral_training_app ENCODING 'UTF8';
     GRANT ALL PRIVILEGES ON DATABASE oral_training TO oral_training_app;
   \q

==========================================================
二、初始化数据库（执行迁移）
==========================================================
在本目录用 PowerShell 按顺序执行（注意先设置编码）：
  $env:PGCLIENTENCODING='UTF8'
  $env:DATABASE_URL='postgresql://oral_training_app:oral_training_pass@127.0.0.1:5432/oral_training'
  & 'C:\Program Files\PostgreSQL\18\bin\psql.exe' $env:DATABASE_URL -v ON_ERROR_STOP=1 -f migrations\001_initial.sql
  ...（002 ~ 019 同理，依次执行）

迁移文件：001_initial、002_roleplay、003_reliability、004_identity、
005_pair_and_state_repair、006_learner_insights、007_training_experience、
008_supervisor_growth、009_legacy_report_totals、010_knowledge_catalog、
011_roleplay_rag_mvp、012_custom_patient_profile、013_recommendation_scenario、
014_training_plans、015_supervisor_team、016_message_emotion、017_hint_per_round、
018_scenario_reaction_rules、019_roleplay_free_template。
迁移可重复执行（幂等）。

可选演示数据：若想立即看到主管端聚合/成员数据，可在迁移后执行：
  & $psql $env:DATABASE_URL -f migrations\_seed_supervisor_test.sql
（会创建 4 个演示学员及若干已完成训练，仅供测试，非必需。）
preflight_reliability.sql 为生产预检脚本，开发测试可忽略。

==========================================================
三、配置 backend.env
==========================================================
复制 backend.env.example 为 backend.env，并编辑：
- DATABASE_URL：改成你的数据库密码
- DEEPSEEK_API_KEY：填你的 DeepSeek Key（训练/评分功能需要）
- 其余保持默认即可（AUTH_MODE=demo 免微信配置）

==========================================================
四、启动
==========================================================
双击 start-backend.cmd。
看到 "Crow/master server is running at http://127.0.0.1:8080" 即成功。

健康检查：GET /api/health。应确认 HTTP 200、ready=true、database=true、
workerRunning=true、workersInDatabaseBackoff=0，并监控 pendingJobs/deadJobs 与
databasePool。API、身份服务和 Worker 共用有界连接池；连接池耗尽时返回 503。
健康接口不会返回任务内容、Prompt 或密钥。

==========================================================
五、前端（小程序）
==========================================================
用微信开发者工具导入小程序项目根目录（本便携包只含后端，前端在仓库根）。
开发模式勾选「不校验合法域名」，前端默认连 http://127.0.0.1:8080/api。

==========================================================
安全提醒
==========================================================
- 不要提交或转发 backend.env（含密钥）。
- 演示环境禁止输入真实患者隐私。
- 生产请走 HTTPS 反向代理，并设置 PRODUCTION=true、AUTH_MODE=wechat、
  WECHAT_APP_ID/SECRET、HTTPS ALLOWED_ORIGIN、REQUIRE_HTTPS=true、
  非空 TRUSTED_PROXY_IPS、ALLOW_RUNTIME_API_KEY=false。
