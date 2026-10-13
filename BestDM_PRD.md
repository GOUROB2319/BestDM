# BestDM — Product Requirements Document (PRD)

**Document status:** Draft for implementation  
**Product:** BestDM  
**Document version:** 1.0  
**Date:** 12 October 2026  
**Primary implementation:** C++20, Qt 6 / Qt Quick (QML), CMake, SQLite  
**Primary repository:** https://github.com/GOUROB2319/BestDM  
**Feature reference:** https://github.com/GOUROB2319/FastDM

---

## 1. Executive Summary

BestDM is intended to become a reliable, modern, full-featured download manager—not merely a redesigned UI or a single-download demo. It should combine the essential, proven capabilities of the existing FastDM application with a cleaner cross-platform architecture using Qt 6, C++20, Qt Quick/QML, and SQLite.

FastDM is the feature reference, not code to be mechanically ported. BestDM should preserve the user's core workflows while redesigning internal modules for maintainability, testability, responsiveness, security, and eventual platform portability. It should retain working features rather than silently dropping them during the rewrite.

The first release target is a polished Windows desktop app. Architecture and dependencies should be chosen to support Linux where feasible. Features such as browser integration and media extraction must be modular because they depend on platform integration and external tools. Mobile platforms are not part of the first desktop release and must not be claimed as feature-equivalent without a separate feasibility plan.

## 2. Product Vision

**Vision:** Give users one dependable place to add, organize, schedule, resume, inspect, and manage file and media downloads, with transparent progress, safe recovery after restart, and a professional native-feeling interface.

### Product principles

1. **Functionality before decoration:** every visible control must perform its stated action.
2. **No fake completion:** a feature is not complete until its implementation, tests, and user workflow are verified.
3. **Safe by default:** never disable TLS verification by default, never overwrite files silently, and never expose credentials in logs, playlists, URLs, or bridge responses.
4. **Recoverable transfers:** crashes, network loss, and app restarts should not corrupt completed files or unnecessarily discard partial data.
5. **Responsive UI:** network and file operations must not block the QML/UI thread.
6. **Modular design:** protocols, media extraction, browser integration, scheduling, and UI should be separate modules behind defined interfaces.
7. **Honest cross-platform support:** show platform-specific limitations rather than presenting unsupported features as working.

## 3. Problem Statement

The existing FastDM application already demonstrates a broad set of user needs, but its large WinForms codebase is difficult to evolve as a cross-platform product. BestDM has an initial Qt foundation, but the current working slice is much smaller than the feature set users expect from a mature download manager.

The product needs to solve both problems: retain the useful feature set and build a maintainable Qt-native application with reliable multi-download orchestration, complete task persistence, error recovery, clear UX, and a repeatable release process.

## 4. Goals and Non-Goals

### 4.1 Goals

- Provide a complete Windows desktop download manager with multiple concurrent tasks and persistent history.
- Support reliable HTTP/HTTPS downloads, pause/resume, retries, safe partial files, conflict handling, speed/ETA reporting, and optional segmented downloads.
- Provide organized downloads, categories, queues, bulk actions, search/filtering, and detailed per-task information.
- Retain and reimplement FastDM's high-value features: proxy, global speed limit, scheduling, notifications, system tray, folder/batch downloads where supported, FTP/SFTP, media downloads, yt-dlp/FFmpeg integration, browser extension/bridge, settings, update checks, and post-download actions.
- Use Qt/C++20 and CMake with clear module boundaries and automated verification.
- Maintain documented dependency licensing, security behavior, and packaging instructions.

### 4.2 Non-goals for the initial MVP

- Cloud synchronization or a user account system.
- A built-in browser or general-purpose media player.
- Remote control over the public internet.
- Silent installation of external tools or hidden background collection of user activity.
- Guaranteed parity across every operating system for every external tool or browser integration.
- Bypassing access controls, DRM, paywalls, or private content protections. Media features are for URLs and content the user is authorized to access and download.

### 4.3 Future / optional capability

A BitTorrent module may be added behind a separate engine adapter after the core application is stable. It must have a legal, license-compliant dependency, clear user controls, and no hidden seeding or network behavior. It is not a prerequisite for calling the first conventional download-manager release an MVP.

## 5. Target Users and Primary Jobs

### Persona A — Everyday desktop user
- Adds direct download links.
- Chooses a folder and filename.
- Pauses, resumes, cancels, retries, and opens completed files.
- Expects downloads to survive app restarts.

### Persona B — Power user
- Manages many downloads concurrently.
- Uses queues, batch URL input, categories, speed limits, proxies, retries, and scheduled time windows.
- Needs accurate status, ETA, throughput, and error details.

### Persona C — Media downloader
- Pastes a supported video/page URL.
- Reviews detected title, formats, resolutions, subtitles/audio options when available.
- Selects output options and sees a clear explanation if an external runtime or site support is missing.

