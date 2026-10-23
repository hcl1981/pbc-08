#include <string>
#include <new>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <array>
#include <algorithm>

#include "miniz.h"

#include "cart.h"
#include "filehelpers.h"
#include "pfile.h"
#include "pbc_mem.h"

#include "stringToDataHelpers.h"

#include "utils.h"

#include "logger.h"

#include "emojiconversion.h"

#include "DefaultCart.h"

#include "FakoSettings.h"


static char const *legacyCompressionLut = "\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_";

// ---- input: a file on the flash drive or a block of memory ---------------

struct CartSource {
    pfile *f = nullptr;
    const uint8_t *mem = nullptr;
    size_t memSize = 0, memPos = 0;
    uint8_t buf[512];
    int bufLen = 0, bufPos = 0;

    int read(void *dst, int n) {
        if (mem) {
            size_t left = memSize - memPos;
            if ((size_t)n > left) n = (int)left;
            memcpy(dst, mem + memPos, (size_t)n);
            memPos += (size_t)n;
            return n;
        }
        int got = 0;
        uint8_t *d = (uint8_t *)dst;
        while (got < n) {
            if (bufPos == bufLen) {
                // large reads bypass the small buffer
                if (n - got >= (int)sizeof buf) {
                    int r = pf_read(f, d + got, n - got);
                    if (r <= 0) break;
                    got += r;
                    continue;
                }
                bufLen = pf_read(f, buf, sizeof buf);
                bufPos = 0;
                if (bufLen <= 0) { bufLen = 0; break; }
            }
            int k = std::min(n - got, bufLen - bufPos);
            memcpy(d + got, buf + bufPos, (size_t)k);
            bufPos += k;
            got += k;
        }
        return got;
    }

    int getc() {
        uint8_t c;
        return read(&c, 1) == 1 ? c : -1;
    }

    // false at end of input
    bool getline(std::string &line) {
        line.clear();
        int c = getc();
        if (c < 0) return false;
        while (c >= 0 && c != '\n') {
            line += (char)c;
            c = getc();
        }
        return true;
    }
};

//https://github.com/samhocevar/zepto8/blob/b1a13516945c49e47495c739e6a43a241ad99291/src/pico8/code.cpp
// Move to front structure
struct move_to_front
{
    move_to_front()
    {
        for (int n = 0; n < 256; ++n)
            state[n] = uint8_t(n);
    }

    // Get the nth byte and move it to front
    uint8_t get(int n)
    {
        std::rotate(state.begin(), state.begin() + n, state.begin() + n + 1);
        return state.front();
    }

private:
    std::array<uint8_t, 256> state;
};

//implementation from zepto8 code.cpp, pxa_decompress
static std::string pxa_decompress(uint8_t const *input, size_t inputSize)
{
    size_t length = input[4] * 256 + input[5];
    size_t compressed = input[6] * 256 + input[7];
    if (compressed > inputSize) compressed = inputSize;

    size_t pos = size_t(8) * 8; // stream position in bits
    auto get_bits = [&](size_t count) -> uint32_t
    {
        uint32_t n = 0;
        for (size_t i = 0; i < count && pos < compressed * 8; ++i, ++pos)
            n |= ((input[pos >> 3] >> (pos & 0x7)) & 0x1) << i;
        return n;
    };

    move_to_front mtf;
    std::string ret;
    ret.reserve(length);

    while (ret.size() < length && pos < compressed * 8)
    {
        if (get_bits(1))
        {
            int nbits = 4;
            while (get_bits(1))
                ++nbits;
            int n = get_bits(nbits) + (1 << nbits) - 16;
            uint8_t ch = mtf.get(n);
            if (!ch)
                break;
            ret.push_back(char(ch));
        }
        else
        {
            int nbits = get_bits(1) ? get_bits(1) ? 5 : 10 : 15;
            int offset = get_bits(nbits) + 1;

            if (nbits == 10 && offset == 1)
            {
                uint8_t ch = get_bits(8);
                while (ch)
                {
                    ret.push_back(char(ch));
                    ch = get_bits(8);
                }
            }
            else
            {
                int n, len = 3;
                do
                    len += (n = get_bits(3));
                while (n == 7);

                if (offset > (int)ret.size())
                    break;
                for (int i = 0; i < len; ++i)
                    ret.push_back(ret[ret.size() - offset]);
            }
        }
    }

    return ret;
}

