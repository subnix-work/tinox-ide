#include "model.h"

#include "../bridge.h"
#include "../i18n.h"
#include "tinoxkde.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QListView>
#include <QMimeData>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTreeView>
#include <QUrl>

#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace tkde::model
{
namespace
{
QString qs(const char *s)
{
    return QString::fromLatin1(s);
}

const QString kMime = QStringLiteral("application/x-tinox-rows");

struct Cell {
    QString text;
    QString icon;
    QString toolTip;
};

enum Flag { Selectable = 1, Enabled = 2, Editable = 4, Drag = 8, Drop = 16 };

struct Item {
    QString uid;
    Item *parent = nullptr;
    std::vector<std::unique_ptr<Item>> kids;
    QString text;
    QString icon;
    QString toolTip;
    QString color;
    bool bold = false;
    int check = -1; // -1: no checkbox, else Qt::CheckState
    int flags = Selectable | Enabled;
    bool hasChildren = false;
    bool fetched = true;
    bool pending = false;
    QList<Cell> cells;

    int rowInParent() const
    {
        for (size_t i = 0; i < parent->kids.size(); ++i) {
            if (parent->kids[i].get() == this) {
                return int(i);
            }
        }
        return -1;
    }
};

QIcon iconNamed(const QString &name)
{
    static QHash<QString, QIcon> cache;
    if (name.isEmpty()) {
        return QIcon();
    }
    auto it = cache.find(name);
    if (it == cache.end()) {
        it = cache.insert(name, QIcon::fromTheme(name));
    }
    return it.value();
}

// ---- row descriptions

using Exists = std::function<bool(const QString &)>;

bool checkRow(const QJsonObject &o, std::set<QString> &ids, const Exists &exists, QString &error, int depth = 0)
{
    static const std::set<QString> known{qs("id"),   qs("text"),  qs("icon"),        qs("toolTip"), qs("checked"), qs("color"),
                                         qs("bold"), qs("flags"), qs("hasChildren"), qs("cells"),   qs("children")};
    const QString id = o.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        error = QStringLiteral("every row needs a non-empty string \"id\"");
        return false;
    }
    if (!ids.insert(id).second || exists(id)) {
        error = QStringLiteral("row id '%1' is not unique").arg(id);
        return false;
    }
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (known.count(it.key()) == 0) {
            error = QStringLiteral("row '%1': unknown field '%2'").arg(id, it.key());
            return false;
        }
    }
    for (const char *f : {"text", "icon", "toolTip", "color"}) {
        if (o.contains(qs(f)) && !o.value(qs(f)).isString()) {
            error = QStringLiteral("row '%1': \"%2\" must be a string").arg(id, qs(f));
            return false;
        }
    }
    if (o.contains(QStringLiteral("checked")) && !o.value(QStringLiteral("checked")).isBool() &&
        !o.value(QStringLiteral("checked")).isNull()) {
        error = QStringLiteral("row '%1': \"checked\" must be true, false or null (no checkbox)").arg(id);
        return false;
    }
    // an empty colour clears it (back to the view's own text colour)
    if (o.contains(QStringLiteral("color")) && !o.value(QStringLiteral("color")).toString().isEmpty() &&
        !QColor::isValidColorName(o.value(QStringLiteral("color")).toString())) {
        error = QStringLiteral("row '%1': '%2' is not a colour").arg(id, o.value(QStringLiteral("color")).toString());
        return false;
    }
    if (o.contains(QStringLiteral("flags"))) {
        const QJsonObject fl = o.value(QStringLiteral("flags")).toObject();
        for (auto it = fl.begin(); it != fl.end(); ++it) {
            static const std::set<QString> names{qs("selectable"), qs("enabled"), qs("editable"), qs("drag"), qs("drop")};
            if (names.count(it.key()) == 0 || !it.value().isBool()) {
                error = QStringLiteral("row '%1': flags are selectable, enabled, editable, drag, drop (booleans)").arg(id);
                return false;
            }
        }
    }
    if (o.contains(QStringLiteral("cells")) && !o.value(QStringLiteral("cells")).isArray()) {
        error = QStringLiteral("row '%1': \"cells\" must be an array").arg(id);
        return false;
    }
    if (o.contains(QStringLiteral("children"))) {
        if (!o.value(QStringLiteral("children")).isArray()) {
            error = QStringLiteral("row '%1': \"children\" must be an array").arg(id);
            return false;
        }
        if (depth > 64) {
            error = QStringLiteral("rows nested deeper than 64 levels");
            return false;
        }
        for (const QJsonValue &c : o.value(QStringLiteral("children")).toArray()) {
            if (!checkRow(c.toObject(), ids, exists, error, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

void applyFields(Item &item, const QJsonObject &o)
{
    if (o.contains(QStringLiteral("text"))) {
        item.text = o.value(QStringLiteral("text")).toString();
    }
    if (o.contains(QStringLiteral("icon"))) {
        item.icon = o.value(QStringLiteral("icon")).toString();
    }
    if (o.contains(QStringLiteral("toolTip"))) {
        item.toolTip = o.value(QStringLiteral("toolTip")).toString();
    }
    if (o.contains(QStringLiteral("color"))) {
        item.color = o.value(QStringLiteral("color")).toString();
    }
    if (o.contains(QStringLiteral("bold"))) {
        item.bold = o.value(QStringLiteral("bold")).toBool();
    }
    if (o.contains(QStringLiteral("checked"))) {
        const QJsonValue c = o.value(QStringLiteral("checked"));
        item.check = c.isNull() ? -1 : (c.toBool() ? Qt::Checked : Qt::Unchecked);
    }
    if (o.contains(QStringLiteral("flags"))) {
        const QJsonObject fl = o.value(QStringLiteral("flags")).toObject();
        const std::pair<const char *, int> bits[] = {
            {"selectable", Selectable}, {"enabled", Enabled}, {"editable", Editable}, {"drag", Drag}, {"drop", Drop}};
        for (const auto &b : bits) {
            if (fl.contains(qs(b.first))) {
                item.flags = fl.value(qs(b.first)).toBool() ? (item.flags | b.second) : (item.flags & ~b.second);
            }
        }
    }
    if (o.contains(QStringLiteral("hasChildren"))) {
        item.hasChildren = o.value(QStringLiteral("hasChildren")).toBool();
    }
    if (o.contains(QStringLiteral("cells"))) {
        item.cells.clear();
        for (const QJsonValue &c : o.value(QStringLiteral("cells")).toArray()) {
            Cell cell;
            if (c.isString()) {
                cell.text = c.toString();
            } else {
                cell.text = c.toObject().value(QStringLiteral("text")).toString();
                cell.icon = c.toObject().value(QStringLiteral("icon")).toString();
                cell.toolTip = c.toObject().value(QStringLiteral("toolTip")).toString();
            }
            item.cells.append(cell);
        }
    }
}

class TinoxModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    TinoxModel(Bridge &bridge, QStringList columns, bool lazyRoot) : m_bridge(bridge), m_columns(std::move(columns))
    {
        m_root.fetched = !lazyRoot;
    }

    void bindId(int64_t id) { m_id = id; }
    int64_t id() const { return m_id; }

    Item *find(const QString &uid) const
    {
        if (uid.isEmpty()) {
            return const_cast<Item *>(&m_root);
        }
        const auto it = m_byUid.find(uid);
        return it == m_byUid.end() ? nullptr : it->second;
    }
    Item *root() { return &m_root; }

    QModelIndex indexOf(const Item *item, int column = 0) const
    {
        return item == &m_root ? QModelIndex() : createIndex(item->rowInParent(), column, const_cast<Item *>(item));
    }

    int totalRows() const { return int(m_byUid.size()); }
    bool dropEnabled = false;

    // ---- changes

    /// Builds the items of `rows` below `parent` without announcing anything.
    std::vector<std::unique_ptr<Item>> build(Item *parent, const QJsonArray &rows)
    {
        std::vector<std::unique_ptr<Item>> out;
        for (const QJsonValue &v : rows) {
            const QJsonObject o = v.toObject();
            auto item = std::make_unique<Item>();
            item->uid = o.value(QStringLiteral("id")).toString();
            item->parent = parent;
            applyFields(*item, o);
            if (o.contains(QStringLiteral("children"))) {
                item->kids = build(item.get(), o.value(QStringLiteral("children")).toArray());
                item->hasChildren = !item->kids.empty() || item->hasChildren;
                item->fetched = true;
            } else {
                item->fetched = !item->hasChildren; // "hasChildren" without children: loaded on demand
            }
            out.push_back(std::move(item));
        }
        return out;
    }

    void registerTree(Item *item)
    {
        m_byUid[item->uid] = item;
        for (auto &k : item->kids) {
            registerTree(k.get());
        }
    }

    void unregisterTree(Item *item)
    {
        m_byUid.erase(item->uid);
        for (auto &k : item->kids) {
            unregisterTree(k.get());
        }
    }

    void reset(const QJsonArray &rows)
    {
        beginResetModel();
        m_byUid.clear();
        m_root.kids = build(&m_root, rows);
        for (auto &k : m_root.kids) {
            registerTree(k.get());
        }
        m_root.fetched = true;
        m_root.pending = false;
        endResetModel();
    }

    void insert(Item *parent, int index, const QJsonArray &rows)
    {
        if (rows.isEmpty()) {
            return;
        }
        index = std::clamp(index, 0, int(parent->kids.size()));
        auto items = build(parent, rows);
        beginInsertRows(indexOf(parent), index, index + int(items.size()) - 1);
        int at = index;
        for (auto &it : items) {
            registerTree(it.get());
            parent->kids.insert(parent->kids.begin() + at++, std::move(it));
        }
        endInsertRows();
    }

    void removeRows(Item *parent, int first, int count)
    {
        beginRemoveRows(indexOf(parent), first, first + count - 1);
        for (int i = 0; i < count; ++i) {
            unregisterTree(parent->kids[size_t(first + i)].get());
        }
        parent->kids.erase(parent->kids.begin() + first, parent->kids.begin() + first + count);
        endRemoveRows();
    }

    void remove(Item *item) { removeRows(item->parent, item->rowInParent(), 1); }

    /// Replaces the children of `parent`; used to answer a fetch request.
    void setChildren(Item *parent, const QJsonArray &rows)
    {
        if (!parent->kids.empty()) {
            removeRows(parent, 0, int(parent->kids.size()));
        }
        insert(parent, 0, rows);
        parent->fetched = true;
        parent->pending = false;
        parent->hasChildren = !parent->kids.empty();
        if (parent != &m_root) {
            const QModelIndex i = indexOf(parent);
            Q_EMIT dataChanged(i, i.siblingAtColumn(columnCount() - 1));
        }
    }

    bool move(Item *item, Item *newParent, int index)
    {
        for (Item *p = newParent; p; p = p->parent) {
            if (p == item) {
                return false; // into itself
            }
        }
        const int from = item->rowInParent();
        index = std::clamp(index, 0, int(newParent->kids.size()));
        const int target = (newParent == item->parent && index > from) ? index + 1 : index;
        if (!beginMoveRows(indexOf(item->parent), from, from, indexOf(newParent), target)) {
            return true; // already in place
        }
        auto owned = std::move(item->parent->kids[size_t(from)]);
        item->parent->kids.erase(item->parent->kids.begin() + from);
        int insertAt = (newParent == item->parent && index > from) ? index : index;
        insertAt = std::clamp(insertAt, 0, int(newParent->kids.size()));
        item->parent = newParent;
        newParent->kids.insert(newParent->kids.begin() + insertAt, std::move(owned));
        endMoveRows();
        return true;
    }

    void update(Item *item, const QJsonObject &fields)
    {
        applyFields(*item, fields);
        const QModelIndex i = indexOf(item);
        Q_EMIT dataChanged(i, i.siblingAtColumn(columnCount() - 1));
    }

    // ---- QAbstractItemModel

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
    {
        const Item *p = parent.isValid() ? static_cast<Item *>(parent.internalPointer()) : &m_root;
        if (row < 0 || column < 0 || column >= columnCount() || size_t(row) >= p->kids.size()) {
            return QModelIndex();
        }
        return createIndex(row, column, p->kids[size_t(row)].get());
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        if (!child.isValid()) {
            return QModelIndex();
        }
        const Item *item = static_cast<Item *>(child.internalPointer());
        return item->parent == &m_root ? QModelIndex() : indexOf(item->parent);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.column() > 0) {
            return 0;
        }
        const Item *p = parent.isValid() ? static_cast<Item *>(parent.internalPointer()) : &m_root;
        return int(p->kids.size());
    }

    int columnCount(const QModelIndex & = QModelIndex()) const override { return std::max(1, int(m_columns.size())); }

    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override
    {
        if (parent.column() > 0) {
            return false;
        }
        const Item *p = parent.isValid() ? static_cast<Item *>(parent.internalPointer()) : &m_root;
        return !p->kids.empty() || (p != &m_root && p->hasChildren && !p->fetched);
    }

    bool canFetchMore(const QModelIndex &parent) const override
    {
        const Item *p = parent.isValid() ? static_cast<Item *>(parent.internalPointer()) : &m_root;
        return !p->fetched && !p->pending && (p == &m_root || p->hasChildren);
    }

    void fetchMore(const QModelIndex &parent) override
    {
        Item *p = parent.isValid() ? static_cast<Item *>(parent.internalPointer()) : &m_root;
        if (p->fetched || p->pending) {
            return;
        }
        p->pending = true;
        QJsonObject f;
        f.insert(QStringLiteral("id"), p == &m_root ? QJsonValue(QJsonValue::Null) : QJsonValue(p->uid));
        m_bridge.emitEvent(QStringLiteral("model.fetchChildren"), f, m_id);
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()) {
            return QVariant();
        }
        const Item *item = static_cast<Item *>(index.internalPointer());
        const int col = index.column();
        if (role == UidRole) {
            return item->uid;
        }
        if (col == 0) {
            switch (role) {
            case Qt::DisplayRole:
            case Qt::EditRole:
                return item->text;
            case Qt::DecorationRole:
                return item->icon.isEmpty() ? QVariant() : QVariant(iconNamed(item->icon));
            case Qt::ToolTipRole:
                return item->toolTip.isEmpty() ? QVariant() : QVariant(item->toolTip);
            case Qt::CheckStateRole:
                return item->check < 0 ? QVariant() : QVariant(item->check);
            case Qt::ForegroundRole:
                return item->color.isEmpty() ? QVariant() : QVariant(QBrush(QColor::fromString(item->color)));
            case Qt::FontRole:
                if (item->bold) {
                    QFont f;
                    f.setBold(true);
                    return f;
                }
                return QVariant();
            default:
                return QVariant();
            }
        }
        const int c = col - 1;
        if (c >= item->cells.size()) {
            return QVariant();
        }
        const Cell &cell = item->cells.at(c);
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return cell.text;
        case Qt::DecorationRole:
            return cell.icon.isEmpty() ? QVariant() : QVariant(iconNamed(cell.icon));
        case Qt::ToolTipRole:
            return cell.toolTip.isEmpty() ? QVariant() : QVariant(cell.toolTip);
        default:
            return QVariant();
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (!index.isValid()) {
            return false;
        }
        Item *item = static_cast<Item *>(index.internalPointer());
        if (role == Qt::CheckStateRole && item->check >= 0 && index.column() == 0) {
            item->check = value.toInt();
            Q_EMIT dataChanged(index, index, {Qt::CheckStateRole});
            QJsonObject f;
            f.insert(QStringLiteral("id"), item->uid);
            f.insert(QStringLiteral("checked"), item->check == Qt::Checked);
            m_bridge.emitEvent(QStringLiteral("model.checkChanged"), f, m_id);
            return true;
        }
        if (role == Qt::EditRole && (item->flags & Editable)) {
            // an edit is a request: the logic decides (a rename may fail on disk) and updates the row
            QJsonObject f;
            f.insert(QStringLiteral("id"), item->uid);
            f.insert(QStringLiteral("column"), index.column());
            f.insert(QStringLiteral("text"), value.toString());
            m_bridge.emitEvent(QStringLiteral("model.editRequested"), f, m_id);
            return false;
        }
        return false;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid()) {
            return dropEnabled ? Qt::ItemIsDropEnabled : Qt::NoItemFlags;
        }
        const Item *item = static_cast<Item *>(index.internalPointer());
        Qt::ItemFlags f;
        if (item->flags & Enabled) {
            f |= Qt::ItemIsEnabled;
        }
        if (item->flags & Selectable) {
            f |= Qt::ItemIsSelectable;
        }
        if ((item->flags & Editable)) {
            f |= Qt::ItemIsEditable;
        }
        if (item->check >= 0 && index.column() == 0) {
            f |= Qt::ItemIsUserCheckable;
        }
        if (item->flags & Drag) {
            f |= Qt::ItemIsDragEnabled;
        }
        if (item->flags & Drop) {
            f |= Qt::ItemIsDropEnabled;
        }
        return f;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < m_columns.size()) {
            return m_columns.at(section);
        }
        return QVariant();
    }

    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction | Qt::CopyAction; }
    Qt::DropActions supportedDragActions() const override { return Qt::MoveAction | Qt::CopyAction; }

    QStringList mimeTypes() const override { return {kMime, QStringLiteral("text/uri-list")}; }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        QJsonArray ids;
        std::set<QString> seen;
        for (const QModelIndex &i : indexes) {
            if (i.isValid() && seen.insert(data(i, UidRole).toString()).second) {
                ids.append(data(i, UidRole).toString());
            }
        }
        auto *mime = new QMimeData;
        mime->setData(kMime, QJsonDocument(ids).toJson(QJsonDocument::Compact));
        return mime;
    }

    bool dropMimeData(const QMimeData *mime, Qt::DropAction action, int row, int, const QModelIndex &parent) override
    {
        QJsonObject f;
        QJsonArray ids;
        QJsonArray urls;
        QJsonArray files;
        if (mime->hasFormat(kMime)) {
            ids = QJsonDocument::fromJson(mime->data(kMime)).array();
        }
        for (const QUrl &u : mime->urls()) {
            urls.append(u.toString());
            if (u.isLocalFile()) {
                files.append(u.toLocalFile()); // decoded, ready to use as a path
            }
        }
        if (ids.isEmpty() && urls.isEmpty()) {
            return false;
        }
        f.insert(QStringLiteral("ids"), ids);
        f.insert(QStringLiteral("urls"), urls);
        f.insert(QStringLiteral("files"), files);
        f.insert(QStringLiteral("parent"), parent.isValid() ? QJsonValue(data(parent, UidRole).toString()) : QJsonValue(QJsonValue::Null));
        f.insert(QStringLiteral("index"), row);
        f.insert(QStringLiteral("action"), action == Qt::CopyAction ? QStringLiteral("copy") : QStringLiteral("move"));
        m_bridge.emitEvent(QStringLiteral("model.dropRequested"), f, m_id);
        return false; // the logic moves the rows (it has to move the files first); false keeps the view from removing them
    }

    static constexpr int UidRole = Qt::UserRole;

