--BetterMalody built-in: img2hold (Slide) - turn an image into a field of 1-wide holds
-- Same idea as the web Slide editor's img2hold: the image is stretched to 256 (x) by height (ticks), every vertical run of dark pixels becomes one hold.
-- The first row of ticks starts at the current beat (move the cursor to where the picture should start).
PluginName='BM Image to holds (img2hold)'
PluginType=0
PluginRequire="5.0.1"

local HELP = 'length(beats) ticks-per-beat threshold(0-255) invert(0/1)\n e.g. "4 32 128 0"  (dark pixels = holds)'

local function beat_of(t, ompu) return { beat = t // ompu, numor = t % ompu, denom = ompu } end

-- add one 1-wide hold: head tick (absolute, in 1/ompu beats), x (0-255), duration in ticks
local function add_hold(head, x, dur, ompu)
	local id = Editor:AddNote(2048)
	Editor:SetNoteBeat(id, beat_of(head, ompu), true)
	Editor:SetNoteX(id, x)
	Editor:SetNoteWidth(id, 1)
	Editor:AddNoteSlideBody(id, beat_of(dur, ompu))
	return id
end

local function build(mode, bytes, len, ompu, shiki, inv)
	local cb = Editor:GetCurrentBeat()
	local base = math.floor((cb.beat + cb.numor / cb.denom) * ompu + 0.5)
	local h = math.floor(len * ompu + 0.5)
	if h < 1 then Editor:ShowMessage('Length is too short') return end
	local W = 256
	local count = 0
	if mode == 1 or mode == 2 then      -- ignore the image: one hold at x=0, or every lane
		local dur = math.max(1, h)
		local from, to = 0, 0
		if mode == 2 then to = W - 1 end
		for x = from, to do add_hold(base, x, dur, ompu) count = count + 1 end
	else
		local gray = Editor:DecodeImage(bytes, W, h)
		if not gray then Editor:ShowMessage('Could not read the image') return end
		local byte = string.byte
		for x = 0, W - 1 do
			local runStart, runEnd = nil, nil            -- rows (from the top) of the current run, scanning bottom to top
			for i = h - 1, -1, -1 do
				local dark = false
				if i >= 0 then
					local v = byte(gray, i * W + x + 1)
					dark = (v < shiki)
					if inv then dark = not dark end
				end
				if dark then
					if not runStart then runStart = i end
					runEnd = i
				elseif runStart then
					local head = (h - runStart) - 1 + base
					local d = runStart - runEnd
					if d < 1 then d = 1 end
					add_hold(head, x, d, ompu) count = count + 1
					runStart = nil
				end
			end
		end
	end
	if count == 0 then Editor:ShowMessage('No dark pixels found (try the threshold or invert)') return end
	Editor:ShowMessage('Added ' .. count .. ' holds')
end

local function ask_params(mode, bytes)
	if mode ~= 0 then
		Editor:GetUserInput('length(beats) ticks-per-beat\n e.g. "4 32"', '4 32', function(s)
			local len, ompu = s:match('^%s*([%d%.]+)%s+(%d+)')
			len, ompu = tonumber(len), tonumber(ompu)
			if not len or not ompu or ompu < 1 then Editor:ShowMessage('Bad input') return end
			build(mode, nil, len, math.floor(ompu), 128, false)
		end)
		return
	end
	Editor:GetUserInput(HELP, '4 32 128 0', function(s)
		local a, b, c, d = s:match('^%s*([%d%.]+)%s+(%d+)%s+(%d+)%s*(%d*)')
		local len, ompu, shiki = tonumber(a), tonumber(b), tonumber(c)
		if not len or not ompu or not shiki or ompu < 1 then Editor:ShowMessage('Bad input') return end
		build(0, bytes, len, math.floor(ompu), shiki, d == '1')
	end)
end

function Run()
	Editor:GetUserOption('img2hold', { 'Image to holds', 'One hold at x=0 (no image)', 'Fill every lane with holds (no image)' }, function(mode)
		if mode == 0 then
			Editor:ReadBytesSelect('Choose an image', function(bytes)
				if not bytes then return end
				ask_params(0, bytes)
			end)
		else
			ask_params(mode, nil)
		end
	end)
end
