-- license:BSD-3-Clause
-- copyright-holders:Tim Douglas
-- pmac6200 regression fixture; generated tone CD, no hard disk.  See README.md.
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
-- Start just after a sound-manager tick so its next 20 ms update cannot hide a late
-- threshold IRQ.  Observe saved state, never a register read that updates the stream.
local function align_irq_test()
	clear()
	emu.wait(0.020 - emu.time() % 0.020 + 0.0001)
	clear()
end
local function full_b()
	for i = 1, 1024 do sample(1, 0) end
	w(0xf29, 0); w(0x801, 1)
end
align_irq_test()
full_b()
emu.wait(510 / 22050)
check(pending:read(0) == 0, "playback IRQ is not early")
emu.wait(2 / 22050)
check(pending:read(0) == 4 and rdptr:read(1) == 0x400,
	"playback IRQ arrives at half empty without register polling")

align_irq_test()
w(0x80a, 1); w(0xf09, 0)
emu.wait(510 / 22050)
check(pending:read(0) == 0, "record IRQ is not early")
emu.wait(2 / 22050)
check(pending:read(0) == 1 and wrptr:read(2) == 0x400,
	"record IRQ arrives at half full with playback off and no register polling")

align_irq_test()
full_b()
emu.wait(0.010)
for i = 1, 200 do sample(1, 0) end
emu.wait(0.014)
check(pending:read(0) == 0, "refilling B postpones its IRQ deadline")
emu.wait(0.010)
check(pending:read(0) == 4, "refilled B interrupts at its new deadline")

align_irq_test()
full_b()
w(0xf20, 6); w(0xf21, 0) -- 768 samples: 256 remain before half empty
emu.wait(254 / 22050)
check(pending:read(0) == 0, "pointer write does not trigger an early IRQ")
emu.wait(2 / 22050)
check(pending:read(0) == 4, "pointer write advances the IRQ deadline")

align_irq_test()
full_b()
emu.wait(0.010)
w(0x801, 0)
emu.wait(0.030)
check(pending:read(0) == 0, "stopped playback cancels the IRQ deadline")
w(0x801, 1)
emu.wait(0.014)
check(pending:read(0) == 4, "resuming playback schedules the remaining samples")

align_irq_test()
w(0x80a, 1); w(0xf09, 0)
emu.wait(0.008)
full_b()
emu.wait(0.016)
check(pending:read(0) == 1, "earlier record deadline leaves playback pending")
w(0xf09, 1)
emu.wait(0.008)
check(pending:read(0) == 4, "later playback deadline survives the record IRQ")

