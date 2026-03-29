import test from "node:test";
import assert from "node:assert/strict";

import {
    isStatelessCompletionPrompt,
    sendAndWaitForSessionIdle,
    shouldReuseSessionForComplete,
} from "./copilot-sidecar.js";

function createFakeSession() {
    const handlers = new Map();
    const sentOptions = [];

    return {
        sentOptions,
        on(eventType, handler) {
            const current = handlers.get(eventType) || [];
            current.push(handler);
            handlers.set(eventType, current);
            return () => {
                const next = (handlers.get(eventType) || []).filter((candidate) => candidate !== handler);
                handlers.set(eventType, next);
            };
        },
        async send(options) {
            sentOptions.push(options);
        },
        emit(eventType, data = {}) {
            for (const handler of handlers.get(eventType) || []) {
                handler({ type: eventType, data });
            }
        },
    };
}

test("autocomplete fim prompts are treated as stateless sessions", () => {
    const messages = [
        { role: "system", content: "Local C++ semantic context" },
        {
            role: "user",
            content:
                "You are a code completion engine.\n"
                + "Return ONLY the exact text to insert at <fim_middle>.\n"
                + "<fim_prefix>\nfor\n<fim_middle>\n",
        },
    ];

    assert.equal(isStatelessCompletionPrompt(messages), true);
    assert.equal(shouldReuseSessionForComplete(messages, {}), false);
});

test("regular chat prompts still reuse sessions by default", () => {
    const messages = [
        { role: "system", content: "You are a coding assistant." },
        { role: "user", content: "Explain this diff." },
    ];

    assert.equal(isStatelessCompletionPrompt(messages), false);
    assert.equal(shouldReuseSessionForComplete(messages, {}), true);
});

test("explicit reuseSession false disables session reuse", () => {
    const messages = [{ role: "user", content: "Any prompt" }];

    assert.equal(shouldReuseSessionForComplete(messages, { reuseSession: false }), false);
});

test("zero completion timeout waits for session idle without an SDK default timeout", async () => {
    const session = createFakeSession();
    const waitPromise = sendAndWaitForSessionIdle(session, { prompt: "Hello" }, 0);

    await Promise.resolve();
    assert.deepEqual(session.sentOptions, [{ prompt: "Hello" }]);

    session.emit("assistant.message", { content: "Done" });
    setTimeout(() => {
        session.emit("session.idle");
    }, 10);

    const response = await waitPromise;
    assert.equal(response?.data?.content, "Done");
});

test("explicit completion timeout still rejects when session idle never arrives", async () => {
    const session = createFakeSession();

    await assert.rejects(
        sendAndWaitForSessionIdle(session, { prompt: "Hello" }, 5),
        /Timeout after 5ms waiting for session\.idle/,
    );
});
