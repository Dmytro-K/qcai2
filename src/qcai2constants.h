/*! Defines stable action and menu identifiers for the legacy Qcai2 sample plugin. */
#pragma once

namespace qcai2::Constants
{

/** Action id used to register the sample menu command. */
const char action_id[] = "qcai2.ShowAgent";

/** Action id used to focus the AI Agent goal input. */
const char focus_goal_action_id[] = "qcai2.FocusGoalInput";

/** Action id used to queue the current AI Agent request. */
const char queue_request_action_id[] = "qcai2.QueueRequest";

/** Action id used to trigger qcai2 autocomplete in the current editor. */
const char trigger_completion_action_id[] = "qcai2.TriggerCompletion";

/** Navigation widget id used to register the AI Agent in Qt Creator sidebars. */
const char navigation_id[] = "qcai2.Navigation.AiAgent";

/** Menu id used to create the sample Tools submenu. */
const char menu_id[] = "qcai2.Menu";

}  // namespace qcai2::Constants
