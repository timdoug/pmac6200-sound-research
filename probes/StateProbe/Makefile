CC=$(RETRO68)/bin/m68k-apple-macos-g++
REZ=$(RETRO68)/bin/Rez
CFLAGS=-Os
# Deliberately no -lRetroConsole: linking it drags all of libstdc++'s locale, iostream and
# exception machinery in, which takes even a hello-world past 1.2MB and makes it fail with a
# type 12 error at launch.  Results go to StateProbeResults.txt instead of the screen.
# If libInterface.a is not found, the toolchain install is incomplete -- a Retro68 built with
# --skip-thirdparty leaves it in multiversal/lib68k instead of m68k-apple-macos/lib, and it
# can be pointed at with -L$(RETRO68)/multiversal/lib68k.
LDFLAGS=-lInterface
RINCLUDES=$(RETRO68)/m68k-apple-macos/RIncludes
REZFLAGS=-I$(RINCLUDES)

StateProbe.bin: StateProbe.code.bin
	$(REZ) $(REZFLAGS) \
		--copy "StateProbe.code.bin" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "????" \
		-o StateProbe.bin --cc "._StateProbe.ad" --cc StateProbe.dsk

StateProbe.code.bin: stateprobe.o
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f StateProbe.bin StateProbe.code.bin StateProbe.code.bin.gdb stateprobe.o StateProbe.ad ._StateProbe.ad StateProbe.dsk

.PHONY: test
test:
	LaunchAPPL StateProbe.bin
