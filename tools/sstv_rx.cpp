// SPDX-License-Identifier: GPL-3.0-or-later
// Local PCM-to-JSON adapter around the proven QO100_TRX2 SSTV decoder.
#include "mm_sstv/sstv_audio.hpp"
#include "mm_sstv/audio_spectrum.hpp"
#include <liquid/liquid.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    using namespace mm_sstv;
    auto mode = Modulation::Martin1;
    for (int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if (arg=="--monitor") continue;
        if (arg=="--input-rate" && i+1<argc && std::string(argv[++i])=="48000") continue;
        if (arg=="--modulation" && i+1<argc) {
            const std::string key=argv[++i];
            if(key=="martin1") mode=Modulation::Martin1;
            else if(key=="scottie1") mode=Modulation::Scottie1;
            else if(key=="martin2") mode=Modulation::Martin2;
            else if(key=="scottie2") mode=Modulation::Scottie2;
            else { std::fprintf(stderr,"Expected martin1, martin2, scottie1 or scottie2\n"); return 2; }
        } else { std::fprintf(stderr,"Usage: hs-stv-sstv-rx-core --modulation martin1|martin2|scottie1|scottie2 --input-rate 48000 < mono-float32.raw\n"); return 2; }
    }
    bool active=false, partial=false, synchronized=false;
    std::bitset<256> rows, syncRows;
    unsigned examinedRows = 0;
    double offset=0;
    std::uint64_t samples=0,lastLine=0;
    const auto callbacks=qo100::sstv::Decoder::Callbacks{
        [&](qo100::sstv::Frame frame) {
            active=true;partial=frame.partial;rows.reset();syncRows.reset();examinedRows=0;synchronized=false;
            std::printf("{\"event\":\"sstv_begin\",\"partial\":%s}\n",partial?"true":"false");
        },
        [&](const qo100::sstv::Line& line) {
            if(line.index>=256)return;
            syncRows.set(line.index, line.synchronized); examinedRows = std::max(examinedRows, line.index + 1);
            rows.set(line.index);offset=line.frequency_offset_hz;synchronized=line.synchronized;lastLine=samples;
            std::printf("{\"event\":\"sstv_line\",\"index\":%u,\"synchronized\":%s,\"frequency_offset_hz\":%.3f,\"rgb_hex\":\"",line.index,line.synchronized?"true":"false",offset);
            for(const auto byte:line.rgb)std::printf("%02x",byte);
            std::printf("\"}\n");
        },
        [&](bool complete) {
            active=false;synchronized=false;
            if (!partial) examinedRows = 256; // VIS establishes the complete expected image height.
            std::printf("{\"event\":\"sstv_end\",\"complete\":%s}\n",complete&&!partial&&rows.all()?"true":"false");
        }
    };
    qo100::sstv::Decoder decoder(callbacks,sstvMode(mode));
    auto decim=firdecim_rrrf_create_kaiser(4,8,60);firdecim_rrrf_set_scale(decim,.25f);
    AudioSpectrum spectrum;
    AudioSpectrum waterfall(48000, 2048); // 43-ms windows, 23.44-Hz bins, sampled every 10 ms.
    std::array<float,4800> buffer{};std::array<float,4> group{};unsigned pending=0;
    while(true) {
        const auto bytes=std::fread(buffer.data(),1,buffer.size()*sizeof(float),stdin);
        if(bytes%sizeof(float)){std::fprintf(stderr,"Truncated float32 audio sample\n");firdecim_rrrf_destroy(decim);return 2;}
        const auto count=bytes/sizeof(float);double power=0;
        for(std::size_t i=0;i<count;++i){
            const float value=std::isfinite(buffer[i])?buffer[i]:0;buffer[i]=value;power+=double(value)*value;++samples;
            group[pending++]=value;if(pending<4)continue;pending=0;float y=0;firdecim_rrrf_execute(decim,group.data(),&y);
            const auto pcm=std::int16_t(std::lround(std::clamp(double(y),-1.,32767./32768)*32768));decoder.push(&pcm,1);
        }
        if(count){
            spectrum.push(buffer.data(),count);const auto db=spectrum.levels();
            std::printf("{\"sync_detected\":%zu,\"sync_missed\":%zu,", syncRows.count(), examinedRows - syncRows.count());
            std::printf("\"event\":\"status\",\"locked\":%s,\"receiving\":%s,\"received_lines\":%zu,\"frequency_offset_hz\":%.3f,\"rms_dbfs\":%.2f,\"spectrum_bin_hz\":%.8f,\"spectrum_dbfs\":[",active&&synchronized&&samples-lastLine<72000?"true":"false",active?"true":"false",rows.count(),offset,10*std::log10(std::max(power/count,1e-20)),spectrum.frequencyStep());
            for(std::size_t i=0;i<db.size();++i)std::printf("%s%.1f",i?",":"",db[i]);
            std::printf("],\"waterfall_bin_hz\":%.8f,\"waterfall_dbfs\":[", waterfall.frequencyStep());
            bool firstRow = true;
            for (std::size_t at = 0; at + 480 <= count; at += 480) {
                waterfall.push(buffer.data() + at, std::min<std::size_t>(480, count - at));
                const auto row = waterfall.levels();
                if (row.empty()) continue;
                std::printf("%s[", firstRow ? "" : ","); firstRow = false;
                for (std::size_t i = 0; i < row.size(); ++i) std::printf("%s%.1f", i ? "," : "", row[i]);
                std::printf("]");
            }
            std::printf("]}\n");std::fflush(stdout);
        }
        if(bytes<buffer.size()*sizeof(float))break;
    }
    decoder.finish();std::fflush(stdout);firdecim_rrrf_destroy(decim);
    return std::ferror(stdin)?1:0;
}
