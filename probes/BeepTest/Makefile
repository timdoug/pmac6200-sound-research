CC=$(RETRO68)/bin/m68k-apple-macos-g++
REZ=$(RETRO68)/bin/Rez
CFLAGS=-Os
# Same constraint as ASCTester: no -lRetroConsole, it drags in libstdc++ and the binary dies
# with a type 12 error at launch.
LDFLAGS=-lInterface
RINCLUDES=$(RETRO68)/m68k-apple-macos/RIncludes
REZFLAGS=-I$(RINCLUDES)

BeepTest.bin: BeepTest.code.bin
	$(REZ) $(REZFLAGS) \
		--copy "BeepTest.code.bin" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "????" \
		-o BeepTest.bin --cc "._BeepTest.ad" --cc BeepTest.dsk

BeepTest.code.bin: beep.o
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f BeepTest.bin BeepTest.code.bin BeepTest.code.bin.gdb beep.o BeepTest.ad ._BeepTest.ad BeepTest.dsk