private:
    Bridge &m_bridge;
    QStringList m_columns;
    Item m_root;
    std::unordered_map<QString, Item *> m_byUid;
    int64_t m_id = 0;
};

// -------------------------------------------------------------------- commands

Reply invalid(const QString &message)
{
    return Reply::failure(TKDE_E_INVALID_ARG, message);
}

TinoxModel *modelOf(Bridge &b, const QJsonObject &cmd, Reply &fail, const char *key = "target")
{
    const QJsonValue t = cmd.value(qs(key));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    auto *m = qobject_cast<TinoxModel *>(o);
    if (!m) {
        fail = Reply::failure(t.isDouble() ? (o ? TKDE_E_INVALID_ARG : TKDE_E_NOT_FOUND) : TKDE_E_INVALID_ARG,
                              QStringLiteral("\"%1\" must be the id of a model").arg(qs(key)));
    }
    return m;
}

Item *itemOf(TinoxModel *m, const QJsonValue &v, Reply &fail, const char *what)
{
    Item *item = v.isString() ? m->find(v.toString()) : (v.isNull() || v.isUndefined() ? m->root() : nullptr);
    if (!item) {
        fail = Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("%1: no row '%2'").arg(qs(what), v.toString()));
    }
    return item;
}

bool checkRows(TinoxModel *m, const QJsonValue &rows, QString &error)
{
    if (!rows.isArray()) {
        error = QStringLiteral("\"rows\" must be an array of row descriptions");
        return false;
    }
    std::set<QString> ids;
    const Exists exists = [m](const QString &id) { return m->find(id) != nullptr; };
    for (const QJsonValue &r : rows.toArray()) {
        if (!checkRow(r.toObject(), ids, exists, error)) {
            return false;
        }
    }
    return true;
}