### Persona D — Browser-integrated user
- Sends a link from a Chromium-based browser to BestDM.
- Explicitly pairs the browser extension with the local app.
- Can revoke pairing and understand what information is shared.

## 6. Platforms and Technical Constraints

### Initial target
- Windows 10/11 x64 desktop, tested against the project's installed Qt 6.12 MSVC 2022 toolchain.
- Qt Quick/QML for the interface; C++20 for core logic; CMake as the primary build definition.

### Portability target
- Keep core domain, networking abstractions, task persistence, queue policy, and most QML portable.
- Plan and test Linux builds as a separate release target after the Windows build and dependencies are stable.
- Treat Windows system tray, credential storage, installer, protocol registration, browser native-messaging/bridge integration, and post-download power actions as platform adapters.
- Do not promise iOS/Android parity in this desktop PRD. A mobile edition would need its own product plan and capability matrix.

### Key engineering constraints
- Qt Quick/QML must never own networking, file-writing, persistence, or retry logic.
- Long-running work must not block the UI thread.
- Every task requires a stable ID and an explicit state machine.
- All database writes must use prepared statements and checked errors.
- External processes must be invoked with argument lists, not shell-concatenated commands.
- TLS certificate validation remains enabled by default.

## 7. Current Repository Baseline (Observed)

The current BestDM repository contains a Qt/C++ foundation and a basic download workflow:

- `CMakeLists.txt`: C++20 build; Qt Quick, Network, and SQL dependencies; resource packaging and Windows deployment step.
- `BestDM/main.cpp`: creates the Qt application, loads QML, and exposes a `DownloadManager` object to QML.
- `BestDM/main.qml`: initial light/dark shell, navigation-like sidebar, a new-download dialog, and one current-download panel.
- `BestDM/downloadmanager.h/.cpp`: HTTP/HTTPS request, streamed writes to `.part`, rudimentary pause/resume, progress/speed/ETA, and finalization.
- `BestDM/domain.h`: initial download-state and record definitions.
- `BestDM/persistencerepository.h/.cpp`: SQLite storage for download records and a version table.
- `BestDM/downloadqueue.h/.cpp` and `downloadtask.h/.cpp`: early queue/task implementations, not yet wired through the primary QML application surface.
- The supplied icon assets are already referenced by the app.

### Known gaps to address before expanding features

1. The current manager represents one current transfer rather than a complete collection exposed as a QML model.
2. The visible sidebar items are largely presentation-only; downloads, active items, completed items, history, and queues need functional navigation/filtering.
3. The queue/task layer is not fully integrated with the current `main.cpp`/QML interface.
4. Persistence restore currently retrieves only the latest eligible record, not all tasks required to rebuild the complete task list and queues.
5. Resume handling must validate HTTP status, `Content-Range`, `ETag`/`Last-Modified`, and server Range support. If a server ignores a Range request, the client must restart the partial file safely rather than append a duplicate.
6. HTTP error codes, expired URLs, redirects, authentication prompts, disk-full/write failures, cancellation races, and missing destination folders need explicit handling and tests.
7. Database schema changes need real versioned migrations and visible error handling; a schema-version table alone is insufficient.
8. Every menu item, button, navigation entry, settings field, and keyboard shortcut must be connected to a tested action.
9. The interface needs a full list/table model rather than a single-transfer card.
10. Current build/release instructions and automated tests must be expanded as new modules are introduced.

These are baseline findings from repository source inspection, not a claim that every behavior has been tested on a running machine.

## 8. Product Scope and Priorities

Priority definitions:
- **P0 — Release blocker:** required for a usable, trustworthy MVP.
- **P1 — Core product:** required for a mature first public release.
- **P2 — Advanced feature:** important FastDM parity or power-user capability.
- **P3 — Later/optional:** isolated capability that can follow without weakening the core.

### P0 — Complete conventional download manager

#### FR-001: Add download
The user can add a direct HTTP/HTTPS URL, paste from clipboard, select the destination folder, set/confirm a filename, and review invalid URLs before starting.

Acceptance criteria:
- Valid HTTP and HTTPS links can be queued.
- Invalid schemes, malformed links, empty input, and unsafe filenames are rejected with clear explanations.
- The app never begins writing outside the selected destination because of a URL-derived filename.
- Adding a link does not freeze the UI while metadata is probed.

#### FR-002: Multi-download task list
The app shows all tasks in a real model/view with filename, state, progress, size, speed, ETA, destination, and error status where applicable.

Acceptance criteria:
- Multiple tasks can exist at the same time.
- Sidebar filters show the appropriate tasks, and their counts match the model.
- Search and category filters update the list without destroying task state.
- Sorting is stable and sensible for filename, state, size, date, and progress.

#### FR-003: Download lifecycle and state machine
Implement queued, probing, downloading, paused, waiting/scheduled, retrying, completed, failed, and canceled states as needed. State transitions must be centrally validated, not inferred from loosely related UI booleans.

