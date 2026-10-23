#include <string>
#include <functional>
#include <chrono>
#include <math.h>
#include <setjmp.h>
#include <algorithm>

#include <string.h>
#include <ctime>

#include "vm.h"
#include "graphics.h"
#include "fontdata.h"
#include "cart.h"
#include "stringToDataHelpers.h"
#include "picoluaapi.h"
#include "logger.h"
#include "Input.h"
#include "p8GlobalLuaFunctions.h"
#include "hostVmShared.h"
#include "emojiconversion.h"
#include "filehelpers.h"
#include "pfile.h"

#include "NoLabel.h"

//extern "C" {
  #include <lua.h>
  #include <lualib.h>
  #include <lauxlib.h>
  #include <fix32.h>
//}

using namespace z8;

lua_State *pbc_vm_lua; // PBC+: for the out-of-memory handler

// the clock seeds the RNG; the PC test can fix it to compare runs
static int32_t boot_seed(long long clock) {
#ifdef PBC_LOG_STDOUT
    if (getenv("PBC_FIXED_SEED")) return 12345;
#endif
    return (int32_t)clock;
}

static const char DefaultCartName[] = "__FAKE08-DEFAULT.p8";
static const char SettingsCartName[] = "__FAKE08-SETTINGS.p8";

// PBC+: __z8_load_cart(glue, env) compiles the loaded cart's source
// followed by the glue code, reading both from C++ memory.
static Vm *s_vmForLoad;

struct CartChunks { const char *p[2]; size_t n[2]; int i; };

static const char *cart_reader(lua_State *L, void *ud, size_t *size) {
    (void)L;
    CartChunks *c = (CartChunks *)ud;
    while (c->i < 2) {
        int k = c->i++;
        if (c->n[k]) { *size = c->n[k]; return c->p[k]; }
    }
    *size = 0;
    return nullptr;
}

