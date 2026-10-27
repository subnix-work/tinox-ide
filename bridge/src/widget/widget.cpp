#include "widget.h"

#include "../bridge.h"
#include "tinoxkde.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QBoxLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QTreeView>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace tkde::widget
{
namespace
{
using Setter = std::function<void(QWidget *, const QJsonValue &)>;

enum class Kind { Str, Bool, Num, Arr, Obj };

QJsonObject one(const char *name, const QJsonValue &v)
{
    QJsonObject o;
    o.insert(QString::fromLatin1(name), v);
    return o;
}

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Str:
        return QStringLiteral("a string");
    case Kind::Bool:
        return QStringLiteral("a boolean");
    case Kind::Num:
        return QStringLiteral("a number");
    case Kind::Arr:
        return QStringLiteral("an array");
    case Kind::Obj:
        return QStringLiteral("an object");
    }
    return QString();
}

bool kindMatches(Kind k, const QJsonValue &v)
{
    switch (k) {
    case Kind::Str:
        return v.isString();
    case Kind::Bool:
        return v.isBool();
    case Kind::Num:
        return v.isDouble();
    case Kind::Arr:
        return v.isArray();
    case Kind::Obj:
        return v.isObject();
    }
    return false;
}

struct PropDef {
    Kind kind;
    QJsonValue reset;    // applied when a prop disappears from the tree
    QStringList choices; // for strings that name an enum value
    Setter set;
};

struct TypeDef {
    std::function<QWidget *()> make;
    std::map<QString, PropDef> props;
};

QIcon iconOf(const QJsonValue &v)
{
    return v.toString().isEmpty() ? QIcon() : QIcon::fromTheme(v.toString());
}

QSize sizeOf(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return QSize(a.at(0).toInt(), a.at(1).toInt());
}

QString str(const QJsonValue &v)
{
    return v.toString();
}

const std::map<QString, Qt::DockWidgetArea> &dockAreas()
{
    static const std::map<QString, Qt::DockWidgetArea> m{{QStringLiteral("left"), Qt::LeftDockWidgetArea},
                                                         {QStringLiteral("right"), Qt::RightDockWidgetArea},
                                                         {QStringLiteral("top"), Qt::TopDockWidgetArea},
                                                         {QStringLiteral("bottom"), Qt::BottomDockWidgetArea}};
    return m;
}

template <class W>
PropDef def(Kind kind, QJsonValue reset, std::function<void(W *, const QJsonValue &)> set, QStringList choices = {})
{
    return PropDef{kind, std::move(reset), std::move(choices), [set](QWidget *w, const QJsonValue &v) { set(static_cast<W *>(w), v); }};
}

template <class W>
using Fn = std::function<void(W *, const QJsonValue &)>;

/// Props every widget has.
void addCommon(TypeDef &t)
{
    t.props.emplace(QStringLiteral("enabled"),
                    def<QWidget>(Kind::Bool, true, Fn<QWidget>([](QWidget *w, const QJsonValue &v) { w->setEnabled(v.toBool()); })));
    t.props.emplace(QStringLiteral("visible"),
                    def<QWidget>(Kind::Bool, true, Fn<QWidget>([](QWidget *w, const QJsonValue &v) { w->setVisible(v.toBool()); })));
    t.props.emplace(QStringLiteral("toolTip"),
                    def<QWidget>(Kind::Str, QString(), Fn<QWidget>([](QWidget *w, const QJsonValue &v) { w->setToolTip(str(v)); })));
    t.props.emplace(QStringLiteral("statusTip"),
                    def<QWidget>(Kind::Str, QString(), Fn<QWidget>([](QWidget *w, const QJsonValue &v) { w->setStatusTip(str(v)); })));
    t.props.emplace(
        QStringLiteral("minimumSize"),
        def<QWidget>(Kind::Arr, QJsonArray{0, 0}, Fn<QWidget>([](QWidget *w, const QJsonValue &v) { w->setMinimumSize(sizeOf(v)); })));
    t.props.emplace(QStringLiteral("windowTitle"),
                    def<QWidget>(Kind::Str, QString(), Fn<QWidget>([](QWidget *w, const QJsonValue &v) { w->setWindowTitle(str(v)); })));
    t.props.emplace(QStringLiteral("windowIcon"),
                    def<QWidget>(Kind::Str, QString(), Fn<QWidget>([](QWidget *w, const QJsonValue &v) { w->setWindowIcon(iconOf(v)); })));
}

template <class W>
TypeDef makeType()
{
    TypeDef t;
    t.make = [] { return static_cast<QWidget *>(new W); };
    addCommon(t);
    return t;
}

template <class W>
void add(TypeDef &t, const char *name, Kind kind, QJsonValue reset, Fn<W> set, QStringList choices = {})
{
    t.props.emplace(QString::fromLatin1(name), def<W>(kind, std::move(reset), std::move(set), std::move(choices)));
}

/// Props every item view shares.
template <class W>
void addViewProps(TypeDef &t)
{
    add<W>(t, "alternatingRowColors", Kind::Bool, false, [](W *w, const QJsonValue &v) { w->setAlternatingRowColors(v.toBool()); });
    add<W>(t, "selectionMode", Kind::Str, QStringLiteral("single"),
           [](W *w, const QJsonValue &v) {
               static const std::map<QString, QAbstractItemView::SelectionMode> modes{
                   {QStringLiteral("none"), QAbstractItemView::NoSelection},
                   {QStringLiteral("single"), QAbstractItemView::SingleSelection},
                   {QStringLiteral("extended"), QAbstractItemView::ExtendedSelection},
                   {QStringLiteral("multi"), QAbstractItemView::MultiSelection}};
               w->setSelectionMode(modes.at(str(v)));
           },
           {QStringLiteral("none"), QStringLiteral("single"), QStringLiteral("extended"), QStringLiteral("multi")});
    add<W>(t, "dragDrop", Kind::Bool, false, [](W *w, const QJsonValue &v) {
        w->setDragEnabled(v.toBool());
        w->setAcceptDrops(v.toBool());
        w->setDropIndicatorShown(v.toBool());
        w->setDragDropMode(v.toBool() ? QAbstractItemView::DragDrop : QAbstractItemView::NoDragDrop);
        w->setDefaultDropAction(Qt::MoveAction);
    });
    add<W>(t, "editable", Kind::Bool, false, [](W *w, const QJsonValue &v) {
        w->setEditTriggers(v.toBool() ? QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked
                                      : QAbstractItemView::NoEditTriggers);
    });
}

QStringList dockChoices()
{
    QStringList l;
    for (const auto &e : dockAreas()) {
        l << e.first;
    }
    return l;
}

