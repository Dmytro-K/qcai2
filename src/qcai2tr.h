/*! Declares the translation helper used by the legacy Qcai2 sample plugin. */
#pragma once

#include <QCoreApplication>

namespace qcai2
{

/**
 * Translation context wrapper for QObject-free tr() calls.
 */
struct tr_t
{
    Q_DECLARE_TR_FUNCTIONS(QtC::qcai2)
};

}  // namespace qcai2
