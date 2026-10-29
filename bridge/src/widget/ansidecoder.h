#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>

#include <algorithm>
#include <cmath>

namespace tkde
{
// Incremental SGR decoder for a read-only log: escape sequences split across writes are kept until
// complete, colours and bold survive between writes, other CSI sequences are dropped.
class AnsiDecoder
{
public:
    void append(QPlainTextEdit *console, const QString &text)
    {
        pending += text;
        QTextCursor cursor(console->document());
        cursor.movePosition(QTextCursor::End);
        qsizetype pos = 0;
        while (pos < pending.size()) {
            if (pending.at(pos) != QChar(0x1b)) {
                const qsizetype end = pending.indexOf(QChar(0x1b), pos);
                const qsizetype count = (end < 0 ? pending.size() : end) - pos;
                cursor.insertText(pending.mid(pos, count), legible(format, console->palette()));
                pos += count;
                continue;
            }
            if (pos + 1 == pending.size()) {
                break;
            }
            if (pending.at(pos + 1) != QLatin1Char('[')) {
                pos += 2;
                continue;
            }
            qsizetype end = pos + 2;
            while (end < pending.size() && !(pending.at(end).unicode() >= 0x40 && pending.at(end).unicode() <= 0x7e)) {
                ++end;
            }
            if (end == pending.size()) {
                break;
            }
            if (pending.at(end) == QLatin1Char('m')) {
                sgr(pending.mid(pos + 2, end - pos - 2).split(QLatin1Char(';')));
            }
            pos = end + 1;
        }
        pending.remove(0, pos);
        if (pending.size() > 1024) {
            pending.clear();
        }
        console->setTextCursor(cursor);
        console->ensureCursorVisible();
    }

    // Forgets the colours and any half-received sequence (after the log was cleared).
    void reset()
    {
        pending.clear();
        format = QTextCharFormat();
    }

    // WCAG contrast ratio of two colours.
    static double contrast(const QColor &a, const QColor &b)
    {
        auto luminance = [](const QColor &c) {
            auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
        };
        const double la = luminance(a), lb = luminance(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    }

    // The fixed 16 colours of a terminal assume one background. Against the log's own background (a dark
    // or a high-contrast scheme) dark blue or black text would vanish, so a colour that is hard to read
    // is moved toward the normal text colour until it reaches the contrast WCAG asks for text (4.5:1).
    static QTextCharFormat legible(QTextCharFormat format, const QPalette &palette)
    {
        if (format.foreground().style() == Qt::NoBrush) {
            return format;
        }
        const QColor background = format.background().style() != Qt::NoBrush ? format.background().color() : palette.color(QPalette::Base);
        const QColor text = palette.color(QPalette::Text);
        QColor color = format.foreground().color();
        for (int step = 1; step <= 10 && contrast(color, background) < 4.5; ++step) {
            color =
                QColor::fromRgbF(color.redF() + (text.redF() - color.redF()) * 0.2, color.greenF() + (text.greenF() - color.greenF()) * 0.2,
                                 color.blueF() + (text.blueF() - color.blueF()) * 0.2);
        }
        format.setForeground(color);
        return format;
    }

private:
    void sgr(const QStringList &codes)
    {
        static const QColor colors[] = {QColor(0, 0, 0),     QColor(170, 0, 0),    QColor(0, 170, 0),    QColor(170, 85, 0),
                                        QColor(0, 0, 170),   QColor(170, 0, 170),  QColor(0, 170, 170),  QColor(170, 170, 170),
                                        QColor(85, 85, 85),  QColor(255, 85, 85),  QColor(85, 255, 85),  QColor(255, 255, 85),
                                        QColor(85, 85, 255), QColor(255, 85, 255), QColor(85, 255, 255), QColor(255, 255, 255)};
        for (qsizetype i = 0; i < codes.size(); ++i) {
            const int code = codes.at(i).toInt();
            if (code == 0) {
                format = QTextCharFormat();
            } else if (code == 1 || code == 22) {
                format.setFontWeight(code == 1 ? QFont::Bold : QFont::Normal);
            } else if (code == 39) {
                format.clearForeground();
            } else if (code == 49) {
                format.clearBackground();
            } else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
                format.setForeground(colors[code >= 90 ? code - 90 + 8 : code - 30]);
            } else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) {
                format.setBackground(colors[code >= 100 ? code - 100 + 8 : code - 40]);
            } else if ((code == 38 || code == 48) && i + 1 < codes.size()) {
                // Consume unsupported extended colours as one sequence, never as SGR resets.
                const int mode = codes.at(++i).toInt();
                i += mode == 2 ? 3 : (mode == 5 ? 1 : 0);
            }
        }
    }

    QString pending;
    QTextCharFormat format;
};
} // namespace tkde
