/*
 * ascprobe: drive PrimeTime III's ASC through /dev/mem and print what it does, for diffing the
 * real Performa 6200 against MAME.  Run with the sound driver unbound and DFAC II's output
 * level at 0 (ascprobe.sh does both).  Watches the ASC interrupt as VIA2 IFR bit 4, which
 * latches edges even while the kernel has the interrupt masked.
 * Freestanding: powerpc-linux-gnu-gcc -O2 -mcpu=603 -static -nostdlib -ffreestanding.
 */
typedef unsigned long ul;
typedef unsigned long long u64;

static long sys(long nr, long a, long b, long c, long d, long e, long f)
{
	register long r0 asm("r0") = nr;
	register long r3 asm("r3") = a;
	register long r4 asm("r4") = b;
	register long r5 asm("r5") = c;
	register long r6 asm("r6") = d;
	register long r7 asm("r7") = e;
	register long r8 asm("r8") = f;

	asm volatile("sc\n\tbns+ 1f\n\tneg %1,%1\n1:"
		     : "+r" (r0), "+r" (r3), "+r" (r4), "+r" (r5), "+r" (r6), "+r" (r7), "+r" (r8)
		     : : "cr0", "memory", "r9", "r10", "r11", "r12", "ctr");
	return r3;
}

static char obuf[8192];
static int olen;

static void flush(void)
{
	sys(4, 1, (long)obuf, olen, 0, 0, 0);
	olen = 0;
}

static void out(const char *s)
{
	while (*s) {
		if (olen == sizeof(obuf))
			flush();
		obuf[olen++] = *s++;
	}
}

static void hex(ul v, int digits)
{
	char b[12];
	int i;

	for (i = digits - 1; i >= 0; i--, v >>= 4)
		b[i] = "0123456789abcdef"[v & 15];
	b[digits] = 0;
	out(b);
}

static void dec(long v)
{
	char b[24];
	int i = 23;

	b[i] = 0;
	if (v < 0) {
		out("-");
		v = -v;
	}
	do {
		b[--i] = '0' + v % 10;
		v /= 10;
	} while (v);
	out(b + i);
}

static void die(const char *s)
{
	out(s);
	flush();
	sys(1, 1, 0, 0, 0, 0, 0);
}

static volatile unsigned char *asc, *via2;

static volatile unsigned char *map(ul phys, ul len)
{
	static int fd = -1;
	long p;

	if (fd < 0)
		fd = sys(5, (long)"/dev/mem", 0x101002 /* O_RDWR|O_SYNC */, 0, 0, 0, 0);
	if (fd < 0)
		die("open /dev/mem failed\n");
	p = sys(192, 0, len, 3, 1 /* MAP_SHARED */, fd, phys >> 12);
	if (p < 0 && p > -4096)
		die("mmap failed\n");
	return (volatile unsigned char *)p;
}

/* The time base runs at 9.375 MHz. */
#define TB_HZ 9375000ULL

