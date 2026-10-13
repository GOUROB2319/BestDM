# BestDM — Master Engineering Prompt

**Goal:** Evolve the existing BestDM repository into a reliable, polished, feature-complete Qt 6 desktop download manager, using FastDM as the verified feature reference.  
**Primary stack:** Qt 6.12 LTS, C++20, Qt Quick/QML, CMake, Qt Network, Qt SQL/SQLite.  
**Primary platform:** Windows x64. Keep the core portable so Linux and macOS can be supported where practical; document platform-specific limitations honestly.  
**User communication:** Explain progress, important decisions, blockers, and final verification in natural Bangla. Source code, identifiers, logs, and technical documentation should use clear English unless a UI localization requires otherwise.

---

## 0. Your role and non-negotiable working rules

Act as a senior C++/Qt engineer, software architect, UI engineer, security reviewer, and test engineer. Work directly in the existing `GOUROB2319/BestDM` repository and turn it into a real application—not a static design mockup.

1. **Inspect before changing.** Examine the complete working tree, Git status, current branch, existing CMake/Visual Studio projects, source, tests, dependencies, and build scripts. Build and run the current project first when the environment allows it. Record the baseline and existing user changes.
2. **Protect existing work.** Never run destructive Git commands such as `reset --hard`, `clean -xfd`, forced checkout, or force-push. Do not discard, overwrite, or reformat unrelated user changes. Do not push to GitHub or publish a release unless explicitly instructed. Create a feature branch only if it is safe and will not hide or disturb current work.
3. **Use BestDM as the product and project foundation.** Preserve its name and existing supplied download-manager icon. Refactor the current code incrementally. Do not replace the repository with a new unrelated scaffold, and do not copy C# code mechanically from FastDM.
4. **Treat FastDM as a feature/behavior reference, not a code dependency.** Reimplement behavior in native C++/Qt with clear responsibilities. Do not add a .NET runtime or turn BestDM into a C# wrapper.
5. **No fake features.** Every visible button, menu, navigation item, preference, queue action, and integration must perform a real operation or be clearly disabled with a reason. Never use hard-coded fake download progress, mock data in production views, TODO-only handlers, or “completed” labels for unimplemented work.
6. **No plan-only response.** After a brief audit and implementation plan, make code changes, build, test, and fix defects. Complete the highest-value vertical slices first, then continue through the roadmap. If a context/session limit prevents completion, leave a precise checkpoint in `docs/IMPLEMENTATION_STATUS.md` and continue from it rather than claiming the app is finished.
7. **Keep the UI responsive.** No blocking network calls, long process waits, recursive scanning, or large file operations on the UI thread. Network transfers must be asynchronous; external tools use `QProcess` with timeout, cancellation, bounded output, and clean shutdown.
8. **Test claims.** Run the relevant build and tests after each major phase. Do not claim a build, test, platform, or feature succeeded unless it was actually verified. If a dependency or platform is unavailable, document the exact blocker and the tests still needed.
9. **Make sensible decisions without repeatedly asking the user.** If a detail is unspecified, choose a safe, maintainable default, document it in `docs/DECISIONS.md`, and keep implementing. Ask a question only when a destructive or product-defining decision genuinely cannot be made safely.
10. **Keep a living engineering record.** Maintain `docs/FEATURE_MATRIX.md`, `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/TESTING.md`, and `docs/IMPLEMENTATION_STATUS.md`. Mark features as `Implemented`, `Partial`, `Planned`, or `Blocked`, and include evidence or test names where possible.

## 1. Verified repository context — use this as the starting map

### FastDM reference repository

