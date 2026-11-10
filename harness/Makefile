# s16-pi Linux test harness: make, then ./s16test <rom folder> <set>
CORE	= ../core
THIRD	= ../third_party
CXXFLAGS = -O2 -g -std=gnu++20 -Wall -Wextra -Wshadow -I$(CORE) -I$(THIRD)/musashi -I$(THIRD)/z80 -isystem $(THIRD)/ymfm
CFLAGS	= -O2 -g -w -I$(CORE) -I$(THIRD)/musashi -I$(THIRD)/z80 -DMUSASHI_CNF='"s16_m68kconf.h"' -DZ80_OPCODE_HOOK=s16_z80_opcode
OBJ	= obj/main.o obj/s16.o obj/s16_video.o obj/s16_sound.o obj/i8751.o obj/ymfm_opm.o \
	  obj/m68kcpu.o obj/m68kops.o obj/s16_crypt.o obj/s16_m68kstate.o obj/z80.o

s16test: $(OBJ)
	$(CXX) -o $@ $(OBJ)

obj/main.o: main.cpp $(CORE)/*.h
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c -o $@ $<
obj/%.o: $(CORE)/%.cpp $(CORE)/*.h $(CORE)/*.inc
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -DMUSASHI_CNF='"s16_m68kconf.h"' -DZ80_OPCODE_HOOK=s16_z80_opcode -c -o $@ $<
obj/m68k%.o: $(THIRD)/musashi/m68k%.c $(CORE)/s16_m68kconf.h
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<
obj/s16_m68kstate.o: $(CORE)/s16_m68kstate.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<
obj/ymfm_opm.o: $(THIRD)/ymfm/ymfm_opm.cpp
	@mkdir -p obj
	$(CXX) -O2 -g -std=gnu++20 -w -I$(THIRD)/ymfm -c -o $@ $<
obj/z80.o: $(THIRD)/z80/z80.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf obj s16test
