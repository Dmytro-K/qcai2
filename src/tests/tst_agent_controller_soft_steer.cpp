#include "../src/agent_controller.h"
#include "../src/context/chat_context_manager.h"
#include "../src/mcp/mcp_tool_manager.h"
#include "../src/settings/settings.h"
#include "../src/tools/tool_registry.h"
#include "../src/util/diff.h"
#include "../src/util/prompt_instructions.h"
#include "../src/util/request_detailed_log.h"

#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace qcai2;

namespace qcai2
{

namespace
{

settings_t test_settings;
editor_context_t::snapshot_t test_editor_snapshot;
int persisted_user_message_count = 0;
int persisted_assistant_message_count = 0;
int persisted_compaction_summary_count = 0;
QString last_persisted_compaction_summary;

QStringList message_contents(const QList<chat_message_t> &messages, const QString &role)
{
    QStringList contents;
    for (const chat_message_t &message : messages)
    {
        if (message.role == role)
        {
            contents.append(message.content);
        }
    }
    return contents;
}

bool contains_text(const QStringList &messages, const QString &needle)
{
    for (const QString &message : messages)
    {
        if (message.contains(needle) == true)
        {
            return true;
        }
    }
    return false;
}

class fake_provider_t final : public iai_provider_t
{
public:
    struct request_t
    {
        QList<chat_message_t> messages;
        QString model;
        QString reasoning_effort;
    };

    QString id() const override
    {
        return QStringLiteral("fake");
    }

    QString display_name() const override
    {
        return QStringLiteral("Fake");
    }

    void complete(const QList<chat_message_t> &messages, const QString &model,
                  double /*temperature*/, int /*max_tokens*/, const QString &reasoning_effort,
                  completion_callback_t callback, stream_callback_t stream_callback,
                  progress_callback_t progress_callback) override
    {
        this->requests.append({messages, model, reasoning_effort});
        this->completion_callback = std::move(callback);
        this->stream_callback = std::move(stream_callback);
        this->progress_callback = std::move(progress_callback);
    }

    void cancel() override
    {
        ++this->cancel_count;
    }

    void set_base_url(const QString & /*url*/) override
    {
    }

    void set_api_key(const QString & /*key*/) override
    {
    }

    void stream(const QString &delta)
    {
        if (this->stream_callback != nullptr)
        {
            this->stream_callback(delta);
        }
    }

    void finish(const QString &response, const QString &error = {},
                const provider_usage_t &usage = {})
    {
        QVERIFY(this->completion_callback != nullptr);
        auto callback = this->completion_callback;
        this->completion_callback = nullptr;
        callback(response, error, usage);
    }

    QList<request_t> requests;
    int cancel_count = 0;

private:
    completion_callback_t completion_callback;
    stream_callback_t stream_callback;
    progress_callback_t progress_callback;
};

class steering_tool_t final : public i_tool_t
{
public:
    explicit steering_tool_t(agent_controller_t *controller) : controller(controller)
    {
    }

    QString name() const override
    {
        return QStringLiteral("steering_tool");
    }

    QString description() const override
    {
        return QStringLiteral("Queues a follow-up while the tool is running.");
    }

    QJsonObject args_schema() const override
    {
        return QJsonObject{};
    }

    QString execute(const QJsonObject & /*args*/, const QString & /*work_dir*/) override
    {
        ++this->execute_count;
        QTimer::singleShot(0, this->controller, [this]() {
            this->controller->enqueue_soft_steer_message(
                QStringLiteral("Follow-up after tool"), QStringLiteral("tool steer context"),
                QStringList{QStringLiteral("src/main.cpp")});
        });
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
        return QStringLiteral("tool-result");
    }

