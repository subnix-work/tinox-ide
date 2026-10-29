// Translation of the strings Tinox hands to the bridge. The strings are English literals in the Tinox
// sources (extracted by scripts/i18n-extract.py); the catalogs are KDE's, domain "tinox-ide".
#ifndef TINOXKDE_I18N_H
#define TINOXKDE_I18N_H

#include <KLocalizedString>

#include <QByteArray>
#include <QString>

namespace tkde
{
class Bridge;

/// The translation of a static UI string; the text itself when there is none (also for data that
/// merely passes through, such as a file name, which no catalog contains).
inline QString tr(const QString &text)
{
    if (text.isEmpty()) {
        return text;
    }
    const QByteArray utf8 = text.toUtf8();
    return ki18n(utf8.constData()).toString();
}

namespace translation
{
/// `i18n.translate`, for the messages Tinox assembles at run time (docs/i18n.md).
void registerCommands(Bridge &bridge);
} // namespace translation
} // namespace tkde

#endif // TINOXKDE_I18N_H