const std::map<QString, TypeDef> &types()
{
    static const std::map<QString, TypeDef> table = [] {
        std::map<QString, TypeDef> m;

        m.emplace(QStringLiteral("QWidget"), makeType<QWidget>());

        {
            TypeDef t = makeType<QMainWindow>();
            add<QMainWindow>(t, "size", Kind::Arr, QJsonArray{960, 640}, [](QMainWindow *w, const QJsonValue &v) { w->resize(sizeOf(v)); });
            add<QMainWindow>(t, "interceptClose", Kind::Bool, true,
                             [](QMainWindow *w, const QJsonValue &v) { w->setProperty("tkdeInterceptClose", v.toBool()); });
            add<QMainWindow>(t, "dockNestingEnabled", Kind::Bool, false,
                             [](QMainWindow *w, const QJsonValue &v) { w->setDockNestingEnabled(v.toBool()); });
            m.emplace(QStringLiteral("QMainWindow"), std::move(t));
        }
        {
            TypeDef t = makeType<QDockWidget>();
            add<QDockWidget>(t, "title", Kind::Str, QString(), [](QDockWidget *w, const QJsonValue &v) { w->setWindowTitle(str(v)); });
            add<QDockWidget>(t, "floating", Kind::Bool, false, [](QDockWidget *w, const QJsonValue &v) { w->setFloating(v.toBool()); });
            add<QDockWidget>(t, "closable", Kind::Bool, true, [](QDockWidget *w, const QJsonValue &v) {
                w->setFeatures(v.toBool() ? w->features() | QDockWidget::DockWidgetClosable
                                          : w->features() & ~QDockWidget::DockWidgetClosable);
            });
            m.emplace(QStringLiteral("QDockWidget"), std::move(t));
        }
        {
            TypeDef t = makeType<QSplitter>();
            add<QSplitter>(t, "orientation", Kind::Str, QStringLiteral("horizontal"),
                           [](QSplitter *w, const QJsonValue &v) {
                               w->setOrientation(str(v) == QLatin1String("vertical") ? Qt::Vertical : Qt::Horizontal);
                           },
                           {QStringLiteral("horizontal"), QStringLiteral("vertical")});
            add<QSplitter>(t, "sizes", Kind::Arr, QJsonArray(), [](QSplitter *w, const QJsonValue &v) {
                QList<int> sizes;
                for (const QJsonValue &s : v.toArray()) {
                    sizes.append(s.toInt());
                }
                w->setSizes(sizes);
            });
            add<QSplitter>(t, "childrenCollapsible", Kind::Bool, true,
                           [](QSplitter *w, const QJsonValue &v) { w->setChildrenCollapsible(v.toBool()); });
            m.emplace(QStringLiteral("QSplitter"), std::move(t));
        }
        {
            TypeDef t = makeType<QTabWidget>();
            add<QTabWidget>(t, "tabsClosable", Kind::Bool, false,
                            [](QTabWidget *w, const QJsonValue &v) { w->setTabsClosable(v.toBool()); });
            add<QTabWidget>(t, "movable", Kind::Bool, false, [](QTabWidget *w, const QJsonValue &v) { w->setMovable(v.toBool()); });
            add<QTabWidget>(t, "documentMode", Kind::Bool, false,
                            [](QTabWidget *w, const QJsonValue &v) { w->setDocumentMode(v.toBool()); });
            add<QTabWidget>(t, "currentIndex", Kind::Num, 0, [](QTabWidget *w, const QJsonValue &v) { w->setCurrentIndex(v.toInt()); });
            m.emplace(QStringLiteral("QTabWidget"), std::move(t));
        }
        {
            TypeDef t = makeType<QToolBar>();
            add<QToolBar>(t, "title", Kind::Str, QString(), [](QToolBar *w, const QJsonValue &v) { w->setWindowTitle(str(v)); });
            add<QToolBar>(t, "movable", Kind::Bool, true, [](QToolBar *w, const QJsonValue &v) { w->setMovable(v.toBool()); });
            add<QToolBar>(t, "iconSize", Kind::Num, 22,
                          [](QToolBar *w, const QJsonValue &v) { w->setIconSize(QSize(v.toInt(), v.toInt())); });
            add<QToolBar>(
                t, "toolButtonStyle", Kind::Str, QStringLiteral("icon"),
                [](QToolBar *w, const QJsonValue &v) {
                    static const std::map<QString, Qt::ToolButtonStyle> s{{QStringLiteral("icon"), Qt::ToolButtonIconOnly},
                                                                          {QStringLiteral("text"), Qt::ToolButtonTextOnly},
                                                                          {QStringLiteral("textBesideIcon"), Qt::ToolButtonTextBesideIcon},
                                                                          {QStringLiteral("textUnderIcon"), Qt::ToolButtonTextUnderIcon}};
                    w->setToolButtonStyle(s.at(str(v)));
                },
                {QStringLiteral("icon"), QStringLiteral("text"), QStringLiteral("textBesideIcon"), QStringLiteral("textUnderIcon")});
            m.emplace(QStringLiteral("QToolBar"), std::move(t));
        }
        {
            TypeDef t = makeType<QStatusBar>();
            add<QStatusBar>(t, "message", Kind::Str, QString(), [](QStatusBar *w, const QJsonValue &v) { w->showMessage(str(v)); });
            add<QStatusBar>(t, "sizeGrip", Kind::Bool, true, [](QStatusBar *w, const QJsonValue &v) { w->setSizeGripEnabled(v.toBool()); });
            m.emplace(QStringLiteral("QStatusBar"), std::move(t));
        }
        {
            TypeDef t = makeType<QLabel>();
            add<QLabel>(t, "text", Kind::Str, QString(), [](QLabel *w, const QJsonValue &v) { w->setText(str(v)); });
            add<QLabel>(t, "wordWrap", Kind::Bool, false, [](QLabel *w, const QJsonValue &v) { w->setWordWrap(v.toBool()); });
            add<QLabel>(t, "textFormat", Kind::Str, QStringLiteral("auto"),
                        [](QLabel *w, const QJsonValue &v) {
                            const QString f = str(v);
                            w->setTextFormat(f == QLatin1String("plain")  ? Qt::PlainText
                                             : f == QLatin1String("rich") ? Qt::RichText
                                                                          : Qt::AutoText);
                        },
                        {QStringLiteral("auto"), QStringLiteral("plain"), QStringLiteral("rich")});
            m.emplace(QStringLiteral("QLabel"), std::move(t));
        }
        {
            TypeDef t = makeType<QLineEdit>();
            add<QLineEdit>(t, "text", Kind::Str, QString(), [](QLineEdit *w, const QJsonValue &v) {
                if (w->text() != str(v)) {
                    w->setText(str(v));
                }
            });
            add<QLineEdit>(t, "placeholder", Kind::Str, QString(),
                           [](QLineEdit *w, const QJsonValue &v) { w->setPlaceholderText(str(v)); });
            add<QLineEdit>(t, "readOnly", Kind::Bool, false, [](QLineEdit *w, const QJsonValue &v) { w->setReadOnly(v.toBool()); });
            add<QLineEdit>(t, "clearButton", Kind::Bool, false,
                           [](QLineEdit *w, const QJsonValue &v) { w->setClearButtonEnabled(v.toBool()); });
            m.emplace(QStringLiteral("QLineEdit"), std::move(t));
        }
        {
            TypeDef t = makeType<QPushButton>();
            add<QPushButton>(t, "text", Kind::Str, QString(), [](QPushButton *w, const QJsonValue &v) { w->setText(str(v)); });
            add<QPushButton>(t, "icon", Kind::Str, QString(), [](QPushButton *w, const QJsonValue &v) { w->setIcon(iconOf(v)); });
            add<QPushButton>(t, "checkable", Kind::Bool, false, [](QPushButton *w, const QJsonValue &v) { w->setCheckable(v.toBool()); });
            add<QPushButton>(t, "checked", Kind::Bool, false, [](QPushButton *w, const QJsonValue &v) { w->setChecked(v.toBool()); });
            add<QPushButton>(t, "flat", Kind::Bool, false, [](QPushButton *w, const QJsonValue &v) { w->setFlat(v.toBool()); });
            m.emplace(QStringLiteral("QPushButton"), std::move(t));
        }
        {
            TypeDef t = makeType<QCheckBox>();
            add<QCheckBox>(t, "text", Kind::Str, QString(), [](QCheckBox *w, const QJsonValue &v) { w->setText(str(v)); });
            add<QCheckBox>(t, "checked", Kind::Bool, false, [](QCheckBox *w, const QJsonValue &v) { w->setChecked(v.toBool()); });
            m.emplace(QStringLiteral("QCheckBox"), std::move(t));
        }
        {
            TypeDef t = makeType<QComboBox>();
            add<QComboBox>(t, "items", Kind::Arr, QJsonArray(), [](QComboBox *w, const QJsonValue &v) {
                const int current = w->currentIndex();
                w->clear();
                for (const QJsonValue &i : v.toArray()) {
                    if (i.isObject()) {
                        w->addItem(iconOf(i.toObject().value(QStringLiteral("icon"))),
                                   i.toObject().value(QStringLiteral("text")).toString());
                    } else {
                        w->addItem(i.toString());
                    }
                }
                w->setCurrentIndex(std::min(current, w->count() - 1));
            });
            add<QComboBox>(t, "currentIndex", Kind::Num, 0, [](QComboBox *w, const QJsonValue &v) { w->setCurrentIndex(v.toInt()); });
            add<QComboBox>(t, "editable", Kind::Bool, false, [](QComboBox *w, const QJsonValue &v) { w->setEditable(v.toBool()); });
            m.emplace(QStringLiteral("QComboBox"), std::move(t));
        }
        {
            TypeDef t = makeType<QTreeView>();
            addViewProps<QTreeView>(t);
            add<QTreeView>(t, "headerHidden", Kind::Bool, false, [](QTreeView *w, const QJsonValue &v) { w->setHeaderHidden(v.toBool()); });
            add<QTreeView>(t, "rootIsDecorated", Kind::Bool, true,
                           [](QTreeView *w, const QJsonValue &v) { w->setRootIsDecorated(v.toBool()); });
            add<QTreeView>(t, "uniformRowHeights", Kind::Bool, false,
                           [](QTreeView *w, const QJsonValue &v) { w->setUniformRowHeights(v.toBool()); });
            m.emplace(QStringLiteral("QTreeView"), std::move(t));
        }
        {
            TypeDef t = makeType<QListView>();
            addViewProps<QListView>(t);
            add<QListView>(t, "uniformItemSizes", Kind::Bool, false,
                           [](QListView *w, const QJsonValue &v) { w->setUniformItemSizes(v.toBool()); });
            m.emplace(QStringLiteral("QListView"), std::move(t));
        }
        {
            TypeDef t = makeType<QTableView>();
            addViewProps<QTableView>(t);
            add<QTableView>(t, "horizontalHeaderVisible", Kind::Bool, true,
                            [](QTableView *w, const QJsonValue &v) { w->horizontalHeader()->setVisible(v.toBool()); });
            add<QTableView>(t, "verticalHeaderVisible", Kind::Bool, true,
                            [](QTableView *w, const QJsonValue &v) { w->verticalHeader()->setVisible(v.toBool()); });
            add<QTableView>(t, "stretchLastSection", Kind::Bool, false,
                            [](QTableView *w, const QJsonValue &v) { w->horizontalHeader()->setStretchLastSection(v.toBool()); });
            m.emplace(QStringLiteral("QTableView"), std::move(t));
        }
        {
            TypeDef t = makeType<QPlainTextEdit>();
            add<QPlainTextEdit>(t, "text", Kind::Str, QString(), [](QPlainTextEdit *w, const QJsonValue &v) { w->setPlainText(str(v)); });
            add<QPlainTextEdit>(t, "readOnly", Kind::Bool, false,
                                [](QPlainTextEdit *w, const QJsonValue &v) { w->setReadOnly(v.toBool()); });
            add<QPlainTextEdit>(t, "maximumBlockCount", Kind::Num, 0,
                                [](QPlainTextEdit *w, const QJsonValue &v) { w->setMaximumBlockCount(v.toInt()); });
            add<QPlainTextEdit>(t, "lineWrap", Kind::Bool, true, [](QPlainTextEdit *w, const QJsonValue &v) {
                w->setLineWrapMode(v.toBool() ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
            });
            m.emplace(QStringLiteral("QPlainTextEdit"), std::move(t));
        }
        {
            TypeDef t = makeType<QStackedWidget>();
            add<QStackedWidget>(t, "currentIndex", Kind::Num, 0,
                                [](QStackedWidget *w, const QJsonValue &v) { w->setCurrentIndex(v.toInt()); });
            m.emplace(QStringLiteral("QStackedWidget"), std::move(t));
        }
        {
            TypeDef t = makeType<QScrollArea>();
            add<QScrollArea>(t, "widgetResizable", Kind::Bool, true,
                             [](QScrollArea *w, const QJsonValue &v) { w->setWidgetResizable(v.toBool()); });
            m.emplace(QStringLiteral("QScrollArea"), std::move(t));
        }
        {
            // an existing widget owned by another module (an editor view, a terminal, ...)
            TypeDef t;
            t.make = nullptr;
            m.emplace(QStringLiteral("Embed"), std::move(t));
        }
        (void)dockChoices;
        return m;
    }();
    return table;
}

// -------------------------------------------------------------------- tree

struct Node {
    QString key;
    QString type;
    QJsonObject props;
    QJsonObject place;
    QJsonObject layout;
    QJsonObject lateProps; // currentIndex / sizes as last applied, see applyLate()
    QPointer<QWidget> widget;
    int64_t id = 0;
    bool external = false;
    int64_t object = 0; // Embed: the embedded object's id
    std::vector<std::unique_ptr<Node>> children;
};

QString layoutKind(const QJsonObject &layout)
{
    return layout.isEmpty() ? QString() : layout.value(QStringLiteral("kind")).toString();
}

/// Intercepts the close button of a window: the logic decides (docs/widget.md).
class CloseGuard : public QObject
{
    Q_OBJECT
public:
    CloseGuard(Bridge &bridge, QWidget *window, const QString &key, int64_t id) : QObject(window), m_bridge(bridge), m_key(key), m_id(id)
    {
        window->installEventFilter(this);
    }
    bool allow = false;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Close && !allow && watched->property("tkdeInterceptClose").toBool()) {
            event->ignore();
            QJsonObject f;
            f.insert(QStringLiteral("key"), m_key);
            m_bridge.emitEvent(QStringLiteral("widget.closeRequested"), f, m_id);
            return true;
        }
        return false;
    }