    int execute_count = 0;

private:
    agent_controller_t *controller = nullptr;
};

}  // namespace

settings_t &settings()
{
    return test_settings;
}

void settings_t::load()
{
}

void settings_t::save() const
{
}

QString project_session_state_file_path(const QString & /*project_root*/)
{
    return {};
}

QString project_rules_file_path(const QString & /*project_root*/)
{
    return {};
}

QString read_project_rules(const QString & /*project_root*/, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return {};
}

QStringList configured_system_instructions(const QString & /*project_root*/,
                                           const QString & /*global_prompt*/, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return {};
}

void append_configured_system_instructions(QList<chat_message_t> * /*messages*/,
                                           const QString & /*project_root*/,
                                           const QString & /*global_prompt*/, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
}

editor_context_t::snapshot_t editor_context_t::capture() const
{
    return test_editor_snapshot;
}

QList<editor_context_t::project_info_t> editor_context_t::open_projects() const
{
    return {};
}

void editor_context_t::set_selected_project_file_path(const QString &project_file_path)
{
    this->active_project_file_path = project_file_path;
}

QString editor_context_t::selected_project_file_path() const
{
    return this->active_project_file_path;
}

QString editor_context_t::to_prompt_fragment() const
{
    return {};
}

QString editor_context_t::file_contents_fragment(int /*max_chars*/) const
{
    return {};
}

QStringList mcp_tool_manager_t::refresh_for_project(const QString & /*project_dir*/)
{
    return {};
}

bool chat_context_manager_t::set_active_workspace(const QString &workspace_id,
                                                  const QString &workspace_root,
                                                  const QString &conversation_id, QString *error)
{
    this->workspace_id = workspace_id;
    this->workspace_root = workspace_root;
    this->conversation.conversation_id = conversation_id;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

QString chat_context_manager_t::active_workspace_id() const
{
    return this->workspace_id;
}

QString chat_context_manager_t::active_workspace_root() const
{
    return this->workspace_root;
}

QString chat_context_manager_t::active_conversation_id() const
{
    return this->conversation.conversation_id;
}

conversation_record_t chat_context_manager_t::active_conversation() const
{
    return this->conversation;
}

QString chat_context_manager_t::ensure_active_conversation(QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return this->conversation.conversation_id;
}

QString chat_context_manager_t::start_new_conversation(const QString &title, QString *error)
{
    this->conversation.conversation_id = title;
    if (error != nullptr)
    {
        error->clear();
    }
    return this->conversation.conversation_id;
}

QList<conversation_record_t> chat_context_manager_t::conversations(QString *error) const
{
    if (error != nullptr)
    {
        error->clear();
    }
    return {};
}

bool chat_context_manager_t::rename_conversation(const QString & /*conversation_id*/,
                                                 const QString & /*title*/, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool chat_context_manager_t::delete_conversation(const QString & /*conversation_id*/,
                                                 QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

void chat_context_manager_t::sync_workspace_state(
    const editor_context_t::snapshot_t & /*snapshot*/, const settings_t & /*settings*/,
    QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
}

QString chat_context_manager_t::begin_run(context_request_kind_t /*kind*/,
                                          const QString & /*provider_id*/,
                                          const QString & /*model*/,
                                          const QString & /*reasoning_effort*/,
                                          const QString & /*thinking_level*/, bool /*dry_run*/,
                                          const QJsonObject & /*metadata*/, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return QStringLiteral("run");
}

bool chat_context_manager_t::finish_run(const QString & /*run_id*/, const QString & /*status*/,
                                        const provider_usage_t & /*usage*/,
                                        const QJsonObject & /*metadata*/, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool chat_context_manager_t::append_user_message(const QString & /*run_id*/,
                                                 const QString & /*content*/,
                                                 const QString & /*source*/,
                                                 const QJsonObject & /*metadata*/, QString *error)
{
    ++persisted_user_message_count;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool chat_context_manager_t::append_assistant_message(const QString & /*run_id*/,
                                                      const QString & /*content*/,
                                                      const QString & /*source*/,
                                                      const QJsonObject & /*metadata*/,
                                                      QString *error)
{
    ++persisted_assistant_message_count;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool chat_context_manager_t::append_compaction_summary(const QString & /*run_id*/,
                                                       const QString &content,
                                                       const QJsonObject & /*metadata*/,
                                                       QString *error)
{
    ++persisted_compaction_summary_count;
    last_persisted_compaction_summary = content;
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool chat_context_manager_t::append_artifact(const QString & /*run_id*/, const QString & /*kind*/,
                                             const QString & /*title*/,
                                             const QString & /*content*/,
                                             const QJsonObject & /*metadata*/, QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

context_envelope_t
chat_context_manager_t::build_context_envelope(context_request_kind_t /*kind*/,
                                               const QString & /*system_instruction*/,
                                               const QStringList & /*dynamic_system_messages*/,
                                               int /*max_output_tokens*/, QString *error) const
{
    if (error != nullptr)
    {
        error->clear();
    }
    return {};
}

QString chat_context_manager_t::build_completion_context_block(const QString & /*file_path*/,
                                                               int /*max_output_tokens*/,
                                                               QString *error) const
{
    if (error != nullptr)
    {
        error->clear();
    }
    return {};
}

bool chat_context_manager_t::maybe_refresh_summary(QString *error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

QString chat_context_manager_t::database_path() const
{
    return {};
}

QString chat_context_manager_t::artifact_directory_path() const
{
    return {};
}

request_detailed_log_t::request_detailed_log_t() = default;

void request_detailed_log_t::begin_request(
    const QString & /*workspace_root*/, const QString & /*run_id*/, const QString & /*goal*/,
    const QString & /*request_context*/, const QStringList & /*linked_files*/,
    const QString & /*provider_id*/, const QString & /*model*/,
    const QString & /*reasoning_effort*/, const QString & /*thinking_level*/,
    const QString & /*run_mode*/, bool /*dry_run*/, const QStringList & /*mcp_refresh_messages*/)
{
}

void request_detailed_log_t::set_run_id(const QString & /*run_id*/)
{
}

void request_detailed_log_t::record_iteration_input(
    int /*iteration*/, const QString & /*provider_id*/, const QString & /*model*/,
    const QString & /*reasoning_effort*/, const QString & /*thinking_level*/,
    double /*temperature*/, int /*max_tokens*/,
    const QList<request_log_input_part_t> & /*input_parts*/)
{
}

void request_detailed_log_t::record_iteration_output(
    int /*iteration*/, const QString & /*output_text*/, const QString & /*error_text*/,
    const QString & /*response_type*/, const QString & /*summary*/,
    const provider_usage_t & /*usage*/, qint64 /*duration_ms*/)
{
}

void request_detailed_log_t::record_tool_event(const request_log_tool_event_t & /*event*/)
{
}

void request_detailed_log_t::finish_request(const QString & /*status*/,
                                            const QJsonObject & /*metadata*/,
                                            const provider_usage_t & /*accumulated_usage*/,
                                            const QString & /*final_diff*/)
{
}

bool request_detailed_log_t::append_to_project_log(QString *error) const
{
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

QString request_detailed_log_t::log_file_path() const
{
    return {};
}

chat_context_store_t::chat_context_store_t() = default;
chat_context_store_t::~chat_context_store_t() = default;

int estimate_token_count(const QString &text)
{
    return text.isEmpty() == true ? 0 : qMax(1, static_cast<int>(text.size() / 4));
}

QString safety_policy_t::requires_approval(const QString & /*tool_name*/, int /*files_changed*/,
                                           int /*lines_changed*/) const
{
    return {};
}

namespace Diff
{

validation_result_t validate(const QString &unified_diff, int /*max_lines*/, int /*max_files*/)
{
    validation_result_t result;
    result.valid = unified_diff.trimmed().isEmpty() == false;
    return result;
}

QString normalize(const QString &unified_diff)
{
    return unified_diff;
}

bool apply_patch(const QString & /*unified_diff*/, const QString & /*work_dir*/, bool /*dry_run*/,
                 QString &error_msg)
{
    error_msg.clear();
    return true;
}

bool revert_patch(const QString & /*unified_diff*/, const QString & /*work_dir*/,
                  QString &error_msg)
{
    error_msg.clear();
    return true;
}

new_file_result_t extract_and_create_new_files(const QString &patch_text,
                                               const QString & /*work_dir*/, bool /*dry_run*/)
{
    new_file_result_t result;
    result.remaining_diff = patch_text;
    return result;
}

}  // namespace Diff

class tst_agent_controller_soft_steer_t : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void normal_final_response_without_steer_finishes_once();
    void streaming_follow_up_supersedes_partial_answer_without_cancel();
    void queued_follow_up_messages_keep_order();
    void steering_during_tool_execution_replans_after_tool_completion();
    void steering_while_waiting_for_approval_restarts_without_manual_resolution();
    void steering_while_reviewing_inline_diff_clears_diff_and_restarts();
    void decision_request_enters_waiting_state();
    void decision_request_without_id_gets_generated();
    void invalid_decision_request_retries_without_waiting();
    void answering_decision_with_option_resumes_run();
    void answering_decision_with_freeform_resumes_run();
    void invalid_decision_answers_are_rejected();
    void freeform_answer_is_rejected_when_disabled();
    void steering_queued_while_waiting_for_decision_applies_after_answer();
    void decision_double_submit_is_blocked();
    void compaction_persists_summary_without_chat_history_or_stream_echo();
};

void tst_agent_controller_soft_steer_t::init()
{
    test_settings = settings_t{};
    test_settings.provider = QStringLiteral("fake");
    test_settings.detailed_request_logging = false;
    test_settings.debug_logging = false;
    test_settings.inline_diff_refinement_enabled = false;
    test_settings.max_iterations = 8;
    test_settings.max_tool_calls = 25;
    persisted_user_message_count = 0;
    persisted_assistant_message_count = 0;
    persisted_compaction_summary_count = 0;
    last_persisted_compaction_summary.clear();
    test_editor_snapshot = {};
}

void tst_agent_controller_soft_steer_t::normal_final_response_without_steer_finishes_once()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);
    QSignalSpy steer_applied_spy(&controller, &agent_controller_t::soft_steer_applied);

    controller.start(QStringLiteral("Initial request"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    QCOMPARE(provider.requests.size(), 1);

    provider.finish(QStringLiteral("{\"type\":\"final\",\"summary\":\"Done\",\"diff\":\"\"}"));

    QTRY_COMPARE(stopped_spy.count(), 1);
    QCOMPARE(steer_applied_spy.count(), 0);
    QCOMPARE(provider.requests.size(), 1);
    QCOMPARE(provider.cancel_count, 0);
}

void tst_agent_controller_soft_steer_t::
    streaming_follow_up_supersedes_partial_answer_without_cancel()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy steer_applied_spy(&controller, &agent_controller_t::soft_steer_applied);
    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);
    QStringList steer_log_order;
    QStringList steer_request_logs;
    QObject::connect(
        &controller, &agent_controller_t::soft_steer_applied, &controller,
        [&steer_log_order](int) { steer_log_order.append(QStringLiteral("applied")); });
    QObject::connect(&controller, &agent_controller_t::log_message, &controller,
                     [&steer_log_order, &steer_request_logs](const QString &message) {
                         if (message.startsWith(QStringLiteral("[pending] Soft steer request")) ==
                             true)
                         {
                             steer_log_order.append(QStringLiteral("request"));
                             steer_request_logs.append(message);
                         }
                     });

    controller.start(QStringLiteral("Initial request"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    QCOMPARE(provider.requests.size(), 1);

    provider.stream(QStringLiteral("{\"type\":\"final\",\"summary\":\"Partial"));
    QVERIFY(controller.enqueue_soft_steer_message(QStringLiteral("Newest request")));
    provider.finish(
        QStringLiteral("{\"type\":\"final\",\"summary\":\"Superseded answer\",\"diff\":\"\"}"));

    QTRY_COMPARE(provider.requests.size(), 2);
    QTRY_COMPARE(steer_applied_spy.count(), 1);
    QTRY_COMPARE(steer_log_order.size(), 2);
    QCOMPARE(provider.cancel_count, 0);
    QCOMPARE(steer_log_order.at(0), QStringLiteral("applied"));
    QCOMPARE(steer_log_order.at(1), QStringLiteral("request"));
    QCOMPARE(steer_request_logs.size(), 1);
    QVERIFY(steer_request_logs.constFirst().contains(QStringLiteral("[pending]")));
    QVERIFY(steer_request_logs.constFirst().contains(QStringLiteral("Newest request")));

    const QList<chat_message_t> second_request = provider.requests.at(1).messages;
    const QStringList assistant_messages =
        message_contents(second_request, QStringLiteral("assistant"));
    const QStringList user_messages = message_contents(second_request, QStringLiteral("user"));
    const QStringList system_messages = message_contents(second_request, QStringLiteral("system"));

    QVERIFY(contains_text(user_messages, QStringLiteral("Initial request")));
    QCOMPARE(user_messages.constLast(), QStringLiteral("Newest request"));
    QVERIFY(contains_text(assistant_messages, QStringLiteral("Superseded answer")) == false);
    QVERIFY(contains_text(system_messages, QStringLiteral("superseded_by_user_steer")));

    provider.finish(
        QStringLiteral("{\"type\":\"final\",\"summary\":\"Updated answer\",\"diff\":\"\"}"));
    QTRY_COMPARE(stopped_spy.count(), 1);
}

void tst_agent_controller_soft_steer_t::queued_follow_up_messages_keep_order()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);
    QStringList steer_request_logs;
    QObject::connect(&controller, &agent_controller_t::log_message, &controller,
                     [&steer_request_logs](const QString &message) {
                         if (message.startsWith(QStringLiteral("[pending] Soft steer request")) ==
                             true)
                         {
                             steer_request_logs.append(message);
                         }
                     });

    controller.start(QStringLiteral("Initial request"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    QCOMPARE(provider.requests.size(), 1);

    QVERIFY(controller.enqueue_soft_steer_message(QStringLiteral("First follow-up")));
    QVERIFY(controller.enqueue_soft_steer_message(QStringLiteral("Second follow-up")));
    provider.finish(
        QStringLiteral("{\"type\":\"final\",\"summary\":\"Old answer\",\"diff\":\"\"}"));

    QTRY_COMPARE(provider.requests.size(), 2);
    QTRY_COMPARE(steer_request_logs.size(), 2);

    const QStringList user_messages =
        message_contents(provider.requests.at(1).messages, QStringLiteral("user"));
    QVERIFY(user_messages.size() >= 3);
    QCOMPARE(user_messages.at(user_messages.size() - 2), QStringLiteral("First follow-up"));
    QCOMPARE(user_messages.at(user_messages.size() - 1), QStringLiteral("Second follow-up"));
    QVERIFY(steer_request_logs.at(0).contains(QStringLiteral("First follow-up")));
    QVERIFY(steer_request_logs.at(1).contains(QStringLiteral("Second follow-up")));
}

void tst_agent_controller_soft_steer_t::
    steering_during_tool_execution_replans_after_tool_completion()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    auto tool = std::make_shared<steering_tool_t>(&controller);
    registry.register_tool(tool);

    QSignalSpy steer_applied_spy(&controller, &agent_controller_t::soft_steer_applied);
    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);

    controller.start(QStringLiteral("Initial request"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    QCOMPARE(provider.requests.size(), 1);

    provider.finish(
        QStringLiteral("{\"type\":\"tool_call\",\"name\":\"steering_tool\",\"args\":{}}"));

    QTRY_COMPARE(tool->execute_count, 1);
    QTRY_COMPARE(provider.requests.size(), 2);
    QTRY_COMPARE(steer_applied_spy.count(), 1);
    QCOMPARE(provider.cancel_count, 0);

    const QStringList user_messages =
        message_contents(provider.requests.at(1).messages, QStringLiteral("user"));
    QVERIFY(user_messages.size() >= 3);
    QVERIFY(user_messages.at(user_messages.size() - 2)
                .contains(QStringLiteral("Before the user's follow-up arrived, tool "
                                         "'steering_tool' finished")));
    QVERIFY(user_messages.constLast().contains(QStringLiteral("Follow-up after tool")));
    QVERIFY(contains_text(user_messages, QStringLiteral("Continue with the next step.")) == false);

    provider.finish(
        QStringLiteral("{\"type\":\"final\",\"summary\":\"Updated answer\",\"diff\":\"\"}"));
    QTRY_COMPARE(stopped_spy.count(), 1);
}

void tst_agent_controller_soft_steer_t::
    steering_while_waiting_for_approval_restarts_without_manual_resolution()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy approval_spy(&controller, &agent_controller_t::approval_requested);
    QSignalSpy steer_applied_spy(&controller, &agent_controller_t::soft_steer_applied);
    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);

    controller.start(QStringLiteral("Initial request"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    QCOMPARE(provider.requests.size(), 1);

    provider.finish(QStringLiteral("{\"type\":\"need_approval\",\"action\":\"dangerous_tool\","
                                   "\"reason\":\"Needs approval\",\"preview\":\"preview\"}"));

    QTRY_COMPARE(approval_spy.count(), 1);
    QVERIFY(controller.enqueue_soft_steer_message(QStringLiteral("Approval follow-up")));

    QTRY_COMPARE(provider.requests.size(), 2);
    QTRY_COMPARE(steer_applied_spy.count(), 1);
    QCOMPARE(provider.cancel_count, 0);

    const QStringList user_messages =
        message_contents(provider.requests.at(1).messages, QStringLiteral("user"));
    QVERIFY(user_messages.contains(QStringLiteral("Initial request")));
    QCOMPARE(user_messages.constLast(), QStringLiteral("Approval follow-up"));

    provider.finish(
        QStringLiteral("{\"type\":\"final\",\"summary\":\"Updated after approval wait\","
                       "\"diff\":\"\"}"));
    QTRY_COMPARE(stopped_spy.count(), 1);
}

void tst_agent_controller_soft_steer_t::
    steering_while_reviewing_inline_diff_clears_diff_and_restarts()
{
    test_settings.inline_diff_refinement_enabled = true;

    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy diff_spy(&controller, &agent_controller_t::diff_available);
    QSignalSpy steer_applied_spy(&controller, &agent_controller_t::soft_steer_applied);
    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);

    controller.start(QStringLiteral("Initial request"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    QCOMPARE(provider.requests.size(), 1);

    provider.finish(QStringLiteral(
        "{\"type\":\"final\",\"summary\":\"Draft diff\","
        "\"diff\":\"--- a/file.cpp\\n+++ b/file.cpp\\n@@ -1 +1 @@\\n-old\\n+new\\n\"}"));

    QTRY_COMPARE(diff_spy.count(), 1);
    QVERIFY(diff_spy.at(0).at(0).toString().isEmpty() == false);
    QVERIFY(controller.enqueue_soft_steer_message(QStringLiteral("Inline diff follow-up")));

    QTRY_COMPARE(provider.requests.size(), 2);
    QTRY_COMPARE(steer_applied_spy.count(), 1);
    QTRY_COMPARE(diff_spy.count(), 2);
    QCOMPARE(diff_spy.at(1).at(0).toString(), QString());
    QCOMPARE(provider.cancel_count, 0);

    const QStringList user_messages =
        message_contents(provider.requests.at(1).messages, QStringLiteral("user"));
    QVERIFY(user_messages.contains(QStringLiteral("Initial request")));
    QCOMPARE(user_messages.constLast(), QStringLiteral("Inline diff follow-up"));

    provider.finish(QStringLiteral("{\"type\":\"final\",\"summary\":\"Updated after diff review\","
                                   "\"diff\":\"\"}"));
    QTRY_COMPARE(stopped_spy.count(), 1);
}

void tst_agent_controller_soft_steer_t::decision_request_enters_waiting_state()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy decision_spy(&controller, &agent_controller_t::decision_requested);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    QCOMPARE(provider.requests.size(), 1);

    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"request_id\":\"decision-1\",\"title\":\"Pick one\","
        "\"description\":\"Need a user choice\",\"options\":[{\"id\":\"a\",\"label\":\"Option "
        "A\",\"description\":\"First\"},{\"id\":\"b\",\"label\":\"Option B\",\"description\":"
        "\"Second\"}],\"allow_freeform\":true,\"freeform_placeholder\":\"Other\",\"recommended_"
        "option_id\":\"b\"}"));

    QTRY_COMPARE(decision_spy.count(), 1);
    QVERIFY(controller.is_waiting_for_user_decision());
    QCOMPARE(controller.pending_decision_request_id(), QStringLiteral("decision-1"));

    const agent_decision_request_t request =
        decision_spy.at(0).at(0).value<agent_decision_request_t>();
    QCOMPARE(request.options.size(), 2);
    QCOMPARE(request.recommended_option_id, QStringLiteral("b"));
    QVERIFY(request.allow_freeform);
}

void tst_agent_controller_soft_steer_t::decision_request_without_id_gets_generated()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy decision_spy(&controller, &agent_controller_t::decision_requested);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"title\":\"Pick one\",\"options\":[{\"id\":\"a\","
        "\"label\":\"Option A\",\"description\":\"First\"}],\"allow_freeform\":true}"));

    QTRY_COMPARE(decision_spy.count(), 1);
    const agent_decision_request_t request =
        decision_spy.at(0).at(0).value<agent_decision_request_t>();
    QVERIFY(request.request_id.isEmpty() == false);
    QCOMPARE(controller.pending_decision_request_id(), request.request_id);
}

void tst_agent_controller_soft_steer_t::invalid_decision_request_retries_without_waiting()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy decision_spy(&controller, &agent_controller_t::decision_requested);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"request_id\":\"broken\",\"title\":\"Broken\","
        "\"options\":[],\"allow_freeform\":false}"));

    QTRY_COMPARE(provider.requests.size(), 2);
    QCOMPARE(decision_spy.count(), 0);
    QVERIFY(controller.is_waiting_for_user_decision() == false);

    const QStringList user_messages =
        message_contents(provider.requests.at(1).messages, QStringLiteral("user"));
    QVERIFY(user_messages.constLast().contains(QStringLiteral("decision_request was invalid")));
}

void tst_agent_controller_soft_steer_t::answering_decision_with_option_resumes_run()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy answered_spy(&controller, &agent_controller_t::decision_answered);
    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"request_id\":\"decision-1\",\"title\":\"Pick one\","
        "\"options\":[{\"id\":\"full\",\"label\":\"Full validation\",\"description\":\"Run all "
        "tests\"}],\"allow_freeform\":false}"));

    QTRY_VERIFY(controller.is_waiting_for_user_decision());
    QVERIFY(controller.submit_user_decision(QStringLiteral("decision-1"), QStringLiteral("full")));
    QTRY_COMPARE(answered_spy.count(), 1);
    QTRY_COMPARE(provider.requests.size(), 2);
    QVERIFY(controller.is_waiting_for_user_decision() == false);

    const QStringList user_messages =
        message_contents(provider.requests.at(1).messages, QStringLiteral("user"));
    QVERIFY(user_messages.constLast().contains(QStringLiteral("Full validation")));

    provider.finish(
        QStringLiteral("{\"type\":\"final\",\"summary\":\"Used chosen option\",\"diff\":\"\"}"));
    QTRY_COMPARE(stopped_spy.count(), 1);
}

