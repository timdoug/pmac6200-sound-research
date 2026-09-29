-- license:BSD-3-Clause
-- copyright-holders:Tim Douglas
-- Diskless pmac6200 regression fixture.  See README.md for invocation and scope.
local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local cuda = machine.devices[":cuda:cudamcu"]
local asc = machine.devices[":primetimeii:asc"]
local space = cpu.spaces["program"]

local function item(device, name)
	for key, index in pairs(device.items) do
		if key:match("/" .. name .. "$") then return emu.item(index) end
	end
	error("Missing saved item: " .. device.tag .. "/" .. name)
end

local function check(condition, message)
	assert(condition, "FAIL: " .. message)
	print("PASS: " .. message)
end

-- Stop guest software, not emulated time or the sound streams.  These internal saved
-- items are deliberately used only by the test fixture, not by the device implementation.
for _, device in ipairs({cpu, cuda}) do
	item(device, "m_suspend"):write(0, 0x10)
	item(device, "m_nextsuspend"):write(0, 0x10)
	item(device, "m_eatcycles"):write(0, 1)
	item(device, "m_nexteatcycles"):write(0, 1)
end

local wrptr = item(asc, "m_wrptr16")
local rdptr = item(asc, "m_rdptr16")
local pending = item(asc, "m_irq_pending")
local function w(reg, data) space:write_u8(0x50f14000 + reg, data) end
local function r(reg) return space:read_u8(0x50f14000 + reg) end
local function sample(ch, data) space:write_u16(0x50f15000 + ch * 0x800, data) end
local function clear()
	w(0xf09, 1); w(0xf29, 1); w(0x80a, 0); w(0x801, 0)
	w(0x803, 0x80); w(0x803, 0); w(0x806, 0xee)
end

check(r(0x800) == 0xbb, "PrimeTime II version")
clear()
w(0x80a, 1)
emu.wait(0.0002)
local data = space:read_u16(0x50f15000)
check(data == 0x8000 and wrptr:read(2) >= 8 and rdptr:read(2) == 2,
	"empty record read catches up and consumes one offset-binary sample")
check(r(0) == 0x80 and rdptr:read(2) == 4, "byte record read consumes a whole sample")

clear()
for i = 1, 100 do sample(1, 0x2000) end
emu.wait(0.0002)
w(0x801, 1)
check(rdptr:read(1) == 0, "enabling does not consume samples retroactively")
check((r(0x804) & 8) == 0, "enabling does not report a nonempty FIFO as empty")
emu.wait(0.0002)
w(0x801, 0)
local stopped = rdptr:read(1)
check(stopped >= 8 and stopped <= 10, "disabling accounts for elapsed playback")
emu.wait(0.0002)
r(0x804)
check(rdptr:read(1) == stopped, "disabled playback does not drain")

clear()
for i = 1, 1024 do sample(1, i) end
check(wrptr:read(1) == 0 and r(0x804) == 0x0a, "full FIFO wraps and sets status")
sample(1, 0x5555)
check(wrptr:read(1) == 0 and r(0x804) == 0x0a, "full FIFO drops writes")
clear()
w(0x400, 0xa0)
check(wrptr:read(1) == 2 and space:read_u16(0x50f15800) == 0x2000,
	"byte window widens offset binary into signed 16-bit samples")
clear()
space:write_u8(0x50f15800, 0x12)
space:write_u8(0x50f15801, 0x34)
check(wrptr:read(1) == 4 and space:read_u16(0x50f15800) == 0x1200,
	"provisional partial-word policy: one sample per bus write, inactive lane zero")

clear()
machine.sounds[":primetimeii:asc"].hook = true
local left_final, left_fallback = 0, 0
local capture_last = true
emu.register_sound_update(function(outputs)
	local channels = outputs[":primetimeii:asc"]
	if capture_last and channels then
		for _, value in ipairs(channels[1]) do
			if math.abs(value - 0.5) < 0.00001 then left_final = left_final + 1 end
			if math.abs(value - 0.25) < 0.00001 then left_fallback = left_fallback + 1 end
		end
	end
end)
sample(0, 0x4000)
for i = 1, 100 do sample(1, 0x2000) end
w(0x80a, 1); w(0x801, 1)
emu.wait(0.025)
capture_last = false
check(left_final == 1 and left_fallback > 0, "last A sample precedes recording fallback to B")

