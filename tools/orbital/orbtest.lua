-- orbtest.lua -- abs_orbital.lua against the game's own data (Android 2.2.14,
-- decrypted in lua_dec/) and the player's copy of the mod, served by the real
-- abs_orbital.c (orbhost): the registration, the carousel, a level load
-- rebuilding actions, and each prototype bird's taps against stubs of every
-- function the game has (a call to one it lacks fails). See setup.sh.
--   QUIET=1 ./orbhost <folder holding orbital/data> <apk assets> orbtest.lua <abs_orbital.lua>
local SRC = arg[1]
local fails, oks = 0, 0
local function check(ok, what)
  if ok then oks = oks + 1 else fails = fails + 1 end
  print((ok and 'ok: ' or 'FAIL: ') .. what)
end

local function env_of(path)
  local e = setmetatable({}, {__index = _G})
  local f, err = loadfile(path)
  if not f then error(path .. ': ' .. tostring(err)) end
  setfenv(f, e)
  local ok, err2 = pcall(f)
  if not ok then print('  (load ' .. path .. ': ' .. tostring(err2) .. ')') end
  return e
end
local function raw(e) local t = {} for k, v in pairs(e) do t[k] = v end return t end
-- a file of the mod, read as the script reads it (orb:load: plain, bytecode
-- or under the PC key)
local function mod_env(rel)
  local e = setmetatable({}, {__index = _G})
  local f = math.frexp('orb:load', rel)
  if type(f) == 'function' then setfenv(f, e) pcall(f) end
  return e
end

-- ------------------------------------------------ the game, as loaded
imagePath, audioPath, scriptPath = 'images', 'audio', 'scripts' -- as the game has them
deviceModel = "android"
setChannelCountLimit = function() end
local groups, audio_made = {}, {}
native = {
  ResourceManager = {
    setGroup = function(name, dir, entries) groups[name] = {dir = dir, entries = entries} end,
    acquireGroup = function() end, releaseGroup = function() end,
    createAudio = function(path, name, flag) audio_made[name] = path end,
  },
}
local scenes = {}
native.loadLuaTable = function(path, x)
  if path == 'scenes/EpisodeSelection.lua' then return raw(env_of('lua_dec/scenes/EpisodeSelection.lua')) end
  local p = path:match('^data/images/(.*)$') or path:match('^images/(.*)$')
  if p then return raw(env_of('lua_dec/images/' .. p)) end
  return nil, 'no ' .. path
end
blockTable = {blocks = {}, themes = raw(env_of('lua_dec/scripts/themes.lua')),
              materials = raw(env_of('lua_dec/scripts/materials.lua')),
              damageFactors = raw(env_of('lua_dec/scripts/damagefactors.lua'))}
for f in io.popen('ls lua_dec/scripts/blocks/*.lua'):lines() do
  if not f:find('backup') then
    for _, v in pairs(raw(env_of(f))) do
      if type(v) == 'table' then
        for _, d in ipairs(v) do
          if type(d) == 'table' and type(d.definition) == 'string' then blockTable.blocks[d.definition] = d end
        end
      end
    end
  end
end
local nblocks = 0 for _ in pairs(blockTable.blocks) do nblocks = nblocks + 1 end
particleTable = env_of('lua_dec/scripts/particles.lua').particleTable
starTable = env_of('lua_dec/scripts/starLimits.lua').starTable
actions = {}
do local e = env_of('lua_dec/scripts/levelOrder.lua') end
audioData = {sounds = {}}
do
  local e = env_of('lua_dec/scripts/soundManager.lua')
  local sm = e.soundManager
  if sm and sm.createAudioAssets then local ok, er = pcall(sm.createAudioAssets) if not ok then print('  (sounds: ' .. tostring(er) .. ')') end audioData = rawget(e, 'audioData') or audioData end
end
local game_sounds = 0 for _ in pairs(audioData.sounds) do game_sounds = game_sounds + 1 end
print(('the game: %d blocks, %d themes, %d level folders, %d sounds, %d particles'):format(nblocks,
  (function() local n = 0 for _ in pairs(blockTable.themes) do n = n + 1 end return n end)(), #actions.myLevelFolders,
  game_sounds, (function() local n = 0 for _ in pairs(particleTable.particles) do n = n + 1 end return n end)()))