private:
    Bridge &m_bridge;
    QString m_key;
    int64_t m_id;
};

class Mount : public QObject
{
    Q_OBJECT
public:
    Mount(Bridge &bridge) : m_bridge(bridge) {}

    ~Mount() override
    {
        detachExternals(root.get());
        delete rootWidget.data();
    }

    Bridge &bridge() { return m_bridge; }

    std::unique_ptr<Node> root;
    QPointer<QWidget> rootWidget;
    std::map<QString, Node *> byKey;

    void index(Node *n)
    {
        byKey[n->key] = n;
        for (auto &c : n->children) {
            index(c.get());
        }
    }

private:
    static void detachExternals(Node *n)
    {
        if (!n) {
            return;
        }
        for (auto &c : n->children) {
            detachExternals(c.get());
        }
        if (n->external && n->widget) {
            n->widget->hide();
            n->widget->setParent(nullptr);
        }
    }

    Bridge &m_bridge;
};

// ---- validation: the whole tree is checked before anything is built or touched

bool validate(Bridge &bridge, const QJsonObject &json, std::set<QString> &keys, QString &error, bool isRoot = false)
{
    const QString key = json.value(QStringLiteral("key")).toString();
    const QString type = json.value(QStringLiteral("type")).toString();
    if (key.isEmpty()) {
        error = QStringLiteral("every node needs a string \"key\"");
        return false;
    }
    if (!keys.insert(key).second) {
        error = QStringLiteral("duplicate node key '%1'").arg(key);
        return false;
    }
    const auto typeIt = types().find(type);
    if (typeIt == types().end()) {
        error = QStringLiteral("node '%1': unknown type '%2'").arg(key, type);
        return false;
    }
    if (type == QLatin1String("Embed")) {
        const QJsonValue o = json.value(QStringLiteral("object"));
        auto *w = o.isDouble() ? qobject_cast<QWidget *>(bridge.object(int64_t(o.toDouble()))) : nullptr;
        if (!w) {
            error = QStringLiteral("node '%1': \"object\" must be the id of a widget").arg(key);
            return false;
        }
    }
    const QJsonValue props = json.value(QStringLiteral("props"));
    if (!props.isUndefined() && !props.isObject()) {
        error = QStringLiteral("node '%1': \"props\" must be an object").arg(key);
        return false;
    }
    const QJsonObject po = props.toObject();
    for (auto it = po.begin(); it != po.end(); ++it) {
        const auto def = typeIt->second.props.find(it.key());
        if (def == typeIt->second.props.end()) {
            error = QStringLiteral("node '%1' (%2): unknown prop '%3'").arg(key, type, it.key());
            return false;
        }
        if (!kindMatches(def->second.kind, it.value())) {
            error = QStringLiteral("node '%1' (%2): prop '%3' must be %4").arg(key, type, it.key(), kindName(def->second.kind));
            return false;
        }
        if (!def->second.choices.isEmpty() && !def->second.choices.contains(it.value().toString())) {
            error = QStringLiteral("node '%1' (%2): prop '%3' must be one of %4")
                        .arg(key, type, it.key(), def->second.choices.join(QLatin1String(", ")));
            return false;
        }
        if (def->second.kind == Kind::Arr && it.key() != QLatin1String("items") && it.key() != QLatin1String("sizes") &&
            it.value().toArray().size() != 2) {
            error = QStringLiteral("node '%1' (%2): prop '%3' must have two numbers").arg(key, type, it.key());
            return false;
        }
    }
    const QJsonValue layout = json.value(QStringLiteral("layout"));
    if (!layout.isUndefined()) {
        const QString kind = layout.toObject().value(QStringLiteral("kind")).toString();
        if (!layout.isObject() || (kind != QLatin1String("box") && kind != QLatin1String("grid") && kind != QLatin1String("form"))) {
            error = QStringLiteral("node '%1': \"layout\" needs a \"kind\" of box, grid or form").arg(key);
            return false;
        }
        if (type != QLatin1String("QWidget")) {
            error = QStringLiteral("node '%1': only QWidget nodes take a \"layout\"").arg(key);
            return false;
        }
    }
    const QJsonValue place = json.value(QStringLiteral("place"));
    if (!place.isUndefined() && !place.isObject()) {
        error = QStringLiteral("node '%1': \"place\" must be an object").arg(key);
        return false;
    }
    const QString area = place.toObject().value(QStringLiteral("area")).toString();
    if (!area.isEmpty() && dockAreas().find(area) == dockAreas().end()) {
        error = QStringLiteral("node '%1': place.area must be left, right, top or bottom").arg(key);
        return false;
    }
    const QJsonValue children = json.value(QStringLiteral("children"));
    if (!children.isUndefined() && !children.isArray()) {
        error = QStringLiteral("node '%1': \"children\" must be an array").arg(key);
        return false;
    }
    if (type == QLatin1String("Embed") && !children.toArray().isEmpty()) {
        error = QStringLiteral("node '%1': an Embed node has no children").arg(key);
        return false;
    }
    for (const QJsonValue &c : children.toArray()) {
        if (!c.isObject()) {
            error = QStringLiteral("node '%1': a child is not an object").arg(key);
            return false;
        }
        if (!validate(bridge, c.toObject(), keys, error)) {
            return false;
        }
    }
    (void)isRoot;
    return true;
}

