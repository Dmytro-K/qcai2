/*! Declares the compact linked-files view shown above the goal editor. */
#pragma once

#include <QListWidget>

namespace qcai2
{

/**
 * Flat wrapping list used to render linked request files inline above the goal editor.
 */
class linked_files_list_widget_t final : public QListWidget
{
    Q_OBJECT
public:
    explicit linked_files_list_widget_t(QWidget *parent = nullptr);
    void sync_to_contents();

protected:
    void resizeEvent(QResizeEvent *event) override;
};

}  // namespace qcai2