static int load_cart_code(lua_State *L) {
    size_t glen = 0;
    const char *glue = luaL_optlstring(L, 1, "", &glen);
    const std::string &src = s_vmForLoad->GetLoadedLua();
    CartChunks c = { { src.data(), glue }, { src.size(), glen }, 0 };
    int st = lua_load(L, cart_reader, &c, "=cart", "t");
    // PBC+: the source is not needed any more: give the RAM to the game.
    // run() reads it from the cart file again (GetLoadedLua).
    if (st == LUA_OK)
        s_vmForLoad->ReleaseLoadedLua();
    if (st != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (lua_istable(L, 2)) {
        lua_pushvalue(L, 2);
        lua_setupvalue(L, -2, 1);
    }
    return 1;
}

bool _initializeLuaState(lua_State* luaState) {
    // initialize Lua interpreter
    
    // load Lua base libraries (print / math / etc)
    luaL_openlibs(luaState);
    lua_pushglobaltable(luaState);

    //system
    //must be registered before loading globals for pause menu to work
    lua_register(luaState, "__listcarts", listcarts);
    lua_register(luaState, "__listdirs", listdirs);
    lua_register(luaState, "__cd", cd);
    lua_register(luaState, "__pwd", pwd);
    lua_register(luaState, "__getbioserror", getbioserror);
    lua_register(luaState, "__loaddefaultcart", loaddefaultcart);
    lua_register(luaState, "__loadsettingscart", loadsettingscart);
    lua_register(luaState, "__togglepausemenu", togglepausemenu);
    lua_register(luaState, "__ispaused", ispaused);
    lua_register(luaState, "__resetcart", resetcart);
    lua_register(luaState, "__load", load);
	
    //settings
    lua_register(luaState, "__getsetting", getsetting);
    lua_register(luaState, "__setsetting", setsetting);
    
    lua_register(luaState, "__installpackins", installpackins);
    
    //label
    lua_register(luaState, "__loadlabel", loadlabel);
    
    lua_register(luaState, "__getlualine", getlualine);
    lua_register(luaState, "__z8_load_cart", load_cart_code);
    
    //register global functions first, they will get local aliases when
    //the rest of the api is registered
    //graphics
    lua_register(luaState, "cls", cls);
    lua_register(luaState, "pset", pset);
    lua_register(luaState, "pget", pget);
    lua_register(luaState, "color", color);
    lua_register(luaState, "line", line);
    lua_register(luaState, "tline", tline);
    lua_register(luaState, "circ", circ);
    lua_register(luaState, "circfill", circfill);
    lua_register(luaState, "oval", oval);
    lua_register(luaState, "ovalfill", ovalfill);
    lua_register(luaState, "rect", rect);
    lua_register(luaState, "rectfill", rectfill);
    lua_register(luaState, "rrect", rrect);
    lua_register(luaState, "rrectfill", rrectfill);
    lua_register(luaState, "print", print);
    lua_register(luaState, "cursor", cursor);
    lua_register(luaState, "spr", spr);
    lua_register(luaState, "sspr", sspr);
    lua_register(luaState, "fget", fget);
    lua_register(luaState, "fset", fset);
    lua_register(luaState, "sget", sget);
    lua_register(luaState, "sset", sset);
    lua_register(luaState, "camera", camera);
    lua_register(luaState, "clip", clip);

    lua_register(luaState, "pal", pal);
    lua_register(luaState, "palt", palt);

    lua_register(luaState, "mget", mget);
    lua_register(luaState, "mset", mset);
    lua_register(luaState, "map", gfx_map);
    lua_register(luaState, "mapdraw", gfx_map);

    //stubbed in graphics:
    lua_register(luaState, "fillp", fillp);

    //input
    lua_register(luaState, "btn", btn);
    lua_register(luaState, "btnp", btnp);

    lua_register(luaState, "time", time);
    lua_register(luaState, "t", time);

    //audio:
    lua_register(luaState, "music", music);
    lua_register(luaState, "sfx", sfx);

    //memory
    lua_register(luaState, "cstore", cstore);
    lua_register(luaState, "memcpy", api_memcpy);
    lua_register(luaState, "memset", api_memset);
    lua_register(luaState, "peek", peek);
    lua_register(luaState, "poke", poke);
    lua_register(luaState, "peek2", peek2);
    lua_register(luaState, "poke2", poke2);
    lua_register(luaState, "peek4", peek4);
    lua_register(luaState, "poke4", poke4); 
    lua_register(luaState, "reload", reload);
    lua_register(luaState, "reset", reset);

    //cart data
    // lua_register(luaState, "cartdata", cartdata);
    lua_register(luaState, "__cartdata", cartdata);
    lua_register(luaState, "dget", dget);
    lua_register(luaState, "dset", dset);

    //
    lua_register(luaState, "printh", printh);
    lua_register(luaState, "stat", stat);
    lua_register(luaState, "_update_buttons", _update_buttons);
    lua_register(luaState, "run", run);
    lua_register(luaState, "extcmd", extcmd);
    lua_register(luaState, "_set_fps", setFps);

    //rng
    lua_register(luaState, "rnd", rnd);
    lua_register(luaState, "srand", srand);

    //load in global lua fuctions for pico 8- part of this is setting a local variable
    //with the same name as all the globals we just registered
    //auto convertedGlobalLuaFunctions = convert_emojis(p8GlobalLuaFunctions);
    
    auto convertedP8Bios = charset::utf8_to_pico8(p8Bios);
    
    int loadedBiosResult = luaL_dostring(luaState, convertedP8Bios.c_str());

    if (loadedBiosResult != LUA_OK) {
        Logger_Write("ERROR loading pico 8 bios\n");
        Logger_Write("Error: %s\n", lua_tostring(luaState, -1));
        lua_pop(luaState, 1);

        return false;
    }

    // Push the eris.init_persist_all function on the top of the lua stack (or nil if it doesn't exist)
    // we call this function to establish the default global state of things not to save in the save state
    // needs to be called after globals are loaded but before the cart is run, or _init is called
    //TODO: move these calls to the glue code?
    // lua_getglobal(luaState, "eris");
	// lua_getfield(luaState, -1, "init_persist_all");

    // if (lua_pcall(luaState, 0, 0, 0)){
    //     Logger_Write("Error setting up lua persistence: %s\n", lua_tostring(luaState, -1));
    //     lua_pop(luaState, 1);
    //     return false;
    // }

    // //pop the eris.init_persist_all fuction off the stack now that we're done with it
    // lua_pop(luaState, 1);


    return true;
}

int panic_hook(lua_State* l) {
    char const *message = lua_tostring(l, -1);
    printf("Lua panic: %s\n", message);
    Logger_Write("ERROR: Lua panic: %s\n", message);
    return 0;
}

void printLuaStack(lua_State* L) {
    int top = lua_gettop(L);

    std::string str = "From top to bottom, the lua stack is \n";
    for (unsigned index = top; index > 0; index--)
    {
        int type = lua_type(L, index);
        switch (type)
        {
            // booleans
            case LUA_TBOOLEAN:
                str = str + (lua_toboolean(L, index) ? "true" : "false") + "\n";
                break;

            // numbers
            case LUA_TNUMBER:
                str = str + std::to_string(lua_tonumber(L, index)) + "\n";
                break;

        // strings
            case LUA_TSTRING:
                str = str + lua_tostring(L, index) + "\n";
                break;

            // other
            default:
                str = str + lua_typename(L, type) + "\n";
                break;
        }
    }

    str = str + "\n";
    Logger_Write("%s", str.c_str());
}


Vm::Vm(
    Host* host,
    PicoRam* memory,
    Graphics* graphics,
    Input* input,
    Audio* audio) :
        _loadedCart(nullptr),
        _luaState(nullptr),
        _cleanupDeps(false),
        //_targetFps(30),
        _picoFrameCount(0),
        _cartChangeQueued(false),
        _nextCartKey(""),
        _cartLoadError(""),
        _cartdataKeyCount(0),
        _currentCartdataKey("")
{
    _host = host;
    s_vmForLoad = this;

    if (memory == nullptr) {
        memory = new PicoRam();
        _cleanupDeps = true;
    }
    _memory = memory;

    _pauseMenu = false;
    _clearInputOnResume = false;
    memset(_drawStateCopy, 0, sizeof(drawState_t));
    
    if (graphics == nullptr) {
        graphics = new Graphics(get_font_data(), _memory);
        _cleanupDeps = true;
    }
    _graphics = graphics;

    if (input == nullptr){
        input = new Input(memory);
        _cleanupDeps = true;
    }
    _input = input;

    if (audio == nullptr) {
        audio = new Audio(_memory);
        _cleanupDeps = true;
    }
    _audio = audio;

    //this can probably go away when I'm loading actual carts and just have to expose api to lua
    Logger_Write("Initializing global api\n");
    initPicoApi(_memory, _graphics, _input, this, _audio);
    //initGlobalApi(_graphics);

    //reset memory (may have to be more selective about zeroing out to be accurate?)
    _memory->Reset();

    //seed rng
    auto now = std::chrono::high_resolution_clock::now();
    api_srand(fix32::frombits(boot_seed(now.time_since_epoch().count())));

    //set graphics state
    _graphics->color();
    _graphics->clip();
    _graphics->pal();

    //reset audio
    _audio->resetAudioState();

    _luaState = luaL_newstate();
    lua_atpanic(_luaState, panic_hook);
    // PBC+: collect early; the heap is small and refused allocations
    // (see pbc_lua_budget) force a full collection anyway
    lua_gc(_luaState, LUA_GCSETPAUSE, 130);
    pbc_vm_lua = _luaState;

    lua_setpico8memory(_luaState, (uint8_t *)&_memory->data);
    _initializeLuaState(_luaState);
}

Vm::~Vm(){
    CloseCart();

    if (_luaState != nullptr) {
        Logger_Write("closing lua state\n");
    
        lua_close(_luaState);
        _luaState = nullptr;
        Logger_Write("closed lua state\n");
    }
    else {
        Logger_Write("lua state was null\n");
    }


    if (_cleanupDeps){
        if (_input != nullptr) {
            delete _input;
        }
        if (_graphics != nullptr) {
            delete _graphics;
        }
        if (_audio != nullptr) {
            delete _audio;
        }
        if (_memory != nullptr) {
            delete _memory;
        }
    }
}

PicoRam* Vm::getPicoRam(){
    return _memory;
}

jmp_buf place;
bool abortLua;


bool Vm::loadCart(Cart* cart) {
    _picoFrameCount = 0;

    _cartdataKeyCount = 0;
    _currentCartdataKey = "";

    //reset memory (may have to be more selective about zeroing out to be accurate?)
    _memory->Reset();

    //seed rng
    auto now = std::chrono::high_resolution_clock::now();
    api_srand(fix32::frombits(boot_seed(now.time_since_epoch().count())));

    //set graphics state
    _graphics->color();
    _graphics->clip();
    _graphics->pal();

    if (cart->LuaString == "") {
        if (_cartLoadError == "") {
            _cartLoadError = "No Lua to load. Aborting cart load";
        }
        Logger_Write("%s\n", _cartLoadError.c_str());
        
        // Clear cart change queue to prevent infinite loop
        _cartChangeQueued = false;

        return false;
    }

    //reset audio
    _audio->resetAudioState();

    //copy data from cart rom to ram
    vm_reload(0, 0, sizeof(CartRomData), cart);

    // PBC+: the ROM copy (17 KB) moves to a cache file on the drive
    cacheCartRom(cart);

    _loadedCart = cart;
    _luaReleased = false;
    _cartChangeQueued = false;
    abortLua = false;


    //customize bios per host's requirements
    if (cart->FullCartPath == DefaultCartName || cart->FullCartPath == SettingsCartName) {
        std::string customBiosLua = _host->customBiosLua();

        if (customBiosLua.length() > 0) {
            int doStrRes = luaL_dostring(_luaState, customBiosLua.c_str());

            if (doStrRes != LUA_OK){
                //bad lua passed
                Logger_Write("Error: %s\n", lua_tostring(_luaState, -1));
                lua_pop(_luaState, 1);
            }
        }
    }

    //move this to cart glue code?
    if (_cartBreadcrumb.length() > 0) {
        ExecuteLua("__addbreadcrumb(\"" + _cartBreadcrumb +"\", \"" + _prevCartKey +"\")", "");
    }

    // Only clear error if we successfully loaded a non-default cart
    // (preserve error when loading default cart to display it)
    if (cart->FullCartPath != DefaultCartName) {
        _cartLoadError = "";
    }

    return true;
}

bool Vm::LoadBiosCart(){
    CloseCart();
#ifdef PBC_NATIVE_MENU
    // PBC+: the cart menu is native code outside the VM; CloseCart() has
    // written the cartdata
    _host->exitToMenu(_cartLoadError);
#endif
    Cart *cart = new Cart(DefaultCartName, "");

    bool success = loadCart(cart);

    if (!success) {
        CloseCart();
    }
    return success;
}

void Vm::LoadSettingsCart(){
    CloseCart();
#ifdef PBC_NATIVE_MENU
    _host->exitToMenu("");
#endif

    Cart *cart = new Cart(SettingsCartName, "");

    bool success = loadCart(cart);

    if (!success) {
        CloseCart();
    }
}

bool Vm::LoadCart(std::string filename, bool loadBiosOnFail){
#ifdef PBC_NATIVE_MENU
    if (filename == DefaultCartName) {
        LoadBiosCart();
        return false;
    }
#endif
    if (_luaState) lua_gc(_luaState, LUA_GCCOLLECT, 0);
    // if (filename == "__FAKE08-DEFAULT.p8") {
    //     LoadBiosCart();
    //     return;
    // }
    if (filename == "__FAKE08-SETTINGS.p8") {
        LoadSettingsCart();
        return true;
    }
    Logger_Write("Loading cart %s\n", filename.c_str());
    CloseCart();

    // If the filename is an absolute path, update the cart directory
    if (isAbsolutePath(filename)) {
        std::string dir = getDirectory(filename);
        if (dir.length() > 0) {
            Logger_Write("Setting cart directory to %s\n", dir.c_str());
            _host->setCartDirectory(dir);
            // Also update the cart list for the default cart
            SetCartList(_host->listcarts());
        }
    }

    Logger_Write("Calling Cart Constructor\n");
    auto cartDir = _host->getCartDirectory();
    Cart *cart = new Cart(filename, cartDir);
    Logger_Write("Cart Constructor called\n");

    // PBC+: keep the error that sent us back to the menu, for the menu
    if (cart->LoadError.length() > 0 || filename != DefaultCartName) {
        _cartLoadError = cart->LoadError;
    }

    // If cart has a load error (file not found, etc.), load default cart with error
    if (cart->LoadError.length() > 0) {
        Logger_Write("Cart load error: %s\n", cart->LoadError.c_str());
        // Save the error message so default cart can display it
        _cartLoadError = cart->LoadError;
        delete cart;
        // Load default cart to display error
        LoadBiosCart();
        return false;
    }

    bool success = loadCart(cart);
    Logger_Write("Loaded cart into vm\n");

    if (!success) {
        delete cart;
        if (_cartLoadError.length() == 0) {
            _cartLoadError = "Cart file has no Lua code";
        }
        LoadBiosCart();
    }

    return success;
}

bool Vm::LoadCart(const unsigned char* cartData, size_t size, bool loadBiosOnFail){
    Logger_Write("Loading cart from memory\n");
    CloseCart();

    Logger_Write("Calling Cart Constructor\n");
    Cart *cart = new Cart(cartData, size);

    _cartLoadError = cart->LoadError;

    bool success = loadCart(cart);

    if (loadBiosOnFail && !success) {
        CloseCart();
        //todo: show an error message on the bios?
        //LoadBiosCart();
    }

    return success;
}

void Vm::togglePauseMenu(){
    _input->SetState(0, 0);
    if (_memory->drawState.suppressPause) {    
        _memory->drawState.suppressPause = 0;
        return;
    }

    _pauseMenu = !_pauseMenu;

    if (_pauseMenu){
        //save old draw state
        //0x5f00-0x5f3f - 64 bytes
        memcpy(_drawStateCopy, &_memory->drawState, 64);

        _graphics->pal();
        _graphics->fillp(0);
        _graphics->clip();
        _graphics->camera();
        _graphics->color();

        // Pause audio
        _audio->setPaused(true);
    }
    else{
        //restore old draw state
        memcpy(&_memory->drawState, _drawStateCopy, 64);
        // Clear input on next update so the button press that closed the menu
        // doesn't get passed to the cart
        _clearInputOnResume = true;

        // Resume audio
        _audio->setPaused(false);
    }
    
}

bool Vm::IsPaused(){
    return _pauseMenu;
}


//https://stackoverflow.com/a/30606613
std::vector<uint8_t> HexToBytes(std::string hex) {
  std::vector<uint8_t> bytes;

  hex.erase(std::remove(hex.begin(), hex.end(), '\n'), hex.end());
  hex.erase(std::remove(hex.begin(), hex.end(), '\r'), hex.end());

  for (unsigned int i = 0; i < hex.length(); i += 2) {
    std::string byteString = hex.substr(i, 2);
    uint8_t byte = (uint8_t) strtol(byteString.c_str(), NULL, 16);
    bytes.push_back(byte);
  }

  return bytes;
}

std::string Vm::getSerializedCartData() {
    std::string outputstr = "";

    char hex_string[9] = "00000000";

    //ATTN: writing one byte at a time instead of one 32 bit int at a time
    //to ensure same behavior across platforms and cpu architectures
    for(int i = 0; i < 64; i++){
        for(int b = 3; b >= 0; b--){
            uint8_t byte = _memory->data[0x5e00 + (i*4) + b];

            sprintf(hex_string, "%02x", byte);

            outputstr.append(hex_string);
        }

        if ((i + 1) % 8 == 0) {
            outputstr.append("\n");
        }
    }

    return outputstr;
}

void Vm::deserializeCartDataToMemory(std::string cartDataStr) {
    //populate from string (assume correct length? TODO: validation)
    std::vector<uint8_t> bytesVector = HexToBytes(cartDataStr);

    //ATTN: writing one byte at a time instead of one 32 bit int at a time
    //to ensure same behavior across platforms and cpu architectures
    for(size_t i = 0; i < 64; i++){
        size_t idxStart = (i*4);
        size_t idxEnd = idxStart + 3;
        if (idxEnd < bytesVector.size()){
            _memory->data[0x5e00 + idxStart + 0] = bytesVector[idxEnd];
            _memory->data[0x5e00 + idxStart + 1] = bytesVector[idxEnd - 1];
            _memory->data[0x5e00 + idxStart + 2] = bytesVector[idxEnd - 2];
            _memory->data[0x5e00 + idxStart + 3] = bytesVector[idxEnd - 3];
        }
    }
}

bool Vm::Step(){
    _picoFrameCount++;

    // PBC+: there is no orderly shutdown on a handheld; write changed
    // cartdata every 5 s instead of only when the cart is left
    if (_picoFrameCount % 300 == 0 && _currentCartdataKey.length() > 0) {
        std::string now = getSerializedCartData();
        if (now != _savedCartData) {
            _host->saveCartData(_currentCartdataKey, now);
            _savedCartData = now;
        }
    }
    bool ret = false;
    lua_getglobal(_luaState, "__z8_tick");
    int status = lua_pcall(_luaState, 0, 1, 0);
    if (status != LUA_OK)
    {
        char const *message = lua_tostring(_luaState, -1);
        Logger_Write("error calling tick function: %s\n", message);
        _cartLoadError = "Error in main loop: " + std::string(message);
        lua_pop(_luaState, 1);
        // Return to menu on error
        QueueCartChange(DefaultCartName);
        return false;
    }
    else
    {
        int tickResult = (int)lua_tonumber(_luaState, -1);
        ret = tickResult >= 0;
        // If __z8_tick returned -1, it means there was an error (already handled in Lua)
        // but we should ensure error message is set if it's not already
        if (tickResult == -1 && _cartLoadError.empty()) {
            _cartLoadError = "Error in cart execution";
        }
    }
    lua_pop(_luaState, 1);

    // Process any queued cart change (from reset, load, etc.)
    if (_cartChangeQueued) {
        // PBC+: drop the old cart's code and data before the new one is read
        lua_pushnil(_luaState);
        lua_setglobal(_luaState, "__z8_loop");
        lua_pushnil(_luaState);
        lua_setglobal(_luaState, "__cart_sandbox");
        lua_pushnil(_luaState);
        lua_setfield(_luaState, LUA_REGISTRYINDEX, "__PICO8_SANDBOX");
        lua_gc(_luaState, LUA_GCCOLLECT, 0);
        Logger_Write("Processing cart change, _pauseMenu = %s\n", _pauseMenu ? "true" : "false");
        _prevCartKey = CurrentCartFilename();
        if (_nextCartSize > 0){
            LoadCart(_nextCartData, _nextCartSize);
        }
        else {
            LoadCart(_nextCartKey);
        }
        // Only run if cart loaded successfully (has Lua code)
        if (_loadedCart && !_loadedCart->LuaString.empty()) {
            Logger_Write("Before vm_run, _pauseMenu = %s\n", _pauseMenu ? "true" : "false");
            vm_run();
            Logger_Write("After vm_run, _pauseMenu = %s\n", _pauseMenu ? "true" : "false");
        }
        else {
            Logger_Write("Cart load failed, not running vm_run()\n");
            // Clear cart change queue to prevent infinite retry loop
            _cartChangeQueued = false;
        }
    }

    return ret;
}

// void Vm::UpdateAndDraw() {
//     update_buttons();

//     _picoFrameCount++;

//     if (_cartChangeQueued) {
//         _prevCartKey = CurrentCartFilename();
//         if (_nextCartSize > 0){
//             LoadCart(_nextCartData, _nextCartSize);
//         }
//         else {
//             LoadCart(_nextCartKey);
//         }
//     }

//     if (_pauseMenu){

//         lua_getglobal(_luaState, "__f08_menu_update");
//         lua_call(_luaState, 0, 0);
//         lua_pop(_luaState, 0);

//         lua_getglobal(_luaState, "__f08_menu_draw");
//         lua_call(_luaState, 0, 0);
//         lua_pop(_luaState, 0);
//     }
//     else{
//         // Push the _update function on the top of the lua stack
//         if (_targetFps == 60) {
//             lua_getglobal(_luaState, "_update60");
//             if (!lua_isfunction(_luaState, -1)) {
//                 lua_getglobal(_luaState, "_update");
//             }
//         } else {
//             lua_getglobal(_luaState, "_update");
//             if (!lua_isfunction(_luaState, -1)) {
//                 lua_getglobal(_luaState, "_update60");
//             }
//         }

//         if (lua_isfunction(_luaState, -1)) {
//             if (lua_pcall(_luaState, 0, 0, 0)){
//                 _cartLoadError = lua_tostring(_luaState, -1);
//                 Logger_Write("Error: %s\n", lua_tostring(_luaState, -1));
//                 lua_pop(_luaState, 1);
//                 QueueCartChange(BiosCartName);
//                 return;
//             }
//         }
//         //pop the update fuction off the stack now that we're done with it
//         lua_pop(_luaState, 0);

//         lua_getglobal(_luaState, "_draw");
//         if (lua_isfunction(_luaState, -1)) {
//             if (lua_pcall(_luaState, 0, 0, 0)){
//                 _cartLoadError = lua_tostring(_luaState, -1);
//                 Logger_Write("Error: %s\n", lua_tostring(_luaState, -1));
//                 lua_pop(_luaState, 1);
//                 QueueCartChange(BiosCartName);
//                 return;
//             }
//         }
//         lua_pop(_luaState, 0);

//         if (_input->btnp(6)) {
//             togglePauseMenu();
//         }
//     }

// }

uint8_t* Vm::GetPicoInteralFb(){
    return _graphics->GetP8FrameBuffer();
}

uint8_t* Vm::GetScreenPaletteMap(){
    return _graphics->GetScreenPaletteMap();
}

void Vm::FillAudioBuffer(void *audioBuffer, size_t offset, size_t size){
   _audio->FillAudioBuffer(audioBuffer, offset, size);
}

void Vm::CloseCart() {
    if (_loadedCart){
        Logger_Write("deleting cart\n");
        delete _loadedCart;
        _loadedCart = nullptr;
    }
    
    // if (_luaState) {
    //     Logger_Write("closing lua state\n");
    //     lua_close(_luaState);
    //     _luaState = nullptr;
    // }

    Logger_Write("writing cart data\n");
    if (_currentCartdataKey.length() > 0) {
        _host->saveCartData(_currentCartdataKey, getSerializedCartData());
    }

    Logger_Write("resetting state\n");
    _targetFps = 60;
    _picoFrameCount = 0;
}

void Vm::QueueCartChange(std::string filename){
    _nextCartKey = filename;
    _nextCartData = nullptr;
    _nextCartSize = 0;
    _cartChangeQueued = true;
    _pauseMenu = false;
    _clearInputOnResume = true;
    _audio->setPaused(false);
}

void Vm::QueueCartChange(const unsigned char* cartData, size_t size){
    _nextCartKey = "";
    _nextCartData = cartData;
    _nextCartSize = size;
    _cartChangeQueued = true;
    _pauseMenu = false;
    _clearInputOnResume = true;
    _audio->setPaused(false);
}

int Vm::GetTargetFps() {
    return _targetFps;
}

std::string Vm::CurrentCartFilename(){
    if (_loadedCart){
        return _loadedCart->FullCartPath;
    }

    return "";
}

int Vm::GetFrameCount() {
    return _picoFrameCount;
}

void Vm::SetCartList(vector<string> cartList){
    std::sort(cartList.begin(), cartList.end());
    _cartList = cartList;
}

vector<string> Vm::GetCartList(){
    return _cartList;
}

vector<string> Vm::GetDirList(){
    return _host->listdirs();
}

bool Vm::ChangeDirectory(string dir){
    std::string currentDir = _host->getCartDirectory();
    std::string newDir;
    
    if (dir == "..") {
        // Go up one directory
        size_t lastSlash = currentDir.find_last_of("/\\");
        if (lastSlash != std::string::npos && lastSlash > 0) {
            newDir = currentDir.substr(0, lastSlash);
        } else if (lastSlash == 0) {
            newDir = ""; // PBC+: "" is the root of the drive
        } else {
            return false; // Already at root
        }
    } else if (dir.length() > 0 && (dir[0] == '/' || dir.find(':') != std::string::npos)) {
        // Absolute path
        newDir = dir;
    } else {
        // Relative path
        newDir = currentDir + "/" + dir;
    }
    
    // Verify the directory exists by trying to list its contents
    _host->setCartDirectory(newDir);
    vector<string> testList = _host->listcarts();
    
    // Update cart list even if empty (the directory might only contain subdirs)
    SetCartList(testList);
    
    Logger_Write("Changed directory to: %s\n", newDir.c_str());
    return true;
}

string Vm::GetCurrentDirectory(){
    return _host->getCartDirectory();
}

string Vm::GetBiosError() {
    return _cartLoadError;
}

void Vm::GameLoop() {
    Logger_Write("Start of GameLoop()\n");
    while (_host->shouldRunMainLoop())
    {
        //shouldn't need to set this every frame
        //_host->setTargetFps(_targetFps);

        //is this better at the end of the loop?
        _host->waitForTargetFps();

        if (_host->shouldQuit()) break; // break in order to return to hbmenu
        //this should probably be handled just in the host class
        _host->changeStretch();

        //update buttons needs to be callable from the cart, and also flip
        //it should update call the pico part of scanInput and set the values in memory
        //then we don't need to pass them in here
        //UpdateAndDraw();
        Step();

        uint8_t* picoFb = GetPicoInteralFb();
        uint8_t* screenPaletteMap = GetScreenPaletteMap();

        _host->drawFrame(picoFb, screenPaletteMap, _memory->drawState.drawMode);

        if (_host->shouldFillAudioBuff()) {
            FillAudioBuffer(_host->getAudioBufferPointer(), 0, _host->getAudioBufferSize());

            _host->playFilledAudioBuffer();
        }
    }
}

bool Vm::ExecuteLua(string luaString, string callbackFunction) {
    // Get the sandbox environment
    lua_getglobal(_luaState, "__cart_sandbox");
    
    if (!lua_istable(_luaState, -1)) {
        fprintf(stderr, "__cart_sandbox is not a table\n");
        lua_pop(_luaState, 1);
        return false;
    }
    
    // Load the Lua code with the sandbox as its environment
    if (luaL_loadstring(_luaState, luaString.c_str()) != LUA_OK) {
        fprintf(stderr, "Error loading Lua code: %s\n", lua_tostring(_luaState, -1));
        lua_pop(_luaState, 2); // Remove the error message and sandbox table
        return false;
    }
    
    // Set the environment of the loaded function to the sandbox
    lua_pushvalue(_luaState, -2); // Duplicate the sandbox table
    lua_setupvalue(_luaState, -2, 1); // Set it as the first upvalue of the function (environment)
    
    // Execute the loaded Lua code in the sandbox environment
    if (lua_pcall(_luaState, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "Error executing Lua code: %s\n", lua_tostring(_luaState, -1));
        lua_pop(_luaState, 2); // Remove the error message and sandbox table
        return false;
    }
    
    if (callbackFunction.length() > 0) {
        // Get the callback function from the sandbox
        lua_getfield(_luaState, -1, callbackFunction.c_str());
        
        if (!lua_isfunction(_luaState, -1)) {
            fprintf(stderr, "Callback function '%s' not found in sandbox\n", callbackFunction.c_str());
            lua_pop(_luaState, 2); // Remove the nil value and sandbox table
            return false;
        }
        
        // Call the function
        if (lua_pcall(_luaState, 0, 1, 0) != LUA_OK) {
            fprintf(stderr, "Error calling callback function: %s\n", lua_tostring(_luaState, -1));
            lua_pop(_luaState, 2); // Remove the error message and sandbox table
            return false;
        }
        
        // Get the result
        bool result = lua_toboolean(_luaState, -1);
        lua_pop(_luaState, 2); // Remove the result and sandbox table
        
        return result;
    }
    
    // Clean up the sandbox table
    lua_pop(_luaState, 1);
    
    return true;
}
const int MemoryUpperBound = 0x10000;

uint8_t Vm::vm_peek(int addr){
    if (addr < 0 || addr > MemoryUpperBound){
        return 0;
    }

    return _memory->data[addr];
}

int16_t Vm::vm_peek2(int addr){
    //zepto8
    int16_t bits = 0;
    for (int i = 0; i < 2; ++i)
    {
        /* This code handles partial reads by adding zeroes */
        if (addr + i < MemoryUpperBound)
            bits |= _memory->data[addr + i] << (8 * i);
        else if (addr + i >= MemoryUpperBound)
            bits |= _memory->data[addr + i - MemoryUpperBound] << (8 * i);
    }

    return bits;
}

//note: this should return a 32 bit fixed point number
fix32 Vm::vm_peek4(int addr){
    //zepto8
    int32_t bits = 0;
    for (int i = 0; i < 4; ++i)
    {
        /* This code handles partial reads by adding zeroes */
        if (addr + i < MemoryUpperBound)
            bits |= _memory->data[addr + i] << (8 * i);
        else if (addr + i >= MemoryUpperBound)
            bits |= _memory->data[addr + i - MemoryUpperBound] << (8 * i);
    }

    return fix32::frombits(bits);
} 

void Vm::vm_poke(int addr, uint8_t value){
    //todo: check how pico 8 handles out of bounds
    if (addr < 0 || addr > MemoryUpperBound){
        return;
    }
    
    _memory->data[addr] = value;
}

void Vm::vm_poke2(int addr, int16_t value){
    if (addr < 0 || addr > MemoryUpperBound - 1){
        return;
    }

    _memory->data[addr] = (uint8_t)value;
    _memory->data[addr + 1] = (uint8_t)(value >> 8);

}

void Vm::vm_poke4(int addr, fix32 value){
    if (addr < 0 || addr > MemoryUpperBound - 3){
        return;
    }

    uint32_t ubits = (uint32_t)value.bits();
    _memory->data[addr + 0] = (uint8_t)ubits;
    _memory->data[addr + 1] = (uint8_t)(ubits >> 8);
    _memory->data[addr + 2] = (uint8_t)(ubits >> 16);
    _memory->data[addr + 3] = (uint8_t)(ubits >> 24);
}

bool Vm::vm_cartdata(string key) {
    //match pico 8 errors
    if (key.length() == 0 || key.length() > 64) {
        QueueCartChange(DefaultCartName);
        _cartLoadError = "cart data id too long";
        return false;
    }
    //todo: validate chars

    // If we have a current key, save its data before switching
    if (_currentCartdataKey.length() > 0) {
        _host->saveCartData(_currentCartdataKey, getSerializedCartData());
    }

    // Check if this key is already in our list
    bool keyExists = false;
    for (int i = 0; i < _cartdataKeyCount; i++) {
        if (_cartdataKeys[i] == key) {
            keyExists = true;
            break;
        }
    }

    // If it's a new key, add it to our list (up to 4 keys max)
    if (!keyExists) {
        if (_cartdataKeyCount >= 4) {
            QueueCartChange(DefaultCartName);
            _cartLoadError = "too many cart data keys (max 4)";
            return false;
        }
        _cartdataKeys[_cartdataKeyCount] = key;
        _cartdataKeyCount++;
    }

    // Only clear memory if we're switching to a different key
    if (_currentCartdataKey != key) {
        // Clear the cart data memory (0x5e00-0x5eff)
        memset(&_memory->data[0x5e00], 0, 0x100);
    }

    // Set this as the current active key
    _currentCartdataKey = key;

    // Try to load data from file for this key
    auto cartDataStr = _host->getCartDataFileContents(_currentCartdataKey);
    _savedCartData = cartDataStr;

    //todo: validate hex format
    if (cartDataStr.length() > 0) {
        deserializeCartDataToMemory(cartDataStr);
        return true;
    } else {
        return false;
    }
}

fix32 Vm::vm_dget(uint8_t n) {
    if (n < 64) {
        return vm_peek4(0x5e00 + 4 * n);
    }

    return 0;
}

void Vm::vm_dset(uint8_t n, fix32 value){
    if (n < 64) {
        vm_poke4(0x5e00 + 4 * n, value);
    }
}

void Vm::vm_reload(int destaddr, int sourceaddr, int len, Cart* cart){
    if (len <= 0 || sourceaddr < 0) {
        return;
    }
    // PBC+: past the end of the ROM there is nothing (fake-08 read whatever
    // followed the ROM in memory)
    if (sourceaddr + len > (int)sizeof(CartRomData)) {
        int avail = std::max(0, (int)sizeof(CartRomData) - sourceaddr);
        memset(&_memory->data[destaddr + avail], 0, (size_t)(len - avail));
        len = avail;
        if (len == 0) return;
    }
    if (cart->CartRom) {
        memcpy(&_memory->data[destaddr], &cart->CartRom->data[sourceaddr], len);
        return;
    }
    // PBC+: the ROM lives in the cache file (cacheCartRom)
    pfile *f = pf_open_read(_romCachePath.c_str());
    bool ok = f && pf_seek(f, kRomCacheHeader + sourceaddr)
              && pf_read(f, &_memory->data[destaddr], len) == len;
    if (f) pf_close(f);
    if (!ok) {
        // cache gone (drive changed under us): read the cart again
        Cart *again = new Cart(cart->FullCartPath, "");
        if (again->CartRom)
            memcpy(&_memory->data[destaddr], &again->CartRom->data[sourceaddr], len);
        delete again;
    }
}

// PBC+: "/cdata/<hash of the path>.rom": a 16 byte header (magic, size and
// date of the cart file it was made from) and the 0x4300 ROM bytes. It is
// written once per cart (and again when the cart file changes). If it cannot
// be written, the ROM simply stays in RAM.
static const char kRomCacheMagic[4] = { 'P', '8', 'R', 'C' };

void Vm::cacheCartRom(Cart *cart) {
    _romCachePath.clear();
    const std::string &path = cart->FullCartPath;
    if (!cart->CartRom || path.empty() || path.rfind("__FAKE08", 0) == 0)
        return;
#ifdef PBC_LOG_STDOUT
    if (getenv("PBC_NO_ROMCACHE")) return;      // PC test: compare against the ROM in RAM
#endif

    long size;
    unsigned long stamp;
    if (!pf_stat(path.c_str(), &size, &stamp))
        return;
    uint32_t h = 2166136261u;                       // FNV-1a
    for (unsigned char c : path) { h ^= c; h *= 16777619u; }
    char name[32];
    snprintf(name, sizeof name, "/cdata/%08lx.rom", (unsigned long)h);

    uint8_t want[kRomCacheHeader] = {0}, have[kRomCacheHeader] = {0};
    memcpy(want, kRomCacheMagic, 4);
    uint32_t sz = (uint32_t)size, st = (uint32_t)stamp;
    memcpy(want + 4, &sz, 4);
    memcpy(want + 8, &st, 4);

    // up to date already?
    pfile *f = pf_open_read(name);
    bool valid = false;
    if (f) {
        valid = pf_read(f, have, kRomCacheHeader) == kRomCacheHeader
                && memcmp(have, want, kRomCacheHeader) == 0
                && pf_size(f) == (long)(kRomCacheHeader + sizeof(CartRomData));
        if (valid) {
            // same file name for another path is possible: compare contents
            static uint8_t chunk[256];
            for (size_t off = 0; valid && off < sizeof(CartRomData); off += sizeof chunk) {
                size_t n = std::min(sizeof chunk, sizeof(CartRomData) - off);
                valid = pf_read(f, chunk, (int)n) == (int)n
                        && memcmp(chunk, cart->CartRom->data + off, n) == 0;
            }
        }
        pf_close(f);
    }
    if (!valid) {
        f = pf_open_write(name);
        bool ok = f && pf_write(f, want, kRomCacheHeader) == kRomCacheHeader
                  && pf_write(f, cart->CartRom->data, sizeof(CartRomData)) == (int)sizeof(CartRomData);
        if (f) pf_close(f);
        if (!ok) {
            Logger_Write("rom cache %s not written, ROM stays in RAM\n", name);
            return;
        }
    }
    _romCachePath = name;
    cart->ReleaseRom();
}

void Vm::vm_reload(int destaddr, int sourceaddr, int len, string filename){
    if (len <= 0) {
        return;
    }
    if (destaddr < 0 || destaddr > (int)sizeof(PicoRam)) {
        //invalid dest address
        return;
    }
    if (sourceaddr < 0 || sourceaddr > (int)sizeof(CartRomData)) {
        //invalid source address
        return;
    }
    if (len < 0 || destaddr + len > (int)sizeof(PicoRam)) {
        //invalid length address
        return;
    }

    Cart* cart = _loadedCart;
    bool multicart = false;

    if (filename.length() > 0) {
        cart = new Cart(filename, _host->getCartDirectory());
        if (cart->LoadError.length() > 0) {
            //error, can't load cart
            //todo: see what kind of error pico 8 throws, emulate
            delete cart;

            return;
        }

        multicart = true;
    }

    vm_reload(destaddr, sourceaddr, len, cart);

    if (multicart) {
        delete cart;
    }
}

void Vm::vm_memset(int destaddr, uint8_t val, int len){
    if (len <= 0) {
        return;
    }
    if (destaddr < 0 || destaddr + len > (int)sizeof(PicoRam)) {
        return;
    }

    memset(&_memory->data[destaddr], val, len);

}
void Vm::vm_memcpy(int destaddr, int sourceaddr, int len){
    if (len <= 0) {
        return;
    }
    if (sourceaddr < 0 || sourceaddr + len > (int)sizeof(PicoRam)) {
        return;
    }
    if (destaddr < 0 || destaddr + len > (int)sizeof(PicoRam)) {
        return;
    }

    memcpy(&_memory->data[destaddr], &_memory->data[sourceaddr], len);
}

void Vm::update_prng()
{
    uint32_t* rngState = _memory->hwState.rngState;
    rngState[1] = rngState[0] + ((rngState[1] >> 16) | (rngState[1] << 16));
    rngState[0] += rngState[1];
}

fix32 Vm::api_rnd()
{
    return api_rnd((fix32)1);
}

fix32 Vm::api_rnd(fix32 in_range)
{
    update_prng();
    uint32_t b = _memory->hwState.rngState[1];
    uint32_t range = in_range.bits();
    return fix32::frombits(range > 0 ? b % range : 0);
}

void Vm::api_srand(fix32 seed)
{
    uint32_t* rngState = _memory->hwState.rngState;
    rngState[0] = seed ? seed.bits() : 0xdeadbeef;
    rngState[1] = rngState[0] ^ 0xbead29ba;
    for (int i = 0; i < 32; ++i) {
        update_prng();
    }
}

void Vm::update_buttons() {
    //get button states from hardware
    auto inputState = _host->scanInput();
    
    // If we just resumed from pause menu, clear input so the button press
    // that closed the menu doesn't get passed to the cart.
    // Keep clearing until the user releases all buttons.
    if (_clearInputOnResume) {
        // Check if action buttons (4=O, 5=X) are still held
        if ((inputState.KHeld & 0x30) == 0) {
            _clearInputOnResume = false;
        }
        _input->SetState(0, 0);
        _input->SetMouse(0, 0, 0);
        _input->SetKeyboard(false, "");
        return;
    }
    
    _input->SetState(inputState.KDown, inputState.KHeld);
    if (_memory->drawState.devkitMode) {
        _input->SetMouse(inputState.mouseX, inputState.mouseY, inputState.mouseBtnState);
        _input->SetKeyboard(inputState.KBdown,inputState.KBkey);
    }
    else {
        _input->SetMouse(0, 0, 0);
        _input->SetKeyboard(false,"");
    }

    // Check for pause button (bit 6 / 0x40)
    if (_input->btnp(6)) {
        togglePauseMenu();
    }
}

// void Vm::vm_flip() {
//     if (!_host->shouldRunMainLoop()){
//         abortLua = true;
//         if (abortLua){
//             longjmp(place, 1);
//         }
//     }

//     if (!_host->shouldQuit() && !_cartChangeQueued) {
//         update_buttons();

//         _picoFrameCount++;

//         //todo: pause menu here, but for now just load bios
//         if (_input->btnp(6)) {
//             //QueueCartChange(BiosCartName);
//             togglePauseMenu();

//             //shouldn't get here
//             return;
//         }

//         _host->changeStretch();

//         _host->setTargetFps(_targetFps);

//         uint8_t* picoFb = GetPicoInteralFb();
//         uint8_t* screenPaletteMap = GetScreenPaletteMap();

//         if (_pauseMenu){
//             //pause menu probably needs refactor out of lua. For now this is better than just quitting
//             lua_getglobal(_luaState, "__f08_menu_update");
//             lua_call(_luaState, 0, 0);
//             lua_pop(_luaState, 0);

//             lua_getglobal(_luaState, "__f08_menu_draw");
//             lua_call(_luaState, 0, 0);
//             lua_pop(_luaState, 0);
//         }

//         _host->drawFrame(picoFb, screenPaletteMap, _memory->drawState.drawMode);

//         //is this better at the end of the loop?
//         _host->waitForTargetFps();
//     }
// }

void Vm::vm_run() {
    // if (_loadedCart) {
    //     loadCart(_loadedCart);
    // }

    //should combine this with other memory reset code
    _memory->Reset();

    //seed rng
    auto now = std::chrono::high_resolution_clock::now();
    api_srand(fix32::frombits(boot_seed(now.time_since_epoch().count())));

    //set graphics state
    _graphics->color();
    _graphics->clip();
    _graphics->pal();

    _audio->resetAudioState();

    // Clear any pending input so button presses from pause menu don't
    // get seen by the cart's _init() function. The flag stays set so the
    // first _update_buttons() call will also return cleared input.
    if (_clearInputOnResume) {
        _input->SetState(0, 0);
    }

    lua_getglobal(_luaState, "__z8_run_cart");
    lua_pushnil(_luaState);  // PBC+: the source is read by __z8_load_cart
    int status = lua_pcall(_luaState, 1, 0, 0);
    if (status != LUA_OK)
    {
        char const *message = lua_tostring(_luaState, -1);
        Logger_Write("error in vm_run: %s\n", message);
        _cartLoadError = "Error loading cart: " + std::string(message);
        lua_pop(_luaState, 1);
        // Return to menu on error
        QueueCartChange(DefaultCartName);
    }

}

void Vm::vm_extcmd(std::string cmd){
    //screenshots, recording, and breadcrumb not supported
    if (cmd == "reset"){
        QueueCartChange(CurrentCartFilename());
    }
    else if (cmd == "pause") {
        togglePauseMenu();
    }
    else if (cmd == "shutdown") {
        QueueCartChange(DefaultCartName);
    }
}

// PBC+: the file load() would open, resolved like the Cart constructor
// does, or "" if there is none
static std::string find_cart_file(std::string filename, const std::string &dir) {
    if (!filename.empty() && filename[0] == '#') filename = filename.substr(1);
    std::string ext = getFileExtension(filename);
    if (ext == "") filename += ".p8";
    std::string path = dir.length() > 0 && !isAbsolutePath(filename) ? dir + "/" + filename : filename;
    for (int i = 0; i < 2; i++) {
        pfile *f = pf_open_read(path.c_str());
        if (f) { pf_close(f); return path; }
        if (ext != "" && ext != ".p8") break;
        path += ".png";
    }
    return "";
}

// PBC+: as in PICO-8, a load() that finds its cart does not come back.
// The cart change is queued and the cart's coroutine is left for good
// (the tick drops it and starts the new cart from the top), instead of
// starting the new cart inside the old one's call stack, which then ran
// on after load() returned. A missing cart gives false, so a cart can
// try the next name.
bool Vm::vm_load(std::string filename, std::string breadcrumb, std::string param){
    Logger_Write("vm_load: loading %s\n", filename.c_str());

    if (find_cart_file(filename, _host->getCartDirectory()).empty()) {
        Logger_Write("vm_load: %s not found\n", filename.c_str());
        return false;
    }

    _cartBreadcrumb = breadcrumb;
    if (param.length() > 0) {
        _cartParam = param;
    }

    if (_currentCartdataKey.length() > 0) {
        _host->saveCartData(_currentCartdataKey, getSerializedCartData());
    }

    QueueCartChange(filename);
    return true;
}

void Vm::vm_reset(){
    memset(&_memory->data[0x5f00], 0, 0x7f);

    _memory->hwState.colorBitmask = 0xff;
    _memory->hwState.spriteSheetMemMapping = 0x00;
    _memory->hwState.screenDataMemMapping = 0x60;
    _memory->hwState.mapMemMapping = 0x20;
    _memory->hwState.widthOfTheMap = 128;

    _graphics->color();
    _graphics->clip();
    _graphics->pal();
}

void Vm::setTargetFps(int targetFps){
    //currently handled by lua loop?
    //_targetFps = targetFps;
}

int Vm::getFps(){
    //TODO: return actual fps (as fix32?)
    return _targetFps;
}

int Vm::getTargetFps(){
    return _targetFps;
}

int Vm::getYear(){
    std::time_t t = std::time(0);
    std::tm* now = std::localtime(&t);

    return now->tm_year + 1900;
}

int Vm::getMonth(){
    std::time_t t = std::time(0);
    std::tm* now = std::localtime(&t);

    return now->tm_mon + 1;
}

int Vm::getDay(){
    std::time_t t = std::time(0);
    std::tm* now = std::localtime(&t);

    return now->tm_mday;
}

int Vm::getHour(){
    std::time_t t = std::time(0);
    std::tm* now = std::localtime(&t);

    return now->tm_hour;
}

int Vm::getMinute(){
    std::time_t t = std::time(0);
    std::tm* now = std::localtime(&t);

    return now->tm_min;
}

int Vm::getSecond(){
    std::time_t t = std::time(0);
    std::tm* now = std::localtime(&t);

    return now->tm_sec;
}

std::string Vm::getCartBreadcrumb() {
    return _cartBreadcrumb;
}

std::string Vm::getCartParam() {
    return _cartParam;
}


//settings 
int Vm::getSetting(std::string sname) {
    
    return _host->getSetting(sname);
    
}

void Vm::setSetting(std::string sname, int sval) {
    
    _host->setSetting(sname,sval);
    
}


void Vm::installPackins() {
    #if LOAD_PACK_INS
    _host->setSetting("packinloaded",0);
    _host->unpackCarts();
    #endif
}




void Vm::loadLabel(std::string filename, bool mini, int minioffset) {
    
    auto cartDir = _host->getCartDirectory();
    Cart *labelcart = new Cart(filename, cartDir, true);
    std::string labelstr = labelcart->LabelString;
    if(labelstr.length() == 0){
        labelstr = NoLabelString;
    }
    if (mini) {
        copy_mini_label_to_sprite_memory(_memory->spriteSheetData, labelstr, minioffset);
    } else {
        copy_string_to_sprite_memory(_memory->spriteSheetData, labelstr);
    }
    delete labelcart;
    
}

const std::string &Vm::GetLoadedLua() {
    static const std::string empty;
    if (!_loadedCart) return empty;
    if (_luaReleased) {
        // compiled once already (run() / restart): read it again
        Logger_Write("re-reading the lua source of %s\n", _loadedCart->FullCartPath.c_str());
        Cart *again = new Cart(_loadedCart->FullCartPath, "");
        _loadedCart->LuaString.swap(again->LuaString);
        delete again;
        _luaReleased = false;
    }
    return _loadedCart->LuaString;
}

void Vm::ReleaseLoadedLua() {
    // built-in carts have no file to read it back from
    if (!_loadedCart || _loadedCart->FullCartPath.rfind("__FAKE08", 0) == 0) return;
    std::string().swap(_loadedCart->LuaString);
    _luaReleased = true;
}

std::string Vm::getLuaLine(string filename, int linenumber) {
    
    auto cartDir = _host->getCartDirectory();
    Cart *luacart = new Cart(filename, cartDir);
    std::string luastr = luacart->LuaString;
    
    std::string line;
    size_t pos = 0;
    
    while (linenumber-- >= 0 && pos <= luastr.size()){
        size_t nl = luastr.find('\n', pos);
        if (nl == std::string::npos) nl = luastr.size();
        line = luastr.substr(pos, nl - pos);
        pos = nl + 1;
    }
    
    delete luacart;
    
    return line;
    
}


size_t Vm::serializeLuaState(char* dest) {
    lua_getglobal(_luaState, "eris");
	lua_getfield(_luaState, -1, "persist_all");

	if (lua_pcall(_luaState, 0, 1, 0) != 0) {
		std::string e = lua_tostring(_luaState, -1);
		lua_pop(_luaState, 1);
		return 0;
	}

	size_t len;
	const char* result = lua_tolstring(_luaState, -1, &len);
    memcpy(dest, result, len);
	lua_pop(_luaState, 2);

    return len;
}

void Vm::deserializeLuaState(const char* src, size_t len) {
    lua_getglobal(_luaState, "eris");
	lua_getfield(_luaState, -1, "restore_all");
	lua_pushlstring(_luaState, src, len);

	if (lua_pcall(_luaState, 1, 0, 0) != 0) {
		std::string e = lua_tostring(_luaState, -1);
		lua_pop(_luaState, 1);
		return;
	}
	lua_pop(_luaState, 1);
}

