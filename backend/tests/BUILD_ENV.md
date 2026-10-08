# Windows 本机构建与测试前置

本机（Zhangjie 的开发机）与 `AGENTS.md` 写的环境有出入，直接照抄那里的命令会失败。
这份文件记录实际可用的路径和两个必须的环境变量修复。

## 工具链实际位置

`cmake` / `ctest` **不在 PATH**，且 Visual Studio 不是 2022 而是 **18 (2026) Community**
（生成器 `Visual Studio 18 2026`，MSVC 14.44）。要显式用绝对路径：

```
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe
```

PostgreSQL 在 `D:\PostgreSQL`（`CMakeCache.txt` 里的 `PostgreSQL_INCLUDE_DIR`）。

## 两个必须修的环境问题

### 1. proxy 变量大小写成对 → MSBuild 派生子进程直接抛异常

本机同时存在 `HTTP_PROXY` 与 `http_proxy`、`HTTPS_PROXY` 与 `https_proxy`。
.NET 的 `ProcessStartInfo` 用**大小写不敏感**的 Hashtable 复制环境变量，
MSBuild 启动 `cl.exe` / `link.exe` 时抛：

```
System.ArgumentException: 已添加项。字典中的关键字:"HTTP_PROXY"  所添加的关键字:"http_proxy"
  在 System.Diagnostics.ProcessStartInfo.get_EnvironmentVariables()
```

表现为**每个目标都报 `error MSB6001`**，源码其实一行都没编。判据：
**全 `MSB6001` / 零 `error Cxxxx` = 工具起不来，不是代码写错。**

清理进程内的小写副本，并禁用 MSBuild 节点复用（否则旧 worker 仍持旧环境）：

```powershell
foreach ($n in @('http_proxy','https_proxy','ftp_proxy','all_proxy')) {
  [Environment]::SetEnvironmentVariable($n, $null, 'Process')
}
$env:MSBUILDDISABLENODEREUSE = '1'
```

### 2. `D:\PostgreSQL\bin` 不在 PATH → 链接 pqxx 的目标 0xc0000135

CTest 里链接 pqxx 的目标（`litellm_settings`、`database_feature`、
`patient_initialization`、`audit_regression`、`scenario_draft`、`rag_retriever`）
需要 `libpq.dll` 及其同目录依赖（`icu*`、`libcrypto-3-x64`、`libcurl` 等）。
缺 DLL 时退出码是 `0xc0000135`（`STATUS_DLL_NOT_FOUND`），
**一个断言都没跑到**，别误判成测试失败：

```powershell
$env:PATH = 'D:\PostgreSQL\bin;' + $env:PATH
```

## 完整构建测试流程

```powershell
# 记得先切到仓库根目录
Set-Location 'C:\Users\Zhangjie\Desktop\小程序'

foreach ($n in @('http_proxy','https_proxy','ftp_proxy','all_proxy')) {
  [Environment]::SetEnvironmentVariable($n, $null, 'Process')
}
$env:MSBUILDDISABLENODEREUSE = '1'
$env:PATH = 'D:\PostgreSQL\bin;' + $env:PATH

$bin = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
& "$bin\cmake.exe" --build backend\build-msvc --config Release
& "$bin\ctest.exe" --test-dir backend\build-msvc -C Release --output-on-failure
& powershell -NoProfile -ExecutionPolicy Bypass -File backend\tests\static_checks.ps1
```

预期：CTest **18/18 通过**，其中 4 个专用数据库测试
（`database_feature`、`patient_initialization`、`audit_regression`、`scenario_draft`）
在普通运行中 `Skipped`，需独立临时 PostgreSQL 才跑，属正常。

## 写构建脚本的坑

**包装脚本必须纯 ASCII。** `.cmd` 的 `REM` 注释和 `.ps1` 的注释里出现中文或 `─` 之类
符号会导致解析崩溃 —— cmd 会把注释行当命令执行并刷一堆
`'xxx' 不是内部或外部命令`；PowerShell 5.1 读无 BOM 的 UTF-8 按 GBK 解码，
中文字节会让引号失配报 `字符串缺少终止符`。脚本里也不要写中文路径，
先 `Set-Location` 再执行。