// ---- Lua section of a .p8.png (0x4300..0x7fff), three encodings ---------

void Cart::decodeLua(const uint8_t *code, size_t size) {
    if (size >= 4 && code[0] == '\0' && code[1] == 'p' && code[2] == 'x' && code[3] == 'a') {
        LuaString = pxa_decompress(code, size);
    }
    else if (size >= 4 && code[0] == ':' && code[1] == 'c' && code[2] == ':' && code[3] == '\0') {
        //from pico 8 wiki: https://pico-8.fandom.com/wiki/P8PNGFileFormat
        size_t length = code[4] * 256 + code[5];

        LuaString.clear();
        LuaString.reserve(length);

        for (size_t i = 8; i < size && LuaString.length() < length; ++i){
            //0x00: Copy the next byte directly to the output stream. 
            if(code[i] == 0x00){
                if (i + 1 < size) LuaString += code[++i];
            }
            //0x01-0x3b: Emit a character from a lookup table
            else if (code[i] < 0x3c){
                LuaString += legacyCompressionLut[code[i] - 1];
            }
            //0x3c-0xff: copy from what has already been emitted
            else if (i + 1 < size) {
                size_t offset = (code[i] - 0x3c) * 16 + (code[i + 1] & 0xf);
                size_t len = (code[i + 1] >> 4) + 2;

                int startIndex = (int)LuaString.length() - (int)offset;
                if (startIndex > -1) {
                    for(size_t j = 0; j < len; ++j) {
                        LuaString += LuaString[startIndex + j];
                    }
                }

                ++i;
            }
        }
    }
    else {
        const uint8_t *end = (const uint8_t *)memchr(code, '\0', size);
        LuaString.assign((const char *)code, end ? (size_t)(end - code) : size);
    }
}

// ---- .p8.png: streamed through tinfl, one scanline at a time ------------
//
// A cart image is 160x205 RGBA, 8 bit, not interlaced. Every pixel carries
// one byte in the two low bits of each channel (ARGB order), 0x8000 bytes
// plus a version byte. The label is the picture at (16,24)-(143,151).

#define PNG_W 160
#define PNG_H 205
#define PNG_STRIDE (PNG_W * 4)

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint8_t paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}

// the 16 standard colours and the 16 secret ones (128..143)
static const uint8_t pico_pal32[32][3] = {
    {0,0,0},{29,43,83},{126,37,83},{0,135,81},{171,82,54},{95,87,79},
    {194,195,199},{255,241,232},{255,0,77},{255,163,0},{255,236,39},
    {0,228,54},{41,173,255},{131,118,156},{255,119,168},{255,204,170},
    {41,24,20},{17,29,53},{66,33,54},{18,83,89},{116,47,41},{73,51,59},
    {162,136,121},{243,239,125},{190,18,80},{255,108,36},{168,231,46},
    {0,181,67},{6,90,181},{117,70,101},{255,110,89},{255,157,129}};

