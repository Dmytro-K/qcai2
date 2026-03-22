#!/usr/bin/env node
// Qcai2 Copilot Sidecar — bridges Qt Creator plugin to GitHub Copilot SDK.
// Protocol: JSON Lines over stdin/stdout.
//
// Request:  {"id":N,"method":"start"|"complete"|"cancel"|"stop","params":{...}}
// Response: {"id":N,"result":{...}} or {"id":N,"error":"msg"}
// Streaming: {"id":N,"stream_delta":"text"}

import { CopilotClient } from "@github/copilot-sdk";

let client = null;

// Active requests: id -> { cancelled: false, session: null }
const activeRequests = new Map();
let reusableSession = null;
let reusableSessionLock = Promise.resolve();

function send(obj) {
    process.stdout.write(JSON.stringify(obj) + "\n");
}

function log(msg) {
    process.stderr.write(`[sidecar] ${msg}\n`);
}

function sendResult(id, result) {
    send({ id, result });
}

function sendError(id, message) {
    send({ id, error: message });
}

function sendProgress(id, kind, rawType, extra = {}) {
    send({ id, progress: { kind, rawType, ...extra } });
}

function firstNonNull(...values) {
    for (const value of values) {
        if (typeof value === "number" && Number.isFinite(value)) {
            return value;
        }
    }
    return undefined;
}

function normalizeUsage(usage) {
    if (!usage || typeof usage !== "object") {
        return undefined;
    }

    const normalized = {};
    const inputTokens = firstNonNull(
        usage.input_tokens,
        usage.prompt_tokens,
        usage.inputTokens,
    );
    const outputTokens = firstNonNull(
        usage.output_tokens,
        usage.completion_tokens,
        usage.outputTokens,
    );
    const totalTokens = firstNonNull(
        usage.total_tokens,
        usage.totalTokens,
    );
    const reasoningTokens = firstNonNull(
        usage.reasoning_tokens,
        usage.output_tokens_details?.reasoning_tokens,
        usage.completion_tokens_details?.reasoning_tokens,
    );
    const cachedInputTokens = firstNonNull(
        usage.cached_input_tokens,
        usage.input_tokens_details?.cached_tokens,
        usage.prompt_tokens_details?.cached_tokens,
        usage.cacheReadTokens,
    );

    if (inputTokens !== undefined) normalized.input_tokens = inputTokens;
    if (outputTokens !== undefined) normalized.output_tokens = outputTokens;
    if (totalTokens !== undefined) normalized.total_tokens = totalTokens;
    if (reasoningTokens !== undefined) normalized.reasoning_tokens = reasoningTokens;
    if (cachedInputTokens !== undefined) normalized.cached_input_tokens = cachedInputTokens;

    return Object.keys(normalized).length > 0 ? normalized : undefined;
}

function buildSessionKey(sessionOpts) {
    return JSON.stringify({
        model: sessionOpts.model || "",
        reasoningEffort: sessionOpts.reasoningEffort || "",
        maxOutputTokens: sessionOpts.maxOutputTokens ?? null,
        systemMessage: sessionOpts.systemMessage?.content || "",
    });
}

function isSessionBusy(session) {
    for (const state of activeRequests.values()) {
        if (!state.cancelled && state.session === session) {
            return true;
        }
    }
    return false;
}

async function destroySessionSafely(session, reason) {
    try {
        await session.destroy();
        log(`${reason}: session destroyed`);
    } catch (e) {
        log(`${reason}: session destroy failed: ${e.message}`);
    }
}

async function withReusableSessionLock(action) {
    const previousLock = reusableSessionLock;
    let releaseLock = () => {};
    reusableSessionLock = new Promise((resolve) => {
        releaseLock = resolve;
    });

    await previousLock;
    try {
        return await action();
    } finally {
        releaseLock();
    }
}

async function invalidateReusableSession(reason, session = reusableSession?.session) {
    await withReusableSessionLock(async () => {
        if (!reusableSession || reusableSession.session !== session) {
            return;
        }

        if (isSessionBusy(session)) {
            reusableSession.invalidated = true;
            reusableSession.invalidatedReason = reason;
            return;
        }

        reusableSession = null;
        await destroySessionSafely(session, reason);
    });
}