Reply create(Bridge &b, const QJsonObject &cmd)
{
    QStringList columns;
    for (const QJsonValue &c : cmd.value(QStringLiteral("columns")).toArray()) {
        if (!c.isString()) {
            return invalid(QStringLiteral("\"columns\" are header strings"));
        }
        columns << tr(c.toString());
    }
    auto *m = new TinoxModel(b, columns, cmd.value(QStringLiteral("lazy")).toBool());
    m->dropEnabled = cmd.value(QStringLiteral("acceptDrops")).toBool();
    const int64_t id = b.registerObject(m);
    m->bindId(id);
    QJsonObject f;
    f.insert(QStringLiteral("id"), double(id));
    return Reply::success(f);
}

Reply reset(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    QString error;
    // ids may be reused by a reset: check against an empty model
    std::set<QString> ids;
    const Exists none = [](const QString &) { return false; };
    if (!cmd.value(QStringLiteral("rows")).isArray()) {
        return invalid(QStringLiteral("\"rows\" must be an array of row descriptions"));
    }
    for (const QJsonValue &r : cmd.value(QStringLiteral("rows")).toArray()) {
        if (!checkRow(r.toObject(), ids, none, error)) {
            return invalid(error);
        }
    }
    m->reset(cmd.value(QStringLiteral("rows")).toArray());
    QJsonObject f;
    f.insert(QStringLiteral("rows"), m->totalRows());
    return Reply::success(f);
}