// ---- props

/// Props that refer to children or items (a current index, splitter sizes) are applied after
/// those exist, see applyLate(); everything else goes first.
bool isLate(const QString &name)
{
    return name == QLatin1String("currentIndex") || name == QLatin1String("sizes");
}

void applyProps(Node &n, const QJsonObject &props)
{
    const TypeDef &t = types().at(n.type);
    QSignalBlocker blocker(n.widget.data()); // programmatic changes are not user events
    // props that vanished go back to their default
    for (auto it = n.props.begin(); it != n.props.end(); ++it) {
        if (!props.contains(it.key()) && !isLate(it.key())) {
            const PropDef &d = t.props.at(it.key());
            d.set(n.widget, d.reset);
        }
    }
    for (auto it = props.begin(); it != props.end(); ++it) {
        if (!isLate(it.key()) && (!n.props.contains(it.key()) || n.props.value(it.key()) != it.value())) {
            t.props.at(it.key()).set(n.widget, it.value());
        }
    }
    n.props = props;
}

void applyLate(Node &n)
{
    const TypeDef &t = types().at(n.type);
    QSignalBlocker blocker(n.widget.data());
    QJsonObject wanted;
    for (auto it = n.props.begin(); it != n.props.end(); ++it) {
        if (isLate(it.key())) {
            wanted.insert(it.key(), it.value());
        }
    }
    for (auto it = n.lateProps.begin(); it != n.lateProps.end(); ++it) {
        if (!wanted.contains(it.key())) {
            const PropDef &d = t.props.at(it.key());
            d.set(n.widget, d.reset);
        }
    }
    for (auto it = wanted.begin(); it != wanted.end(); ++it) {
        if (n.lateProps.value(it.key()) != it.value()) {
            t.props.at(it.key()).set(n.widget, it.value());
        }
    }
    n.lateProps = wanted;
}

void applyLayoutSpec(Node &n, const QJsonObject &spec)
{
    auto *l = n.widget->layout();
    if (!l) {
        return;
    }
    const QJsonValue m = spec.value(QStringLiteral("margins"));
    if (m.isArray()) {
        const QJsonArray a = m.toArray();
        l->setContentsMargins(a.at(0).toInt(), a.at(1).toInt(), a.at(2).toInt(), a.at(3).toInt());
    } else if (m.isDouble()) {
        l->setContentsMargins(m.toInt(), m.toInt(), m.toInt(), m.toInt());
    }
    if (spec.contains(QStringLiteral("spacing"))) {
        l->setSpacing(spec.value(QStringLiteral("spacing")).toInt());
    }
    if (auto *box = qobject_cast<QBoxLayout *>(l)) {
        box->setDirection(spec.value(QStringLiteral("orientation")).toString() == QLatin1String("horizontal") ? QBoxLayout::LeftToRight
                                                                                                              : QBoxLayout::TopToBottom);
    }
}