local game_blocks = {} for k in pairs(blockTable.blocks) do game_blocks[k] = blockTable.blocks[k] end
local game_themes = {} for k, v in pairs(blockTable.themes) do game_themes[k] = v end
local game_folders = #actions.myLevelFolders
local loaded_themes = {}
acquired = {}
function checkAndLoadTheme(t)
  loaded_themes[#loaded_themes + 1] = t
  local th = blockTable.themes[t]
  acquired = {th.assetPrefix .. '_THEME'}
  if not th.noIngame then acquired[#acquired + 1] = th.assetPrefix .. '_INGAME' end
  for _, g in ipairs((th.loadList or {}).groups or {}) do acquired[#acquired + 1] = g end
end
function initializeEventSystem()
  actions = {}
  local e = env_of('lua_dec/scripts/levelOrder.lua')
end
GameScene = {updateActions = function() end, draw = function() end}
function fireAction(name, p) if name and actions[name] then actions[name].action(p) end end
function birdCollision() birdSpecialtyAvailable = false end -- as the game: a hit ends the power
mirrorWorldHandler = {mirror = false}
menu = {EpisodeSelection = {}}
menu.EpisodeSelection.__index = menu.EpisodeSelection
function menu.EpisodeSelection:requireGroup(...) self._groups = {...} end

local texture_set
function setTexture(obj, tex) texture_set = tex end -- the game's (gamelogic.lua)

-- ------------------------------------------------ the script
dofile(SRC)
local O = __abs_orbital
check(O.present, 'the mod is found (orb:present)')
local nframes = 0
repeat O.frame() nframes = nframes + 1 until O.done or nframes > 500
print('registered in ' .. nframes .. ' updates')
print(O.report())
check(O.ok, 'registration ran')
check(#O.errors == 0, 'no errors')

-- episodes
local W1, W2 = O.worlds[1], O.worlds[2]
check(actions.myLevelFolders[game_folders + 1] == 'oe_vegetoids' and actions.myLevelFolders[game_folders + 2] == 'oe_omelettification',
      'two folders after the game\'s ' .. game_folders)
check(#actions.myLevels.oe_vegetoids == 33 and actions.myLevels.oe_vegetoids[2] == 'OEV_Level1', 'Vege-toids: 33 entries, OEV_Level1 second')
check(actions.myLevels.oe_vegetoids[31] == 'OEV_Boss', 'Vege-toids boss is OEV_Boss')
for _, n in ipairs(actions.myLevels.oe_omelettification) do io.write(n, ' ') end print()
local function all_levels_have_stars(list)
  for _, n in ipairs(list) do
    if n:find('^OE') and not starTable[n] then return false, n end
  end
  return true
end
check(all_levels_have_stars(actions.myLevels.oe_vegetoids), 'Vege-toids star limits')
check(all_levels_have_stars(actions.myLevels.oe_omelettification), 'Omelettification star limits')
local em = actions.myEpisodesMetadata.oe_vegetoids
check(em and em.levelNumberPrefix == 'V' and not em.leaderBoard and not em.antennaEggLevelIndexes, 'episode metadata: prefix, no leaderboard or antenna eggs')

-- no game table was changed
local changed = {}
for k, v in pairs(game_blocks) do if blockTable.blocks[k] ~= v then changed[#changed + 1] = k end end
check(#changed == 0, 'the game\'s own blocks untouched (' .. table.concat(changed, ' ') .. ')')
for k, v in pairs(game_themes) do if blockTable.themes[k] ~= v then check(false, 'theme ' .. k .. ' changed') end end

-- themes
for _, t in ipairs({'themeGA1', 'themeGA2', 'themeGA2Boss'}) do
  local th = blockTable.themes[t]
  check(th and th.assetPrefix and th.noIngame, 'theme ' .. t .. ' registered (' .. tostring(th and th.assetPrefix) .. ')')
end

-- every definition the worlds use is known
local lo = mod_env('scripts/levelOrder.lua')
local missing = {}
for _, W in ipairs(O.worlds) do
  for _, n in ipairs(lo.myLevels[W.mod]) do
    local e = mod_env('levels/' .. W.mod .. '/' .. n .. '.lua')
    for _, o in pairs(e.world or {}) do
      if type(o) == 'table' and o.definition and not blockTable.blocks[o.definition] and not (O.over_blocks or {})[o.definition] then
        missing[o.definition] = true
      end
    end
  end
end
local m = {} for k in pairs(missing) do m[#m + 1] = k end table.sort(m)
check(#m == 0, 'every object is defined (' .. table.concat(m, ' ') .. ')')

-- the theme loads its groups
levelFolder = 'levels/oe_vegetoids/'
O.frame()
check(O.applied, 'in Vege-toids: the mod\'s birds')
check(O.theme_hooked and O.events_hooked and O.powers_hooked and O.planets_hooked, 'hooks in place')
check(blockTable.blocks.RedBigBird.specialty == 'BULK', 'Big Red is BULK there')
checkAndLoadTheme('themeGA1')
local th = blockTable.themes.themeGA1
check(th.loadList and th.loadList.groups and #th.loadList.groups >= 1, 'themeGA1 groups: ' .. table.concat(acquired, ' '))
local g = groups[acquired[1]]
check(g and #g.entries > 1, 'its group of the mod\'s sheets: ' .. (g and #g.entries or 0) .. ' entries')
if g then for _, e in ipairs(g.entries) do io.write(e[1], ' ') end print() end
local hastex = false
for _, e in ipairs(g and g.entries or {}) do if e[1] == 'OE_INGAME_TEXTURE_EARTH_1.dat' then hastex = true end end
check(hastex and th.texture == 'OE_INGAME_TEXTURE_EARTH_1', 'themeGA1: its ground texture sheet')

-- every object's ground texture as the level loader picks it (game.lua:
-- a themed block takes the current theme's, or its own themeTexture, its
-- definition's, the level theme's) through setTexture: one loaded with
-- the level's theme, or a sheet of the game's
do
  local game_sheet = {}
  local ll = native.loadLuaTable('images/1024x768_android/loadlist.lua')
  for _, gs in pairs(ll and ll.assetLoadList or {}) do
    for _, entries in pairs(gs) do
      for _, e in ipairs(entries) do local f = type(e) == 'table' and e[1] or e if type(f) == 'string' then game_sheet[f] = true end end
    end
  end
  local mlo = mod_env('scripts/levelOrder.lua')
  local n_obj, n_levels, bad, names = 0, 0, {}, {}
  for _, mod in ipairs({'theme1', 'theme2'}) do
    for _, n in ipairs(mlo.myLevels[mod]) do
      local env = mod_env('levels/' .. mod .. '/' .. n .. '.lua')
      if type(env.world) == 'table' and type(env.theme) == 'string' then
        n_levels = n_levels + 1
        checkAndLoadTheme(env.theme)
        currentTheme = env.theme
        local th = blockTable.themes[env.theme]
        local files = {}
        for _, e in ipairs((groups[acquired[1]] or {}).entries or {}) do files[e[1]] = true end
        for on, o in pairs(env.world) do
          local d = blockTable.blocks[o.definition]
          if d and d.themed then
            local tex
            if d.useCurrentTheme ~= nil then tex = blockTable.themes[currentTheme].texture
            else tex = o.themeTexture or d.themeTexture or th.texture end
            texture_set = nil
            setTexture(on, tex)
            n_obj = n_obj + 1
            local t = texture_set
            names[tostring(tex) .. '>' .. tostring(t)] = true
            if not (type(t) == 'string' and (files[t .. '.dat'] or game_sheet[t .. '.dat'])) then
              bad[#bad + 1] = n .. ':' .. on .. '=' .. tostring(t)
            end
          end
        end
      end
    end
  end
  local l = {} for k in pairs(names) do l[#l + 1] = k end table.sort(l)
  check(#bad == 0 and n_obj > 0, n_obj .. ' themed objects in ' .. n_levels .. ' levels, each texture loaded with its theme (' .. table.concat(l, ' ') .. ')' .. (#bad > 0 and (' -- not: ' .. table.concat(bad, ' ', 1, math.min(#bad, 8))) or ''))
end
texture_set = nil
setTexture('x', 'INGAME_TEXTURE_SAND_1')
check(texture_set == blockTable.themes[currentTheme].texture and texture_set:find('^OE_'), 'in these worlds a ground texture not loaded is the theme\'s (' .. tostring(texture_set) .. ')')
levelFolder = 'levels/theme1/'
O.frame()
check(not O.applied and blockTable.blocks.RedBigBird.specialty == 'SOUND', 'back in Pig Bang: the game\'s own Big Red')
texture_set = nil
setTexture('x', 'INGAME_COMMON_TEXTURE_SAND')
check(texture_set == 'INGAME_COMMON_TEXTURE_SAND', 'elsewhere the game\'s textures as they are')

-- planets
local page = setmetatable({}, menu.EpisodeSelection)
local scene = native.loadLuaTable('scenes/EpisodeSelection.lua', true)
check(scene.entities.oe_vegetoids and scene.entities.oe_omelettification, 'two planet entities')
page._buttonCount = 13
check(page._buttonCount == 15, 'the carousel has 15 places (' .. tostring(page._buttonCount) .. ')')
-- a level load rebuilds actions: the episodes come back
initializeEventSystem()
check(actions.myLevelFolders[19] == 'oe_vegetoids' and actions.myLevels.oe_omelettification ~= nil, 'episodes back after initializeEventSystem')
check(actions.RedBigBirdCollided ~= nil and actions.useGrenadeSpecialty ~= nil and actions.BossOCollisionEnter ~= nil, 'the mod\'s actions added')
check(page._groups and page._groups[1] == 'OE_PLANETS', 'the planets\' sprite group is required')
local e1 = scene.entities.oe_vegetoids
check(e1.pos.metaData.index == 14, 'Vege-toids is place 14')
local lm = actions.myLevelsMetadata
check(lm.OEV_Level1 and lm.OEV_Level1.doesNotchangeGlobalTheme and lm.OEO_Boss and lm.OEO_Boss.lastLevelOfEpisode and not lm.OEO_Boss.levelAchievement, 'level metadata')
local c1 = lm.LevelComicOEV_1
check(actions.myLevels.oe_vegetoids[1] == 'LevelComicOEV_1' and c1 and c1.type == 'comic', 'Vege-toids opens with its own comic, LevelComicOEV_1')
check(c1 and c1.comicFrames[1].spriteName == 'COMIC_1_FRAME_1_1' and c1.comicFrames[4].spriteName == 'OE_COMIC_1_FRAME_3_1', 'its frames: the mod\'s new names as they are, the game\'s as OE_ (' .. tostring(c1 and c1.comicFrames[4].spriteName) .. ')')
check(c1 and c1.comicFrames.groups and c1.comicFrames.groups[1] == 'OE_LEVELCOMICOEV_1' and groups.OE_LEVELCOMICOEV_1, 'its pictures\' group')
check(lm.LevelComic1 and lm.LevelComic1.comicFrames[1].spriteName == 'COMIC_1_FRAME_1', 'Pig Bang keeps its own comic')
check(lm.LevelComicOEO_3part2 and lm.LevelComicOEO_3part2.comicFrames.groups[1] == 'OE_LEVELCOMICOEO_3PART2', 'Omelettification\'s second comic')
check(lm.LevelComicOEV_2 and lm.LevelComicOEV_2.episodeEndComic, 'the closing comic ends the episode')

-- each comic as the level loader builds it (game.lua's comic step), with
-- the game's own Frame, Image and ComicPage: frames added by name, then the
-- page entered
do
  local E = setmetatable({}, {__index = _G})
  local loaded = {['ui/ui.lua'] = true, ['menu/menu.lua'] = true, ['Assets.lua'] = true}
  E.ui, E.menu = {}, {}
  E.requireFile = function(rel)
    if loaded[rel] then return end
    loaded[rel] = true
    local f = assert(loadfile('lua_dec/scripts/' .. rel))
    setfenv(f, E) f()
  end
  local got = {}
  E.Assets = {acquireGroup = function(g) got[#got + 1] = g end, acquireSpriteSheet = function() end,
              acquireCompoSprites = function() end, releaseGroup = function() end,
              releaseSpriteSheet = function() end, releaseCompoSprites = function() end}
  E.res = {getString = function() return nil end, getSpriteBounds = function() return 100, 80 end,
           getSpritePivot = function() return 50, 40 end}
  E.screenWidth, E.screenHeight = 1280, 720
  E.ReferenceScaler = {getWorldScale = function() return 1 end}
  local ok, er = pcall(E.requireFile, 'menu/ComicPage.lua')
  check(ok and E.menu.ComicPage, 'the game\'s comic page loads (' .. tostring(er) .. ')')
  local function build(name)
    local m = lm[name]
    local cf = m.comicFrames
    local page = E.menu.ComicPage:new('comicPage', name, cf.initialTimer, cf.groups)
    for _, f in ipairs(cf) do page:addFrame(f.name, f.spriteName, f.timer, f.pos, f.sound, f.scale) end
    local base = E.ui.Frame:new('notifications')
    base._entered = true
    got = {}
    base:addChild(page)
    return page
  end
  local comics = {'LevelComic1'}
  for _, f in ipairs({'oe_vegetoids', 'oe_omelettification'}) do
    for _, n in ipairs(actions.myLevels[f]) do
      if lm[n] and lm[n].type == 'comic' then comics[#comics + 1] = n end
    end
  end
  for _, n in ipairs(comics) do
    local ok2, page = pcall(build, n)
    check(ok2 and #page.children == #lm[n].comicFrames and got[1] == lm[n].comicFrames.groups[1],
          'comic ' .. n .. ': ' .. (ok2 and (#page.children .. ' frames, group ' .. tostring(got[1])) or tostring(page)))
  end
end
do
  local g = groups.OE_TUTORIALS
  local l = {}
  for _, e in ipairs(g and g.entries or {}) do l[#l + 1] = e[1] end
  check(g and #l > 0, 'the mod\'s tutorial pictures, a group of their own (' .. table.concat(l, ' ') .. ')')
end
print(O.report())
print(string.format('%d ok, %d failed', oks, fails))
print(fails == 0 and 'ALL OK' or 'SOME FAILED')
-- the sounds the worlds name that the game lacks
local modsounds = {}
local text = math.frexp('orb:text', 'scripts/soundManager.lua')
for path, name in text:gmatch('createAudio%(%s*audioPath%s*%.%.%s*"([^"]+)"%s*,%s*"([^"]+)"') do modsounds[name] = path end
local strs = {}
local function strings(v, d) d = d or 0 if d > 8 then return end if type(v) == 'string' then strs[v] = true elseif type(v) == 'table' then for k, x in pairs(v) do strings(x, d + 1) end end end
local used = {}
for _, W in ipairs(O.worlds) do
  for _, n in ipairs(lo.myLevels[W.mod]) do
    local e = mod_env('levels/' .. W.mod .. '/' .. n .. '.lua')
    for _, o in pairs(e.world or {}) do if type(o) == 'table' and o.definition then used[o.definition] = true end end
    strings(e.world)
  end
  for _, info in pairs(W.info) do if info.theme and blockTable.themes[info.theme] then strings(blockTable.themes[info.theme]) end end
end
for d in pairs(used) do
  local def = (O.over_blocks or {})[d] or blockTable.blocks[d]
  strings(def)
  if def and def.material then strings(blockTable.materials[def.material]) end
end
local need = {}
for s in pairs(strs) do if modsounds[s] and not audioData.sounds[s] then need[#need + 1] = s end end
table.sort(need)
print('sounds the worlds need from the mod: ' .. #need)
print('  ' .. table.concat(need, ', '))
local notfound = {}
for s in pairs(strs) do if s:find(' ') and not modsounds[s] and not audioData.sounds[s] then notfound[#notfound + 1] = s end end
table.sort(notfound)
print('named, in neither: ' .. table.concat(notfound, ', '))
-- the game's actions
requireFile = function() end
local game_actions = {}
do
  local acts = {}
  for f in io.popen('ls lua_dec/scripts/actions*.lua lua_dec/scripts/powerups.lua'):lines() do
    local e = setmetatable({}, {__index = _G})
    rawset(e, 'actions', acts)
    local ok, err = pcall(function()
      local fn = assert(loadfile(f)) setfenv(fn, e) fn()
    end)
    if not ok then print('  (' .. f .. ': ' .. tostring(err) .. ')') end
    acts = rawget(e, 'actions') or acts
    for k, v in pairs(acts) do if type(v) == 'table' and v.action then game_actions[k] = true end end
  end
  GAME_ACTIONS = acts
end
-- the mod's actions (a table of their own)
local mod_actions = {}
for _, f in ipairs({'actions', 'actions_birds', 'actions_boss1', 'actions_boss2', 'actions_water', 'actions_radiation',
                   'actions_pigEmotions', 'actions_bonus', 'actions_menuStructure', 'powerups'}) do
  local rel = 'scripts/' .. f .. '.lua'
  local fn = math.frexp('orb:load', rel)
  if type(fn) == 'function' then
    local e = setmetatable({gamelua = _G}, {__index = _G})
    setfenv(fn, e)
    local ok, err = pcall(fn)
    if not ok then print('  (' .. rel .. ': ' .. tostring(err) .. ')') end
    for k, v in pairs(e) do if type(v) == 'table' and type(v.action) == 'function' then mod_actions[k] = rel end end
  else print('  (' .. rel .. ': ' .. tostring(fn) .. ')') end
end
local na, nm = 0, 0 for _ in pairs(game_actions) do na = na + 1 end for _ in pairs(mod_actions) do nm = nm + 1 end
print(('actions: the game %d, the mod %d'):format(na, nm))
local refs = {}
for s in pairs(strs) do if mod_actions[s] or game_actions[s] then refs[s] = true end end
local missing_act, have = {}, {}
for s in pairs(refs) do if game_actions[s] then have[#have+1] = s else missing_act[#missing_act + 1] = s .. '(' .. mod_actions[s] .. ')' end end
table.sort(missing_act) table.sort(have)
print('actions the worlds name, the game has: ' .. table.concat(have, ' '))
print('actions the worlds name, only the mod has: ' .. table.concat(missing_act, ' '))

-- ============================================ the powers, against stubs
-- every global the game has (its scripts' and its engine's names) answers
-- as a stub; a name it lacks is nil, and calling it fails the test
local ANDROID = {}
for l in io.lines('android_globals.txt') do ANDROID[l] = true end
for l in io.lines('so_strings.txt') do ANDROID[l] = true end
local calls = {}
local function stub(name) return function(...) calls[name] = (calls[name] or 0) + 1 return 0, 0 end end
setmetatable(_G, {__index = function(_, k) if ANDROID[k] then local f = stub(k) rawset(_G, k, f) return f end end})
local W = {}
objects = {world = W, worldGravity = 10, castleCameraData = {android = {px = 3000, py = 0}}, birdCameraData = {android = {px = 0, py = 0}}}
birds, updateCalls, events = {}, {}, {}
cursor, cursorPhysics = {x = 500, y = 300}, {x = 50, y = 30}
boostForce, physicsScale, worldScale, deviceModel = 10, 1, 1, 'android'
levelLeftEdgePhysics, levelRightEdgePhysics, levelTopEdgePhysics, levelBottomEdgePhysics = -100, 100, -100, 100
res = {playAudio = function() end, isAudioPlaying = function() return false end, stopAudio = function() end,
       getSpriteBounds = function() return 10, 10 end, getSpritePivot = function() return 5, 5 end, drawSprite = function() end}
SettingsWrapper = {markTutorialShown = function() end}
local nobj = 0
function createObject(bt, def, name, x, y)
  assert(bt.blocks[def], 'createObject: no ' .. tostring(def))
  nobj = nobj + 1
  name = name or (def .. '_' .. nobj)
  W[name] = {name = name, definition = def, x = x, y = y, xVel = 0, yVel = 0, angle = 0, mass = 1, velocity = 0}
  return name
end
function getObjectDefinition(n) return W[n] and blockTable.blocks[W[n].definition] end
function checkStillInWorld(n) return W[n] ~= nil end
function removeBird(b) W[b.name] = nil birds[b.name] = nil if flyingBird == b then flyingBird = nil birdSpecialtyAvailable = false end end
function destroyObject(n) W[n] = nil end
function vLength(x, y) return math.sqrt(x * x + y * y) end
function vNormalize(x, y) local l = math.sqrt(x * x + y * y) if l == 0 then return 0, 0 end return x / l, y / l end
function distance(a, b, c, d) return vLength(c - a, d - b) end
function dotProduct(a, b, c, d) return a * c + b * d end
function physicsToScreenTransform(x, y) return x * 10, y * 10 end
function screenToPhysicsTransform(x, y) return x / 10, y / 10 end
function physicsToWorldTransform(x, y) return x * 100, y * 100 end
function worldToPhysicsTransform(x, y) return x / 100, y / 100 end
function getAudioName(n) return n end
function getLinearVelocity(n) return W[n].xVel, W[n].yVel end
function setVelocity(n, x, y) if W[n] then W[n].xVel, W[n].yVel = x, y end end
function setPosition(n, x, y) if W[n] then W[n].x, W[n].y = x, y end end
function getIntersectingObjects(b)
  local t = {}
  for k, v in pairs(W) do
    if v.x >= b.x + b.left and v.x <= b.x + b.right and v.y >= b.y + b.down and v.y <= b.y + b.up then t[#t + 1] = k end
  end
  return t
end
function getSquaredDistanceFromOBBToPoint(x, y, w, h, a, px, py) return {distance = (x - px) ^ 2 + (y - py) ^ 2} end
function addEventCall(name, p, t) events[#events + 1] = {name, p, t or 0} end
function addUpdateCall(name, p, t, key) updateCalls[key or #updateCalls + 1] = {actionName = name, params = p} end
function removeUpdateCall(k) updateCalls[k] = nil end
local function frames(n)
  for _ = 1, n do
    for _, u in pairs(updateCalls) do u.params.dt = 1 / 60 actions[u.actionName].action(u.params) end
    local ev = events
    events = {}
    for _, e in ipairs(ev) do
      if e[3] > 0 then e[3] = e[3] - 1 / 60 events[#events + 1] = e
      elseif actions[e[1]] then e[2] = e[2] or {} e[2].dt = 1 / 60 actions[e[1]].action(e[2]) end
    end
    GameScene.updateActions(GameScene, 1 / 60)
  end
end
local function launch(def)
  for k in pairs(W) do W[k] = nil end
  updateCalls, events = {}, {}
  W.bird = {name = 'bird', definition = def, x = 0, y = 0, xVel = 10, yVel = -5, angle = 0, mass = 1, shot = true, velocity = 11, controllable = true}
  W.block = {name = 'block', definition = 'BLOCK_WOOD_2X4_1', x = 12, y = 1, xVel = 0, yVel = 0, angle = 0, mass = 2, velocity = 0}
  birds.bird = W.bird
  flyingBird = W.bird
  birdSpecialtyAvailable = false -- the game's tap handler clears it before the action
end
local function tap(spec) fireAction(nil, {name = flyingBird and flyingBird.name, specialty = spec, dt = 0}) end
levelFolder = 'levels/oe_omelettification/'
O.frame()
check(O.applied, 'in Omelettification')
local function no_errors(what)
  local r = O.report()
  check(not r:find('error'), what .. (r ~= '' and (' -- ' .. r) or ''))
end
launch('IronBird') tap('GRENADE') frames(3)
local egg = false for _, v in pairs(W) do if v.definition == 'EggDroid' then egg = true end end
check(egg, 'Iron bird: its egg is out') no_errors('Iron bird')
launch('BoomerangBird') tap('BOOMERANG') frames(30)
check(birdSpecialtyAvailable == true, 'Boomerang: a second tap is offered')
tap('BOOMERANG') frames(10) no_errors('Boomerang')
launch('BlackHoleBird') tap('BLACK_HOLE') frames(20)
check(flyingBird and flyingBird.definition == 'BlackHoleBirdIn' and birdSpecialtyAvailable == true, 'Black Hole: the hole, and a second tap')
local hole = flyingBird
tap('BLACK_HOLE') frames(20)
check(flyingBird and flyingBird.definition == 'BlackHoleBird', 'Black Hole: out again') no_errors('Black Hole')
check(hole and birds[hole.name] == nil and flyingBird and birds[flyingBird.name] == flyingBird,
      'Black Hole: the switched-off hole leaves the list of birds a lost level waits for, the bird out of it joins')
-- the black portals' swirl: drawn over each, turning (the mod's scene)
do
  local drawn = 0
  local rd, rb, rp = res.drawSprite, res.getSpriteBounds, res.getSpritePivot
  res.drawSprite = function(n) if n == 'WORMHOLE_MASK' then drawn = drawn + 1 end end
  res.getSpriteBounds = function() return 221, 233 end
  res.getSpritePivot = function() return 97, 127 end
  for k in pairs(W) do W[k] = nil end
  W.portal = {name = 'portal', definition = 'BLOCK_SENSOR_BLACK_PORTAL_1', x = 3, y = 4, angle = 0, sensorType = 'portal'}
  frames(2)
  local a1 = W.portal.oeSwirl
  frames(2)
  GameScene.draw(GameScene)
  res.drawSprite, res.getSpriteBounds, res.getSpritePivot = rd, rb, rp
  check(drawn == 1 and a1 and W.portal.oeSwirl and W.portal.oeSwirl > a1, 'a black portal gets its turning swirl (' .. drawn .. ')')
  no_errors('portals')
end
-- a shot bird that does not move for 3 s while its speed says it does
-- (no body) counts as done
launch('RedBird') frames(200)
check(W.bird.hasCollided == true, 'a bird stuck without a body counts as done after 3 s')
launch('DrillBird') tap('DRILL') frames(70)
check(W.bird and W.bird.specialUsed, 'Drill: bored in, then off') no_errors('Drill')
launch('PinkBird') cursorPhysics.x, cursorPhysics.y = 12, 1 cursor.x, cursor.y = 120, 10 tap('GRAVITY_DISRUPTOR') frames(30)
check(W.block.bubbleAntiGravityTimer ~= nil or W.block.inBubble, 'Pink: the block is lifted')
check(birdSpecialtyAvailable == true, 'Pink: a second tap is offered')
tap('GRAVITY_DISRUPTOR') frames(5)
check(not W.block.bubbleAntiGravityTimer, 'Pink: the second tap lets it go') no_errors('Pink')
launch('RedBigBird')
check(blockTable.blocks.RedBigBird.onCollisionEnter == 'RedBigBirdCollided', 'Big Red: its landing')
actions.RedBigBirdCollided.action({name = 'bird', other = 'block'})
check(W.bird.abilityActivated, 'Big Red: the shockwave') no_errors('Big Red')
launch('BlackBird') birdSpecialtyAvailable = true
local saved_hit = birdCollision
birdCollision('bird', 'block')
check(birdSpecialtyAvailable == true, 'Black bird: still armed after a hit')
frames(160)
check(birdSpecialtyAvailable == false, 'Black bird: 2.5 s later, no more') no_errors('Black bird')
-- outside the worlds: the game's own
levelFolder = 'levels/theme3/'
O.frame()
check(not O.applied and blockTable.blocks.BlackBird.specialtyActivationDelay == nil, 'Cold Cuts: the game\'s own birds')
local passed = false
local of = fireAction
check(select('#', fireAction(nil, {name = 'x', specialty = 'GRENADE'})) == 0, 'outside: a GRENADE tap does nothing of ours')
print(string.format('%d ok, %d failed', oks, fails))
print(fails == 0 and 'ALL OK' or 'SOME FAILED')
local t = {} for k, v in pairs(calls) do t[#t + 1] = k .. '=' .. v end table.sort(t) print('stubbed calls: ' .. table.concat(t, ' '))
-- the planets as the mod draws them
dofile('dumpt.lua')
for _, id in ipairs({'oe_vegetoids', 'oe_omelettification'}) do
  local ent = scene.entities[id]
  local c = ent.def.condition.falseDef.composite
  local t = {}
  -- where the scene draws each part: its pos times its scale
  local far, near_sign = {}, false
  for i, e in ipairs(c) do
    local sc = e.scale or 1
    local x, y = (e.pos and e.pos.x or 0) * sc, (e.pos and e.pos.y or 0) * sc
    t[#t + 1] = (e.sprite or (e.condition and 'scorebox') or '?') .. (e.pos and string.format('@%.0f,%.0f', x, y) or '') .. (e.scale and string.format('x%.2f', e.scale) or '')
    if e.sprite and i > 1 and not e.sprite:find('SIGN') then
      local r = math.sqrt(x * x + y * y)
      if r < 110 or r > 215 then far[#far + 1] = e.sprite .. string.format('@%.0f', r) end
    end
    if e.sprite and e.sprite:find('PLANET_SIGN') and math.abs(y - 95) < 1 then near_sign = true end
  end
  print(id, #c, table.concat(t, ' '))
  check(#far == 0 and near_sign, id .. ': the decorations drawn round the planet\'s edge, the sign where the game\'s are' .. (#far > 0 and (' -- off: ' .. table.concat(far, ' ')) or ''))
end
print('planet groups: ' .. table.concat(O.planet_groups or {}, ' '))
check(O.planet_groups and O.planet_groups[1] == 'OE_PLANETS', 'the planets\' groups')
local pg = groups.OE_PLANETS
if pg then for _, e in ipairs(pg.entries) do io.write(e[1], '/', e[2], ' ') end print() end
local tg = groups.OE1_THEME
local compo = false
for _, e in ipairs(tg and tg.entries or {}) do if e[2] == 1 then compo = e[1] end end
check(compo == 'OE1_THEME_COMPOSPRITES.dat', 'the theme group\'s composites named for the group (' .. tostring(compo) .. ')')
local body = orb_asset('data/images/oe/OE1_THEME_COMPOSPRITES.dat')
check(body and body:sub(1, 4) == 'KA3D' and body:sub(9, 12) == 'COMP' and #body > 100, 'OE1_THEME_COMPOSPRITES.dat is served (' .. (body and #body or 0) .. ' bytes)')
local full = orb_asset('data/images/oe/OEF_STORY_1_COMIC_1.dat')
check(full and full:find('OE_COMIC_1_FRAME_3_1', 1, true) and full:find('COMIC_1_FRAME_1_1', 1, true) and full:find('OE_STORY_1_COMIC_1.png', 1, true), 'the comic sheet served whole, the game\'s names as OE_')
local function served_level(path)
  local b = orb_asset(path)
  if not b then return nil end
  local f = io.open('served.tmp', 'wb') f:write(b) f:close()
  local p = io.popen('python3 -c "import sys; sys.path.insert(0, \'.\'); from decrypt import dec; o = dec(\'served.tmp\'); open(\'served.plain\', \'wb\').write(o or b\'\')"')
  p:read('*a') p:close()
  local e = setmetatable({}, {__index = _G})
  local fn = loadfile('served.plain')
  if not fn then return nil end
  setfenv(fn, e) pcall(fn)
  return e.filename
end
check(served_level('data/levels/oe_vegetoids/OEV_Level1.lua') == 'OEV_Level1.lua', 'OEV_Level1 declares its own name')
check(served_level('data/levels/oe_vegetoids/LevelComicOEV_1.lua') == 'LevelComicOEV_1.lua', 'LevelComicOEV_1 declares its own name')
check(served_level('data/levels/oe_omelettification/OEO_Boss.lua') == 'OEO_Boss.lua', 'OEO_Boss declares its own name')
check(served_level('data/levels/oe_vegetoids/OEV_Level119.lua') == 'OEV_Level119.lua', 'a bytecode level renamed in its constants')
check(served_level('data/levels/oe_vegetoids/LevelSelection.lua') == 'LevelSelection.lua', 'the level selection as it is')
local empty = orb_asset('data/images/oe/OE_NOT_A_SHEET.dat')
check(empty and empty:sub(1, 4) == 'KA3D', 'a missing sheet is served empty')
print(string.format('%d ok, %d failed', oks, fails))
print(fails == 0 and 'ALL OK' or 'SOME FAILED')
-- every sound registered from the mod is a file of the mod's
local missing_audio = {}
local nsnd = 0
for name, path in pairs(audio_made) do
  local rel = path:match('^audio/oe/(.*)$') or path:match('^data/audio/oe/(.*)$')
  if rel then
    nsnd = nsnd + 1
    -- as the engine reads it, through the overlay
    if not orb_asset('data/audio/oe/' .. rel) then missing_audio[#missing_audio + 1] = name .. ' (' .. rel .. ')' end
  end
end
check(#missing_audio == 0, nsnd .. ' mod sounds, all present (' .. table.concat(missing_audio, ', ') .. ')')
print(string.format('%d ok, %d failed', oks, fails))
local ov = {}
for n in pairs(O.over_blocks or {}) do ov[#ov + 1] = n end
table.sort(ov)
print('OVERRIDES ' .. table.concat(ov, ' '))
