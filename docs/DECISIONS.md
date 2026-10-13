# Decisions

## 2026-10-12: Preserve the current repository and use phased implementation

The checked-out project is an early Qt foundation, not the full PRD application. The existing code and user-provided prompt/PRD files are preserved. Features will be marked complete only after implementation and verification. The current environment has no Qt 6.12 MSVC kit, so source builds cannot yet be checked here.

## 2026-10-12: Safe HTTP resume behavior

A resumed response is accepted only when the server returns HTTP 206 and Content-Range begins at the exact saved offset. If a server returns HTTP 200, restart the partial file at offset zero. A 416 response or changed validator/known total safely truncates the partial and retries once from byte zero. Malformed Content-Range responses abort with an actionable error. TLS validation remains enabled.

## 2026-10-12: Typed state names in SQLite

The domain uses `DownloadState`; SQLite continues to store stable readable state names and converts them at the repository boundary. This avoids scattering state string comparisons through new domain code while keeping the DB inspectable.


## 2026-10-12: Persist transient retry scheduling

Transient HTTP/network failures use exponential delays (2, 4, 8 seconds for the first three retries) and a user-configurable retry count from 0 to 10. A distinct Retrying state and absolute next-retry timestamp are persisted so countdowns survive restart and users can cancel a scheduled retry. Manual retries reset the automatic retry count.

## 2026-10-12: Keep redirect target metadata separate from source URL

Metadata probes retain the final validated HTTP(S) URL for inspection, while the original source URL remains the request source for retries and resume. Redirect targets are shown only in task details and use the existing sensitive-query redaction.

## 2026-10-12: Clipboard monitoring is opt-in and prompt-only

Clipboard URL detection is disabled by default, limited to a single HTTP(S) URL, and only opens the existing Add dialog after user confirmation. This avoids background queueing and keeps clipboard contents out of persistence.

## 2026-10-12: Gate segmented HTTP on validators

Parallel byte ranges require a known total, advertised range support, and an ETag or Last-Modified validator. Without all three, BestDM uses a single stream. This avoids combining ranges from different same-sized representations when the origin changes during a transfer.

## 2026-10-12: Adopt safe server filenames across extensions

For URL-derived names, a safe Content-Disposition filename may change the extension. Re-run the configured category rules and collision reservation using that name before transfer starts. Keep explicit user filenames untouched and persist the original batch destination plus the automatic-category-folder choice so an asynchronous rename can be routed consistently.

## 2026-10-12: Keep simple completion actions explicit and local

The after-download preference defaults to None. When selected, BestDM may open the finished file or its containing folder through the operating system only after a live successful completion transition. A user-selected executable can also receive the finished path as one QProcess argument, without a shell, under a two-minute timeout; running children are killed on app shutdown with a bounded wait. Queue power actions are covered below.

## 2026-10-13: Scan with installed Windows Defender before post-actions

Defender scanning is opt-in and uses the installed `MpCmdRun.exe` with its file-scan arguments, discovered from the current platform folder or legacy installation folder. Run it before file-opening or custom-program completion actions; a failed scan blocks those actions. Keep the scan local, drain process output, enforce a bounded timeout, and report generic errors without logging file contents. Windows documents an elevated command prompt for MpCmdRun, so permission failures are surfaced rather than automatically requesting elevation.

## 2026-10-13: Confirm queue power actions twice

Windows shutdown, sleep, and hibernate are opt-in and only offered after a successful completion in the current session leaves no nonterminal tasks. A confirmation dialog is followed by a 60-second countdown that the user can cancel; new work cancels the pending action. Persisted completed history alone never schedules a power action.

## 2026-10-13: Never replace an existing output during final publication

The safe default is collision-renaming. If an output name becomes occupied after queue-time reservation, finalization tries the next suffix using a no-replace OS operation. Windows uses `MoveFileW`; Unix uses atomic hard-link creation in the same destination directory and then unlinks the partial. Other platforms fail closed until an equivalent primitive is added. There is no silent overwrite mode.

## 2026-10-13: Keep origin credentials session-only

For HTTP 401 challenges, ask for a username and password through the existing app dialog, then supply them to Qt's `QAuthenticator`. Key values by task and canonical `(scheme, host, port)` origin, so a redirect to another origin triggers a separate prompt and never reuses the first origin's credentials. Keep values only in the in-memory queue for retries during the current process; do not write origin credentials to the SQLite task table, QSettings, errors, or logs. Invalid credentials require another explicit user submission and do not trigger an automatic challenge loop.

## 2026-10-13: Do not depend on the host curl executable for protocol support

The checked Windows `curl.exe` advertises FTP/FTPS but not SFTP, and it is an operating-system tool rather than a BestDM-managed dependency. Do not silently invoke it as the transfer backend or claim FTP/SFTP support based on its presence. Implement these backends only after selecting and packaging a maintained libcurl build with the required protocol features, credential handling, license notices, and supported-platform behavior; until then, the URL intake continues to state that HTTP/HTTPS are supported.

## 2026-10-13: Make overwrite opt-in and defer it until verified completion

The filename collision preference defaults to Rename. Ask lets a user review an on-disk conflict and choose rename, overwrite, or cancel. For Overwrite, keep the original file intact through transfer, validation, and flush; replace it atomically only during successful final publication. Existing task-owned paths remain reserved to keep completed history from silently referring to later content. Persist overwrite intent per task so a restart cannot change the selected behavior.
