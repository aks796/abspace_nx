-- loadtest.lua -- after orbtest.lua's registration: the game's own level
-- loader (game.lua loadLevelInternal, gamelogic.lua, math.lua, the real
-- scripts) run over levels, the engine's natives stubbed, then the
-- level-dependent steps of GameScene.update for each level's first seconds.
-- Calibrated on the game's levels; then every level of the two worlds.
--   QUIET=1 ./orbhost orbroot <assets> loadtest.lua <abs_orbital.lua>
dofile('orbtest.lua')
local O = __abs_orbital
print('==== the level loader')
local fails, oks = 0, 0
local function check(ok, what)
  if ok then oks = oks + 1 else fails = fails + 1 end
  print((ok and 'ok: ' or 'FAIL: ') .. what)
end

-- everything the engine provides: callable, indexable, returns nothing
local calls = {}
local autos = setmetatable({}, {__mode = 'k'}) -- stand-in -> {parent, used}
local function auto(name, parent)
  local t = {}
  autos[t] = {parent = parent}
  return setmetatable(t, {
    __index = function(_, k) local v = auto(name .. '.' .. tostring(k), t) rawset(t, k, v) return v end,
    __call = function()
      calls[name] = (calls[name] or 0) + 1
      local a = t
      while a and autos[a] and not autos[a].used do autos[a].used = true a = autos[a].parent end
    end,
  })
end
local real_G = {}
for k, v in pairs(_G) do real_G[k] = v end
-- globals the game leaves nil until it sets them (not stand-ins)
local NILS = {}
local function nils(...) for _, k in ipairs({...}) do NILS[k] = true rawset(_G, k, nil) end end
local strict = false -- once playing: a global the game has not set is nil
setmetatable(_G, {__index = function(_, k)
  if type(k) ~= 'string' or NILS[k] or strict then return nil end
  local v = auto(k) rawset(_G, k, v) return v
end})

local loaded = {}
function requireFile(rel)
  if loaded[rel] then return end
  loaded[rel] = true
  local f = loadfile('lua_dec/scripts/' .. rel)
  if not f then return end
  local ok, e = pcall(f)
  if not ok then print('  (' .. rel .. ': ' .. tostring(e) .. ')') end
end

-- the engine's side of it
native = native or {}
native.loadLuaScript = function(rel)
  local f = loadfile('lua_dec/scripts/' .. rel)
  if not f then print('  (no script ' .. rel .. ')') return end
  local ok, e = pcall(f)
  if not ok then print('  (' .. rel .. ': ' .. tostring(e) .. ')') end
