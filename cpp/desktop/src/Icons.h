#pragma once

// Ícones desenhados a partir da fonte Segoe MDL2 Assets (vem no Windows 10 e 11), sem arquivos extras.

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

namespace odonto::icons {

inline constexpr ushort home = 0xE80F;
inline constexpr ushort review = 0xE73A;
inline constexpr ushort search = 0xE721;
inline constexpr ushort chart = 0xE9D2;
inline constexpr ushort cpu = 0xE950;
inline constexpr ushort help = 0xE897;
inline constexpr ushort settings = 0xE713;
inline constexpr ushort play = 0xE768;
inline constexpr ushort refresh = 0xE72C;
inline constexpr ushort save = 0xE74E;
inline constexpr ushort folder = 0xE8B7;
inline constexpr ushort stop = 0xE71A;
inline constexpr ushort download = 0xE896;

inline QIcon glyph(ushort code, const QColor& color = QColor("#2d3a4a")) {
    QIcon icon;
    for (int size : {16, 32}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        QFont font("Segoe MDL2 Assets");
        font.setPixelSize(size * 13 / 16);
        painter.setFont(font);
        painter.setPen(color);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(QChar(code)));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

}  // namespace odonto::icons