Acceptance criteria:
- Pause stops network consumption and retains a valid partial file.
- Resume performs a validated range request or restarts safely when range resume is unsupported.
- Cancel behavior is explicit: remove task only, or remove task and partial data, according to the user's chosen action.
- A completed file is published only after successful completion and final validation.
- A failure reports actionable details and offers retry where appropriate.

#### FR-004: Concurrency and queue
The user can configure a maximum number of active transfers and create/select queues. Queued tasks start when capacity becomes available.

Acceptance criteria:
- Active transfer count never exceeds the configured limit.
- Paused and canceled tasks do not consume an active slot.
- The application does not start duplicate workers for the same task.
- Queue order and task states survive restart.

#### FR-005: Persistence and recovery
Persist all task metadata and relevant settings to SQLite. Recover the full task collection on startup and reconcile database state with actual files.

Acceptance criteria:
- Relaunching restores all completed-history entries and all eligible incomplete tasks.
- In-flight tasks restore in a safe paused/queued state; they must not silently resume unexpectedly unless the user enabled that behavior.
- Missing `.part` files, missing destination folders, stale states, and removed files are reconciled without crashing.
- Database failures are logged safely and surfaced in the UI.
- Versioned migrations preserve existing user data when the schema changes.

#### FR-006: Safe file handling
Use a temporary/partial path, collision policy (ask, rename, or overwrite), safe finalization, and cleanup rules. Never silently overwrite a completed file.

Acceptance criteria:
- Default collision handling renames or asks; silent overwrite is not the default.
- Failed or canceled tasks cannot leave a corrupt file masquerading as complete.
- Existing completed files remain untouched when finalization fails.
- Disk-full and permission errors are visible and do not corrupt other tasks.

#### FR-007: Main UX completeness
Deliver complete Downloads, Active, Completed, History, Queue, and Settings views; useful empty states; responsive layout; light/dark/system theme; keyboard navigation; accessible focus; and user-readable errors.

Acceptance criteria:
- No navigation item is decorative only.
- Every enabled control has a working action and state feedback.
- The app remains responsive while downloads are active.
- Theme preferences persist across sessions.

### P1 — Reliability, speed, and daily-use features

#### FR-008: Connection strategy and segmented downloads
Support configurable per-task connections. Segment only when the server supports byte ranges and the resource can be safely segmented; otherwise fall back to a single stream.

Acceptance criteria:
- All segment workers write to non-overlapping byte ranges or separate temporary segment files.
- Segment results are verified and merged safely.
- Failed segments can retry without duplicating bytes.
- A segmented download is never advertised as faster when it has fallen back to a single connection.
- Connection count is bounded and configurable.

#### FR-009: Speed and ETA
Show current and average throughput where useful, downloaded/total bytes, percentage, and ETA when calculable.

Acceptance criteria:
- Unknown content size is represented as indeterminate, not as 0% of 0 bytes.
- Speed uses a smoothed interval and becomes zero/idle when paused or stalled.
- ETA is hidden or marked unknown when there is insufficient data.

#### FR-010: Retry and network resilience
Support bounded retries with exponential backoff and cancellation. Distinguish temporary network errors, HTTP status errors, authentication problems, rate limits, and permanent failures.

Acceptance criteria:
- Retry attempts and next retry time are visible.
- Canceled tasks never restart because of an old retry timer.
- Authentication challenges do not loop forever.
- Retry limits are configurable and documented.

#### FR-011: Global speed limiter
Apply a global download bandwidth limit across all active tasks, with a no-limit option.

Acceptance criteria:
- Combined download throughput approximately respects the configured limit over a reasonable interval.
- The limiter does not allocate the full cap independently to every task.
- UI and settings show the effective limit.

#### FR-012: Proxy and network settings
Support direct, system proxy, and manual proxy modes where the selected backend supports them. Include proxy authentication via secure storage and a test connection action.

Acceptance criteria:
- Proxy settings apply consistently to supported protocols.
- Unsupported protocol/proxy combinations are explained rather than silently ignored.
- Secrets are excluded from logs and exported configuration.
- TLS validation remains enabled unless the user explicitly changes a separately documented advanced option; there must be no insecure default.

#### FR-013: Batch URLs and download categories
Allow pasting/importing multiple URLs, deduplicate or confirm duplicates, and optionally categorize files by extension or host.

Acceptance criteria:
- Invalid rows are reported independently; one invalid link does not discard all valid links.
- Batch size is bounded and the UI remains responsive.
- Category rules are transparent and user-editable.

#### FR-014: Notifications and system tray
Show configurable completion/failure notifications, minimize-to-tray and close-to-tray options, and tray actions where supported by the OS.

Acceptance criteria:
- Notifications can be disabled.
- Closing to tray is clearly configured and the app can be exited from the tray.
- Unsupported platform behavior is hidden or replaced with a clear fallback.

#### FR-015: Scheduler and post-download actions
Schedule permitted days/time windows and optional actions after a queue or all downloads complete. Power actions are platform-specific and must include a confirmation/cancel countdown.

