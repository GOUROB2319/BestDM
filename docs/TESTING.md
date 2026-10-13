# Testing and build verification

## Intended toolchain

Windows x64, Qt 6.12 MSVC 2022, C++20, Visual Studio C++ tools, and CMake. The app directly uses Qt Quick Controls and Qt Quick Dialogs QML modules alongside Quick, Network, SQL, and Widgets. On this machine Visual Studio/MSBuild is installed, but the Qt 6.12 kit (`rcc.exe`, `moc`, QML tools) is absent. `cmake`, `qmlformat`, and `qmllint` are not on PATH.

## Commands

Visual Studio project:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' BestDM.slnx /t:Build /p:Configuration=Debug /p:Platform=x64
```

CMake after installing Qt 6.12 MSVC 2022 and CMake:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH='C:/Qt/6.12.0/msvc2022_64' -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Verification log

- 2026-10-12: Baseline MSBuild failed in Qt resource generation because `/rcc` could not be started; C++ compilation did not begin.
- 2026-10-12: CMake configure could not start because `cmake` is not recognized on PATH.
- After implementation edits: `git diff --check` passed; QRC XML parses. Domain/persistence and local HTTP/queue tests were authored, but build, Qt Test execution, QML lint, and runtime verification are unavailable in this environment.
- `test_download_task` cleanup removes SQLite main/journal/WAL/SHM files after each case and resets the saved default destination; this test isolation change is source-reviewed only.
- The diagnostic logger/export suite is wired into CTest; its redaction, rotation, handler, and selected-file export cases are authored but uncompiled/unexecuted.

## Required tests

- State transitions, role exposure (including percentage, average speed, and category), search/category-filter correctness after changing source models with different numeric role IDs, and count accuracy.
- SQLite migration from the original schema, upsert, restore-all, history preservation, category and active-duration persistence, and error handling.
- Local HTTP server: full response, known/unknown size, same-scheme and downgrade redirects, redirect loops and chains beyond the 10-hop cap with a specific safe error, valid/ignored/malformed Range, a valid capped 206 followed by continuation from the next offset, changed ETag/Last-Modified/size, 416, 401/403/404/408/429/5xx, early disconnect and timeout.
- Batch limit: verify the default 100-link cap, persisted custom limits at both UI and backend validation, and rejection without creating any task when a submitted batch exceeds the configured bound.
- HTTP origin authentication: Basic challenge success and rejection/one-reprompt behavior on single-stream are covered by `httpBasicAuthenticationRetriesWithSessionCredential` and `httpBasicAuthenticationRejectsBadCredentialWithoutLooping`; still test Digest and segmented challenges, origin changes, prompt cancellation, and ensure credentials never appear in SQLite, QSettings, errors, or logs.
- Metadata probing: asynchronous HEAD response capture, safe Content-Disposition parsing, explicit filename preservation, same- and changed-extension category/folder updates, disk/queue collision reservation, server rejection/fallback to GET, pause/resume and cancellation during probing, and probe-intent persistence across migration/restart.
- Proxy networking: direct/system/manual selection, local HTTP proxy routing, asynchronous test results, 407 handling, credential rejection, and no secrets in UI/error text.
- File handling: Rename preserves existing output, Ask blocks enqueue until the conflict is reviewed and offers Rename/Overwrite/Cancel, Overwrite keeps old data until successful transfer then atomically replaces it, collision preference survives restart, active/history name reservations, Windows reserved names, permissions, write/flush/rename failure, cancel cleanup and pause retention.
- Filesystem links: regular-file no-follow helper supports append/truncate/read; direct no-follow opens and transfer setup reject existing symlinks at partial/segment paths without changing their targets; also test destination-directory reparse points and path replacement between close and publication.
- Collision review treats symlinked output entries as existing targets, including when evaluating Ask policy; dangling-link collision handling should also be verified on supported platforms.
- Final-name collision: create the reserved target while a single-stream transfer is running, and separately during segmented transfer; completion must select a suffix, preserve the competing file byte-for-byte, publish the full download, and update the displayed filename. Also verify publication fails closed on a platform without a no-replace primitive.
- Low disk space: known-size single-stream preflight, known-size segmented preflight including merge space, free-space exhaustion during concurrent transfers, unknown-size write failure, flush failure, and retained/retryable partial state.
- Cancel actions: delete or retain partial data, update retained byte count, resume a canceled partial when validators permit, and active-task removal cleanup.
- Remove actions: completed-file retention/deletion, active-task abort with partial retention/deletion, row removal only after requested cleanup, and cleanup failure retaining a history item with the blocked path reported.
- Queue: concurrency ceiling, stable ordering, pause-slot release, cancellation during retry backoff, configurable retry bounds, persisted retry countdown, 429/503 `Retry-After` deadlines, and restart recovery.
- Startup recovery: restart with persisted Retrying before/after its deadline; verify a single retry starts and cancellation prevents it; reconcile a Paused task after its `.part`/segment data is deleted; restore queued tasks with a deleted destination, a destination path replaced by a file, and a destination that cannot be created or written; confirm a visible Failed reason and successful retry after choosing a valid folder.
- Segments: bounded per-download connection count; non-overlapping ranges; strict 206, Content-Range, validator and length checks; resumed partial segments; 200/416/malformed response fallback; transient retry isolation; pause/cancel/delete/restart recovery; ordered merge and final-size verification; shared limiter aggregate rate.
- Scheduler: disabled/selected-day boundaries, overnight windows, equal-time full-day windows, queued waiting, active pause/resume, and persisted settings.
- Bandwidth limiter: zero/no-limit mode, bounded token grants, updated limit behavior, fair per-task shares, and aggregate throughput across two simultaneous local HTTP tasks.
- Category rules and folders: extension and host matches, subdomain boundary, first-rule precedence, invalid-update preservation, rule-count bounds, safe folder-name handling, category persistence, and proxy filtering.
- UI: batch URL flow, All/Active/Queued/Paused/Completed/Failed/History navigation and per-queue counts, search, settings persistence, keyboard/focus, min-window sizing, display scaling, and dark/light/system themes.
- Queue-scoped retry and shortcuts: with failed tasks in two queues, verify Retry failed counts/retries only the selected queue; Ctrl+N opens Add, Ctrl+F focuses/selects Search, and Ctrl+, opens Settings without enqueueing work.
- URL drag-and-drop: drop one and multiple HTTP/HTTPS URLs over the window; verify the Add dialog opens with each URL, invalid schemes appear in the ordinary review, the configured batch bound remains enforced, no download starts before explicit Add, and dragging out clears the visual drop cue.
- Post-download action: None by default; persisted open-file/open-folder/custom-program choices run once on successful live completion, do not run for restored history or failure/cancel; verify safe single-argument file-path passing, program-not-found/nonzero-exit handling, output draining, timeout/termination, and app shutdown with an active child.
- Microsoft Defender: opt-in and Windows-only; locate current platform and legacy `MpCmdRun.exe` paths, pass a file path containing spaces safely, verify scan exits zero before post-action, nonzero/start failure prevents the action, standard-user permission failure is surfaced (Microsoft documents elevated command prompt use), 30-minute timeout, cancellation on task removal/app shutdown, visible failure dialog/tray message, and disabled preference across restart.
- Queue power actions: None by default and unavailable off Windows; old completed history cannot trigger the action; prompt only after the enabled session has a successful completion and no nonterminal tasks; explicit confirmation starts the 60-second countdown; cancel/new task/pause/retry prevents the action; verify shutdown process launch failure, sleep/hibernate API failure, app responsiveness during suspend, and user cancellation.
- Tray and notifications: tray availability gating; open/quit actions; restore after minimize/close; disabled and enabled filename-only notifications; final failures only after retries exhaust; normal close when the tray is unavailable.
- Custom protocol/single instance: register and unregister per user; preserve another application's pre-existing handler; startup link and secondary-process forwarding; malformed actions, duplicate/unknown parameters, userinfo, fragments, overlong input, non-HTTP(S), invalid local-socket JSON, and oversized frames beyond the bounded read buffer are rejected; accepted links restore/focus the window and populate Add without enqueueing; user-only socket access and stale endpoint recovery.
- Clipboard privacy: monitoring is disabled by default; when enabled, copying one HTTP/HTTPS URL prompts without queueing; Ignore and Review link behave correctly; copied destination paths and multi-line clipboard text do not prompt; copying a URL from BestDM does not prompt itself.
- Default destination: accept an existing writable folder, reject missing/non-directory paths, persist across manager recreation, and allow per-batch override.
- Per-task destination changes: reject active/probing/retrying and invalid paths; move completed outputs and inactive partial/segmented tasks on the same and different volumes; choose a collision-free name; rollback all earlier moves after a simulated later move failure; preserve resume data and disable category subfolder rerouting for that task.
- Multi-selection: keep selected tasks across filter/search changes, prune removed ids, and apply bulk pause/resume/cancel/remove to valid selected ids.
- Named queues: create unique validated names; reject duplicates/invalid labels; select a queue and ensure list/counters/clear-history scope match; new tasks use the selected queue; move queued, active, failed, and completed selections; assignment and empty queues survive restart; migrate v10 records to Default without losing task data.
- FIFO recovery: enqueue several tasks in a known order across queues, restart while they are queued, and confirm they start in original enqueue order even though the restored history list is newest-first; verify retry timers with equal deadlines preserve creation order.
- Clear history: action is visible only for nonempty History; completed/failed/canceled rows are targeted while active/paused rows are preserved; remove records while keeping files or delete completed/partial/segment files; failed cleanup retains that record and reports the failure.
- Persistence diagnostics: database open/migration failure, restore query and row-iteration error, task save/remove failure, corrupted DB, and disk-full failure produce a visible actionable error without silently presenting failed restore as an empty history.
- Diagnostic log/export: install/uninstall the global Qt handler; remove URL userinfo, query, and fragment values; redact authorization/cookie/password/token fields and the user home path; flatten multiline messages; rotate at the configured byte cap and retain only the configured backups; verify explicit export writes only redacted logs to a selected local `.txt` path and rejects non-local/non-text targets; do not emit raw messages to the previous console handler.
- Batch intake rejects more than 100 newline-separated URLs without adding any tasks.
- URL-list import accepts UTF-8 text with an optional BOM, rejects invalid UTF-8/empty/non-local/non-text inputs, and rejects files larger than 1 MiB.