-- IRQ assertions describe the model's independent-source acknowledgement policy.
-- Hardware confirmation of simultaneous playback/record acknowledgement is still needed.
clear()
w(0x80a, 1); w(0xf09, 0)
emu.wait(0.024)
r(0x804)
check(pending:read(0) == 1, "record half-full raises A request with B disabled")
w(0xf29, 1)
check(pending:read(0) == 1, "disabling B preserves A request")
while rdptr:read(2) ~= wrptr:read(2) do space:read_u16(0x50f15000) end
r(0x804)
check(pending:read(0) == 0, "serviced A acknowledges even while disabled B is empty")
w(0xf29, 0)
check(pending:read(0) == 4, "enabling half-empty B raises B request")
w(0xf09, 1)
check(pending:read(0) == 4, "disabling A preserves B request")
w(0xf09, 0)
emu.wait(0.024)
r(0x804)
check(pending:read(0) == 5, "simultaneous record/playback requests coexist")
for i = 1, 513 do sample(1, 0) end
r(0x804)
check(pending:read(0) == 1, "servicing B leaves unserviced A asserted")
w(0xf09, 1)
check(pending:read(0) == 0, "disabling final source clears request")

-- Exercise the real I2C mix-in through Cuda's memory-mapped GPIO, not a register poke.
-- Port B SDA/SCL are open-drain bits 6/7, controlled by their direction bits.
local mcu = cuda.spaces["program"]
local low_ddr = mcu:read_u8(5) & 0x3f
mcu:write_u8(1, mcu:read_u8(1) & 0x3f)
local function lines(sda, scl)
	mcu:write_u8(5, low_ddr | (sda == 0 and 0x40 or 0) | (scl == 0 and 0x80 or 0))
	emu.wait(0.000002) -- allow the wired-AND input merger's synchronized callback to run
end
local function start() lines(1, 1); lines(0, 1); lines(0, 0) end
local function stop() lines(0, 0); lines(0, 1); lines(1, 1) end
local function send(value)
	for bit = 7, 0, -1 do
		local b = (value >> bit) & 1
		lines(b, 0); lines(b, 1); lines(b, 0)
	end
	lines(1, 0); lines(1, 1)
	local ack = (mcu:read_u8(1) & 0x40) == 0
	lines(1, 0)
	return ack
end
local function receive()
	local value = 0
	for bit = 7, 0, -1 do
		lines(1, 1)
		value = (value << 1) | ((mcu:read_u8(1) >> 6) & 1)
		lines(1, 0)
	end
	lines(1, 1); lines(1, 0) -- master NAK
	return value
end
local function dfac_write(reg, value)
	start()
	assert(send(0xde) and send(reg) and send(value), "DFAC write NAK")
	stop()
end
local function dfac_read(reg)
	start()
	assert(send(0xdf) and send(reg), "DFAC read NAK")
	local value = receive()
	stop()
	return value
end
stop()
dfac_write(0x00, 0xff)
check(dfac_read(0x00) == 0x3f, "DFAC single-byte read subaddress and write mask")
start()
check(send(0xde) and not send(0x01), "DFAC absent register NAK")
stop()
start()
check(send(0xde) and send(0x0c) and send(7) and not send(0x55), "DFAC second write byte NAK")
stop()
check(dfac_read(0x0c) == 7, "DFAC extra write does not replace first byte")

-- The conventional I2C client on the same bus must still accept multiple data bytes
-- and a repeated START.  Rewrite its current clock settings to avoid changing video.
local video = machine.devices[":valkyrie"]
local clock_m = item(video, "m_M"):read(0)
local clock_n = item(video, "m_N"):read(0)
local clock_p = item(video, "m_P"):read(0)
start()
check(send(0x50) and send(1) and send(clock_m) and send(clock_n) and send(clock_p),
	"conventional I2C client still accepts multiple data bytes")
stop()
start()
assert(send(0x50) and send(1))
start()
check(send(0x51) and receive() == 0xff, "conventional I2C repeated-start read is unchanged")
stop()

clear()
dfac_write(0x0c, 7); dfac_write(0x0d, 2); dfac_write(0x0e, 0); dfac_write(0x0f, 0x80)
machine.sounds[":dfac2"].hook = true
local nonfinite, peak, count, stale = 0, 0, 0, 0
local capture_stale = false
local capture_response = nil
emu.register_sound_update(function(outputs)
	local channels = outputs[":dfac2"]
	if channels then
		if capture_response then
			for _, value in ipairs(channels[1]) do table.insert(capture_response, value) end
		end
		for _, channel in ipairs(channels) do
			for _, value in ipairs(channel) do
				count = count + 1
				if value ~= value or math.abs(value) == math.huge then
					nonfinite = nonfinite + 1
				else
					peak = math.max(peak, math.abs(value))
					if capture_stale then stale = math.max(stale, math.abs(value)) end
				end
			end
		end
	end
end)
sample(0, 0x4000); sample(1, 0x4000); w(0x801, 1)
emu.wait(0.2)
check(count > 0 and nonfinite == 0 and peak > 0.49 and peak < 0.65,
	"DFAC filter remains finite and bounded at host rate " .. machine.samplerate)
