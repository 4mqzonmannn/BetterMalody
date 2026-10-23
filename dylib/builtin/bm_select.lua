--BetterMalody built-in: select notes by a filter (type / lane / x / width / beat range)
PluginName='BM Select by filter'
PluginType=0
PluginRequire="5.0.1"

-- terms (AND).  A term is  name[,name...]  (OR)  or  key=list  (list: 1,3,5-7)
--  Key/Taiko: tap long | Slide: wipe slide | Taiko: don ka big small roll balloon
--  lane=1,3 (Key, 1-based)   x=10-50 (Slide)   w=3-8 (Slide width)   beat=4-8 (head beat)
--  a leading ! negates a term
local HELP = 'Filter terms (all must match):\n tap long | wipe slide | don ka big small roll balloon\n lane=1,3 | x=10-50 | w=3-8 | beat=4-8   ( ! = not )\n e.g.  long lane=1,2'

local function inlist(v, list)
	for part in tostring(list):gmatch('[^,]+') do
		local a, b = part:match('^(-?[%d%.]+)%-(-?[%d%.]+)$')
		if a then if v >= tonumber(a) and v <= tonumber(b) then return true end
		elseif tonumber(part) == v then return true end
	end
	return false
end
local NAMES = {
	tap = function(n) return n.type == 1 end, long = function(n) return n.type == 128 end,
	wipe = function(n) return n.type == 1024 end, slide = function(n) return n.type == 2048 end,
	don = function(n) return n.flag == 0 or n.flag == 1 end, ka = function(n) return n.flag == 2 or n.flag == 3 end,
	big = function(n) return n.flag == 1 or n.flag == 3 or n.flag == 5 or n.flag == 7 end,
	small = function(n) return n.flag == 0 or n.flag == 2 or n.flag == 4 or n.flag == 6 end,
	roll = function(n) return n.flag == 4 or n.flag == 5 end, balloon = function(n) return n.flag == 6 or n.flag == 7 end,
}
local function compile(s)
	local terms = {}
	for w in tostring(s):gmatch('%S+') do
		local neg = false
		if w:sub(1, 1) == '!' then neg = true; w = w:sub(2) end
		local k, v = w:match('^(%a+)=(.+)$')
		local fn
		if k then
			if k == 'lane' then fn = function(n) return inlist(n.x + 1, v) end
			elseif k == 'x' then fn = function(n) return inlist(n.x, v) end
			elseif k == 'w' then fn = function(n) return inlist(n.w, v) end
			elseif k == 'beat' then fn = function(n) return inlist(n.beat, v) end end
		else
			local alts = {}
			for name in w:gmatch('[^,]+') do alts[#alts + 1] = NAMES[name:lower()] end
			for _, a in ipairs(alts) do if not a then alts = nil break end end
			if alts and #alts > 0 then fn = function(n) for _, a in ipairs(alts) do if a(n) then return true end end return false end end
		end
		if not fn then return nil, w end
		terms[#terms + 1] = neg and function(n) return not fn(n) end or fn
	end
	if #terms == 0 then return nil, '' end
	return terms
end
local function info(id)
	local b = Editor:GetNoteBeat(id, true)
	return { id = id, type = Editor:GetNoteType(id), flag = Editor:GetNoteFlag(id), x = Editor:GetNoteX(id), w = Editor:GetNoteWidth(id), beat = b.beat + b.numor / b.denom }
end
local function match(n, terms)
	for _, t in ipairs(terms) do if not t(n) then return false end end
	return true
end

function Run()
	Editor:GetUserInput(HELP, '', function(s)
		local terms, bad = compile(s)
		if not terms then Editor:ShowMessage('Unknown term: "' .. tostring(bad) .. '"') return end
		Editor:GetUserOption('Apply', { 'Replace selection', 'Add to selection', 'Keep only (narrow selection)' }, function(mode)
			local ids = {}
			if mode == 2 then
				local sel = Editor:GetSelectNotes()
				for i = 0, sel.Length - 1 do ids[#ids + 1] = sel[i] end
			else
				for i = 0, Editor:GetNoteCount() - 1 do ids[#ids + 1] = Editor:GetNoteAt(i) end
			end
			local hit = {}
			for _, id in ipairs(ids) do if match(info(id), terms) then hit[#hit + 1] = id end end
			if mode ~= 1 then Editor:ClearSelection() end
			for _, id in ipairs(hit) do Editor:SelectNote(id) end
			Editor:ShowMessage('Selected ' .. #hit .. ' note(s)')
		end)
	end)
end