Reply insert(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    Item *parent = itemOf(m, cmd.value(QStringLiteral("parent")), fail, "parent");
    if (!parent) {
        return fail;
    }
    QString error;
    if (!checkRows(m, cmd.value(QStringLiteral("rows")), error)) {
        return invalid(error);
    }
    m->insert(parent, cmd.value(QStringLiteral("index")).toInt(int(parent->kids.size())), cmd.value(QStringLiteral("rows")).toArray());
    return Reply::success();
}

Reply setChildren(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    Item *parent = itemOf(m, cmd.value(QStringLiteral("parent")), fail, "parent");
    if (!parent) {
        return fail;
    }
    // the rows being replaced may reuse their own ids
    QString error;
    if (!cmd.value(QStringLiteral("rows")).isArray()) {
        return invalid(QStringLiteral("\"rows\" must be an array of row descriptions"));
    }
    std::set<QString> replaced;
    std::function<void(Item *)> gather = [&](Item *i) {
        for (auto &k : i->kids) {
            replaced.insert(k->uid);
            gather(k.get());
        }
    };
    gather(parent);
    const Exists exists = [&](const QString &id) { return m->find(id) != nullptr && replaced.count(id) == 0; };
    std::set<QString> ids;
    for (const QJsonValue &r : cmd.value(QStringLiteral("rows")).toArray()) {
        if (!checkRow(r.toObject(), ids, exists, error)) {
            return invalid(error);
        }
    }
    m->setChildren(parent, cmd.value(QStringLiteral("rows")).toArray());
    return Reply::success();
}

