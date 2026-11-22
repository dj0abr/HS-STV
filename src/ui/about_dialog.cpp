// SPDX-License-Identifier: GPL-3.0-or-later
#include "about_dialog.hpp"
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QTextBrowser>
#include <QVBoxLayout>
namespace mm_sstv {
AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("aboutDialog"); setWindowTitle("Hilfe · HS_STV"); setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(480, 360);
    auto* layout = new QVBoxLayout(this);
    auto* browser = new QTextBrowser(this); browser->setObjectName("aboutDetails");
    browser->setOpenExternalLinks(true); browser->setUndoRedoEnabled(false);
    QString html = QStringLiteral(
        "<h2>HS_STV - QO100 Bildübertragung und Bakenmonitor by DJ0ABR</h2>"
        "<p><b>Version %1</b></p>"
        "<p>Github: <a href=\"https://github.com/dj0abr/HS-STV\">https://github.com/dj0abr/HS-STV</a><br>"
        "License: <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GPL-3.0</a></p>"
        "<h3>Verwendete Bibliotheken</h3>"
        "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" border=\"1\">"
        "<tr><th align=\"left\">Bibliothek</th><th align=\"left\">Autor / Mitwirkende</th><th align=\"left\">Lizenz</th></tr>"
        "<tr><td>Qt %2<br>(Core, Gui, Widgets)</td><td>The Qt Company Ltd. und Mitwirkende</td>"
        "<td>LGPL-3.0 / GPL-3.0</td></tr>"
        "<tr><td><a href=\"https://liquidsdr.org/\">liquid-dsp 1.8.3</a></td><td>Joseph Gaeddert und Mitwirkende</td><td>MIT</td></tr>"
        "<tr><td><a href=\"https://zlib.net/\">zlib</a></td><td>Jean-loup Gailly und Mark Adler</td><td>Zlib</td></tr>")
        .arg(QString::fromLatin1(MM_SSTV_VERSION), QString::fromLatin1(qVersion()));
#ifdef MM_SSTV_AUDIO_PORTAUDIO
    html += QStringLiteral(
        "<tr><td><a href=\"https://www.portaudio.com/\">PortAudio</a></td><td>Ross Bencina, Phil Burk und Mitwirkende</td><td>MIT</td></tr>");
#else
    html += QStringLiteral(
        "<tr><td><a href=\"https://www.freedesktop.org/wiki/Software/PulseAudio/\">PulseAudio (libpulse)</a></td>"
        "<td>Lennart Poettering, Pierre Ossman und Mitwirkende</td><td>LGPL-2.1-or-later</td></tr>");
#endif
    html += QStringLiteral(
        "<tr><td><a href=\"http://www.schifra.com/\">Schifra</a><br>(Reed-Solomon)</td><td>Arash Partow</td>"
        "<td><a href=\"http://www.schifra.com/license.html\">General Schifra License</a><br>(laut enthaltenen Headern)</td></tr>"
        "</table>"
        "<h3>Übernommene Decoder und Verfahrensreferenzen</h3>"
        "<p><b>HS-Modem / QO-100-Modem:</b> Kurt Moraw (DJ0ABR), AMSAT-DL und Mitwirkende · GPL-2.0-or-later.</p>"
        "<p><b>PSK-Bakendekoder:</b> Levent (TA2LDV); C++-Port aus QO100_TRX2 · GPL-3.0-only. "
        "FEC-Verfahrensreferenzen: Daniel Estévez (gr-satellites) und Phil Karn (libfec).</p>"
        "<p>Die Liste nennt direkt eingebundene Bibliotheken und übernommene Decoder. "
        "Bildformat-Plugins und weitere Systemabhängigkeiten werden durch Qt beziehungsweise das Betriebssystem bereitgestellt.</p>"
        "<p>Die Lizenz- und Copyright-Hinweise der jeweiligen Komponenten bleiben gültig.</p>");
    browser->setHtml(html); layout->addWidget(browser, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* close = buttons->button(QDialogButtonBox::Close); close->setText("Schließen"); close->setObjectName("aboutCloseButton");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close); layout->addWidget(buttons);
    QSize size(860, 640);
    if (auto* screen = QGuiApplication::primaryScreen()) size = size.boundedTo(screen->availableGeometry().size() - QSize(32, 48));
    resize(size.expandedTo(minimumSize()));
}
}
