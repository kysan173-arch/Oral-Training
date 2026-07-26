# Oral Training Backend

This service implements the MVP contract in `../docs/api.md`: Crow HTTP routes, PostgreSQL persistence, a DeepSeek patient/score gateway, and an independent patient-simulation gateway that returns standard customer-service answers and learning recaps.

## Local setup

1. Use `.env.example` as a reference and set the values in your shell. The executable reads environment variables directly; it does not load `.env` by itself.
2. Start PostgreSQL and create the database named `oral_training`.
3. Apply the migrations in order:

   ```powershell
   & 'C:\Program Files\PostgreSQL\18\bin\psql.exe' -h 127.0.0.1 -p 5432 -U oral_training_app -d oral_training -f migrations\001_initial.sql
   & 'C:\Program Files\PostgreSQL\18\bin\psql.exe' -h 127.0.0.1 -p 5432 -U oral_training_app -d oral_training -f migrations\002_roleplay.sql
   ```

4. Configure and build:

   ```powershell
   cmake -S . -B build-msvc -G 'Visual Studio 17 2022' -A x64
   cmake --build build-msvc --config Release
   ```

5. Run with `DEEPSEEK_API_KEY` set. The API is available at `http://127.0.0.1:8080/api` by default. The default model is `deepseek-v4-flash`; override it with `DEEPSEEK_MODEL` when needed.

   ```powershell
   $env:DATABASE_URL='postgresql://<user>:<password>@127.0.0.1:5432/oral_training'
   $env:DEEPSEEK_API_KEY='<DeepSeek API Key>'
   $env:PATH='C:\Program Files\PostgreSQL\18\bin;' + $env:PATH
   .\build-msvc\Release\oral_training_backend.exe
   ```

For local demonstration only, `POST /api/config/deepseek-key` accepts a key supplied by the Mini Program and holds it in process memory. Disable this endpoint in deployed environments with `ALLOW_RUNTIME_API_KEY=false` and provide `DEEPSEEK_API_KEY` through the server environment.

## Device preview and deployment

The Mini Program resolves its API URL through `utils/config.js`. Development defaults to loopback. For trial or release builds, set an HTTPS `apiBaseUrl` through Mini Program ext config or fill the matching environment entry in that file. Add the HTTPS host to the Mini Program request-domain allowlist.

The backend listens on loopback by default. For LAN testing or when running behind an HTTPS reverse proxy, set `BIND_ADDRESS=0.0.0.0`. Keep `ALLOW_RUNTIME_API_KEY=false` outside local development and terminate TLS at the reverse proxy.

## Smoke tests

With the backend running, verify database and public API behavior without calling a model:

```powershell
.\tests\smoke.ps1
```

After configuring a valid DeepSeek key, run the full patient-reply, scoring, and four-scenario patient-simulation flow:

```powershell
.\tests\smoke.ps1 -WithModel
```