end
local engine_errors = {}
local function engine_error(e) engine_errors[#engine_errors + 1] = e end
objects = {world = {}, counts = {}, joints = {}, animatedObjects = {}}
local function createObject_(bt, def, name, x, y, radius, width, height, sx, sy, vertices, z)
  local d = bt.blocks[def]
  if type(d) ~= 'table' then engine_error('createObject: no definition ' .. tostring(def)) d = {} end
  local o = {}
  for k, v in pairs(d) do o[k] = v end
  o.name, o.definition, o.x, o.y = name, def, x, y
  o.radius = radius or d.radius or 1
  o.width = width or d.width or o.radius * 2
  o.height = height or d.height or o.radius * 2
  o.angle = 0
  objects.world[name] = o
  return name
end
-- the level file, as the engine serves it (the overlay for the worlds'),
-- run in a table of its own; its filename must be the one asked for
local function level_env(path, fname)
  -- the game asks for 'levels/<folder>/<name>' (the engine adds .lua)
  if fname:find('/') then path, fname = fname:match('^(.*/)([^/]+)$') end
  if not fname:find('%.lua$') then fname = fname .. '.lua' end
  local fn
  if path:find('/oe_') then
    local b = orb_asset('data/' .. path .. fname)
    if not b then engine_error('no level file ' .. path .. fname) return nil end
    local f = io.open('served.tmp', 'wb') f:write(b) f:close()
    local p = io.popen('python3 -c "import sys; sys.path.insert(0, \'.\'); from decrypt import dec; o = dec(\'served.tmp\'); open(\'served.plain\', \'wb\').write(o or b\'\')"')
    p:read('*a') p:close()
    fn = loadfile('served.plain')
  else
    fn = loadfile('lua_dec/' .. path .. fname)
  end
  if not fn then engine_error('level file unreadable: ' .. path .. fname) return nil end
  local e = {}
  setfenv(fn, e)
  local ok, er = pcall(fn)
  if not ok then engine_error('level file: ' .. tostring(er)) end
  if e.filename ~= fname then engine_error('Filename is missing from level file (' .. tostring(e.filename) .. ')') end
  return e
end
local last_extent -- the level's objects' extent (the loader empties the file's table)
local last_birds, last_level_objs
nils('last_kbox') -- no box made: nil (the bosses have none, as the mod's)
local function loadLevel_(fname)
  loadedObjects = level_env(levelFolder, fname)
  local minx, maxx, miny, maxy = 1e9, -1e9, 1e9, -1e9
  for _, o in pairs(type(loadedObjects) == 'table' and loadedObjects.world or {}) do
    minx, maxx = math.min(minx, o.x), math.max(maxx, o.x)
    miny, maxy = math.min(miny, o.y), math.max(maxy, o.y)
  end
  last_extent = {minx, maxx, miny, maxy}
  last_birds, last_level_objs = {}, {}
  for _, o in pairs(type(loadedObjects) == 'table' and loadedObjects.world or {}) do
    if o.startNumber and (o.definition or ''):find('Bird') then last_birds[#last_birds + 1] = o end
    last_level_objs[#last_level_objs + 1] = {name = o.name, x = o.x, y = o.y}
  end
  last_kbox = nil
end

screenWidth, screenHeight = 1280, 720
physicsToWorld, worldScale, physicsScale = 1, 1, 1
local function fill(t, name)
  return setmetatable(t, {__index = function(_, k) local v = auto(name .. '.' .. tostring(k)) rawset(t, k, v) return v end})
end
fill(native, 'native')
fill(native.ResourceManager, 'native.ResourceManager')
rawset(_G, 'slingshot', nil) -- the power tests' stand-in: the game's own slingshot.lua loads below
for _, n in ipairs({'res', 'GameScene', 'SettingsWrapper', 'settings', 'powerups', 'menu', 'ui', 'GameSystem', 'mirrorWorldHandler'}) do
  if type(rawget(_G, n)) == 'table' then fill(rawget(_G, n), n) end
end

-- a fresh save (settingsWrapper.lua fills in its defaults as it loads)
settings = fill({root = {}}, 'settings')
-- the game's scripts
for _, f in ipairs({'math.lua', 'gamelogic.lua', 'game.lua', 'soundManager.lua', 'slingshot.lua', 'camera.lua'}) do requireFile(f) end
for _, dir in ipairs({'ui', 'menu'}) do
  for f in io.popen('ls lua_dec/scripts/' .. dir .. ' | grep "\\.lua$"'):lines() do requireFile(dir .. '/' .. f) end
end
print('  loadLevelInternal: ' .. type(rawget(_G, 'loadLevelInternal')))
-- the start-up step that picks the camera sets for the screen
screenWidth, screenHeight = 1280, 720
loadFonts, setFont, getFontBasic = function() end, function() end, function() end
local ok, e = pcall(createStartUpAssets)
if not ok then print('  (createStartUpAssets: ' .. tostring(e) .. ')') end
-- and this port's script hooked into them, as at start

local function trace(e)
  local t = {tostring(e)}
  for l = 2, 12 do
    local i = debug.getinfo(l, 'Sn')
    if not i then break end
    local pc = orb_pc(l)
    t[#t + 1] = string.format('%s@%s:%d%s', tostring(i.name), i.short_src, i.linedefined, pc and (' pc ' .. pc) or '')
  end
  return table.concat(t, ' < ')
end
-- blocks, themes, materials were loaded with the main menu (the first
-- level; its once-flag can't be set in stripped code): the reload finds no
-- files, and what it requires was loaded at start
loadLuaFile = function() end
native.FileSystem = fill({enumerate = function() return {} end}, 'native.FileSystem')
loaded['particles.lua'] = true
-- the engine's numbers (set after the scripts, which copy natives)
local function numbers()
  for _, k in ipairs({'physicsToWorld', 'worldScale', 'physicsScale', 'maxZoomLevel', 'currentZoomedScale', 'minWorldScale', 'maxWorldScale', 'defaultPhysicsDrag'}) do
    if type(rawget(_G, k)) ~= 'number' then rawset(_G, k, 1) end
  end
  physicsToWorld, physicsScale = 20, 1 / 20 -- the game's: its levels' 20 world units to 1
  screenWidth, screenHeight = 1280, 720
  loadLevel = loadLevel_
  -- a fresh save; the sound manager's lists
  if type(rawget(_G, 'soundManager')) == 'table' then soundManager.levelSpecificAudio = soundManager.levelSpecificAudio or {} end
  if type(rawget(_G, 'highscores')) ~= 'table' or getmetatable(highscores) then highscores = {} end
  local notifications = ui.Frame:new('notificationsFrame')
  notifications._entered = true
  GameSystem = fill({menuManager = fill({getRoot = function() return harness_root or fill({}, 'root') end,
                                          notificationsFrame = function() return notifications end}, 'menuManager')}, 'GameSystem')
  queuedTutorials = {}
  if type(rawget(_G, 'ReferenceScaler')) == 'table' then ReferenceScaler.getWorldScale = function() return 1 end end
  resumingOldLevel, startedFromEditor, showEditor, restartingCurrentLevel = false, false, false, false
  g_rewardVideoReady, isIOSFree = false, function() return false end
  for _, k in ipairs({'levelStartPosition', 'rubberBandPos', 'oldLevelStartPosition'}) do
    if type(rawget(_G, k)) ~= 'table' or getmetatable(rawget(_G, k)) then rawset(_G, k, {x = 0, y = 0}) end
  end
  -- the engine's bodies: each makes the object's entry in objects.world
  local function body(name, sprite, x, y, a, b)
    if name == 'killingBox' then last_kbox = {x = x, y = y, w = a, h = b} end -- the box the engine gets
    objects.world[name] = objects.world[name] or {name = name, sprite = sprite, x = x, y = y, angle = 0}
    local o = objects.world[name]
    o.width, o.height = a or 1, b or a or 1
  end
  createBox, createPolygon, createLineShape = body, body, body
  createCircle = function(name, sprite, x, y, r) body(name, sprite, x, y, r and r * 2, r and r * 2) objects.world[name].radius = r end
end
numbers()
-- and this port's script hooked into them, as at start (after the engine's
-- stand-ins are in, which it wraps)
O.events_hooked, O.theme_hooked, O.textures_hooked, O.tutorials_hooked, O.levels_hooked, O.box_hooked = false, false, false, false, false, false
O.frame()
-- a level, loaded as the game loads one
local function box_off(name)
  local kv = QATools and QATools.killingVolumePhysics
  if not (kv and last_extent) or name:find('Comic') then return nil end
  local minx, maxx, miny, maxy = unpack(last_extent)
  local cx, cy = (kv.left + kv.right) / 2, (kv.top + kv.bottom) / 2
  if cx < minx - 30 or cx > maxx + 30 or cy < miny - 30 or cy > maxy + 30 then
    return string.format('%s (box at %.0f,%.0f; objects %.0f..%.0f, %.0f..%.0f)', name, cx, cy, minx, maxx, miny, maxy)
  end
  return false
end
local function try_level(folder, name)
  levelFolder = 'levels/' .. folder .. '/'
  currentFolder = folder
  levelName = name
  for i = #engine_errors, 1, -1 do engine_errors[i] = nil end
  objects = {world = {}, counts = {}, joints = {}, animatedObjects = {}}
  local ok, e = xpcall(function() return loadLevelInternal(levelFolder .. name, false) end, trace)
  if ok and #engine_errors > 0 then ok, e = false, table.concat(engine_errors, '; ') end
  return ok, e
end
local ok, e = try_level('theme1', 'Level119')
check(ok, 'the game\'s Level119 loads' .. (ok and '' or (': ' .. tostring(e))))
for _, l in ipairs({{'theme2', 'P14_Level277'}, {'theme2', 'LevelBossTheme2'}, {'theme1', 'LevelComic1'}, {'theme1', 'LevelSelection'}, {'theme1', 'Level154'}}) do
  QATools = {}
  local ok2, e2 = try_level(l[1], l[2])
  local off = ok2 and box_off(l[2])
  check(ok2 and not off, 'the game\'s ' .. l[2] .. ' loads' .. (ok2 and '' or (': ' .. tostring(e2))) .. (off and (', killing box off: ' .. off) or ''))
end
print('-- the worlds')
local bad = {}
local n = 0
local off_level, boxed = {}, 0
local no_bird, firsts = {}, {}
local one_cam, birds_out, objs_out = {}, {}, {}
for _, folder in ipairs({'oe_vegetoids', 'oe_omelettification'}) do
  for _, name in ipairs(actions.myLevels[folder]) do
    n = n + 1
    QATools = {}
    local ok2, e2 = try_level(folder, name)
    if not ok2 then bad[#bad + 1] = name .. ': ' .. tostring(e2) end
    -- the killing box (game.lua builds it from the level's cameras) centred
    -- on the level: its objects' extent, in physics units
    local off = ok2 and box_off(name)
    if off then off_level[#off_level + 1] = off end
    -- two cameras to move between (the slingshot's closer than the
    -- target's), and every bird inside the killing box they make
    if ok2 and not name:find('Comic') and name ~= 'LevelSelection' then
      local b = objects.birdCameraData and objects.birdCameraData[deviceModel]
      local c = objects.castleCameraData and objects.castleCameraData[deviceModel]
      if not (b and c and b.sx > c.sx) then one_cam[#one_cam + 1] = name end
      local k = last_kbox
      if k and not (type(k.x) == 'number' and type(k.y) == 'number' and type(k.w) == 'number' and type(k.h) == 'number') then
        print('   (' .. name .. ': killing box of ' .. type(k.x) .. ' ' .. type(k.w) .. ')')
        k = nil
      end
      local function outside(o)
        if not k or type(o.x) ~= 'number' or type(o.y) ~= 'number' then return false end
        return math.abs(o.x - k.x) > k.w * 0.5 or math.abs(o.y - k.y) > k.h * 0.5
      end
      for _, bd in ipairs(last_birds or {}) do
        if outside(bd) then birds_out[#birds_out + 1] = name .. ':' .. bd.name end
      end
      for _, o in ipairs(last_level_objs or {}) do
        if outside(o) then objs_out[#objs_out + 1] = name .. ':' .. tostring(o.name) end
      end
    end
    -- the first frame's slingshot fill, as the game runs it in every level
    -- (level selections and comics too: updateGameTimers asks for bird 1)
    if ok2 then
      currentBirdIndex, fillInNextBird, nextBirdTimer = 0, true, 0
      slingshot.birdToSlingshotBirdName, slingshot.removingBird = nil, nil
      local okb, eb = xpcall(function() slingshot.animateBirdToSlingShot(1 / 60) end, trace)
      local b = slingshot.birdToSlingshotBirdName
      local o = okb and b and objects.world[b]
      if not o then no_bird[#no_bird + 1] = name .. (okb and '' or (': ' .. tostring(eb):sub(1, 90))) else firsts[o.definition] = (firsts[o.definition] or 0) + 1 end
    end
    if off ~= nil and ok2 then boxed = boxed + 1 end
  end
end
check(#bad == 0, n .. ' levels of the two worlds load through the game\'s loader')
do
  local l = {} for d, c in pairs(firsts) do l[#l + 1] = d .. ' ' .. c end table.sort(l)
  check(#no_bird == 0, 'the first frame\'s slingshot fill finds bird 1 in every level (' .. table.concat(l, ', ') .. ')' .. (#no_bird > 0 and (' -- none in ' .. table.concat(no_bird, ' ')) or ''))
end
check(#one_cam == 0, 'every level has a slingshot camera closer than the whole view' .. (#one_cam > 0 and (' -- not: ' .. table.concat(one_cam, ' ', 1, math.min(#one_cam, 8))) or ''))
check(#birds_out == 0, 'every bird inside its level\'s killing box' .. (#birds_out > 0 and (' -- out: ' .. table.concat(birds_out, ' ', 1, math.min(#birds_out, 8))) or ''))
check(#objs_out == 0, 'every object of every level inside its killing box when it starts' .. (#objs_out > 0 and (' -- out (' .. #objs_out .. '): ' .. table.concat(objs_out, ' ', 1, math.min(#objs_out, 8))) or ''))
check(boxed > 50 and #off_level == 0, boxed .. ' levels\' killing boxes on their objects' .. (#off_level > 0 and (' -- off in ' .. #off_level .. ': ' .. table.concat(off_level, '; ', 1, math.min(#off_level, 4))) or ''))
local seen_t, mod_t = {}, 0
for _, f in ipairs(queuedTutorials or {}) do
  if type(f) == 'table' and f.birdName and not seen_t[f.birdName] then
    seen_t[f.birdName] = (f.tutorialImages or {})[1] or '?'
    local req = f._requiredAssets and f._requiredAssets.groups
    local has = false
    for g in pairs(req and req.items or req or {}) do if g == 'OE_TUTORIALS' then has = true end end
    if type(req) == 'table' and req.contains then has = req:contains('OE_TUTORIALS') end
    if tostring(seen_t[f.birdName]):find('BOOMERANG') or tostring(seen_t[f.birdName]):find('DRILL') or tostring(seen_t[f.birdName]):find('PINK') or tostring(seen_t[f.birdName]):find('IRON') or tostring(seen_t[f.birdName]):find('BLACK_HOLE') then
      mod_t = mod_t + 1
    end
  end
end
local l = {} for b, i in pairs(seen_t) do l[#l + 1] = b .. '=' .. tostring(i) end table.sort(l)
check(mod_t >= 3, 'tutorials in the worlds, the mod\'s birds with the mod\'s pictures (' .. table.concat(l, ' ') .. ')')
for _, b in ipairs(bad) do print('   ' .. b) end
-- ---------------------------------------------------------------- play
-- a level as the game plays it: a GameScene page entered (its onEntry loads
-- the level), then its update for the first seconds
requireFile('gamescene.lua')
nils('queuedLevelFolder', 'queuedLevelName', 'queuedCurrentPack', 'queuedCurrentLevel', 'queuedCurrentFolder')
numbers() -- its loading copies natives again
playAudio, stopAudio, playAudioNative = function() end, function() end, function() end -- no sound here
getAudioName = function(n) return n end
g_eaglePurchasesTable = {root = {}} -- a fresh save's
O.events_hooked, O.theme_hooked, O.textures_hooked, O.tutorials_hooked, O.levels_hooked, O.box_hooked = false, false, false, false, false, false
O.frame()
-- from here on: the stand-ins never called (state the game reads before
-- setting it) nil again, and so is any new global
local function go_strict()
  for k, v in pairs(_G) do
    -- a stand-in made for a global read (not an engine function copied
    -- from gamelua/native, not called, holding nothing)
    if type(v) == 'table' and autos[v] and not autos[v].parent and not autos[v].used and next(v) == nil then rawset(_G, k, nil) end
  end
  strict = true
end
-- the level-dependent steps of GameScene.update, in its order, on a real
-- GameScene (its HUD aside: the same in every level)
local STEPS = {
  function(sc, dt) sc:rotateMenuObjects() end,
  function(sc, dt) sc:rotateAndScaleObjects(dt, dt) end,
  function(sc, dt) sc:spawnObjects() end,
  function(sc, dt) sc:updateSpawners(dt) end,
  function(sc, dt) sc:updateBirds(dt) end,
  function(sc, dt) sc:updatePigs(dt, dt) end,
  function(sc, dt) sc:updateActions(dt) end,
  function(sc, dt) sc:updateFreezingObjects(dt) end,
  function(sc, dt) sc:updateGameTimers(dt, dt) end,
  function(sc, dt) slingshot.update(dt) end,
  function(sc, dt) sc:resetBirdCamera(dt) end,
  function(sc, dt) slingshot.animateBirdToSlingShot(dt) end,
  function(sc, dt) sc:updateTrajectory(dt) end,
  function(sc, dt) sc:updateBirdsDying(dt) end,
  function(sc, dt) sc:updateObjects(dt) end,
  function(sc, dt) sc:updateGravityVisuals(dt) end,
  function(sc, dt) if type(cameraFunction) == 'function' then cameraFunction(dt) end end,
}
local STEP_NAMES = {'rotateMenuObjects', 'rotateAndScaleObjects', 'spawnObjects', 'updateSpawners', 'updateBirds',
  'updatePigs', 'updateActions', 'updateFreezingObjects', 'updateGameTimers', 'slingshot.update', 'resetBirdCamera',
  'animateBirdToSlingShot', 'updateTrajectory', 'updateBirdsDying', 'updateObjects', 'updateGravityVisuals', 'cameraFunction'}
-- one GameScene, made as the game makes it
local the_scene, scene_err
do
  local ok, e = xpcall(function() the_scene = GameScene:new() end, trace)
  if not ok then the_scene, scene_err = nil, e end
end
local function play(folder, name, frames)
  harness_root = nil
  -- a shot's state: none at a level's start (the power tests left some)
  nils('flyingBird', 'currentBirdName', 'birdSpecialtyAvailable', 'g_birdEmitterTargets', 'levelCompleted', 'levelFailed')
  strict = false
  local ok, e = try_level(folder, name)
  if not ok then return false, 'loading: ' .. tostring(e) end
  go_strict()
  local sc = the_scene
  if not sc then return false, 'no scene: ' .. tostring(scene_err) end
  for f = 1, frames do
    for i, step in ipairs(STEPS) do
      local ok2, e2 = xpcall(function() step(sc, 1 / 60) end, trace)
      if not ok2 then strict = false return false, 'frame ' .. f .. ', ' .. STEP_NAMES[i] .. ': ' .. tostring(e2) end
    end
  end
  strict = false
  return true
end
for _, l in ipairs({{'theme1', 'Level119'}, {'theme2', 'P14_Level277'}, {'theme1', 'LevelSelection'}, {'theme2', 'LevelBossTheme2'}}) do
  local okp, ep = play(l[1], l[2], 120)
  check(okp, 'the game\'s ' .. l[2] .. ' plays 2 s' .. (okp and '' or (': ' .. tostring(ep))))
end
do
  local bad_play, np = {}, 0
  for _, folder in ipairs({'oe_vegetoids', 'oe_omelettification'}) do
    for _, name in ipairs(actions.myLevels[folder]) do
      np = np + 1
      local okp, ep = play(folder, name, 600)
      if not okp then bad_play[#bad_play + 1] = name .. ': ' .. tostring(ep):sub(1, 160) end
    end
  end
  check(#bad_play == 0, np .. ' levels of the two worlds play their first 10 s' .. (#bad_play > 0 and (' -- ' .. #bad_play .. ' fail') or ''))
  for i = 1, math.min(#bad_play, 12) do print('   ' .. bad_play[i]) end
end
-- ---------------------------------------------------------------- pages
-- level selection's pages, turned as the controller turns them: the PC
-- arrow keys' sweep (updatePCCameraPanningToDirection), the next once the
-- last has come to rest; to the last page, then back to the first. Each
-- sweep must go from its page straight to the next, never by another.
-- ctl: turned by the controller script instead (abs_ctl.lua's own turn, a
-- ZL/ZR press through __abs.frame, which runs each update after the game's)
local function page_walk(folder, ctl)
  local okp, ep = play(folder, 'LevelSelection', 60)
  if not okp then return nil, ep end
  local sc = the_scene
  -- its pages: cameras at one place are one (abs_ctl.lua ls_page_list)
  local cams = originalCameras or {}
  local pages, page_of_cam = {}, {}
  for i, c in ipairs(cams) do
    local near = screenWidth / math.max(c.sx or 0.4, 0.05) * 0.2
    for j = 1, i - 1 do
      if math.abs(cams[j].px - c.px) < near and math.abs(cams[j].py - c.py) < near then page_of_cam[i] = page_of_cam[j] break end
    end
    if not page_of_cam[i] then pages[#pages + 1] = c.px page_of_cam[i] = #pages end
  end
  if os.getenv('PAGES') then
    for i, c in ipairs(cams) do print(string.format('   %s camera %d (page %d): px %.0f py %.0f sx %.3f', folder, i, page_of_cam[i], c.px, c.py, c.sx)) end
  end
  if #pages < 2 then return nil, #pages .. ' page(s)' end
  local function page_at(x)
    local b, bd
    for i, px in ipairs(pages) do
      local d = math.abs(px - x)
      if not bd or d < bd then b, bd = i, d end
    end
    return b
  end
  -- abs_ctl.lua camera_settled: the controller's page is at rest
  local function settled()
    local sl = cameraAnimationSlider or 0
    return math.abs(sweepSpeed or 0) < 1 and (sl <= 0.001 or sl >= 0.999)
  end
  local longest = 0
  local function settle(xs)
    strict = true
    local rest
    for f = 1, 300 do
      for _, step in ipairs(STEPS) do step(sc, 1 / 60) end
      if ctl then
        strict = false
        __abs.frame(0, 0, 0, 0, 0, 0, 0, 0)
        strict = true
      end
      xs[#xs + 1] = screen.x
      if not rest and settled() then rest = f end
    end
    strict = false
    longest = math.max(longest, rest or 999)
  end
  local log, bad = {}, nil
  local function turn(right)
    local from = page_at(screen.x)
    local to = from + (right and 1 or -1)
    if to < 1 or to > #pages then return end
    -- a camera at a time, as the controller turns (two at one place: the
    -- first turn shows no change)
    local xs = {}
    for _ = 1, #cams do
      local okt, et = xpcall(function()
        if ctl then
          strict = false
          local r = tostring(__abs.frame(right and 6 or 7, 0, 0, 0, 0, 0, 0, 0))
          for n in r:gmatch('note:[%w_:%-]+') do bad = bad or n end
        else
          updatePCCameraPanningToDirection(right)
        end
        settle(xs)
      end, trace)
      strict = false
      if not okt then bad = bad or ('turning: ' .. tostring(et)) return end
      if page_at(screen.x) ~= from then break end
    end
    -- every frame between the two pages' positions, and there at the end
    local lo, hi = math.min(pages[from], pages[to]), math.max(pages[from], pages[to])
    local slack = (hi - lo) * 0.02 + 1
    local out
    for _, x in ipairs(xs) do
      if x < lo - slack or x > hi + slack then out = out or x end
    end
    local now = page_at(screen.x)
    -- the controller's reading of it (abs_ctl.lua ls_page_now, at rest)
    local said = page_of_cam[currentCamera + 1 + ((cameraAnimationSlider or 0) >= 0.5 and 1 or 0)]
    if said ~= now and not bad then bad = string.format('page %d shown, the camera\'s pair and slider say %s', now, tostring(said)) end
    log[#log + 1] = from .. '>' .. now .. (out and string.format(' (by page %d)', page_at(out)) or '')
    if (out or now ~= to) and not bad then
      bad = string.format('page %d to %d: %s', from, to, out and string.format('went by %.0f (page %d)', out, page_at(out)) or ('ended on page ' .. now))
    end
  end
  -- each level button's page (abs_ctl.lua ls_page_of: the camera nearest
  -- its object) is the one whose view holds it
  for name, o in pairs(objects.world) do
    if type(o) == 'table' and o.startNumber and type(o.x) == 'number' and not tostring(o.definition):find('Bird') then
      local x, y = o.x * physicsToWorld, o.y * physicsToWorld
      local best, bd
      for i, c in ipairs(originalCameras) do
        local d = (c.px - x) ^ 2 + (c.py - y) ^ 2
        if not bd or d < bd then best, bd = i, d end
      end
      local m = getModifiedCameraData(best)
      if not (x >= m.left and x <= m.right and y >= m.top and y <= m.bottom) and not bad then
        bad = string.format('%s (%s) on page %d by its camera, outside that page\'s view', name, tostring(o.definition), best)
      end
    end
  end
  for _ = 1, #pages do turn(true) end
  for _ = 1, #pages do turn(false) end
  for _ = 1, #pages do turn(true) end
  return bad == nil, (bad or '') .. ' [' .. table.concat(log, ' ') .. '] at rest after ' .. longest .. ' updates at most', #pages
end
for _, l in ipairs({{'theme1', 'the game\'s Pig Bang'}, {'theme2', 'the game\'s Cold Cuts'}, {'oe_vegetoids', 'Vege-toids'}, {'oe_omelettification', 'Omelettification'}}) do
  local okw, ew, np = page_walk(l[1])
  check(okw, l[2] .. '\'s level selection turns page by page' .. (np and (' (' .. np .. ' pages)') or '') .. ': ' .. tostring(ew))
end
do
  local bad_w, nw = {}, 0
  for _, f in ipairs({'theme3', 'theme4', 'theme5', 'theme6', 'theme7', 'theme81', 'theme82', 'theme9', 'theme10', 'theme_Kelloggs', 'dangerZone', 'bonus'}) do
    local okw, ew, np = page_walk(f)
    if np then nw = nw + 1 end
    if okw == false then bad_w[#bad_w + 1] = f .. ': ' .. tostring(ew) end
  end
  check(#bad_w == 0 and nw >= 8, 'the game\'s other level selections turn page by page too (' .. nw .. ' with pages; one page: Fry Me to the Moon, Froot Loops)' .. (#bad_w > 0 and (' -- ' .. table.concat(bad_w, '; ')) or ''))
end
-- ---------------------------------------------------------------- the eagle
-- the Space Eagle's score in the game's own HUD, releaseBuild unset (as on
-- Android): the score and the percentage on two lines (drawn over each other
-- on the console); with the controller script (abs_ctl.lua) in, the
-- percentage alone
do
  local ctl = tostring(arg and arg[1] or ''):gsub('abs_orbital%.lua$', 'abs_ctl.lua')
  local before, after, err2, paused, resumed, cover
  local okh, eh = xpcall(function()
    -- the engine's text layout: a line per line break
    clipText = function(_, s)
      local t = {widestLine = 10}
      for l in (tostring(s) .. '\n'):gmatch('(.-)\n') do t[#t + 1] = l end
      return t
    end
    for _, k in ipairs({'getFontLeading', 'getFontMaxAscending', 'getFontHeight'}) do res[k] = function() return 0 end end
    local hud = menu.GameHud:new()
    if not hud:getChild('feather') then
      local f = ui.Frame:new('feather')
      f.setValue = function() end
      hud:addChild(f)
    end
    hud.eagleMode = true
    nils('releaseBuild') -- unset, as on Android (not the harness's stand-in)
    numMightyEaglesShot, score = 1, 11670
    isPhysicsEnabled = function() return true end
    local function shown()
      hud.tempEagleScore, hud.currentEaglePercentage = 0, 77
      hud:updateScoreTexts(1 / 60)
      local t = hud:getChild('eagleHighscore')
      return t and t:getText()
    end
    before = shown()
    dofile(ctl)
    __abs.frame(0, 0, 0, 0, 0)
    after = shown()
    -- paused and back: nothing of the score while paused, then the eagle's
    -- percentage and not the score
    hud.eagleScore = true
    hud:layoutEagleScore()
    local function seen(n) local c = hud:getChild(n) return c and c.visible and n or nil end
    local function which()
      local t = {}
      for _, n in ipairs({'score', 'scoreText', 'eagleHighscoreText', 'eagleHighscore'}) do t[#t + 1] = seen(n) end
      return table.concat(t, ',')
    end
    local okp, ep2 = pcall(hud.showPauseMenu, hud)
    paused = okp and which() or ('showPauseMenu: ' .. tostring(ep2))
    local okr, er = pcall(hud.returnToGame, hud)
    resumed = okr and which() or ('returnToGame: ' .. tostring(er))
    -- the pause page's cover over the level: dimmed, not black
    local pp = hud:getChild('pausePage')
    if pp then
      local seen_a, dr = nil, drawRect
      drawRect = function(r, g, b, a) if seen_a == nil then seen_a = a end end
      pp.visible, pp.extendState = true, 1
      local okd, ed = pcall(pp.draw, pp, 0, 0, 1, 1)
      drawRect = dr
      cover = okd and seen_a or ('draw: ' .. tostring(ed))
    end
  end, trace)
  if not okh then err2 = eh end
  local function q(t) return tostring(t and t:gsub('\n', '|')) end
  check(okh and type(before) == 'string' and before:find('\n') and type(after) == 'string' and not after:find('\n') and after:find('%%$'),
        'the Space Eagle\'s score: the game\'s readout ' .. q(before) .. ', the percentage alone ' .. q(after) .. (err2 and (': ' .. err2) or ''))
  check(paused == '' and resumed == 'eagleHighscoreText,eagleHighscore',
        'the Space Eagle\'s score: paused, nothing shown (' .. tostring(paused) .. '); back in the level, the percentage, not the score (' .. tostring(resumed) .. ')')
  check(type(cover) == 'number' and cover > 0.3 and cover < 0.9, 'the pause page dims the level, see-through (cover opacity ' .. tostring(cover) .. ')')
end
-- ---------------------------------------------------------------- pages, by the controller
-- the same walk, each page turned by the controller script (abs_ctl.lua,
-- loaded by the eagle's check above): straight from page to page, never
-- past either (Omelettification's turn to its first page went out past it
-- with the game's sweep, on the console)
if type(__abs) == 'table' and type(__abs.frame) == 'function' then
  local bad_c, nc = {}, 0
  for _, f in ipairs({'theme1', 'theme2', 'oe_vegetoids', 'oe_omelettification', 'theme3', 'theme4', 'theme5', 'theme6',
                      'theme7', 'theme81', 'theme82', 'theme9', 'theme10', 'dangerZone', 'bonus'}) do
    local okw, ew, np = page_walk(f, true)
    if np then nc = nc + 1 end
    if okw == false then bad_c[#bad_c + 1] = f .. ': ' .. tostring(ew) end
  end
  check(#bad_c == 0 and nc >= 12, 'the controller turns every level selection page by page (' .. nc .. ' with pages)' ..
        (#bad_c > 0 and (' -- ' .. table.concat(bad_c, '; ')) or ''))
else
  check(false, 'the controller script loaded for its page turns')
end
print(string.format('%d ok, %d failed', oks, fails))
print(fails == 0 and 'ALL OK' or 'SOME FAILED')