Reply remove(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    std::vector<QString> ids;
    for (const QJsonValue &v : cmd.value(QStringLiteral("ids")).toArray()) {
        if (!v.isString() || !m->find(v.toString()) || v.toString().isEmpty()) {
            return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no row '%1'").arg(v.toString()));
        }
        ids.push_back(v.toString());
    }
    for (const QString &id : ids) {
        if (Item *item = m->find(id)) { // a row removed together with its parent is already gone
            m->remove(item);
        }
    }
    return Reply::success();
}

Reply move(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    Item *item = itemOf(m, cmd.value(QStringLiteral("id")), fail, "id");
    if (!item || item == m->root()) {
        return item ? invalid(QStringLiteral("\"id\" is required")) : fail;
    }
    Item *parent = itemOf(m, cmd.value(QStringLiteral("parent")), fail, "parent");
    if (!parent) {
        return fail;
    }
    if (!m->move(item, parent, cmd.value(QStringLiteral("index")).toInt(int(parent->kids.size())))) {
        return invalid(QStringLiteral("a row cannot move into itself"));
    }
    return Reply::success();
}

Reply update(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    Item *item = itemOf(m, cmd.value(QStringLiteral("id")), fail, "id");
    if (!item || item == m->root()) {
        return item ? invalid(QStringLiteral("\"id\" is required")) : fail;
    }
    QJsonObject fields = cmd.value(QStringLiteral("fields")).toObject();
    if (fields.contains(QStringLiteral("id")) || fields.contains(QStringLiteral("children"))) {
        return invalid(QStringLiteral("update changes a row's own fields: use move/insert/remove for structure"));
    }
    fields.insert(QStringLiteral("id"), item->uid);
    std::set<QString> ids;
    const Exists none = [](const QString &) { return false; };
    QString error;
    if (!checkRow(fields, ids, none, error)) {
        return invalid(error);
    }
    fields.remove(QStringLiteral("id"));
    m->update(item, fields);
    return Reply::success();
}

Reply get(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    Item *item = itemOf(m, cmd.value(QStringLiteral("id")), fail, "id");
    if (!item) {
        return fail;
    }
    QJsonObject f;
    f.insert(QStringLiteral("id"), item->uid);
    f.insert(QStringLiteral("parent"),
             item->parent && item->parent != m->root() ? QJsonValue(item->parent->uid) : QJsonValue(QJsonValue::Null));
    f.insert(QStringLiteral("index"), item == m->root() ? -1 : item->rowInParent());
    f.insert(QStringLiteral("text"), item->text);
    f.insert(QStringLiteral("count"), int(item->kids.size()));
    f.insert(QStringLiteral("fetched"), item->fetched);
    f.insert(QStringLiteral("hasChildren"), item->hasChildren || !item->kids.empty());
    QJsonArray kids;
    for (auto &k : item->kids) {
        kids.append(k->uid);
    }
    f.insert(QStringLiteral("children"), kids);
    return Reply::success(f);
}

