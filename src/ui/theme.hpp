// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class QApplication;
namespace mm_sstv {
// Embedded resources and Fusion keep the Qt 5/6 appearance independent of the OS.
void applyDarkTheme(QApplication& app);
}
