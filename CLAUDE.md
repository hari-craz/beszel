# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A fork of **Beszel** (upstream `github.com/henrygd/beszel`), a lightweight server-monitoring platform (Go hub + agents, PocketBase-backed, React web UI). This fork adds an **ESP32 hardware-recovery subsystem**: physical relay watchdogs that reboot unresponsive servers, coordinated with the hub but able to act autonomously when the hub is unreachable.

Three codebases live here:
- **Hub** — Go web app built on PocketBase. Entry `internal/cmd/hub`; logic in `internal/hub`. Embeds the built React UI (`internal/site/embed.go`, `//go:embed all:dist`).
- **Agent** — Go binary that runs on each monitored machine and reports metrics. Entry `internal/cmd/agent`; logic in `agent/`.
- **Frontend** — React 19 + Vite + TypeScript in `internal/site`.
- **Firmware** — a single ESP32 Arduino sketch: `firmware/esp32_recovery/esp32_recovery.ino`.

## Commands

Canonical build/test go through the `Makefile` (assumes a local Go toolchain):
- `make build` — builds agent + hub into `./build`. `make build-hub` / `make build-agent` individually. `make build-hub` builds the web UI first unless `SKIP_WEB=true`.
- `make test` — runs the Go suite. **All Go tests require `-tags=testing`** (helpers in `internal/tests` and `agent` are behind that tag); a bare `go test ./...` fails to compile.
- `make lint` — `golangci-lint run`.
- `make dev` — runs dev-server (Vite), dev-hub (`go run -tags development`, serves on `:8090`), and dev-agent together.

Single Go test: `go test -tags=testing ./internal/hub -run TestApiRoutesAuthentication`.

Frontend (`cd internal/site`, npm or bun):
- `npm run dev` — Vite dev server (no API proxy; needs a hub on the same origin, so usually run via `make dev` or against the built UI served by the hub).
- `npm run build` — **`lingui extract --overwrite && lingui compile && vite build`**. The build regenerates locale catalogs (`src/locales/**/*.po` + compiled `.ts`); expect those `.po` files to show up in `git status` after any build.
- `npm run check` / `npm run lint` — Biome. `npm run check:fix` to auto-fix.
- `npx tsc --noEmit -p .` — type-check only.

Firmware: compile with `arduino-cli` (FQBN `esp32:esp32:esp32`, esp32 core 3.x). There is no CI or unit test for it — it is verified by compiling and manual read-through.

## This dev machine (important)

- **No local Go toolchain is installed.** Run Go commands inside a container:
  `MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd)":/app -w /app golang:alpine sh -c "go test -tags=testing ./..."`. The `MSYS_NO_PATHCONV=1` prefix (Git Bash) stops path mangling of `/app`.
- **The primary shell is PowerShell**; a Bash tool is also available. Line endings are CRLF on checkout — the `LF will be replaced by CRLF` git warnings are expected and harmless.
- **arduino-cli** ships with the Arduino IDE: `"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"`, config at `~/.arduinoIDE/arduino-cli.yaml`. Compile: `arduino-cli compile --config-file <cfg> --fqbn esp32:esp32:esp32 firmware/esp32_recovery`.
- **Docker deploy / hub testing**: `docker compose build beszel && docker compose up -d beszel` runs the hub on `:8090`. `docker-compose.yml` is **hub-only by design** (deploying it never gets blocked by the agent's required `TOKEN`/`KEY` env vars); the agent lives in `docker-compose.agent.yml`. Create a superuser for testing with `docker compose exec beszel /beszel superuser upsert <email> <pass>`.

## Architecture notes that span multiple files

**PocketBase migrations (`internal/migrations`)** are Go files registered via `m.Register(up, down)`, run once, in filename order (`<unixts>_<desc>.go`). **A migration only runs once per database** — editing an already-applied migration file in place does *not* change existing DBs. To change schema on already-migrated collections, add a *new* timestamped migration (see `1784310000_*` / `1784320000_*`, which backfill fields that earlier in-place edits to `1712000000_recovery_init.go` never applied). When defining collections by JSON, the `id` text field needs `autogeneratePattern`/`min`/`max` or record creation fails with "Cannot be blank".

**Hub API** lives in `internal/hub/api.go`. Routes register under two groups: `apiAuth` (`/api/beszel`, requires auth) and `apiNoAuth`. Recovery endpoints are `/api/beszel/recovery/*` — note `POST /recovery/ping` is on `apiNoAuth` (the ESP posts here unauthenticated); the rest require an authenticated user (not admin — admin is enforced client-side and via collection rules).

**Recovery config sync** is **revision-number-driven, not hash-driven** (Go and the firmware deliberately don't share a hash algorithm). Each module has desired (`config_revision`/`config_hash`) vs reported (`reported_config_*`) state; `computeRecoverySyncStatus` derives `SYNCED`/`SYNC_PENDING`/`OFFLINE_PENDING`/`CONFLICT`/`SYNC_ERROR`. Any UI edit must bump `config_revision` or the ESP never re-syncs. Several booleans use an **inverted-storage convention** (`hardware_recovery_disabled`, `temperature_monitoring_disabled`, `buzzer_disabled`) so zero-value = enabled, preserving backward compatibility. Module liveness comes from a dedicated `last_ping` timestamp, not the `updated` autodate (which any edit bumps) — see `isRecoveryModuleOnline`.

**Dual-engine safety**: the hub must never directly fire an ESP relay. The hub-side watchdog (`internal/hub/systems/recovery_prober.go`) only sends WOL and uses a lease-based lock (`expirymap`) to serialize its own actions; physical relay recovery is owned by the ESP's independent state machine. Reachability checks use ICMP ping (with a TCP fallback) on both sides, not TCP port probes.

**Frontend state** is nanostores. Atoms/maps are declared in `src/lib/stores.ts`; per-collection managers (`systemsManager.ts`, `alerts.ts`, `recoveryManager.ts`) load initial data (`getFullList`) and subscribe to PocketBase realtime, wired together in `src/main.tsx`. The systems table cells read from these stores (e.g. the Recovery column joins `$recoveryChannels`/`$recoveryModules`). i18n is Lingui: wrap user-visible strings in the `t` macro or `<Trans>`; `npm run build` extracts them.

**ESP firmware** (`esp32_recovery.ino`) is one file: an onboarding AP + local settings web portal, a per-channel autonomous recovery state machine, DS18B20 temperature, buzzer, and I2C LCD. Config (Wi-Fi, module settings, **channel table**, Telegram token) persists to NVS via `Preferences` and reloads on boot. It syncs with the hub by revision number and alerts the user directly over Telegram on incidents (independent of the hub). Relay GPIO map is `RELAY_PINS = {18,19,25,26,27,32}` (channel N → `RELAY_PINS[N-1]`), mirrored as `RELAY_GPIO_PINS` in `recovery-modules.tsx`.

## Verifying changes

- Go: `go vet -tags=testing ./...` + `go test -tags=testing ./...` (containerized on this machine). Two pre-existing failures unrelated to recovery work — `agent.TestTestDataDirs` and a nil-pointer panic in `internal/alerts` — are known and not caused by recovery changes.
- Frontend: `npx tsc --noEmit`, `npx biome check <files>`, `npm run build`.
- Live UI: rebuild the hub image, run it, and drive the browser against `:8090` with a throwaway superuser/admin user — **delete temp accounts and test records afterward; the local hub runs against real data.**