async function acquireSession(sessionOpts) {
    return await withReusableSessionLock(async () => {
        const sessionKey = buildSessionKey(sessionOpts);
        if (reusableSession && reusableSession.invalidated && !isSessionBusy(reusableSession.session)) {
            const reason = reusableSession.invalidatedReason || "dropping invalidated reusable session";
            const oldSession = reusableSession.session;
            reusableSession = null;
            await destroySessionSafely(oldSession, reason);
        }

        if (reusableSession && reusableSession.key === sessionKey && !reusableSession.invalidated
            && !isSessionBusy(reusableSession.session)) {
            log(`Reusing session ${reusableSession.session.sessionId}`);
            return {
                session: reusableSession.session,
                reusable: true,
                created: false,
                resumed: false,
                previousDynamicSystemContents: [...(reusableSession.dynamicSystemContents || [])],
            };
        }

        if (reusableSession && reusableSession.key !== sessionKey && !isSessionBusy(reusableSession.session)) {
            const oldSession = reusableSession.session;
            const oldSessionId = oldSession.sessionId;
            const oldKey = reusableSession.key;
            try {
                const resumedSession = await client.resumeSession(oldSessionId, {
                    ...sessionOpts,
                    disableResume: true,
                });
                reusableSession = {
                    key: sessionKey,
                    session: resumedSession,
                    invalidated: false,
                    invalidatedReason: "",
                    dynamicSystemContents: [...(reusableSession?.dynamicSystemContents || [])],
                };
                log(`Resumed reusable session ${oldSessionId} from key=${oldKey} to key=${sessionKey}`);
                return {
                    session: resumedSession,
                    reusable: true,
                    created: false,
                    resumed: true,
                    previousDynamicSystemContents: [...(reusableSession.dynamicSystemContents || [])],
                };
            } catch (e) {
                log(`Resume failed for reusable session ${oldSessionId}: ${e.message}`);
                reusableSession = null;
                await destroySessionSafely(oldSession, `resume failed for reusable session ${oldSessionId}`);
            }
        }

        if (!reusableSession) {
            const session = await client.createSession(sessionOpts);
            reusableSession = {
                key: sessionKey,
                session,
                invalidated: false,
                invalidatedReason: "",
                dynamicSystemContents: [],
            };
            log(`Created reusable session ${session.sessionId}`);
            return {
                session,
                reusable: true,
                created: true,
                resumed: false,
                previousDynamicSystemContents: [],
            };
        }

        const session = await client.createSession(sessionOpts);
        log(`Created ephemeral session ${session.sessionId} because reusable session is busy`);
        return {
            session,
            reusable: false,
            created: true,
            resumed: false,
            previousDynamicSystemContents: [],
        };
    });
}

// ── Handlers ───────────────────────────────────────────────────

let clientStarting = null; // promise for in-progress start

async function ensureClient() {
    if (client) return;
    if (clientStarting) { await clientStarting; return; }
    clientStarting = (async () => {
        log("Starting CopilotClient...");
        client = new CopilotClient({
            logLevel: "debug",
            cliArgs: ["--log-dir", "/tmp/qcai2/log"],
            enableCacheControl: true,
        });
        await client.start();
        log("CopilotClient started OK");
    })();
    await clientStarting;
    clientStarting = null;
}

async function handleStart(id) {
    try {
        await ensureClient();
        sendResult(id, { status: "ready" });
    } catch (e) {
        log(`CopilotClient start failed: ${e.message}`);
        sendError(id, `Failed to start CopilotClient: ${e.message}`);
    }
}