// nearest palette entry (0..31); the low two bits of every channel carry
// cart data, so the picture colours are never exact
static int label_index(uint8_t r, uint8_t g, uint8_t b, int ncolours) {
    int best = 0, bestd = 1 << 30;
    for (int i = 0; i < ncolours; i++) {
        int dr = r - pico_pal32[i][0], dg = g - pico_pal32[i][1], db = b - pico_pal32[i][2];
        int d = dr * dr + dg * dg + db * db;
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

// nearest of the 16 standard colours, as a label hex digit
static char label_digit(uint8_t r, uint8_t g, uint8_t b) {
    return "0123456789abcdef"[label_index(r, g, b, 16)];
}

struct PngState {
    tinfl_decompressor inf;
    uint8_t dict[TINFL_LZ_DICT_SIZE];
    uint8_t prev[PNG_STRIDE], cur[PNG_STRIDE];
    uint8_t filter;
    int col;            // -1: next byte is the filter byte
    int row;
    uint8_t *lua;       // 0x3d00 bytes
    CartRomData *rom;
    std::string *label;
    uint8_t *labelIdx;  // 128x128 colour indices 0..31, or null
};

static void png_row_done(PngState &s) {
    uint8_t *c = s.cur, *p = s.prev;
    for (int i = 0; i < PNG_STRIDE; i++) {
        int a = i >= 4 ? c[i - 4] : 0, b = p[i], d = i >= 4 ? p[i - 4] : 0;
        switch (s.filter) {
            case 1: c[i] += a; break;
            case 2: c[i] += b; break;
            case 3: c[i] += (uint8_t)((a + b) >> 1); break;
            case 4: c[i] += paeth(a, b, d); break;
            default: break;
        }
    }
    int y = s.row;
    for (int x = 0; x < PNG_W; x++) {
        const uint8_t *px = c + x * 4;
        uint8_t v = (uint8_t)((px[3] & 3) << 6 | (px[0] & 3) << 4 | (px[1] & 3) << 2 | (px[2] & 3));
        int idx = y * PNG_W + x;
        if (s.rom) {
            if (idx < 0x4300) s.rom->data[idx] = v;
            else if (idx < 0x8000) s.lua[idx - 0x4300] = v;
        }
        if (y >= 24 && y < 152 && x >= 16 && x < 144) {
            if (s.label) {
                *s.label += label_digit(px[0], px[1], px[2]);
                if (x == 143) *s.label += '\n';
            }
            if (s.labelIdx)
                s.labelIdx[(y - 24) * 128 + x - 16] = (uint8_t)label_index(px[0], px[1], px[2], 32);
        }
    }
    memcpy(s.prev, s.cur, PNG_STRIDE);
    s.row++;
}

static void png_consume(PngState &s, const uint8_t *d, size_t n) {
    while (n && s.row < PNG_H) {
        if (s.col < 0) {
            s.filter = *d++;
            n--;
            s.col = 0;
            continue;
        }
        size_t k = std::min(n, (size_t)(PNG_STRIDE - s.col));
        memcpy(s.cur + s.col, d, k);
        s.col += (int)k;
        d += k;
        n -= k;
        if (s.col == PNG_STRIDE) {
            png_row_done(s);
            s.col = -1;
        }
    }
}

bool Cart::loadCartFromPng(CartSource &src) {
    uint8_t hdr[8];
    if (src.read(hdr, 8) != 8 || memcmp(hdr, "\x89PNG\r\n\x1a\n", 8) != 0) {
        LoadError = "not a png file";
        return false;
    }

    PngState *s = (PngState *)pbc_malloc(sizeof(PngState));
    uint8_t *lua = _labelOnly ? nullptr : (uint8_t *)pbc_malloc(0x3d00);
    uint8_t *in = (uint8_t *)pbc_malloc(1024);
    if (lua) memset(lua, 0, 0x3d00);
    if (!s || (!_labelOnly && !lua) || !in) {
        pbc_free(s); pbc_free(lua); pbc_free(in);
        LoadError = "out of memory loading png";
        return false;
    }
    tinfl_init(&s->inf);
    memset(s->prev, 0, sizeof s->prev);
    s->col = -1;
    s->row = 0;
    s->lua = lua;
    s->rom = _labelOnly ? nullptr : CartRom;
    s->label = _labelOnly && !_labelIdx ? &LabelString : nullptr;
    s->labelIdx = _labelIdx;
    if (s->label) LabelString.reserve(128 * 129);

    size_t dictOfs = 0;
    bool ok = false, sawHeader = false, done = false;

    while (!done) {
        uint8_t ch[8];
        if (src.read(ch, 8) != 8) break;
        uint32_t len = be32(ch);
        if (memcmp(ch + 4, "IHDR", 4) == 0) {
            uint8_t ih[13];
            if (len != 13 || src.read(ih, 13) != 13) break;
            if (be32(ih) != PNG_W || be32(ih + 4) != PNG_H) {
                LoadError = "Invalid png dimensions";
                break;
            }
            if (ih[8] != 8 || ih[9] != 6 || ih[12] != 0) {
                LoadError = "png is not 8 bit RGBA";
                break;
            }
            sawHeader = true;
            len = 0;
        }
        else if (memcmp(ch + 4, "IDAT", 4) == 0 && sawHeader) {
            while (len > 0 && !done) {
                int want = (int)std::min<uint32_t>(len, 1024);
                int got = src.read(in, want);
                if (got <= 0) { len = 0; done = true; break; }
                len -= (uint32_t)got;
                const uint8_t *ip = in;
                size_t inLeft = (size_t)got;
                for (;;) {
                    size_t inBytes = inLeft, outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
                    tinfl_status st = tinfl_decompress(&s->inf, ip, &inBytes, s->dict,
                        s->dict + dictOfs, &outBytes,
                        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT);
                    ip += inBytes;
                    inLeft -= inBytes;
                    png_consume(*s, s->dict + dictOfs, outBytes);
                    dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
                    if (s->row >= PNG_H || st == TINFL_STATUS_DONE) { done = true; break; }
                    if (_labelOnly && s->row >= 152) { done = true; break; }
                    if (st < 0) { LoadError = "png inflate error"; done = true; break; }
                    if (st == TINFL_STATUS_NEEDS_MORE_INPUT && inLeft == 0) break;
                }
            }
        }
        else if (memcmp(ch + 4, "IEND", 4) == 0) {
            break;
        }
        // skip the rest of the chunk and its CRC
        uint8_t skip[64];
        uint32_t rest = len + 4;
        if (done) rest = 0;
        while (rest > 0) {
            int k = src.read(skip, (int)std::min<uint32_t>(rest, sizeof skip));
            if (k <= 0) { done = true; break; }
            rest -= (uint32_t)k;
        }
    }

    ok = s->row >= (_labelOnly ? 152 : PNG_H);
    if (ok && _labelIdx) _labelFound = true;
    if (!ok && LoadError.empty()) LoadError = "png cart is truncated";
    if (ok && !_labelOnly) decodeLua(lua, 0x3d00);

    pbc_free(in);
    pbc_free(lua);
    pbc_free(s);
    return ok;
}

// ---- .p8 text: one line at a time ---------------------------------------

static inline uint8_t hexval(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
}

// "#include file" at the start of a lua line (spaces allowed before)
static bool parseInclude(const std::string &line, std::string &file) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    if (line.compare(i, 8, "#include") != 0) return false;
    i += 8;
    size_t j = i;
    while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) j++;
    if (j == i) return false;
    size_t k = j;
    while (k < line.size() && line[k] > ' ') k++;
    if (k == j) return false;
    file = line.substr(j, k - j);
    return true;
}

