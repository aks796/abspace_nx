-- abs_orbital.lua -- Angry Birds Orbital Escapade's two worlds as two more
-- planets: Vege-toids and Omelettification.
--
-- Orbital Escapade (ShadowBird81) is a mod of the PC edition of Angry Birds
-- Space 1.4; its new worlds are the two episodes in its levels/theme1 and
-- levels/theme2, played with five prototype birds (Iron, Pink, Boomerang,
-- Black Hole, Drill). The player copies the mod's data folder to
-- orbital/data; abs_orbital.c serves its files (its levels in the game's own
-- container, its sprite sheets cut down to what the game lacks, its sounds)
-- and hands this script its definitions (math.frexp, see abs_lua.c).
--
-- Beside the game's own content, which stays as it is, this adds:
--   * two episodes after the game's (actions.myLevelFolders: oe_vegetoids,
--     oe_omelettification), their levels -- named behind a prefix, OEV_ /
--     OEO_, as the game keeps scores by name -- pages, metadata, star limits;
--   * their themes, each loading the sprites its levels need: the mod's new
--     ones (a group of its own, served from images/oe) and the game's groups
--     its levels borrow from;
--   * the blocks, birds, particles, materials, damage factors, actions and
--     sounds the game lacks;
--   * two planets on the episode carousel after Danger Zone.
-- While a level of these worlds is played, and only then: the mod's
-- versions of the game's birds (Big Red's shockwave on landing, the Black
-- and Ice birds' power after a hit, ...), the mod's boss logic, and the
-- prototype birds' powers, ported from the mod's scripts.
--
-- The game rebuilds `actions` at every level load (initializeEventSystem):
-- what lives there is put back each time. Everything is guarded: a missing
-- piece leaves the worlds out or a power unused, never breaks the game. MIT.

local O = {errors = {}, notes = {}, new_blocks = {}}
__abs_orbital = O

local type, pairs, ipairs, pcall, tostring, tonumber, next = type, pairs, ipairs, pcall, tostring, tonumber, next
local sfmt, concat, floor, sqrt, atan2, cos, sin = string.format, table.concat, math.floor, math.sqrt, math.atan2,
  math.cos, math.sin

local function ask(op, arg)
  local ok, r = pcall(math.frexp, op, arg or '')
  if ok then return r end
  return nil
end

O.present = ask('orb:present', '') == '1'

