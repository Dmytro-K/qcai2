/*! Declares the Qt state machine that tracks the high-level lifecycle of one agent run. */
#pragma once

#include <QObject>
#include <QString>

#include <cstdint>

class QState;
class QStateMachine;

namespace qcai2
{

/**
 * Encapsulates the controller's high-level run lifecycle as a Qt state machine.
 *
 * This type owns the allowed phase transitions for one agent run so callers do not
 * need to coordinate multiple boolean flags manually. It models only the coarse
 * execution phase; counters, messages, approvals, and other run data stay in
 * `agent_controller_t`.
 */
class agent_run_state_machine_t : public QObject
{
    Q_OBJECT
public:
    /**
     * High-level execution phases for a single controller run.
     */
    enum class state_t : std::uint8_t
    {
        /** No run is currently active. */
        IDLE,
        /** A new run is being initialized before the first provider request. */
        STARTING,
        /** The controller is waiting for the provider to produce the next response. */
        WAITING_FOR_PROVIDER,
        /** The controller is blocked on an explicit user approval decision. */
        WAITING_FOR_APPROVAL,
        /** The controller is waiting for the user to review a proposed inline diff. */
        WAITING_FOR_INLINE_DIFF_REVIEW,
        /** The run finished successfully. */
        COMPLETED,
        /** The run ended because of an error or safety limit. */
        FAILED,
        /** The run was stopped explicitly by the user. */
        STOPPED
    };
    Q_ENUM(state_t)

    /**
     * Creates the run state machine and starts it in the `IDLE` state.
     * @param parent Owning QObject.
     */
    explicit agent_run_state_machine_t(QObject *parent = nullptr);

    /**
     * Destroys the state machine and its owned Qt states.
     */
    ~agent_run_state_machine_t() override;

    /**
     * Returns the current lifecycle phase.
     * @return Current high-level run state.
     */
    state_t current_state() const;

    /**
     * Returns the current lifecycle phase as a stable machine-readable name.
     * @return Current state identifier such as `idle` or `waiting_for_provider`.
     */
    QString current_state_name() const;

    /**
     * Reports whether the controller should be considered mid-run.
     * @return True for active non-terminal phases.
     */
    bool is_running() const;

    /**
     * Reports whether the current phase is waiting on a provider response.
     * @return True only in `WAITING_FOR_PROVIDER`.
     */
    bool is_waiting_for_provider() const;

    /**
     * Reports whether the current phase is waiting on a user approval choice.
     * @return True only in `WAITING_FOR_APPROVAL`.
     */
    bool is_waiting_for_approval() const;

    /**
     * Reports whether the current phase is waiting on inline diff review.
     * @return True only in `WAITING_FOR_INLINE_DIFF_REVIEW`.
     */
    bool is_waiting_for_inline_diff_review() const;

    /**
     * Enters the start of a new run.
     */
    void start_run();

    /**
     * Enters the phase that waits for the provider's next response.
     */
    void await_provider_response();

    /**
     * Enters the phase that waits for a user approval decision.
     */
    void await_user_approval();

    /**
     * Enters the phase that waits for inline diff review.
     */
    void await_inline_diff_review();

    /**
     * Marks the run as completed successfully.
     */
    void mark_completed();

    /**
     * Marks the run as failed.
     */
    void mark_failed();

    /**
     * Marks the run as stopped by the user.
     */
    void mark_stopped();

signals:
    /**
     * Emitted after the current lifecycle phase changes.
     * @param state New lifecycle phase.
     */
    void state_changed(state_t state);

private:
    /**
     * Flushes queued Qt state machine events so synchronous callers observe the new state.
     */
    void flush_state_machine_events();

    /**
     * Updates the cached state value and emits `state_changed` when needed.
     * @param state Newly entered phase.
     */
    void set_current_state(state_t state);

signals:
    /** Internal trigger used to request transition into `STARTING`. */
    void start_run_requested();
    /** Internal trigger used to request transition into `WAITING_FOR_PROVIDER`. */
    void provider_response_wait_requested();
    /** Internal trigger used to request transition into `WAITING_FOR_APPROVAL`. */
    void user_approval_wait_requested();
    /** Internal trigger used to request transition into `WAITING_FOR_INLINE_DIFF_REVIEW`. */
    void inline_diff_review_wait_requested();
    /** Internal trigger used to request transition into `COMPLETED`. */
    void run_completed_requested();
    /** Internal trigger used to request transition into `FAILED`. */
    void run_failed_requested();
    /** Internal trigger used to request transition into `STOPPED`. */
    void run_stopped_requested();

private:
    /** Backing Qt state machine that owns and drives the lifecycle states. */
    QStateMachine *state_machine = nullptr;
    /** Idle phase state object. */
    QState *idle_state = nullptr;
    /** Starting phase state object. */
    QState *starting_state = nullptr;
    /** Waiting-for-provider phase state object. */
    QState *waiting_for_provider_state = nullptr;
    /** Waiting-for-approval phase state object. */
    QState *waiting_for_approval_state = nullptr;
    /** Waiting-for-inline-review phase state object. */
    QState *waiting_for_inline_diff_review_state = nullptr;
    /** Completed phase state object. */
    QState *completed_state = nullptr;
    /** Failed phase state object. */
    QState *failed_state = nullptr;
    /** Stopped phase state object. */
    QState *stopped_state = nullptr;
    /** Cached current state for synchronous queries. */
    state_t current_state_value = state_t::IDLE;
};

/**
 * Converts a run lifecycle phase to its stable string name.
 * @param state Lifecycle phase to stringify.
 * @return Stable lowercase identifier for logs, tests, and diagnostics.
 */
QString agent_run_state_name(agent_run_state_machine_t::state_t state);

}  // namespace qcai2