bool Cart::loadCartFromText(CartSource &src) {
    enum { S_NONE, S_LUA, S_GFX, S_GFF, S_MAP, S_SFX, S_MUSIC, S_LABEL, S_OTHER } sec = S_NONE;
    std::string line, inc;
    int row = 0;
    size_t gffPos = 0, mapPos = 0;

    if (!_labelOnly) {
        // SFX speed defaults to 16. Everything else should be zeroed out
        for (int i = 0; i < 64; i++) {
            CartRom->SfxData[i].speed = 16;
        }
    }

    while (src.getline(line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        if (line.length() > 2 && line[0] == '_' && line[1] == '_') {
            row = 0;
            if (line == "__lua__") sec = S_LUA;
            else if (line == "__gfx__") sec = S_GFX;
            else if (line == "__gff__") sec = S_GFF;
            else if (line == "__map__") sec = S_MAP;
            else if (line == "__sfx__") sec = S_SFX;
            else if (line == "__music__") sec = S_MUSIC;
            else if (line == "__label__") sec = S_LABEL;
            else sec = S_OTHER;
            continue;
        }

        if (_labelOnly) {
            if (sec == S_LABEL) {
                if (_labelIdx) {
                    if (row < 128) {
                        for (size_t n = 0; n < line.size() && n < 128; n++) {
                            char c = line[n];
                            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'v' ? c - 'a' + 10 : 0;
                            _labelIdx[row * 128 + n] = (uint8_t)v;
                        }
                    }
                    row++;
                    _labelFound = true;
                } else {
                    LabelString += line + "\n";
                }
            }
            continue;
        }

        switch (sec) {
        case S_LUA:
            line = charset::utf8_to_pico8(line);
            if (parseInclude(line, inc)) {
                auto fullPath = getDirectory(FullCartPath) + "/" + inc;
                auto includeContents = get_file_contents(fullPath);
                if (includeContents.length() > 0){
                    LuaString += charset::utf8_to_pico8(includeContents) + "\n";
                }
                else{
                    LoadError = "Can't find included file " + inc;
                    return false;
                }
            }
            else {
                LuaString += line;
                LuaString += '\n';
            }
            break;
        case S_GFX:
            // left pixel is the low nibble
            if (row < 128) {
                uint8_t *dst = &CartRom->SpriteSheetData[row * 64];
                for (size_t n = 0; n + 1 < line.size() && n < 128; n += 2)
                    dst[n / 2] = (uint8_t)(hexval(line[n]) | hexval(line[n + 1]) << 4);
            }
            row++;
            break;
        case S_GFF:
            for (size_t n = 0; n + 1 < line.size() && gffPos < sizeof CartRom->SpriteFlagsData; n += 2)
                CartRom->SpriteFlagsData[gffPos++] = (uint8_t)(hexval(line[n]) << 4 | hexval(line[n + 1]));
            break;
        case S_MAP:
            for (size_t n = 0; n + 1 < line.size() && mapPos < sizeof CartRom->MapData; n += 2)
                CartRom->MapData[mapPos++] = (uint8_t)(hexval(line[n]) << 4 | hexval(line[n + 1]));
            break;
        case S_SFX:
            if (row < 64 && line.size() >= 168) setSfxLine(row, line);
            row++;
            break;
        case S_MUSIC:
            if (row < 64 && line.size() >= 11) setMusicLine(row, line);
            row++;
            break;
        default:
            break;
        }
    }

    return true;
}

Cart::Cart (const unsigned char* cartData, size_t size){
    _labelOnly = false;
    _labelIdx = nullptr;
    _labelFound = false;
    initCartRom();
    if (size < 5) {
        LoadError = "Invalid cart. Less than 5 bytes";
        return;
    }

    CartSource src;
    src.mem = cartData;
    src.memSize = size;

    if (cartData[0] == 0x89 && cartData[1] == 'P' && cartData[2] == 'N' && cartData[3] == 'G') {
        if (!loadCartFromPng(src)) return;
        LoadError = "";
    }
    else if (memcmp(cartData, "pico", 4) == 0) {
        if (!loadCartFromText(src)) return;
        LoadError = "";
    }
    else {
        LoadError = "unknown cart file format";
    }
}

//tac08 based cart parsing and stripping of emoji
Cart::Cart(std::string filename, std::string cartDirectory, bool labelOnly, uint8_t *labelIdx){
    _labelOnly = labelOnly || labelIdx;
    _labelIdx = labelIdx;
    _labelFound = false;
    //the leading # indicates it is the BBS key
    if (filename.length() > 0 && filename[0] == '#') {
        filename = filename.substr(1);
    }

    std::string fileExtension = getFileExtension(filename);

    if (fileExtension == "") {
        filename = filename + ".p8";
    }

    // Check for built-in carts before prepending cart directory
    bool isDefaultCart = (filename == "__FAKE08-DEFAULT.p8");
    bool isSettingsCart = (filename == "__FAKE08-SETTINGS.p8");

    if (isDefaultCart || isSettingsCart) {
        FullCartPath = filename;
    }
    else if (cartDirectory.length() > 0 && ! isAbsolutePath(filename)) {
        FullCartPath = cartDirectory + "/" + filename;
    }
    else {
        FullCartPath = filename;
    }
    //zero out cart rom so no garbage is left over. A label needs none:
    // the menu shows previews next to its two big frame buffers
    if (!_labelOnly) initCartRom();

    if (isDefaultCart || isSettingsCart) {
        const char *text = isDefaultCart ? fake08DefaultCart : fake08SettingsP8;
        CartSource src;
        src.mem = (const uint8_t *)text;
        src.memSize = strlen(text);
        loadCartFromText(src);
        return;
    }

    Logger_Write("getting file contents for %s\n", FullCartPath.c_str());

    pfile *f = pf_open_read(FullCartPath.c_str());
    if (!f && (fileExtension == "" || fileExtension == ".p8")) {
        //fallback for cart loading by key - support .p8 and .p8.png
        FullCartPath += ".png";
        f = pf_open_read(FullCartPath.c_str());
    }
    if (!f) {
        LoadError = "cart file not found: " + FullCartPath;
        Logger_Write("%s\n", LoadError.c_str());
        return;
    }

    CartSource src;
    src.f = f;
    uint8_t first[4] = {0};
    int got = src.read(first, 4);
    // rewind: reopen is cheaper than seeking through the FatFs buffer logic
    pf_close(f);
    f = pf_open_read(FullCartPath.c_str());
    src = CartSource();
    src.f = f;

    if (got == 4 && memcmp(first, "pico", 4) == 0) {
        loadCartFromText(src);
    }
    else if (got == 4 && first[0] == 0x89 && first[1] == 'P' && first[2] == 'N' && first[3] == 'G') {
        if (loadCartFromPng(src)) {
            LoadError = "";
        }
    }
    else {
        LoadError = "unknown cart file format: " + FullCartPath;
    }
    pf_close(f);
}

Cart::~Cart(){
    delete CartRom;
}

void Cart::ReleaseRom(){
    delete CartRom;
    CartRom = nullptr;
}

void Cart::initCartRom(){
    if (!CartRom) CartRom = new CartRomData;
    memset(CartRom, 0, sizeof *CartRom);
}

void Cart::setMusicLine(int musicIdx, const std::string &line){
    auto hex2 = [&](size_t i) { return (uint8_t)(hexval(line[i]) << 4 | hexval(line[i + 1])); };

    uint8_t flagByte = hex2(0);

    uint8_t mode = (flagByte & 8) >> 3;
    uint8_t fstop = (flagByte & 4) >> 2;
    uint8_t frepeat = (flagByte & 2) >> 1;
    uint8_t fnext = flagByte & 1;

    CartRom->SongData[musicIdx].data[0] = hex2(3) | fnext << 7;
    CartRom->SongData[musicIdx].data[1] = hex2(5) | frepeat << 7;
    CartRom->SongData[musicIdx].data[2] = hex2(7) | fstop << 7;
    CartRom->SongData[musicIdx].data[3] = hex2(9) | mode << 7;
}

void Cart::setSfxLine(int sfxIdx, const std::string &line) {
    auto hex2 = [&](size_t i) { return (uint8_t)(hexval(line[i]) << 4 | hexval(line[i + 1])); };

    CartRom->SfxData[sfxIdx].filters = hex2(0);
    CartRom->SfxData[sfxIdx].speed = hex2(2);
    CartRom->SfxData[sfxIdx].loopRangeStart = hex2(4);
    CartRom->SfxData[sfxIdx].loopRangeEnd = hex2(6);

    //32 notes, 5 chars each
    int noteIdx = 0;
    for (int i = 8; i < 168; i+=5) {
        uint8_t key = hex2(i);
        uint8_t waveform = hexval(line[i + 2]);
        uint8_t custom = waveform > 7 ? 1 : 0;
        uint8_t volume = hexval(line[i + 3]);
        uint8_t effect = hexval(line[i + 4]);

        CartRom->SfxData[sfxIdx].notes[noteIdx].setKey(key);
        CartRom->SfxData[sfxIdx].notes[noteIdx].setWaveform(waveform);
        CartRom->SfxData[sfxIdx].notes[noteIdx].setVolume(volume);
        CartRom->SfxData[sfxIdx].notes[noteIdx].setEffect(effect);
        CartRom->SfxData[sfxIdx].notes[noteIdx].setCustom(custom);

        noteIdx++;
    }
}

// PBC+: the menu's cart preview
bool cart_load_label(const std::string &path, uint8_t out[128 * 128]) {
    memset(out, 0, 128 * 128);
    // on the heap: a Cart holds the 17 KB cart ROM, and the menu runs on
    // core 0's 4 KB stack
    Cart *c = new (std::nothrow) Cart(path, "", true, out);
    if (!c) return false;
    // a .p8.png always has a picture; a .p8 only with a __label__ section
    bool ok = c->LoadError.empty() && c->labelFound();
    delete c;
    return ok;
}