async function handleComplete(id, params) {
    try {
        await ensureClient();
    } catch (e) {
        sendError(id, `Client not ready: ${e.message}`);
        return;
    }

    const {
        messages,
        model,
        temperature,
        maxTokens,
        max_tokens,
        streaming,
        reasoningEffort: reasoning_effort,
    } = params;
    const requestedModel = model || "gpt-5.4";
    const max_output_tokens = Number.isFinite(maxTokens)
        ? Math.max(1, Math.trunc(maxTokens))
        : (Number.isFinite(max_tokens) ? Math.max(1, Math.trunc(max_tokens)) : undefined);
    const completionTimeoutSec = Number.isFinite(params?.sessionIdleTimeoutSec)
        ? Math.max(0, Math.trunc(params.sessionIdleTimeoutSec))
        : (Number.isFinite(params?.completionTimeoutSec)
            ? Math.max(0, Math.trunc(params.completionTimeoutSec))
            : 1200);

    const state = { cancelled: false, session: null, reusable: false, invalidateReusable: false };
    activeRequests.set(id, state);
    sendProgress(id, "request_started", "request.started");

    let session = null;
    try {
        const sessionOpts = {
            model: requestedModel,
            streaming: true,
            availableTools: [],
        };
        if (reasoning_effort)
            sessionOpts.reasoningEffort = reasoning_effort;
        if (max_output_tokens !== undefined)
            sessionOpts.maxOutputTokens = max_output_tokens;
        const { systemMessage } = splitMessagesForSession(messages);
        if (systemMessage) {
            sessionOpts.systemMessage = { mode: "replace", content: systemMessage };
        }
        log(`Request #${id}: acquiring session model=${requestedModel} reasoning_effort=${reasoning_effort || "default"} session_idle_timeout=${completionTimeoutSec > 0 ? `${completionTimeoutSec}s` : "none"}`);
        const sessionLease = await acquireSession(sessionOpts);
        session = sessionLease.session;
        state.session = session;
        state.reusable = sessionLease.reusable;

        if (state.cancelled) {
            log(`Request #${id} cancelled during session acquisition`);
            if (state.reusable) {
                state.invalidateReusable = true;
            }
            return;
        }

        const sessionAcquisitionMode = sessionLease.created
            ? "new"
            : (sessionLease.resumed ? "resumed" : "reused");
        const {
            prompt,
            mode: promptMode,
            currentDynamicSystemContents,
        } = splitMessagesForSession(messages, {
            incremental: sessionLease.created === false,
            previousDynamicSystemContents: sessionLease.previousDynamicSystemContents,
        });
        log(`Request #${id}: sending prompt (${prompt.length} chars), active=${activeRequests.size}, session=${session.sessionId}, acquisition=${sessionAcquisitionMode}, prompt_mode=${promptMode}`);

        const seenEventIds = new Set();
        let latestUsage = undefined;
        const unsubscribe = session.on("assistant.message_delta", (event) => {
            if (state.cancelled) return;
            // Deduplicate — SDK may dispatch the same event twice
            if (event.id && seenEventIds.has(event.id)) return;
            if (event.id) seenEventIds.add(event.id);
            const delta = event.data?.deltaContent || "";
            if (delta && streaming) {
                sendProgress(id, "message_delta", "assistant.message_delta", { message: delta });
                send({ id, stream_delta: delta });
            }
        });
        const unsubscribeToolStart = session.on("tool.execution_start", (event) => {
            if (state.cancelled) return;
            const toolName = event.data?.toolName || event.data?.tool?.name || event.data?.name || "";
            sendProgress(id, "tool_started", "tool.execution_start", { toolName });
        });
        const unsubscribeToolComplete = session.on("tool.execution_complete", (event) => {
            if (state.cancelled) return;
            const toolName = event.data?.toolName || event.data?.tool?.name || event.data?.name || "";
            sendProgress(id, "tool_completed", "tool.execution_complete", { toolName });
        });
        const unsubscribeIdle = session.on("session.idle", () => {
            if (state.cancelled) return;
            sendProgress(id, "idle", "session.idle");
        });
        const unsubscribeSessionError = session.on("session.error", (event) => {
            if (state.cancelled) return;
            sendProgress(id, "error", "session.error", { message: event.data?.message || "" });
        });
        const unsubscribeUsage = session.on("assistant.usage", (event) => {
            if (state.cancelled) return;
            const usage = normalizeUsage(event.data);
            if (usage) {
                latestUsage = usage;
                log(`Request #${id} usage: ${JSON.stringify(usage)}`);
            }
        });

        const response = completionTimeoutSec > 0
            ? await session.sendAndWait({ prompt }, completionTimeoutSec * 1000)
            : await session.sendAndWait({ prompt });
        unsubscribe();
        unsubscribeToolStart();
        unsubscribeToolComplete();
        unsubscribeIdle();
        unsubscribeSessionError();
        unsubscribeUsage();

        if (!state.cancelled) {
            const content = response?.data?.content || "";
            const usage = latestUsage || normalizeUsage(
                response?.data?.usage || response?.usage || response?.usageMetadata || null,
            );
            if (state.reusable) {
                await updateReusableSessionDynamicContext(session, currentDynamicSystemContents);
            }
            log(`Request #${id} done: ${content.length} chars`);

            // Fetch quota (non-blocking: ignore errors, don't delay response)
            let quota = undefined;
            try {
                const quotaResult = await client.rpc.account.getQuota();
                const snap = quotaResult?.quotaSnapshots?.premium_interactions;
                if (snap && typeof snap.remainingPercentage === "number") {
                    quota = {
                        remaining_percentage: snap.remainingPercentage,
                        used_requests: snap.usedRequests ?? -1,
                        entitlement_requests: snap.entitlementRequests ?? -1,
                        overage: snap.overage ?? 0,
                        reset_date: snap.resetDate ?? null,
                    };
                    log(`Request #${id} quota: ${JSON.stringify(quota)}`);
                }
            } catch (qe) {
                log(`Request #${id} quota fetch skipped: ${qe.message}`);
            }

            sendProgress(id, "response_completed", "response.completed");
            const result = { content };
            if (usage) result.usage = usage;
            if (quota) result.quota = quota;
            sendResult(id, result);
        } else {
            log(`Request #${id} done but was cancelled`);
        }
    } catch (e) {
        if (!state.cancelled) {
            log(`Request #${id} error: ${e.message}`);
            sendProgress(id, "error", "response.error", { message: e.message });
            sendError(id, `Completion error: ${e.message}`);
        }
        if (state.reusable) {
            state.invalidateReusable = true;
        }
    } finally {
        activeRequests.delete(id);
        if (state.reusable && state.invalidateReusable && session) {
            await invalidateReusableSession(`invalidating reusable session after request #${id}`, session);
        }
        if (!state.reusable && session) {
            await destroySessionSafely(session, `cleaning up ephemeral session after request #${id}`);
        }
    }
}

