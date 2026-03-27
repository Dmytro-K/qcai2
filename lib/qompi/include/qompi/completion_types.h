/*! @file
    @brief Declares transport-agnostic completion request, result, and error types.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qompi
{

/**
 * Describes how one completion request was initiated by the caller.
 */
enum class completion_trigger_kind_t
{
    AUTOMATIC,
    MANUAL,
    RETRY,
};

/**
 * Tracks the lifecycle state of one inflight completion operation.
 */
enum class completion_operation_state_t
{
    QUEUED,
    STARTED,
    STREAMING,
    COMPLETED,
    FAILED,
    CANCELLED,
};

/**
 * Stores one normalized completion request.
 */
struct completion_request_t
{
    /** Correlation identifier chosen by the caller, or empty to auto-generate one. */
    std::string request_id;

    /** Source text before the cursor. */
    std::string prefix;

    /** Source text after the cursor. */
    std::string suffix;

    /** Editor or language identifier such as `cpp`. */
    std::string language_id;

    /** Best-effort file path associated with the request. */
    std::string file_path;

    /** Ordered system context blocks prepended before the final user prompt. */
    std::vector<std::string> system_context_blocks;

    /** Final user prompt used to request the completion. */
    std::string user_prompt;

    /** Trigger source for the request. */
    completion_trigger_kind_t trigger_kind = completion_trigger_kind_t::AUTOMATIC;
};

/**
 * Stores per-request overrides that tune provider behavior without changing session state.
 */
struct completion_options_t
{
    /** Optional model identifier override. */
    std::string model;

    /** Optional thinking-level hint understood by the backend. */
    std::string thinking_level;

    /** Optional reasoning-effort hint understood by the backend. */
    std::string reasoning_effort;

    /** Optional sampling temperature override. */
    std::optional<double> temperature;

    /** Optional max output token limit override. */
    std::optional<std::int32_t> max_output_tokens;

    /** Optional transport timeout in milliseconds. */
    std::optional<std::uint32_t> timeout_ms;

    /** Optional deterministic seed override. */
    std::optional<std::uint64_t> seed;

    /** Additional stop words applied on top of provider defaults. */
    std::vector<std::string> stop_words;
};

/**
 * Represents one streamed completion update.
 */
struct completion_chunk_t
{
    /** Incremental delta emitted by the backend since the previous chunk. */
    std::string text_delta;

    /** Current stable text after normalization and aggregation. */
    std::string stable_text;

    /** Whether this chunk already corresponds to the terminal output. */
    bool is_final = false;
};

/**
 * Stores the final result of one completion operation.
 */
struct completion_result_t
{
    /** Correlation identifier of the originating request. */
    std::string request_id;

    /** Final normalized completion text. */
    std::string text;

    /** Best-effort provider finish reason, if known. */
    std::string finish_reason;
};

/**
 * Stores a surfaced completion failure.
 */
struct completion_error_t
{
    /** Implementation-defined error code. */
    int code = 0;

    /** Human-readable error message. */
    std::string message;

    /** Whether retrying the request may succeed. */
    bool is_retryable = false;

    /** Whether the failure actually represents user or lifecycle cancellation. */
    bool is_cancellation = false;
};

}  // namespace qompi
