-- Generated symbols/output/assisted_health are prepended by test-mame-level1.py.
local f=0
local p=manager.machine.ioport.ports
local joy=p[':edge:joy:JOY1'].fields
local m=manager.machine.devices[':maincpu'].spaces['program']
local function u8(n) return m:read_u8(sym[n]) end
local function s16(n) return m:read_i16(sym[n]) end
local jump=false
local release=0
local hold=0
local jump_limit=14
local recentground=0
local snapshots={}
local lastlives=0
local cleared_at=nil
local max_x=0
local boss_seen={}
local duck_timer=0
emu.register_frame_done(function()
 f=f+1
 p[':AUDIO_COIN'].fields['Coin 1']:set_value((f>=180 and f<184) and 1 or 0)
 p[':edge:joy:START'].fields['1 Player Start']:set_value((f>=210 and f<216) and 1 or 0)
 if f==170 then manager.machine.screens[':screen']:snapshot(output..'/title.png') end
 if f>230 and u8('lives')>0 then
  local x=s16('player_x')
  if x>max_x then max_x=x end
  if assisted_health then m:write_u8(sym.invuln,120) end
  local foot=s16('player_y')+64
  local grounded=u8('player_ground')>0
  if grounded then recentground=5 elseif recentground>0 then recentground=recentground-1 end
  if jump then hold=hold+1 else hold=0 end
  local bossflags=u8('boss_spawned')>0 and m:read_u8(sym.enemies+4) or 0
  if u8('boss_spawned')>0 and not boss_seen[bossflags] then
   boss_seen[bossflags]=true;print('BOSS_PHASE',f,bossflags,m:read_u8(sym.enemies+2))
   manager.machine.screens[':screen']:snapshot(output..'/boss-'..bossflags..'.png')
  end
  if bossflags==2 then duck_timer=30 elseif duck_timer>0 then duck_timer=duck_timer-1 end
  joy['P1 Down']:set_value(duck_timer>0 and 1 or 0)
  local need=false
  local next_limit=14
  for _,b in ipairs(solids) do if x+((foot-b[2]<=16) and 15 or 24)>=b[1] and x-7<b[1]+b[3] and foot>b[2] then need=true;next_limit=(foot-b[2]<=16) and 3 or 14 end end
  for _,edge in ipairs({544,928,1536,2448,3152}) do if x>=edge-(foot<208 and 75 or 25) and x<edge then need=true;next_limit=60 end end
  if u8('boss_spawned')>0 and bossflags==4 and m:read_i16(sym.enemies+12)>0 and m:read_i16(sym.enemies+8)>65 then need=true;next_limit=60 end
  if u8('boss_spawned')>0 and bossflags==5 then need=true;next_limit=60 end
  if jump and hold>=jump_limit then jump=false;release=2
  elseif grounded and jump and hold>4 then jump=false;release=2
  elseif not jump and recentground>0 and need and release==0 then jump=true;jump_limit=next_limit end
  if release>0 then release=release-1 end
  joy['P1 A']:set_value(jump and 1 or 0)
  local bossx=u8('boss_spawned')>0 and m:read_i16(sym.enemies+6) or 0
  local run=u8('boss_spawned')>0 and x<bossx-70 or (u8('boss_spawned')==0 and x<3970)
  for _,edge in ipairs({544,928,1536,2448,3152}) do
   if x>=edge-40 and x<edge and not grounded and foot>180 and m:read_i32(sym.player_vy_fp)>=0 then run=false end
  end
  local flee=u8('boss_spawned')>0 and x>3824 and (bossflags==4 or x>bossx-45)
  joy['P1 Left']:set_value(flee and 1 or 0)
  joy['P1 Right']:set_value((run and not flee) and 1 or 0)
  joy['P1 B']:set_value((f%12<6) and 1 or 0)
  for _,v in ipairs({0,256,720,1400,1800,2050,2600,3200,3700,3950}) do
   if x>=v and not snapshots[v] then snapshots[v]=true;manager.machine.screens[':screen']:snapshot(output..'/world-'..v..'.png') end
  end
  if lastlives~=u8('lives') then print('LIVES',f,x,u8('lives'));lastlives=u8('lives') end
 end
 if f%300==0 then print('STATE',f,s16('player_x'),s16('player_y'),u8('lives'),m:read_u16(sym.frame_counter),u8('checkpoint_reached'),u8('boss_spawned'),u8('stage_clear'),m:read_u32(sym.score),s16('camera_x')) end
 if u8('stage_clear')>0 and not cleared_at then
  cleared_at=f
  print('STAGE_CLEAR',f,m:read_u32(sym.score),u8('checkpoint_reached'),m:read_u16(sym.wave_mask))
  if u8('checkpoint_reached')~=1 or m:read_u16(sym.wave_mask)~=8191 then print('VALIDATION_FAILED');manager.machine:exit() end
 end
 if cleared_at and f>=cleared_at+150 then
  manager.machine.screens[':screen']:snapshot(output..'/clear.png')
  print('LEVEL1_PASS',max_x,u8('lives'));manager.machine:exit()
 end
 if f>250 and lastlives>0 and u8('lives')==0 then print('GAME_OVER');manager.machine:exit() end
 if f==9000 then print('TIMEOUT');manager.machine:exit() end
end)
