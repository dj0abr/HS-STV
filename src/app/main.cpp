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
        QCoreApplication::setOrganizationName("mm-sstv"); QCoreApplication::setApplicationName("mm-sstv");
        QSettings settings;
        mm_sstv::MainWindow window(mm_sstv::makeAudioOutput(), settings, mm_sstv::makeAudioInput()); window.show();
        return app.exec();
    } catch (const std::exception& e) { std::cerr << "mm-sstv: " << e.what() << '\n'; return 1; }
}
