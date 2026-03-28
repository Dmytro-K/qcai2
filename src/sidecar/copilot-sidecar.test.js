import test from "node:test";
import assert from "node:assert/strict";

import {
    isStatelessCompletionPrompt,
    shouldReuseSessionForComplete,
} from "./copilot-sidecar.js";

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
