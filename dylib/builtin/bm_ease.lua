--BetterMalody built-in: ease effects (scroll / hs) between two values along a curve, from the cursor position
PluginName='BM Ease effects'
PluginType=0
PluginRequire="5.0.1"

local function mk(inf)   -- inf: ease-in 関数 → { In, Out, In-Out }
	return {
		function(t) return inf(t) end,
		function(t) return 1 - inf(1 - t) end,
		function(t) if t < 0.5 then return 0.5 * inf(2 * t) end return 1 - 0.5 * inf(2 * (1 - t)) end,
	}
end
local quad  = mk(function(t) return t * t end)
local cubic = mk(function(t) return t * t * t end)
local sine  = mk(function(t) return 1 - math.cos(t * math.pi / 2) end)
local expo  = mk(function(t) if t <= 0 then return 0 end return 2 ^ (10 * (t - 1)) end)
local circ  = mk(function(t) return 1 - math.sqrt(1 - t * t) end)
local CURVES = {
	{ 'Linear', function(t) return t end },
	{ 'Quad In', quad[1] }, { 'Quad Out', quad[2] }, { 'Quad In-Out', quad[3] },
	{ 'Cubic In', cubic[1] }, { 'Cubic Out', cubic[2] }, { 'Cubic In-Out', cubic[3] },
	{ 'Sine In', sine[1] }, { 'Sine Out', sine[2] }, { 'Sine In-Out', sine[3] },
	{ 'Expo In', expo[1] }, { 'Expo Out', expo[2] }, { 'Expo In-Out', expo[3] },
	{ 'Circ In', circ[1] }, { 'Circ Out', circ[2] }, { 'Circ In-Out', circ[3] },
}

local function parse(s)
	local f = {}
	for w in tostring(s):gmatch('[^,%s]+') do f[#f + 1] = w end
	local kind = 'scroll'
	if f[1] == 'scroll' or f[1] == 'hs' then kind = f[1]; table.remove(f, 1) end
	local v0, v1, len, div = tonumber(f[1]), tonumber(f[2]), tonumber(f[3]), tonumber(f[4]) or 16
	if not v0 or not v1 or not len or len <= 0 or div < 1 then return nil end
	return kind, v0, v1, len, math.floor(div)
end

local function apply(kind, v0, v1, len, div, curve)
	local n = math.floor(len * div + 0.5)
	if n < 1 then n = 1 end
	if n > 1000 then Editor:ShowMessage('Too many points (' .. n .. '). Max 1000: shorten the range or lower the points per beat') return end
	local start = Editor:GetCurrentBeat()
	local f = CURVES[curve][2]
	Editor:StartBatch()
	for i = 0, n do
		local t = i / n
		local v = v0 + (v1 - v0) * f(t)
		local b = Editor:BeatAdd(start, Editor:MakeBeat(i // div, i % div, div))
		Editor:AddEffect(b, kind, v)
	end
	Editor:FinishBatch()
	Editor:ShowMessage(string.format('%s: %d points, %g -> %g over %g beats (%s)', kind, n + 1, v0, v1, n / div, CURVES[curve][1]))
end

function Run()
	local def = 'scroll, 1, 0.5, 4, 16'
	if tostring(Editor:ChartInfo('mode')) == '5' then def = 'hs, 1, 0.5, 4, 16' end   -- Taiko は hs を使う
	Editor:GetUserInput('Ease effects from the cursor.  [scroll|hs], start, end, length(beats), points per beat\n e.g.  ' .. def, def,
		function(s)
			local kind, v0, v1, len, div = parse(s)
			if not kind then Editor:ShowMessage('Invalid input. Example: scroll, 1, 0.5, 4, 16') return end
			local names = {}
			for i, c in ipairs(CURVES) do names[i] = c[1] end
			Editor:GetUserOption('Curve', names, function(idx)
				apply(kind, v0, v1, len, div, (idx or 0) + 1)
			end)
		end)
end
