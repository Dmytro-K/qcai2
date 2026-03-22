#include "../src/agent_run_state_machine.h"

#include <QtTest>

namespace qcai2
{

class tst_agent_run_state_machine_t : public QObject
{
    Q_OBJECT

private slots:
    void initial_state_is_idle();
    void start_run_enters_starting_state();
    void awaiting_provider_enters_waiting_for_provider_state();
    void approval_wait_enters_waiting_for_approval_state();
    void inline_diff_review_wait_enters_review_state();
    void completion_enters_completed_state();
    void failure_enters_failed_state();
    void stop_enters_stopped_state();
    void start_run_restarts_after_terminal_state();
    void inline_review_can_resume_provider_wait();
};

void tst_agent_run_state_machine_t::initial_state_is_idle()
{
    agent_run_state_machine_t state_machine;

    QCOMPARE(state_machine.current_state(), agent_run_state_machine_t::state_t::IDLE);
    QCOMPARE(state_machine.current_state_name(), QStringLiteral("idle"));
    QVERIFY(state_machine.is_running() == false);
}

void tst_agent_run_state_machine_t::start_run_enters_starting_state()
{
    agent_run_state_machine_t state_machine;

    state_machine.start_run();

    QCOMPARE(state_machine.current_state(), agent_run_state_machine_t::state_t::STARTING);
    QVERIFY(state_machine.is_running());
}

void tst_agent_run_state_machine_t::awaiting_provider_enters_waiting_for_provider_state()
{
    agent_run_state_machine_t state_machine;
    state_machine.start_run();

    state_machine.await_provider_response();

    QCOMPARE(state_machine.current_state(),
             agent_run_state_machine_t::state_t::WAITING_FOR_PROVIDER);
    QVERIFY(state_machine.is_waiting_for_provider());
}

void tst_agent_run_state_machine_t::approval_wait_enters_waiting_for_approval_state()
{
    agent_run_state_machine_t state_machine;
    state_machine.start_run();
    state_machine.await_provider_response();

    state_machine.await_user_approval();

    QCOMPARE(state_machine.current_state(),
             agent_run_state_machine_t::state_t::WAITING_FOR_APPROVAL);
    QVERIFY(state_machine.is_waiting_for_approval());
}

void tst_agent_run_state_machine_t::inline_diff_review_wait_enters_review_state()
{
    agent_run_state_machine_t state_machine;
    state_machine.start_run();
    state_machine.await_provider_response();

    state_machine.await_inline_diff_review();

    QCOMPARE(state_machine.current_state(),
             agent_run_state_machine_t::state_t::WAITING_FOR_INLINE_DIFF_REVIEW);
    QVERIFY(state_machine.is_waiting_for_inline_diff_review());
}

void tst_agent_run_state_machine_t::completion_enters_completed_state()
{
    agent_run_state_machine_t state_machine;
    state_machine.start_run();
    state_machine.await_provider_response();

    state_machine.mark_completed();

    QCOMPARE(state_machine.current_state(), agent_run_state_machine_t::state_t::COMPLETED);
    QVERIFY(state_machine.is_running() == false);
}

void tst_agent_run_state_machine_t::failure_enters_failed_state()
{
    agent_run_state_machine_t state_machine;
    state_machine.start_run();
    state_machine.await_provider_response();

    state_machine.mark_failed();

    QCOMPARE(state_machine.current_state(), agent_run_state_machine_t::state_t::FAILED);
    QVERIFY(state_machine.is_running() == false);
}

void tst_agent_run_state_machine_t::stop_enters_stopped_state()
{
    agent_run_state_machine_t state_machine;
    state_machine.start_run();
    state_machine.await_provider_response();

    state_machine.mark_stopped();

    QCOMPARE(state_machine.current_state(), agent_run_state_machine_t::state_t::STOPPED);
    QVERIFY(state_machine.is_running() == false);
}

void tst_agent_run_state_machine_t::start_run_restarts_after_terminal_state()
{
    agent_run_state_machine_t state_machine;
    state_machine.start_run();
    state_machine.await_provider_response();
    state_machine.mark_completed();

    state_machine.start_run();

    QCOMPARE(state_machine.current_state(), agent_run_state_machine_t::state_t::STARTING);
    QVERIFY(state_machine.is_running());
}

void tst_agent_run_state_machine_t::inline_review_can_resume_provider_wait()
{
    agent_run_state_machine_t state_machine;
    state_machine.start_run();
    state_machine.await_provider_response();
    state_machine.await_inline_diff_review();

    state_machine.await_provider_response();

    QCOMPARE(state_machine.current_state(),
             agent_run_state_machine_t::state_t::WAITING_FOR_PROVIDER);
    QVERIFY(state_machine.is_waiting_for_provider());
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_agent_run_state_machine_t)

#include "tst_agent_run_state_machine.moc"
