Overview
========

``qcai2`` is a Qt Creator plugin that embeds an autonomous coding agent into the IDE.
It combines an editor-aware controller, pluggable providers, and IDE-integrated
tool execution to help inspect, modify, and validate code changes from within
Qt Creator.

Main subsystems
---------------

* ``AiAgentPlugin`` wires the plugin into Qt Creator and registers providers and tools.
* ``AgentController`` drives the plan/act/observe/verify loop.
* ``AgentDockWidget`` presents the UI, logs, and diff approval flow.
* ``src/tools`` contains the IDE-facing tool implementations exposed to the agent.
* ``src/providers`` contains the model/provider integrations, including the Copilot sidecar bridge.

The API reference section is generated from the C++ sources via Doxygen XML and
rendered by Sphinx through Breathe and Exhale.