void tst_agent_controller_soft_steer_t::answering_decision_with_freeform_resumes_run()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy answered_spy(&controller, &agent_controller_t::decision_answered);
    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"request_id\":\"decision-2\",\"title\":\"Pick one\","
        "\"description\":\"Need a custom answer\",\"options\":[{\"id\":\"a\",\"label\":\"Option "
        "A\",\"description\":\"First\"}],\"allow_freeform\":true}"));

    QTRY_VERIFY(controller.is_waiting_for_user_decision());
    QVERIFY(controller.submit_user_decision(QStringLiteral("decision-2"), {},
                                            QStringLiteral("Use a staged rollout instead.")));
    QTRY_COMPARE(answered_spy.count(), 1);
    QTRY_COMPARE(provider.requests.size(), 2);

    const QStringList user_messages =
        message_contents(provider.requests.at(1).messages, QStringLiteral("user"));
    QVERIFY(user_messages.constLast().contains(QStringLiteral("Use a staged rollout instead.")));

    provider.finish(
        QStringLiteral("{\"type\":\"final\",\"summary\":\"Used custom answer\",\"diff\":\"\"}"));
    QTRY_COMPARE(stopped_spy.count(), 1);
}

void tst_agent_controller_soft_steer_t::invalid_decision_answers_are_rejected()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"request_id\":\"decision-invalid\",\"title\":\"Pick "
        "one\",\"options\":[{\"id\":\"a\",\"label\":\"Option A\",\"description\":\"First\"}],"
        "\"allow_freeform\":true}"));

    QTRY_VERIFY(controller.is_waiting_for_user_decision());
    QVERIFY(controller.submit_user_decision(QStringLiteral("wrong-id"), QStringLiteral("a")) ==
            false);
    QVERIFY(controller.submit_user_decision(QStringLiteral("decision-invalid"),
                                            QStringLiteral("missing-option")) == false);
    QVERIFY(controller.submit_user_decision(QStringLiteral("decision-invalid"), {}, {}) == false);
    QCOMPARE(provider.requests.size(), 1);
    QVERIFY(controller.is_waiting_for_user_decision());
}