// ---- placing children into their parent

QWidget *widgetOf(const std::unique_ptr<Node> &c)
{
    return c->widget.data();
}

void place(Node &parent, Node &child, int index)
{
    QWidget *pw = parent.widget;
    QWidget *w = child.widget;
    if (auto *mw = qobject_cast<QMainWindow *>(pw)) {
        if (auto *dock = qobject_cast<QDockWidget *>(w)) {
            const auto area = dockAreas().find(child.place.value(QStringLiteral("area")).toString(QStringLiteral("left")));
            mw->addDockWidget(area->second, dock);
        } else if (auto *tb = qobject_cast<QToolBar *>(w)) {
            mw->addToolBar(tb);
        } else if (auto *sb = qobject_cast<QStatusBar *>(w)) {
            mw->setStatusBar(sb);
        } else {
            mw->setCentralWidget(w);
        }
    } else if (auto *dock = qobject_cast<QDockWidget *>(pw)) {
        dock->setWidget(w);
    } else if (auto *scroll = qobject_cast<QScrollArea *>(pw)) {
        scroll->setWidget(w);
    } else if (auto *splitter = qobject_cast<QSplitter *>(pw)) {
        splitter->insertWidget(index, w);
        if (child.place.contains(QStringLiteral("stretch"))) {
            splitter->setStretchFactor(index, child.place.value(QStringLiteral("stretch")).toInt());
        }
    } else if (auto *tabs = qobject_cast<QTabWidget *>(pw)) {
        const QJsonObject tab = child.place.value(QStringLiteral("tab")).toObject();
        tabs->insertTab(index, w, iconOf(tab.value(QStringLiteral("icon"))), tab.value(QStringLiteral("title")).toString());
        tabs->setTabToolTip(index, tab.value(QStringLiteral("toolTip")).toString());
    } else if (auto *stack = qobject_cast<QStackedWidget *>(pw)) {
        stack->insertWidget(index, w);
    } else if (auto *toolbar = qobject_cast<QToolBar *>(pw)) {
        toolbar->addWidget(w);
    } else if (auto *status = qobject_cast<QStatusBar *>(pw)) {
        if (child.place.value(QStringLiteral("permanent")).toBool()) {
            status->addPermanentWidget(w, child.place.value(QStringLiteral("stretch")).toInt());
        } else {
            status->addWidget(w, child.place.value(QStringLiteral("stretch")).toInt());
        }
    } else if (auto *box = qobject_cast<QBoxLayout *>(pw->layout())) {
        box->insertWidget(index, w, child.place.value(QStringLiteral("stretch")).toInt());
    } else if (auto *grid = qobject_cast<QGridLayout *>(pw->layout())) {
        grid->addWidget(w, child.place.value(QStringLiteral("row")).toInt(), child.place.value(QStringLiteral("col")).toInt(),
                        std::max(1, child.place.value(QStringLiteral("rowSpan")).toInt(1)),
                        std::max(1, child.place.value(QStringLiteral("colSpan")).toInt(1)));
    } else if (auto *form = qobject_cast<QFormLayout *>(pw->layout())) {
        const QString label = child.place.value(QStringLiteral("label")).toString();
        if (label.isEmpty()) {
            form->addRow(w);
        } else {
            form->addRow(label, w);
        }
    } else {
        w->setParent(pw); // a plain parent without a layout: free placement
    }
    if (child.props.value(QStringLiteral("visible")).toBool(true)) {
        w->show(); // reparenting hides a widget
    }
}

void detach(Node &parent, Node &child)
{
    QWidget *pw = parent.widget;
    QWidget *w = child.widget;
    if (!w || !pw) {
        return;
    }
    if (auto *mw = qobject_cast<QMainWindow *>(pw)) {
        if (auto *dock = qobject_cast<QDockWidget *>(w)) {
            mw->removeDockWidget(dock);
        } else if (auto *tb = qobject_cast<QToolBar *>(w)) {
            mw->removeToolBar(tb);
        } else if (qobject_cast<QStatusBar *>(w)) {
            return; // setStatusBar() replaces it in place; Qt deletes the one it drops
        } else if (mw->centralWidget() == w) {
            mw->takeCentralWidget();
        }
    } else if (auto *tabs = qobject_cast<QTabWidget *>(pw)) {
        tabs->removeTab(tabs->indexOf(w));
    } else if (auto *toolbar = qobject_cast<QToolBar *>(pw)) {
        for (QAction *a : toolbar->actions()) {
            if (toolbar->widgetForAction(a) == w) {
                toolbar->removeAction(a);
                delete a;
                break;
            }
        }
    } else if (auto *status = qobject_cast<QStatusBar *>(pw)) {
        status->removeWidget(w);
    } else if (pw->layout()) {
        if (auto *form = qobject_cast<QFormLayout *>(pw->layout())) {
            const auto info = [&] {
                int row;
                QFormLayout::ItemRole role;
                form->getWidgetPosition(w, &row, &role);
                return row;
            }();
            if (info >= 0) {
                QFormLayout::TakeRowResult r = form->takeRow(info);
                if (r.labelItem && r.labelItem->widget()) {
                    delete r.labelItem->widget();
                }
                delete r.labelItem;
                delete r.fieldItem;
            }
        } else {
            pw->layout()->removeWidget(w);
        }
    }
    w->hide();
    w->setParent(nullptr);
}

// ---- building

void connectEvents(Bridge &bridge, Node &n)
{
    QWidget *w = n.widget;
    const QString key = n.key;
    const int64_t id = n.id;
    auto emitEvent = [&bridge, key, id](const char *type, QJsonObject f) {
        f.insert(QStringLiteral("key"), key);
        bridge.emitEvent(QString::fromLatin1(type), f, id);
    };
    if (auto *b = qobject_cast<QPushButton *>(w)) {
        QObject::connect(b, &QPushButton::clicked, b, [emitEvent](bool checked) { emitEvent("widget.clicked", one("checked", checked)); });
    } else if (auto *c = qobject_cast<QCheckBox *>(w)) {
        QObject::connect(c, &QCheckBox::clicked, c, [emitEvent](bool checked) { emitEvent("widget.clicked", one("checked", checked)); });
    } else if (auto *e = qobject_cast<QLineEdit *>(w)) {
        QObject::connect(e, &QLineEdit::textChanged, e,
                         [emitEvent](const QString &text) { emitEvent("widget.textChanged", one("text", text)); });
        QObject::connect(e, &QLineEdit::returnPressed, e, [emitEvent] { emitEvent("widget.returnPressed", QJsonObject()); });
    } else if (auto *combo = qobject_cast<QComboBox *>(w)) {
        QObject::connect(combo, &QComboBox::currentIndexChanged, combo,
                         [emitEvent](int index) { emitEvent("widget.currentChanged", one("index", index)); });
    } else if (auto *tabs = qobject_cast<QTabWidget *>(w)) {
        QObject::connect(tabs, &QTabWidget::currentChanged, tabs,
                         [emitEvent](int index) { emitEvent("widget.currentChanged", one("index", index)); });
        QObject::connect(tabs, &QTabWidget::tabCloseRequested, tabs,
                         [emitEvent](int index) { emitEvent("widget.closeRequested", one("index", index)); });
    } else if (auto *stack = qobject_cast<QStackedWidget *>(w)) {
        QObject::connect(stack, &QStackedWidget::currentChanged, stack,
                         [emitEvent](int index) { emitEvent("widget.currentChanged", one("index", index)); });
    } else if (auto *dock = qobject_cast<QDockWidget *>(w)) {
        QObject::connect(dock, &QDockWidget::visibilityChanged, dock,
                         [emitEvent](bool visible) { emitEvent("widget.dockVisibilityChanged", one("visible", visible)); });
    } else if (auto *splitter = qobject_cast<QSplitter *>(w)) {
        QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [emitEvent, splitter] {
            QJsonArray sizes;
            for (int s : splitter->sizes()) {
                sizes.append(s);
            }
            emitEvent("widget.splitterMoved", one("sizes", sizes));
        });
    } else if (qobject_cast<QMainWindow *>(w)) {
        w->setProperty("tkdeInterceptClose", true); // the default; the prop interceptClose changes it
        new CloseGuard(bridge, w, key, id);
    }
}

