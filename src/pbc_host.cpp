// pbc_host.cpp -- fake-08's Host class for the PicoBoy Color Plus.
//
// Replaces hostCommonFunctions.cpp and the per-platform host files. All
// machine access goes through plat.h, so the PC test build uses this file
// unchanged. Save data and settings live on the FAT drive:
//   /cdata/<key>.p8d.txt   cartdata() of each cart
//   /pico8.cfg             scaling, volume (settings.c)
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include "host.h"
#include "hostVmShared.h"
#include "filehelpers.h"
#include "logger.h"
#include "pfile.h"
#include "plat.h"
#include "video.h"
#include "settings.h"

using namespace std;

// ---- file helpers ----------------------------------------------------------

static bool write_file(const std::string &path, const void *data, size_t n)
{
    pfile *f = pf_open_write(path.c_str());
    if (!f) return false;
    bool ok = pf_write(f, data, (int)n) == (int)n;
    pf_close(f);
    return ok;
}

// ---- Host ------------------------------------------------------------------

Host::Host(int windowWidth, int windowHeight)
{
    (void)windowWidth; (void)windowHeight;
    currKDown = currKHeld = 0;
    _logFilePrefix = "/";
    _cartDirectory = "";
    stretch = (StretchOption)VIDEO_240;
    loadSettingsIni();
    plat_set_scale((int)stretch);
}

void Host::setUpPaletteColors(){
    static const Color base[16] = {
        COLOR_00, COLOR_01, COLOR_02, COLOR_03, COLOR_04, COLOR_05, COLOR_06, COLOR_07,
        COLOR_08, COLOR_09, COLOR_10, COLOR_11, COLOR_12, COLOR_13, COLOR_14, COLOR_15 };
    static const Color alt[16] = {
        COLOR_128, COLOR_129, COLOR_130, COLOR_131, COLOR_132, COLOR_133, COLOR_134, COLOR_135,
        COLOR_136, COLOR_137, COLOR_138, COLOR_139, COLOR_140, COLOR_141, COLOR_142, COLOR_143 };

    uint8_t rgb[144][3];
    for (int i = 0; i < 144; i++) {
        Color c = i < 16 ? base[i] : i >= 128 ? alt[i - 128] : Color{0, 0, 0, 0};
        _paletteColors[i] = c;
        rgb[i][0] = c.Red; rgb[i][1] = c.Green; rgb[i][2] = c.Blue;
    }
    video_set_palette(rgb);
}

Color* Host::GetPaletteColors(){
    return _paletteColors;
}

void Host::oneTimeSetup(Audio* audio){
    pf_mkdir("/cdata");
    plat_audio_start(audio);
}

void Host::oneTimeCleanup(){ }

void Host::unpackCarts(){ }

static uint64_t frame_us = 1000000 / 60;
static uint64_t next_frame;

void Host::setTargetFps(int targetFps){
    if (targetFps <= 0) targetFps = 60;
    frame_us = 1000000u / (unsigned)targetFps;
}

void Host::waitForTargetFps(){
    uint64_t now = plat_time_us();
    if (next_frame == 0 || now > next_frame + frame_us * 2) {
        // first frame, or far behind: don't try to catch up
        next_frame = now + frame_us;
        return;
    }
    if (now < next_frame)
        plat_sleep_until_us(next_frame);
    next_frame += frame_us;
}

double Host::deltaTMs(){
    static uint64_t last;
    uint64_t now = plat_time_us();
    double d = last ? (double)(now - last) / 1000.0 : 0.0;
    last = now;
    return d;
}

bool Host::shouldRunMainLoop(){ return true; }
bool Host::shouldQuit(){ return false; }

void Host::changeStretch(){ }

void Host::forceStretch(StretchOption newStretch){
    int s = (int)newStretch;
    if (s < 0 || s >= VIDEO_NSCALES) s = VIDEO_240;
    stretch = (StretchOption)s;
    plat_set_scale(s);
}

// Joystick -> d-pad, A -> O (btn 4), B -> X (btn 5), centre + A -> pause.
// The centre alone does nothing, so the pause menu does not pop up by
// accident; while it is held, A belongs to the combination.
InputState_t Host::scanInput(){
    uint32_t raw = plat_buttons();
    bool centre = raw & PB_CENTER;
    uint8_t held = 0;
    if (raw & PB_LEFT)   held |= P8_KEY_LEFT;
    if (raw & PB_RIGHT)  held |= P8_KEY_RIGHT;
    if (raw & PB_UP)     held |= P8_KEY_UP;
    if (raw & PB_DOWN)   held |= P8_KEY_DOWN;
    if (raw & PB_B)      held |= P8_KEY_X;
    if (centre) {
        if (raw & PB_A)  held |= P8_KEY_PAUSE;
    } else if (raw & PB_A) {
        held |= P8_KEY_O;
    }

    currKDown = held & ~currKHeld;
    currKHeld = held;

    return InputState_t { currKDown, currKHeld, 0, 0, 0, false, "" };
}

