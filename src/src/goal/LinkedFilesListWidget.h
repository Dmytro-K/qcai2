/*! Declares the compact linked-files view shown above the goal editor. */
#pragma once

#include <QListWidget>

namespace qcai2
{

/**
 * Flat wrapping list used to render linked request files inline above the goal editor.
 */
class LinkedFilesListWidget final : public QListWidget
{
    Q_OBJECT
public:
    explicit LinkedFilesListWidget(QWidget *parent = nullptr);
    void syncToContents();

protected:
    void resizeEvent(QResizeEvent *event) override;
};

}  // namespace qcai2
