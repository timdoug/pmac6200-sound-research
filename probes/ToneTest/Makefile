CC=$(RETRO68)/bin/m68k-apple-macos-g++
REZ=$(RETRO68)/bin/Rez
CFLAGS=-Os
# Same constraint as ASCTester: no -lRetroConsole, it drags in libstdc++ and the binary dies
# with a type 12 error at launch.
LDFLAGS=-lInterface
RINCLUDES=$(RETRO68)/m68k-apple-macos/RIncludes
REZFLAGS=-I$(RINCLUDES)

ToneTest.bin: ToneTest.code.bin
	$(REZ) $(REZFLAGS) \
		--copy "ToneTest.code.bin" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "????" \
		-o ToneTest.bin --cc "._ToneTest.ad" --cc ToneTest.dsk

ToneTest.code.bin: tone.o
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f ToneTest.bin ToneTest.code.bin ToneTest.code.bin.gdb tone.o ToneTest.ad ._ToneTest.ad ToneTest.dsk