/* No libgcc: 64-bit division by shift and subtract. */
static u64 div64(u64 n, u64 d)
{
	u64 q = 0, bit = 1;

	while (d < n && !(d >> 63)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	return q;
}

static u64 tb(void)
{
	ul hi, lo, hi2;

	do {
		asm volatile("mftbu %0; mftb %1; mftbu %2" : "=r" (hi), "=r" (lo), "=r" (hi2));
	} while (hi != hi2);
	return ((u64)hi << 32) | lo;
}

static void wait_us(ul us)
{
	u64 end = tb() + div64((u64)us * TB_HZ, 1000000);

	while (tb() < end)
		;
}

static unsigned rb(int o) { return asc[o]; }
static void wb(int o, unsigned v) { asc[o] = v; }
static void w16(int o, unsigned v) { *(volatile unsigned short *)(asc + o) = v; }

/* An 11-bit FIFO pointer, high byte then low; reread if the high byte moved. */
static unsigned ptr(int o)
{
	unsigned h, l, h2;

	do {
		h = rb(o);
		l = rb(o + 1);
		h2 = rb(o);
	} while (h != h2);
	return ((h << 8) | l) & 0x7ff;
}

#define B_WR 0xf20
#define B_RD 0xf22
#define R_WR 0xf00
#define R_RD 0xf02

static unsigned fill_b(void) { return (ptr(B_WR) - ptr(B_RD)) & 0x7ff; }
static unsigned fill_rec(void) { return (ptr(R_WR) - ptr(R_RD)) & 0x7ff; }
static unsigned irq(void) { return (via2[0xa00] >> 4) & 1; }
static void irq_clear(void) { via2[0xa00] = 0x10; }

static void reset(void)
{
	wb(0xf09, 1);
	wb(0xf29, 1);
	wb(0x80a, 0);
	wb(0x801, 0);
	wb(0x803, 0x80);
	wb(0x803, 0);
	irq_clear();
}

static void push(int both, unsigned n, unsigned v)
{
	while (n--) {
		if (both)
			w16(0x1000, v);
		w16(0x1800, v);
	}
}

static void state(const char *label)
{
	out(label);
	out(": B wr ");
	hex(ptr(B_WR), 3);
	out(" rd ");
	hex(ptr(B_RD), 3);
	out(" rec wr ");
	hex(ptr(R_WR), 3);
	out(" rd ");
	hex(ptr(R_RD), 3);
	out(" 804 ");
	hex(rb(0x804), 2);
	out(" irq ");
	dec(irq());
	out("\n");
}

static void regs(void)
{
	static const short masks[] = {
		0x802, 0x805, 0x806, 0x807, 0x808, 0x809, 0x80b, 0x80c, 0x80d, 0x80e,
		0xf04, 0xf05, 0xf06, 0xf07, 0xf08, 0xf0a, 0xf0b, 0xf0c, 0xf0d, 0xf0f,
		0xf10, 0xf13, 0xf17, 0xf24, 0xf25, 0xf26, 0xf27, 0xf28, 0xf2a, 0xf2b,
		0xf2c, 0xf2d, 0xf2f, 0xf30, 0xf37, 0xf09, 0xf29, 0x80a,
	};
	unsigned i, o, old;

	out("== registers after reset\n");
	reset();
	for (o = 0x800; o < 0x810; o++) {
		if (o == 0x804)
			continue;
		hex(o, 3);
		out(" ");
		hex(rb(o), 2);
		out((o & 7) == 7 ? "\n" : "  ");
	}
	out("\n== write ff / 00 / 55, read back\n");
	for (i = 0; i < sizeof(masks) / sizeof(masks[0]); i++) {
		o = masks[i];
		old = rb(o);
		hex(o, 3);
		out(":");
		wb(o, 0xff);
		out(" ");
		hex(rb(o), 2);
		wb(o, 0x00);
		out(" ");
		hex(rb(o), 2);
		wb(o, 0x55);
		out(" ");
		hex(rb(o), 2);
		wb(o, old);
		out("\n");
	}
	wb(0x806, 0xee);
	reset();
	out("== pointer writes (chip off)\n");
	for (o = 0xf00; o < 0xf04; o++)
		wb(o, 0xff);
	for (o = 0xf20; o < 0xf24; o++)
		wb(o, 0xff);
	state("all ff");
	wb(0xf21, 0x10);
	state("B wr low = 10");
	reset();
	state("after clear");
}

static void fifo(void)
{
	static const unsigned marks[] = { 1, 2, 511, 512, 513, 1023, 1024, 1025 };
	unsigned i, n = 0;
	char label[24];

	out("== FIFO B fill, chip off\n");
	reset();
	state("0");
	for (i = 0; i < sizeof(marks) / sizeof(marks[0]); i++) {
		push(0, marks[i] - n, 0);
		n = marks[i];
		label[0] = 0;
		{
			int k = 0;
			unsigned v = n;
			char t[8];
			int j = 0;

			do {
				t[j++] = '0' + v % 10;
				v /= 10;
			} while (v);
			while (j)
				label[k++] = t[--j];
			label[k] = 0;
		}
		state(label);
	}
	out("== FIFO A fill, chip off\n");
	reset();
	for (i = 0; i < 1025; i++)
		w16(0x1000, 0);
	state("A 1025");

	out("== access widths into FIFO B\n");
	reset();
	asc[0x1800] = 0x12;
	state("stb 1800");
	asc[0x1801] = 0x34;
	state("stb 1801");
	w16(0x1800, 0x5678);
	state("sth 1800");
	*(volatile unsigned int *)(asc + 0x1800) = 0x11112222;
	state("stw 1800");
	asc[0x400] = 0x80;
	state("stb 400 (byte window)");
	asc[0x7ff] = 0x80;
	state("stb 7ff (byte window)");

	out("== reads, chip off, not recording\n");
	reset();
	push(0, 4, 0x1234);
	state("4 in B");
	(void)*(volatile unsigned short *)(asc + 0x1800);
	state("lhz 1800");
	(void)asc[0x400];
	state("lbz 400");
	(void)*(volatile unsigned short *)(asc + 0x1000);
	state("lhz 1000");
	reset();
}

static void timing(void)
{
	u64 t0, t1, t;
	unsigned last, cur, total = 0, i, c, lc, laps = 0;
	long rate;

	out("== drain rate: 1024 frames, chip on\n");
	reset();
	push(1, 1024, 0);
	last = ptr(B_RD);
	wb(0x801, 1);
	t0 = tb();
	t1 = t0;
	while (total < 1600) {
		cur = ptr(B_RD);
		total += (cur - last) & 0x7ff;
		last = cur;
		if (total < 400)
			t1 = tb();
		if (tb() - t0 > TB_HZ)
			break;
	}
	t = tb();
	rate = (long)div64((u64)(total - 400) / 2 * TB_HZ, t - t1);
	out("samples/s ");
	dec(rate);
	out("\n");
	reset();

	out("== counter $F2E rate\n");
	push(1, 1024, 0);
	wb(0x801, 1);
	lc = rb(0xf2e);
	t0 = tb();
	for (i = 0; i < 20000; i++) {
		c = rb(0xf2e);
		laps += (c - lc) & 0x3f;
		lc = c;
	}
	t = tb();
	out("counts/s ");
	dec((long)div64((u64)laps * TB_HZ, t - t0));
	out(" over ");
	dec(i);
	out(" reads (laps can hide in stalls)\n");
	reset();
}

/* Poll for the ASC interrupt for up to US microseconds; returns the elapsed microseconds or -1. */
static long wait_irq(ul us)
{
	u64 t0 = tb(), end = t0 + div64((u64)us * TB_HZ, 1000000);

	while (tb() < end)
		if (irq())
			return (long)div64((tb() - t0) * 1000000, TB_HZ);
	return -1;
}

static void report(const char *label, long us)
{
	out(label);
	if (us < 0) {
		out(": no edge, B fill ");
	} else {
		out(": edge after ");
		dec(us / 100 * 100);
		out(" us, B fill ");
	}
	dec(fill_b() / 2);
	out(", 804 ");
	hex(rb(0x804), 2);
	out("\n");
}

static void irqs(void)
{
	long us;

	out("== playback interrupt\n");
	reset();
	push(1, 1024, 0);
	wb(0xf29, 0);
	out("enable with B full: irq ");
	dec(irq());
	out("\n");
	wb(0x801, 1);
	us = wait_irq(100000);
	report("drain from full", us);

	/* The request is still up and nothing has serviced it. */
	irq_clear();
	wait_us(3000);
	out("3 ms after clearing the flag, unserviced: irq ");
	dec(irq());
	out("\n");

	/* Refill above half without touching $804 or $F29. */
	irq_clear();
	push(1, 600, 0);
	report("refill only, then drain", wait_irq(100000));

	/* Refill above half, then read $804. */
	irq_clear();
	push(1, 600, 0);
	(void)rb(0x804);
	report("refill + read 804, then drain", wait_irq(100000));

	/* Mac OS: $F29 = 1, refill, $F29 = 0. */
	irq_clear();
	wb(0xf29, 1);
	push(1, 600, 0);
	wb(0xf29, 0);
	report("F29 1, refill, F29 0, then drain", wait_irq(100000));

	/* Below half with the interrupt off, then enable it. */
	reset();
	push(1, 300, 0);
	wb(0x801, 1);
	wait_us(2000);
	irq_clear();
	wb(0xf29, 0);
	report("enable while below half", wait_irq(2000));

	/* Same, but the flag was raised and never serviced while disabled. */
	reset();
	irq_clear();
	wb(0xf29, 1);
	push(1, 100, 0);
	wb(0x801, 1);
	wait_us(10000);
	irq_clear();
	wb(0xf29, 0);
	report("enable after underrun", wait_irq(2000));
	reset();
}

static void record(void)
{
	unsigned a, b;
	long us;
	u64 t0;

	out("== recording\n");
	reset();
	a = ptr(R_WR);
	wb(0x80a, 1);
	out("80a reads ");
	hex(rb(0x80a), 2);
	out("\n");
	wait_us(10000);
	b = ptr(R_WR);
	out("chip off, 10 ms: rec wr moved ");
	dec((b - a) & 0x7ff);
	out(" bytes\n");
	wb(0x80a, 0);

	reset();
	push(1, 1024, 0);
	wb(0x801, 1);
	a = ptr(R_WR);
	t0 = tb();
	wb(0x80a, 1);
	wait_us(10000);
	b = ptr(R_WR);
	out("chip on, 10 ms: rec wr moved ");
	dec((b - a) & 0x7ff);
	out(" bytes in ");
	dec((long)div64((tb() - t0) * 1000000, TB_HZ) / 100 * 100);
	out(" us\n");
	wb(0x80a, 0);

	out("== record interrupt\n");
	reset();
	push(1, 1024, 0);
	wb(0xf09, 0);
	wb(0x801, 1);
	irq_clear();
	wb(0x80a, 1);
	us = wait_irq(60000);
	out("from empty: ");
	dec(us < 0 ? -1 : us / 100 * 100);
	out(" us, rec fill ");
	dec(fill_rec());
	out(" bytes, 804 ");
	hex(rb(0x804), 2);
	out("\n");

	/* Stop and restart with the FIFO already past half. */
	wb(0x80a, 0);
	wb(0xf09, 1);
	irq_clear();
	push(1, 512, 0);
	wb(0xf09, 0);
	wb(0x80a, 1);
	us = wait_irq(20000);
	out("restart past half: ");
	dec(us < 0 ? -1 : us / 100 * 100);
	out(" us, rec fill ");
	dec(fill_rec());
	out(" bytes, 804 ");
	hex(rb(0x804), 2);
	out("\n");

	out("== record reads\n");
	a = ptr(R_RD);
	(void)*(volatile unsigned short *)(asc + 0x1000);
	b = ptr(R_RD);
	out("lhz 1000 pops ");
	dec((b - a) & 0x7ff);
	a = b;
	(void)*(volatile unsigned int *)(asc + 0x1000);
	b = ptr(R_RD);
	out(", lwz 1000 pops ");
	dec((b - a) & 0x7ff);
	a = b;
	(void)asc[0x000];
	b = ptr(R_RD);
	out(", lbz 000 pops ");
	dec((b - a) & 0x7ff);
	a = b;
	(void)*(volatile unsigned int *)(asc + 0x000);
	b = ptr(R_RD);
	out(", lwz 000 pops ");
	dec((b - a) & 0x7ff);
	out(" bytes\n");
	reset();
}


static void regs_on(void)
{
	static const short list[] = { 0x80b, 0x80f, 0xf04, 0xf0a, 0xf10, 0xf24, 0xf30 };
	unsigned i;

	out("== v2: reads with the chip on\n");
	reset();
	push(1, 1024, 0);
	wb(0x801, 1);
	for (i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
		hex(list[i], 3);
		out(" ");
		hex(rb(list[i]), 2);
		out("  ");
	}
	out("\n");
	reset();
	out("f2a-f2d after clear: ");
	hex(rb(0xf2a), 2);
	hex(rb(0xf2b), 2);
	out(" ");
	hex(rb(0xf2c), 2);
	hex(rb(0xf2d), 2);
	out(", f0a-f0d: ");
	hex(rb(0xf0a), 2);
	hex(rb(0xf0b), 2);
	out(" ");
	hex(rb(0xf0c), 2);
	hex(rb(0xf0d), 2);
	out("\n");
	push(1, 100, 0);
	out("after 100 to A and B: f2a-f2d ");
	hex(rb(0xf2a), 2);
	hex(rb(0xf2b), 2);
	out(" ");
	hex(rb(0xf2c), 2);
	hex(rb(0xf2d), 2);
	out("\n");
	wb(0x801, 1);
	wait_us(2000);
	out("after 2 ms playing: f2a-f2d ");
	hex(rb(0xf2a), 2);
	hex(rb(0xf2b), 2);
	out(" ");
	hex(rb(0xf2c), 2);
	hex(rb(0xf2d), 2);
	out(", B rd ");
	hex(ptr(B_RD), 3);
	out("\n");
	reset();
}

static void status_ab(void)
{
	static const short fills[][2] = {
		{ 0, 0 }, { 600, 0 }, { 0, 600 }, { 600, 100 }, { 100, 600 }, { 1024, 0 }, { 0, 1024 },
		{ 1024, 100 }, { 100, 1024 }, { 1024, 1024 }, { 513, 513 }, { 512, 512 },
	};
	unsigned i;

	out("== v2: $804 against A and B fill, chip off\n");
	for (i = 0; i < sizeof(fills) / sizeof(fills[0]); i++) {
		reset();
		{
			unsigned n;

			for (n = 0; n < (unsigned)fills[i][0]; n++)
				w16(0x1000, 0);
			for (n = 0; n < (unsigned)fills[i][1]; n++)
				w16(0x1800, 0);
		}
		out("A ");
		dec(fills[i][0]);
		out(" B ");
		dec(fills[i][1]);
		out(": 804 ");
		hex(rb(0x804), 2);
		out("\n");
	}
	reset();

	out("== v2: which FIFO drives the playback interrupt\n");
	reset();
	{
		unsigned n;

		for (n = 0; n < 1024; n++)
			w16(0x1000, 0);
		for (n = 0; n < 700; n++)
			w16(0x1800, 0);
	}
	wb(0xf29, 0);
	wb(0x801, 1);
	irq_clear();
	report("A 1024, B 700, F29 on", wait_irq(100000));
	reset();
	{
		unsigned n;

		for (n = 0; n < 700; n++)
			w16(0x1000, 0);
		for (n = 0; n < 1024; n++)
			w16(0x1800, 0);
	}
	wb(0xf29, 0);
	wb(0x801, 1);
	irq_clear();
	report("A 700, B 1024, F29 on", wait_irq(100000));
	reset();
	{
		unsigned n;

		for (n = 0; n < 1024; n++)
			w16(0x1000, 0);
		for (n = 0; n < 1024; n++)
			w16(0x1800, 0);
	}
	wb(0xf09, 0);
	wb(0x801, 1);
	irq_clear();
	report("A and B 1024, only F09 on", wait_irq(100000));
	reset();
}

static void record2(void)
{
	long us;
	unsigned i, st;
	u64 t0;

	out("== v2: recording with the chip off\n");
	reset();
	wb(0xf09, 0);
	irq_clear();
	wb(0x80a, 1);
	us = wait_irq(60000);
	out("chip off, from empty: ");
	dec(us < 0 ? -1 : us / 100 * 100);
	out(" us, rec fill ");
	dec(fill_rec());
	out(" bytes, 804 ");
	hex(rb(0x804), 2);
	out("\n");
	reset();

	out("== v2: record threshold, 5 runs, polling $804 bit 0 (us, rec fill bytes)\n");
	for (i = 0; i < 5; i++) {
		reset();
		push(1, 1024, 0);
		wb(0x801, 1);
		wb(0x80a, 1);
		t0 = tb();
		do {
			st = rb(0x804);
		} while (!(st & 1) && tb() - t0 < TB_HZ / 10);
		dec((long)div64((tb() - t0) * 1000000, TB_HZ) / 100 * 100);
		out(" ");
		dec(fill_rec());
		out("  ");
	}
	out("\n");
	reset();

	out("== v2: record interrupt after reading below half without ack\n");
	reset();
	push(1, 1024, 0);
	wb(0xf09, 0);
	wb(0x801, 1);
	irq_clear();
	wb(0x80a, 1);
	us = wait_irq(60000);
	irq_clear();
	for (i = 0; i < 400; i++)
		(void)*(volatile unsigned short *)(asc + 0x1000);
	us = wait_irq(60000);
	out("read 400 samples, then: ");
	dec(us < 0 ? -1 : us / 100 * 100);
	out(" us, rec fill ");
	dec(fill_rec());
	out(" bytes\n");
	reset();
}

static void counter2(void)
{
	u64 t0, t1;
	unsigned c0, c1, i, best = 0;
	long rate;

	out("== v2: counter, 16 reads 20 us apart against the time base\n");
	reset();
	push(1, 1024, 0);
	wb(0x801, 1);
	for (i = 0; i < 16; i++) {
		t0 = tb();
		c0 = rb(0xf2e);
		wait_us(20);
		c1 = rb(0xf2e);
		t1 = tb();
		rate = (long)div64((u64)((c1 - c0) & 0x3f) * TB_HZ, t1 - t0);
		if (t1 - t0 < TB_HZ * 30 / 1000000) {
			dec(rate);
			out(" ");
			best++;
		}
	}
	out(" (");
	dec(best);
	out(" unstalled)\n");
	t0 = tb();
	for (i = 0; i < 1000; i++)
		(void)rb(0xf2e);
	out("1000 reads take ");
	dec((long)div64((tb() - t0) * 1000000, TB_HZ));
	out(" us\n");
	reset();
}

static void shared(void)
{
	long us;

	out("== v3: one interrupt line for both FIFOs\n");
	reset();
	push(1, 600, 0);
	wb(0x801, 1);
	wb(0xf09, 0);
	irq_clear();
	wb(0x80a, 1);
	us = wait_irq(60000);
	out("record request raised the line: ");
	dec(us < 0 ? -1 : us / 100 * 100);
	out(" us; B fill ");
	dec(fill_b() / 2);
	out(", 804 ");
	hex(rb(0x804), 2);
	out("\n");
	irq_clear();
	wb(0xf29, 0);
	wait_us(200);
	out("enable playback (below half) while the record request holds the line: irq ");
	dec(irq());
	out("\n");
	irq_clear();
	wb(0xf09, 1);
	wait_us(200);
	out("drop the record request, playback still requesting: irq ");
	dec(irq());
	out("\n");
	irq_clear();
	wb(0xf29, 1);
	wb(0xf29, 0);
	wait_us(200);
	out("toggle F29 off and on: irq ");
	dec(irq());
	out("\n");
	irq_clear();
	wb(0xf29, 1);
	wb(0xf09, 1);
	wait_us(200);
	wb(0xf29, 0);
	wb(0xf09, 0);
	wait_us(200);
	out("both off, then both on with both conditions true: irq ");
	dec(irq());
	out("\n");
	reset();
}

void main2(int argc, char **argv)
{
	asc = map(0x50f14000, 0x2000);
	via2 = map(0x50f03000, 0x1000);
	out("ascprobe: version ");
	hex(rb(0x800), 2);
	out("\n");
	if (argc < 2) {
		regs();
		fifo();
		timing();
		irqs();
		record();
	}
	if (argc < 2 || argv[1][1] != '3') {
		regs_on();
		status_ab();
		record2();
		counter2();
	}
	shared();
	wb(0x806, 0xee);
	out("done\n");
	flush();
}

asm(".globl _start\n"
    "_start:\n"
    "	lwz	3,0(1)\n"
    "	addi	4,1,4\n"
    "	clrrwi	1,1,4\n"
    "	stwu	1,-16(1)\n"
    "	bl	main2\n"
    "	li	0,1\n"
    "	li	3,0\n"
    "	sc\n");