async function handleListModels(id) {
    try {
        await ensureClient();
        const models = await client.listModels();
        sendResult(id, { models: models.map(model => model.id) });
    } catch (e) {
        log(`Request #${id} list_models error: ${e.message}`);
        sendError(id, `Model listing error: ${e.message}`);
    }
}

function isDynamicSystemMessage(content) {
    if (typeof content !== "string" || content.length === 0) {
        return false;
    }

    return content.startsWith("Current editor context:\n")
        || content.startsWith("File contents (from open tabs):\n")
        || content.startsWith("Linked files:\n")
        || content.includes("\nUse these linked files as explicit request context.\n")
        || /^Use \S+ thinking depth for this task\.$/.test(content.trim());
}

async function updateReusableSessionDynamicContext(session, dynamicSystemContents) {
    await withReusableSessionLock(async () => {
        if (!reusableSession || reusableSession.session !== session) {
            return;
        }

        reusableSession.dynamicSystemContents = [...dynamicSystemContents];
    });
}

function buildPromptFromParts(parts) {
    if (parts.length === 0) {
        return "";
    }

    if (parts.length === 1 && parts[0].role === "user") {
        return parts[0].content;
    }

    return parts.map((part) => {
        if (part.role === "system") {
            return `[System Context]\n${part.content}\n`;
        }
        if (part.role === "assistant") {
            return `[Previous Assistant Response]\n${part.content}\n`;
        }
        return `[User]\n${part.content}\n`;
    }).join("\n");
}