Acceptance criteria:
- Downloads obey selected time windows consistently, including schedules crossing midnight.
- The UI explains whether a task is waiting for the schedule.
- Shutdown/sleep/hibernate actions are off by default, confirmed, cancelable, and not run while an unexpected task remains active.

### P2 — FastDM feature parity and media/folder workflows

#### FR-016: FTP/SFTP transfers
Add FTP and SFTP through separate protocol adapters. Credential prompts, remote file metadata, resume support, and folder listing must be handled according to each protocol and server capability.

Acceptance criteria:
- Protocol capability is discovered or reported; resume is not assumed without support.
- Credentials are never emitted in logs, filenames, URLs shown in exports, or history exports.
- Authentication, host-key/certificate verification where applicable, timeout, and retry behavior are documented.
- Folder scanning is depth/size bounded and protected against path traversal and recursive loops.

#### FR-017: Remote folder download
Where supported, browse remote folders, select files/folders, filter by media/file type, preserve safe relative paths, and queue selected items.

Acceptance criteria:
- Remote paths are normalized and cannot write outside the chosen local root.
- Large folders can be canceled and display scan progress.
- Unsupported server directory layouts produce a useful error instead of a crash.

#### FR-018: Media URL detection and format selection
Allow users to inspect supported media/page URLs, load available format metadata, and choose video/audio/quality options where the source and tools support them.

Acceptance criteria:
- Metadata probing runs asynchronously, can be canceled, and has a timeout.
- Unsupported/private/expired URLs produce a clear explanation.
- The app does not claim that any arbitrary webpage is downloadable.
- The user can return to ordinary URL download when appropriate.

#### FR-019: yt-dlp integration
Use a managed or user-provided yt-dlp executable, with explicit version/status display and controlled updates. Support format selection, audio-only mode, progress parsing, cancellation, and accurate error reporting.

Acceptance criteria:
- External process calls use explicit argument arrays and safe temporary files.
- A missing or outdated executable prompts a clear recovery flow.
- Download output is not guessed solely from an intermediate filename; final output is resolved and validated.
- Tool licensing, source, checksum/version, and update channel are documented.
- Site support may change independently of BestDM, and this limitation is communicated.

#### FR-020: HLS/DASH support and FFmpeg
Support compatible HLS/DASH manifests and separate audio/video tracks when lawful and technically supported. Use FFmpeg for merging/remuxing when required and preserve intermediates only when needed for recovery.

Acceptance criteria:
- Manifest fetching validates URL resolution, response size, timeout, and redirects.
- Segment retries and progress are tracked.
- Merge failure preserves recoverable data and explains the missing dependency or codec/container issue.
- DRM-protected content is not bypassed.

#### FR-021: Playlists
Generate M3U/M3U8 playlists for eligible local files or permitted stream URLs. Use relative local paths where possible and strip credentials from playlist URLs.

Acceptance criteria:
- Local playlists continue to work after moving the complete download folder as far as their relative paths permit.
- No password or access token is written into playlist files.
- Unsupported media items are listed as skipped, not silently omitted.

#### FR-022: Post-download actions and file safety
Support optional opening the containing folder/file, launching a user-selected program, and antivirus scanning where the platform supports it.

Acceptance criteria:
- External executable path and arguments are user-configured and clearly displayed.
- Actions run only after a confirmed successful download.
- Dangerous command-shell interpolation is avoided.
- Mark-of-the-Web or similar platform-specific metadata is applied only where appropriate and documented.

### P2/P3 — Browser integration and distribution

#### FR-023: Chromium browser extension
Create a separate Manifest V3 Chrome/Edge extension that can send a user-approved link to the desktop app. Include popup/options UI, supported-language strings as feasible, link capture, and a clear disabled/error state if BestDM is not installed or running.

Acceptance criteria:
- Browser integration is optional and can be disabled in desktop settings.
- The extension sends only necessary link metadata and only after explicit user action or a clearly enabled capture preference.
- Pairing/revocation is visible to the user.
- The extension has automated unit tests and a reproducible packaging process.

#### FR-024: Local bridge security
Use loopback only by default, restrict request origins, authenticate paired clients with revocable high-entropy tokens, validate every request and URL, rate-limit sensitive operations, and protect against DNS rebinding/CSRF-style abuse.

Acceptance criteria:
- Bridge listener binds to `127.0.0.1` or equivalent loopback-only configuration, not all network interfaces by default.
- Pairing requires an in-app approval step.
- Only allowed HTTP/HTTPS URLs can be submitted through the browser bridge unless the user explicitly enables another supported protocol.
- Credentials/cookies are minimized, domain-scoped, and never logged.
- The extension cannot invoke arbitrary shell commands or arbitrary executable paths.
- Revoking a paired client immediately invalidates its token.

#### FR-025: Application protocol handler
Register a BestDM URL scheme (for example `bestdm://`) safely on supported platforms for opening the add-download workflow. Validate parsed commands and use single-instance forwarding.