dfac_write(0x0f, 0)
sample(0, 0); sample(1, 0)
emu.wait(0.1)
dfac_write(0x0f, 0x80)
capture_stale = true
emu.wait(0.04)
capture_stale = false
check(stale < 0.00001, "filter bypass does not preserve stale audio")

-- Steady tones through FIFO A, measured in DFAC's output stream at the same frequency:
-- the filtered/dry ratio is the filter's response.  A stationary tone cancels the
-- ASC-to-DFAC resampler, which is time-varying and would not cancel for a single impulse.
-- The sound core renders streams up to a few tens of milliseconds ahead of the emulated
-- time at which a register is written, so each burst follows a silent gap, its onset is
-- located in the capture, and a settled window after that is measured.  The targets are
-- the analog prototype's response, with room for the impulse-invariant discretization.
-- Below a 13.5 kHz host rate the filter is bypassed and this is skipped.
local function magnitude(trace, first, last, frequency)
	local real, imaginary, n = 0, 0, last - first + 1
	for i = first, last do
		local window = 0.5 - 0.5 * math.cos(2 * math.pi * (i - first) / n)
		local phase = 2 * math.pi * frequency * (i - first) / machine.samplerate
		real = real + window * trace[i] * math.cos(phase)
		imaginary = imaginary + window * trace[i] * math.sin(phase)
	end
	return math.sqrt(real * real + imaginary * imaginary)
end
local function tone_level(frequency, filtered)
	clear()
	dfac_write(0x0f, filtered and 0x80 or 0)
	emu.wait(0.03)
	for n = 0, 1023 do
		sample(0, math.floor(0x4000 * math.sin(2 * math.pi * frequency * n / 22050) + 0.5) & 0xffff)
	end
	capture_response = {}
	w(0x801, 1)
	emu.wait(0.06)
	local trace = capture_response
	capture_response = nil
	local onset = 1
	while onset < #trace and math.abs(trace[onset] - trace[1]) < 0.02 do onset = onset + 1 end
	local first = onset + math.floor(0.002 * machine.samplerate)
	local last = first + math.floor(0.020 * machine.samplerate) - 1
	assert(last <= #trace, "tone burst ended before the measurement window")
	return magnitude(trace, first, last, frequency)
end
if machine.samplerate >= 13500 then
	for _, point in ipairs({{1000, -0.204, 0.1}, {6750, -0.500, 0.15}, {8000, -14.647, 0.5}}) do
		local db = 20 * math.log(tone_level(point[1], true) / tone_level(point[1], false), 10)
		check(math.abs(db - point[2]) < point[3],
			string.format("DFAC response at %d Hz: %.3f dB", point[1], db))
	end
else
	print("SKIP: DFAC filter is bypassed below a 13.5 kHz host rate (" .. machine.samplerate .. ")")
end

clear()
sample(0, 0x4000); sample(1, 0x4000); w(0x801, 1)
emu.wait(0.002)
clear()
sample(0, 0x1234); sample(1, 0x5678)
w(0x80a, 1); w(0xf09, 0)
emu.wait(0.024)
r(0x804)
local saved_wr = wrptr:read(2)
local state = machine:buffer_save()
local filter_z = item(machine.devices[":dfac2"], "m_filter_z")
local saved_filter = filter_z:read_block(0, filter_z.size * filter_z.count)
clear()
dfac_write(0x0d, 0)
emu.wait(0.01)
check(machine:buffer_load(state), "save state loads")
check(wrptr:read(0) == 2 and wrptr:read(1) == 2 and wrptr:read(2) == saved_wr
	and pending:read(0) == 1 and space:read_u16(0x50f15800) == 0x5678,
	"partial FIFOs, record pointers and pending IRQ survive save/load")
check(filter_z:read_block(0, filter_z.size * filter_z.count) == saved_filter,
	"DFAC filter history survives save/load")

print("PMAC_SOUND_TEST_PASS")
machine:exit()
