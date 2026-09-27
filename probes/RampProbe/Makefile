CC=$(RETRO68)/bin/m68k-apple-macos-g++
REZ=$(RETRO68)/bin/Rez
CFLAGS=-Os
# Deliberately no -lRetroConsole: linking it drags all of libstdc++'s locale, iostream and
# exception machinery in, which takes even a hello-world past 1.2MB and makes it fail with a
# type 12 error at launch.  Results go to RampProbeResults.txt instead of the screen.
# If libInterface.a is not found, the toolchain install is incomplete -- a Retro68 built with
# --skip-thirdparty leaves it in multiversal/lib68k instead of m68k-apple-macos/lib, and it
# can be pointed at with -L$(RETRO68)/multiversal/lib68k.
LDFLAGS=-lInterface
RINCLUDES=$(RETRO68)/m68k-apple-macos/RIncludes
REZFLAGS=-I$(RINCLUDES)

RampProbe.bin: RampProbe.code.bin
	$(REZ) $(REZFLAGS) \
		--copy "RampProbe.code.bin" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "????" \
		-o RampProbe.bin --cc "._RampProbe.ad" --cc RampProbe.dsk

RampProbe.code.bin: rampprobe.o
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f RampProbe.bin RampProbe.code.bin RampProbe.code.bin.gdb rampprobe.o RampProbe.ad ._RampProbe.ad RampProbe.dsk

.PHONY: test
test:
	LaunchAPPL RampProbe.bin