function splitMessagesForSession(messages, options = {}) {
    const incremental = options.incremental === true;
    const previousDynamicSystemContents = Array.isArray(options.previousDynamicSystemContents)
        ? options.previousDynamicSystemContents
        : [];
    if (!messages || messages.length === 0) {
        return {
            systemMessage: "",
            prompt: "",
            mode: "empty",
            currentDynamicSystemContents: [],
        };
    }

    let systemMessage = "";
    let primarySystemConsumed = false;
    const fullParts = [];
    const dynamicSystemParts = [];
    for (const msg of messages) {
        const role = msg.role || "user";
        const content = typeof msg.content === "string" ? msg.content : "";
        if (!primarySystemConsumed && role === "system") {
            systemMessage = content;
            primarySystemConsumed = true;
            continue;
        }

        if (role === "system") {
            fullParts.push({ role, content });
            if (isDynamicSystemMessage(content) === true) {
                dynamicSystemParts.push({ role, content });
            }
            continue;
        }

        fullParts.push({ role, content });
    }

    const nonSystemMessages = messages.filter(msg => (msg.role || "user") !== "system");
    if (!systemMessage && nonSystemMessages.length === 1 && (nonSystemMessages[0].role || "user") === "user") {
        return {
            systemMessage: "",
            prompt: nonSystemMessages[0].content,
            mode: "single_user",
            currentDynamicSystemContents: dynamicSystemParts.map((part) => part.content),
        };
    }

    if (incremental === true) {
        const changedDynamicSystemParts = dynamicSystemParts.filter((part) => {
            return previousDynamicSystemContents.includes(part.content) === false;
        });
        let lastAssistantIndex = -1;
        for (let i = messages.length - 1; i >= 0; --i) {
            if ((messages[i].role || "user") === "assistant") {
                lastAssistantIndex = i;
                break;
            }
        }

        const incrementalConversationParts = [];
        for (let i = lastAssistantIndex + 1; i < messages.length; ++i) {
            const role = messages[i].role || "user";
            const content = typeof messages[i].content === "string" ? messages[i].content : "";
            if (role === "system" || content.length === 0) {
                continue;
            }
            incrementalConversationParts.push({ role, content });
        }

        if (incrementalConversationParts.length === 0) {
            const fallbackUser = [...messages].reverse().find((msg) => {
                return (msg.role || "user") === "user"
                    && typeof msg.content === "string"
                    && msg.content.length > 0;
            });
            if (fallbackUser) {
                incrementalConversationParts.push({ role: "user", content: fallbackUser.content });
            }
        }

        if (incrementalConversationParts.length > 0) {
            const prompt = buildPromptFromParts([...changedDynamicSystemParts, ...incrementalConversationParts]);
            return {
                systemMessage,
                prompt,
                mode: changedDynamicSystemParts.length > 0
                    ? "incremental_dynamic"
                    : "incremental_user_only",
                currentDynamicSystemContents: dynamicSystemParts.map((part) => part.content),
            };
        }
    }

    if (fullParts.length === 1 && nonSystemMessages.length === 1 && (nonSystemMessages[0].role || "user") === "user") {
        return {
            systemMessage,
            prompt: nonSystemMessages[0].content,
            mode: "single_user",
            currentDynamicSystemContents: dynamicSystemParts.map((part) => part.content),
        };
    }

    return {
        systemMessage,
        prompt: buildPromptFromParts(fullParts),
        mode: "full",
        currentDynamicSystemContents: dynamicSystemParts.map((part) => part.content),
    };
}

async function handleStop(id) {
    try {
        for (const [, state] of activeRequests) {
            state.cancelled = true;
        }
        activeRequests.clear();

        if (reusableSession) {
            const session = reusableSession.session;
            reusableSession = null;
            await destroySessionSafely(session, "stopping sidecar");
        }

        if (client) {
            await client.stop();
            client = null;
        }
        sendResult(id, { status: "stopped" });
    } catch (e) {
        sendError(id, `Stop error: ${e.message}`);
    }
    process.exit(0);
}

