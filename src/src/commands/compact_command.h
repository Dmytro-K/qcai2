/*! Exposes the compact-prompt text generator for use outside the slash-command system. */
#pragma once

#include <QString>

namespace qcai2
{

/** Returns the structured compaction prompt, optionally with an extra focus hint. */
QString compact_prompt_text(const QString &focus = {});

}  // namespace qcai2
