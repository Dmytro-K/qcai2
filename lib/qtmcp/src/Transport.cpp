/*! Implements the base transport abstraction for the Qt MCP client. */

#include <qtmcp/Transport.h>

namespace qtmcp
{

Transport::Transport(QObject *parent) : QObject(parent)
{
}

Transport::~Transport() = default;

}  // namespace qtmcp
