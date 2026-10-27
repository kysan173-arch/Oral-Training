# Repository Guidelines

## Architecture and directories

This repository contains a WeChat Mini Program and a Windows C++ backend for dental customer-service training.

- `app.*`, `pages/`, `utils/`, `static/`: Mini Program client.
- `backend/src/main.cpp`: Crow routes, DeepSeek gateway and same-process Worker orchestration.
- `backend/src/reliable_store.h`: transactional message leases, user-scoped persistence and PostgreSQL AI jobs.
- `backend/src/identity.h`: WeChat/demo login, bearer sessions, roles and rate limits.
- `backend/migrations/`: ordered, additive PostgreSQL migrations. Never edit a migration that has already shipped; add the next numbered file.
- `backend/tests/`: CTest validation, static checks, migration/database tests and controlled smoke tests.
- `docs/architecture/api.md`: current public API contract.

DeepSeek request URL, request parameters, prompts, response parsing and the existing model-call retry loop are compatibility-sensitive. Do not change them unless a task explicitly requires a model contract change.

## Build and validation

Build the backend on Windows with Visual Studio 2022 and PostgreSQL client libraries:

```powershell
cmake -S backend -B backend\build-msvc -G 'Visual Studio 17 2022' -A x64
cmake --build backend\build-msvc --config Release
ctest --test-dir backend\build-msvc -C Release --output-on-failure
```

Run client syntax/JSON checks with `backend/tests/static_checks.ps1`. Use `backend/tests/smoke.ps1` without `-WithModel` for the normal API smoke. A real-model smoke is controlled, run only after all offline checks pass, and should not be repeated casually.

Open the repository root in WeChat DevTools for simulator/device validation. Before committing, run `git diff --check` and manually cover both training modes, resume, history and result polling.

## Default generation model

User preference recorded on 2026-10-27: use `MiniMax-M3.1-Flash` for future generation-model configuration, development and validation instead of `MiniMax-M2.7`. The official OpenAI-compatible base URL is `https://api.minimax.cn/v1`; use the user's valid M Plan subscription key without exposing it. This preference does not replace EmbeddingGemma or explicitly configured fine-tuned models. Before declaring an existing deployment migrated, verify its saved model configuration, thinking/output-token limits, timeouts and structured report output. The existing MiniMax primary account configuration and probe defaults were updated to this model on 2026-10-27. Basic API availability was verified; historical M2.7 validation reports are not M3.1 validation evidence.

## Code conventions

Use two-space indentation, single quotes and semicolons in JavaScript. Use existing C++ formatting (two-space indentation, RAII, short transactions). Page folders/files are lowercase; JavaScript uses `camelCase`; WXML classes use descriptive `kebab-case`.

Keep model network calls outside database transactions. Any final-round message, session completion, report state transition and job enqueue must remain atomic. Do not reintroduce detached threads or trust client-supplied user IDs.

## Data and migrations

Preserve user data and auditability. Before destructive repair, archive complete source rows. Test migrations against an empty database and representative history, including duplicates, failed/pending generation, and rerunning the migration. Never run test cleanup against a database whose exact test scope has not been verified.

Production is currently one institution with `learner` and `admin`; multi-tenant behavior, rankings and team operations remain out of scope. Learner records are user-isolated and admins receive aggregates only.

## Commits and security

Use focused Conventional Commits (`feat:`, `fix:`, `refactor:`, `docs:`). Do not commit credentials, bearer tokens, `backend.env`, generated build output, local DevTools state or unrelated files. Production requires HTTPS through a reverse proxy, exact CORS, disabled runtime-key upload, request IDs and structured logs.