Acceptance criteria:
- Invalid or hostile protocol input is rejected.
- Protocol requests open an add/confirmation dialog by default; they do not silently start untrusted downloads unless the user deliberately configures that behavior.
- Registration/unregistration and reinstall behavior are documented.

#### FR-026: Updates and packaging
Provide versioned builds, repeatable packaging, clear update source, changelog, and signature/checksum guidance where available. Do not assume the Microsoft Store is the only deployment channel.

Acceptance criteria:
- Fresh install, upgrade, uninstall, and data preservation behavior are tested.
- Package does not bundle dependencies whose licenses prohibit redistribution.
- External tools are shipped or updated only after license review and integrity checks.
- The app can show update availability without forcing an update or blocking downloads.

### P3 — Optional BitTorrent adapter

#### FR-027: BitTorrent capability (separate roadmap decision)
If implemented, provide add-torrent/magnet, progress/seeding status, pause/resume/remove, destination selection, queue/concurrency controls, and explicit upload/seed controls.

Acceptance criteria:
- Library license is compatible with distribution and the repository includes dependency/license notices.
- Seeding/upload defaults and behavior are transparent and user-configurable.
- Network ports and peer connectivity are documented; no UPnP/firewall changes occur without clear permission.
- This module cannot destabilize normal HTTP/FTP/SFTP downloads if disabled or unavailable.

## 9. UX and Interaction Requirements

### Main navigation
- Downloads / All
- Active
- Queued / Scheduled
- Completed
- Failed
- History
- Queues/categories
- Media tools (if enabled)
- Browser integration (status/settings)
- Settings / About

### Main download list
Columns or adaptive fields:
- File name and type icon
- Status
- Progress bar and percent
- Downloaded / total size
- Speed and ETA
- Destination or category (optional)
- Added/updated date (optional)

### Contextual actions
Task actions: Start/resume, Pause, Retry, Cancel, Remove from list, Remove and delete partial data, Open file, Open folder, Copy URL, Details, Change destination where safe.

Bulk actions: pause all, resume eligible, retry failed, cancel selected, remove selected, set category, change queue. Confirm destructive bulk operations.

### Details view
Expose URL (with credentials/tokens redacted), effective destination, content type, file size, server/range support where known, timestamps, retries, current/average speed, error details, and relevant protocol/segment status.

### Design direction
- Use the existing BestDM icon and brand palette as a starting point, not as a reason to compromise contrast.
- Support light, dark, and follow-system themes.
- Use consistent spacing, typography, icons, empty/loading/error states, and clear active/focus states.
- Prefer scalable vector icons or licensed icon sets over emoji as primary UI icons.
- UI must work at the project's minimum supported window size and scale with Windows display scaling.
- Keep animation subtle and optional; do not animate large lists on every progress tick.

## 10. Recommended Architecture

Keep domain logic independent from QML and separate the current monolithic manager responsibilities into cohesive modules. The exact class names may change, but the boundaries must remain.

### Suggested modules

1. **App / Composition Root** — application startup, dependency creation, settings migration, single-instance coordination.
2. **Domain** — `DownloadRecord`, stable task ID, URL/request specification, state machine, error types, capabilities, queue configuration.
3. **Application Services** — add, pause, resume, retry, cancel, remove, bulk actions, scheduler decisions, post-download actions.
4. **Download Orchestrator** — task registry, queue scheduling, maximum concurrency, retries, duplicate checks, lifecycle coordination.
5. **Protocol Adapters** — HTTP/HTTPS, FTP, SFTP, media/yt-dlp, HLS/DASH, optional BitTorrent. Each adapter advertises capabilities such as resume, listing, auth, segmentation, and known size.
6. **Persistence** — SQLite repository, schema migrations, atomic state changes, all-task restore, safe query access.
7. **Networking / Policy** — proxy settings, request headers, cookies, TLS policy, speed limiter, timeouts, redirect rules.
8. **External Tool Manager** — executable discovery, version check, integrity validation, process lifecycle, output parsing, license notes.
9. **Browser Bridge** — optional local server, pairing/auth, URL validation, rate limiting, request DTOs.
10. **Presentation** — QML pages, reusable components, view models/list models, dialogs, theme, accessibility.
11. **Platform Services** — system tray, notifications, credential storage, protocol registration, power actions, installer/update adapters.
12. **Diagnostics** — structured logs with redaction, crash/error reports stored locally by default, support bundle generated only with user consent.

### Core implementation rules

- Use a `QAbstractListModel`-based task model (or equivalent stable Qt model) as the source of truth for the UI.
- QML calls narrow, typed service methods and observes model/property changes; it must not implement transfer algorithms.
- Represent each task's current state in the domain layer, with validated transitions.
- Persist by stable IDs; never rely on a single `restoreLatest()` query for the application state.
- Use `QSaveFile` or an equally safe finalization strategy where appropriate. Flush/close and validate before final rename.
- Use cancellation tokens/owned cancellation objects consistently across network, media, process, and queue operations.
- Avoid one network manager per task unless required by proxy/cookie isolation; document ownership and thread affinity.
- Every worker emits structured state/progress/error events and never mutates QML objects from a worker thread.
- Avoid shared mutable globals for network settings or secrets.

