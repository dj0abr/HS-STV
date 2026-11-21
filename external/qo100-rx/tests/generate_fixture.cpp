// Offline test fixture only: never opens a sound card or sends network data.
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "hsmodem.h"
#include <iostream>
#include <string>
#include <algorithm>
#ifdef MM_SSTV_TEST_BULLETINS
#include "bulletin_fixture.hpp"
#endif
#include "fec/schifra_galois_field.hpp"
#include "fec/schifra_galois_field_polynomial.hpp"
#include "fec/schifra_sequential_root_generator_polynomial_creator.hpp"
#include "fec/schifra_reed_solomon_encoder.hpp"
#include "fec/schifra_reed_solomon_block.hpp"
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    double offset = argc > 1 ? atof(argv[1]) : 0;
    std::string mode = argc > 2 ? argv[2] : "clean";
    bool corrupt = mode == "corrupt";
    int frame_count = argc > 3 ? atoi(argv[3]) : 45;
    const schifra::galois::field field(8, schifra::galois::primitive_polynomial_size06,
                                      schifra::galois::primitive_polynomial06);
    schifra::galois::field_polynomial generator(field);
    schifra::make_sequential_root_generator_polynomial(field,120,32,generator);
    schifra::reed_solomon::encoder<255,32,223> encoder(field,generator);
    const std::string modulation = argc > 4 ? argv[4] : "8apsk";
    if (modulation != "8apsk" && modulation != "qpsk4410" && modulation != "mm-beacon") return 2;
    const unsigned bps = modulation == "qpsk4410" ? 2 : 3;
    const unsigned rate = bps == 2 ? 44100 : 48000, k = bps == 2 || modulation == "mm-beacon" ? 20 : 24;
    auto mod=modem_create(bps == 2 ? LIQUID_MODEM_QPSK : LIQUID_MODEM_APSK8);
    float h[721];
    liquid_firdes_prototype(LIQUID_FIRFILT_RRC,k,15,0.2f,-0.2f,h);
    auto interp=firinterp_crcf_create(k,h,2*k*15+1);
    double phase=argc > 5 ? atof(argv[5]) : 0, pilot_phase=0;
    uint64_t audio_samples = 0;
#ifdef MM_SSTV_TEST_BULLETINS
    const auto bulletins = mode == "bulletin" ? beacon_test::frames(beacon_test::zip(beacon_test::content())) : std::vector<mm_sstv::ReceivedFrame>{};
#endif
    for(int frame=0;frame<frame_count;++frame) {
        uint8_t raw[223]={}; raw[0]=frame; raw[1]=8|0x30;
        raw[2]=2;
        const char* msg="TEST CW-SKIMMER RX VALIDATION";
        memcpy(raw+3,msg,strlen(msg));
        if (mode == "spots") {
            std::fill(raw + 2, raw + 221, 0);
            if (frame % 2 == 0) {
                raw[2] = 2;
                const char* text = "DL0ABC:~10489750~DL1TEST~18~23~CQ~1301Z";
                memcpy(raw + 3, text, strlen(text));
            } else {
                raw[2] = 0;
                const char* text = "DL1AAA~14025.0~VK9TEST~CQ DX~1300Z~JO40";
                raw[3] = static_cast<uint8_t>(strlen(text));
                memcpy(raw + 4, text, strlen(text));
            }
        }
#ifdef MM_SSTV_TEST_BULLETINS
        if (!bulletins.empty()) {
            const auto& file = bulletins[unsigned(frame) % bulletins.size()];
            raw[0] = uint8_t(file.counter); raw[1] = uint8_t(((file.counter >> 8) << 6) | (file.status << 4) | file.type);
            std::copy(file.payload.begin(), file.payload.end(), raw + 2);
        }
#endif
        uint16_t crc=Crc16_messagecalc(0,raw,221);
        raw[221]=crc>>8; raw[222]=crc;
        if(corrupt) raw[222]^=1; // Valid RS, deliberately invalid frame CRC.
        schifra::reed_solomon::block<255,32> block;
        for(int i=0;i<223;++i)block[i]=raw[i];
        if(!encoder.encode(block))return 2;
        uint8_t air[258]={0x53,0xe1,0xa6};
        for(int i=0;i<255;++i)air[3+i]=block[i];
        uint8_t* scrambled=RX_Scramble(air+3,255); // XOR is symmetric.
        memcpy(air+3,scrambled,255);
        unsigned int acc=0; int bits=0;
        for(auto byte:air) {
            acc=(acc<<8)|byte;bits+=8;
            while(bits>=int(bps)) {
                bits-=bps;unsigned int sym=(acc>>bits)&((1u<<bps)-1);
                if (bps == 2) sym ^= sym >> 1;
                liquid_float_complex x,y[24];modem_modulate(mod,sym,&x);
                firinterp_crcf_execute(interp,x,y);
                for(unsigned yi=0; yi<k; ++yi) {
                    const auto c = y[yi];
                    double t = audio_samples++/double(rate);
                    double frequency_offset = offset;
                    if(mode == "drift") frequency_offset += t;
                    if(mode == "jump" && t >= 8) frequency_offset += 70;
                    if(mode == "dropout" && t >= 11) frequency_offset += 40;
                    phase+=2*M_PI*(1500+frequency_offset)/double(rate);
                    pilot_phase+=2*M_PI*(100+frequency_offset)/double(rate);
                    // Matches the original SSB audio synthesis (real + imag).
                    float re=c.real*cos(phase)-c.imag*sin(phase);
                    float im=c.real*sin(phase)+c.imag*cos(phase);
                    float audio=0.1f*(re+im)+0.006f*cos(pilot_phase);
                    if(mode == "nopilot") audio-=0.006f*cos(pilot_phase);
                    if(mode == "interferer") audio+=0.04f*cos(2*M_PI*310*t);
                    if(mode == "dropout" && t>=8 && t<11) audio=0;
                    fwrite(&audio,sizeof(audio),1,stdout);
                }
            }
        }
    }
    firinterp_crcf_destroy(interp);modem_destroy(mod);
}