Reply info(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    QJsonObject f;
    f.insert(QStringLiteral("rows"), m->totalRows());
    f.insert(QStringLiteral("topLevel"), int(m->root()->kids.size()));
    return Reply::success(f);
}

// ---- proxy: sorting and filtering

QSortFilterProxyModel *proxyOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    auto *p = qobject_cast<QSortFilterProxyModel *>(o);
    if (!p) {
        fail = Reply::failure(t.isDouble() && !o ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG,
                              QStringLiteral("\"target\" must be the id of a proxy model"));
    }
    return p;
}

bool configureProxy(QSortFilterProxyModel *p, const QJsonObject &cmd, QString &error)
{
    if (cmd.contains(QStringLiteral("filter"))) {
        const QJsonObject f = cmd.value(QStringLiteral("filter")).toObject();
        const Qt::CaseSensitivity cs = f.value(QStringLiteral("caseSensitive")).toBool() ? Qt::CaseSensitive : Qt::CaseInsensitive;
        p->setFilterCaseSensitivity(cs);
        p->setFilterKeyColumn(f.value(QStringLiteral("column")).toInt(0));
        p->setRecursiveFilteringEnabled(f.value(QStringLiteral("recursive")).toBool(true));
        if (f.value(QStringLiteral("regex")).toBool()) {
            QRegularExpression re(f.value(QStringLiteral("text")).toString(), cs == Qt::CaseSensitive
                                                                                  ? QRegularExpression::NoPatternOption
                                                                                  : QRegularExpression::CaseInsensitiveOption);
            if (!re.isValid()) {
                error = QStringLiteral("bad regular expression: %1").arg(re.errorString());
                return false;
            }
            p->setFilterRegularExpression(re);
        } else {
            p->setFilterFixedString(f.value(QStringLiteral("text")).toString());
        }
    }
    if (cmd.contains(QStringLiteral("sort"))) {
        const QJsonObject s = cmd.value(QStringLiteral("sort")).toObject();
        p->setSortCaseSensitivity(s.value(QStringLiteral("caseSensitive")).toBool() ? Qt::CaseSensitive : Qt::CaseInsensitive);
        p->setSortLocaleAware(true);
        p->sort(s.value(QStringLiteral("column")).toInt(0),
                s.value(QStringLiteral("order")).toString() == QLatin1String("desc") ? Qt::DescendingOrder : Qt::AscendingOrder);
    }
    return true;
}

Reply createProxy(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *source = modelOf(b, cmd, fail, "source");
    if (!source) {
        return fail;
    }
    auto *p = new QSortFilterProxyModel;
    p->setSourceModel(source);
    p->setParent(source); // gone with its model
    p->setDynamicSortFilter(true);
    p->setSortRole(Qt::DisplayRole);
    QString error;
    if (!configureProxy(p, cmd, error)) {
        delete p;
        return invalid(error);
    }
    QJsonObject f;
    f.insert(QStringLiteral("id"), double(b.registerObject(p)));
    return Reply::success(f);
}

Reply setFilter(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QSortFilterProxyModel *p = proxyOf(b, cmd, fail);
    if (!p) {
        return fail;
    }
    QString error;
    if (!configureProxy(p, cmd, error)) {
        return invalid(error);
    }
    return Reply::success();
}

// ---- views

QAbstractItemView *viewOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("view"));
    auto *v = qobject_cast<QAbstractItemView *>(t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr);
    if (!v) {
        fail = Reply::failure(t.isDouble() ? TKDE_E_INVALID_ARG : TKDE_E_INVALID_ARG,
                              QStringLiteral("\"view\" must be the id of a QTreeView, QListView or QTableView node"));
    }
    return v;
}

QString uidAt(const QModelIndex &i)
{
    return i.isValid() ? i.sibling(i.row(), 0).data(TinoxModel::UidRole).toString() : QString();
}

QJsonValue uidValue(const QModelIndex &i)
{
    return i.isValid() ? QJsonValue(uidAt(i)) : QJsonValue(QJsonValue::Null);
}

QJsonArray selectedIds(QAbstractItemView *v)
{
    QJsonArray ids;
    if (!v->selectionModel()) {
        return ids;
    }
    for (const QModelIndex &i : v->selectionModel()->selectedRows(0)) {
        ids.append(uidAt(i));
    }
    return ids;
}

/// Finds the index of a row in the model the view shows (a proxy or the model itself).
QModelIndex viewIndex(QAbstractItemView *v, const QString &uid)
{
    QAbstractItemModel *shown = v->model();
    // walk down to the TinoxModel
    QAbstractItemModel *m = shown;
    std::vector<QSortFilterProxyModel *> chain;
    while (auto *p = qobject_cast<QSortFilterProxyModel *>(m)) {
        chain.push_back(p);
        m = p->sourceModel();
    }
    auto *tm = qobject_cast<TinoxModel *>(m);
    Item *item = tm ? tm->find(uid) : nullptr;
    if (!item) {
        return QModelIndex();
    }
    QModelIndex idx = tm->indexOf(item);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        idx = (*it)->mapFromSource(idx);
    }
    return idx;
}