Repository: `https://github.com/GOUROB2319/FastDM` (C#/.NET Windows desktop app plus Chromium Manifest V3 extension).

The source has the following major responsibility areas:

- `FastDM/Form1.cs`: core models/settings, primary WinForms UI, standard HTTP download engine, task controls, state save/restore, tray/single-instance behavior, clipboard URL detection, local bridge host integration, and application orchestration. This file is large and should be used to identify behavior—not copied wholesale.
- `FastDM/StreamEngine.cs`: HLS/DASH stream downloads, segments, track processing, and merge/fallback behavior.
- `FastDM/MediaSupport.cs` and `FastDM/MediaForms.cs`: media URL detection/probing, HLS/DASH manifest parsing, quality/audio selection, media candidate selection, and ffmpeg discovery/use.
- `FastDM/YtDlpSupport.cs` and `FastDM/YtDlpForms.cs`: yt-dlp-based source resolution, quality selection, audio-only downloads, progress/error parsing, runtime discovery and update handling. The current supported websites depend on yt-dlp and the tool version; do not hard-code an inaccurate site list.
- `FastDM/RemoteSupport.cs`, `FastDM/NetworkSupport.cs`: HTTP/FTP/SFTP operations, remote directory traversal, credentials, proxy behavior, request authentication, speed limiting, and network utilities.
- `FastDM/FolderDownloadForm.cs` and `FastDM/PlaylistSupport.cs`: recursive remote folder scanning, selection/filtering, batch downloads, natural sorting, and M3U playlist output. Stream playlists must not leak credentials.
- `FastDM/SchedulerSupport.cs`: day/time scheduler and post-download power actions with confirmation/countdown.
- `FastDM/PreferencesSupport.cs`, `FastDM/PreferencesForm.cs`, `FastDM/AppTheme.cs`: settings, categories, logging, post-download actions, theme, and preferences UI.
- `FastDM/Bridge/BridgeServer.cs`, `BridgeAuth.cs`, `BridgeModels.cs`, and `ProtocolHandler.cs`: loopback browser bridge, pairing and token validation, URL validation, `fastdm://` handling, and named-pipe/single-instance command routing.
- `FastDM/UpdateChecker.cs`: release/update checks and Store-aware behavior.
- `FastDM.Extension/`: browser extension source, popup, options, background bridge, context menus, cookie handling, localizations, and tests.

FastDM settings/behaviors to preserve where applicable include: default download folder; per-file connection count (default 8 in the reference); maximum concurrent downloads (default 3); system/light/dark theme; sidebar and compact/detail views; tray behavior; notifications; startup update checks; speed cap; system/manual proxy with HTTP/SOCKS5 options; scheduler days and start/end times; stop outside the permitted schedule; retries and retry limit; ignore webpage-only links; server timestamps option; file-category/host folder suggestions; automatic cleanup preferences; file-exists and delete-confirmation behavior; batch URL limit; completion notifications; antivirus/custom application after download; logging; browser integration enable/disable; and safe secret storage.

### Current BestDM repository

Repository: `https://github.com/GOUROB2319/BestDM`.

Existing files include `CMakeLists.txt`, `BestDM/main.cpp`, `BestDM/main.qml`, `BestDM/domain.h`, `downloadmanager.*`, `downloadqueue.*`, `downloadtask.*`, and `persistencerepository.*`, plus the supplied icon. The configured build currently uses Qt Quick, Qt Network, and Qt SQL, and the CMake entry point requests C++20.

The current implementation is only an initial vertical slice. It includes an HTTP/HTTPS single-download flow, streamed `.part` file writes, initial pause/resume/cancel logic, progress/speed/ETA properties, safe-ish unique-name handling, and an SQLite repository. There is code for `DownloadQueue` and `DownloadTask`, but the current `main.cpp` exposes only `DownloadManager` to QML; the queue is not wired into the actual app shell. The QML sidebar contains visual navigation items that are not yet full navigation controls. Repository restoration currently asks SQLite for only the latest non-completed/non-canceled record, not a complete multi-download collection. Some README details may not match the code, so verify claims against implementation.

Known areas requiring deliberate review:

- Validate actual HTTP response status and `Content-Range` before appending resumed/segmented bytes. A server may ignore the `Range` header and return `200`; this must never append a duplicate body to an existing partial file.
- `DownloadTask` and `DownloadManager` currently have different resume/fallback behavior; unify the behavior in one well-tested engine.
- Manual pause must not be treated as a normal failure that automatically retries.
- Check file open/write/flush/rename errors and preserve useful error information.
- Restore **all** unfinished tasks and show completed history from SQLite; do not restore just one task.
- Persist settings separately from download records; migrate the schema safely.
- Keep the current README honest: code using SQLite must not be described as using QSettings for downloads unless that is actually true. QSettings is appropriate for application preferences.
- Ensure that QML navigation, buttons, filters, menu items, and toolbar actions are genuinely connected to C++ state and behavior.
- Do not let URL inspection, webpage/media probing, or yt-dlp discovery make the user wait for a long period before a download can be queued. Add the item immediately; resolve metadata asynchronously, support cancellation and timeouts, and show clear progress/errors.

## 2. Product goal and design direction

Deliver **BestDM**, a professional desktop download manager inspired by the practical workflows of FDM/IDM-like tools, with the feature coverage of FastDM and a modern, clean Qt Quick interface. A new visual design is welcome; feature parity and reliability are mandatory.

Design principles:

- Modern native desktop feel, compact but readable layout, strong hierarchy, deliberate spacing, restrained brand colors, usable at 100–200% Windows display scaling.
- Reuse the supplied blue/aqua/green download icon and its visual language. Do not use emoji as core UI icons. Use a consistent SVG/icon set or Qt-supported vector assets.
- Provide System, Light, and Dark themes with accessible text/background contrast and persisted theme preference.
- Main window: navigation/sidebar, download table/list, status summary, global speed/activity, quick search/filter, queue status, clear empty state, and a primary “New Download” action.
- A download row must show a real file icon/category, filename, state, progress, transferred/total size, speed, ETA, destination or useful secondary info, and contextual actions where relevant.
- Provide practical details view and optional compact view. Support sorting, filtering by state/category, multi-selection, keyboard navigation, and sensible context menus.
- Keep UI state separate from download/business logic. QML renders and invokes model/service APIs; it must not implement network, filesystem, credentials, or process-management logic.
- Localization-ready UI. English can be the initial default, but centralize UI strings and avoid scattering text literals through complex components.

## 3. Required technical architecture

Inspect and adapt the existing project, then organize it into clear modules. Names may differ if the existing structure provides a strong reason, but responsibilities must remain separate.

### Core domain and state

- `DownloadRecord`/domain entity with a stable ID, URL, normalized source, filename, destination, category, creation/update/completion timestamps, transferred bytes, total size, speed, ETA, ETag/Last-Modified, retry count, error details, optional media metadata, and persisted state.
- Use a typed `enum class DownloadState`, not scattered state strings. At minimum: `Queued`, `Probing`, `WaitingForSchedule`, `Downloading`, `Paused`, `Verifying`, `Completed`, `Failed`, and `Canceled`.
- Define legal state transitions and centralize them. Prevent duplicate finish/pause/cancel callbacks and race conditions.
- Expose records through a real `QAbstractListModel` with explicit roles for QML. Keep filtering/sorting in a proxy model or a clearly tested model layer.
- `DownloadQueue` is responsible for maximum concurrency, ordering, priorities if implemented, and scheduling new tasks. It must be integrated with the main manager, persistence, and QML.
- `DownloadTask` represents one logical download, not an unrelated second download engine. It delegates to a selected protocol backend and emits consistent progress/state/error signals.

### Suggested modules

- **Application/UI:** app bootstrap, main window state, QML pages/components, theme manager, notifications, tray and platform adapters.
- **Download core:** download manager/facade, typed domain objects, task lifecycle, queue scheduler, category/path helpers, post-download workflow.
- **Network:** HTTP/HTTPS probe, direct download, robust resume, segmented downloader, global speed limiter, proxy setup, retry policy, cookie/header support where explicitly supplied.
- **Protocol backends:** interface-based HTTP, FTP and SFTP backend implementations. Use Qt Network for HTTP/HTTPS. Evaluate a maintained libcurl build (with the appropriate FTP/SFTP features enabled) for FTP/SFTP instead of attempting to recreate protocol handling ad hoc. Check dependency licenses, host-key verification, TLS configuration, and packaging before enabling a backend. Unsupported capabilities must be explicit, never simulated.
- **Media:** one media-resolution coordinator with direct-media, HLS/DASH, and yt-dlp backends; ffmpeg integration for stream remux/merge where needed. Avoid running separate overlapping implementations without a shared contract.
- **Data:** SQLite repository for every download and history item, schema migrations, application preferences via QSettings, and a platform credential-store abstraction.
- **Integration:** browser extension/local bridge, custom URL scheme on supported desktop platforms, clipboard monitoring, single-instance forwarding, app update checker.
- **Scheduling:** day/time window logic, pause/resume at schedule boundaries, after-completion actions, confirmation/countdown.
- **Testing:** Qt Test unit and integration tests; local fake HTTP server for reliable network tests; fake process/backend adapters for error paths.

### Qt/CMake rules

- Target Qt 6.12 LTS and C++20 initially, matching the current BestDM installation/build setup. Do not upgrade Qt versions or compilers just for preference.
- Modernize the CMake/QML setup with Qt’s current CMake helpers (prefer `qt_add_qml_module` for application QML), clear target dependencies, and explicit resource packaging. Avoid fragile manually duplicated QRC paths unless a deliberate reason exists.
- Keep CMake usable from the command line and Visual Studio. Use a build preset or short documented PowerShell commands with the Qt installation actually available. Do not assume a generator/toolset without checking.
- Use Qt signals/slots/properties correctly and respect QObject thread affinity. One `QNetworkAccessManager` per relevant thread is normally sufficient; do not call it from the wrong thread. For high-volume file writes, ensure disk activity cannot freeze the UI.
- Use RAII, clear ownership, `deleteLater()` for completed replies where appropriate, scoped connections where useful, and cancellation tokens/objects that prevent use-after-free.
- Use current Qt/QML idioms supported by Qt 6.12. Keep warnings enabled; fix new warnings instead of suppressing them globally.

## 4. Feature-complete requirements

Build features as real, testable vertical slices. Preserve these behaviors from FastDM and improve them where needed.

### A. Essential download manager — release blocking

1. Add one URL, paste a URL, add multiple newline-separated URLs (honor a configurable safe batch limit), drag/drop URLs if practical, and optionally import a list of URLs from a text file.
2. Validate and normalize schemes before creating tasks. Support HTTP/HTTPS first. Reject unsupported or malformed URLs with actionable messages; never silently switch to an unrelated site.
3. Probe metadata asynchronously: filename, MIME type, file size when known, server resume/range support, modification/ETag validators, and redirect target. Never make the UI wait for probing before creating a visible queue entry.
4. A stable queue/list with any number of records; maximum simultaneous tasks is configurable (default 3 reference value). Per-download connection count is configurable (default 8 reference value) but adapt down when the server does not support byte ranges.
5. Real progress, size, speed, ETA, state, timestamps, error detail and destination. Handle unknown total size and indeterminate progress.
6. Pause, resume, cancel, retry, retry-all-failed, remove from list, optionally delete partial/completed files with confirmation, open file, open containing folder, copy URL, copy destination, and show details.
7. Restart recovery for all unfinished tasks, with partial bytes validated and each task restored safely. Completed downloads remain visible in History unless the user deletes/removes them according to preferences.
8. Unique filename policy when a target already exists: Ask, Rename, or Overwrite according to user preference. Default to safe non-overwriting behavior. Never accidentally overwrite files.
9. Automatic retry with bounded attempts and exponential backoff for transient failures only. Do not retry manual pause/cancel, invalid URL, authentication refusal, permission failures, or permanent HTTP errors blindly.
10. Global speed limit shared across all active download tasks; changing the limit should take effect without restarting the app. Zero means unlimited.
11. Optional category-aware destination folders or host-based folder suggestions. Always show and allow the user to change the final path.
12. Optional clipboard monitoring with a setting and clear privacy expectation. Do not auto-download clipboard links without user consent; offer an add prompt instead.
13. Accurate state counters in sidebar/status bar: all, active, queued, completed, failed, paused, and history. Search/filter/sort/navigation must work.

### B. Reliable HTTP/HTTPS engine — release blocking

- Stream response bytes directly to a partial file; do not buffer large files in memory.
- Resume using `Range` and validators such as ETag/Last-Modified when available. Confirm status `206` and validate `Content-Range` before appending. If the server returns `200` to a range request, restart safely from byte zero. Handle `416` and changed server content by safely restarting or presenting a clear decision.
- For segmented downloads, create independently tracked segment files, validate every response and expected byte count, retry failed segments, and merge in order only when all required segments are valid. Remove/retain temporary pieces according to a recoverable policy.
- If a server does not support range requests or segmented download fails, fall back to a single-stream download safely.
- Handle redirects, TLS errors, authentication prompts, `401/403/404/429/5xx`, expired URLs, unavailable content length, connection reset, disk full, write/flush failure, and low disk space with useful messages.
- Respect server headers and avoid changing the meaning of signed URLs. Support a user agent and referer only when needed and obtained through an authorized workflow.
- Do not disable TLS certificate validation. Avoid unlimited redirects, response bodies, or retries.
- Keep progress and speed calculations monotonic and accurate through pause/resume and retry. Ensure a reply’s finished/error handler cannot run twice against a destroyed task.

### C. Remote and batch downloads

- FTP and SFTP single-file transfers and resume where the selected backend genuinely supports it.
- Auth prompts when credentials are required, optional save-credentials flow, and safe handling of passwords embedded in a URL (never display/store/log secrets unintentionally).
- For SFTP, verify SSH host keys according to a user-visible trust policy; do not silently trust every host.
- Remote directory browsing/scanning for supported HTTP directory listings, FTP, and SFTP. Use depth/entry/size bounds and cancellation. Do not follow loops indefinitely.
- Let users select subfolders/files, filter common media extensions, show counts/estimated size when known, choose concurrency and destination, then enqueue the selections.
- Natural-sort numbered filenames where useful. Generate stream `.m3u8`/M3U playlists and local downloaded-file playlists where supported; strip credentials and sensitive query values from exported links where possible.
- Provide clear capability differences when server directory listing is not available.

### D. Media download workflows

- Detect direct media links and common HLS (`.m3u8`) and DASH (`.mpd`) manifests. Parse available video/audio variants and show truthful quality, resolution, codec, language, and estimated-size information where the source provides it.
- Support separate audio/video tracks and merge/remux with ffmpeg when needed. Prefer stream copy/remux over unnecessary transcoding. Keep segment manifests, byte ranges, retries, and partial files recoverable.
- Use yt-dlp as a separate managed backend for websites it currently supports. Check executable/runtime availability, version, exit code, stderr, and format metadata. Do not promise support for a website solely because it appears in a hard-coded list.
- Offer video quality selection, best available, and audio-only options when yt-dlp/source metadata allows. Let users choose the output folder and clear filename.
- Run metadata detection asynchronously with cancellation and a bounded timeout. Allow a task to be queued while probing; never freeze the UI while waiting for slow websites or JavaScript runtimes.
- Discover ffmpeg/yt-dlp and any supported JavaScript runtime in documented locations; show clear setup instructions when unavailable. Update bundled tools only from approved HTTPS sources with version/checksum verification where available. Do not download opaque executables without user-facing provenance.
- Do not attempt to bypass DRM, paywalls, authentication, or access controls. Use only media the user is authorized to download; fail clearly when the source is protected or unsupported.

### E. Scheduler and completion actions

- Scheduling by selected weekdays and start/end clock times, including windows that cross midnight. Correctly handle local time and daylight-saving behavior where applicable.
- Option to prevent/pause new or active downloads outside the allowed window, then resume at the next permitted time; never corrupt partial data.
- Queue priorities only if they can be implemented predictably and tested.
- After all applicable downloads finish, support optional shutdown/sleep/hibernate only on platforms where it is implemented. Show a countdown/confirmation and provide Cancel. Never execute a power action from an accidental or stale callback.
- Clearly define whether failed/paused tasks block “all downloads complete.” Persist scheduler settings.

### F. Network and security preferences

- Proxy choices: none, system, manual HTTP and SOCKS5 where supported by the chosen backend. Proxy test action before saving, with sanitized error messages.
- Apply settings consistently to the backends they claim to control; explain when a backend cannot use a given proxy mode.
- Store credentials in the operating system’s credential store where available: Windows Credential Manager on Windows, and a secure platform key store where feasible elsewhere. Do not keep plaintext secrets in QSettings, JSON, SQLite, logs, command-line arguments, or exported playlists.
- If a platform credential store is not available, ask before using a documented fallback and protect it appropriately; never pretend plaintext storage is encrypted.
- Add logging that is disabled or privacy-preserving by default, bounded in size/rotation, and redacts credentials, auth headers, cookies, signed query tokens, and personal paths when practical.
- Optionally run Windows Defender/custom antivirus after completion and optionally open a custom app with the downloaded path, using a safe `QProcess` argument list—not shell concatenation. Show command/path validation and failure state.
- Mark downloaded files as Internet-origin on Windows only when supported and requested. Make platform-specific behavior explicit.
- Secure all URL parsing, filesystem paths, symlinks where relevant, extension requests, and external process execution. Limit API/body sizes and avoid command injection.

### G. Browser integration and app lifecycle

- Provide a Chrome/Edge Manifest V3 extension in a dedicated directory, based on FastDM’s workflows: context-menu add link, optional page/media link handling where supported, popup/status/options UI, and English/Bangla localization if practical.
- The Qt app owns the download queue. The extension sends approved URLs to the local app; it does not become a second download engine.
- Implement a loopback bridge bound only to `127.0.0.1` (and/or `::1` where correct), not all network interfaces. Use explicit pairing with user approval, strong random tokens, token hashes in the app, strict origin checks, host/DNS-rebinding defenses, URL/scheme allow-list, rate limits, request-size limits, timeouts, and per-request authorization. Never trust a paired extension blindly.
- Suggested endpoints (adjust to existing needs and document the contract): ping/status, pair, add URL, list task summaries, pause/resume/cancel a task. Return small validated JSON responses. Never expose filesystem access or arbitrary command execution through the bridge.
- Keep cookie transfer opt-in, domain-scoped to the requested URL, size-limited, short-lived, and never logged. Do not read/export unrelated browsing data.
- Implement the `bestdm://` custom URI scheme only where supported, validate every action and parameter, and route commands to the already-running app through a safe single-instance mechanism. Register/unregister protocol handlers safely per user.
- Closing/minimizing to tray, tray menu, reopen/restore, single-instance forwarding, clipboard behavior, notifications and shutdown must be configurable and tested. Do not swallow shutdown errors or orphan child processes.

### H. User preferences, history, updates, and shipping

- Preferences UI sections: General, Downloads, Browser Integration, Network/Proxy, Schedule, Notifications, Media/Tools, Post-download actions, Appearance, Advanced/Logging. Save atomically and validate all values.
- Preferences to include relevant FastDM behaviors: default folder, default concurrency, per-file connections, themes, sidebar/detail/compact view, tray behavior, notifications, update checks, speed cap, proxy settings, scheduler, retry policy, auto-removal policy, skip webpage links, category/host folder suggestions, server time option where meaningful, batch URL cap, antivirus/custom completion app, logging and bridge enable/disable.
- Version/update check over HTTPS, with timeouts, graceful offline behavior, and clearly presented release notes. Do not silently execute downloaded updates. Keep Store/package-specific behavior separate from generic desktop behavior.
- History page, completed/failed/paused filters, clear-history controls, remove-from-list vs delete-file distinction, confirmation for destructive actions, and a visible “open downloads folder” action.
- Version the app and database schema. Add a proper application icon on the executable/window/taskbar, metadata, license notices for dependencies and bundled tools, and a repeatable release build/deployment path.
- Provide packaging notes for Windows x64, including required Qt runtime/QML modules and optional tools. Keep Linux/macOS build instructions accurate. Do not claim iOS/Android parity for desktop-only system tray, shell protocol, or browser-extension features.

### I. Optional advanced capability: torrents

Torrent support is not part of the verified FastDM baseline, so it must not be faked or allowed to block core download reliability. If implemented, use a maintained library such as libtorrent-rasterbar behind an optional backend, confirm license/build/package requirements first, and add real magnet/torrent metadata, pause/resume, progress, seeding limits, and cancellation. If the dependency is unavailable, mark torrent support `Planned` or `Blocked` in the feature matrix rather than showing a non-functional control.

## 5. Data integrity, persistence, and recovery rules

1. Use SQLite for all download records and history. Implement `listAll`, `listUnfinished`, `findById`, `save/upsert`, `remove`, and history queries; do not limit app restoration to one record.
2. Store app preferences in QSettings or a dedicated settings object, not mixed into arbitrary download row data. Version the schema and write explicit migrations.
3. Every active record must be saved at meaningful state transitions and periodic progress checkpoints without writing to the database for every tiny network chunk. Use transactions for grouped/critical updates.
4. On startup, reconcile the database with `.part` and segment files, handle missing/corrupt paths, and restore unfinished tasks in a safe paused/queued state. Never assume a database byte count is more authoritative than validated file/segment state.
5. A final file should appear only after successful completion and required verification. Use same-volume rename where possible. If finalization fails, preserve recoverable partial data and tell the user.
6. History deletion must not accidentally delete the downloaded file. File deletion and record deletion are separate, explicit actions.
7. Database open, migration, query, disk-full, and corrupted-database errors must be visible in diagnostics; do not silently ignore `QSqlQuery::exec()` failures.

## 6. Testing and acceptance gates

Create automated tests at the same time as core functionality. Use local deterministic servers/fixtures rather than relying on public websites for routine tests.

### Core network tests

- Successful HTTP/HTTPS download, unknown content length, redirect, error response, connection drop, retry, pause/resume, process restart, and cancel.
- Server accepts Range and returns a correct `206`/`Content-Range`; server ignores Range and returns `200`; invalid/mismatched `Content-Range`; `416`; ETag change; Last-Modified fallback; a filename collision; insufficient/unwritable destination; disk/write/final rename failure.
- Segmented downloading with out-of-order completion, failed-segment retry, cancellation, merge validation, and safe fallback to a single stream.
- Multiple concurrent tasks stay within the configured limit, global speed cap is shared, and all counters match task states.
- Database migration, multiple unfinished tasks restored, completed history retained, corrupted/missing partial files, schema/query failure handling, and clean app restart.

### Feature integration tests

- FTP/SFTP authentication and resume where test fixtures/dependencies support them; invalid credentials; host-key rejection; cancellation.
- Media URL probe timeouts, absent tools, child-process cancellation, stderr error presentation, HLS/DASH parsing fixtures and merge output. Do not require a live YouTube login for automated CI.
- Scheduler windows including midnight crossing and weekdays; pause at boundary and resume at next allowed time.
- Browser bridge rejects invalid origin, missing/invalid token, invalid scheme, oversized body, malicious host, unauthorized cookies, and requests from non-loopback interfaces.
- QML smoke test: all navigation pages open, add dialog works, controls match current model state, no fake progress, and no visible inert buttons.

### Build/release gate

- Configure and build from a clean build directory using the installed Qt 6.12 kit/toolchain where available. Run CTest/Qt Test and any extension Node tests that already exist or are added.
- Check compiler warnings, QML import/loading warnings, missing runtime modules, path assumptions and resource loading.
- Prepare a manual release checklist for Windows x64: clean install/run, first start, destination permission, app restart recovery, tray and notifications, proxy, browser extension pairing, update behavior, uninstall, and data retention.
- Every feature matrix item must have a status. “Feature-complete” means implemented and verified or explicitly documented as platform-limited—not merely planned.

## 7. Implementation sequence — follow in this order

### Phase 0 — Safe audit and baseline

- Inspect all repository files and current Git status/branch before changes.
- Build the current app, if possible. Record exact commands, current errors and warnings.
- Map the real behavior of FastDM to a `docs/FEATURE_MATRIX.md` with source references and define how it maps into BestDM.
- Write `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/TESTING.md`, and `docs/IMPLEMENTATION_STATUS.md`.
- Correct stale README claims based on verified code. Do not claim new features yet.

### Phase 1 — Core foundation and robust single-download engine

- Define typed domain/state, repository APIs/migrations, and tests.
- Fix HTTP resume and partial-file integrity edge cases, file write checks, cancellation, response validation and useful errors.
- Preserve the current UI and make it more robust before large UI changes.
- Gate: build passes; baseline HTTP/resume/restart/error tests pass.

### Phase 2 — Real multi-download queue and complete UI model

- Integrate queue/task/model with `DownloadManager`; expose a proper `QAbstractListModel` to QML.
- Restore all unfinished tasks and load history. Wire every visible navigation/control.
- Implement add URL/batch URL, filters, sorting, counters, multi-select actions, search, row details, pause/resume/cancel/retry/remove/open folder.
- Gate: multiple concurrent tasks work, persistence works, no fake controls/progress.

### Phase 3 — Performance and core settings

- Add segmented downloads with range checks and safe fallback, retry policy, global speed cap, destination categories, proxy, configurable concurrency, clipboard prompt, preferences, theme, tray, notifications and logging.
- Ensure URL/media metadata probing never blocks queue creation or UI responsiveness. Use bounded timeouts and cancelable `QProcess` work.
- Gate: concurrency/speed/proxy/pause-resume/restart tests pass; responsiveness manually checked.

### Phase 4 — Remote protocols and folders

- Integrate real FTP/SFTP backend(s), safe credential storage, remote folder scan/select/filter, batch downloads, natural sort, and playlist creation.
- Gate: actual supported backends build and tests pass; unsupported configurations have accurate setup/feature messaging.

### Phase 5 — Media workflows

- Build the media coordinator; add HLS/DASH parsing/downloading, quality/audio selection, ffmpeg merge/remux and yt-dlp integration with progress, cancellation and safe tool discovery/update.
- Gate: manifest fixture tests and tool failure/cancellation tests pass; no DRM/access-control bypass behavior.

### Phase 6 — Browser and OS integration

- Implement the secured local bridge, pairing, extension, custom URL scheme, single-instance routing, post-download actions, scheduler and platform-aware notification/tray behavior.
- Gate: bridge security tests pass; extension/app integration is manually testable on supported Chromium browsers.

### Phase 7 — Release quality

- Finish light/dark/system themes, accessibility, empty/loading/error states, keyboard shortcuts, responsive scaling, icons, settings/history cleanup and translated strings where available.
- Update README, install/build/dependency/licensing notes, `docs/TESTING.md`, feature matrix and release checklist.
- Run clean configure/build/tests and report exactly what succeeded and what remains blocked.

Do not stop after Phase 0 or after drawing a better UI. Keep working through the phases as far as the execution environment permits, and persist a precise checkpoint when a real limit prevents further work.

## 8. Repository hygiene and coding standards

- Use small, responsibility-focused C++ classes and QML components. Avoid a giant `main.cpp`, giant `main.qml`, or one mega-manager that handles networking, UI, persistence, and settings together.
- Use clear English identifiers; use `const`/references appropriately, RAII, safe ownership and explicit error propagation. Avoid raw owning pointers unless the QObject parent model is intentional and clear.
- Avoid global mutable singletons for task state. Prefer dependency injection or a small number of well-defined application services.
- Never place passwords, bearer tokens, cookies, proxy credentials, signed URLs, or sensitive headers in logs, crash messages, screenshots, or demo data.
- Do not add libraries casually. Inspect maintenance, license, supported platforms, build requirements and deployment effects. Prefer CMake feature gates for optional heavy dependencies.
- Keep generated `build`, `bin`, `obj`, deployed Qt runtime, test output, logs, and local secrets out of source control.
- Update README only to describe features that are actually implemented. Include setup instructions for optional yt-dlp/ffmpeg and protocol backends.
- Keep automated tests deterministic and avoid sleep-heavy flaky tests. Inject clocks/process/network backends where it meaningfully improves testability.

## 9. Required completion report

At the end of each working session, report in Bangla:

1. What was inspected and changed, with the main file paths.
2. Which features are now actually implemented and which remain `Partial`, `Planned`, or `Blocked`.
3. Exact build/test commands run and their real results, including failed tests and warnings.
4. Any dependency/install requirements and platform limitations.
5. The next exact implementation step, recorded in `docs/IMPLEMENTATION_STATUS.md` so the work can continue without repeating the audit.

Do not say “complete”, “fully working”, or “production-ready” unless the acceptance gates relevant to that claim pass. Do not claim to have tested a feature only because its code compiles.

---

## 10. Start now

Begin by inspecting the current BestDM working tree and toolchain, then compare the relevant FastDM implementation areas listed in Section 1. Produce only a concise audit summary, then immediately implement Phase 0 documentation and Phase 1 improvements. Continue into subsequent phases without waiting for further permission unless a genuinely destructive choice is required. Keep the app buildable at each milestone, preserve existing user changes, and update the feature matrix and implementation checkpoint as work progresses.

## Continuation prompt (use if the agent/session reaches its context limit)

> Continue the BestDM implementation from the existing repository state. First read `docs/IMPLEMENTATION_STATUS.md`, `docs/FEATURE_MATRIX.md`, `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, and `docs/TESTING.md`; inspect `git status` and do not discard user changes. Re-run the smallest relevant build/test needed to establish the current state. Continue from the first incomplete acceptance gate in the implementation status, not from the beginning. Implement code and tests—not only a plan—using Qt 6.12 LTS, C++20, QML, CMake and the architecture in `BestDM_Master_Prompt.md` (or the prompt supplied in the current conversation). Preserve FastDM behavior where required, never fake feature support, and report progress in Bangla with exact test/build results. Update the status and feature matrix before stopping.

## Official technical references

- Qt 6.12 LTS release: https://www.qt.io/blog/qt-6.12-released
- Qt 6.12 QML: https://doc.qt.io/qt-6.12/qtqml-index.html
- Qt 6.12 CMake/QML application setup: https://doc.qt.io/qt-6.12/cmake-build-qml-application.html
- Qt 6.12 Network: https://doc.qt.io/qt-6.12/qtnetwork-index.html
- QNetworkAccessManager: https://doc.qt.io/qt-6/qnetworkaccessmanager.html
- Qt Quick deployment: https://doc.qt.io/qt-6/qtquick-deployment.html