std::unique_ptr<Node> build(Bridge &bridge, const QJsonObject &json);

void fillChildren(Bridge &bridge, Node &n, const QJsonArray &children)
{
    for (const QJsonValue &c : children) {
        n.children.push_back(build(bridge, c.toObject()));
        place(n, *n.children.back(), int(n.children.size()) - 1);
    }
}

std::unique_ptr<Node> build(Bridge &bridge, const QJsonObject &json)
{
    auto n = std::make_unique<Node>();
    n->key = json.value(QStringLiteral("key")).toString();
    n->type = json.value(QStringLiteral("type")).toString();
    n->place = json.value(QStringLiteral("place")).toObject();
    n->layout = json.value(QStringLiteral("layout")).toObject();
    if (n->type == QLatin1String("Embed")) {
        n->external = true;
        n->object = int64_t(json.value(QStringLiteral("object")).toDouble());
        n->widget = qobject_cast<QWidget *>(bridge.object(n->object));
        n->id = n->object;
        return n;
    }
    const TypeDef &t = types().at(n->type);
    n->widget = t.make();
    n->widget->setObjectName(n->key); // QMainWindow::saveState identifies docks and toolbars by it
    n->id = bridge.registerObject(n->widget);
    const QString kind = layoutKind(n->layout);
    if (kind == QLatin1String("box")) {
        n->widget->setLayout(new QBoxLayout(QBoxLayout::TopToBottom));
    } else if (kind == QLatin1String("grid")) {
        n->widget->setLayout(new QGridLayout);
    } else if (kind == QLatin1String("form")) {
        n->widget->setLayout(new QFormLayout);
    }
    applyLayoutSpec(*n, n->layout);
    applyProps(*n, json.value(QStringLiteral("props")).toObject());
    connectEvents(bridge, *n);
    fillChildren(bridge, *n, json.value(QStringLiteral("children")).toArray());
    applyLate(*n);
    return n;
}

// ---- updating (the diff)

bool sameShape(const Node &old, const QJsonObject &json)
{
    return old.type == json.value(QStringLiteral("type")).toString() &&
           layoutKind(old.layout) == layoutKind(json.value(QStringLiteral("layout")).toObject()) &&
           (!old.external || old.object == int64_t(json.value(QStringLiteral("object")).toDouble()));
}

void destroy(Node &n)
{
    if (n.external) {
        if (n.widget) {
            n.widget->hide();
            n.widget->setParent(nullptr);
        }
    } else {
        delete n.widget.data();
    }
}

void update(Bridge &bridge, Node &n, const QJsonObject &json);

/// Layout-like parents keep their children in order; main windows, docks and scroll areas
/// give each child a role instead.
bool orderSensitive(const Node &parent)
{
    QWidget *w = parent.widget;
    return !(qobject_cast<QMainWindow *>(w) || qobject_cast<QDockWidget *>(w) || qobject_cast<QScrollArea *>(w));
}

void reconcile(Bridge &bridge, Node &parent, const QJsonArray &jsonChildren)
{
    std::map<QString, size_t> oldIndex;
    for (size_t i = 0; i < parent.children.size(); ++i) {
        oldIndex[parent.children[i]->key] = i;
    }
    std::vector<std::unique_ptr<Node>> next; // nullptr = to be built
    std::vector<bool> moved;                 // must be (re)placed into the parent
    std::vector<bool> kept(parent.children.size(), false);
    bool anyChange = false;
    for (const QJsonValue &cv : jsonChildren) {
        const QJsonObject cj = cv.toObject();
        const auto it = oldIndex.find(cj.value(QStringLiteral("key")).toString());
        if (it != oldIndex.end() && sameShape(*parent.children[it->second], cj)) {
            Node &old = *parent.children[it->second];
            const bool placeChanged = old.place != cj.value(QStringLiteral("place")).toObject();
            const bool reordered = it->second != next.size();
            update(bridge, old, cj);
            kept[it->second] = true;
            moved.push_back(placeChanged || reordered);
            anyChange = anyChange || placeChanged || reordered;
            next.push_back(std::move(parent.children[it->second]));
        } else {
            moved.push_back(true);
            anyChange = true;
            next.push_back(nullptr);
        }
    }
    for (size_t i = 0; i < parent.children.size(); ++i) {
        if (!kept[i]) {
            anyChange = true;
            detach(parent, *parent.children[i]);
            destroy(*parent.children[i]);
        }
    }
    parent.children.clear();
    if (anyChange && orderSensitive(parent)) {
        // positions shift: take every remaining child out and put all back in the new order
        for (size_t i = 0; i < next.size(); ++i) {
            if (next[i]) {
                detach(parent, *next[i]);
            }
            moved[i] = true;
        }
    } else {
        for (size_t i = 0; i < next.size(); ++i) {
            if (next[i] && moved[i]) {
                detach(parent, *next[i]);
            }
        }
    }
    for (size_t i = 0; i < next.size(); ++i) {
        const QJsonObject cj = jsonChildren.at(int(i)).toObject();
        if (!next[i]) {
            next[i] = build(bridge, cj);
        }
        next[i]->place = cj.value(QStringLiteral("place")).toObject();
    }
    parent.children = std::move(next);
    for (size_t i = 0; i < parent.children.size(); ++i) {
        if (moved[i]) {
            place(parent, *parent.children[i], int(i));
        }
    }
}

void update(Bridge &bridge, Node &n, const QJsonObject &json)
{
    if (n.external) {
        n.place = json.value(QStringLiteral("place")).toObject();
        return;
    }
    n.place = json.value(QStringLiteral("place")).toObject();
    const QJsonObject layout = json.value(QStringLiteral("layout")).toObject();
    if (layout != n.layout) {
        n.layout = layout;
        applyLayoutSpec(n, layout);
    }
    applyProps(n, json.value(QStringLiteral("props")).toObject());
    reconcile(bridge, n, json.value(QStringLiteral("children")).toArray());
    applyLate(n);
}

// -------------------------------------------------------------------- state, dump

void collect(Node &n, QJsonObject &out)
{
    out.insert(n.key, double(n.id));
    for (auto &c : n.children) {
        collect(*c, out);
    }
}

