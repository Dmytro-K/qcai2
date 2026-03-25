/*! Declares queued-request helpers used by the agent dock. */
#pragma once

#include "../agent_controller.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

namespace qcai2
{

/**
 * Captures one user request that can be executed later from the dock queue.
 */
struct queued_request_t
{
    /** User goal text that will be sent to the controller. */
    QString goal;

    /** Extra linked-file prompt context captured when the request was queued. */
    QString request_context;

    /** Linked file labels shown to the user for this request. */
    QStringList linked_files;

    /** File attachments stored for this request. */
    QList<file_attachment_t> attachments;

    /** Dry-run mode captured at queue time. */
    bool dry_run = true;

    /** Ask/Agent mode captured at queue time. */
    agent_controller_t::run_mode_t run_mode = agent_controller_t::run_mode_t::AGENT;

    /** Model captured at queue time. */
    QString model_name;

    /** Reasoning effort captured at queue time. */
    QString reasoning_effort;

    /** Thinking level captured at queue time. */
    QString thinking_level;

    /** Returns true when the queued request has enough data to run. */
    bool is_valid() const;

    /** Returns one concise single-line label for pending-items UI. */
    QString display_text() const;

    /** Serializes the queued request for optional persistence or tests. */
    QJsonObject to_json() const;

    /** Restores one queued request from JSON. */
    static queued_request_t from_json(const QJsonObject &obj);
};

/**
 * Simple FIFO queue for dock requests waiting to run.
 */
class request_queue_t
{
public:
    /** Appends one valid request to the tail of the queue. */
    bool enqueue(const queued_request_t &request);

    /** Removes and returns the next request, if one exists. */
    bool take_next(queued_request_t *request);

    /** Removes all queued requests. */
    void clear();

    /** Returns true when no requests are waiting. */
    bool is_empty() const;

    /** Returns how many requests are queued. */
    int size() const;

    /** Returns a snapshot of the queued requests in FIFO order. */
    QList<queued_request_t> items() const;

    /** Serializes the whole queue to JSON. */
    QJsonArray to_json() const;

    /** Restores the queue from JSON, skipping invalid entries. */
    void restore(const QJsonValue &value);

private:
    QList<queued_request_t> queued_requests;
};

}  // namespace qcai2