Reply attach(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QAbstractItemView *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    const QJsonValue mv = cmd.value(QStringLiteral("model"));
    QAbstractItemModel *model = nullptr;
    if (!mv.isNull()) {
        model = qobject_cast<QAbstractItemModel *>(mv.isDouble() ? b.object(int64_t(mv.toDouble())) : nullptr);
        if (!model) {
            return invalid(QStringLiteral("\"model\" must be the id of a model or proxy (null detaches)"));
        }
    }
    view->setModel(model);
    if (!model) {
        return Reply::success();
    }
    // a view fetches the top level when it lays out; it may not have been shown yet
    if (model->canFetchMore(QModelIndex())) {
        model->fetchMore(QModelIndex());
    }
    if (view->property("tkdeWired").toBool()) {
        return Reply::success(); // the signals are connected once per view
    }
    view->setProperty("tkdeWired", true);
    const int64_t id = int64_t(cmd.value(QStringLiteral("view")).toDouble());
    // the node key is the widget's objectName (docs/widget.md)
    auto emitEvent = [&b, view, id](const char *type, QJsonObject f) {
        f.insert(QStringLiteral("key"), view->objectName());
        b.emitEvent(QString::fromLatin1(type), f, id);
    };
    QObject::connect(view, &QAbstractItemView::activated, view, [emitEvent](const QModelIndex &i) {
        QJsonObject f;
        f.insert(QStringLiteral("id"), uidValue(i));
        emitEvent("model.activated", f);
    });
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(view, &QWidget::customContextMenuRequested, view, [emitEvent, view](const QPoint &pos) {
        QJsonObject f;
        f.insert(QStringLiteral("id"), uidValue(view->indexAt(pos)));
        f.insert(QStringLiteral("x"), view->viewport()->mapToGlobal(pos).x());
        f.insert(QStringLiteral("y"), view->viewport()->mapToGlobal(pos).y());
        f.insert(QStringLiteral("ids"), selectedIds(view));
        emitEvent("model.contextMenuRequested", f);
    });
    // selection models come and go with setModel: connect through the view's own signal
    auto connectSelection = [emitEvent, view] {
        if (!view->selectionModel()) {
            return;
        }
        QObject::connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, view, [emitEvent, view] {
            QJsonObject f;
            f.insert(QStringLiteral("ids"), selectedIds(view));
            emitEvent("model.selectionChanged", f);
        });
        QObject::connect(view->selectionModel(), &QItemSelectionModel::currentChanged, view, [emitEvent](const QModelIndex &cur) {
            QJsonObject f;
            f.insert(QStringLiteral("id"), uidValue(cur));
            emitEvent("model.currentChanged", f);
        });
    };
    connectSelection();
    view->setProperty("tkdeSelectionWiring", QVariant::fromValue(true));
    if (auto *tree = qobject_cast<QTreeView *>(view)) {
        QObject::connect(tree, &QTreeView::expanded, tree, [emitEvent](const QModelIndex &i) {
            QJsonObject f;
            f.insert(QStringLiteral("id"), uidValue(i));
            emitEvent("model.expanded", f);
        });
        QObject::connect(tree, &QTreeView::collapsed, tree, [emitEvent](const QModelIndex &i) {
            QJsonObject f;
            f.insert(QStringLiteral("id"), uidValue(i));
            emitEvent("model.collapsed", f);
        });
    }
    return Reply::success();
}

Reply select(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QAbstractItemView *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    if (!view->selectionModel()) {
        return invalid(QStringLiteral("the view has no model attached"));
    }
    QItemSelection selection;
    for (const QJsonValue &v : cmd.value(QStringLiteral("ids")).toArray()) {
        const QModelIndex i = viewIndex(view, v.toString());
        if (!i.isValid()) {
            return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("row '%1' is not shown (filtered out or unknown)").arg(v.toString()));
        }
        selection.select(i, i.siblingAtColumn(view->model()->columnCount() - 1));
    }
    view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    if (cmd.contains(QStringLiteral("current"))) {
        const QModelIndex i = viewIndex(view, cmd.value(QStringLiteral("current")).toString());
        if (i.isValid()) {
            view->selectionModel()->setCurrentIndex(i, QItemSelectionModel::NoUpdate);
        }
    }
    return Reply::success();
}

Reply selection(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QAbstractItemView *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    QJsonObject f;
    f.insert(QStringLiteral("ids"), selectedIds(view));
    f.insert(QStringLiteral("current"), uidValue(view->currentIndex()));
    return Reply::success(f);
}

Reply expand(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QAbstractItemView *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    auto *tree = qobject_cast<QTreeView *>(view);
    if (!tree) {
        return invalid(QStringLiteral("only a QTreeView expands"));
    }
    const QModelIndex i = viewIndex(view, cmd.value(QStringLiteral("id")).toString());
    if (!i.isValid()) {
        return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("row '%1' is not shown").arg(cmd.value(QStringLiteral("id")).toString()));
    }
    // a view that has not been laid out yet only remembers the expansion; fetch now like a visible one
    tree->doItemsLayout();
    tree->setExpanded(i, cmd.value(QStringLiteral("expanded")).toBool(true));
    return Reply::success();
}

Reply scrollTo(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QAbstractItemView *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    const QModelIndex i = viewIndex(view, cmd.value(QStringLiteral("id")).toString());
    if (!i.isValid()) {
        return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("row '%1' is not shown").arg(cmd.value(QStringLiteral("id")).toString()));
    }
    view->scrollTo(i);
    return Reply::success();
}

