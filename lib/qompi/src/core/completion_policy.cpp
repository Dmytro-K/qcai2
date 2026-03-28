/*! @file
    @brief Implements transport-agnostic completion normalization helpers.
*/

#include "qompi/completion_policy.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace qompi
{

namespace
{

std::string trim_outer_blank_lines(std::string text)
{
    for (std::size_t offset = 0; (offset = text.find("\r\n", offset)) != std::string::npos;)
    {
        text.replace(offset, 2, "\n");
    }
    std::replace(text.begin(), text.end(), '\r', '\n');

    while (text.empty() == false && text.front() == '\n')
    {
        text.erase(text.begin());
    }
    while (text.empty() == false && text.back() == '\n')
    {
        text.pop_back();
    }
    return text;
}

std::string trim_copy(std::string_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
    {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
    {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string strip_markdown_fences(std::string text)
{
    const std::string trimmed = trim_copy(text);
    if (trimmed.rfind("```", 0) != 0)
    {
        return trim_outer_blank_lines(std::move(text));
    }

    const std::size_t first_newline = trimmed.find('\n');
    if (first_newline == std::string::npos)
    {
        return {};
    }

    text = trimmed.substr(first_newline + 1);
    if (text.size() >= 3 && text.ends_with("```"))
    {
        text.resize(text.size() - 3);
    }
    return trim_outer_blank_lines(std::move(text));
}

std::string extract_fenced_block_body(const std::string &text)
{
    const std::size_t opening = text.find("```");
    if (opening == std::string::npos)
    {
        return text;
    }
    const std::size_t first_newline = text.find('\n', opening);
    if (first_newline == std::string::npos)
    {
        return text;
    }
    const std::size_t closing = text.find("```", first_newline + 1);
    if (closing == std::string::npos || closing <= first_newline + 1)
    {
        return text;
    }
    return trim_outer_blank_lines(text.substr(first_newline + 1, closing - first_newline - 1));
}

std::string extract_fim_middle_segment(std::string text)
{
    constexpr std::string_view marker = "<fim_middle>";
    const std::size_t marker_position = text.rfind(marker);
    if (marker_position == std::string::npos)
    {
        return text;
    }

    text.erase(0, marker_position + marker.size());
    return trim_outer_blank_lines(std::move(text));
}

std::string sanitize_raw_completion(std::string text)
{
    text = extract_fim_middle_segment(std::move(text));
    text = strip_markdown_fences(std::move(text));
    if (text.find("```") != std::string::npos)
    {
        text = extract_fenced_block_body(text);
    }
    return trim_outer_blank_lines(std::move(text));
}

std::string leading_whitespace(std::string_view text)
{
    std::size_t count = 0;
    while (count < text.size() && std::isspace(static_cast<unsigned char>(text[count])) != 0 &&
           text[count] != '\n')
    {
        ++count;
    }
    return std::string(text.substr(0, count));
}

std::string normalize_leading_inline_whitespace(std::string text, const std::string &line_prefix)
{
    if (trim_copy(line_prefix).empty() == true || text.empty() == true)
    {
        return text;
    }

    std::size_t whitespace_count = 0;
    while (whitespace_count < text.size() &&
           (text[whitespace_count] == ' ' || text[whitespace_count] == '\t'))
    {
        ++whitespace_count;
    }
    if (whitespace_count == 0 || whitespace_count >= text.size())
    {
        return text;
    }
    if (text[whitespace_count] == '\n' || text[whitespace_count] == '\r')
    {
        return text;
    }

    text.replace(0, whitespace_count, " ");
    return text;
}

int largest_suffix_prefix_overlap(std::string_view left, std::string_view right)
{
    const std::size_t max_overlap = std::min(left.size(), right.size());
    for (std::size_t overlap = max_overlap; overlap > 0; --overlap)
    {
        if (left.substr(left.size() - overlap) == right.substr(0, overlap))
        {
            return static_cast<int>(overlap);
        }
    }
    return 0;
}

bool is_identifier_character(char character)
{
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

std::string trailing_identifier_fragment(std::string_view line_prefix)
{
    std::size_t position = line_prefix.size();
    while (position > 0 && is_identifier_character(line_prefix[position - 1]) == true)
    {
        --position;
    }
    return std::string(line_prefix.substr(position));
}

bool is_control_flow_keyword(std::string_view fragment)
{
    static constexpr std::array<std::string_view, 8> keywords = {"if",     "else", "for", "while",
                                                                 "switch", "case", "do",  "catch"};
    return std::find(keywords.begin(), keywords.end(), fragment) != keywords.end() ||
           fragment == "return";
}

char first_non_space_character(std::string_view text)
{
    for (const char character : text)
    {
        if (std::isspace(static_cast<unsigned char>(character)) == 0)
        {
            return character;
        }
    }
    return '\0';
}

bool trim_matching_stop_word(std::string &completion, const std::vector<std::string> &stop_words)
{
    std::size_t longest_match_length = 0;
    for (const std::string &stop_word : stop_words)
    {
        if (stop_word.empty() == true || completion.size() < stop_word.size())
        {
            continue;
        }
        if (completion.ends_with(stop_word) == true)
        {
            longest_match_length = std::max(longest_match_length, stop_word.size());
        }
    }

    if (longest_match_length == 0)
    {
        return false;
    }
    completion.resize(completion.size() - longest_match_length);
    return true;
}

void append_unique_stop_word(std::vector<std::string> &stop_words, const std::string &stop_word)
{
    if (stop_word.empty() == true)
    {
        return;
    }
    if (std::find(stop_words.begin(), stop_words.end(), stop_word) != stop_words.end())
    {
        return;
    }
    stop_words.push_back(stop_word);
}

}  // namespace

completion_stop_policy_t build_completion_stop_policy(const completion_request_t &request,
                                                      const completion_options_t &options)
{
    completion_stop_policy_t stop_policy;
    stop_policy.stop_words = options.stop_words;

    const std::string line_suffix = current_line_suffix_after_cursor(request.suffix);
    if (line_suffix.empty() == false && line_suffix.size() <= 64)
    {
        append_unique_stop_word(stop_policy.stop_words, line_suffix);
    }

    return stop_policy;
}

std::string apply_completion_stop_policy(const std::string &completion,
                                         const completion_stop_policy_t &stop_policy,
                                         bool *matched_stop_word)
{
    std::string trimmed_completion = completion;
    const bool matched = stop_policy.trim_trailing_stop_words == true
                             ? trim_matching_stop_word(trimmed_completion, stop_policy.stop_words)
                             : false;
    if (matched_stop_word != nullptr)
    {
        *matched_stop_word = matched;
    }
    return trimmed_completion;
}

completion_stream_update_t aggregate_completion_stream(completion_stream_state_t &state,
                                                       const completion_request_t &request,
                                                       const std::string_view text_delta,
                                                       const completion_stop_policy_t &stop_policy)
{
    completion_stream_update_t update;
    state.accumulated_response.append(text_delta);

    update.normalized_completion =
        normalize_completion_response(request.prefix, request.suffix, state.accumulated_response);
    update.normalized_completion = apply_completion_stop_policy(
        update.normalized_completion, stop_policy, &update.matched_stop_word);

    const std::string stable_completion =
        stable_streaming_completion_prefix(update.normalized_completion);
    if (stable_completion.empty() == true || stable_completion == state.last_stable_completion)
    {
        return update;
    }

    update.has_chunk = true;
    if (stable_completion.starts_with(state.last_stable_completion) == true)
    {
        update.chunk.text_delta = stable_completion.substr(state.last_stable_completion.size());
    }
    else
    {
        update.chunk.text_delta = stable_completion;
    }
    update.chunk.stable_text = stable_completion;
    update.chunk.is_final = false;
    state.last_stable_completion = stable_completion;
    return update;
}

completion_result_t finalize_completion_response(const completion_request_t &request,
                                                 const std::string &raw_response,
                                                 const completion_stop_policy_t &stop_policy,
                                                 std::string finish_reason)
{
    bool matched_stop_word = false;
    completion_result_t result;
    result.request_id = request.request_id;
    result.text = normalize_completion_response(request.prefix, request.suffix, raw_response);
    result.text = apply_completion_stop_policy(result.text, stop_policy, &matched_stop_word);
    result.finish_reason =
        matched_stop_word == true ? std::string("stop_word") : std::move(finish_reason);
    return result;
}

std::string current_line_prefix_before_cursor(const std::string &prefix)
{
    const std::size_t last_newline = prefix.rfind('\n');
    return last_newline == std::string::npos ? prefix : prefix.substr(last_newline + 1);
}

std::string current_line_suffix_after_cursor(const std::string &suffix)
{
    const std::size_t next_newline = suffix.find('\n');
    return next_newline == std::string::npos ? suffix : suffix.substr(0, next_newline);
}

std::string normalize_completion_response(const std::string &prefix, const std::string &suffix,
                                          const std::string &raw_response)
{
    std::string completion = sanitize_raw_completion(raw_response);
    if (trim_copy(completion).empty() == true)
    {
        return {};
    }

    const std::string line_prefix = current_line_prefix_before_cursor(prefix);
    const std::string line_suffix = current_line_suffix_after_cursor(suffix);
    const std::string line_indentation = leading_whitespace(line_prefix);
    const std::string fragment = trailing_identifier_fragment(line_prefix);

    int prefix_overlap = 0;
    if (line_prefix.empty() == false && completion.starts_with(line_prefix) == true)
    {
        prefix_overlap = static_cast<int>(line_prefix.size());
    }
    else
    {
        prefix_overlap = largest_suffix_prefix_overlap(line_prefix, completion);
    }
    if (prefix_overlap > 0)
    {
        completion.erase(0, static_cast<std::size_t>(prefix_overlap));
    }

    const int suffix_overlap = largest_suffix_prefix_overlap(completion, line_suffix);
    if (suffix_overlap > 0)
    {
        const std::size_t suffix_overlap_size = static_cast<std::size_t>(suffix_overlap);
        const std::size_t trimmed_size =
            suffix_overlap_size >= completion.size() ? 0 : completion.size() - suffix_overlap_size;
        completion.resize(trimmed_size);
    }
    completion = normalize_leading_inline_whitespace(std::move(completion), line_prefix);

    const std::string trimmed_completion = trim_copy(completion);
    if (trimmed_completion.empty() == true)
    {
        return {};
    }

    if (trim_copy(line_prefix).empty() == false && prefix_overlap == 0)
    {
        if (line_indentation.empty() == false && completion.starts_with(line_indentation) == true)
        {
            return {};
        }

        if (trimmed_completion.starts_with("//") == true ||
            trimmed_completion.starts_with("/*") == true)
        {
            return {};
        }

        if (is_control_flow_keyword(fragment) == true)
        {
            const char first_character = first_non_space_character(completion);
            if (is_identifier_character(first_character) == true)
            {
                return {};
            }
        }
    }

    return completion;
}

std::string build_inline_completion_text(const std::string &prefix, const std::string &suffix,
                                         const std::string &insertion_text)
{
    return current_line_prefix_before_cursor(prefix) + insertion_text +
           current_line_suffix_after_cursor(suffix);
}

std::string stable_streaming_completion_prefix(const std::string &completion)
{
    if (completion.empty() == true)
    {
        return {};
    }

    if (is_identifier_character(completion.back()) == false)
    {
        return completion;
    }

    std::size_t stable_length = completion.size();
    while (stable_length > 0 && is_identifier_character(completion[stable_length - 1]) == true)
    {
        --stable_length;
    }

    return stable_length > 0 ? completion.substr(0, stable_length) : std::string();
}

}  // namespace qompi
