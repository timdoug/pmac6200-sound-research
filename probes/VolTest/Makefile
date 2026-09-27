CC=$(RETRO68)/bin/m68k-apple-macos-g++
REZ=$(RETRO68)/bin/Rez
CFLAGS=-Os
# Same constraint as ASCTester: no -lRetroConsole, it drags in libstdc++ and the binary dies
# with a type 12 error at launch.
LDFLAGS=-lInterface
RINCLUDES=$(RETRO68)/m68k-apple-macos/RIncludes
REZFLAGS=-I$(RINCLUDES)

VolTest.bin: VolTest.code.bin
	$(REZ) $(REZFLAGS) \
		--copy "VolTest.code.bin" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "????" \
		-o VolTest.bin --cc "._VolTest.ad" --cc VolTest.dsk

VolTest.code.bin: vol.o
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f VolTest.bin VolTest.code.bin VolTest.code.bin.gdb vol.o VolTest.ad ._VolTest.ad VolTest.dsk
