// SPDX-License-Identifier: GPL-3.0-or-later
// Offline SSTV export; does not open any audio device.
#include "image_codec.hpp"
#include <QCoreApplication>
#include <QImageReader>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
namespace {
void word(std::ostream& out, std::uint32_t value, unsigned bytes) { for(unsigned n=0;n<bytes;++n)out.put(char((value>>(8*n))&255)); }
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try {
        if(argc!=4&&argc!=5)throw std::invalid_argument("Usage: hs-stv-sstv-signal INPUT OUTPUT.wav martin1|martin2|scottie1|scottie2 [LEVEL_PERCENT=100]");
        const std::string key(argv[3]);
        if(key!="martin1"&&key!="scottie1"&&key!="martin2"&&key!="scottie2")throw std::invalid_argument("Expected martin1, martin2, scottie1 or scottie2");
        const auto mode=key=="martin1"?mm_sstv::Modulation::Martin1:key=="martin2"?mm_sstv::Modulation::Martin2:key=="scottie1"?mm_sstv::Modulation::Scottie1:mm_sstv::Modulation::Scottie2;
        const int level=argc==5?std::stoi(argv[4]):100;if(level<0||level>100)throw std::invalid_argument("Level must be 0..100");
        QImageReader reader(QString::fromLocal8Bit(argv[1]));reader.setAutoTransform(true);
        const auto picture=mm_sstv::fitPicture(reader.read(),QSize(320,256));
        if(!picture.save(QString::fromLocal8Bit(argv[2])+".png","PNG"))throw std::runtime_error("Cannot write reference PNG");
        const auto raster=mm_sstv::sstvRaster(picture);mm_sstv::SstvAudio signal(mode,raster);const auto count=signal.frames();
        std::ofstream out(argv[2],std::ios::binary);out.exceptions(std::ios::badbit|std::ios::failbit);
        out.write("RIFF",4);word(out,36+count*2,4);out.write("WAVEfmt ",8);word(out,16,4);word(out,1,2);word(out,1,2);
        word(out,48000,4);word(out,96000,4);word(out,2,2);word(out,16,2);out.write("data",4);word(out,count*2,4);
        std::array<float,4800> buffer{};
        while(const auto n=signal.render(buffer.data(),buffer.size(),level/100.f))for(std::size_t i=0;i<n;++i){
            if(!std::isfinite(buffer[i])||std::abs(buffer[i])>=1)throw std::runtime_error("Clipped SSTV audio");
            word(out,std::uint16_t(std::int16_t(std::lround(buffer[i]*32767))),2);
        }
        out.close();std::cout<<"SSTV "<<key<<" 320x256; "<<count<<" samples\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