align_irq_test()
full_b()
emu.wait(0.010)
r(0x804) -- synchronize the FIFO before saving its in-flight deadline
local timed_state = machine:buffer_save()
local saved_consumed = rdptr:read(1) / 2
-- Cancel the event, then restore it at the same emulated time.  buffer_load is
-- synchronous inside this Lua timer callback; rewinding time here would leave the
-- current callback's old expiry in force until it returns (see luaengine.cpp FIXME).
clear()
assert(machine:buffer_load(timed_state))
check(pending:read(0) == 0, "loading restores the unexpired IRQ deadline")
emu.wait((510 - saved_consumed) / 22050)
check(pending:read(0) == 0, "restored IRQ is not early")
emu.wait(2 / 22050)
check(pending:read(0) == 4, "restored IRQ fires without register polling")

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
machine.sounds[":lpf_l"].hook = true
machine.sounds[":lpf_r"].hook = true
local stream_rates = {}
local previous_sound_time = nil
local nonfinite, peak, count, stale = 0, 0, 0, 0
local capture_stale = false
local capture_response = nil
local capture_board = nil
emu.register_sound_update(function(outputs)
	local now = emu.time()
	if previous_sound_time and now > previous_sound_time then
		for _, tag in ipairs({":dfac2", ":lpf_l", ":lpf_r"}) do
			stream_rates[tag] = math.floor(#outputs[tag][1] / (now - previous_sound_time) + 0.5)
		end
	end
	previous_sound_time = now
	if capture_board then
		for tag, trace in pairs(capture_board) do
			if outputs[tag] then
				for _, value in ipairs(outputs[tag][1]) do table.insert(trace, value) end
			end
		end
	end
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
local dfac_rate = assert(stream_rates[":dfac2"])
local board_rates = {assert(stream_rates[":lpf_l"]), assert(stream_rates[":lpf_r"])}
print(string.format("Stream rates: DFAC %d, board L/R %d/%d, host %d",
	dfac_rate, board_rates[1], board_rates[2], machine.samplerate))
check(dfac_rate == machine.samplerate and board_rates[1] == machine.samplerate
	and board_rates[2] == machine.samplerate, "filters follow the host output rate")

-- Steady tones through FIFO A, measured in DFAC's output stream at the same frequency:
-- the filtered/dry ratio is the filter's response.  A stationary tone cancels the
-- ASC-to-DFAC resampler, which is time-varying and would not cancel for a single impulse.
-- The sound core renders streams up to a few tens of milliseconds ahead of the emulated
-- time at which a register is written, so each burst follows a silent gap, its onset is
-- located in the capture, and a settled window after that is measured.  The targets are
-- the analog prototype's response, allowing the standard biquad approximation.
-- Use the actual stream rate: an adaptive DFAC stream follows its downstream filters.
local function magnitude(trace, first, last, frequency, rate)
	local real, imaginary, n = 0, 0, last - first + 1
	for i = first, last do
		local window = 0.5 - 0.5 * math.cos(2 * math.pi * (i - first) / n)
		local phase = 2 * math.pi * frequency * (i - first) / rate
		real = real + window * trace[i] * math.cos(phase)
		imaginary = imaginary + window * trace[i] * math.sin(phase)
	end
	return math.sqrt(real * real + imaginary * imaginary) / n
end

-- Play the generated stereo tone disc through the real SCSI/CDDA path.
local function scsi_w(reg, value) space:write_u8(0x50f10000 + reg * 16, value) end
local function scsi_r(reg) return space:read_u8(0x50f10000 + reg * 16) end
scsi_w(3, 2); scsi_w(3, 0) -- reset controller, then NOP
scsi_w(8, 7); scsi_w(9, 0); scsi_w(5, 0xff); scsi_w(7, 0)
local function scsi_command(bytes)
	scsi_w(3, 1); scsi_w(4, 3) -- flush FIFO and select target 3
	for _, byte in ipairs(bytes) do scsi_w(2, byte) end
	scsi_w(3, 0x41); emu.wait(0.001) -- select without ATN, send command
	assert((scsi_r(4) & 7) == 3, "SCSI command did not reach status phase")
	scsi_r(5)
	scsi_w(3, 0x11); emu.wait(0.001) -- receive status and completion message
	local status = scsi_r(2)
	assert(scsi_r(2) == 0, "SCSI command-complete message missing")
	scsi_r(5)
	scsi_w(3, 0x12); emu.wait(0.001) -- accept message and disconnect
	scsi_r(5)
	return status
end
scsi_command({0, 0, 0, 0, 0, 0}) -- clear initial media-change condition
check(scsi_command({0x48, 0, 0, 0, 1, 1, 0, 1, 1, 0}) == 0, "SCSI starts generated audio CD")
clear(); sample(0, 0x2000); sample(1, 0x2000); w(0x801, 1)
dfac_write(0x0f, 0)
local function output_levels()
	emu.wait(0.04)
	capture_board = {[":lpf_l"] = {}, [":lpf_r"] = {}}
	emu.wait(0.04)
	local traces, levels = capture_board, {}
	capture_board = nil
	for ch, tag in ipairs({":lpf_l", ":lpf_r"}) do
		local trace, rate = traces[tag], board_rates[ch]
		local first, last = #trace - math.floor(0.02 * rate) + 1, #trace
		local dc = 0
		for i = first, last do dc = dc + trace[i] end
		levels[ch] = {dc / (last - first + 1), magnitude(trace, first, last, ch == 1 and 400 or 600, rate)}
	end
	return levels
end
local reference = output_levels()
for ch = 1, 2 do
	check(math.abs(reference[ch][1] - 0.25) < 0.002 and reference[ch][2] > 0.04,
		"ASC and CD audio reach output channel " .. ch)
end
local function controls(label, reg_c, reg_d, reg_e, reg_f, left, right, cd_left, cd_right)
	dfac_write(0x0c, reg_c); dfac_write(0x0d, reg_d)
	dfac_write(0x0e, reg_e); dfac_write(0x0f, reg_f)
	local levels = output_levels()
	for ch, expected in ipairs({{left, cd_left}, {right, cd_right}}) do
		assert(math.abs(levels[ch][1] - 0.25 * expected[1]) < 0.002,
			label .. ": DAC level differs on channel " .. ch .. ": " .. levels[ch][1])
		assert(math.abs(levels[ch][2] / reference[ch][2] - expected[2]) < 0.02,
			label .. ": CD level differs on channel " .. ch)
	end
	check(true, label)
end
controls("DAC attenuation leaves CD unchanged", 1, 2, 0, 0, 0.1258925, 0.1258925, 1, 1)
controls("right -9 dB leaves left and CD unchanged", 0x87, 2, 0, 0, 1, 0.3548134, 1, 1)
controls("first -6 dB control affects only DAC", 7, 6, 0, 0, 0.5, 0.5, 1, 1)
controls("second -6 dB control affects only DAC", 7, 10, 0, 0, 0.5, 0.5, 1, 1)
controls("both -6 dB controls combine", 7, 14, 0, 0, 0.25, 0.25, 1, 1)
controls("DAC disable leaves CD playing", 7, 0, 0, 0, 0, 0, 1, 1)
controls("DFAC filter leaves CD unchanged", 7, 2, 0, 0x80, 1, 1, 1, 1)
controls("left mute silences DAC and CD", 7, 0x42, 0, 0, 0, 1, 0, 1)
controls("right mute silences DAC and CD", 7, 0x82, 0, 0, 1, 0, 1, 0)
controls("master mute silences DAC and CD", 7, 2, 0x80, 0, 0, 0, 0, 0)
controls("prepare nondefault controls for save/load", 1, 0x82, 0, 0x80, 0.1258925, 0, 1, 0)
local control_state = machine:buffer_save()
dfac_write(0x0c, 7); dfac_write(0x0d, 2); dfac_write(0x0e, 0x80); dfac_write(0x0f, 0)
assert(machine:buffer_load(control_state))
local restored = output_levels()
check(math.abs(restored[1][1] - 0.25 * 0.1258925) < 0.002 and math.abs(restored[2][1]) < 0.002
	and math.abs(restored[1][2] / reference[1][2] - 1) < 0.02 and restored[2][2] < 0.00001,
	"volume, channel and master mute controls restore audible output on save/load")
check(scsi_command({0x4b, 0, 0, 0, 0, 0, 0, 0, 0, 0}) == 0, "SCSI pauses audio CD")
dfac_write(0x0c, 7); dfac_write(0x0d, 2); dfac_write(0x0e, 0)
sample(0, 0); sample(1, 0); emu.wait(0.04)

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
	while onset < #trace and math.abs(trace[onset] - trace[1]) < 0.0001 do onset = onset + 1 end
	local first = onset + math.floor(0.002 * dfac_rate)
	local last = first + math.floor(0.020 * dfac_rate) - 1
	assert(last <= #trace, "tone burst ended before the measurement window")
	return magnitude(trace, first, last, frequency, dfac_rate)
end
if dfac_rate >= 13500 then
	-- At ordinary host rates, compare to the measured analog fit.  At 22.05 kHz,
	-- frequency warping is large: check the known approximation, not analog fidelity.
	local points = dfac_rate == 22050 and {{1000, -0.36, 0.1}, {6750, -8.19, 0.3}, {8000, -34.18, 0.5}}
		or {{1000, -0.204, 0.1}, {6750, -0.500, 1.6}, {8000, -14.647, 3.0}}
	for _, point in ipairs(points) do
		local db = 20 * math.log(tone_level(point[1], true) / tone_level(point[1], false), 10)
		check(math.abs(db - point[2]) < point[3],
			string.format("DFAC response at %d Hz: %.3f dB", point[1], db))
	end
else
	print("SKIP: DFAC response at low host rate (stock stages bypass above Nyquist): " .. dfac_rate)
end

-- Isolate the board's always-on two-pole filter from the DAC/resampler and switchable
-- DFAC filter.  Compare each output to the DFAC input tone, including near 11 kHz.
-- Allow the standard RC device's approximation error, which grows near Nyquist.
local function board_level(trace, frequency, rate)
	local onset = 1
	while onset < #trace and math.abs(trace[onset] - trace[1]) < 0.002 do onset = onset + 1 end
	local first = onset + math.floor(0.005 * rate)
	local last = first + math.floor(0.020 * rate) - 1
	assert(last <= #trace, "board tone burst ended before the measurement window")
	return magnitude(trace, first, last, frequency, rate)
end
dfac_write(0x0f, 0)
local board_frequencies = {}
for _, frequency in ipairs({1000, 6750, 8000, 9000, 10500}) do
	if frequency < dfac_rate / 2 then table.insert(board_frequencies, frequency) end
end
local board_tolerance = dfac_rate >= 96000 and 0.4 or dfac_rate >= 44100 and 1.7
	or dfac_rate >= 22050 and 5.6 or 1.0
for _, frequency in ipairs(board_frequencies) do
	clear()
	sample(0, 0); sample(1, 0); w(0x801, 1); emu.wait(0.03)
	clear(); emu.wait(0.03)
	for n = 0, 1023 do
		local value = math.floor(0x4000 * math.sin(2 * math.pi * frequency * n / 22050) + 0.5) & 0xffff
		sample(0, value); sample(1, value)
	end
	capture_board = {[":dfac2"] = {}, [":lpf_l"] = {}, [":lpf_r"] = {}}
	w(0x801, 1); emu.wait(0.065)
	local traces = capture_board
	capture_board = nil
	local input = board_level(traces[":dfac2"], frequency, dfac_rate)
	local target = -10 * math.log((1 + (frequency / 2270)^2) * (1 + (frequency / 11723)^2), 10)
	for ch, tag in ipairs({":lpf_l", ":lpf_r"}) do
		local db = 20 * math.log(board_level(traces[tag], frequency, board_rates[ch]) / input, 10)
		check(math.abs(db - target) < board_tolerance,
			string.format("board filter %s at %d Hz: %.3f dB (target %.3f)", tag, frequency, db, target))
	end
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
local dfac_state = {}
for tag, device in pairs(machine.devices) do
	if tag == ":dfac2" or tag:match("^:dfac2:") then
		for name, index in pairs(device.items) do
			if name:match("/m_w[012]$") or name:match("/m_gain$") or name:match("/m_regs$")
				or name:match("/m_output_channel_gain$") or name:match("/route_gain$") then
				local value = emu.item(index)
				table.insert(dfac_state, {value, value:read_block(0, value.size * value.count)})
			end
		end
	end
end
assert(#dfac_state >= 27, "missing DFAC filter/control saved items")
local board_state = {}
for _, tag in ipairs({":lpf1_l", ":lpf_l", ":lpf1_r", ":lpf_r"}) do
	local value = item(machine.devices[tag], "m_memory")
	table.insert(board_state, {value, value:read_block(0, value.size * value.count)})
end
clear()
dfac_write(0x0c, 1); dfac_write(0x0d, 0); dfac_write(0x0e, 0x80); dfac_write(0x0f, 0x80)
emu.wait(0.01)
check(machine:buffer_load(state), "save state loads")
check(wrptr:read(0) == 2 and wrptr:read(1) == 2 and wrptr:read(2) == saved_wr
	and pending:read(0) == 1 and space:read_u16(0x50f15800) == 0x5678,
	"partial FIFOs, record pointers and pending IRQ survive save/load")
for _, saved in ipairs(dfac_state) do
	assert(saved[1]:read_block(0, saved[1].size * saved[1].count) == saved[2], "DFAC filter/control state changed")
end
print("PASS: DFAC stock filter history, registers and volume state survive save/load")
for _, saved in ipairs(board_state) do
	assert(saved[1]:read_block(0, saved[1].size * saved[1].count) == saved[2], "board filter history changed")
end
print("PASS: board filter history survives save/load")

print("PMAC_SOUND_TEST_PASS")
machine:exit()
