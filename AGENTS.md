# Repository Guidelines

## Project Structure & Module Organization

This is a WeChat Mini Program MVP for dental customer-service training. Application-wide setup lives in `app.js`, `app.json`, and `app.wxss`. Feature pages are organized under `pages/<feature>/` and each page normally contains a matching `.js`, `.json`, `.wxml`, and `.wxss` file. Current flows include `home`, `index`, `training`, `result`, `report`, and `admin`.

Put reusable client-side logic in `utils/`; `utils/mvp.js` owns the local scenarios, session persistence, reply simulation, and evaluation rules. Store tab icons and other runtime assets in `static/`. Product requirements and API notes belong in `docs/`.

## Build, Test, and Development Commands

There is no npm package or command-line build pipeline. Open the repository directory in WeChat DevTools as a Mini Program project, then use:

- **Compile** to build and run the simulator.
- **Preview** to test on a device through a generated preview build.
- **Upload** only after manually validating the MVP flow.

`project.config.json` defines the shared DevTools settings; keep `project.private.config.json` limited to developer-specific settings. Before committing, review `git diff --check` and inspect the changed flow in the simulator.

## Coding Style & Naming Conventions

Use two-space indentation, single quotes, semicolons, and ES6 syntax consistent with existing files. Name page folders and files in lowercase (for example, `pages/training/training.js`). Use `camelCase` for JavaScript variables/functions, `PascalCase` only for constructors, and descriptive `kebab-case` for WXML class names. Keep page behavior in its page controller and extract shared, storage-related, or rule-based logic to `utils/`.

## Testing Guidelines

No automated test framework is configured. Manually test the affected user journey in WeChat DevTools: start a scenario, send messages, finish training, view the result, and confirm report/admin history reflects saved data. For changes to `utils/mvp.js`, cover normal, empty-storage, resumed-session, and unsafe-response cases. Add automated tests alongside new tooling when practical.

## Commit & Pull Request Guidelines

Follow the existing Conventional Commit style: `feat: ...`, `fix: ...`, `refactor: ...`, or `docs: ...`. Keep each commit focused. Pull requests should explain the user-visible change, list manual validation performed, link related requirements/issues, and include screenshots or a short recording for WXML/WXSS UI changes. Do not commit credentials, generated local state, or unrelated DevTools configuration changes.
