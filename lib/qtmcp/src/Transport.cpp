/*! Implements the base transport abstraction for the Qt MCP client. */

#include <qtmcp/Transport.h>

namespace qtmcp
{

transport_t::transport_t(QObject *parent) : QObject(parent)
{
}

transport_t::~transport_t() = default;

}  // namespace qtmcp
