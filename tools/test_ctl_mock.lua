-- test_ctl_mock.lua -- run source/abs_ctl.lua against a mock of the game's globals:
--   luajit tools/test_ctl_mock.lua source/abs_ctl.lua
-- The mock follows the game's own classes where the script depends on them
-- (decompiled 2.2.14): lua.Class, ui.Frame (children get x - self.x, entry
-- and exit, unique child names), ui.Image (size from res.getSpriteBounds),
-- ui.Text, ui.ScalableButton (hit test with every ancestor's scale).
screenWidth, screenHeight = 1024, 768
deviceModel = 'android'
physicsToWorld, worldScale = 10, 1.5
screen = {left = 0, top = 0}
function physicsToScreenTransform(x, y) return (x*physicsToWorld - screen.left)*worldScale, (y*physicsToWorld - screen.top)*worldScale end

local failures = 0
local function check(cond, what)
  if not cond then failures = failures + 1; print('FAIL: ' .. what) else print('ok: ' .. what) end
end

-- ---------------------------------------------------------------- classes
lua = {Class = {}}
function lua.Class.create(base)
  local c = {}
  setmetatable(c, base or lua.Class)
  c.__index = c
  return c
end
function lua.Class.new(cls, ...)
  local o = {}
  setmetatable(o, cls)
  cls.__index = cls
  if cls.init then o:init(...) end
  return o
end
lua.Class.__index = lua.Class

local loaded_groups = {}
local SPRITES = {MISSIONS_UI_MAIN_BG = {832, 685, 'MISSIONS'}, POPUP_BANNER = {628, 115, 'MENU'},
                 MINER_UNLOCK_POPUP_BUTTON = {233, 84, 'MENU'}, BTN_X = {90, 90, 'BUTTONS'},
                 BTN_CHECK = {100, 100, 'BUTTONS'}, NEW_EPSEL_MISC_NASA_ISS = {162, 157, 'EPISODESELECTION2'},
                 MINER_BRANDED_POPUP_GLOW = {638, 636, 'MENU'}, IN_APP_LOADING = {64, 64, 'MENU'}}
res = {}
function res.getSpriteBounds(name)
  local s = SPRITES[name]
  if not s then error('no sprite ' .. tostring(name)) end
  if not loaded_groups[s[3]] then error('sprite ' .. name .. ' drawn before its group ' .. s[3] .. ' was loaded') end
  return s[1], s[2]
end
function res.getSpritePivot(name) local s = SPRITES[name]; return s[1] / 2, s[2] / 2 end

ui = {}
local Frame = lua.Class.create()
ui.Frame = Frame
function Frame:init(name)
  self.name, self.x, self.y, self.w, self.h = name, 0, 0, 0, 0
  self.scaleX, self.scaleY, self.visible, self.active = 1, 1, true, true
  self.children, self._childIndex, self._groups, self._entered = {}, {}, {}, false
end
function Frame:requireGroup(...) for _, g in ipairs({...}) do self._groups[#self._groups + 1] = g end end
function Frame:onEntry()
  assert(not self._entered, 'onEntry twice')
  for _, g in ipairs(self._groups) do loaded_groups[g] = true end
  self._entered = true
  for _, c in ipairs(self.children) do c:onEntry() end
end
function Frame:onExit()
  self._entered = false
  for _, c in ipairs(self.children) do c:onExit() end
end
function Frame:addChild(c)
  assert(not self._childIndex[c.name], 'Trying to add child with same name.')
  table.insert(self.children, c)
  self._childIndex[c.name] = c
  c._parent = self
  if self._entered then c:onEntry(); c:layout() end
end
function Frame:removeChild(c)
  for i, x in ipairs(self.children) do
    if x == c then table.remove(self.children, i) break end
  end
  self._childIndex[c.name] = nil
  if c._entered then c:onExit() end
end
function Frame:removeSelf() if self._parent then self._parent:removeChild(self) end end
function Frame:getChild(n) return self._childIndex[n] end
function Frame:layout() for _, c in ipairs(self.children) do c:layout() end end
function Frame:setScale(s) self.scaleX, self.scaleY = s, s end
function Frame:update(dt) for _, c in ipairs(self.children) do c:update(dt) end end
function Frame:draw() for _, c in ipairs(self.children) do if c.visible then c:draw() end end end
function Frame:onPointerEvent(e, x, y)
  for i = #self.children, 1, -1 do
    local c = self.children[i]
    if c.visible and c.active then
      local r, a, b = c:onPointerEvent(e, x - self.x, y - self.y)
      if r then return r, a, b end
    end
  end
end
function Frame:onKeyEvent(e, k)
  for i = #self.children, 1, -1 do
    local r = self.children[i]:onKeyEvent(e, k)
    if r then return r end
  end
end

local Image = lua.Class.create(Frame)
ui.Image = Image
function Image:setImage(name)
  self.image = name
  self.w, self.h = res.getSpriteBounds(name)
  self.px, self.py = res.getSpritePivot(name)
end
function Image:onEntry()
  Frame.onEntry(self)
  if self.image then self.w, self.h = res.getSpriteBounds(self.image) end
end

local Text = lua.Class.create(Frame)
ui.Text = Text
function Text:init(name, text, group, font, h, v)
  Frame.init(self, name)
  assert(type(text) == 'string', 'text must be a string')
  assert(font == nil or type(font) == 'function', 'a font fetcher is a function')
  self._text = text
end
function Text:setMaxWidth(w) self._maxWidth = w end
function getFontSmall() return 'FONT_SPACE_BASIC_SMALL' end

local Spinner = lua.Class.create(Image)
ui.Spinner = Spinner
function Spinner:init(name, speed) Frame.init(self, name); self.rotSpeed = speed end
function Text:setText(t) self._text = t end

local Button = lua.Class.create(Image)
ui.ScalableButton = Button
function Button:init(name) Frame.init(self, name); self.enabled = true end
function Button:hitTest(x, y)
  local sx, sy = self.scaleX, self.scaleY
  local p = self._parent
  while p do sx = sx * p.scaleX; sy = sy * p.scaleY; p = p._parent end
  return x >= self.x - self.px * sx and x <= self.x + (self.w - self.px) * sx and
         y >= self.y - self.py * sy and y <= self.y + (self.h - self.py) * sy
end
function Button:onPointerEvent(e, x, y)
  local r = Frame.onPointerEvent(self, e, x, y)
  if r then return r end
  if e == 'LPRESS' and self:hitTest(x, y) then return self.returnValue end
end

ReferenceScaler = {getWorldScale = function() return 1 end}
function drawRect() end
local update_enabled = true
function disableGameUpdate() update_enabled = false end
function enableGameUpdate() update_enabled = true end

-- quick builders for plain frames and buttons
local function frame(t)
  local f = Frame:new(t.name or 'f')
  for k, v in pairs(t) do if k ~= 'children' then f[k] = v end end
  for _, c in ipairs(t.children or {}) do f:addChild(c) end
  return f
end
local nbtn = 0
local function button(t)
  nbtn = nbtn + 1
  local b = Button:new(t.name or ('b' .. nbtn))
  for k, v in pairs(t) do b[k] = v end
  b.px, b.py = t.px or 0, t.py or 0
  return b
end

local function items_of(line)
  local t = {}
  for w in line:gmatch('%S+') do t[#t + 1] = w end
  local n = tonumber(t[24])
  local r = {mode = tonumber(t[2]), sig = t[15], req = t[16], clock = tonumber(t[17]), mm = tonumber(t[18]),
             vr = {tonumber(t[19]), tonumber(t[20]), tonumber(t[21]), tonumber(t[22])},
             carousel = tonumber(t[23]), n = n}
  for i = 0, n - 1 do
    local b = 25 + i * 12
    r[#r + 1] = {x = tonumber(t[b]), y = tonumber(t[b + 1]), w = tonumber(t[b + 2]), h = tonumber(t[b + 3]),
                 ax = tonumber(t[b + 4]), ay = tonumber(t[b + 5]), kind = tonumber(t[b + 6]),
                 shape = tonumber(t[b + 7]), prio = tonumber(t[b + 8]), id = tonumber(t[b + 9]),
                 grp = tonumber(t[b + 10]), vis = tonumber(t[b + 11])}
  end
  return r
end

-- ---------------------------------------------------------------- the UI
local page = frame{name = 'page', children = {
  button{x = 100, y = 100, w = 80, h = 40, returnValue = 'PLAY'},
  button{x = 300, y = 100, w = 80, h = 40, returnValue = 'RESTART_LEVEL'},
  frame{name = 'scroller', scroll = {x = 50, y = 0}, clip = {x = 0, y = 0, w = 1024, h = 768},
        children = {button{x = 500, y = 300, w = 60, h = 60, name = 'lvl1'}},
        next = function() print('page next') end, previous = function() end},
}}
local notifications = frame{name = 'notificationsFrame'}
local base = frame{name = 'base', children = {page, frame{name = 'menuParticlesFrame'}, notifications}}
base:onEntry()
GameSystem = {menuManager = {_baseFrame = base, allowInput = true, getRoot = function() return page end,
                             notificationsFrame = function() return notifications end}}

-- level state
isInGameMode = function() return true end
isPausePageVisible = function() return false end
objects = {world = {bird1 = {x = 10, y = 20}}, levelMode = {usesSlingshot = function() return true end},
           birdCameraData = {android = {px = 0, py = 0, sx = 1.0}},
           castleCameraData = {android = {px = 100, py = 0, sx = 0.8}}}
function doItAllCamera() end
cameraAnimationSlider, minWorldScale = 0, 0.4
currentBirdName, birdReady = 'bird1', true
levelStartPosition = {x = 10, y = 19}
currentZoomedScale = 1.0
function updatePCCameraPanningToDirection(d) print('pan to', d) end
function togglePausePage() print('toggle pause') end
dofile(arg[1])

time = 12.5
local out = __abs.frame(0, 0.5, 0.2, 1, 0)
print(out)
check(out:match('^1 2 '), 'in a level: aiming mode')
check(cameraAnimationSlider > 0 and isSwipingCamera == true and cameraFunction == doItAllCamera,
      'camera: the stick moves the slider toward the target, as a drag does (' .. cameraAnimationSlider .. ')')
local held = cameraAnimationSlider
isSwipingCamera = false
__abs.frame(0, 0, 0, 1, 0)
check(cameraAnimationSlider == held and isSwipingCamera == true, 'camera: let go, it stays where it was put')
__abs.frame(0, 0, -1, 1, 0)
check(currentZoomedScale < 1.0, 'camera: zooming out works at once at the slingshot (' .. currentZoomedScale .. ')')
for _ = 1, 200 do __abs.frame(0, 0, -1, 1, 0) end
check(math.abs(currentZoomedScale - 0.4) < 1e-6, 'camera: down to the game\'s smallest scale')
for _ = 1, 200 do __abs.frame(0, 0, 1, 1, 0) end
check(math.abs(currentZoomedScale - 1.0) < 1e-6, 'camera: and back in, up to the slingshot camera\'s scale')
cameraAnimationSlider = 0
check(items_of(out).clock == 12.5, 'the game clock is reported')
print(__abs.frame(2, 0, 0, 1, 0))
print(__abs.frame(4, 0, 0, 1, 0))

-- menus
isInGameMode = function() return false end
local r = items_of(__abs.frame(6, 0, 0, 1, 0))
check(r.n == 3, 'menu: 3 buttons (' .. r.n .. ')')

-- a popup in notificationsFrame that blocks everything under it
local popup = frame{name = 'Prompt', x = 312, y = 184, w = 400, h = 400, children = {
  button{x = 150, y = 300, w = 100, h = 100, name = 'close_button', returnValue = 'CLOSE', enabledImage = 'BTN_CHECK'},
  button{x = 20, y = 300, w = 120, h = 60, returnValue = 'VIEW'},
}}
popup.onPointerEvent = function(self, e, x, y) return Frame.onPointerEvent(self, e, x, y) or 'BLOCK' end
notifications:addChild(popup)
r = items_of(__abs.frame(0, 0, 0, 1, 0))
check(r.n == 2, 'popup: only its 2 buttons (' .. r.n .. ')')
local best
for _, it in ipairs(r) do if not best or it.prio > best.prio then best = it end end
check(best and math.abs(best.ax * 1024 - (312 + 200)) < 1 and math.abs(best.ay * 768 - (184 + 350)) < 1,
      'popup: the check is the best focus, at its centre')
check(best and best.shape == 1, 'popup: the round check is round')
popup:removeSelf()

-- a scaled page: the button's box must use the parent's scale
local scaled = frame{name = 'Scaled', scaleX = 2, scaleY = 2, children = {
  button{x = 400, y = 300, w = 50, h = 50, px = 25, py = 25, name = 'closeButton', returnValue = 'CLOSE', enabledImage = 'BTN_CHECK'},
}}
scaled.onPointerEvent = function(self, e, x, y) return Frame.onPointerEvent(self, e, x, y) or 'BLOCK' end
notifications:addChild(scaled)
r = items_of(__abs.frame(0, 0, 0, 1, 0))
check(r.n == 1 and math.abs(r[1].w * 1024 - 100) < 1, 'scaled: box scaled by the parent (w ' .. (r[1] and r[1].w * 1024 or -1) .. ')')
local hit = base:onPointerEvent('LPRESS', r[1].ax * 1024, r[1].ay * 768)
check(hit == 'CLOSE', 'scaled: a tap at the action point reaches the button')
scaled:removeSelf()

-- a comic over a level: its black picture hides the HUD, its check comes later
menu = {}
menu.ComicPage = lua.Class.create(Frame)
local hud = frame{name = 'hud', children = {button{x = 10, y = 10, w = 80, h = 80, returnValue = 'PAUSE'}}}
page:addChild(hud)
local comic = menu.ComicPage:new('ComicPage')
comic.onPointerEvent = function(self, e, x, y) return Frame.onPointerEvent(self, e, x, y) end
page:addChild(comic)
r = items_of(__abs.frame(0, 0, 0, 1, 0))
check(r.n == 0, 'comic: nothing under it is reachable (' .. r.n .. ' items)')
comic:addChild(button{x = 900, y = 700, w = 100, h = 100, px = 50, py = 50, name = 'closeButton', enabledImage = 'BTN_CHECK'})
r = items_of(__abs.frame(0, 0, 0, 1, 0))
check(r.n == 1 and r[1].prio == 9, 'comic: its check, when it comes')
comic:removeSelf()
hud:removeSelf()

-- the planet carousel (it goes round: the anchor is not wrapped, positions are)
local ents = {
  {id = 'ep_red', x0 = 380, x1 = 640, y0 = 250, y1 = 520},
  {id = 'ep_left', x0 = 60, x1 = 200, y0 = 330, y1 = 450},
  {id = 'ep_right', x0 = 820, x1 = 960, y0 = 330, y1 = 450},
  {id = 'mars_link', x0 = 480, x1 = 540, y0 = 560, y1 = 600},
}
local scene = {pickEntity = function(self, pt)
  local ids = {}
  for _, e in ipairs(ents) do
    if pt.x >= e.x0 and pt.x <= e.x1 and pt.y >= e.y0 and pt.y <= e.y1 then ids[#ids + 1] = e.id end
  end
  return ids
end}
local spun
local epsel = frame{name = 'EpisodeSelection', _currentAnchor = 2, _buttonCount = 3, w = 1024, h = 768,
  _entityEpisodeMapping = {ep_red = {}, ep_left = {}, ep_right = {}},
  _entityLinkMapping = {mars_link = 'CURIOSITY_URL'},
  _positionMapping = {ep_red = 2, ep_left = 1, ep_right = 3, mars_link = 2}}
epsel:addChild(frame{name = 'sceneFrame', _scene = scene, w = 1024, h = 768})
epsel.sceneFrame = epsel:getChild('sceneFrame')
epsel.sceneFrame.onPointerEvent = function(self, e) if e == 'LRELEASE' then return 'SCENE_ENTITY_PICKED' end end
epsel:addChild(button{x = 20, y = 20, w = 90, h = 90, returnValue = 'BACK'})
epsel.onKeyEvent = function(self, ev, key) spun = key; return 'BLOCK' end
epsel.onPointerEvent = function(self, e, x, y) return Frame.onPointerEvent(self, e, x, y) end
page:removeSelf()
local particles = base:getChild('menuParticlesFrame')
particles:removeSelf()
notifications:removeSelf()
base:addChild(epsel)
base:addChild(particles)
base:addChild(notifications)
for _ = 1, 10 do r = items_of(__abs.frame(0, 0, 0, 1, 0)) end
check(r.carousel == 1, 'carousel: seen')
local kinds = {}
for _, it in ipairs(r) do kinds[it.kind] = (kinds[it.kind] or 0) + 1 end
check(kinds[2] == 1 and not kinds[1] and not kinds[3] and kinds[0] == 1,
      'carousel: the middle planet and the button, no side or tiny planets')
local centre
for _, it in ipairs(r) do if it.kind == 2 then centre = it end end
check(centre and math.abs(centre.ax * 1024 - 510) < 20, 'carousel: the middle planet is ep_red')
check(centre and centre.id == 1 and centre.y * 768 > 250 + (520 - 250) * 0.5,
      'carousel: its box is the name sign, in the planet\'s lower part (y ' .. (centre and centre.y * 768 or -1) .. ')')
-- Froot Loops Bloopers: a sign at the middle planet's position that opens an
-- episode of its own (ignoreAnchor), over the planet's top right corner
local function bloopers_case(first)
  local e = {id = 'bloopers', x0 = 560, x1 = 700, y0 = 200, y1 = 300}
  if first then table.insert(ents, 1, e) else ents[#ents + 1] = e end
  epsel._entityEpisodeMapping.bloopers = {index = 14, ignoreAnchor = true}
  epsel._positionMapping.bloopers = 2
  for _ = 1, 10 do r = items_of(__abs.frame(0, 0, 0, 1, 0)) end
  local c, sign
  for _, it in ipairs(r) do
    if it.kind == 2 then c = it end
    if it.kind == 0 and it.id >= 10 and it.x * 1024 > 500 then sign = it end
  end
  check(c and math.abs(c.ax * 1024 - 510) < 20 and c.id == 1,
        'bloopers: the planet in the middle is still ep_red (' .. (c and c.ax * 1024 or -1) .. ')')
  local ax, ay = sign and sign.ax * 1024, sign and sign.ay * 768
  check(sign and math.abs(sign.x * 1024 - 560) < 8 and math.abs((sign.x + sign.w) * 1024 - 700) < 8,
        'bloopers: its sign is an item, boxed (' .. (sign and sign.x * 1024 or -1) .. ')')
  -- a tap there opens it: the last episode under the point wins
  local on_planet = ax and ax >= 380 and ax <= 640 and ay >= 250 and ay <= 520
  check(sign and ax >= 560 and ax <= 700 and ay >= 200 and ay <= 300 and (not first or not on_planet),
        'bloopers: tapped where a tap opens it (' .. tostring(ax) .. ', ' .. tostring(ay) .. ')')
  for i, x in ipairs(ents) do if x == e then table.remove(ents, i) break end end
  epsel._entityEpisodeMapping.bloopers = nil
  epsel._positionMapping.bloopers = nil
end
bloopers_case(false)
bloopers_case(true)
-- it goes with its planet: at another position, not listed
epsel._entityEpisodeMapping.bloopers = {index = 14, ignoreAnchor = true}
epsel._positionMapping.bloopers = 3
ents[#ents + 1] = {id = 'bloopers', x0 = 860, x1 = 950, y0 = 280, y1 = 320}
for _ = 1, 10 do r = items_of(__abs.frame(0, 0, 0, 1, 0)) end
local nsign = 0
for _, it in ipairs(r) do if it.kind == 0 and it.id >= 10 and it.x * 1024 > 800 then nsign = nsign + 1 end end
check(nsign == 0, 'bloopers: not listed beside the carousel\'s middle')
table.remove(ents)
epsel._entityEpisodeMapping.bloopers = nil
epsel._positionMapping.bloopers = nil
-- the episode is shown: the game's own answer is replaced, and put back
SettingsWrapper = {isKelloggsAvailable = function() return nil end}
__abs.frame(0, 0, 0, 17, 0)
check(SettingsWrapper:isKelloggsAvailable() == true, 'froot loops: shown with the flag')
__abs.frame(0, 0, 0, 1, 0)
check(SettingsWrapper:isKelloggsAvailable() == nil, 'froot loops: the game\'s answer without it')
__abs.frame(0, 0, 0, 25, 0)
check(SettingsWrapper:isKelloggsAvailable() == true, 'froot loops: with online and links flags too')
SettingsWrapper = nil
-- turned round past the end: anchor 5 is position 2 again
epsel._currentAnchor = 5
for _ = 1, 10 do r = items_of(__abs.frame(0, 0, 0, 1, 0)) end
centre = nil
for _, it in ipairs(r) do if it.kind == 2 then centre = it end end
check(centre and math.abs(centre.ax * 1024 - 510) < 20, 'carousel: found after going round (anchor 5 of 3)')
epsel._currentAnchor = 0 -- position 3 is in the middle now: ep_right's entity
epsel._positionMapping = {ep_red = 1, ep_left = 2, ep_right = 3, mars_link = 1}
for _ = 1, 10 do r = items_of(__abs.frame(0, 0, 0, 1, 0)) end
centre = nil
for _, it in ipairs(r) do if it.kind == 2 then centre = it end end
check(centre and math.abs(centre.ax * 1024 - 890) < 20, 'carousel: anchor 0 is position 3')
__abs.frame(9, 0, 0, 1, 0)
check(spun == 'RIGHT', 'carousel: spin right')
-- the turn on the page's animator, as the game makes it, a little quicker;
-- a stick held runs on from the speed it has
ui.FrameAnimator = {cubicEaseOut = function(t) t = t - 1 return t * t * t + 1 end}
local anim = frame{name = 'animator', _t = 1}
anim.setAnimation = function(self, tg, len, fn)
  self._targets = {_currentAnchor = {v1 = epsel._currentAnchor, v2 = tg._currentAnchor}}
  self._length, self._interpolator, self._t = len, fn, 0
end
epsel:addChild(anim)
spun = nil
__abs.frame(9 + 340 * 256, 0, 0, 1, 0)
check(spun == nil and anim._targets._currentAnchor.v2 == 1 and math.abs(anim._length - 0.34) < 1e-6 and
      anim._interpolator == ui.FrameAnimator.cubicEaseOut, 'carousel: a press turns it in 0.34 s, eased out')
anim._t = 0.5
epsel._currentAnchor = 1 - 0.125 -- halfway through the ease-out
__abs.frame(9 + 400 * 256, 0, 0, 1, 0)
local fn = anim._interpolator
local d0 = (fn(0.001) - fn(0)) / 0.001 * (anim._targets._currentAnchor.v2 - epsel._currentAnchor) / anim._length
check(anim._targets._currentAnchor.v2 == 2 and fn ~= ui.FrameAnimator.cubicEaseOut and math.abs(fn(1) - 1) < 1e-5 and
      math.abs(d0 - 3 * 0.25 / 0.34) < 0.05, 'carousel: held, the next turn goes on from the speed it had')
anim._t = 0.2
epsel._currentAnchor = 1.6
__abs.frame(8 + 400 * 256, 0, 0, 1, 0)
check(anim._targets._currentAnchor.v2 == 1 and anim._interpolator == ui.FrameAnimator.cubicEaseOut,
      'carousel: turned back while moving: the game\'s way (the planet behind, eased out)')
anim:removeSelf()
ui.FrameAnimator = nil
epsel._currentAnchor = 0
__abs.frame(8 + 340 * 256, 0, 0, 1, 0)
check(spun == 'LEFT', 'carousel: no animator: the game\'s own turn')
popup = frame{name = 'Prompt2', x = 312, y = 184, w = 400, h = 400, children = {
  button{x = 150, y = 300, w = 100, h = 100, returnValue = 'CLOSE', enabledImage = 'BTN_CHECK'},
  button{x = 20, y = 300, w = 120, h = 60, returnValue = 'VIEW'}}}
popup.onPointerEvent = function(self, e, x, y) return Frame.onPointerEvent(self, e, x, y) or 'BLOCK' end
notifications:addChild(popup)
for _ = 1, 10 do r = items_of(__abs.frame(0, 0, 0, 1, 0)) end
check(r.carousel == 0 and r.n == 2, 'carousel: a popup over it hides the planet (' .. r.n .. ' items)')
popup:removeSelf()

-- the eagle's button, whatever its picture (Toucan Sam in Froot Loops)
local saved_objects = objects
isInGameMode = function() return true end
objects = {world = {}}
local hud2 = frame{name = 'hud2', children = {
  button{x = 900, y = 20, w = 80, h = 80, name = 'MEButton', returnValue = 'ME_CLICKED', enabledImage = 'BTN_MIGHTY_SAM'}}}
base:addChild(hud2)
local line = __abs.frame(5, 0, 0, 1, 0)
local w = {}
for x in line:gmatch('%S+') do w[#w + 1] = tonumber(x) or x end
check(w[13] and math.abs(w[13] * 1024 - 940) < 2 and math.abs(w[14] * 768 - 60) < 2,
      'eagle: Toucan Sam\'s button is tapped (' .. tostring(w[13]) .. ', ' .. tostring(w[14]) .. ')')
hud2:removeSelf()
isInGameMode = function() return false end
objects = saved_objects

-- free purchases
local bought
native = {CloudPayment = {onProductPurchased = function(id) bought = id end}}
iap = {Payment = {buyProduct = function() error('store') end, getPrice = function() return '$0.99' end}}
__abs.frame(0, 0, 0, 3, 200)
check(iap.Payment.getPrice('spaceeagle.small') == 'FREE', 'free: price says FREE')
iap.Payment.buyProduct('spaceeagle.small')
__abs.frame(0, 0, 0, 3, 200)
check(bought == 'spaceeagle.small', 'free: the purchase is granted')

-- links: the game's own kind of popup
__abs.set_videos('curiosity')
url = {prompt = function() error('browser') end}
__abs.frame(0, 0, 0, 5 + 8, 0) -- online
url.prompt('NASA_URL')
local pop = notifications:getChild('absInfoPopup')
check(pop ~= nil, 'links: a popup in notificationsFrame' .. (__abs.last_error and (' (' .. __abs.last_error .. ')') or ''))
check(not update_enabled, 'links: the game waits while it is open')
if pop then
  check(pop:getChild('background').w == 832, 'links: the missions panel, its size known')
  check(pop:getChild('icon') and pop:getChild('glow') and not pop:getChild('bannerFrame'),
        'links: the ISS picture on a glow, no New Horizons banner')
  check(pop:getChild('watchVideo') ~= nil, 'links: Watch video (online, the video is on YouTube)')
  r = items_of(__abs.frame(0, 0, 0, 13, 0))
  check(r.n == 2, 'links: its 2 buttons only (' .. r.n .. ')')
  best = nil
  for _, it in ipairs(r) do if not best or it.prio > best.prio then best = it end end
  local res1 = base:onPointerEvent('LPRESS', best.ax * 1024, best.ay * 768)
  check(res1 == 'BLOCK', 'links: the popup answers BLOCK')
  r = items_of(__abs.frame(0, 0, 0, 13, 0, 0))
  check(r.req == 'play:nasa:PW8et34Uxt8', 'links: Watch video streams it (' .. tostring(r.req) .. ')')
  check(pop.vmode and pop:getChild('spinner').visible and not pop:getChild('body').visible,
        'links: the popup makes room for the video, the spinner turns')
  check(r.vr[3] > 0.5 and r.vr[4] > 0.3, 'links: the video\'s rectangle is reported')
  __abs.frame(0, 0, 0, 13, 0, 1)
  pop:update(0.016)
  check(pop:getChild('status').visible and pop:getChild('status')._text == 'Connecting to YouTube...',
        'links: "Connecting to YouTube..." while it resolves')
  __abs.frame(0, 0, 0, 13, 0, 3)
  pop:update(0.016)
  check(not pop:getChild('spinner').visible and not pop:getChild('status').visible, 'links: playing: only the video')
  __abs.frame(0, 0, 0, 13, 0, 0) -- stopped by the controller (B)
  pop:update(0.016)
  r = items_of(__abs.frame(0, 0, 0, 13, 0, 0))
  check(not pop.vmode and pop:getChild('body').visible and r.req == 'vstop', 'links: stopped: the page again (' .. r.req .. ')')
  base:onKeyEvent('PRESS', 'KEY_BACK')
  r = items_of(__abs.frame(0, 0, 0, 13, 0, 0))
  check(notifications:getChild('absInfoPopup') == nil and update_enabled and r.req == 'closed',
        'links: B closes it, the game goes on, the port is told')
end
__abs.frame(0, 0, 0, 13, 0) -- online
url.prompt('TOONS_URL')
pop = notifications:getChild('absInfoPopup')
local sub1 = pop and pop:getChild('subtitle') and pop:getChild('subtitle')._text
check(pop and pop:getChild('watchVideo') ~= nil and tostring(sub1):match('^Episode %d+: '),
      'links: Toons, a random episode (' .. tostring(sub1) .. ')')
if pop then pop:close() end
url.prompt('MOVIE_URL')
pop = notifications:getChild('absInfoPopup')
check(pop and pop:getChild('watchVideo') ~= nil, 'links: the trailer has its video')
if pop then pop:close() end
url.prompt('SPIRIT_URL') -- no video: no button, no hint
pop = notifications:getChild('absInfoPopup')
check(pop and not pop:getChild('watchVideo') and not pop:getChild('hint'), 'links: no video, no button, no hint (Spirit)')
if pop then pop:close() end
__abs.frame(0, 0, 0, 5, 0) -- offline
url.prompt('MERCURY_URL')
pop = notifications:getChild('absInfoPopup')
check(pop and pop:getChild('hint') ~= nil and not pop:getChild('watchVideo'), 'links: offline, a hint to connect (Mercury)')
url.prompt('CURIOSITY_URL') -- on the card: plays without the internet
pop = notifications:getChild('absInfoPopup')
check(#notifications.children == 1 and pop:getChild('watchVideo') ~= nil, 'links: one popup at a time; a card video plays offline')
local wb = pop:getChild('watchVideo')
base:onPointerEvent('LPRESS', pop.x + wb.x, pop.y + wb.y)
r = items_of(__abs.frame(0, 0, 0, 5, 0, 0))
check(r.req:find('play:curiosity:%-') ~= nil, 'links: the card\'s file (' .. r.req .. ')')
notifications:getChild('absInfoPopup'):close()
__abs.frame(0, 0, 0, 5, 0)
-- if the game's popup can't be made, the port's page instead
local saved = ui.Text
ui.Text = nil
url.prompt('CURIOSITY_URL')
ui.Text = saved
r = items_of(__abs.frame(0, 0, 0, 5, 0))
check(r.req == 'page:CURIOSITY_URL', 'links: the port\'s page when the popup cannot be made')

-- the main menu: its tabs
isInGameMode = function() return false end
local mmpage = frame{name = 'MainMenu'}
local lslider = frame{name = 'optionsSlider', state = 'OPEN'}
lslider:addChild(button{x = 40, y = 400, w = 80, h = 80, name = 'sound', returnValue = 'SOUND'})
mmpage:addChild(lslider)
mmpage:addChild(button{x = 40, y = 680, w = 80, h = 80, name = 'optionsButton', returnValue = 'TOGGLE_OPTIONS'})
mmpage:addChild(frame{name = 'rightSlider', state = 'CLOSED', visible = false})
mmpage:addChild(button{x = 460, y = 340, w = 100, h = 100, name = 'play', returnValue = 'PLAY'})
epsel:removeSelf()
notifications:removeSelf()
base:addChild(mmpage)
base:addChild(notifications)
GameSystem.menuManager.getRoot = function() return mmpage end
r = items_of(__abs.frame(0, 0, 0, 1, 0))
local g = {}
for _, it in ipairs(r) do g[it.grp] = (g[it.grp] or 0) + 1 end
check(r.mm == 3 and g[2] == 1 and g[3] == 1 and g[0] == 1, 'main menu: flags 3, the tab\'s member and its button (mm ' .. r.mm .. ')')
-- B in the main menu: the open tab closes, the game never asks to quit
local quit = false
menu.MainMenu = lua.Class.create(Frame)
function menu.MainMenu:onKeyEvent(ev, key)
  local r2 = Frame.onKeyEvent(self, ev, key)
  if not r2 and ev == 'PRESS' and key == 'KEY_BACK' then quit = true end
end
setmetatable(mmpage, menu.MainMenu)
function lslider:toggle() self.state = self.state == 'OPEN' and 'CLOSING' or 'OPENING' end
__abs.frame(0, 0, 0, 1, 0)
mmpage:onKeyEvent('PRESS', 'KEY_BACK')
check(not quit and lslider.state == 'CLOSING', 'main menu: B closes the tab that is out, no quit prompt')
lslider.state = 'CLOSED'
mmpage:onKeyEvent('PRESS', 'KEY_BACK')
check(not quit, 'main menu: B with no tab out does nothing')
lslider.state = 'OPEN'

-- the pause page on top: flag 8 (B does nothing there)
isInGameMode = function() return true end
isPausePageVisible = function() return true end
r = items_of(__abs.frame(0, 0, 0, 1, 0))
check(r.mm % 16 >= 8, 'pause page on top: flag 8 (mm ' .. r.mm .. ')')
isPausePageVisible = function() return false end
isInGameMode = function() return false end

-- the Solar System's strip of planets (ScrollFrameFree: velocity is a
-- vector): a planet past the edge glides in, and velocity stays a vector
local strip = frame{name = 'solarSystemSelector', scroll = {x = 0, y = 0}, velocity = {x = 0, y = 0}, axis = 'x',
                    scroll_min = 0, scroll_max = 2000, clip = {x = 0, y = 0, w = 1024, h = 768}}
strip:addChild(button{x = 1100, y = 100, w = 100, h = 100, name = 'saturn'})
mmpage:removeSelf()
notifications:removeSelf()
base:addChild(strip)
base:addChild(notifications)
r = items_of(__abs.frame(0, 0, 0, 1, 0))
local sat
for _, it in ipairs(r) do if it.x * 1024 > 1024 then sat = it end end
__abs.frame(0, 0, 0, 1, 0, 0, sat and sat.id or 0)
check(type(strip.velocity) == 'table' and strip.velocity.x > 0,
      'free scroller: velocity stays a vector and carries the planet in (the crash) (' .. tostring(strip.velocity) .. ')')
strip:removeSelf()

-- level selection: its pages are cameras; a level past the edge turns the page
local flipped
local saved_pan = updatePCCameraPanningToDirection
updatePCCameraPanningToDirection = function(right) flipped = right end
levelName = 'LevelSelection'
local lsel = frame{name = 'levelSelectionUI'}
lsel:addChild(button{x = 200, y = 300, w = 90, h = 90, name = 'level9'})
lsel:addChild(button{x = 1250, y = 300, w = 90, h = 90, name = 'level10'})
notifications:removeSelf()
base:addChild(lsel)
base:addChild(notifications)
r = items_of(__abs.frame(0, 0, 0, 1, 0))
local l10
for _, it in ipairs(r) do if it.x * 1024 > 1024 then l10 = it end end
check(l10 and l10.vis == 0, 'level selection: the next page\'s level is listed, not visible')
__abs.frame(0, 0, 0, 1, 0, 0, l10 and l10.id or 0)
check(flipped == true, 'level selection: focusing it turns to the next page')
flipped = nil
for _ = 1, 30 do __abs.frame(0, 0, 0, 1, 0) end
__abs.frame(7, 0, 0, 1, 0) -- the previous page (ZL)
check(flipped == false, 'level selection: ZL turns back a page')
lsel:removeSelf()

-- its pages by camera: a level's page is the camera nearest its object, not
-- where the button is on the screen (a page easing in, one zoomed out)
local flips = {}
local saved_ls = {originalCameras, physicsToWorld, objects, currentCamera, cameraAnimationSlider, cameraAnimationSliderTarget, sweepSpeed}
updatePCCameraPanningToDirection = function(right) flips[#flips + 1] = right end
originalCameras = {{px = 0, py = 0}, {px = 2000, py = 0}, {px = 4000, py = 0}}
physicsToWorld = 20
objects = {world = {lvA = {x = 10, y = 0}, lvB = {x = 95, y = 0}, lvC = {x = 190, y = 0}}}
currentCamera, cameraAnimationSlider, cameraAnimationSliderTarget, sweepSpeed = 1, 0, 0, 0 -- page 2
local psel = frame{name = 'levelSelectionUI'}
local bA = button{x = 1100, y = 300, w = 90, h = 90, name = 'lvA'} -- page 1, drawn right of the screen
local bB = button{x = 400, y = 300, w = 90, h = 90, name = 'lvB'}
local bC = button{x = 1500, y = 300, w = 90, h = 90, name = 'lvC'}
psel:addChild(bA) psel:addChild(bB) psel:addChild(bC)
notifications:removeSelf()
base:addChild(psel)
base:addChild(notifications)
local function by_x(list, x0)
  for _, it in ipairs(list) do if math.abs(it.x * 1024 - x0) < 2 then return it end end
end
r = items_of(__abs.frame(0, 0, 0, 1, 0))
local iA, iB, iC = by_x(r, 1100), by_x(r, 400), by_x(r, 1500)
check(iA and iB and iC and iA.vis == 0 and iB.vis == 1 and iC.vis == 0,
      'level selection by camera: only the levels of the page shown are visible')
for _ = 1, 100 do __abs.frame(0, 0, 0, 1, 0) end
flips = {}
__abs.frame(0, 0, 0, 1, 0, 0, iA and iA.id or 0)
check(#flips == 1 and flips[1] == false, 'level selection by camera: a level of page 1 turns back a page, wherever it is drawn (' .. tostring(flips[1]) .. ')')
-- turning: the page turned to is the one shown; the focus's own page asks no more
currentCamera, cameraAnimationSlider, cameraAnimationSliderTarget, sweepSpeed = 0, 0.6, 0, 500
flips = {}
for _ = 1, 100 do __abs.frame(0, 0, 0, 1, 0, 0, iA and iA.id or 0) end
check(#flips == 0, 'level selection by camera: no more turns once its page is being turned to (' .. #flips .. ')')
cameraAnimationSlider, sweepSpeed = 0, 0
r = items_of(__abs.frame(0, 0, 0, 1, 0, 0, iA and iA.id or 0))
iA = by_x(r, 1100)
check(iA and iA.vis == 1, 'level selection by camera: page 1 shown, its level visible')
-- ZL/ZR turn the page from under the focus: it is not turned back
for _ = 1, 100 do __abs.frame(0, 0, 0, 1, 0) end
flips = {}
__abs.frame(6, 0, 0, 1, 0, 0, iA and iA.id or 0) -- ZR
currentCamera, cameraAnimationSlider, cameraAnimationSliderTarget = 0, 1, 1 -- page 2
for _ = 1, 100 do __abs.frame(0, 0, 0, 1, 0, 0, iA and iA.id or 0) end
check(#flips == 1 and flips[1] == true, 'level selection by camera: ZR turns a page and the focus left behind does not turn it back (' .. #flips .. ')')
-- cameras at one place are one page (Fry Me to the Moon, Froot Loops): the
-- level is on it whichever of them is showing
originalCameras = {{px = 0, py = 0, sx = 0.4}, {px = 0, py = 0, sx = 0.4}}
currentCamera, cameraAnimationSlider, cameraAnimationSliderTarget, sweepSpeed = 0, 1, 1, 0
bA.x = 400
r = items_of(__abs.frame(0, 0, 0, 1, 0))
iA = by_x(r, 400)
check(iA and iA.vis == 1, 'level selection by camera: two cameras at one place are one page, its level visible')
-- a comic not reached yet: its button is there but LevelButton draws
-- nothing for it (disabled, no locked picture): not listed
menu.LevelButton = {setupDrawBatches = function() end}
local comic = button{x = 700, y = 300, w = 90, h = 90, name = 'lvComic', disabled = true, physicsObject = {},
                     setupDrawBatches = menu.LevelButton.setupDrawBatches}
psel:addChild(comic)
r = items_of(__abs.frame(0, 0, 0, 1, 0))
check(not by_x(r, 700) and by_x(r, 400), 'level selection: a comic not reached yet (nothing drawn) is not a button to go to')
comic.originalImageLocked = 'LOCK'
r = items_of(__abs.frame(0, 0, 0, 1, 0))
check(by_x(r, 700), 'level selection: a locked level drawn with its lock still is')
comic:removeSelf()
menu.LevelButton = nil
-- no turn past the first page (the game's turn there swings out and back)
originalCameras = {{px = 0, py = 0}, {px = 2000, py = 0}, {px = 4000, py = 0}}
currentCamera, cameraAnimationSlider, cameraAnimationSliderTarget, sweepSpeed = 0, 0, 0, 0
for _ = 1, 400 do __abs.frame(0, 0, 0, 1, 0) end
flips = {}
__abs.frame(7, 0, 0, 1, 0) -- ZL on page 1
__abs.frame(6, 0, 0, 1, 0) -- ZR
check(#flips == 1 and flips[1] == true, 'level selection: no turn back past the first page, a turn on from it (' .. #flips .. ')')
psel:removeSelf()
originalCameras, physicsToWorld, objects, currentCamera, cameraAnimationSlider, cameraAnimationSliderTarget, sweepSpeed = unpack(saved_ls, 1, 7)
levelName = nil
updatePCCameraPanningToDirection = saved_pan
base:addChild(mmpage)

-- a level list in pages: the focused item on the next page is brought into view
local pager = frame{name = 'levels', scroll = {x = 0, y = 0}, clip = {x = 0, y = 0, w = 1024, h = 768},
                    anchors = {{x = 0, y = 0}, {x = 1024, y = 0}}, target_anchor = 1,
                    next = function() end, previous = function() end}
pager:addChild(button{x = 100, y = 300, w = 90, h = 90, name = 'l1'})
pager:addChild(button{x = 1124, y = 300, w = 90, h = 90, name = 'l13'})
mmpage:removeSelf()
notifications:removeSelf()
base:addChild(pager)
base:addChild(notifications)
r = items_of(__abs.frame(0, 0, 0, 1, 0))
local off
for _, it in ipairs(r) do if it.vis == 0 then off = it end end
check(off and off.x * 1024 > 1024, 'pages: the next page\'s level is listed, not visible')
__abs.frame(0, 0, 0, 1, 0, 0, off and off.id or 0)
check(pager.target_anchor == 2, 'pages: focusing it turns to its page (anchor ' .. tostring(pager.target_anchor) .. ')')

-- a popup over a level: its buttons, not the slingshot
pager:removeSelf()
isInGameMode = function() return true end
__abs.frame(0, 0, 0, 13, 0)
url.prompt('NASA_URL')
r = items_of(__abs.frame(0, 0, 0, 5, 0))
check(r.n == 2 and tonumber(__abs.frame(0, 0, 0, 5, 0):match('^%d+ (%d+)')) == 1,
      'a popup over a level: menu mode, its buttons (' .. r.n .. ')')
notifications:getChild('absInfoPopup'):close()
check(tonumber(__abs.frame(0, 0, 0, 5, 0):match('^%d+ (%d+)')) == 2, 'closed: the level again')

-- a flying bird's power: aimed (the Lazer bird's) reports 2 and where the
-- bird is, for the cursor; another reports 1
do
  local sb, sp, sf, sa, sc, sr = blockTable, physicsToScreenTransform, flyingBird, birdSpecialtyAvailable, currentBirdName, birdReady
  currentBirdName, birdReady = nil, false -- in flight: no bird ready on the slingshot
  blockTable = {blocks = {LaserBird = {specialty = 'LASER'}, RedBird = {specialty = 'SOUND'}}}
  physicsToScreenTransform = function(x, y) return x * 10, y * 10 end
  flyingBird, birdSpecialtyAvailable = {name = 'LaserBird_1', definition = 'LaserBird', x = 40, y = 30}, true
  local f = {} for w in __abs.frame(0, 0, 0, 5, 0):gmatch('%S+') do f[#f + 1] = w end
  check(tonumber(f[2]) == 3 and tonumber(f[12]) == 2 and math.abs(tonumber(f[6]) - 400 / tonumber(f[3])) < 0.01,
        'a flying Lazer bird: its power aimed, the cursor from the bird (mode ' .. tostring(f[2]) .. ' sw ' .. tostring(f[3]) .. ' special ' .. tostring(f[12]) .. ' bx ' .. tostring(f[6]) .. ')')
  flyingBird = {name = 'RedBird_1', definition = 'RedBird', x = 40, y = 30}
  f = {} for w in __abs.frame(0, 0, 0, 5, 0):gmatch('%S+') do f[#f + 1] = w end
  check(tonumber(f[12]) == 1, 'a flying Red bird: its power, not aimed')
  -- the next bird on the slingshot while the Lazer bird's power is unused:
  -- still flight (a grab would spend the power), then aiming once it is used
  currentBirdName, birdReady = sc, true
  flyingBird = {name = 'LaserBird_1', definition = 'LaserBird', x = 40, y = 30}
  f = {} for w in __abs.frame(0, 0, 0, 5, 0):gmatch('%S+') do f[#f + 1] = w end
  check(tonumber(f[2]) == 3 and tonumber(f[12]) == 2, 'the next bird ready, the flying bird\'s power unused: still flight (' .. tostring(f[2]) .. ')')
  birdSpecialtyAvailable = false
  f = {} for w in __abs.frame(0, 0, 0, 5, 0):gmatch('%S+') do f[#f + 1] = w end
  check(tonumber(f[2]) == 2, 'the power used: aiming the next bird (' .. tostring(f[2]) .. ')')
  blockTable, physicsToScreenTransform, flyingBird, birdSpecialtyAvailable, currentBirdName, birdReady = sb, sp, sf, sa, sc, sr
end
isInGameMode = function() return false end

-- the power-ups bar: folded when a level starts; the command opens / folds it
do
  local sw, gr = SettingsWrapper, GameSystem.menuManager.getRoot
  SettingsWrapper = {getIsPowerUpSliderOpen = function() return true end}
  __abs.frame(0, 0, 0, 5, 0)
  local folded = SettingsWrapper:getIsPowerUpSliderOpen() == false
  local toggles = 0
  local hud = {powerupSlider = {toggle = function() toggles = toggles + 1 end}}
  local root = {getChild = function(_, n) if n == 'gameHud' then return hud end end}
  GameSystem.menuManager.getRoot = function() return root end
  isInGameMode = function() return true end
  __abs.frame(10, 0, 0, 5, 0)
  isInGameMode = function() return false end
  check(folded and toggles == 1, 'the power-ups bar starts folded, the command toggles it (' .. toggles .. ')')
  SettingsWrapper, GameSystem.menuManager.getRoot = sw, gr
end

-- the shop opened from a level takes the level's place and keeps levelName
-- (isInGameMode says yes): the menus' controls there, not the level's
do
  local gr, gs = GameSystem.menuManager.getRoot, GameScene
  GameScene = {}
  local scene = setmetatable({}, GameScene)
  isInGameMode = function() return true end
  GameSystem.menuManager.getRoot = function() return scene end
  local m1 = tonumber(__abs.frame(0, 0, 0, 5, 0):match('^%d+ (%d+)'))
  local shop = frame{name = 'shop'}
  shop:addChild(button{x = 300, y = 500, w = 200, h = 60, name = 'buy', returnValue = 'BUY'})
  notifications:removeSelf()
  base:addChild(shop)
  base:addChild(notifications)
  GameSystem.menuManager.getRoot = function() return shop end
  local r2 = items_of(__abs.frame(0, 0, 0, 5, 0))
  check(m1 ~= 1 and r2.mode == 1 and r2.n > 0,
        'the shop from a level: the menus\' controls and its buttons (mode ' .. tostring(m1) .. ', then ' .. tostring(r2.mode) .. ' with ' .. r2.n .. ' buttons)')
  shop:removeSelf()
  isInGameMode = function() return false end
  GameSystem.menuManager.getRoot, GameScene = gr, gs
end

-- level selection: a page turning is reported (the ring holds still), and
-- not once it rests
do
  local sl, ss, ln = cameraAnimationSlider, sweepSpeed, levelName
  levelName, cameraAnimationSlider, sweepSpeed = 'LevelSelection', 0.4, 12
  local f = {} for w in __abs.frame(0, 0, 0, 5, 0):gmatch('%S+') do f[#f + 1] = w end
  local turning = tonumber(f[18]) % 32 >= 16
  cameraAnimationSlider, sweepSpeed = 1, 0
  f = {} for w in __abs.frame(0, 0, 0, 5, 0):gmatch('%S+') do f[#f + 1] = w end
  check(turning and tonumber(f[18]) % 32 < 16, 'level selection: a turning page reported, then at rest')
  cameraAnimationSlider, sweepSpeed, levelName = sl, ss, ln
end

print(__abs.dump())
print(failures == 0 and 'ALL OK' or (failures .. ' FAILED'))
os.exit(failures == 0 and 0 or 1)