void tst_agent_controller_soft_steer_t::freeform_answer_is_rejected_when_disabled()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"request_id\":\"decision-no-freeform\",\"title\":\"Pick "
        "one\",\"options\":[{\"id\":\"a\",\"label\":\"Option A\",\"description\":\"First\"}],"
        "\"allow_freeform\":false}"));

    QTRY_VERIFY(controller.is_waiting_for_user_decision());
    QVERIFY(controller.submit_user_decision(QStringLiteral("decision-no-freeform"), {},
                                            QStringLiteral("Custom answer")) == false);
    QCOMPARE(provider.requests.size(), 1);
    QVERIFY(controller.is_waiting_for_user_decision());
}

void tst_agent_controller_soft_steer_t::
    steering_queued_while_waiting_for_decision_applies_after_answer()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy steer_pending_spy(&controller, &agent_controller_t::soft_steer_pending);
    QSignalSpy steer_applied_spy(&controller, &agent_controller_t::soft_steer_applied);
    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"request_id\":\"decision-3\",\"title\":\"Pick one\","
        "\"options\":[{\"id\":\"full\",\"label\":\"Full validation\",\"description\":\"Run all "
        "tests\"}],\"allow_freeform\":true}"));

    QTRY_VERIFY(controller.is_waiting_for_user_decision());
    QVERIFY(
        controller.enqueue_soft_steer_message(QStringLiteral("After that, also update docs.")));
    QCOMPARE(steer_pending_spy.count(), 1);
    QCOMPARE(provider.requests.size(), 1);

    QVERIFY(controller.submit_user_decision(QStringLiteral("decision-3"), QStringLiteral("full")));
    QTRY_COMPARE(provider.requests.size(), 2);
    QTRY_COMPARE(steer_applied_spy.count(), 1);

    const QStringList user_messages =
        message_contents(provider.requests.at(1).messages, QStringLiteral("user"));
    QVERIFY(user_messages.size() >= 3);
    QVERIFY(
        user_messages.at(user_messages.size() - 2).contains(QStringLiteral("Full validation")));
    QCOMPARE(user_messages.constLast(), QStringLiteral("After that, also update docs."));

    provider.finish(QStringLiteral("{\"type\":\"final\",\"summary\":\"Done\",\"diff\":\"\"}"));
    QTRY_COMPARE(stopped_spy.count(), 1);
}

