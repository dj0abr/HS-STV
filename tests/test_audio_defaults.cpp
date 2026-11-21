// SPDX-License-Identifier: GPL-3.0-or-later
// A private PulseAudio server verifies default selection without physical audio.
#include "mm_sstv/audio_input.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace mm_sstv;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
QByteArray pactl(const QStringList& args, bool mustSucceed = true) {
    QProcess p; p.start("pactl", args);
    const bool ok = p.waitForFinished(3000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (mustSucceed && !ok) throw std::runtime_error(p.readAllStandardError().toStdString());
    return ok ? p.readAllStandardOutput() : QByteArray();
}
int indexOf(const QString& type, const QString& name) {
    const auto entries = QJsonDocument::fromJson(pactl({"-f", "json", "list", type})).array();
    for (const auto entry : entries) if (entry.toObject().value("name").toString() == name)
        return entry.toObject().value("index").toInt(-1);
    throw std::runtime_error("Missing private audio device");
}
void checkStreams(const QString& name) {
    const auto outputs = QJsonDocument::fromJson(pactl({"-f", "json", "list", "sink-inputs"})).array();
    const auto inputs = QJsonDocument::fromJson(pactl({"-f", "json", "list", "source-outputs"})).array();
    require(outputs.size() == 1 && inputs.size() == 1, "Unexpected private audio stream count");
    require(outputs[0].toObject().value("sink").toInt(-1) == indexOf("sinks", name), "Wrong default output");
    require(inputs[0].toObject().value("source").toInt(-1) == indexOf("sources", name + ".monitor"), "Wrong default input");
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (QStandardPaths::findExecutable("pulseaudio").isEmpty() || QStandardPaths::findExecutable("pactl").isEmpty()) return 77;
    QProcess server;
    try {
        QTemporaryDir directory; require(directory.isValid(), "Cannot create private audio directory");
        const auto runtime = directory.filePath("runtime"); require(QDir().mkpath(runtime), "Cannot create runtime directory");
        QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QFile client(directory.filePath("client.conf")); require(client.open(QIODevice::WriteOnly), "Cannot create audio config");
        client.write("autospawn = no\nenable-shm = no\n"); client.close();
        qputenv("PULSE_SERVER", ("unix:" + directory.filePath("native")).toUtf8());
        qputenv("PULSE_CLIENTCONFIG", client.fileName().toUtf8()); qputenv("XDG_RUNTIME_DIR", runtime.toUtf8());
        server.setProcessChannelMode(QProcess::MergedChannels);
        server.start("pulseaudio", {"-n", "--daemonize=no", "--use-pid-file=no", "--exit-idle-time=-1",
            "--load", "module-native-protocol-unix socket=" + directory.filePath("native") + " auth-anonymous=1",
            "--load", "module-null-sink sink_name=default_a rate=48000 channels=1",
            "--load", "module-null-sink sink_name=default_b rate=48000 channels=1"});
        require(server.waitForStarted(), "Cannot start private audio server");
        bool ready = false;
        for (unsigned i = 0; i < 100; ++i) {
            if (!pactl({"info"}, false).isEmpty()) { ready = true; break; }
            QThread::msleep(20);
        }
        require(ready, "Private audio server unavailable");
        pactl({"set-default-sink", "default_a"}); pactl({"set-default-source", "default_a.monitor"});
        const auto output = makeAudioOutput(); const auto input = makeAudioInput();
        const auto start = [&] {
            output->start(systemDefaultAudioDevice, [](float* samples, std::size_t count) {
                std::fill_n(samples, count, .01f); return count;
            }, 48000);
            input->start(systemDefaultAudioDevice, [](const float*, std::size_t) {}, 48000);
        };
        start(); checkStreams("default_a");
        pactl({"set-default-sink", "default_b"}); pactl({"set-default-source", "default_b.monitor"});
        checkStreams("default_a"); // An active stream stays on its resolved device.
        input->stop(); output->stop(); start(); checkStreams("default_b");
        input->stop(); output->stop();
        bool rejected = false;
        try { input->start("missing-explicit-device", [](const float*, std::size_t) {}, 48000); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "Explicit missing input fell back to a default");
        server.terminate(); require(server.waitForFinished(3000), "Cannot stop private server");
        std::cout << "System defaults: input/output resolved, active devices retained, defaults followed after reopen and no explicit fallback passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n' << server.readAll().constData();
        server.kill(); server.waitForFinished(3000); return 1;
    }
}
