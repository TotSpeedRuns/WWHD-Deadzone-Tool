#include "../source/deadzone.hpp"
#include "../source/title.hpp"
#include "../source/pro_profile.hpp"
#include <cassert>
#include <bit>
#include <cstdint>
#include <limits>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

int main() {
    // Windows strtol has the same 32-bit signed range as the Wii U parser.
    if (sizeof(long)==4) {
        errno=0;
        assert(static_cast<uint32_t>(std::strtol("-2140132552",nullptr,0))==0x80702b38);
        assert(errno==0);
        errno=0;
        std::strtol("0x80702b38",nullptr,0);
        assert(errno==ERANGE);
    }
    assert(supportedTitle(0x0005000010143600ULL));
    assert(supportedTitle(0x0005000010575a01ULL));
    assert(!supportedTitle(0x0005000010143500ULL));
    assert(!supportedTitle(0x0005000010101c00ULL));
    assert(!supportedTitle(0));
    nativepro::Snapshot saved[4];
    auto &s=saved[0]; s.schema=1; s.minimum=258; s.maximum=919;
    s.address[5]=1; s.origin[0]=-17; s.origin[1]=29;
    assert(nativepro::valid(s));
    const uint8_t first[6]={0,0,0,0,0,1}, second[6]={0,0,0,0,0,2};
    assert(nativepro::match(saved,first)==0);
    assert(nativepro::match(saved,second)==-1); // Never apply another pad's origin.
    s.origin[0]=-2049; assert(!nativepro::valid(s));
    s.origin[0]=-17; s.maximum=258; assert(!nativepro::valid(s));
    s.maximum=919; s.cross=2; assert(!nativepro::valid(s)); s.cross=0;
    assert(nativepro::relocatedAddress(0x3c001001,0x30008380)==0x10008380);
    assert(nativepro::relocatedAddress(0x3c003001,0x3000f380)==0x3000f380);
    uint32_t code[8]={0x7c0802a6,0x9421fff0,0x93e1000c,0x90010014,0x3c003001,0x546c103a,0x30008380,0x7fec002e};
    assert(nativepro::recalibrateSignature(code));
    code[5]^=1; assert(!nativepro::recalibrateSignature(code));
    alignas(4) uint8_t block[0xc20]; std::memset(block,0x55,sizeof(block));
    for (auto offset:nativepro::originOffsets) *reinterpret_cast<int16_t *>(block+offset)=7;
    uint8_t original[sizeof(block)]; std::memcpy(original,block,sizeof(block));
    int32_t before[4]={}, after[4]={};
    assert(nativepro::applyOrigins(block,s,before,after)==1);
    for (int i=0;i<4;++i) assert(before[i]==7 && after[i]==s.origin[i]);
    for (unsigned i=0;i<sizeof(block);++i) {
        bool originByte=false;
        for (auto offset:nativepro::originOffsets) originByte |= i==offset || i==offset+1;
        if (!originByte) assert(block[i]==original[i]);
    }
    assert(nativepro::applyOrigins(block,s,before,after)==0);
    s.schema=0; std::memcpy(original,block,sizeof(block));
    assert(nativepro::applyOrigins(block,s,before,after)==-1);
    assert(std::memcmp(block,original,sizeof(block))==0);
    float x = -0.0f, y = 1.2f;
    const auto xb = std::bit_cast<uint32_t>(x), yb = std::bit_cast<uint32_t>(y);
    deadzone::apply(x, y, 0);
    assert(std::bit_cast<uint32_t>(x) == xb && std::bit_cast<uint32_t>(y) == yb);
    x = .03f; y = .04f; deadzone::apply(x, y, 10); assert(x == 0 && y == 0);
    x = .1f; y = 0; deadzone::apply(x, y, 10); assert(x == 0 && y == 0);
    x = 1; y = 0; deadzone::apply(x, y, 50); assert(x == 1 && y == 0);
    x = .2f; y = 0; float px = x, py = y;
    deadzone::apply(x, y, 10); deadzone::apply(px, py, 30);
    assert(x > .11f && x < .12f && px == 0 && py == 0);
    x = std::numeric_limits<float>::quiet_NaN(); y = .3f;
    deadzone::apply(x, y, 10); assert(std::isnan(x) && y == .3f);
    std::mt19937 rng(0x574857);
    std::uniform_real_distribution<float> input(-1.0f, 1.0f);
    for (int p = 1; p <= 50; ++p) {
        float previous = 0;
        for (int i = 0; i <= 1000; ++i) {
            float a = i / 1000.0f, b = 0;
            deadzone::apply(a, b, p);
            assert(a + 1e-6f >= previous); previous = a;
        }
        for (int i = 0; i < 2000; ++i) {
            float a = input(rng), b = input(rng), ox = a, oy = b;
            deadzone::apply(a, b, p);
            assert(std::isfinite(a) && std::isfinite(b));
            assert(a*a + b*b <= 1.00001f);
            assert(std::fabs(a*oy - b*ox) < 1e-5f); // Preserve direction.
            assert(a*ox >= 0 && b*oy >= 0);
        }
    }
    std::puts("PASS: title guards, native Pro identity/profile/signature validation, signed relocations; deadzone properties (150050 samples)");
}
