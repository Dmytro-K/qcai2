#include "agent_run_state_machine.h"

#include <QCoreApplication>
#include <QState>
#include <QStateMachine>

namespace qcai2
{

namespace
{

bool is_running_state(agent_run_state_machine_t::state_t state)
{
    switch (state)
    {
        case agent_run_state_machine_t::state_t::STARTING:
        case agent_run_state_machine_t::state_t::WAITING_FOR_PROVIDER:
        case agent_run_state_machine_t::state_t::WAITING_FOR_APPROVAL:
        case agent_run_state_machine_t::state_t::WAITING_FOR_INLINE_DIFF_REVIEW:
            return true;
        case agent_run_state_machine_t::state_t::IDLE:
        case agent_run_state_machine_t::state_t::COMPLETED:
        case agent_run_state_machine_t::state_t::FAILED:
        case agent_run_state_machine_t::state_t::STOPPED:
            return false;
    }
    return false;
}

}  // namespace

QString agent_run_state_name(agent_run_state_machine_t::state_t state)
{
    switch (state)
    {
        case agent_run_state_machine_t::state_t::IDLE:
            return QStringLiteral("idle");
        case agent_run_state_machine_t::state_t::STARTING:
            return QStringLiteral("starting");
        case agent_run_state_machine_t::state_t::WAITING_FOR_PROVIDER:
            return QStringLiteral("waiting_for_provider");
        case agent_run_state_machine_t::state_t::WAITING_FOR_APPROVAL:
            return QStringLiteral("waiting_for_approval");
        case agent_run_state_machine_t::state_t::WAITING_FOR_INLINE_DIFF_REVIEW:
            return QStringLiteral("waiting_for_inline_diff_review");
        case agent_run_state_machine_t::state_t::COMPLETED:
            return QStringLiteral("completed");
        case agent_run_state_machine_t::state_t::FAILED:
            return QStringLiteral("failed");
        case agent_run_state_machine_t::state_t::STOPPED:
            return QStringLiteral("stopped");
    }
    return QStringLiteral("unknown");
}

agent_run_state_machine_t::agent_run_state_machine_t(QObject *parent) : QObject(parent)
{
    this->state_machine = new QStateMachine(this);
    this->idle_state = new QState(this->state_machine);
    this->starting_state = new QState(this->state_machine);
    this->waiting_for_provider_state = new QState(this->state_machine);
    this->waiting_for_approval_state = new QState(this->state_machine);
    this->waiting_for_inline_diff_review_state = new QState(this->state_machine);
    this->completed_state = new QState(this->state_machine);
    this->failed_state = new QState(this->state_machine);
    this->stopped_state = new QState(this->state_machine);

    const auto bind_state = [this](QState *state, state_t value) {
        QObject::connect(state, &QState::entered, this,
                         [this, value]() { this->set_current_state(value); });
    };
    bind_state(this->idle_state, state_t::IDLE);
    bind_state(this->starting_state, state_t::STARTING);
    bind_state(this->waiting_for_provider_state, state_t::WAITING_FOR_PROVIDER);
    bind_state(this->waiting_for_approval_state, state_t::WAITING_FOR_APPROVAL);
    bind_state(this->waiting_for_inline_diff_review_state,
               state_t::WAITING_FOR_INLINE_DIFF_REVIEW);
    bind_state(this->completed_state, state_t::COMPLETED);
    bind_state(this->failed_state, state_t::FAILED);
    bind_state(this->stopped_state, state_t::STOPPED);

    const auto add_terminal_transitions = [this](QState *source) {
        source->addTransition(this, &agent_run_state_machine_t::run_completed_requested,
                              this->completed_state);
        source->addTransition(this, &agent_run_state_machine_t::run_failed_requested,
                              this->failed_state);
        source->addTransition(this, &agent_run_state_machine_t::run_stopped_requested,
                              this->stopped_state);
    };

    this->idle_state->addTransition(this, &agent_run_state_machine_t::start_run_requested,
                                    this->starting_state);
    this->completed_state->addTransition(this, &agent_run_state_machine_t::start_run_requested,
                                         this->starting_state);
    this->failed_state->addTransition(this, &agent_run_state_machine_t::start_run_requested,
                                      this->starting_state);
    this->stopped_state->addTransition(this, &agent_run_state_machine_t::start_run_requested,
                                       this->starting_state);

    this->starting_state->addTransition(
        this, &agent_run_state_machine_t::provider_response_wait_requested,
        this->waiting_for_provider_state);
    this->waiting_for_provider_state->addTransition(
        this, &agent_run_state_machine_t::provider_response_wait_requested,
        this->waiting_for_provider_state);
    this->waiting_for_provider_state->addTransition(
        this, &agent_run_state_machine_t::user_approval_wait_requested,
        this->waiting_for_approval_state);
    this->waiting_for_provider_state->addTransition(
        this, &agent_run_state_machine_t::inline_diff_review_wait_requested,
        this->waiting_for_inline_diff_review_state);
    this->waiting_for_approval_state->addTransition(
        this, &agent_run_state_machine_t::provider_response_wait_requested,
        this->waiting_for_provider_state);
    this->waiting_for_inline_diff_review_state->addTransition(
        this, &agent_run_state_machine_t::provider_response_wait_requested,
        this->waiting_for_provider_state);

    add_terminal_transitions(this->starting_state);
    add_terminal_transitions(this->waiting_for_provider_state);
    add_terminal_transitions(this->waiting_for_approval_state);
    add_terminal_transitions(this->waiting_for_inline_diff_review_state);

    this->state_machine->setInitialState(this->idle_state);
    this->state_machine->start();
    this->flush_state_machine_events();
}

agent_run_state_machine_t::~agent_run_state_machine_t() = default;

agent_run_state_machine_t::state_t agent_run_state_machine_t::current_state() const
{
    return this->current_state_value;
}

QString agent_run_state_machine_t::current_state_name() const
{
    return agent_run_state_name(this->current_state_value);
}

bool agent_run_state_machine_t::is_running() const
{
    return is_running_state(this->current_state_value);
}

bool agent_run_state_machine_t::is_waiting_for_provider() const
{
    return this->current_state_value == state_t::WAITING_FOR_PROVIDER;
}

bool agent_run_state_machine_t::is_waiting_for_approval() const
{
    return this->current_state_value == state_t::WAITING_FOR_APPROVAL;
}

bool agent_run_state_machine_t::is_waiting_for_inline_diff_review() const
{
    return this->current_state_value == state_t::WAITING_FOR_INLINE_DIFF_REVIEW;
}

void agent_run_state_machine_t::start_run()
{
    emit this->start_run_requested();
    this->flush_state_machine_events();
}

void agent_run_state_machine_t::await_provider_response()
{
    emit this->provider_response_wait_requested();
    this->flush_state_machine_events();
}

void agent_run_state_machine_t::await_user_approval()
{
    emit this->user_approval_wait_requested();
    this->flush_state_machine_events();
}

void agent_run_state_machine_t::await_inline_diff_review()
{
    emit this->inline_diff_review_wait_requested();
    this->flush_state_machine_events();
}

void agent_run_state_machine_t::mark_completed()
{
    emit this->run_completed_requested();
    this->flush_state_machine_events();
}

void agent_run_state_machine_t::mark_failed()
{
    emit this->run_failed_requested();
    this->flush_state_machine_events();
}

void agent_run_state_machine_t::mark_stopped()
{
    emit this->run_stopped_requested();
    this->flush_state_machine_events();
}

void agent_run_state_machine_t::flush_state_machine_events()
{
    QCoreApplication::sendPostedEvents(this->state_machine, 0);
}

void agent_run_state_machine_t::set_current_state(state_t state)
{
    if (this->current_state_value == state)
    {
        return;
    }

    this->current_state_value = state;
    emit this->state_changed(state);
}

}  // namespace qcai2