// ---- test hooks

Reply debugPointer(Bridge &b, const QJsonObject &cmd)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    if (!qobject_cast<QAbstractItemModel *>(o)) {
        return invalid(QStringLiteral("\"target\" must be a model"));
    }
    QJsonObject f;
    f.insert(QStringLiteral("ptr"), double(reinterpret_cast<quintptr>(o))); // in-process tests only (QAbstractItemModelTester)
    return Reply::success(f);
}

Reply debugRows(Bridge &b, const QJsonObject &cmd)
{
    // the rows as the view sees them (after proxies): depth-first list of "id" with depth
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    auto *m = qobject_cast<QAbstractItemModel *>(t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr);
    if (!m) {
        return invalid(QStringLiteral("\"target\" must be a model"));
    }
    QJsonArray out;
    std::function<void(const QModelIndex &, int)> walk = [&](const QModelIndex &parent, int depth) {
        for (int r = 0; r < m->rowCount(parent); ++r) {
            const QModelIndex i = m->index(r, 0, parent);
            QJsonObject row;
            row.insert(QStringLiteral("id"), i.data(TinoxModel::UidRole).toString());
            row.insert(QStringLiteral("text"), i.data(Qt::DisplayRole).toString());
            row.insert(QStringLiteral("depth"), depth);
            out.append(row);
            walk(i, depth + 1);
        }
    };
    walk(QModelIndex(), 0);
    QJsonObject f;
    f.insert(QStringLiteral("rows"), out);
    return Reply::success(f);
}

Reply debugData(Bridge &b, const QJsonObject &cmd)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    auto *m = qobject_cast<TinoxModel *>(t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr);
    if (!m) {
        return invalid(QStringLiteral("\"target\" must be a TinoxModel"));
    }
    Item *item = m->find(cmd.value(QStringLiteral("id")).toString());
    if (!item) {
        return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no such row"));
    }
    const QModelIndex i = m->indexOf(item, cmd.value(QStringLiteral("column")).toInt(0));
    QJsonObject f;
    f.insert(QStringLiteral("display"), i.data(Qt::DisplayRole).toString());
    f.insert(QStringLiteral("checkState"),
             i.data(Qt::CheckStateRole).isValid() ? QJsonValue(i.data(Qt::CheckStateRole).toInt()) : QJsonValue(QJsonValue::Null));
    f.insert(QStringLiteral("toolTip"), i.data(Qt::ToolTipRole).toString());
    f.insert(QStringLiteral("flags"), int(m->flags(i)));
    f.insert(QStringLiteral("hasIcon"), i.data(Qt::DecorationRole).isValid());
    f.insert(QStringLiteral("bold"), i.data(Qt::FontRole).isValid());
    f.insert(QStringLiteral("color"),
             i.data(Qt::ForegroundRole).isValid() ? i.data(Qt::ForegroundRole).value<QBrush>().color().name() : QString());
    return Reply::success(f);
}

/// Drops onto a model as if dragged from a file manager or from the tree itself.
Reply debugDrop(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    TinoxModel *m = modelOf(b, cmd, fail);
    if (!m) {
        return fail;
    }
    QMimeData mime;
    QJsonArray ids = cmd.value(QStringLiteral("ids")).toArray();
    if (!ids.isEmpty()) {
        mime.setData(kMime, QJsonDocument(ids).toJson(QJsonDocument::Compact));
    }
    QList<QUrl> urls;
    for (const QJsonValue &u : cmd.value(QStringLiteral("urls")).toArray()) {
        urls << QUrl(u.toString());
    }
    if (!urls.isEmpty()) {
        mime.setUrls(urls);
    }
    Item *parent = itemOf(m, cmd.value(QStringLiteral("parent")), fail, "parent");
    if (!parent) {
        return fail;
    }
    const bool accepted =
        m->dropMimeData(&mime, cmd.value(QStringLiteral("action")).toString() == QLatin1String("copy") ? Qt::CopyAction : Qt::MoveAction,
                        cmd.value(QStringLiteral("index")).toInt(-1), 0, m->indexOf(parent));
    QJsonObject f;
    f.insert(QStringLiteral("accepted"), accepted);
    return Reply::success(f);
}
} // namespace

void registerCommands(Bridge &bridge)
{
    bridge.addNamespace(QStringLiteral("model"));
    auto reg = [&bridge](const char *name, Reply (*fn)(Bridge &, const QJsonObject &)) {
        bridge.registerCommand(qs(name), [&bridge, fn](const QJsonObject &cmd) { return fn(bridge, cmd); });
    };
    reg("model.create", create);
    reg("model.reset", reset);
    reg("model.insert", insert);
    reg("model.remove", remove);
    reg("model.move", move);
    reg("model.update", update);
    reg("model.setChildren", setChildren);
    reg("model.get", get);
    reg("model.info", info);
    reg("model.createProxy", createProxy);
    reg("model.setFilter", setFilter);
    reg("model.attach", attach);
    reg("model.select", select);
    reg("model.selection", selection);
    reg("model.expand", expand);
    reg("model.scrollTo", scrollTo);
    if (bridge.debugCommandsEnabled()) {
        reg("model.debug.pointer", debugPointer);
        reg("model.debug.rows", debugRows);
        reg("model.debug.data", debugData);
        reg("model.debug.drop", debugDrop);
    }
}
} // namespace tkde::model

#include "model.moc"
