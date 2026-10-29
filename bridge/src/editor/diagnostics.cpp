#include "diagnostics.h"
#include "editor.h"

#include "../bridge.h"
#include "positions.h"
#include "tinoxkde.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonObject>
#include <QTextCharFormat>

using namespace KTextEditor;

namespace tkde::editor
{
namespace
{
constexpr uint kError = Document::markType07;
constexpr uint kWarning = Document::markType06;
constexpr uint kInfo = Document::markType08;
constexpr uint kHint = Document::markType09;
constexpr uint kAllMarks = kError | kWarning | kInfo | kHint;

uint markFor(int severity)
{
    switch (severity) {
    case 1:
        return kError;
    case 2:
        return kWarning;
    case 3:
        return kInfo;
    default:
        return kHint;
    }
}

QString severityName(int severity)
{
    switch (severity) {
    case 1:
        return QStringLiteral("error");
    case 2:
        return QStringLiteral("warning");
    case 3:
        return QStringLiteral("information");
    default:
        return QStringLiteral("hint");
    }
}
} // namespace

QColor severityColor(int severity, const QPalette &palette)
{
    // Breeze's negative / neutral / active / inactive text colours, tuned for the
    // brightness of the editor background so the underline reads on light and dark schemes.
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    switch (severity) {
    case 1:
        return dark ? QColor(0xff, 0x6b, 0x6b) : QColor(0xda, 0x44, 0x53);
    case 2:
        return dark ? QColor(0xf6, 0x74, 0x00) : QColor(0xc9, 0x5f, 0x00);
    case 3:
        return dark ? QColor(0x3d, 0xae, 0xe9) : QColor(0x1d, 0x99, 0xf3);
    default:
        return dark ? QColor(0xa0, 0xa8, 0xb0) : QColor(0x70, 0x78, 0x80);
    }
}

Diagnostics::Diagnostics(Document *doc, QObject *parent) : QObject(parent), m_doc(doc)
{
    // The ranges belong to the document's moving interface: drop them before it goes away
    // or reloads, a diagnostic for text that no longer exists would be misleading.
    connect(doc, &Document::aboutToInvalidateMovingInterfaceContent, this, [this] { clearAll(); });
    qApp->installEventFilter(this); // palette changes arrive as QEvent::ApplicationPaletteChange
}

bool Diagnostics::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange) {
        refreshStyles();
    }
    return false;
}

Diagnostics::~Diagnostics()
{
    // Runs while the Document is being destroyed, when its moving interface is gone: the ranges
    // were already freed by the release hook (editor.cpp) while the document was still alive.
    qApp->removeEventFilter(this);
}

Attribute::Ptr Diagnostics::attributeFor(int severity) const
{
    Attribute::Ptr attr(new Attribute);
    attr->setUnderlineStyle(severity >= 4 ? QTextCharFormat::DotLine : QTextCharFormat::WaveUnderline);
    attr->setUnderlineColor(severityColor(severity, QGuiApplication::palette()));
    return attr;
}

void Diagnostics::clear(const QString &owner)
{
    auto it = m_items.find(owner);
    if (it == m_items.end()) {
        return;
    }
    for (Item &i : it.value()) {
        delete i.range;
    }
    m_items.erase(it);
}

void Diagnostics::clearAll()
{
    for (auto &list : m_items) {
        for (Item &i : list) {
            delete i.range;
        }
    }
    m_items.clear();
    refreshMarks();
}

bool Diagnostics::set(const QString &owner, const QJsonArray &items, QString &error)
{
    QList<Item> parsed;
    int n = 0;
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        Range r;
        if (!readRange(o.value(QStringLiteral("range")), m_doc, r, error)) {
            error = QStringLiteral("diagnostic %1: %2").arg(n).arg(error);
            return false;
        }
        const int severity = o.value(QStringLiteral("severity")).toInt(1);
        if (severity < 1 || severity > 4) {
            error = QStringLiteral("diagnostic %1: severity must be 1 (error) to 4 (hint)").arg(n);
            return false;
        }
        if (!o.value(QStringLiteral("message")).isString()) {
            error = QStringLiteral("diagnostic %1 has no string \"message\"").arg(n);
            return false;
        }
        // an empty range would be invisible: widen it to the neighbouring character
        if (r.isEmpty()) {
            const int line = r.start().line();
            if (r.start().column() < m_doc->lineLength(line)) {
                r.setEnd(Cursor(line, r.start().column() + 1));
            } else if (r.start().column() > 0) {
                r.setStart(Cursor(line, r.start().column() - 1));
            }
        }
        Item item;
        item.pendingRange = r; // the moving range is created once the whole list is valid
        item.severity = severity;
        item.message = o.value(QStringLiteral("message")).toString();
        item.code = o.value(QStringLiteral("code")).toVariant().toString();
        item.source = o.value(QStringLiteral("source")).toString();
        parsed.append(item);
        ++n;
    }
    for (Item &item : parsed) {
        item.range = m_doc->newMovingRange(item.pendingRange, MovingRange::DoNotExpand);
        item.range->setAttribute(attributeFor(item.severity));
    }
    clear(owner); // only after every item was accepted
    if (!parsed.isEmpty()) {
        m_items.insert(owner, parsed);
    }
    refreshMarks();
    return true;
}