async function handleCancel(id, params) {
    const targetId = params?.requestId;
    if (targetId !== undefined) {
        const state = activeRequests.get(targetId);
        if (state) {
            state.cancelled = true;
            log(`Cancelled request #${targetId}`);
            if (state.session) {
                if (state.reusable) {
                    state.invalidateReusable = true;
                }
                state.session.abort().catch((error) => {
                    log(`Abort failed for request #${targetId}: ${error.message}`);
                });
            }
        }
    } else {
        // Cancel all active requests
        for (const [reqId, state] of activeRequests) {
            state.cancelled = true;
            log(`Cancelled request #${reqId}`);
            if (state.session) {
                if (state.reusable) {
                    state.invalidateReusable = true;
                }
                state.session.abort().catch((error) => {
                    log(`Abort failed for request #${reqId}: ${error.message}`);
                });
            }
        }
    }
    sendResult(id, { status: "cancelled" });
}

// ── Dispatch ───────────────────────────────────────────────────

function dispatch(method, id, params) {
    switch (method) {
        case "cancel":
            handleCancel(id, params).catch(e => {
                log(`Unhandled error in cancel: ${e.message}`);
                sendError(id, e.message);
            });
            break;
        case "start":
            handleStart(id).catch(e => {
                log(`Unhandled error in start: ${e.message}`);
                sendError(id, e.message);
            });
            break;
        case "complete":
            handleComplete(id, params).catch(e => {
                log(`Unhandled error in complete #${id}: ${e.message}`);
                sendError(id, e.message);
            });
            break;
        case "list_models":
            handleListModels(id).catch(e => {
                log(`Unhandled error in list_models #${id}: ${e.message}`);
                sendError(id, e.message);
            });
            break;
        case "stop":
            handleStop(id).catch(e => {
                log(`Unhandled error in stop: ${e.message}`);
                sendError(id, e.message);
            });
            break;
        default:
            sendError(id, `Unknown method: ${method}`);
    }
}

// ── Main loop ──────────────────────────────────────────────────
// Use a brace-depth JSON parser instead of readline to handle payloads
// that may contain embedded newlines (e.g. from Qt's toJson quirks).

let inputBuffer = "";

/**
 * Scans the buffer for a complete top-level JSON object by tracking brace
 * depth and string escaping.  Returns the index of the closing '}' or -1.
 */
function findJsonEnd(buf) {
    let depth = 0;
    let inString = false;
    let escape = false;
    for (let i = 0; i < buf.length; i++) {
        const c = buf[i];
        if (escape) { escape = false; continue; }
        if (inString) {
            if (c === "\\") { escape = true; continue; }
            if (c === '"') inString = false;
            continue;
        }
        if (c === '"') { inString = true; continue; }
        if (c === "{") depth++;
        else if (c === "}") {
            depth--;
            if (depth === 0) return i;
        }
    }
    return -1;
}

function processBuffer() {
    while (true) {
        // Skip leading whitespace / newlines between messages
        const startIdx = inputBuffer.search(/\S/);
        if (startIdx < 0) { inputBuffer = ""; return; }
        if (startIdx > 0) inputBuffer = inputBuffer.substring(startIdx);
        if (inputBuffer[0] !== "{") {
            // Junk before JSON — discard up to next '{'
            const braceIdx = inputBuffer.indexOf("{");
            if (braceIdx < 0) { inputBuffer = ""; return; }
            inputBuffer = inputBuffer.substring(braceIdx);
        }

        const end = findJsonEnd(inputBuffer);
        if (end < 0) return; // incomplete object, wait for more data

        const jsonStr = inputBuffer.substring(0, end + 1);
        inputBuffer = inputBuffer.substring(end + 1);

        log(`Received complete JSON: ${jsonStr.length} bytes`);
        let req;
        try {
            req = JSON.parse(jsonStr);
        } catch (e) {
            sendError(-1, `Invalid JSON: ${e.message}`);
            continue;
        }

        const { id, method, params } = req;
        dispatch(method, id, params || {});
    }
}

process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => {
    inputBuffer += chunk;
    processBuffer();
});

process.stdin.on("end", () => {
    if (client) client.stop().catch(() => {});
    process.exit(0);
});

// Signal readiness
send({ id: 0, result: { status: "sidecar_ready" } });