local seen_errors = {}
local function err(what)
  what = tostring(what)
  if seen_errors[what] then return end -- once each: a power fails every frame
  seen_errors[what] = true
  O.errors[#O.errors + 1] = what
end
local function note(what) O.notes[#O.notes + 1] = tostring(what) end

local WORLDS = {
  {folder = 'oe_vegetoids', mod = 'theme1', prefix = 'OEV_', number = 'V', name = 'Vege-toids',
   boss = 'LevelBossTheme1', planet = 'PLANET_ORANGE', sign = 'PLANET_SIGN_VEGETOIDS'},
  {folder = 'oe_omelettification', mod = 'theme2', prefix = 'OEO_', number = 'O', name = 'Omelettification',
   boss = 'LevelBossTheme2', planet = 'PLANET_PURPLE', sign = 'PLANET_SIGN_OMELETTIFICATION'},
}
O.worlds = WORLDS
O.served = {} -- every sheet file of the mod's put in a group (OE_<file>, OEF_<file>)

-- ------------------------------------------------------------ helpers
local function copy(v, seen)
  if type(v) ~= 'table' then return v end
  seen = seen or {}
  if seen[v] then return seen[v] end
  local t = {}
  seen[v] = t
  for k, x in pairs(v) do t[copy(k, seen)] = copy(x, seen) end
  return t
end

-- A mod data file run in a table of its own: it sees only Lua's libraries.
local SAFE = {math = math, string = string, table = table, pairs = pairs, ipairs = ipairs, type = type,
              tostring = tostring, tonumber = tonumber, next = next, select = select, unpack = unpack,
              print = function() end}
local function modfile(path)
  local f = ask('orb:load', path)
  if type(f) ~= 'function' then err(path .. ': ' .. tostring(f)) return nil end
  local env = setmetatable({}, {__index = SAFE})
  setfenv(f, env)
  local ok, e = pcall(f)
  if not ok then err(path .. ': ' .. tostring(e)) end
  return env
end

-- every definition list of a block file: {definition = ...} tables
local function defs_of(env, into)
  for _, v in pairs(env or {}) do
    if type(v) == 'table' then
      for _, d in ipairs(v) do
        if type(d) == 'table' and type(d.definition) == 'string' and into[d.definition] == nil then
          into[d.definition] = d
        end
      end
    end
  end
end

-- the strings in a table, and among them the upper-case names (sprites, and
-- other names the sheet lookups pass over)
local function strings_in(v, strs, depth)
  depth = depth or 0
  if depth > 8 then return end
  if type(v) == 'string' then
    strs[v] = true
  elseif type(v) == 'table' then
    for _, x in pairs(v) do strings_in(x, strs, depth + 1) end
  end
end
local function upper(strs)
  local t = {}
  for s in pairs(strs) do
    if s:match('^[A-Z][A-Z0-9_]+$') then t[s] = true end
  end
  return t
end

local function csv(set)
  local t = {}
  for k in pairs(set) do t[#t + 1] = k end
  table.sort(t)
  return concat(t, ',')
end

local function split(s)
  local t = {}
  for w in tostring(s or ''):gmatch('[^,]+') do t[#t + 1] = w end
  return t
end

local function has_game(f) return type(f) == 'function' end

-- ------------------------------------------------------------ the mod
local MOD = {}
local THEMES = {} -- the mod's themes these worlds use (register_themes)

-- the arguments of a createAudio(...) line of the mod's sound manager
local function audio_args(rest)
  local a = {}
  for v in (rest .. ','):gmatch('%s*([^,]*),') do a[#a + 1] = v:match('^%s*(.-)%s*$') end
  return a
end

-- The mod's action scripts, each in a table of its own (as the PC game
-- keeps them) that reads the game's globals: `gamelua` is the game, with a
-- few of the PC game's functions this one lacks.
local SHIM = {}
local GAME = setmetatable({}, {
  __index = function(_, k)
    local v = SHIM[k]
    if v ~= nil then return v end
    return _G[k]
  end,
  __newindex = function(_, k, v) _G[k] = v end,
})
local ACTION_FILES = {'actions', 'actions_birds', 'actions_boss1', 'actions_boss2', 'actions_water',
                      'actions_radiation', 'actions_pigEmotions', 'actions_bonus'}
-- the mod's boss logic, under the names of the game's own (the Cold Cuts
-- boss): the game's while its levels play, the mod's in these worlds
local WORLD_ACTION_FILES = {actions_boss1 = true, actions_boss2 = true}

local function read_action_file(f)
  local fn = ask('orb:load', 'scripts/' .. f .. '.lua')
  if type(fn) ~= 'function' then return end
  local env = setmetatable({gamelua = GAME}, {__index = GAME})
  setfenv(fn, env)
  local ok, e = pcall(fn)
  if not ok then err(f .. ': ' .. tostring(e)) end
  if type(rawget(env, 'Initialize')) == 'function' then pcall(env.Initialize) end
  for k, v in pairs(env) do
    if type(v) == 'table' and type(v.action) == 'function' and type(v.params) == 'table' then
      MOD.actions[k] = v -- the last file's wins, as in the PC game
      if WORLD_ACTION_FILES[f] then MOD.world_actions[k] = v end
    end
  end
end

local BLOCK_FILES = {'birds', 'blocks_asteroids', 'blocks_bonusLevels', 'blocks_bosses', 'blocks_comic',
                     'blocks_compound', 'blocks_decorations', 'blocks_gameElements', 'blocks_glass', 'blocks_hazard',
                     'blocks_items', 'blocks_levelgoals', 'blocks_menu', 'blocks_planets', 'blocks_seasonBlocks',
                     'blocks_sensors', 'blocks_static', 'blocks_stone', 'blocks_wood'}

local function read_tables()
  MOD.starLimits = modfile('scripts/starLimits.lua') or {}
  MOD.themes = modfile('scripts/themes.lua') or {}
  MOD.particles = (modfile('scripts/particles.lua') or {}).particles or {}
  MOD.materials = modfile('scripts/materials.lua') or {}
  MOD.damage = modfile('scripts/damagefactors.lua') or {}
  MOD.blocks, MOD.birds, MOD.actions, MOD.world_actions = {}, {}, {}, {}
end

local function read_block_file(f)
  local env = modfile('scripts/' .. f .. '.lua')
  if f == 'birds' then
    for _, d in ipairs(env and env.birds or {}) do
      if type(d) == 'table' and type(d.definition) == 'string' then MOD.birds[d.definition] = d end
    end
  end
  defs_of(env, MOD.blocks)
end

-- the sounds: the createAudio lines of its sound manager, read as text
-- (run, its functions would write into the game's tables)
local function read_sounds()
  MOD.sounds = {}
  local text = ask('orb:text', 'scripts/soundManager.lua')
  if type(text) ~= 'string' then return end
  for path, name, rest in text:gmatch('createAudio%(%s*audioPath%s*%.%.%s*"([^"]+)"%s*,%s*"([^"]+)"([^)\n]*)%)') do
    local a = audio_args((rest:gsub('^%s*,', '')))
    MOD.sounds[name] = {path = path, flag = a[1] ~= 'false' and a[1] ~= 'nil',
                        channel = tonumber(a[2]) or 3, volume = tonumber(a[3]) or 1}
  end
end

-- a level: its theme and its objects' definitions
local function read_level(W, n)
  local env = modfile('levels/' .. W.mod .. '/' .. n .. '.lua')
  local defs = {}
  for _, o in pairs(env and env.world or {}) do
    if type(o) == 'table' and type(o.definition) == 'string' then defs[o.definition] = true end
  end
  W.info[n] = {theme = env and env.theme, defs = defs}
end

-- ------------------------------------------------------------ registration
local function level_name(W, n)
  if n == W.boss then return W.prefix .. 'Boss' end
  if n == 'LevelSelection' then return n end
  -- the mod's comics are its own, drawn anew under the game's names: its
  -- worlds have comics of their own, still named LevelComic...
  if n:find('^LevelComic') then return 'LevelComic' .. W.prefix .. n:sub(11) end
  return W.prefix .. n
end

-- the mod's metadata for one of its levels, with the game's for the same
-- level filling what it lacks
local function level_meta(n, game_lm)
  local lm = MOD.levelOrder.myLevelsMetadata or {}
  local m = lm[n]
  if type(m) ~= 'table' then return nil end
  m = copy(m)
  m.levelAchievement = nil
  m.nextButtonURL = nil
  m.doesNotchangeGlobalTheme = true -- the main menu's background stays the game's
  local g = game_lm and game_lm[n]
  if type(g) == 'table' then
    for _, k in ipairs({'additionalLevelTime', 'isLevelFailedImageFloat'}) do
      if m[k] == nil then m[k] = copy(g[k]) end
    end
  end
  return m
end

local function build_episodes()
  local lo = MOD.levelOrder
  local game_lm = type(actions) == 'table' and actions.myLevelsMetadata or {}
  for _, W in ipairs(WORLDS) do
    if type(W.source) ~= 'table' then err('no level list for ' .. W.mod) return false end
    local list = {}
    for i, n in ipairs(W.source) do list[i] = level_name(W, n) end
    W.levels = list
    W.pages = copy((lo.myPages or {})[W.mod]) or {11, 21}
    local em = copy((lo.myEpisodesMetadata or {})[W.mod]) or {}
    for _, k in ipairs({'leaderBoard', 'scoreAchievement', 'starAchievement', 'threeStarsReward',
                        'allFeathersGainedReward', 'antennaEggLevelIndexes', 'featherLevelIndex',
                        'featherLevelPage', 'levelsAvailableInFree'}) do em[k] = nil end
    em.levelNumberPrefix = W.number
    W.episode_meta = em
    -- the levels' metadata: the mod's for its own levels (the boss, a comic
    -- of its own); what it shares with the game (comics, the level
    -- selection) keeps the game's
    W.level_meta = {}
    for i, n in ipairs(W.source) do
      local new = list[i]
      local mm = (lo.myLevelsMetadata or {})[n]
      if type(mm) == 'table' and mm.type == 'comic' and type(mm.comicFrames) == 'table' then
        -- a comic: the game's own of that name (or its first) as it runs
        -- them, with the mod's frames
        local m = copy(game_lm[n] or game_lm.LevelComic1) or {type = 'comic'}
        if not game_lm[n] then m.episodeEndComic, m.showEpisodeCompleteDialog = mm.episodeEndComic, nil end
        m.comicFrames = copy(mm.comicFrames)
        -- each frame is a child of the comic page by its name, and the
        -- game's pages refuse a name twice (the mod's first comic names two
        -- pictures frame1, which the PC game allowed)
        local seen = {closeButton = true}
        for i, f in ipairs(m.comicFrames) do
          if type(f) == 'table' then
            local nm = tostring(f.name or 'frame')
            while seen[nm] do nm = nm .. '_' .. i end
            seen[nm], f.name = true, nm
          end
        end
        m.levelAchievement, m.doesNotchangeGlobalTheme = nil, true
        W.level_meta[new] = m
      elseif new ~= n or game_lm[n] == nil then
        W.level_meta[new] = level_meta(n, game_lm) or {doesNotchangeGlobalTheme = true}
      end
    end
    -- star limits under the new names
    for i, n in ipairs(W.source) do
      local v = MOD.starLimits[n]
      if type(v) == 'table' and list[i] ~= n then
        local gold = tonumber(v.goldScore) or 0
        starTable[list[i]] = {silverScore = tonumber(v.silverScore) or gold, goldScore = gold,
                              eagleScore = tonumber(v.eagleScore) or floor(gold * 1.25)}
      end
    end
  end
  return true
end

local function in_world()
  local f = type(levelFolder) == 'string' and levelFolder or ''
  for _, W in ipairs(WORLDS) do
    if f:find(W.folder, 1, true) then return W end
  end
  return nil
end

-- into the game's `actions` (rebuilt at every level load): the episodes, and
-- the mod's actions the game lacks -- in these worlds also its boss logic
local ours = {}
local function apply_actions()
  if type(actions) ~= 'table' or type(actions.myLevelFolders) ~= 'table' or not O.ok then return end
  for _, W in ipairs(WORLDS) do
    local idx
    for i, f in ipairs(actions.myLevelFolders) do if f == W.folder then idx = i end end
    if not idx then
      actions.myLevelFolders[#actions.myLevelFolders + 1] = W.folder
      idx = #actions.myLevelFolders
    end
    W.index = idx
    if type(actions.myLevels) == 'table' then actions.myLevels[W.folder] = W.levels end
    if type(actions.myPages) == 'table' then actions.myPages[W.folder] = W.pages end
    if type(actions.myEpisodesMetadata) == 'table' then actions.myEpisodesMetadata[W.folder] = W.episode_meta end
    if type(actions.myLevelsMetadata) == 'table' then
      for n, m in pairs(W.level_meta) do actions.myLevelsMetadata[n] = m end
    end
  end
  for k, a in pairs(MOD.actions or {}) do
    if actions[k] == nil or actions[k] == ours[k] then
      actions[k] = a
      ours[k] = a
    end
  end
  if in_world() then
    for k, a in pairs(MOD.world_actions or {}) do actions[k] = a end
  end
end

local function register_blocks()
  local blocks = blockTable.blocks
  local added = 0
  for name, d in pairs(MOD.blocks) do
    if blocks[name] == nil then
      blocks[name] = copy(d)
      O.new_blocks[name] = true
      added = added + 1
    end
  end
  -- For these worlds only: the mod's definitions of the game's blocks and
  -- birds its levels use (and of those they name: projectiles, spawned
  -- objects), where they differ -- the levels were built with them (gravity
  -- fields a third as strong, bigger portals, pickups, boss waypoints that
  -- are sensors, the mod's birds) -- and the game's damage factors with what
  -- the mod adds to them.
  O.over_blocks, O.over_damage = {}, {}
  local used, todo = {}, {}
  for name in pairs(MOD.birds) do todo[#todo + 1] = name end
  for _, W in ipairs(WORLDS) do
    for _, info in pairs(W.info) do
      for d in pairs(info.defs) do todo[#todo + 1] = d end
    end
  end
  while #todo > 0 do
    local name = table.remove(todo)
    if not used[name] then
      used[name] = true
      local strs = {}
      strings_in(MOD.blocks[name], strs)
      for s in pairs(strs) do
        if not used[s] and MOD.blocks[s] then todo[#todo + 1] = s end
      end
    end
  end
  local function same(a, b, depth)
    if type(a) ~= 'table' or type(b) ~= 'table' then return a == b end
    if depth > 8 then return true end
    for k, v in pairs(a) do if not same(v, b[k], depth + 1) then return false end end
    for k in pairs(b) do if a[k] == nil then return false end end
    return true
  end
  -- differs: a value the mod sets that the game's definition does not have
  -- (keys only the game's has may be the engine's own, added as it loads)
  local n, extra = 0, nil
  for name in pairs(used) do
    local d, g = MOD.blocks[name], blocks[name]
    if d and type(g) == 'table' and not O.over_blocks[name] then
      local differs = false
      for k, v in pairs(d) do
        if not same(v, g[k], 0) then differs = true break end
      end
      if differs then
        O.over_blocks[name] = copy(d)
        n = n + 1
        if not extra then
          local t = {}
          for k in pairs(g) do if d[k] == nil then t[#t + 1] = tostring(k) end end
          if #t > 0 then extra = name .. ': ' .. concat(t, ' ') end
        end
      end
    end
  end
  if extra then note('the game\'s keys the mod lacks, e.g. ' .. extra) end
  note(n .. ' of the game\'s definitions as the mod has them, in its worlds')
  local dmg = blockTable.damageFactors
  if type(dmg) == 'table' then
    for name, d in pairs(MOD.damage) do
      if type(d) == 'table' then
        if dmg[name] == nil then
          dmg[name] = copy(d)
        elseif type(dmg[name]) == 'table' then
          local merged, changed = copy(dmg[name]), false
          for k, v in pairs(d) do
            if type(v) == 'table' and type(merged[k]) == 'table' then
              for kk, vv in pairs(v) do
                if merged[k][kk] == nil then merged[k][kk] = copy(vv) changed = true end
              end
            elseif merged[k] == nil then
              merged[k] = copy(v)
              changed = true
            end
          end
          if changed then O.over_damage[name] = merged end
        end
      end
    end
  end
  note(sfmt('%d blocks and birds added', added))
  -- particles and materials the game lacks
  local pt = particleTable and particleTable.particles
  if type(pt) == 'table' then
    for name, p in pairs(MOD.particles) do
      if pt[name] == nil then pt[name] = copy(p) end
    end
  end
  local mats = blockTable.materials
  if type(mats) == 'table' then
    for name, m in pairs(MOD.materials) do
      if type(m) == 'table' and mats[name] == nil then mats[name] = copy(m) end
    end
  end
end

-- the sounds these worlds use that the game lacks (created as the game
-- creates its own, in audioData.sounds, which it reloads from)
local function register_sounds(strs)
  if type(audioData) ~= 'table' or type(audioData.sounds) ~= 'table' then return end
  local rm = native and native.ResourceManager
  if type(rm) ~= 'table' or type(rm.createAudio) ~= 'function' then return end
  local base = (type(audioPath) == 'string' and audioPath or 'data/audio') .. '/oe'
  local n = 0
  for name in pairs(strs) do
    local s = MOD.sounds[name]
    if s and audioData.sounds[name] == nil then
      local path = base .. s.path
      if pcall(rm.createAudio, path, name, s.flag) then
        audioData.sounds[name] = {path = path, name = name, channel = s.channel, volume = s.volume, flag = s.flag}
        n = n + 1
      end
    end
  end
  if n > 0 then note(n .. ' sounds') end
end

-- the strings of everything these worlds use: objects (the mod's birds for
-- the game's), their materials, themes, level metadata, and the mod's
-- action scripts (the sounds their code plays)
local function world_strings()
  local strs, defs = {}, {}
  local blocks = blockTable.blocks
  for _, W in ipairs(WORLDS) do
    for _, info in pairs(W.info) do
      for d in pairs(info.defs) do defs[d] = true end
    end
    for _, m in pairs(W.level_meta) do strings_in(m, strs) end
  end
  local mats = {}
  for d in pairs(defs) do
    local def = O.over_blocks[d] or blocks[d]
    strings_in(def, strs)
    if type(def) == 'table' and def.material then mats[def.material] = true end
  end
  if type(blockTable.materials) == 'table' then
    for m in pairs(mats) do strings_in(blockTable.materials[m], strs) end
  end
  for t in pairs(THEMES) do strings_in(MOD.themes[t], strs) end
  local files = {}
  for i, f in ipairs(ACTION_FILES) do files[i] = 'scripts/' .. f .. '.lua' end
  for s in tostring(ask('orb:quoted', concat(files, ',')) or ''):gmatch('[^\n]+') do strs[s] = true end
  return strs
end

-- the game's loadlist: which of its groups holds each sheet file
local game_groups
local function group_of_file()
  if game_groups then return game_groups end
  game_groups = {}
  local ok, t = pcall(native.loadLuaTable, (imagePath or 'data/images') .. '/1024x768_android/loadlist.lua', true)
  if ok and type(t) == 'table' and type(t.assetLoadList) == 'table' then
    for _, groups in pairs(t.assetLoadList) do
      for g, entries in pairs(groups) do
        for _, e in ipairs(entries) do
          local f = type(e) == 'table' and e[1] or e
          if type(f) == 'string' then game_groups[f] = game_groups[f] or g end
        end
      end
    end
  end
  return game_groups
end

-- a group of the mod's sheets holding the new ones among these names (and
-- these sheets)
local function mod_group(name, names, files)
  local entries, got, comps = {}, {}, false
  for _, f in ipairs(split(ask('orb:sheets', csv(names)))) do
    if f:find('COMPOSPRITES') then
      comps = true
    else
      entries[#entries + 1] = {f, 0}
      got[f] = true
    end
  end
  -- a group's composites are read from <group>_COMPOSPRITES.dat, whatever
  -- the entry says: served with the mod's composites among these names
  if comps and tonumber(ask('orb:compos', name .. '|' .. csv(names))) ~= 0 then
    entries[#entries + 1] = {name .. '_COMPOSPRITES.dat', 1}
  end
  for _, f in ipairs(files or {}) do
    if not got[f] then entries[#entries + 1] = {f, 0} end
  end
  local ok, e = pcall(native.ResourceManager.setGroup, name, (imagePath or 'data/images') .. '/oe', entries)
  if not ok then
    err('setGroup ' .. name .. ': ' .. tostring(e))
    return 0, false
  end
  for _, en in ipairs(entries) do O.served[en[1]] = true end
  return #entries, true
end

-- the fewest of the game's groups holding these names: each sheet's sprites
-- ("FILE.dat=NAME|NAME,...") gathered by group, then the group covering the
-- most of what is left, until nothing is (INGAME_COMMON first: a level has
-- it anyway)
local function game_cover(names)
  local gg, cover = group_of_file(), {}
  for entry in tostring(ask('orb:gamesheets', csv(names)) or ''):gmatch('[^,]+') do
    local file, list = entry:match('^([^=]+)=(.*)$')
    local g = file and gg[file]
    if g then
      cover[g] = cover[g] or {}
      for n in list:gmatch('[^|]+') do cover[g][n] = true end
    end
  end
  local groups, left = {}, {}
  for _, c in pairs(cover) do for n in pairs(c) do left[n] = true end end
  local function take(g)
    groups[#groups + 1] = g
    for n in pairs(cover[g]) do left[n] = nil end
    cover[g] = nil
  end
  if cover.INGAME_COMMON then take('INGAME_COMMON') end
  while next(left) do
    local best, most = nil, 0
    for g, c in pairs(cover) do
      local k = 0
      for n in pairs(c) do if left[n] then k = k + 1 end end
      if k > most or (k == most and best and g < best) then best, most = g, k end
    end
    if not best then break end
    take(best)
  end
  return groups
end

-- names and the parts of the composites among them
local function with_parts(names)
  for _ = 1, 2 do
    local more = false
    for _, p in ipairs(split(ask('orb:parts', csv(names)))) do
      if not names[p] then names[p] = true more = true end
    end
    if not more then break end
  end
  return names
end

-- Each theme of these worlds: its levels' names (objects, the mod's birds,
-- their particles, the theme's pictures) -> a group of the mod's sheets,
-- named for the theme's asset prefix so the game's theme loader takes and
-- releases it (<prefix>_THEME), and the game's groups for the rest.
local function theme_names(T)
  local strs = {}
  local blocks = blockTable.blocks
  for _, info in pairs(T.levels) do
    for d in pairs(info.defs) do strings_in(O.over_blocks[d] or blocks[d], strs) end
  end
  strings_in(MOD.themes[T.name], strs)
  -- the portals' swirl, drawn over them (draw_portals)
  for _, info in pairs(T.levels) do
    for d in pairs(info.defs) do
      local def = O.over_blocks[d] or blocks[d]
      if type(def) == 'table' and def.sensorType == 'portal' then strs.WORMHOLE_MASK = true end
    end
  end
  local pt = particleTable and particleTable.particles or {}
  for n in pairs(upper(strs)) do
    if pt[n] then strings_in(pt[n], strs) end
  end
  return with_parts(upper(strs))
end

local function theme_load_list(T)
  if T.loadList then return T.loadList end
  local names = theme_names(T)
  local k, made = mod_group(T.prefix .. '_THEME', names, T.files)
  if not made then
    -- the loader takes <prefix>_THEME whatever: one the game has, then
    T.theme.assetPrefix = 'PIGBANG'
  end
  local groups = game_cover(names)
  T.loadList = {groups = groups}
  note(sfmt('%s: %d of the mod\'s sheets, groups %s', T.name, k, concat(groups, ' ')))
  return T.loadList
end

local function register_themes()
  local themes = blockTable.themes
  local k = 0
  for _, W in ipairs(WORLDS) do
    for n, info in pairs(W.info) do
      local t = info.theme
      if type(t) == 'string' and type(MOD.themes[t]) == 'table' and (themes[t] == nil or THEMES[t]) then
        if not THEMES[t] then
          k = k + 1
          THEMES[t] = {name = t, prefix = 'OE' .. k, levels = {}}
        end
        THEMES[t].levels[n .. '@' .. W.mod] = info
      end
    end
  end
  local gg = group_of_file()
  for t, T in pairs(THEMES) do
    local th = copy(MOD.themes[t])
    -- the ground's texture is a sheet of its own, named for its file: the
    -- mod's, served as OE_<name>, unless the game has it
    if type(th.texture) == 'string' and not gg[th.texture .. '.dat'] then
      T.files = {'OE_' .. th.texture .. '.dat'}
      th.texture = 'OE_' .. th.texture
    end
    th.loadListGroup = nil
    th.assetPrefix = T.prefix
    th.noIngame = true
    th.noMirror = true
    themes[t] = th
    T.theme = th
  end
end

-- a theme of these worlds: its groups worked out the first time it loads (a
-- level is loading: the moment for it)
local function hook_themes()
  if type(checkAndLoadTheme) ~= 'function' or O.theme_hooked then return end
  O.theme_hooked = true
  local orig = checkAndLoadTheme
  checkAndLoadTheme = function(name, ...)
    local T = THEMES[name]
    if T and T.theme and blockTable.themes[name] == T.theme and not T.theme.loadList then
      local ok, l = pcall(theme_load_list, T)
      T.theme.loadList = ok and l or {groups = {}}
      if not ok then err('groups: ' .. tostring(l)) end
    end
    return orig(name, ...)
  end
end

-- The level loader gives each themed object its ground texture by name
-- (setTexture: the current theme's, or the object's own themeTexture, its
-- definition's, the level theme's), and the engine crashes on a name it has
-- not loaded (a null texture read). The definitions in these worlds all take
-- the current theme's, which is loaded with it; should one name another
-- (the mod's INGAME_TEXTURE_EARTH_1 and the like are not the game's), in
-- these worlds it gets the theme's.
local function texture_for(tex)
  local T = type(currentTheme) == 'string' and THEMES[currentTheme]
  if not (T and T.theme and type(T.theme.texture) == 'string') then return tex end
  return T.theme.texture
end

local function hook_textures()
  if type(setTexture) ~= 'function' or O.textures_hooked then return end
  O.textures_hooked = true
  local orig = setTexture
  setTexture = function(obj, tex, ...)
    if in_world() then tex = texture_for(tex) end
    return orig(obj, tex, ...)
  end
end

-- The worlds' comics: the mod drew its own under the game's sprite names
-- (COMIC_1_FRAME_...). Its sheets are served whole, those names as
-- OE_<name>, in a group of each comic's own, and its frames name them so;
-- the game's comics keep theirs.
local function register_comics()
  local dir = (imagePath or 'data/images') .. '/oe'
  for _, W in ipairs(WORLDS) do
    for n, m in pairs(W.level_meta) do
      if m.type == 'comic' and type(m.comicFrames) == 'table' then
        local strs = {}
        strings_in(m.comicFrames, strs)
        local names = upper(strs)
        local game = {}
        for entry in tostring(ask('orb:gamesheets', csv(names)) or ''):gmatch('[^,]+') do
          for x in (entry:match('=(.*)$') or ''):gmatch('[^|]+') do game[x] = true end
        end
        local entries = {}
        for _, f in ipairs(split(ask('orb:fullsheets', csv(names)))) do entries[#entries + 1] = {f, 0} end
        local g = 'OE_' .. n:upper()
        if #entries > 0 and pcall(native.ResourceManager.setGroup, g, dir, entries) then
          for _, en in ipairs(entries) do O.served[en[1]] = true end
          local function rename(t, depth)
            if depth > 4 then return end
            for k, v in pairs(t) do
              if type(v) == 'string' and game[v] then
                t[k] = 'OE_' .. v
              elseif type(v) == 'table' then
                rename(v, depth + 1)
              end
            end
          end
          m.comicFrames.groups = nil
          rename(m.comicFrames, 0)
          m.comicFrames.groups = {g}
        else
          err('no pictures for the comic ' .. n)
        end
      end
    end
  end
end

-- ------------------------------------------------------------ cameras
-- The mod framed its levels again for the PC only: their iPad and iPhone
-- camera entries -- the ones the game uses, and builds each level's killing
-- box from -- mostly still point where the objects once were, off the level,
-- which then empties at once (whatever is outside the box is removed, the
-- birds too; the next bird is then missing and the game stops). In these
-- worlds a level's cameras are its PC ones, as the mod plays; the game fits
-- an entry framed for 1366x768 to the screen as it does the iPad's.
local CAMERA_TABLES = {'birdCameraData', 'castleCameraData', 'entryCameraData'}

-- Most of the mod's levels have one PC camera -- the slingshot's and the
-- target's the same view of the whole level -- so on the Switch there was
-- nothing to pan to nor zoom into (the game zooms no closer than a camera's
-- own scale). Such a level gets a slingshot camera as the game's levels
-- have: at bird 1, leaning toward the level, as close as the level's own
-- slingshot camera once was (its iPad one's height on a 16:9 screen) and at
-- least a little closer than the whole view; the target's stays the whole
-- level. The right stick then pans between them and zooms, as elsewhere.
local function slingshot_camera(level, whole, old)
  local sw = tonumber(whole.screenWidth) or 1366
  local sx = tonumber(whole.sx)
  if not sx or sx <= 0 or type(level.world) ~= 'table' then return nil end
  local bird
  for _, o in pairs(level.world) do
    local d = type(o) == 'table' and o.startNumber == 1 and blockTable.blocks[o.definition]
    if type(d) == 'table' and d.controllable and type(o.x) == 'number' and type(o.y) == 'number' then bird = o end
  end
  if not bird then return nil end
  local ptw = tonumber(level.physicsToWorld) or 20
  local wide = sw / sx -- the whole view's width, in world units
  local want = 3000
  if type(old) == 'table' and tonumber(old.sx) and old.sx > 0 then
    want = (tonumber(old.screenHeight) or 768) / old.sx * 16 / 9
  end
  local w = math.min(want, wide * 0.85)
  local bx, by = bird.x * ptw, bird.y * ptw
  local dx, dy = (tonumber(whole.px) or bx) - bx, (tonumber(whole.py) or by) - by
  local len = math.sqrt(dx * dx + dy * dy)
  if len > 0 then
    local lean = math.min(len, w * 0.3)
    bx, by = bx + dx / len * lean, by + dy / len * lean
  end
  local s = sw / w
  return {px = bx, py = by, sx = s, sy = s, screenWidth = sw, screenHeight = whole.screenHeight}
end

local function pc_cameras(level)
  if type(level) ~= 'table' then return 0 end
  local bc, cc = level.birdCameraData, level.castleCameraData
  local sling
  if type(bc) == 'table' and type(cc) == 'table' and type(bc.windows) == 'table' and type(cc.windows) == 'table' then
    local b, c = bc.windows, cc.windows
    if math.abs((tonumber(b.px) or 0) - (tonumber(c.px) or 0)) + math.abs((tonumber(b.py) or 0) - (tonumber(c.py) or 0)) < 1 then
      sling = slingshot_camera(level, c, bc.ipad)
    end
  end
  local n = 0
  for _, k in ipairs(CAMERA_TABLES) do
    local c = level[k]
    if type(c) == 'table' and type(c.windows) == 'table' then
      local src = (k == 'birdCameraData' and sling) or c.windows
      for dev, v in pairs(c) do
        if dev ~= 'windows' and type(v) == 'table' then c[dev] = copy(src) end
      end
      c.ipad, c.iphone = c.ipad or copy(src), c.iphone or copy(src)
      n = n + 1
    end
  end
  return n
end

-- Every level of the game has a bird numbered 1, its level selections too
-- (hidden there, hideBirdsAndSlingshot, with no killing box): the slingshot
-- asks for it on the first frame, and without one the game stops
-- (objects.world[nil] in animateBirdToSlingShot). The mod's level
-- selections have none (the PC game never asked); in these worlds a level
-- without one gets one as the game's have, to the left of everything.
local function first_bird(level)
  if type(level) ~= 'table' or type(level.world) ~= 'table' then return nil end
  local blocks = blockTable.blocks
  local minx, sumy, n = math.huge, 0, 0
  for _, o in pairs(level.world) do
    if type(o) == 'table' then
      local d = blocks[o.definition]
      if o.startNumber == 1 and type(d) == 'table' and d.controllable then return nil end
      if type(o.x) == 'number' and type(o.y) == 'number' then
        if o.x < minx then minx = o.x end
        sumy, n = sumy + o.y, n + 1
      end
    end
  end
  if n == 0 or type(blocks.RedBird) ~= 'table' then return nil end
  local name = 'OE_SlingshotBird_1'
  level.world[name] = {name = name, definition = 'RedBird', x = minx - 40, y = sumy / n, angle = 0, startNumber = 1}
  return name
end

-- The level's killing box (game.lua: 84% of its cameras' view) removes
-- whatever is outside it once the level starts. The mod framed its levels
-- for the PC, with birds waiting their turn and pieces of the level at the
-- edges of that view: in these worlds the box takes in all of the level,
-- and 20 units more. Birds flying away still leave it.
local function level_extent(level)
  local minx, maxx, miny, maxy
  for _, o in pairs(type(level) == 'table' and type(level.world) == 'table' and level.world or {}) do
    if type(o) == 'table' and type(o.x) == 'number' and type(o.y) == 'number' then
      minx, maxx = math.min(minx or o.x, o.x), math.max(maxx or o.x, o.x)
      miny, maxy = math.min(miny or o.y, o.y), math.max(maxy or o.y, o.y)
    end
  end
  return minx and {minx, maxx, miny, maxy} or nil
end

local BOX_MARGIN = 20
local function hook_killing_box()
  if type(createBox) ~= 'function' or O.box_hooked then return end
  O.box_hooked = true
  local orig = createBox
  createBox = function(name, sprite, x, y, w, h, ...)
    local e = O.level_extent
    if name == 'killingBox' and e and type(x) == 'number' and type(y) == 'number' and type(w) == 'number' and
       type(h) == 'number' and in_world() then
      local l = math.min(x - w * 0.5, e[1] - BOX_MARGIN)
      local r = math.max(x + w * 0.5, e[2] + BOX_MARGIN)
      local t = math.min(y - h * 0.5, e[3] - BOX_MARGIN)
      local b = math.max(y + h * 0.5, e[4] + BOX_MARGIN)
      x, y, w, h = (l + r) * 0.5, (t + b) * 0.5, r - l, b - t
    end
    return orig(name, sprite, x, y, w, h, ...)
  end
end

local function hook_level_files()
  if type(loadLevel) ~= 'function' or O.levels_hooked then return end
  O.levels_hooked = true
  local function wrap(orig)
    return function(...)
      local r = orig(...)
      O.level_extent = nil
      if in_world() then
        local ok, e = pcall(pc_cameras, loadedObjects)
        if not ok then err('cameras: ' .. tostring(e)) end
        ok, e = pcall(first_bird, loadedObjects)
        if not ok then err('first bird: ' .. tostring(e)) end
        ok, e = pcall(level_extent, loadedObjects)
        if ok then O.level_extent = e end
      end
      return r
    end
  end
  loadLevel = wrap(loadLevel)
  if type(loadLevelFromAppData) == 'function' then loadLevelFromAppData = wrap(loadLevelFromAppData) end
end

-- ------------------------------------------------------------ tutorials
-- A bird's first level queues its tutorial (setupTutorial ->
-- menu.TutorialFrame), whose pictures the game's frame knows only for its
-- own birds: for another it has none, and the frame stops the game (ipairs
-- of nil). In these worlds the mod's birds get the mod's tutorials (its
-- TutorialFrame's lists, read as text; its pictures, in a group of their
-- own), and a bird with none anywhere no tutorial.
local TUTORIAL_GROUP = 'OE_TUTORIALS'

local function read_tutorials()
  MOD.tutorials = {}
  local text = ask('orb:text', 'scripts/menus_space/TutorialFrame.lua')
  if type(text) ~= 'string' then return end
  for cond, list, char in text:gmatch('if%s+([^\n]-)%s+then%s+self%.tutorialImages%s*=%s*(%b{})%s*self%.birdImageName%s*=%s*"([^"]*)"') do
    local images = {}
    for n in list:gmatch('"([^"]+)"') do images[#images + 1] = n end
    for b in cond:gmatch('birdName%s*==%s*"([^"]+)"') do
      if MOD.tutorials[b] == nil and #images > 0 then MOD.tutorials[b] = {images = images, character = char} end
    end
  end
end

local function register_tutorials()
  local names, k = {}, 0
  for b, t in pairs(MOD.tutorials) do
    if O.new_blocks[b] then
      for _, n in ipairs(t.images) do names[n] = true end
      names[t.character] = true
      k = k + 1
    end
  end
  if k == 0 then return end
  local n = mod_group(TUTORIAL_GROUP, with_parts(names))
  note(sfmt('tutorials of %d of the mod\'s birds, %d sheets', k, n))
end

-- the frame for one of the mod's birds, as the game's init builds one (its
-- pictures, the bird, the check button)
local function mod_tutorial_frame(self, t)
  self:requireGroup(TUTORIAL_GROUP)
  self.tutorialImages, self.birdImageName = copy(t.images), t.character
  for _, n in ipairs(self.tutorialImages) do
    local img = ui.Image:new(n, false)
    img.tutorialScreen = true
    img:setImage(n)
    self:addChild(img)
  end
  local hero = ui.Image:new('superHeroImage')
  hero:setImage(self.birdImageName)
  self:addChild(hero)
  local close = ui.ScalableButton:new('closeButton', true)
  close.clickSound = 'ButtonProceed'
  close:setImage('BTN_CHECK')
  self:addChild(close)
end

local function hook_tutorial_frame()
  local TF = type(menu) == 'table' and menu.TutorialFrame
  if type(TF) ~= 'table' or type(TF.init) ~= 'function' or rawget(TF, '_oe_init') then return TF end
  local orig = TF.init
  TF._oe_init = orig
  TF.init = function(self, name, bird, ...)
    local t = in_world() and MOD.tutorials[bird]
    if not t then return orig(self, name, bird, ...) end
    -- the game's own, if it has this bird's; else, with the frame begun
    -- (it stops before any picture), the mod's
    local ok, e = pcall(orig, self, name, bird, ...)
    if ok then return end
    if self.tutorialImages ~= nil or #(self.children or {}) > 0 then error(e, 0) end
    mod_tutorial_frame(self, t)
  end
  return TF
end

-- does the game's frame have pictures for this bird (tried once, on a
-- frame of no use)
local game_tutorial = {}
local function game_has_tutorial(TF, bird)
  if game_tutorial[bird] == nil then
    local probe = setmetatable({}, {__index = TF})
    local ok = pcall(TF._oe_init, probe, 'oe_probe', bird)
    game_tutorial[bird] = ok and type(probe.tutorialImages) == 'table'
  end
  return game_tutorial[bird]
end

local function hook_tutorials()
  if type(setupTutorial) ~= 'function' or O.tutorials_hooked then return end
  O.tutorials_hooked = true
  local orig = setupTutorial
  setupTutorial = function(name, force, by_name, ...)
    if in_world() then
      -- the frame's bird: the object's definition, as the game picks it
      local key = name
      if not by_name then
        local o = type(objects) == 'table' and type(objects.world) == 'table' and objects.world[name]
        key = type(o) == 'table' and o.definition or name
      end
      local ok, TF = pcall(function()
        requireFile('menu/TutorialFrame.lua')
        return hook_tutorial_frame()
      end)
      if not ok or type(TF) ~= 'table' then return end
      if not MOD.tutorials[key] and not game_has_tutorial(TF, key) then return end
    end
    return orig(name, force, by_name, ...)
  end
end

-- ------------------------------------------------------------ the planets
-- Each world's planet as the mod draws it on its own episode screen (its
-- levels/EpisodeSelection.lua): the episode's button, its planet picture,
-- and what lies on and around it -- plants and a tomato on Vege-toids,
-- candy, popcorn and pigs on Omelettification -- in the game's carousel
-- scale (its planets are about 289 units across), with the mod's title
-- sign where the game puts its planets' signs.
local PLANET_W = 289
local SIGN_H = 82 -- the game's signs' height
local SELECTOR_SKIP = {'PIPE', 'TITLE', 'EPISODE_BUTTON', 'SENSOR', 'BUBBLE', 'LINK', 'MENU_'}

local function selector_def(name)
  return MOD.blocks[name] or (blockTable and blockTable.blocks[name])
end

-- a sprite's width and height as it shows (the index)
local function sprite_size(name)
  local w, h = tostring(ask('orb:size', name) or ''):match('^(%d+),(%d+)$')
  return tonumber(w), tonumber(h)
end

local function read_selector()
  local env = modfile('levels/EpisodeSelection.lua')
  local world = env and env.world
  if type(world) ~= 'table' then return end
  local ptw = tonumber(env.physicsToWorld) or 20
  local folders = MOD.levelOrder.myLevelFolders or {}
  for _, W in ipairs(WORLDS) do
    -- its button: the one that opens its episode (startNumber: its place
    -- in the mod's own level order)
    local episode
    for i, f in ipairs(folders) do if f == W.mod then episode = i end end
    local button, bdef
    for _, o in pairs(world) do
      if type(o) == 'table' and type(o.definition) == 'string' and o.definition:find('EPISODE_BUTTON', 1, true) and
         tonumber(o.startNumber) == episode then
        button, bdef = o, selector_def(o.definition)
      end
    end
    if button and bdef and type(bdef.sprite) == 'string' then
      -- its title: the sign nearest the button
      local best
      for _, o in pairs(world) do
        local d = type(o) == 'table' and type(o.definition) == 'string' and o.definition:find('_TITLE', 1, true) and
                  selector_def(o.definition)
        if d and type(d.sprite) == 'string' then
          local dd = ((tonumber(o.x) or 0) - button.x) ^ 2 + ((tonumber(o.y) or 0) - button.y) ^ 2
          if dd < 30 * 30 and (not best or dd < best) then best, W.sign = dd, d.sprite end
        end
      end
      W.planet = bdef.sprite
      local pw = sprite_size(bdef.sprite) or 300
      local f = PLANET_W / (pw * (tonumber(bdef.scale) or 1))
      local reach = (tonumber(bdef.radius) or 14) + 9
      local parts = {{z = tonumber(bdef.z_order) or 10, i = 0,
                      e = {sprite = bdef.sprite, pos = {x = 0, y = 0}, scale = (tonumber(bdef.scale) or 1) * f,
                           angle = tonumber(button.angle) or 0}}}
      for _, o in pairs(world) do
        local d = type(o) == 'table' and o ~= button and type(o.definition) == 'string' and selector_def(o.definition)
        local dx, dy = d and (tonumber(o.x) or 0) - button.x, d and (tonumber(o.y) or 0) - button.y
        local keep = d and type(d.sprite) == 'string' and dx * dx + dy * dy <= reach * reach
        for _, k in ipairs(SELECTOR_SKIP) do
          if keep and o.definition:find(k, 1, true) then keep = false end
        end
        if keep then
          -- the scene scales a part's pos by its own scale (the game's sun:
          -- pos 15 at scale 4 and 17.14 at 3.5, both 60 out): the offset
          -- over the part's scale
          local sc = (tonumber(d.scale) or 1) * f
          parts[#parts + 1] = {z = tonumber(d.z_order) or 50, i = #parts,
                               e = {sprite = d.sprite, pos = {x = dx * ptw * f / sc, y = dy * ptw * f / sc},
                                    scale = sc, angle = tonumber(o.angle) or 0}}
        end
      end
      table.sort(parts, function(a, b) if a.z ~= b.z then return a.z < b.z end return a.i < b.i end)
      W.planet_parts = {}
      for i, p in ipairs(parts) do W.planet_parts[i] = p.e end
    else
      err('no planet for ' .. W.name .. ' on the mod\'s episode screen')
    end
  end
end

-- the planets' sprites: the mod's new ones in a group of their own, and the
-- fewest of the game's groups for the rest (the page requires them all)
local function register_planets()
  local strs = {}
  for _, W in ipairs(WORLDS) do
    strs[W.planet], strs[W.sign] = true, true
    for _, e in ipairs(W.planet_parts or {}) do strs[e.sprite] = true end
  end
  local names = with_parts(strs)
  local groups = {}
  local _, made = mod_group('OE_PLANETS', names)
  if made then groups[1] = 'OE_PLANETS' end
  for _, g in ipairs(game_cover(names)) do groups[#groups + 1] = g end
  O.planet_groups = groups
  note('planets: groups ' .. concat(groups, ' '))
end

local function planet_entity(W, template)
  local e = copy(template)
  local visible = e.def and e.def.condition and e.def.condition.falseDef
  if type(visible) ~= 'table' or type(visible.composite) ~= 'table' then return nil end
  -- the template's parts: its planet, its sign, then its score boxes
  local c = {}
  if W.planet_parts then
    for _, p in ipairs(W.planet_parts) do c[#c + 1] = copy(p) end
  else
    c[1] = {sprite = W.planet, pos = {x = 0, y = -2}, scale = PLANET_W / (sprite_size(W.planet) or 300)}
  end
  -- the sign where the game puts its planets' (95 below the middle; pos is
  -- in the part's scaled units)
  local _, sh = sprite_size(W.sign)
  local ss = SIGN_H / (sh or 100)
  c[#c + 1] = {sprite = W.sign, pos = {x = 0, y = 95 / ss}, scale = ss}
  for i = 3, #visible.composite do c[#c + 1] = visible.composite[i] end
  visible.composite = c
  local function renumber(t, depth)
    if type(t) ~= 'table' or depth > 12 then return end
    if type(t.episode) == 'number' then t.episode = W.index end
    for _, v in pairs(t) do renumber(v, depth + 1) end
  end
  renumber(e.def, 0)
  e.pos.metaData.index = W.slot
  return e
end

-- EpisodeSelection loads its scene (native.loadLuaTable('scenes/
-- EpisodeSelection.lua', true)) and lays the planets out on a circle of
-- _buttonCount places, set once from a constant: the two planets go after
-- the last and the count grows by two (seen by the class at the page's
-- first assignment), and the page takes the planets' sprites.
local function hook_planets()
  if O.planets_hooked then return end
  local ES = type(menu) == 'table' and rawget(menu, 'EpisodeSelection')
  if type(ES) ~= 'table' or type(native) ~= 'table' or type(native.loadLuaTable) ~= 'function' then return end
  O.planets_hooked = true
  local load = native.loadLuaTable
  native.loadLuaTable = function(path, ...)
    local t, e2, e3 = load(path, ...)
    if path == 'scenes/EpisodeSelection.lua' and type(t) == 'table' and type(t.entities) == 'table' and O.ok then
      local ok, e = pcall(function()
        local last = 0
        for _, ent in pairs(t.entities) do
          local p = ent.pos
          if type(p) == 'table' and p.metaType == 'carousel' and type(p.metaData) == 'table' then
            last = math.max(last, tonumber(p.metaData.index) or 0)
          end
        end
        local template = t.entities.dangerzone
        for i, W in ipairs(WORLDS) do
          W.slot = last + i
          local ent = template and W.index and planet_entity(W, template)
          if ent then t.entities[W.folder] = ent end
        end
      end)
      if not ok then err('planets: ' .. tostring(e)) end
    end
    return t, e2, e3
  end
  local mt_newindex = rawget(ES, '__newindex')
  ES.__newindex = function(page, k, v)
    if k == '_buttonCount' and type(v) == 'number' and O.ok and not (type(mirrorWorldHandler) == 'table' and
       (mirrorWorldHandler.mirror or mirrorWorldHandler.mirrorVisualsOnly)) then
      v = v + #WORLDS
      if type(page.requireGroup) == 'function' and O.planet_groups and #O.planet_groups > 0 then
        pcall(page.requireGroup, page, unpack(O.planet_groups))
      end
    end
    if mt_newindex then return mt_newindex(page, k, v) end
    rawset(page, k, v)
  end
end

-- ------------------------------------------------------------ in the worlds
-- The mod's versions of the game's birds and damage factors while a level
-- of these worlds is played (the level selection included: it loads first).
O.saved_blocks, O.saved_damage = {}, {}
local function apply_overrides(on)
  if (on and O.applied) or (not on and not O.applied) then return end
  local blocks, dmg = blockTable and blockTable.blocks, blockTable and blockTable.damageFactors
  if type(blocks) ~= 'table' then return end
  if on then
    for name, d in pairs(O.over_blocks or {}) do
      O.saved_blocks[name] = blocks[name]
      blocks[name] = d
    end
    if type(dmg) == 'table' then
      for name, d in pairs(O.over_damage or {}) do
        O.saved_damage[name] = dmg[name]
        dmg[name] = d
      end
    end
  else
    for name, d in pairs(O.saved_blocks) do blocks[name] = d end
    if type(dmg) == 'table' then for name, d in pairs(O.saved_damage) do dmg[name] = d end end
    O.saved_blocks, O.saved_damage = {}, {}
  end
  O.applied = on
end

local function hook_event_system()
  if type(initializeEventSystem) ~= 'function' or O.events_hooked then return end
  O.events_hooked = true
  local orig = initializeEventSystem
  initializeEventSystem = function(...)
    local r = orig(...)
    local ok, e = pcall(function()
      apply_overrides(in_world() ~= nil)
      apply_actions()
    end)
    if not ok then err('actions: ' .. tostring(e)) end
    return r
  end
end

-- ================================================ the prototype birds
-- Ported from the mod: its gamescene decides at a tap which action runs;
-- here the game's own handler (DefaultMode.handleInputLogic) finds no action
-- for these powers and calls fireAction(nil, {name, specialty, dt}), where
-- this takes over. The mod's actions run as they are (the tables of
-- read_actions); what its gamescene did every frame is in O.update.
local function world_obj(name)
  return type(objects) == 'table' and type(objects.world) == 'table' and name ~= nil and objects.world[name] or nil
end
local function def_of(o) return type(o) == 'table' and o.definition and blockTable.blocks[o.definition] or nil end
local dt_now = 1 / 60

-- the PC game's functions the mod's actions call
SHIM.g_birdEmitterTargets = {}
SHIM.activeSensors = {}
SHIM.activateBulletTime = function() end
SHIM.getDeltaTimeMultiplier = function() return 1 end
SHIM.setDeltaTimeMultiplier = function() end
SHIM.bulletTimeSmoothOut = 0
SHIM.registerAchievement = function() end
SHIM.loginfo = function() end
SHIM.destroyEmitter = function() end
SHIM.deepCopy = copy
-- the mod turns a sensor type into a sensor by name ("gravitation"): only
-- objects of the level are passed on
SHIM.setAsSensor = function(n, on)
  if world_obj(n) and type(setAsSensor) == 'function' then return setAsSensor(n, on) end
end
setmetatable(SHIM, {__index = function(_, k) if k == 'g_dt' then return dt_now end end})

-- drawGameSprite(sprite, x, y, scale, angle, alpha): a sprite at a point of
-- the level, drawn after the scene (GameScene.draw)
local queue = {}
SHIM.drawGameSprite = function(sprite, x, y, scale, angle, alpha)
  if type(sprite) ~= 'string' or type(x) ~= 'number' or type(y) ~= 'number' or #queue > 64 then return end
  queue[#queue + 1] = {sprite, x, y, type(scale) == 'number' and scale or 1, angle or 0, alpha or 1}
end
SHIM.drawGameSpriteBack = SHIM.drawGameSprite

local function draw_queue()
  local ws = worldScale or 1
  for _, q in ipairs(queue) do
    local sprite, x, y, s, a = q[1], q[2], q[3], q[4], q[5]
    local w, h = res.getSpriteBounds(sprite)
    local px, py = res.getSpritePivot(sprite)
    if w and px and s > 0 then
      local sx, sy = physicsToScreenTransform(x, y)
      setRenderState(0, 0, ws, ws, a, px * s, py * s)
      res.drawSprite(sprite, sx / ws - px * s, sy / ws - py * s, 'LEFT', 'TOP', w * s, h * s)
    end
  end
  setRenderState(0, 0, 1, 1, 0, 0, 0)
end

-- Black portals: the mod's scene draws a turning swirl (WORMHOLE_MASK)
-- over each portal sensor, whose own picture is only the hole; the game's
-- draws nothing there. In these worlds, drawn after the scene, turning as
-- the mod turns it (O.update).
local function portal_def(v)
  local d = type(v) == 'table' and v.definition and blockTable.blocks[v.definition]
  if type(d) == 'table' and (v.sensorType or d.sensorType) == 'portal' then return d end
end

local function draw_portals()
  local w, h = res.getSpriteBounds('WORMHOLE_MASK')
  local px, py = res.getSpritePivot('WORMHOLE_MASK')
  if not (w and px) or w <= 0 then return end
  local ws = worldScale or 1
  for _, v in pairs(objects.world) do
    local d = portal_def(v)
    if d and type(v.x) == 'number' and type(v.y) == 'number' then
      local s = tonumber(d.scale) or 1
      local sx, sy = physicsToScreenTransform(v.x, v.y)
      setRenderState(0, 0, ws, ws, v.oeSwirl or 0, px * s, py * s)
      res.drawSprite('WORMHOLE_MASK', sx / ws - px * s, sy / ws - py * s, 'LEFT', 'TOP', w * s, h * s)
    end
  end
  setRenderState(0, 0, 1, 1, 0, 0, 0)
end

local function turn_portals(dt)
  for _, v in pairs(objects.world) do
    if portal_def(v) then
      v.oeSwirl = (v.oeSwirl or math.random(0, 360)) + dt * math.random(15, 35) / 10
    end
  end
end

-- getRayCastedObjects{x1, y1, x2, y2, source}: what a ray from (x1, y1)
-- meets first -- {name, x, y, nx, ny, fraction, ...} -- by stepping along
-- it with the game's own box query
SHIM.getRayCastedObjects = function(r)
  local out = {}
  if type(getIntersectingObjects) ~= 'function' then return out end
  local dx, dy = r.x2 - r.x1, r.y2 - r.y1
  local len = sqrt(dx * dx + dy * dy)
  if len < 0.01 then return out end
  local n = math.min(floor(len), 400)
  for i = 1, n do
    local f = i / n
    local x, y = r.x1 + dx * f, r.y1 + dy * f
    local hits = getIntersectingObjects({x = x, y = y, left = -0.5, right = 0.5, down = -0.5, up = 0.5})
    for _, k in pairs(hits or {}) do
      local v = objects.world[k]
      if v and k ~= r.source and not v.isSensor and not v.controllable then
        local c = #out
        out[c + 1], out[c + 2], out[c + 3], out[c + 4], out[c + 5], out[c + 6] = k, x, y, 0, 0, f
      end
    end
    if #out > 0 then break end
  end
  return out
end

-- where the player tapped, in the level: the tap, or -- when it is on the
-- bird, as the controller's A taps the middle of the screen, where the
-- camera keeps the flying bird -- straight ahead
local function aim(bird)
  local cx, cy
  if type(cursor) == 'table' and type(cursor.x) == 'number' and has_game(screenToPhysicsTransform) then
    cx, cy = screenToPhysicsTransform(cursor.x, cursor.y)
  end
  if type(cx) ~= 'number' and type(cursorPhysics) == 'table' then cx, cy = cursorPhysics.x, cursorPhysics.y end
  cx, cy = cx or bird.x, cy or bird.y
  local dx, dy = cx - bird.x, cy - bird.y
  if dx * dx + dy * dy < 16 then
    local vx, vy = bird.xVel or 0, bird.yVel or 0
    local l = sqrt(vx * vx + vy * vy)
    if l < 0.01 then vx, vy, l = cos(bird.angle or 0), sin(bird.angle or 0), 1 end
    cx, cy = bird.x + vx / l * 20, bird.y + vy / l * 20
  end
  return cx, cy
end

-- run f with the game's cursor on (x, y) (the mod's actions read it)
local function with_cursor(x, y, f)
  local cp, cu = type(cursorPhysics) == 'table' and cursorPhysics, type(cursor) == 'table' and cursor
  local ox, oy, sx, sy
  if cp then ox, oy = cp.x, cp.y cp.x, cp.y = x, y end
  if cu and has_game(physicsToScreenTransform) then
    local ok, a, b = pcall(physicsToScreenTransform, x, y)
    if ok and type(a) == 'number' then sx, sy = cu.x, cu.y cu.x, cu.y = a, b end
  end
  local ok, e = pcall(f)
  if cp then cp.x, cp.y = ox, oy end
  if sx then cu.x, cu.y = sx, sy end
  if not ok then error(e, 0) end
end

local orig_fire
local function fire(name, p)
  if actions[name] == nil then err('no action ' .. name) return end
  orig_fire(name, p)
end

-- Drill: a tap stops it and it bores in (O.update), then explodes
local function drill_start(bird)
  addBirdSpecialParticles(bird)
  local lx, ly = physicsToWorldTransform(bird.x, bird.y)
  addPuffToTrajectory(1, lx, ly)
  setAngularVelocity(bird.name, 0)
  setVelocity(bird.name, (bird.xVel or 0) * 0.001, (bird.yVel or 0) * 0.001)
  bird.drillPosX, bird.drillPosY, bird.drillAngle = bird.x, bird.y, bird.angle
  bird.ignoreBomb, bird.collision = true, true
  bird.drillTimer, bird.bombTimer = 1, 1.7
end

-- Pink: a tap lifts what the beam meets (usePinkBirdSpecialty), a second
-- tap lets it go
local function pink_release(bird)
  setAngularVelocity(bird.name, 0)
  for k, v in pairs(objects.world) do
    if v ~= bird and v.bubbleAntiGravityTimer and v.rayName == bird.rayName then
      v.specialtyClassicGravityMultiplier, v.specialtyMaximumVelocity = nil, nil
      v.bubbleAntiGravityTimer, v.inBubble, v.reticleName, v.bubblePrimaryTarget = nil, false, nil, nil
      v.bubbleBeamTimer, v.bubbleBeamPhase, v.bubbleBeamAnimationTimer, v.bubbleBeamAnimationFrame = nil, nil, nil, nil
      v.antiGravityHolder = nil
      setRevertGravity(k, false)
    end
  end
  bird.activateSecondAbility, bird.bubbleAntiGravityTimer = false, nil
  bird.stopAntiGravity, bird.disableTrail, bird.zeroPos = true, true, 1000
  bird.rotateWhileFlying, bird.ignoreTrailParticles, bird.inBubble, bird.boostAim = true, false, false, nil
  bird.activateAntigravity = false
end

local function pink_tap(bird, d, ax, ay, p)
  if bird.stopAntiGravity then return end
  if bird.activateSecondAbility then return pink_release(bird) end
  setRotation(bird.name, atan2(ay - bird.y, ax - bird.x))
  bird.specialtyAvailable = true -- a second tap
  addBirdSpecialParticles(bird)
  bird.rotateWhileFlying, bird.activateSecondAbility, bird.ignoreTrailParticles = false, true, true
  SHIM.g_birdEmitterTargets[bird.name] = {ax, ay, nil, nil, d.reticleSprite, d.specialtyDuration}
  bird.removeEmitterTargetTimer = d.specialtyDuration
  fire('usePinkBirdSpecialty', {name = bird.name, xPos = ax, yPos = ay, dt = p.dt})
end

-- Black Hole, second tap: the mod's action makes the bird again (a new
-- object) and switches the hole off -- left in the game's list of birds,
-- never to collide, and (no body) never slowed nor stopped: a level lost
-- would wait for it for ever. The hole leaves the list; the bird out of it
-- joins, to be waited for and cleared as any bird.
local function black_hole_out(hole)
  if type(birds) ~= 'table' then return end
  hole.hasCollided = true
  birds[hole.name] = nil
  local fb = flyingBird
  if type(fb) == 'table' and fb ~= hole and fb.name and objects.world[fb.name] == fb then
    fb.controllable = true
    birds[fb.name] = fb
  end
end

local SPECIAL = {GRENADE = true, BOOMERANG = true, BLACK_HOLE = true, DRILL = true, GRAVITY_DISRUPTOR = true,
                 BULK = true}
local function special(p)
  local bird = world_obj(p.name)
  local d = def_of(bird)
  if not bird or not d then return end
  local s = p.specialty
  local ax, ay = aim(bird)
  with_cursor(ax, ay, function()
    if s == 'GRENADE' then
      addBirdSpecialParticles(bird)
      fire('useGrenadeSpecialty', p)
    elseif s == 'BOOMERANG' then
      fire('useBoomerangSpecialty', p)
    elseif s == 'BLACK_HOLE' then
      addBirdSpecialParticles(bird)
      local second = bird.activateSecondAbility
      fire('useBlackHoleSpecialty', p)
      if second then black_hole_out(bird) end
    elseif s == 'DRILL' then
      drill_start(bird)
    elseif s == 'GRAVITY_DISRUPTOR' then
      pink_tap(bird, d, ax, ay, p)
    end -- BULK: Big Red's power is its landing (RedBigBirdCollided)
  end)
end

-- the drill's frames while it flies, and its boring (the game counts its
-- fuse, bombTimer, and explodes it)
local function drill_update(v, d, dt)
  local timer = d.sprites.timer
  if not v.collision and v.shot and not v.stop then
    v.flyingAnimTimer = (v.flyingAnimTimer or 0.2) - dt * 2
    if v.flyingAnimTimer < 0 then v.flyingAnimTimer = 0.8 end
    local f = 8 - floor(math.min(0.79, v.flyingAnimTimer) * 10)
    if timer[f] then setSprite(v.name, timer[f]) end
  end
  local fuse = v.bombTimer or 1
  if v.animDrillingTimer and not v.animStop and fuse > 0.1 then
    v.animDrillingTimer = v.animDrillingTimer - v.animSpeed
    if v.animSpeed < 0 then
      v.animStop = true
      setSprite(v.name, timer[1])
    end
    if (v.xVel or 0) ~= 0 or (v.yVel or 0) ~= 0 then setRotation(v.name, atan2(v.yVel or 0, v.xVel or 0)) end
    if v.animDrillingTimer < 0 and v.animSpeed > 0 then
      v.animDrillingTimer = 0.8
      v.animSpeed = v.animSpeed - dt * 4.5
    elseif not v.animStop then
      local f = 8 - floor(math.max(0, math.min(0.79, v.animDrillingTimer)) * 10)
      if timer[f] then setSprite(v.name, timer[f]) end
    end
  elseif v.drillTimer and not v.specialUsed and fuse > 0.1 then
    v.drillTimer = v.drillTimer - dt
    setPosition(v.name, v.drillPosX, v.drillPosY)
    if not v.oeDrillSound then
      v.oeDrillSound = true
      res.playAudio(getAudioName(d.sounds.collided), 1, false)
    end
    if v.drillTimer > 0.4 then
      setRotation(v.name, v.drillAngle + sin((v.drillTimer + 0.6) * (80 - v.drillTimer * 15)) *
        (math.pi * 0.2 - v.drillTimer - 0.4))
    else
      setRotation(v.name, v.drillAngle)
    end
    setAngularVelocity(v.name, 0)
    if v.drillTimer < 0 then
      enableGravityForBird(v.name, false)
      v.disableGravityForBird = true
      v.specialUsed, v.drillTimer = true, nil
      v.animSpeed, v.animDrillingTimer = 0.5, 0.8
      setVelocity(v.name, 1200 * cos(v.angle), 1200 * sin(v.angle))
      if not v.oeDrillGo then
        v.oeDrillGo = true
        res.playAudio(getAudioName(d.sounds.special), 1, false)
      end
    elseif v.drillTimer < 0.1 then
      setVelocity(v.name, (v.xVel or 0) * 0.02, (v.yVel or 0) * 0.02)
    end
  end
end

-- the Pink bird's beam and lift: the beam's two phases, the lifted objects
-- floating up for specialtyDuration, the bird pulled after them
local function pink_update(dt)
  for k, v in pairs(objects.world) do
    if (v.inBubble or v.bubblePrimaryTarget) and v.bubbleBeamTimer then
      v.bubbleBeamTimer = v.bubbleBeamTimer - dt
      if v.bubbleBeamTimer < 0 then
        if v.bubbleBeamPhase == 1 then
          if v.destroyWhenBeamHits then
            destroyObject(v.name)
          else
            v.bubbleBeamPhase = 2
            local bd = def_of(v.reticleName and objects.world[v.reticleName] or v)
            v.bubbleBeamTimer = bd and (v.bubbleBeamTimer + (bd.specialtyBeamPhase2Time or 0.1)) or nil
          end
        else
          v.bubbleBeamTimer = nil
        end
        if v.bubblePrimaryTarget then
          v.bubblePrimaryTarget = false
          if not v.inBubble and v.reticleName then SHIM.g_birdEmitterTargets[v.reticleName] = nil end
        end
      end
    end
    if v.inBubble and v.bubbleAntiGravityTimer then
      if v.bubbleAntiGravityTimer > 0 then
        v.bubbleAntiGravityTimer = v.bubbleAntiGravityTimer - dt
        local vd = def_of(v)
        if (v.mass or 0) > 0 and vd and vd.material ~= 'staticGround' and v.specialtyMaximumVelocity and
           objects.worldGravity and (v.velocity or 0) < v.specialtyMaximumVelocity then
          applyForce(v.name, 0, -objects.worldGravity * v.mass * (v.specialtyClassicGravityMultiplier or 1), v.x, v.y)
        end
      else
        v.bubbleAntiGravityTimer, v.inBubble = nil, nil
        setRevertGravity(k, false)
        if v.reticleName then SHIM.g_birdEmitterTargets[v.reticleName] = nil end
      end
    end
  end
  -- the bird, pulled after what it lifted, until the beam fades
  for _, bird in pairs(objects.world) do
    if bird.activateAntigravity and not bird.stopAntiGravity then
      local bd = def_of(bird)
      if bird.hasCollided then
        pink_release(bird)
      elseif bd and bird.rayFadedTimer then
        bird.rayFadedTimer = bird.rayFadedTimer - dt
        if bird.rayFadedTimer < 12 then
          bird.zeroPos, bird.rotateWhileFlying, bird.ignoreTrailParticles = 1000, true, false
        end
        if bird.rayFadedTimer <= 0 then
          pink_release(bird)
        elseif bird.activateSecondAbility then
          for _, v in pairs(objects.world) do
            if v ~= bird and v.rayName == bird.rayName and v.bubbleAntiGravityTimer then
              v.inBubble = true
              setSleeping(v.name, false)
              local dir = atan2(v.y - bird.y, v.x - bird.x)
              local si = bd.specialtyStartImpulse or -0.1
              local k = (v.mass or 0) * 1.1 * dt * 135.75
              applyImpulse(v.name, si * cos(dir) * k, si * sin(dir) * k, v.x, v.y)
              local n = (bird.blockAmount or 5) / 5
              local bi = bd.specialtyBirdStartImpulse or -0.225
              local kb = dt * (bd.specialtyMinVelocity or 75) * (bird.mass or 1) / n
              applyImpulse(bird.name, -bi * cos(dir) * kb, -bi * sin(dir) * kb, bird.x, bird.y)
              setAngularVelocity(bird.name, 0)
              setRotation(bird.name, dir)
            end
          end
        end
      end
    end
  end
end

-- A shot bird the game waits for (a lost level ends only when every bird
-- in its list has hit something or been cleared for going slow): one that
-- has not moved at all for 3 s while its speed says otherwise has no body
-- left (switched off by a power) and counts as done.
local frozen = setmetatable({}, {__mode = 'k'})
local function settle_frozen_birds(dt)
  if type(birds) ~= 'table' then return end
  for _, b in pairs(birds) do
    if type(b) == 'table' and b.shot and b.controllable and not b.hasCollided and type(b.x) == 'number' then
      local f = frozen[b]
      local speed = math.abs(tonumber(b.xVel) or 0) + math.abs(tonumber(b.yVel) or 0)
      if f and f.x == b.x and f.y == b.y and speed > 0.35 then
        f.t = f.t + dt
        if f.t > 3 then b.hasCollided = true end
      else
        frozen[b] = {x = b.x, y = b.y, t = 0}
      end
    end
  end
end

function O.update(dt)
  if type(objects) ~= 'table' or type(objects.world) ~= 'table' then return end
  -- a power the mod's code offers again (a second tap: the Boomerang's
  -- return, the Black Hole's release, the Pink bird's drop) or takes away
  local fb = flyingBird
  if type(fb) == 'table' then
    if fb.specialtyAvailable == true then
      birdSpecialtyAvailable = true
    elseif fb.specialtyAvailable == false then
      birdSpecialtyAvailable = false
    end
    fb.specialtyAvailable = nil
    if fb.oeDelay and fb.oeDelay > 0 then
      fb.oeDelay = fb.oeDelay - dt
      if fb.oeDelay <= 0 then birdSpecialtyAvailable = false end
    end
  end
  for _, v in pairs(objects.world) do
    local d = v.definition and blockTable.blocks[v.definition]
    if type(d) == 'table' then
      if d.specialty == 'DRILL' and type(d.sprites) == 'table' and type(d.sprites.timer) == 'table' then
        drill_update(v, d, dt)
      elseif d.specialty == 'BOOMERANG' and v.shot and not v.hasCollided and not v.boomerangBirdActive and
             not v.activateSecondAbility then
        v.angularVelocity = (v.angularVelocity or 0) + dt * 15
        setSpriteRotation(v.name, v.angularVelocity)
      end
    end
  end
  pink_update(dt)
  settle_frozen_birds(dt)
  turn_portals(dt)
end

local function hook_powers()
  if O.powers_hooked or type(fireAction) ~= 'function' or type(GameScene) ~= 'table' then return end
  O.powers_hooked = true
  orig_fire = fireAction
  fireAction = function(name, p, key)
    if name == nil and type(p) == 'table' and SPECIAL[p.specialty] and O.applied then
      local ok, e = pcall(special, p)
      if not ok then err('power ' .. tostring(p.specialty) .. ': ' .. tostring(e)) end
      return
    end
    return orig_fire(name, p, key)
  end
  -- a hit: the mod's Black and Ice birds keep their power a while
  -- (specialtyActivationDelay); the Iron and Pink birds' aim is dropped
  if type(birdCollision) == 'function' then
    local orig_hit = birdCollision
    birdCollision = function(...)
      local before, bird = birdSpecialtyAvailable, flyingBird
      local r = orig_hit(...)
      if O.applied and type(bird) == 'table' then
        local ok, e = pcall(function()
          local d = world_obj(bird.name) and def_of(bird)
          if not d then return end
          if (d.specialty == 'GRENADE' or d.specialty == 'GRAVITY_DISRUPTOR') and bird.boostAim then
            bird.boostAim, bird.zeroPos = nil, 1000
          end
          if before and not birdSpecialtyAvailable and d.specialtyActivationDelay and not bird.oeDelay then
            bird.oeDelay = d.specialtyActivationDelay
            birdSpecialtyAvailable = true
          end
        end)
        if not ok then err('hit: ' .. tostring(e)) end
      end
      return r
    end
  end
  -- every update of a level: the mod's per-frame logic; every draw: its
  -- sprites
  local orig_ua = GameScene.updateActions
  if type(orig_ua) == 'function' then
    GameScene.updateActions = function(self, dt, ...)
      queue = {}
      local r = orig_ua(self, dt, ...)
      if O.applied then
        dt_now = type(dt) == 'number' and dt or dt_now
        local ok, e = pcall(O.update, dt_now)
        if not ok then err('update: ' .. tostring(e)) end
      end
      return r
    end
  end
  local orig_draw = GameScene.draw
  if type(orig_draw) == 'function' then
    GameScene.draw = function(self, ...)
      local r = orig_draw(self, ...)
      if O.applied and type(objects) == 'table' and type(objects.world) == 'table' then
        local ok, e = pcall(draw_portals)
        if not ok then err('portals: ' .. tostring(e)) end
      end
      if #queue > 0 and O.applied then
        local ok, e = pcall(draw_queue)
        if not ok then err('draw: ' .. tostring(e)) end
        queue = {}
      end
      return r
    end
  end
end

-- ------------------------------------------------------------ the card
-- The mod is the whole PC game (137 MB); these worlds use a part of it. Once
-- they are added, what they use is known: every sheet of the mod's the game
-- is given (the groups: the worlds' themes -- worked out here, as their first
-- level would -- their comics, the tutorials, the planets) and the pictures
-- those sheets name, the sounds made for the worlds, its scripts, the two
-- worlds' level folders. The port (abs_orbital.c, abs_orbital_prune) removes
-- the rest from the card: its pictures, sounds and level folders no sheet,
-- sound or world of these names; never a sheet's table, a script, or one of
-- the worlds' levels. Nothing is removed if anything here went wrong, and
-- once trimmed (orb:trimmed) none of this is worked out again.
local function prune_card()
  if #O.errors > 0 or ask('orb:trimmed', '') == '1' then return end
  for _, T in pairs(THEMES) do
    local ok, e = pcall(theme_load_list, T)
    if not ok then err('groups: ' .. tostring(e)) return end
  end
  if #O.errors > 0 then return end
  local keep, sheets, sounds = {}, 0, 0
  for f in pairs(O.served) do
    local file = f:match('^OEF_(.+%.dat)$') or f:match('^OE_(.+%.dat)$')
    if file and not file:find('_COMPOSPRITES', 1, true) then
      keep[#keep + 1] = 'images/PC/' .. file
      sheets = sheets + 1
    end
  end
  -- the sounds made for these worlds (register_sounds: the only ones of the
  -- mod's the game is given)
  local own = type(audioData) == 'table' and type(audioData.sounds) == 'table' and audioData.sounds or {}
  for name, snd in pairs(MOD.sounds or {}) do
    local g = own[name]
    if type(g) == 'table' and type(g.path) == 'string' and g.path:find('/oe/', 1, true) and type(snd.path) == 'string' then
      keep[#keep + 1] = 'audio' .. (snd.path:sub(1, 1) == '/' and '' or '/') .. snd.path
      sounds = sounds + 1
    end
  end
  if sheets < 10 or sounds < 10 then return end
  local r = ask('orb:prune', concat(keep, '\n'))
  if r and r ~= '' then note(r) end
end

-- ------------------------------------------------------------ each update
local function ready()
  return type(blockTable) == 'table' and type(blockTable.blocks) == 'table' and blockTable.blocks.RedBird ~= nil and
         type(blockTable.themes) == 'table' and blockTable.themes.theme1 ~= nil and type(actions) == 'table' and
         type(actions.myLevelFolders) == 'table' and #actions.myLevelFolders >= 18 and type(actions.myLevels) == 'table' and
         type(starTable) == 'table' and type(native) == 'table' and type(native.ResourceManager) == 'table' and
         type(native.ResourceManager.setGroup) == 'function' and type(native.loadLuaTable) == 'function'
end

-- The mod read and its worlds added in small steps, a few each update (a
-- level file is one): the main menu keeps moving meanwhile.
local function make_steps()
  local steps = {}
  local function add(w, f, ...)
    local args = {...}
    steps[#steps + 1] = {w, function() return f(unpack(args)) end}
  end
  MOD.levelOrder = modfile('scripts/levelOrder.lua') or {}
  add(2, read_tables)
  for _, f in ipairs(BLOCK_FILES) do add(1, read_block_file, f) end
  add(1, read_sounds)
  add(1, read_tutorials)
  for _, W in ipairs(WORLDS) do
    W.source, W.info = (MOD.levelOrder.myLevels or {})[W.mod], {}
    for _, n in ipairs(W.source or {}) do add(1, read_level, W, n) end
  end
  for _, f in ipairs(ACTION_FILES) do add(1, read_action_file, f) end
  add(1, function()
    if not build_episodes() then error('the mod\'s level order has no episodes', 0) end
  end)
  add(2, register_blocks)
  add(1, register_themes)
  add(4, register_comics)
  add(2, register_tutorials)
  add(4, function() register_sounds(world_strings()) end)
  add(1, read_selector)
  add(2, register_planets)
  add(1, function()
    O.ok = true
    apply_actions()
    note(sfmt('images %s, sounds %s', tostring(imagePath), tostring(audioPath)))
  end)
  return steps
end

local STEPS_PER_UPDATE = 2
local steps, step_i
local function register_some()
  if not steps then steps, step_i = make_steps(), 1 end
  local budget = STEPS_PER_UPDATE
  while step_i <= #steps and budget > 0 do
    local st = steps[step_i]
    local ok, e = pcall(st[2])
    step_i = step_i + 1
    if not ok then
      err(e)
      return true, false
    end
    budget = budget - st[1]
  end
  if step_i > #steps then return true, O.ok end
  return false
end

function O.frame()
  if not O.present then return end
  if not O.done then
    if not ready() then return end
    local ok, finished, good = pcall(register_some)
    if not ok then
      err(finished)
      finished, good = true, false
    end
    if not finished then return end
    O.done, steps = true, nil
    if good then
      note('Orbital Escapade: Vege-toids and Omelettification added')
      local okp, ep = pcall(prune_card)
      if not okp then err('trimming the mod: ' .. tostring(ep)) end
    else
      O.ok = false
      note('Orbital Escapade: not added')
    end
  end
  if not O.ok then return end
  if not O.events_hooked then pcall(hook_event_system) end
  if not O.planets_hooked then pcall(hook_planets) end
  if not O.theme_hooked then pcall(hook_themes) end
  if not O.textures_hooked then pcall(hook_textures) end
  if not O.tutorials_hooked then pcall(hook_tutorials) end
  if not O.levels_hooked then pcall(hook_level_files) end
  if not O.box_hooked then pcall(hook_killing_box) end
  if not O.powers_hooked then pcall(hook_powers) end
  local on = in_world() ~= nil
  if on ~= (O.applied or false) then
    pcall(apply_overrides, on)
    pcall(apply_actions)
  end
end

-- for the port's log
function O.report()
  if #O.notes == 0 and #O.errors == 0 then return '' end
  local t = {}
  for _, n in ipairs(O.notes) do t[#t + 1] = n end
  for _, e in ipairs(O.errors) do t[#t + 1] = 'error: ' .. e end
  O.notes, O.errors = {}, {}
  return concat(t, '\n')
end