void walk(Node &n, const std::function<void(Node &)> &fn)
{
    fn(n);
    for (auto &c : n.children) {
        walk(*c, fn);
    }
}

QString dockAreaName(Qt::DockWidgetArea a)
{
    for (const auto &e : dockAreas()) {
        if (e.second == a) {
            return e.first;
        }
    }
    return QStringLiteral("none");
}

/// What a test can compare: the real Qt state behind a node, without geometry.
QJsonObject dump(Node &n)
{
    QJsonObject o;
    o.insert(QStringLiteral("key"), n.key);
    o.insert(QStringLiteral("type"), n.type);
    QWidget *w = n.widget;
    if (!w) {
        o.insert(QStringLiteral("gone"), true);
        return o;
    }
    o.insert(QStringLiteral("class"), QString::fromLatin1(w->metaObject()->className()));
    o.insert(QStringLiteral("objectName"), w->objectName());
    o.insert(QStringLiteral("enabled"), w->isEnabled());
    o.insert(QStringLiteral("hidden"), w->isHidden());
    if (w->layout()) {
        QJsonObject l;
        l.insert(QStringLiteral("class"), QString::fromLatin1(w->layout()->metaObject()->className()));
        l.insert(QStringLiteral("count"), w->layout()->count());
        o.insert(QStringLiteral("layout"), l);
    }
    if (auto *x = qobject_cast<QLabel *>(w)) {
        o.insert(QStringLiteral("text"), x->text());
    } else if (auto *x = qobject_cast<QLineEdit *>(w)) {
        o.insert(QStringLiteral("text"), x->text());
        o.insert(QStringLiteral("placeholder"), x->placeholderText());
    } else if (auto *x = qobject_cast<QAbstractButton *>(w)) {
        o.insert(QStringLiteral("text"), x->text());
        o.insert(QStringLiteral("checked"), x->isChecked());
    } else if (auto *x = qobject_cast<QComboBox *>(w)) {
        QJsonArray items;
        for (int i = 0; i < x->count(); ++i) {
            items.append(x->itemText(i));
        }
        o.insert(QStringLiteral("items"), items);
        o.insert(QStringLiteral("currentIndex"), x->currentIndex());
    } else if (auto *x = qobject_cast<QPlainTextEdit *>(w)) {
        o.insert(QStringLiteral("text"), x->toPlainText());
    } else if (auto *x = qobject_cast<QTabWidget *>(w)) {
        QJsonArray titles;
        for (int i = 0; i < x->count(); ++i) {
            titles.append(x->tabText(i));
        }
        o.insert(QStringLiteral("tabs"), titles);
        o.insert(QStringLiteral("currentIndex"), x->currentIndex());
    } else if (auto *x = qobject_cast<QStackedWidget *>(w)) {
        o.insert(QStringLiteral("count"), x->count());
        o.insert(QStringLiteral("currentIndex"), x->currentIndex());
    } else if (auto *x = qobject_cast<QSplitter *>(w)) {
        o.insert(QStringLiteral("orientation"),
                 x->orientation() == Qt::Horizontal ? QStringLiteral("horizontal") : QStringLiteral("vertical"));
        QJsonArray order;
        for (int i = 0; i < x->count(); ++i) {
            order.append(x->widget(i)->objectName());
        }
        o.insert(QStringLiteral("order"), order);
    } else if (auto *x = qobject_cast<QDockWidget *>(w)) {
        o.insert(QStringLiteral("title"), x->windowTitle());
        if (auto *mw = qobject_cast<QMainWindow *>(x->parentWidget())) {
            o.insert(QStringLiteral("area"), dockAreaName(mw->dockWidgetArea(x)));
        }
    } else if (auto *x = qobject_cast<QMainWindow *>(w)) {
        o.insert(QStringLiteral("title"), x->windowTitle());
        o.insert(QStringLiteral("central"), x->centralWidget() ? x->centralWidget()->objectName() : QString());
        o.insert(QStringLiteral("statusBar"), x->findChild<QStatusBar *>(QString(), Qt::FindDirectChildrenOnly) != nullptr);
    } else if (auto *x = qobject_cast<QStatusBar *>(w)) {
        o.insert(QStringLiteral("message"), x->currentMessage());
    } else if (auto *x = qobject_cast<QToolBar *>(w)) {
        o.insert(QStringLiteral("title"), x->windowTitle());
    }
    QJsonArray children;
    auto *grid = qobject_cast<QGridLayout *>(w->layout());
    for (auto &c : n.children) {
        QJsonObject child = dump(*c);
        if (grid && c->widget) {
            const int index = grid->indexOf(c->widget.data());
            int row = 0, col = 0, rowSpan = 0, colSpan = 0;
            if (index >= 0) {
                grid->getItemPosition(index, &row, &col, &rowSpan, &colSpan);
                child.insert(QStringLiteral("cell"), QJsonArray{row, col, rowSpan, colSpan});
            }
        }
        children.append(child);
    }
    o.insert(QStringLiteral("children"), children);
    return o;
}

// -------------------------------------------------------------------- commands

Mount *mountOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    auto *m = qobject_cast<Mount *>(o);
    if (!m) {
        fail = Reply::failure(t.isDouble() ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG,
                              QStringLiteral("this command needs the \"target\" id of a mounted tree"));
    }
    return m;
}

QWidget *widgetTarget(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    if (auto *m = qobject_cast<Mount *>(o)) {
        o = m->rootWidget.data();
    }
    auto *w = qobject_cast<QWidget *>(o);
    if (!w) {
        fail = Reply::failure(t.isDouble() ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG,
                              QStringLiteral("this command needs the \"target\" id of a widget or a mounted tree"));
    }
    return w;
}

Reply treeReply(Mount &m)
{
    QJsonObject nodes;
    collect(*m.root, nodes);
    QJsonObject f;
    f.insert(QStringLiteral("root"), double(m.root->id));
    f.insert(QStringLiteral("nodes"), nodes);
    return Reply::success(f);
}

bool treeOf(Bridge &b, const QJsonObject &cmd, QJsonObject &tree, Reply &fail)
{
    tree = cmd.value(QStringLiteral("tree")).toObject();
    QString error;
    std::set<QString> keys;
    if (tree.isEmpty()) {
        fail = Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("this command needs a \"tree\" object"));
        return false;
    }
    if (tree.value(QStringLiteral("type")).toString() == QLatin1String("Embed")) {
        fail = Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("the root of a tree cannot be an Embed node"));
        return false;
    }
    if (!validate(b, tree, keys, error, true)) {
        fail = Reply::failure(TKDE_E_INVALID_ARG, error);
        return false;
    }
    return true;
}

Reply mount(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QJsonObject tree;
    if (!treeOf(b, cmd, tree, fail)) {
        return fail;
    }
    QWidget *parent = nullptr;
    if (cmd.contains(QStringLiteral("parent"))) {
        parent = qobject_cast<QWidget *>(b.object(int64_t(cmd.value(QStringLiteral("parent")).toDouble())));
        if (!parent) {
            return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("\"parent\" is not a widget"));
        }
    }
    auto *m = new Mount(b);
    m->root = build(b, tree);
    m->rootWidget = m->root->widget;
    m->setProperty("tkdeRoot", QVariant::fromValue(m->rootWidget.data())); // for other namespaces (action)
    if (parent) {
        m->rootWidget->setParent(parent);
    }
    const int64_t id = b.registerObject(m);
    Reply r = treeReply(*m);
    r.fields.insert(QStringLiteral("id"), double(id));
    return r;
}

