/*
HyperDrive Companion
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/
#pragma once

/*
 * What the dock and the settings dialog share: swatches, the line icons,
 * and a status dot. Colors come from OBS's theme (palette) where they can.
 */

#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>

#include "net/hyperdrive-client.hpp"
#include "text.hpp"

namespace style {

/** A rounded color swatch ("" for none) */
inline QIcon swatch(const QString &color, int size = 16)
{
	QPixmap pixmap(size, size);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(QColor(128, 128, 128), 1));
	painter.setBrush(color.isEmpty() ? QBrush(Qt::NoBrush) : QBrush(QColor(color)));
	painter.drawRoundedRect(QRectF(1.5, 1.5, size - 3, size - 3), size / 4.0, size / 4.0);
	return QIcon(pixmap);
}

/** A 24x24 line icon from SVG path data, in a color */
inline QIcon icon(const char *paths, const QColor &color)
{
	QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='" +
			 color.name().toUtf8() + "' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>" +
			 paths + "</svg>";
	QSvgRenderer renderer(svg);
	QPixmap pixmap(48, 48);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	renderer.render(&painter);
	return QIcon(pixmap);
}

/** "● Connected to localhost:5500", rich text */
inline QString status(hd::ConnectionStatus status, const QString &address)
{
	QString where = address.toHtmlEscaped();
	switch (status) {
	case hd::ConnectionStatus::Connected:
		return QStringLiteral("<span style=\"color:#3dd68c\">●</span> ") +
		       T("HyperDrive.Status.Connected").arg(where);
	case hd::ConnectionStatus::Connecting:
		return QStringLiteral("<span style=\"color:#ff8a3d\">●</span> ") +
		       T("HyperDrive.Status.Connecting").arg(where);
	case hd::ConnectionStatus::Disconnected:
		break;
	}
	return QStringLiteral("<span style=\"color:#f2555a\">●</span> ") +
	       T("HyperDrive.Status.Disconnected").arg(where);
}

} // namespace style