void Diagnostics::refreshMarks()
{
    // clear our mark bits on every line, then set them from where the ranges are NOW
    const auto marks = m_doc->marks();
    const QList<int> lines = marks.keys();
    for (int line : lines) {
        const uint ours = marks.value(line)->type & kAllMarks;
        if (ours) {
            m_doc->removeMark(line, ours);
        }
    }
    for (const auto &list : std::as_const(m_items)) {
        for (const Item &i : list) {
            m_doc->addMark(i.range->start().line(), markFor(i.severity));
        }
    }
}

void Diagnostics::refreshStyles()
{
    for (auto &list : m_items) {
        for (Item &i : list) {
            i.range->setAttribute(attributeFor(i.severity));
        }
    }
}

QJsonArray Diagnostics::toJson(const QString &owner) const
{
    QJsonArray out;
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (!owner.isEmpty() && it.key() != owner) {
            continue;
        }
        for (const Item &i : it.value()) {
            QJsonObject o;
            o.insert(QStringLiteral("owner"), it.key());
            o.insert(QStringLiteral("range"), tkde::editor::toJson(i.range->toRange()));
            o.insert(QStringLiteral("severity"), i.severity);
            o.insert(QStringLiteral("message"), i.message);
            if (!i.code.isEmpty()) {
                o.insert(QStringLiteral("code"), i.code);
            }
            if (!i.source.isEmpty()) {
                o.insert(QStringLiteral("source"), i.source);
            }
            o.insert(QStringLiteral("color"), i.range->attribute() ? i.range->attribute()->underlineColor().name() : QString());
            out.append(o);
        }
    }
    return out;
}

QString Diagnostics::hintAt(Cursor position) const
{
    QStringList parts;
    for (const auto &list : m_items) {
        for (const Item &i : list) {
            if (i.range->toRange().contains(position)) {
                QString line = QStringLiteral("<b>%1</b>").arg(severityName(i.severity).toHtmlEscaped());
                if (!i.source.isEmpty() || !i.code.isEmpty()) {
                    line += QStringLiteral(" <i>(%1)</i>")
                                .arg(QString(i.source + (i.source.isEmpty() || i.code.isEmpty() ? QString() : QStringLiteral(" ")) + i.code)
                                         .toHtmlEscaped());
                }
                line += QStringLiteral("<br>") + i.message.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
                parts.append(line);
            }
        }
    }
    return parts.join(QStringLiteral("<hr>"));
}

int Diagnostics::count() const
{
    int n = 0;
    for (const auto &list : m_items) {
        n += list.size();
    }
    return n;
}

void installMarkDefaults(Document *doc)
{
    doc->setEditableMarks(0); // the user cannot toggle ours
    doc->setMarkDescription(Document::MarkTypes(kError), QStringLiteral("Error"));
    doc->setMarkDescription(Document::MarkTypes(kWarning), QStringLiteral("Warning"));
    doc->setMarkDescription(Document::MarkTypes(kInfo), QStringLiteral("Information"));
    doc->setMarkDescription(Document::MarkTypes(kHint), QStringLiteral("Hint"));
    doc->setMarkIcon(Document::MarkTypes(kError), QIcon::fromTheme(QStringLiteral("dialog-error")));
    doc->setMarkIcon(Document::MarkTypes(kWarning), QIcon::fromTheme(QStringLiteral("dialog-warning")));
    doc->setMarkIcon(Document::MarkTypes(kInfo), QIcon::fromTheme(QStringLiteral("dialog-information")));
    doc->setMarkIcon(Document::MarkTypes(kHint), QIcon::fromTheme(QStringLiteral("help-hint")));
}