void tst_agent_controller_soft_steer_t::decision_double_submit_is_blocked()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    controller.start(QStringLiteral("Need a user choice"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"));
    provider.finish(QStringLiteral(
        "{\"type\":\"decision_request\",\"request_id\":\"decision-4\",\"title\":\"Pick one\","
        "\"options\":[{\"id\":\"a\",\"label\":\"Option A\",\"description\":\"First\"}],"
        "\"allow_freeform\":true}"));

    QTRY_VERIFY(controller.is_waiting_for_user_decision());
    QVERIFY(controller.submit_user_decision(QStringLiteral("decision-4"), QStringLiteral("a")));
    QVERIFY(controller.submit_user_decision(QStringLiteral("decision-4"), QStringLiteral("a")) ==
            false);
    QTRY_COMPARE(provider.requests.size(), 2);
}

void tst_agent_controller_soft_steer_t::
    compaction_persists_summary_without_chat_history_or_stream_echo()
{
    fake_provider_t provider;
    tool_registry_t registry;
    agent_controller_t controller;
    controller.set_provider(&provider);
    controller.set_tool_registry(&registry);

    QSignalSpy log_spy(&controller, &agent_controller_t::log_message);
    QSignalSpy stream_spy(&controller, &agent_controller_t::streaming_token);
    QSignalSpy stopped_spy(&controller, &agent_controller_t::stopped);

    controller.start(QStringLiteral("Compact this conversation"), true,
                     agent_controller_t::run_mode_t::AGENT, QStringLiteral("fake-model"),
                     QStringLiteral("off"), QStringLiteral("off"),
                     agent_controller_t::run_options_t{true});
    QCOMPARE(provider.requests.size(), 1);
    QVERIFY(controller.is_compaction_run());

    provider.stream(QStringLiteral("secret compact draft"));
    provider.finish(QStringLiteral(
        "{\"type\":\"final\",\"summary\":\"## Current Goal\\nCompacted goal\\n\\n## Stable "
        "Memory\\n- fact\\n\",\"diff\":\"\"}"));

    QTRY_COMPARE(stopped_spy.count(), 1);
    QCOMPARE(stream_spy.count(), 0);

    QStringList visible_logs;
    for (const QList<QVariant> &entry : log_spy)
    {
        visible_logs.append(entry.at(0).toString());
    }
    QVERIFY(contains_text(visible_logs, QStringLiteral("🗜 Compaction started.")));
    QVERIFY(contains_text(visible_logs, QStringLiteral("🗜 Compaction completed.")));
    QVERIFY(contains_text(visible_logs, QStringLiteral("Compact this conversation")) == false);
    QVERIFY(contains_text(visible_logs, QStringLiteral("secret compact draft")) == false);
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_agent_controller_soft_steer_t)

#include "tst_agent_controller_soft_steer.moc"