Reply updateTree(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Mount *m = mountOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    QJsonObject tree;
    if (!treeOf(b, cmd, tree, fail)) {
        return fail;
    }
    if (!sameShape(*m->root, tree) || m->root->key != tree.value(QStringLiteral("key")).toString()) {
        return Reply::failure(TKDE_E_INVALID_ARG,
                              QStringLiteral("the root node keeps its key, type and layout kind: mount a new tree instead"));
    }
    update(b, *m->root, tree);
    return treeReply(*m);
}

Reply showWidget(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetTarget(b, cmd, fail);
    if (!w) {
        return fail;
    }
    w->show();
    if (w->isWindow()) {
        w->raise();
        w->activateWindow();
    }
    return Reply::success();
}

Reply hideWidget(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetTarget(b, cmd, fail);
    if (!w) {
        return fail;
    }
    w->hide();
    return Reply::success();
}

Reply closeWidget(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetTarget(b, cmd, fail);
    if (!w) {
        return fail;
    }
    // without "force" a window runs the same path as its close button: the logic is asked first
    auto *guard = w->findChild<CloseGuard *>(QString(), Qt::FindDirectChildrenOnly);
    if (guard) {
        guard->allow = cmd.value(QStringLiteral("force")).toBool();
    }
    w->close();
    if (guard) {
        guard->allow = false;
    }
    return Reply::success();
}

Reply focusWidget(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetTarget(b, cmd, fail);
    if (!w) {
        return fail;
    }
    w->setFocus();
    return Reply::success();
}

Reply appendText(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetTarget(b, cmd, fail);
    if (!w) {
        return fail;
    }
    auto *edit = qobject_cast<QPlainTextEdit *>(w);
    if (!edit) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("widget.append needs a QPlainTextEdit"));
    }
    edit->appendPlainText(cmd.value(QStringLiteral("text")).toString());
    return Reply::success();
}

KConfigGroup stateGroup(const QJsonObject &cmd)
{
    const QString file = cmd.value(QStringLiteral("file")).toString();
    KSharedConfigPtr config = file.isEmpty() ? KSharedConfig::openConfig() : KSharedConfig::openConfig(file, KConfig::SimpleConfig);
    return KConfigGroup(config, cmd.value(QStringLiteral("group")).toString(QStringLiteral("Window")));
}

Reply saveState(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Mount *m = mountOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    KConfigGroup g = stateGroup(cmd);
    QWidget *w = m->rootWidget;
    g.writeEntry("geometry", w->saveGeometry());
    if (auto *mw = qobject_cast<QMainWindow *>(w)) {
        g.writeEntry("windowState", mw->saveState());
    }
    walk(*m->root, [&g](Node &n) {
        if (auto *s = qobject_cast<QSplitter *>(n.widget.data())) {
            g.writeEntry(QStringLiteral("splitter.") + n.key, s->sizes());
        }
    });
    g.sync();
    return Reply::success();
}

Reply restoreState(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Mount *m = mountOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    KConfigGroup g = stateGroup(cmd);
    QWidget *w = m->rootWidget;
    bool any = false;
    if (g.hasKey("geometry")) {
        any = w->restoreGeometry(g.readEntry("geometry", QByteArray())) || any;
    }
    if (auto *mw = qobject_cast<QMainWindow *>(w)) {
        if (g.hasKey("windowState")) {
            any = mw->restoreState(g.readEntry("windowState", QByteArray())) || any;
        }
    }
    walk(*m->root, [&g, &any](Node &n) {
        if (auto *s = qobject_cast<QSplitter *>(n.widget.data())) {
            const QList<int> sizes = g.readEntry(QStringLiteral("splitter.") + n.key, QList<int>());
            if (!sizes.isEmpty()) {
                s->setSizes(sizes);
                any = true;
            }
        }
    });
    QJsonObject f;
    f.insert(QStringLiteral("restored"), any);
    return Reply::success(f);
}

Reply dumpTree(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Mount *m = mountOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    QJsonObject f;
    f.insert(QStringLiteral("tree"), dump(*m->root));
    return Reply::success(f);
}

/// Test hook: does what a user would do to the widget, so the events fire for real.
Reply debugAct(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetTarget(b, cmd, fail);
    if (!w) {
        return fail;
    }
    const QString act = cmd.value(QStringLiteral("act")).toString();
    const QJsonValue v = cmd.value(QStringLiteral("value"));
    if (act == QLatin1String("click")) {
        if (auto *btn = qobject_cast<QAbstractButton *>(w)) {
            btn->click();
            return Reply::success();
        }
    } else if (act == QLatin1String("text")) {
        if (auto *e = qobject_cast<QLineEdit *>(w)) {
            e->setText(v.toString());
            return Reply::success();
        }
    } else if (act == QLatin1String("index")) {
        if (auto *c = qobject_cast<QComboBox *>(w)) {
            c->setCurrentIndex(v.toInt());
            return Reply::success();
        }
        if (auto *t = qobject_cast<QTabWidget *>(w)) {
            t->setCurrentIndex(v.toInt());
            return Reply::success();
        }
        if (auto *st = qobject_cast<QStackedWidget *>(w)) {
            st->setCurrentIndex(v.toInt());
            return Reply::success();
        }
    } else if (act == QLatin1String("closeTab")) {
        if (auto *t = qobject_cast<QTabWidget *>(w)) {
            Q_EMIT t->tabCloseRequested(v.toInt());
            return Reply::success();
        }
    } else if (act == QLatin1String("visible")) {
        if (auto *d = qobject_cast<QDockWidget *>(w)) {
            d->setVisible(v.toBool());
            return Reply::success();
        }
    } else if (act == QLatin1String("split")) {
        if (auto *sp = qobject_cast<QSplitter *>(w)) {
            QList<int> sizes;
            for (const QJsonValue &x : v.toArray()) {
                sizes.append(x.toInt());
            }
            sp->setSizes(sizes);
            Q_EMIT sp->splitterMoved(sizes.value(0), 1); // setSizes does not emit it; a drag does
            return Reply::success();
        }
    }
    return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("cannot '%1' this widget").arg(act));
}
} // namespace

void registerCommands(Bridge &bridge)
{
    bridge.addNamespace(QStringLiteral("widget"));
    auto reg = [&bridge](const char *name, Reply (*fn)(Bridge &, const QJsonObject &)) {
        bridge.registerCommand(QString::fromLatin1(name), [&bridge, fn](const QJsonObject &cmd) { return fn(bridge, cmd); });
    };
    reg("widget.mount", mount);
    reg("widget.update", updateTree);
    reg("widget.show", showWidget);
    reg("widget.hide", hideWidget);
    reg("widget.close", closeWidget);
    reg("widget.focus", focusWidget);
    reg("widget.append", appendText);
    reg("widget.saveState", saveState);
    reg("widget.restoreState", restoreState);
    if (bridge.debugCommandsEnabled()) {
        reg("widget.debug.dump", dumpTree);
        reg("widget.debug.act", debugAct);
    }
}
} // namespace tkde::widget

#include "widget.moc"
