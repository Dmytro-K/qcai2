/*! @file
    @brief Declares transport-agnostic normalization, stop-policy, and stream helpers.
*/

#pragma once

#include "completion_types.h"

#include <string>
#include <string_view>
#include <vector>

namespace qompi
{

/**
 * Stores the stop-word policy used for provider dispatch and post-processing.
 */
struct completion_stop_policy_t
{
    std::vector<std::string> stop_words;
    bool trim_trailing_stop_words = true;
};

/**
 * Stores stream-aggregation state across provider deltas.
 */
struct completion_stream_state_t
{
    std::string accumulated_response;
    std::string last_stable_completion;
};

/**
 * Stores one stream-aggregation update after ingesting a provider delta.
 */
struct completion_stream_update_t
{
    bool has_chunk = false;
    bool matched_stop_word = false;
    std::string normalized_completion;
    completion_chunk_t chunk;
};

/**
 * Builds the stop policy for one request after option resolution.
 * @param request Completion request that provides cursor context.
 * @param options Effective resolved completion options.
 * @return Stop policy used for provider dispatch and post-processing.
 */
completion_stop_policy_t build_completion_stop_policy(const completion_request_t &request,
                                                      const completion_options_t &options);

/**
 * Applies one stop policy to already normalized insertion-only completion text.
 * @param completion Normalized insertion-only completion text.
 * @param stop_policy Stop policy to apply.
 * @param matched_stop_word Optional output flag set when a trailing stop word was trimmed.
 * @return Completion text after stop-word trimming.
 */
std::string apply_completion_stop_policy(const std::string &completion,
                                         const completion_stop_policy_t &stop_policy,
                                         bool *matched_stop_word = nullptr);

/**
 * Updates stream aggregation for one provider delta.
 * @param state Persistent aggregation state for the inflight request.
 * @param request Completion request that supplies prefix/suffix context.
 * @param text_delta Raw provider delta to append.
 * @param stop_policy Stop policy used for trimming and stable-prefix extraction.
 * @return Aggregation update with an optional renderable chunk.
 */
completion_stream_update_t
aggregate_completion_stream(completion_stream_state_t &state, const completion_request_t &request,
                            std::string_view text_delta,
                            const completion_stop_policy_t &stop_policy);

/**
 * Finalizes one raw provider response into a normalized qompi completion result.
 * @param request Completion request that supplies prefix/suffix context.
 * @param raw_response Raw provider response text.
 * @param stop_policy Stop policy used during final trimming.
 * @param finish_reason Best-effort finish reason to store in the result.
 * @return Final normalized completion result.
 */
completion_result_t finalize_completion_response(const completion_request_t &request,
                                                 const std::string &raw_response,
                                                 const completion_stop_policy_t &stop_policy,
                                                 std::string finish_reason = "stop");

/**
 * Returns the current line fragment before the cursor.
 * @param prefix Full request prefix.
 * @return Text between the most recent newline and the cursor.
 */
std::string current_line_prefix_before_cursor(const std::string &prefix);

/**
 * Returns the current line fragment after the cursor.
 * @param suffix Full request suffix.
 * @return Text between the cursor and the next newline.
 */
std::string current_line_suffix_after_cursor(const std::string &suffix);

/**
 * Normalizes raw completion text into the insertion-only payload used by qompi policies.
 * @param prefix Full request prefix.
 * @param suffix Full request suffix.
 * @param raw_response Raw provider text.
 * @return Insertion-only normalized completion text.
 */
std::string normalize_completion_response(const std::string &prefix, const std::string &suffix,
                                          const std::string &raw_response);

/**
 * Expands an insertion-only completion into the full resulting current line text.
 * @param prefix Full request prefix.
 * @param suffix Full request suffix.
 * @param insertion_text Normalized insertion-only text.
 * @return Full current-line text after applying the insertion.
 */
std::string build_inline_completion_text(const std::string &prefix, const std::string &suffix,
                                         const std::string &insertion_text);

/**
 * Returns the stable completion prefix that can be rendered for one streamed chunk.
 * @param completion Current normalized insertion-only completion.
 * @return Longest insertion-only prefix that does not end mid-identifier.
 */
std::string stable_streaming_completion_prefix(const std::string &completion);

}  // namespace qompi
