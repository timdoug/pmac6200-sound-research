CC=$(RETRO68)/bin/m68k-apple-macos-g++
REZ=$(RETRO68)/bin/Rez
CFLAGS=-Os
# Deliberately no -lRetroConsole: linking it drags all of libstdc++'s locale, iostream and
# exception machinery in, which takes even a hello-world past 1.2MB and makes it fail with a
# type 12 error at launch.  Results go to XAProbeResults.txt instead of the screen.
# If libInterface.a is not found, the toolchain install is incomplete -- a Retro68 built with
# --skip-thirdparty leaves it in multiversal/lib68k instead of m68k-apple-macos/lib, and it
# can be pointed at with -L$(RETRO68)/multiversal/lib68k.
LDFLAGS=-lInterface
RINCLUDES=$(RETRO68)/m68k-apple-macos/RIncludes
REZFLAGS=-I$(RINCLUDES)

XAProbe.bin: XAProbe.code.bin
	$(REZ) $(REZFLAGS) \
		--copy "XAProbe.code.bin" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "????" \
		-o XAProbe.bin --cc "._XAProbe.ad" --cc XAProbe.dsk

XAProbe.code.bin: xaprobe.o
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f XAProbe.bin XAProbe.code.bin XAProbe.code.bin.gdb xaprobe.o XAProbe.ad ._XAProbe.ad XAProbe.dsk

.PHONY: test
test:
	LaunchAPPL XAProbe.bin
