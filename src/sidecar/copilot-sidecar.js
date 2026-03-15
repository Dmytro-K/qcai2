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

// ── Handlers ───────────────────────────────────────────────────

let clientStarting = null; // promise for in-progress start

async function ensureClient() {
    if (client) return;
    if (clientStarting) { await clientStarting; return; }
    clientStarting = (async () => {
        log("Starting CopilotClient...");
        client = new CopilotClient({logLevel: "debug", cliArgs: ["--log-dir", "/tmp/qcai2/log"]});
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

    const { messages, model, temperature, maxTokens, streaming, reasoningEffort } = params;
    const requestedModel = model || "gpt-4.1";

    const state = { cancelled: false, session: null };
    activeRequests.set(id, state);

    let session = null;
    try {
        log(`Request #${id}: creating session model=${requestedModel} reasoningEffort=${reasoningEffort || "default"}`);
        const sessionOpts = {
            model: requestedModel,
            streaming: true,
            availableTools: [],
            systemMessage: { mode: "replace", content: "" },
        };
        if (reasoningEffort)
            sessionOpts.reasoningEffort = reasoningEffort;
        session = await client.createSession(sessionOpts);
        state.session = session;

        if (state.cancelled) {
            log(`Request #${id} cancelled during session creation`);
            return;
        }

        const prompt = formatMessages(messages);
        log(`Request #${id}: sending prompt (${prompt.length} chars), active=${activeRequests.size}`);

        const seenEventIds = new Set();
        let latestUsage = undefined;
        const unsubscribe = session.on("assistant.message_delta", (event) => {
            if (state.cancelled) return;
            // Deduplicate — SDK may dispatch the same event twice
            if (event.id && seenEventIds.has(event.id)) return;
            if (event.id) seenEventIds.add(event.id);
            const delta = event.data?.deltaContent || "";
            if (delta && streaming) {
                send({ id, stream_delta: delta });
            }
        });
        const unsubscribeUsage = session.on("assistant.usage", (event) => {
            if (state.cancelled) return;
            const usage = normalizeUsage(event.data);
            if (usage) {
                latestUsage = usage;
                log(`Request #${id} usage: ${JSON.stringify(usage)}`);
            }
        });

        const response = await session.sendAndWait({ prompt }, 300000);
        unsubscribe();
        unsubscribeUsage();

        if (!state.cancelled) {
            const content = response?.data?.content || "";
            const usage = latestUsage || normalizeUsage(
                response?.data?.usage || response?.usage || response?.usageMetadata || null,
            );
            log(`Request #${id} done: ${content.length} chars`);
            sendResult(id, usage ? { content, usage } : { content });
        } else {
            log(`Request #${id} done but was cancelled`);
        }
    } catch (e) {
        if (!state.cancelled) {
            log(`Request #${id} error: ${e.message}`);
            sendError(id, `Completion error: ${e.message}`);
        }
    } finally {
        activeRequests.delete(id);
        if (session) {
            try { await session.destroy(); } catch (_) {}
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

function formatMessages(messages) {
    if (!messages || messages.length === 0) return "";

    if (messages.length === 1 && messages[0].role === "user") {
        return messages[0].content;
    }

    const parts = [];
    for (const msg of messages) {
        const role = msg.role || "user";
        if (role === "system") {
            parts.push(`[System Instructions]\n${msg.content}\n`);
        } else if (role === "assistant") {
            parts.push(`[Previous Assistant Response]\n${msg.content}\n`);
        } else {
            parts.push(`[User]\n${msg.content}\n`);
        }
    }
    return parts.join("\n");
}

async function handleStop(id) {
    try {
        for (const [, state] of activeRequests) {
            state.cancelled = true;
        }
        activeRequests.clear();

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

function handleCancel(id, params) {
    const targetId = params?.requestId;
    if (targetId !== undefined) {
        const state = activeRequests.get(targetId);
        if (state) {
            state.cancelled = true;
            log(`Cancelled request #${targetId}`);
            // Destroy the session to abort the pending API call
            if (state.session) {
                state.session.destroy().catch(() => {});
                state.session = null;
            }
        }
    } else {
        // Cancel all active requests
        for (const [reqId, state] of activeRequests) {
            state.cancelled = true;
            log(`Cancelled request #${reqId}`);
            if (state.session) {
                state.session.destroy().catch(() => {});
                state.session = null;
            }
        }
    }
    sendResult(id, { status: "cancelled" });
}

// ── Dispatch ───────────────────────────────────────────────────

function dispatch(method, id, params) {
    switch (method) {
        case "cancel":
            handleCancel(id, params);
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
