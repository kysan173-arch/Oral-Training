口腔客服智能陪练后端（Windows x64 便携版）

本目录为绿色便携包：exe 与所需 DLL 已内置，无需安装 Visual Studio 或 CMake。
你只需安装 PostgreSQL、初始化数据库，然后双击 start-backend.cmd 即可运行。

==========================================================
一、环境准备
==========================================================
1. 安装并启动 PostgreSQL（建议 18 或 16）。
   记下超级用户（postgres）密码；把 bin 目录（如 C:\Program Files\PostgreSQL\18\bin）
   加入系统 PATH，或用完整路径调用 psql.exe。

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
  ...（002 ~ 009 同理，依次执行）

迁移文件：001_initial、002_roleplay、003_reliability、004_identity、
005_learner_insights、006_training_experience、007_supervisor_growth、
008_custom_patient_profile、009_recommendation_scenario。
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

健康检查：浏览器访问 http://127.0.0.1:8080/api/health
应看到 database:true、workerRunning:true。

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
  WECHAT_APP_ID/SECRET、精确 ALLOWED_ORIGIN、REQUIRE_HTTPS=true、
  ALLOW_RUNTIME_API_KEY=false。
