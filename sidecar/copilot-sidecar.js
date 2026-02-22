#!/usr/bin/env node
// Qcai2 Copilot Sidecar — bridges Qt Creator plugin to GitHub Copilot SDK.
// Protocol: JSON Lines over stdin/stdout.
//
// Request:  {"id":N,"method":"start"|"complete"|"cancel"|"stop","params":{...}}
// Response: {"id":N,"result":{...}} or {"id":N,"error":"msg"}

import { CopilotClient } from "@github/copilot-sdk";
import { createInterface } from "node:readline";

let client = null;
let activeRequestId = null;
let isProcessing = false;    // true while sendAndWait is in progress

// Session cache: reuse sessions for sequential requests (same model).
// Sessions track conversation history, so multi-turn agent chats work naturally.
let session = null;
let sessionModel = null;
let sessionStreaming = null;

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

// ── Request queue ──────────────────────────────────────────────
// Readline async callbacks can overlap (await yields control).
// We serialize all requests through a queue to prevent races.
const requestQueue = [];
let processingQueue = false;

function enqueue(method, id, params) {
    // Cancel is handled immediately (synchronous, just sets flags)
    if (method === "cancel") {
        handleCancel(id, params);
        return;
    }
    requestQueue.push({ method, id, params });
    drainQueue();
}

async function drainQueue() {
    if (processingQueue) return;
    processingQueue = true;
    while (requestQueue.length > 0) {
        const { method, id, params } = requestQueue.shift();
        try {
            switch (method) {
                case "start":
                    await handleStart(id, params);
                    break;
                case "complete":
                    await handleComplete(id, params);
                    break;
                case "stop":
                    await handleStop(id);
                    break;
                default:
                    sendError(id, `Unknown method: ${method}`);
            }
        } catch (e) {
            log(`Unhandled error in ${method}: ${e.message}`);
            sendError(id, e.message);
        }
    }
    processingQueue = false;
}

// ── Handlers ───────────────────────────────────────────────────

async function handleStart(id, params) {
    try {
        log("Starting CopilotClient...");
        client = new CopilotClient();
        await client.start();
        log("CopilotClient started OK");
        sendResult(id, { status: "ready" });
    } catch (e) {
        log(`CopilotClient start failed: ${e.message}`);
        sendError(id, `Failed to start CopilotClient: ${e.message}`);
    }
}

async function handleComplete(id, params) {
    if (!client) {
        sendError(id, "Client not started. Send 'start' first.");
        return;
    }

    const { messages, model, temperature, maxTokens, streaming } = params;
    const requestedModel = model || "gpt-4.1";
    const useStreaming = streaming === true;

    isProcessing = true;
    activeRequestId = id;

    try {
        // Reuse session if model and streaming mode match
        const needNew = !session
            || sessionModel !== requestedModel
            || sessionStreaming !== useStreaming;

        if (needNew) {
            // Destroy old session if exists
            if (session) {
                try { await session.destroy(); } catch (_) {}
                session = null;
            }
            log(`Creating session: model=${requestedModel} streaming=${useStreaming}`);
            session = await client.createSession({
                model: requestedModel,
                streaming: useStreaming,
                availableTools: [],
                systemMessage: { mode: "replace", content: "" },
            });
            sessionModel = requestedModel;
            sessionStreaming = useStreaming;
            log("Session created OK");
        } else {
            log(`Reusing session: model=${requestedModel}`);
        }

        const prompt = formatMessages(messages);
        log(`Request #${id}: sending prompt (${prompt.length} chars)...`);

        if (useStreaming) {
            let fullContent = "";

            const unsubscribe = session.on("assistant.message_delta", (event) => {
                if (activeRequestId !== id) return;
                const delta = event.data?.deltaContent || "";
                if (delta) {
                    fullContent += delta;
                    send({ id, stream_delta: delta });
                }
            });

            await session.sendAndWait({ prompt }, 300000);
            unsubscribe();

            if (activeRequestId === id) {
                log(`Request #${id} streaming done: ${fullContent.length} chars`);
                sendResult(id, { content: fullContent });
            } else {
                log(`Request #${id} streaming done but stale (active=${activeRequestId})`);
            }
        } else {
            const response = await session.sendAndWait({ prompt }, 300000);
            if (activeRequestId === id) {
                const content = response?.data?.content || "";
                log(`Request #${id} done: ${content.length} chars`);
                sendResult(id, { content });
            } else {
                log(`Request #${id} done but stale (active=${activeRequestId})`);
            }
        }
    } catch (e) {
        log(`Request #${id} error: ${e.message}`);
        // Invalidate session on error
        session = null;
        sessionModel = null;
        sessionStreaming = null;
        sendError(id, `Completion error: ${e.message}`);
    } finally {
        activeRequestId = null;
        isProcessing = false;
    }
}

function formatMessages(messages) {
    if (!messages || messages.length === 0) return "";

    // If there's only one user message, return it directly
    if (messages.length === 1 && messages[0].role === "user") {
        return messages[0].content;
    }

    // For multi-turn conversations, format as structured text
    // so the model understands the conversation context
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
        activeRequestId = null;
        isProcessing = false;
        if (session) {
            try { await session.destroy(); } catch (_) {}
            session = null;
            sessionModel = null;
            sessionStreaming = null;
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

function handleCancel(id, params) {
    // Mark active request as stale (will be discarded when sendAndWait returns)
    if (activeRequestId !== null) {
        log(`Marking active request #${activeRequestId} as stale`);
        activeRequestId = null;
    }
    sendResult(id, { status: "cancelled" });
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
    enqueue(method, id, params || {});
});

rl.on("close", () => {
    // stdin closed — clean up
    if (client) client.stop().catch(() => {});
    process.exit(0);
});

// Signal readiness
send({ id: 0, result: { status: "sidecar_ready" } });
