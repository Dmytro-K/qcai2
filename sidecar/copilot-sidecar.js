#!/usr/bin/env node
// Qcai2 Copilot Sidecar — bridges Qt Creator plugin to GitHub Copilot SDK.
// Protocol: JSON Lines over stdin/stdout.
//
// Request:  {"id":N,"method":"start"|"complete"|"cancel"|"stop","params":{...}}
// Response: {"id":N,"result":{...}} or {"id":N,"error":"msg"}
// Streaming: {"id":N,"stream_delta":"text"}

import { CopilotClient } from "@github/copilot-sdk";
import { createInterface } from "node:readline";

let client = null;

// Active requests: id -> { cancelled: false }
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

    const state = { cancelled: false };
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

        if (state.cancelled) {
            log(`Request #${id} cancelled during session creation`);
            return;
        }

        const prompt = formatMessages(messages);
        log(`Request #${id}: sending prompt (${prompt.length} chars), active=${activeRequests.size}`);

        const seenEventIds = new Set();
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

        const response = await session.sendAndWait({ prompt }, 300000);
        unsubscribe();

        if (!state.cancelled) {
            const content = response?.data?.content || "";
            log(`Request #${id} done: ${content.length} chars`);
            sendResult(id, { content });
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
        }
    } else {
        // Cancel all active requests
        for (const [reqId, state] of activeRequests) {
            state.cancelled = true;
            log(`Cancelled request #${reqId}`);
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
const rl = createInterface({ input: process.stdin, terminal: false });

rl.on("line", (line) => {
    const trimmed = line.trim();
    if (!trimmed) return;

    let req;
    try {
        req = JSON.parse(trimmed);
    } catch (e) {
        sendError(-1, `Invalid JSON: ${e.message}`);
        return;
    }

    const { id, method, params } = req;
    dispatch(method, id, params || {});
});

rl.on("close", () => {
    if (client) client.stop().catch(() => {});
    process.exit(0);
});

// Signal readiness
send({ id: 0, result: { status: "sidecar_ready" } });