### Data model essentials

Minimum persistent fields:
- `id`, `url` (sanitized for display if needed), `file_name`, `destination`, `protocol`, `state`
- `received_bytes`, `total_bytes`, `etag`, `last_modified`, `range_supported`
- `created_at`, `started_at`, `updated_at`, `completed_at`
- `retry_count`, `next_retry_at`, `priority`, `queue_id`, `category_id`
- `error_code`, `error_message` (sanitized), `content_type`, `checksum` if implemented
- adapter-specific metadata stored in a versioned/validated form; do not store secrets in clear text

Additional tables may include settings, queues, categories, segments, cookies with scoped lifetime, browser clients (hashed tokens only), schema version, and event history. Design indexes for active/history filters. Provide migrations and rollback-safe error behavior.

## 11. Security, Privacy, and Compliance

- Do not collect analytics or send task history to a remote service by default.
- Do not log authorization headers, cookies, credentials, tokens, signed query parameters, or full sensitive URLs.
- Store reusable credentials in the operating system's secure credential facility when available; otherwise explain the limitations and offer session-only credentials.
- Apply strict input validation to URLs, paths, protocol commands, manifest entries, and external process arguments.
- Protect against path traversal, unsafe file names, symlink/path reparse issues where relevant, archive/manifest bombs, uncontrolled recursion, and unbounded metadata responses.
- Keep TLS certificate checks enabled by default.
- Make browser pairing explicit, scoped, revocable, and loopback-only by default.
- Provide a privacy statement that explains local database contents, cookies/credentials behavior, external tools, update checks, and browser extension communication.
- Review licenses/notices for Qt and every bundled third-party library/tool before releasing binaries.
- Respect source-site terms, access permissions, copyright, and DRM boundaries. The app must not attempt to bypass DRM or access controls.

## 12. Performance and Reliability Requirements

Initial acceptance targets (measure on supported reference hardware and revise only with evidence):
- App startup should not wait for slow network operations or external media probes.
- Adding and switching tasks should remain responsive with at least 500 history records and 50 queued tasks in a representative test dataset.
- Progress updates should be throttled/coalesced so high-throughput downloads do not cause excessive QML repainting or database writes.
- The task model should not be rebuilt from scratch on every progress signal.
- Memory use should remain bounded during large downloads; file content must be streamed rather than accumulated in memory.
- Metadata probing, folder scans, media discovery, merging, and tool updates must support cancellation and timeouts.
- One failing task must not stop unrelated tasks or crash the app.
- On restart, state reconciliation must be deterministic and should not automatically restart all interrupted downloads unless configured.

## 13. Observability and Error Handling

- Use structured local logs with levels and rotation/size caps.
- Errors should include a stable internal error code plus a user-readable explanation.
- Redact secrets before writing logs.
- Add diagnostics for request start/end, state transitions, retry decisions, persistence failures, and external tool exits—without logging private payloads.
- Let users export a redacted diagnostic bundle explicitly.
- Never display raw stack traces as the only user-facing error.

## 14. Test Strategy

### Unit tests
- URL parsing and scheme validation
- Filename/path sanitization and collision policies
- Task state-machine transitions
- Retry/backoff/cancellation policy
- Resume capability decisions and `Content-Range` validation
- Speed limiter allocation
- Scheduler/time-window boundaries, including midnight and daylight/timezone cases
- Database migrations, save/restore, and secret redaction
- Playlist path and credential stripping
- Browser bridge token validation, origin checks, rate limiting, and URL allowlist

### Integration tests
- Local HTTP test server with known size, unknown size, redirects, range support, ignored Range, malformed `Content-Range`, 401, 403, 404, 416, 429, 500, early disconnect, slow responses, and changed ETag.
- File system tests for collisions, permission denied, disk-full simulation where available, interrupted writes, missing partial files, and final rename failures.
- Multi-task tests verifying concurrency caps, fairness, retry isolation, pause/resume, and task removal.
- Restart tests with many simultaneous tasks and completed-history items.
- FTP/SFTP tests against controlled test servers if these adapters are enabled.
- Media tests against local fixtures and mocked external processes; do not depend only on live third-party websites.
- Extension/bridge tests for pairing, revocation, malformed requests, unauthorized origins, oversized bodies, and app-not-running state.

### UI tests / manual QA
- Add URL, browse destination, keyboard flows, validation, and duplicate filenames.
- Navigation filters and count accuracy.
- Light/dark/system theme and display scaling.
- Minimize/restore/tray behavior.
- Settings persist and take effect.
- Accessibility/focus order and empty/loading/error states.

