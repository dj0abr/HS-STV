// SPDX-License-Identifier: GPL-3.0-or-later
// C++ adaptation of QO100_TRX2/external/mmdecoder/data_collector.py (DJ0ABR).
#pragma once
#include "mm_sstv/image_assembler.hpp"
#include <QDateTime>
#include <QMap>
#include <QString>
#include <deque>
#include <optional>

namespace mm_sstv {
enum class BeaconChannel { CwSkimmer, DxCluster };
struct BeaconMessage {
    BeaconChannel channel = BeaconChannel::CwSkimmer;
    QDateTime receivedAt;
    QString raw, text;
    QMap<QString, QString> fields;
};
// Called with FEC/CRC-validated frames, on the receiver's owning thread.
class BeaconMessages {
public:
    static constexpr unsigned duplicateSeconds = 120;
    static constexpr std::size_t maximumRecent = 4096;
    std::optional<BeaconMessage> accept(const ReceivedFrame& frame,
                                      const QDateTime& receivedAt = QDateTime::currentDateTimeUtc());
private:
    struct Seen { BeaconChannel channel; QString raw; QDateTime at; };
    std::deque<Seen> recent_;
};
}
