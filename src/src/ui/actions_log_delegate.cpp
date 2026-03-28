/*! @file
    @brief Implements the custom delegate used to paint one Actions Log row.
*/

#include "actions_log_delegate.h"

#include "actions_log_links.h"
#include "actions_log_model.h"

#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFrame>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QStyle>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>

namespace qcai2
{

namespace
{

constexpr int horizontal_padding = 8;
constexpr int vertical_padding = 6;

void configure_document(QTextDocument *document, const QStyleOptionViewItem &option,
                        const QString &markdown, int text_width)
{
    if (document == nullptr)
    {
        return;
    }

    document->setDocumentMargin(0);
    document->setUndoRedoEnabled(false);
    document->setDefaultFont(option.font);
    document->setTextWidth(text_width);
    document->setMarkdown(markdown);

    QTextOption text_option = document->defaultTextOption();
    text_option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document->setDefaultTextOption(text_option);

    const QColor text_color =
        (option.state & QStyle::State_Selected) != 0
            ? option.palette.color(QPalette::Active, QPalette::HighlightedText)
            : option.palette.color(QPalette::Active, QPalette::Text);
    document->setDefaultStyleSheet(
        QStringLiteral("body { color: %1; } a { color: %2; } code, pre { white-space: pre-wrap; }")
            .arg(text_color.name(QColor::HexRgb),
                 option.palette.color(QPalette::Active, QPalette::Link).name(QColor::HexRgb)));
}

void configure_editor(QTextBrowser *editor)
{
    if (editor == nullptr)
    {
        return;
    }

    editor->setReadOnly(true);
    editor->setFrameShape(QFrame::NoFrame);
    editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor->setOpenExternalLinks(false);
    editor->setOpenLinks(false);
    editor->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard |
                                    Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    editor->setContextMenuPolicy(Qt::DefaultContextMenu);
    editor->setStyleSheet(
        QStringLiteral("QTextBrowser { background: transparent; border: none; }"));

    auto *copy_shortcut = new QShortcut(QKeySequence::Copy, editor);
    copy_shortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(copy_shortcut, &QShortcut::activated, editor, &QTextBrowser::copy);

    auto *select_all_shortcut = new QShortcut(QKeySequence::SelectAll, editor);
    select_all_shortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(select_all_shortcut, &QShortcut::activated, editor, &QTextBrowser::selectAll);
}

QString anchor_at_position(const QStyleOptionViewItem &option, const QModelIndex &index,
                           const QPoint &position)
{
    const QString markdown = index.data(actions_log_model_t::RENDERED_MARKDOWN_ROLE).toString();
    if (markdown.isEmpty() == true)
    {
        return {};
    }

    QStyleOptionViewItem style_option(option);
    const int text_width = qMax(0, style_option.rect.width() - (horizontal_padding * 2));
    QTextDocument document;
    configure_document(&document, style_option, markdown, text_width);

    const QPoint relative_position =
        position - QPoint(style_option.rect.left() + horizontal_padding,
                          style_option.rect.top() + vertical_padding);
    if (relative_position.x() < 0 || relative_position.y() < 0)
    {
        return {};
    }

    return document.documentLayout()->anchorAt(relative_position);
}

}  // namespace

actions_log_delegate_t::actions_log_delegate_t(QObject *parent) : QStyledItemDelegate(parent)
{
}

QSize actions_log_delegate_t::sizeHint(const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const
{
    const QString markdown = index.data(actions_log_model_t::RENDERED_MARKDOWN_ROLE).toString();
    const int text_width = this->text_width_for_option(option);
    QTextDocument document;
    configure_document(&document, option, markdown, text_width);
    const QSizeF document_size = document.documentLayout()->documentSize();
    return QSize(qMax(0, text_width + (horizontal_padding * 2)),
                 qCeil(document_size.height()) + (vertical_padding * 2));
}

void actions_log_delegate_t::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    if (painter == nullptr)
    {
        return;
    }

    QStyleOptionViewItem style_option(option);
    this->initStyleOption(&style_option, index);
    style_option.text.clear();
    style_option.icon = QIcon();

    QStyle *style =
        style_option.widget != nullptr ? style_option.widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &style_option, painter,
                         style_option.widget);

    const QString markdown = index.data(actions_log_model_t::RENDERED_MARKDOWN_ROLE).toString();
    const int text_width = this->text_width_for_option(style_option);
    QTextDocument document;
    configure_document(&document, style_option, markdown, text_width);

    painter->save();
    painter->setClipRect(style_option.rect);
    painter->translate(style_option.rect.left() + horizontal_padding,
                       style_option.rect.top() + vertical_padding);
    document.drawContents(
        painter, QRectF(0, 0, text_width, style_option.rect.height() - (vertical_padding * 2)));
    painter->restore();
}

QWidget *actions_log_delegate_t::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                              const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);

    auto *editor = new QTextBrowser(parent);
    configure_editor(editor);
    QObject::connect(editor, &QTextBrowser::anchorClicked, editor, [this](const QUrl &url) {
        if (this->link_activated_handler)
        {
            this->link_activated_handler(url.toString());
        }
    });
    return editor;
}

void actions_log_delegate_t::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *text_browser = qobject_cast<QTextBrowser *>(editor);
    if (text_browser == nullptr)
    {
        return;
    }

    const QString markdown = index.data(actions_log_model_t::RENDERED_MARKDOWN_ROLE).toString();
    text_browser->setMarkdown(markdown);
    text_browser->document()->setDocumentMargin(0);
    text_browser->document()->setTextWidth(qMax(0, text_browser->viewport()->width()));
}

void actions_log_delegate_t::updateEditorGeometry(QWidget *editor,
                                                  const QStyleOptionViewItem &option,
                                                  const QModelIndex &index) const
{
    Q_UNUSED(index);

    if (editor == nullptr)
    {
        return;
    }

    editor->setGeometry(option.rect.adjusted(horizontal_padding, vertical_padding,
                                             -horizontal_padding, -vertical_padding));
    if (auto *text_browser = qobject_cast<QTextBrowser *>(editor))
    {
        text_browser->document()->setTextWidth(qMax(0, text_browser->viewport()->width()));
    }
}

int actions_log_delegate_t::text_width_for_option(const QStyleOptionViewItem &option) const
{
    int width = option.rect.width();
    if (width <= 0 && option.widget != nullptr)
    {
        if (const auto *view = qobject_cast<const QAbstractItemView *>(option.widget))
        {
            width = view->viewport()->width();
        }
        else
        {
            width = option.widget->width();
        }
    }

    return qMax(0, width - (horizontal_padding * 2));
}

bool actions_log_delegate_t::editorEvent(QEvent *event, QAbstractItemModel *model,
                                         const QStyleOptionViewItem &option,
                                         const QModelIndex &index)
{
    Q_UNUSED(model);

    if (event == nullptr)
    {
        return false;
    }

    if (event->type() != QEvent::MouseButtonRelease)
    {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    const auto *mouse_event = dynamic_cast<QMouseEvent *>(event);
    if (mouse_event == nullptr || mouse_event->button() != Qt::LeftButton)
    {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    const QString anchor = anchor_at_position(option, index, mouse_event->pos());
    if (anchor.isEmpty() == true)
    {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    if (parse_actions_log_file_link(QUrl(anchor)).has_value() == false)
    {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    if (this->link_activated_handler)
    {
        this->link_activated_handler(anchor);
    }
    return true;
}

void actions_log_delegate_t::set_link_activated_handler(
    std::function<void(const QString &href)> handler)
{
    this->link_activated_handler = std::move(handler);
}

}  // namespace qcai2