### Build matrix
- Windows x64 Debug and Release.
- Clean build from a fresh checkout with documented Qt/CMake/compiler versions.
- Linux build after portability is introduced.
- Release packaging/deployment test, including required Qt plugins/QML imports and external dependencies.

## 15. Release Acceptance Criteria

A release candidate is not accepted until:

1. P0 requirements are implemented and tests pass.
2. A fresh install and an upgrade preserve expected data.
3. Multiple concurrent transfers complete correctly from a controlled test server.
4. Pause/resume works when range-supported and safely restarts when it is not.
5. All tasks and history are restored after forced app termination without duplicate/corrupt output files.
6. All visible primary UI controls are functional and manually tested.
7. TLS verification remains enabled by default and no secrets appear in logs/exports.
8. Build, test, package, and run commands are documented and reproducible.
9. Known limitations, supported protocols, third-party licenses, privacy behavior, and external-tool behavior are documented.
10. The README and feature matrix distinguish implemented, tested, partially supported, and planned features.

## 16. Delivery Roadmap

### Phase 0 — Stabilize and establish guardrails
- Review the entire BestDM tree and current Git diff before editing.
- Create `docs/PRD.md`, `docs/ARCHITECTURE.md`, `docs/FEATURE_MATRIX.md`, `docs/TESTING.md`, and `docs/IMPLEMENTATION_STATUS.md`.
- Add repeatable configure/build/test commands and ensure the existing Qt app still builds.
- Add core test infrastructure and a local HTTP fixture server.
- Fix current single-download correctness issues before expanding scope.

### Phase 1 — P0 functional core
- Domain/state machine and stable download IDs.
- Persistent task list model with SQLite restore-all and schema migrations.
- Multi-download queue and active-concurrency control wired to QML.
- Safe HTTP/HTTPS range handling, partial files, collisions, cancellation, retries, and error states.
- Complete main list, filters, task actions, settings basics, and theme persistence.

### Phase 2 — Reliability and power-user fundamentals
- Segmented downloads where safe.
- Global speed limit, proxy, batch URLs, categories, queue management.
- Notifications/tray, scheduler, preferences persistence, and post-download actions.
- Expand tests for network failures, concurrency, restart recovery, and scheduling.

### Phase 3 — Protocol parity
- FTP/SFTP adapters, credentials, supported folder listing and folder download workflows.
- Feature capability discovery and protocol-specific error handling.
- Playlist generation and folder/category polish.

### Phase 4 — Media support
- External tool management and yt-dlp integration.
- HLS/DASH detection, format/quality selection, FFmpeg merge/remux, audio-only options.
- Tests built around fixtures/mocks and documentation of third-party site limitations.

### Phase 5 — Browser integration and distribution hardening
- Extension UI and packaging.
- Secure localhost bridge, explicit pairing/revocation, custom protocol handler.
- Update checks, package/installer, clean install/upgrade testing, licensing/privacy documentation.

### Phase 6 — Portability and optional modules
- Linux build and platform service adapters.
- Evaluate the optional BitTorrent module only after the core release is stable.
- Make a separate product decision for mobile platforms.

Phases may overlap when dependencies are clear, but later features must not bypass P0 architecture or testing foundations.

## 17. Definition of Done for Every Feature

A feature is done only when all applicable items are complete:

- It is implemented in the correct module, with no network/business logic hidden in QML.
- UI, state, errors, loading, cancellation, and empty states are connected.
- Unit/integration tests cover success, failure, cancellation, and recovery paths.
- No hardcoded local path or personal machine assumption is required to build/run it.
- Logs and persisted data are checked for secrets.
- Documentation and feature matrix are updated.
- Debug and Release builds pass, and the actual workflow has been run or any unverified part is explicitly marked.
- No unrelated feature or file is removed without an explicit migration/reason.

## 18. Documentation Deliverables

- `README.md`: product overview, feature matrix, supported platforms, build/run/test instructions, screenshots, current limitations.
- `docs/PRD.md`: this document.
- `docs/ARCHITECTURE.md`: modules, dependency directions, task lifecycle, persistence, bridge security, threading model.
- `docs/FEATURE_MATRIX.md`: FastDM capability → BestDM status → implementation module → tests → platform constraints.
- `docs/TESTING.md`: exact commands for unit/integration/UI tests and manual QA.
- `docs/IMPLEMENTATION_STATUS.md`: phase status, completed tests, known issues, next concrete tasks, last verified commit/build.
- `docs/PRIVACY.md`: local data, secrets, browser integration, update checks, external tools, diagnostic exports.
- `THIRD_PARTY_NOTICES.md`: licenses and notices for bundled libraries, tools, icons, and other assets.

