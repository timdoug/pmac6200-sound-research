CC=$(RETRO68)/bin/m68k-apple-macos-g++
REZ=$(RETRO68)/bin/Rez
CFLAGS=-Os
# Deliberately no -lRetroConsole: linking it drags all of libstdc++'s locale, iostream and
# exception machinery in, which takes even a hello-world past 1.2MB and makes it fail with a
# type 12 error at launch.  Results go to FilterProbeResults.txt instead of the screen.
# If libInterface.a is not found, the toolchain install is incomplete -- a Retro68 built with
# --skip-thirdparty leaves it in multiversal/lib68k instead of m68k-apple-macos/lib, and it
# can be pointed at with -L$(RETRO68)/multiversal/lib68k.
LDFLAGS=-lInterface
RINCLUDES=$(RETRO68)/m68k-apple-macos/RIncludes
REZFLAGS=-I$(RINCLUDES)

FilterProbe.bin: FilterProbe.code.bin
	$(REZ) $(REZFLAGS) \
		--copy "FilterProbe.code.bin" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "????" \
		-o FilterProbe.bin --cc "._FilterProbe.ad" --cc FilterProbe.dsk

FilterProbe.code.bin: filterprobe.o
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f FilterProbe.bin FilterProbe.code.bin FilterProbe.code.bin.gdb filterprobe.o FilterProbe.ad ._FilterProbe.ad FilterProbe.dsk

.PHONY: test
test:
	LaunchAPPL FilterProbe.bin
