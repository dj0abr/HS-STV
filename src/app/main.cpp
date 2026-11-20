// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"
#include <QApplication>
#include <QSettings>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--list-audio") {
            for (const auto& d : mm_sstv::makeAudioOutput()->devices()) std::cout << d.id << "\t" << d.name << '\n';
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--list-inputs") {
            for (const auto& d : mm_sstv::makeAudioInput()->devices()) std::cout << d.id << "\t" << d.name << '\n';
            return 0;
        }
        QApplication app(argc, argv);
        QCoreApplication::setOrganizationName("HS-STV"); QCoreApplication::setApplicationName("HS-STV");
        QSettings settings;
        // Preserve audio devices, levels and display preferences from the former name.
        if (!settings.value("migration/mmSstv", false).toBool()) {
            QSettings previous("mm-sstv", "mm-sstv");
            for (const auto& key : previous.allKeys())
                if (!settings.contains(key)) settings.setValue(key, previous.value(key));
            settings.setValue("migration/mmSstv", true);
        }
        mm_sstv::MainWindow window(mm_sstv::makeAudioOutput(), settings, mm_sstv::makeAudioInput()); window.show();
        return app.exec();
    } catch (const std::exception& e) { std::cerr << "HS-STV: " << e.what() << '\n'; return 1; }
}