void Host::drawFrame(uint8_t* picoFb, uint8_t* screenPaletteMap, uint8_t drawMode){
    plat_present(picoFb, screenPaletteMap, drawMode);
}

// sound is pulled by the platform (plat_audio_start), not pushed per frame
bool Host::shouldFillAudioBuff(){ return false; }
void* Host::getAudioBufferPointer(){ return nullptr; }
size_t Host::getAudioBufferSize(){ return 0; }
void Host::playFilledAudioBuffer(){ }

// ---- carts and directories ---------------------------------------------------

struct ListCtx { vector<string> *out; bool dirs; };

static void list_cb(const char *name, bool is_dir, void *ctx)
{
    ListCtx *c = (ListCtx *)ctx;
    string n = name;
    if (c->dirs) {
        if (is_dir && !isHiddenFile(n) && n != "cdata" && n != "System Volume Information")
            c->out->push_back(n);
    } else if (!is_dir && isCartFile(n)) {
        c->out->push_back(n);
    }
}

vector<string> Host::listcarts(){
    vector<string> carts;
    ListCtx c = { &carts, false };
    pf_list(_cartDirectory.empty() ? "/" : _cartDirectory.c_str(), list_cb, &c);
    return carts;
}

vector<string> Host::listdirs(){
    vector<string> dirs;
    ListCtx c = { &dirs, true };
    pf_list(_cartDirectory.empty() ? "/" : _cartDirectory.c_str(), list_cb, &c);
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

std::string Host::getCartDirectory(){ return _cartDirectory; }
void Host::setCartDirectory(std::string cartDirectory){
    if (cartDirectory == "/") cartDirectory = "";
    _cartDirectory = cartDirectory;
}

void Host::overrideLogFilePrefix(const char* newPrefix){ _logFilePrefix = newPrefix; }
const char* Host::logFilePrefix(){ return _logFilePrefix.c_str(); }
std::string Host::customBiosLua(){ return ""; }

void Host::setPlatformParams(int, int, uint32_t, uint32_t, uint32_t,
                             std::string, std::string, std::string cartDirectory){
    setCartDirectory(cartDirectory);
}

// ---- save data -----------------------------------------------------------------

std::string Host::getCartDataFile(std::string cartDataKey){
    return "/cdata/" + cartDataKey + ".p8d.txt";
}

std::string Host::getCartDataFileContents(std::string cartDataKey){
    return get_file_contents(getCartDataFile(cartDataKey));
}

void Host::saveCartData(std::string cartDataKey, std::string contents){
    // every write erases flash: skip it when nothing changed
    if (getCartDataFileContents(cartDataKey) == contents)
        return;
    write_file(getCartDataFile(cartDataKey), contents.data(), contents.size());
}

size_t Host::getFileContents(std::string fileName, char* buffer){
    std::string s = get_file_contents("/cdata/" + fileName);
    if (s.size() < sizeof(size_t)) return 0;
    size_t len;
    memcpy(&len, s.data(), sizeof len);
    if (len > s.size() - sizeof len) len = s.size() - sizeof len;
    memcpy(buffer, s.data() + sizeof len, len);
    return len;
}

void Host::writeBufferToFile(std::string fileName, char* buffer, size_t length){
    std::string s((const char *)&length, sizeof length);
    s.append(buffer, length);
    write_file("/cdata/" + fileName, s.data(), s.size());
}

// ---- settings --------------------------------------------------------------------

static pbc_settings_t g_settings;

void Host::loadSettingsIni(){
    settings_load(&g_settings);
    stretch = (StretchOption)g_settings.scale;
    menustyle = (MenuStyleOption)g_settings.menustyle;
    bgcolor = (BgColorOption)g_settings.bgcolor;
    plat_set_volume(g_settings.volume);
}

void Host::saveSettingsIni(){
    g_settings.scale = (int)stretch;
    g_settings.menustyle = (int)menustyle;
    g_settings.bgcolor = (int)bgcolor;
    settings_save(&g_settings);
}

int Host::getSetting(std::string sname){
    if (sname == "stretch") return stretch;
    if (sname == "menustyle") return menustyle;
    if (sname == "bgcolor") return bgcolor;
    if (sname == "kbmode") return kbmode;
    if (sname == "resizekey") return resizekey;
    if (sname == "p8_bgcolor") {
        switch (bgcolor) {
            case Gray: return 5;
            case Black: return 0;
            case Blue: return 1;
            case Green: return 3;
            case Purple: return 13;
            case White: return 7;
            default: return 2;
        }
    }
    if (sname == "p8_textcolor") return bgcolor == White ? 0 : 7;
    return 0;
}

void Host::setSetting(std::string sname, int sval){
    if (sname == "stretch") {
        forceStretch((StretchOption)sval);
    } else if (sname == "menustyle") {
        menustyle = (MenuStyleOption)sval;
    } else if (sname == "bgcolor") {
        bgcolor = (BgColorOption)sval;
    } else {
        return;
    }
    saveSettingsIni();
}

void Host::exitToMenu(const std::string &error){
    plat_exit_to_menu(error.c_str());
}