namespace
{
Diagnostics *diagnosticsOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    auto *doc = qobject_cast<Document *>(o);
    if (!doc) {
        fail = Reply::failure(t.isDouble() ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG,
                              QStringLiteral("this command needs the \"target\" id of a document"));
        return nullptr;
    }
    return doc->findChild<Diagnostics *>(QString(), Qt::FindChildrenRecursively);
}
} // namespace

void registerDiagnosticCommands(Bridge &bridge)
{
    bridge.registerCommand(QStringLiteral("editor.setDiagnostics"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        Diagnostics *d = diagnosticsOf(bridge, cmd, fail);
        if (!d) {
            return fail;
        }
        const QJsonValue version = cmd.value(QStringLiteral("version"));
        if (!version.isUndefined()) {
            if (!version.isDouble() || version.toDouble() < 0 || version.toDouble() != double(version.toInteger())) {
                return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("version must be a nonnegative integer"));
            }
            auto *doc = qobject_cast<Document *>(bridge.object(cmd.value(QStringLiteral("target")).toInteger()));
            if (version.toInteger() != documentVersion(doc)) {
                return Reply::failure(TKDE_E_FAILED, QStringLiteral("document changed before diagnostics could be applied"));
            }
        }
        QString error;
        const QString owner = cmd.value(QStringLiteral("owner")).toString(QStringLiteral("default"));
        if (!d->set(owner, cmd.value(QStringLiteral("items")).toArray(), error)) {
            return Reply::failure(TKDE_E_INVALID_ARG, error);
        }
        QJsonObject f;
        f.insert(QStringLiteral("count"), d->count());
        return Reply::success(f);
    });
    bridge.registerCommand(QStringLiteral("editor.getDiagnostics"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        Diagnostics *d = diagnosticsOf(bridge, cmd, fail);
        if (!d) {
            return fail;
        }
        QJsonObject f;
        f.insert(QStringLiteral("items"), d->toJson(cmd.value(QStringLiteral("owner")).toString()));
        return Reply::success(f);
    });
    if (bridge.debugCommandsEnabled()) {
        // Test hooks: the gutter marks of a document, and the hover text at a position.
        bridge.registerCommand(QStringLiteral("editor.debug.marks"), [&bridge](const QJsonObject &cmd) {
            Reply fail;
            if (!diagnosticsOf(bridge, cmd, fail)) {
                return fail;
            }
            auto *doc = qobject_cast<Document *>(bridge.object(int64_t(cmd.value(QStringLiteral("target")).toDouble())));
            QJsonObject marks;
            const auto all = doc->marks();
            for (auto it = all.begin(); it != all.end(); ++it) {
                marks.insert(QString::number(it.key()), double(it.value()->type));
            }
            QJsonObject f;
            f.insert(QStringLiteral("marks"), marks);
            return Reply::success(f);
        });
        bridge.registerCommand(QStringLiteral("editor.debug.hint"), [&bridge](const QJsonObject &cmd) {
            Reply fail;
            Diagnostics *d = diagnosticsOf(bridge, cmd, fail);
            if (!d) {
                return fail;
            }
            auto *doc = qobject_cast<Document *>(bridge.object(int64_t(cmd.value(QStringLiteral("target")).toDouble())));
            Cursor c;
            QString error;
            if (!readPosition(cmd.value(QStringLiteral("position")), doc, c, error)) {
                return Reply::failure(TKDE_E_INVALID_ARG, error);
            }
            QJsonObject f;
            f.insert(QStringLiteral("text"), d->hintAt(c));
            return Reply::success(f);
        });
        // Test hook: switch the application palette between a dark and a light scheme.
        bridge.registerCommand(QStringLiteral("editor.debug.setPalette"), [](const QJsonObject &cmd) {
            QPalette p;
            if (cmd.contains(QStringLiteral("base"))) { // any background, e.g. the pure black and white of a high-contrast scheme
                const QColor base(cmd.value(QStringLiteral("base")).toString());
                p.setColor(QPalette::Base, base);
                p.setColor(QPalette::Text, base.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black));
            } else if (cmd.value(QStringLiteral("dark")).toBool()) {
                p.setColor(QPalette::Base, QColor(0x1b, 0x1e, 0x20));
                p.setColor(QPalette::Text, QColor(0xfc, 0xfc, 0xfc));
            } else {
                p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
                p.setColor(QPalette::Text, QColor(0x23, 0x26, 0x29));
            }
            QApplication::setPalette(p);
            return Reply::success();
        });
    }
}
} // namespace tkde::editor