## 19. Key Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Scope too large for one pass | Deliver in phases; keep P0 stable and maintain a tested build at every stage. |
| FastDM behavior lost during rewrite | Maintain a traceable feature matrix and review each FastDM module before implementing its Qt equivalent. |
| UI becomes a mockup with inert controls | Use acceptance criteria and a manual control audit for every page. |
| Incorrect resume corrupts files | Validate Range/status/Content-Range/validators; fall back to a clean restart. |
| QML model and engine state diverge | Keep one domain/task registry as source of truth, exposed through Qt models/view-models. |
| External media tools change behavior | Version/integrity checks, adaptable output parsing, fixtures/mocks, clear error messages. |
| Browser bridge creates a local attack surface | Loopback-only binding, pairing, scoped tokens, origin checks, URL validation, rate limiting, revocation. |
| Cross-platform feature mismatch | Use capability reporting and platform adapters; never label unverified features as supported. |
| Dependency licensing blocks distribution | Audit licenses before bundling, document alternatives, and avoid shipping incompatible dependencies. |

## 20. Traceability: FastDM to BestDM

| FastDM source/area | Capability to preserve | BestDM target module | Priority |
|---|---|---|---|
| `Form1.cs` — `Engine`, `DownloadItem`, main controls/state | Task lifecycle, HTTP engine, list management, preferences and history | Domain, orchestrator, task model, persistence, QML | P0 |
| `StreamEngine.cs` | HLS/DASH segment download, recovery, merge | Media adapter + segment pipeline | P2 |
| `MediaSupport.cs`, `MediaForms.cs` | Media detection, manifest parsing, quality/audio selection | Media discovery + media selection UI | P2 |
| `YtDlpSupport.cs`, `YtDlpForms.cs` | Site/media extraction, external runtime/tool checks, format selection | External tool adapter + media UI | P2 |
| `RemoteSupport.cs`, `FolderDownloadForm.cs` | FTP/SFTP, remote listing, authentication and folder selection | FTP/SFTP adapters + remote browser UI | P2 |
| `NetworkSupport.cs`, `PreferencesSupport.cs` | Speed limiter, proxy, secret storage, post-download handling, logging | Network policy + platform credential/actions services | P1/P2 |
| `SchedulerSupport.cs` | Time windows and post-download power action | Scheduler + platform services | P1 |
| `PlaylistSupport.cs` | Local/stream playlist generation and credential stripping | Playlist service | P2 |
| `Bridge/BridgeServer.cs`, `BridgeAuth.cs`, `ProtocolHandler.cs` | Paired browser bridge, link intake, token security, deep-link handling | Optional browser bridge and protocol adapter | P2 |
| `PreferencesForm.cs` | Comprehensive settings and feature toggles | QML settings pages + settings service | P1/P2 |
| `UpdateChecker.cs` | Update discovery and release information | Update service + release metadata | P2 |
| `FastDM.Extension/` | Chromium extension UX, permissions, pairing and tests | Separate extension project/folder | P2 |

This mapping is a requirement checklist, not a claim that these features already exist in BestDM.

## 21. Implementation Agent Operating Instructions

Any coding agent implementing this PRD must:

1. Inspect the complete repository and the current Git status before making changes. Do not overwrite or discard user changes.
2. Build and run the current project first, record the baseline, and preserve the supplied application icon/brand assets.
3. Read the relevant FastDM source files and current BestDM files before porting each feature. Treat FastDM behavior as a reference, not as permission to copy Windows-only code unchanged.
4. Maintain the feature matrix and implementation status. Mark a feature `Implemented` only after tests and a real workflow verify it; use `Partial`, `Planned`, or `Blocked` honestly.
5. Implement in phases, compile/test after each meaningful change, fix regressions before starting unrelated features, and avoid large unverified rewrites.
6. Ask a concise clarifying question only when a product decision is genuinely blocking. Otherwise choose a sensible, documented default and continue.
7. Never claim that a build or test passed unless the command was actually run and its result inspected.
8. Do not remove existing working capabilities silently. Record unavoidable trade-offs in the feature matrix and explain them.
9. Keep each change small enough to review, preserve clean separation between C++ core and QML UI, and avoid introducing unnecessary dependencies.
10. At the end of each work session, update `docs/IMPLEMENTATION_STATUS.md` with changes made, files touched, exact build/test results, known limitations, and the next concrete steps.

## 22. Success Metrics

Track these project-level metrics in local QA rather than collecting user behavior remotely by default:

- Percentage of P0 acceptance tests passing.
- Successful completion and resume rate in controlled test scenarios.
- Recovery correctness after force-close/restart.
- Maximum active-task count and queue correctness under load.
- UI responsiveness during concurrent transfers.
- Number of primary controls with verified behavior (target: all).
- Number of known security/privacy test failures (target: zero release blockers).
- Fresh-build and packaging reproducibility.
- Feature-matrix accuracy: no feature marked complete without an implementation and test reference.

## 23. Final Product Statement

BestDM should become a well-engineered download manager with the ease of a modern Qt Quick app and the breadth expected from a mature desktop downloader. The correct order is to make core transfers, state, persistence, and task management trustworthy first; then add advanced networking, protocol, media, browser, scheduler, and packaging capabilities as modular, testable features. A polished interface is required, but reliability and truthful feature status are the release gates.
