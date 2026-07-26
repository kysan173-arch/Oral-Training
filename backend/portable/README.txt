口腔客服智能陪练 MVP 后端（Windows x64 免编译版）
====================================================

这个压缩包已经包含编译后的 oral_training_backend.exe、Crow 代码、
PostgreSQL 客户端 DLL 和 Visual C++ 运行库，不需要安装 CMake、Visual Studio
或单独构建 Crow。

仍然需要：
1. Windows 10/11 x64。
2. 一台可访问的 PostgreSQL 数据库（推荐 PostgreSQL 18）。
3. 一个有效的 DeepSeek API Key。

首次使用：
1. 在 PostgreSQL 中创建用户 oral_training_app 和数据库 oral_training。
2. 使用 psql 依次执行 migrations\001_initial.sql 和 migrations\002_roleplay.sql。
3. 双击 start-backend.cmd。首次运行会生成 backend.env 并提示退出。
4. 用记事本打开 backend.env，填写数据库密码和 DeepSeek API Key。
5. 再次双击 start-backend.cmd。
6. 浏览器访问 http://127.0.0.1:8080/api/health 检查服务。

数据库初始化示例（PowerShell）：
$psql = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
& $psql -U postgres -c "CREATE ROLE oral_training_app LOGIN PASSWORD '替换为密码';"
& $psql -U postgres -c "CREATE DATABASE oral_training OWNER oral_training_app;"
& $psql -h 127.0.0.1 -U oral_training_app -d oral_training -f '.\migrations\001_initial.sql'
& $psql -h 127.0.0.1 -U oral_training_app -d oral_training -f '.\migrations\002_roleplay.sql'

安全提示：
- backend.env 包含真实密钥和密码，不要发送给他人或提交到 Git。
- 生产环境请保持 ALLOW_RUNTIME_API_KEY=false，并通过 HTTPS 反向代理暴露 API。
- 本系统仅用于模拟训练，不构成医疗建议。
