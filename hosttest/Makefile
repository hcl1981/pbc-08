# PC test build of the PBC+ PICO-8 firmware logic (no hardware).
#   make -C hosttest && PF_ROOT=../carts FRAMES=300 SHOTS=300 ./hosttest/pico8_host
F8   = ../fake08/source
LUA  = ../fake08/libs/z8lua
MZ   = ../fake08/libs/miniz
SRC  = ../src
OBJ  = obj

CXX  = g++
CC   = gcc
DEFS = -DPBC_NATIVE_MENU -DLUA_USE_LONGJMP=1 -DPBC_STRIP_DEBUG -DLUAI_MAXCCALLS=120 -DPBC_LOG_STDOUT -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS
INC  = -I$(F8) -I$(LUA) -I$(MZ) -I$(SRC) -I.
OPT  = -O2 -g
CXXFLAGS = $(OPT) $(DEFS) $(INC) -std=c++17 -fno-exceptions -fno-rtti -Wno-deprecated
CFLAGS   = $(OPT) $(DEFS) $(INC)

F8SRC  = Audio cart emojiconversion filehelpers filter fontdata graphics Input logger \
         mathhelpers nibblehelpers picoluaapi printHelper stringToDataHelpers synth vm
LUASRC = $(filter-out ltests,$(basename $(notdir $(wildcard $(LUA)/*.c))))

OBJS = $(F8SRC:%=$(OBJ)/f8_%.o) $(LUASRC:%=$(OBJ)/lua_%.o) $(OBJ)/miniz.o \
       $(OBJ)/pbc_host.o $(OBJ)/pico8_run.o $(OBJ)/video.o $(OBJ)/pbc_mem.o \
       $(OBJ)/plat_host.o $(OBJ)/main_host.o $(OBJ)/pfile_posix.o $(OBJ)/settings.o

pico8_host: $(OBJS)
	$(CXX) -o $@ $^ -lm

$(OBJ)/f8_%.o: $(F8)/%.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ)/lua_%.o: $(LUA)/%.c | $(OBJ)
	$(CXX) -x c++ $(CXXFLAGS) -c $< -o $@
$(OBJ)/miniz.o: $(MZ)/miniz.c | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/%.o: $(SRC)/%.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/%.o: %.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ)/%.o: %.c | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ):
	mkdir -p $@

clean:
	rm -rf $(OBJ) pico8_host pico8_stack menu_host label_host luadump_host

# ---- PC preview of the native menu ----
MENUOBJ = $(OBJ)/m_menu.o $(OBJ)/m_gfx.o $(OBJ)/m_assets.o $(OBJ)/m_settings.o \
          $(OBJ)/m_cart.o $(OBJ)/f8_emojiconversion.o $(OBJ)/f8_filehelpers.o \
          $(OBJ)/f8_logger.o $(OBJ)/miniz.o $(OBJ)/pfile_posix.o $(OBJ)/menu_host.o
menu_host: $(MENUOBJ)
	$(CXX) -o $@ $^
$(OBJ)/m_menu.o: $(SRC)/menu.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -DPBC_HOST -c $< -o $@
$(OBJ)/m_cart.o: $(F8)/cart.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -DPBC_HOST -c $< -o $@
$(OBJ)/m_gfx.o: $(SRC)/gfx.c | $(OBJ)
	$(CC) $(CFLAGS) -DPBC_HOST -c $< -o $@
$(OBJ)/m_assets.o: $(SRC)/assets.c | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/m_settings.o: $(SRC)/settings.c | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

pico8_stack: $(filter-out $(OBJ)/main_host.o,$(OBJS)) $(OBJ)/main_stack.o
	$(CXX) -o $@ $^ -lm -lpthread

# ---- cart helpers: menu label loading, Lua source dump ----
CARTOBJ = $(OBJ)/m_cart.o $(OBJ)/f8_emojiconversion.o $(OBJ)/f8_filehelpers.o \
          $(OBJ)/f8_logger.o $(OBJ)/miniz.o $(OBJ)/pfile_posix.o
label_host: $(OBJ)/label_host.o $(CARTOBJ)
	$(CXX) -o $@ $^
luadump_host: $(OBJ)/luadump_host.o $(CARTOBJ)
	$(CXX) -o $@ $^
$(OBJ)/label_host.o $(OBJ)/luadump_host.o: $(OBJ)/%.o: %.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -DPBC_HOST -c $< -o $@
