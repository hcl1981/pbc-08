#pragma once

#include <string>
#include <vector>

#include "graphics.h"


struct CartRomData
{
    union
    {
        struct 
        {
            uint8_t SpriteSheetData[128 * 64];
            uint8_t MapData[128 * 32];
            uint8_t SpriteFlagsData[256];
            struct song SongData[64];
            struct sfx SfxData[64];
        };

        uint8_t data[0x4300];
    };
};

struct CartSource;

// PBC+ port: carts are parsed while they are read, line by line (.p8) or
// scanline by scanline (.p8.png), straight into CartRom. Only the Lua
// source is kept as a string, and the label only when asked for. The
// original kept the whole file plus one string per section plus a 32 KB
// buffer for the life of the cart.
class Cart {
    void initCartRom();

    bool loadCartFromPng(CartSource &src);
    bool loadCartFromText(CartSource &src);
    void decodeLua(const uint8_t *code, size_t size);

    void setSfxLine(int idx, const std::string &line);
    void setMusicLine(int idx, const std::string &line);

    bool _labelOnly;
    uint8_t *_labelIdx;     // label as colour indices (menu preview)
    bool _labelFound;
	
    public:
    Cart (std::string filename, std::string cartDirectory, bool labelOnly = false,
          uint8_t *labelIdx = nullptr);
    bool labelFound() const { return _labelFound; }
    Cart (const unsigned char* cartData, size_t size);
    ~Cart();

    std::string FullCartPath;

    std::string LuaString;

    std::string LoadError;

    std::string LabelString;

    // PBC+: on the heap, so it can be given back once the VM has copied it
    // to RAM and a cache file on the drive can serve reload() (vm.cpp)
    CartRomData *CartRom = nullptr;
    void ReleaseRom();
};

// PBC+: 128x128 label of a cart as PICO-8 colour indices 0..31 (16..31 are
// the secret colours 128..143). False if the cart has none.
bool cart_load_label(const std::string &path, uint8_t out[128 * 128]);
