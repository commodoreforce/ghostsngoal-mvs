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
local complete_at=nil
local max_x=0
local boss_seen={}
local duck_timer=0
local palette_checks={}
local function palette_check(name)
 if palette_checks[name] then return end
 local data=palette_sets[name]
 for i,v in ipairs(data[2]) do
  if m:read_u16(0x400000+data[1]*32+(i-1)*2)~=v then
   print('VALIDATION_FAILED palette',name,i);manager.machine:exit();return
  end
 end
 palette_checks[name]=true;print('PALETTE_PASS',name)
 -- Players stay resident while scene banks change.
 if name~='PLAYER' and name~='PLAYER_NAKED' and name~='PLAYER_GOLD' then
  palette_checks.PLAYER=nil;palette_checks.PLAYER_NAKED=nil
  palette_check(u8('gold_palette')==1 and 'PLAYER_GOLD' or 'PLAYER');palette_check('PLAYER_NAKED')
 end
end
local scene_names={'SKY','CITY','INSIDE0','INSIDE1',false,false,false,false,'INSIDE0_DARK','INSIDE1_DARK','INSIDE0_FLASH','INSIDE1_FLASH','CITY_FLASH'}
local stadium_names={'STADIUM_BACK','STADIUM_BLEND1','STADIUM_BLEND2','STADIUM_BLEND3','STADIUM_FRONT'}
local clip_start=nil
local cadence_f=nil
local cadence_tick=nil
local logged_death_life=nil
local last_game_tick=nil
local clear_score=nil
local clear_checked=false
emu.register_frame_done(function()
 f=f+1
 p[':AUDIO_COIN'].fields['Coin 1']:set_value((f>=180 and f<184) and 1 or 0)
 p[':edge:joy:START'].fields['1 Player Start']:set_value((f>=210 and f<216) and 1 or 0)
 if f==170 then manager.machine.screens[':screen']:snapshot(output..'/title.png') end
 if f>230 and u8('lives')>0 then
  local scene=u8('scene_palette')
  local stadium=u8('stadium_palette')
  if scene_names[scene+1] then palette_check(scene_names[scene+1]) end
  if (scene==1 or scene==12) and stadium_names[stadium+1] then palette_check(stadium_names[stadium+1]) end
  if u8('death_timer')>0 then
   if logged_death_life~=u8('lives') then
    logged_death_life=u8('lives');print('DEATH',f,s16('player_x'),s16('player_y'),u8('lives'))
    manager.machine.screens[':screen']:snapshot(output..'/death-'..u8('lives')..'.png')
   end
   joy['P1 A']:set_value(0);joy['P1 B']:set_value(0);joy['P1 Left']:set_value(0);joy['P1 Right']:set_value(0);joy['P1 Down']:set_value(0)
   jump=false;release=2;hold=0;return
  end
  local tick=m:read_u16(sym.frame_counter)
  if f>=300 and not cadence_f then cadence_f=f;cadence_tick=tick end
  if u8('stage_clear')==0 and last_game_tick==tick then return end
  last_game_tick=tick
  local x=s16('player_x')
  if x>max_x then max_x=x end
  if assisted_health then m:write_u8(sym.invuln,120) end
  local foot=s16('player_y')+64
  local grounded=u8('player_ground')>0
  if grounded then recentground=5 elseif recentground>0 then recentground=recentground-1 end
  if jump then hold=hold+1 else hold=0 end
  local bossflags=u8('boss_spawned')>0 and m:read_u8(sym.enemies+4) or 0
  if u8('stage_clear')==0 and u8('boss_spawned')>0 and not boss_seen[bossflags] then
   boss_seen[bossflags]=true;print('BOSS_PHASE',f,bossflags,m:read_u8(sym.enemies+2))
   manager.machine.screens[':screen']:snapshot(output..'/boss-'..bossflags..'.png')
  end
  if bossflags==2 and not clip_start then clip_start=f end
  if clip_start and f>=clip_start and f<clip_start+120 and (f-clip_start)%4==0 then
   manager.machine.screens[':screen']:snapshot(output..string.format('/animation-%02d.png',(f-clip_start)/4))
  end
  if bossflags==2 then duck_timer=30 elseif duck_timer>0 then duck_timer=duck_timer-1 end
  joy['P1 Down']:set_value(duck_timer>0 and 1 or 0)
  local aim_up=false;local low_shot=false
  if u8('boss_spawned')==0 then
   local nearest=120
   for slot=0,11 do
    local a=sym.enemies+slot*18;local kind=m:read_u8(a)
    local ex=m:read_i16(a+6);local ey=m:read_i16(a+8);local dx=ex-x
    if kind>0 and m:read_u8(a+1)==0 and dx>0 and dx<nearest then
     nearest=dx
     aim_up=(ey+((kind==2 or kind==3) and 16 or 24))<foot-52
     low_shot=grounded and ((kind==3 and ey+16>foot-28) or (kind==1 and m:read_u8(sym.enemy_attack+slot)==2)) and dx<90
    end
   end
  end
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
  joy['P1 Up']:set_value(aim_up and 1 or 0)
  if low_shot and not need and not jump then joy['P1 Down']:set_value(1) end
  local bossx=u8('boss_spawned')>0 and m:read_i16(sym.enemies+6) or 0
  local run=u8('boss_spawned')>0 and x<bossx-70 or (u8('boss_spawned')==0 and x<3970)
  for _,edge in ipairs({544,928,1536,2448,3152}) do
   if x>=edge-40 and x<edge and not grounded and foot>180 and m:read_i32(sym.player_vy_fp)>=0 then run=false end
  end
  local flee=u8('boss_spawned')>0 and x>3824 and (bossflags==4 or x>bossx-45)
  joy['P1 Left']:set_value(flee and 1 or 0)
  joy['P1 Right']:set_value((run and not flee) and 1 or 0)
  if u8('stage_clear')>0 then
   local state=u8('penalty_state');local kick=u8('penalty_kick')
   local targets={10,90,50,88,12};local shoot=false
   if state==1 then shoot=math.abs(u8('penalty_aim')-targets[kick+1])<=2
   elseif state==2 then shoot=u8('penalty_power')>=86 and u8('penalty_power')<=90 end
   joy['P1 B']:set_value(shoot and 1 or 0)
  else joy['P1 B']:set_value((m:read_u16(sym.frame_counter)%12<6) and 1 or 0) end
  for _,v in ipairs({0,256,720,1400,1800,2050,2600,3200,3700,3950}) do
   if x>=v and not snapshots[v] then snapshots[v]=true;manager.machine.screens[':screen']:snapshot(output..'/world-'..v..'.png') end
  end
  if lastlives~=u8('lives') then print('LIVES',f,x,u8('lives'));lastlives=u8('lives') end
 end
 if f%300==0 then print('STATE',f,s16('player_x'),s16('player_y'),u8('lives'),m:read_u16(sym.frame_counter),u8('checkpoint_reached'),u8('boss_spawned'),u8('stage_clear'),m:read_u32(sym.score),s16('camera_x')) end
 if u8('stage_clear')>0 and not cleared_at then
  cleared_at=f;clear_score=m:read_u32(sym.score)
  if cadence_f then
   local ratio=(m:read_u16(sym.frame_counter)-cadence_tick)/(f-cadence_f)
   print('FRAME_CADENCE',ratio)
   if ratio<0.98 then print('VALIDATION_FAILED frame cadence');manager.machine:exit();return end
  end
  print('STAGE_CLEAR',f,m:read_u32(sym.score),u8('checkpoint_reached'),m:read_u16(sym.wave_mask))
  if u8('checkpoint_reached')~=1 or m:read_u16(sym.wave_mask)~=8191 then print('VALIDATION_FAILED');manager.machine:exit() end
 end
 if cleared_at and not clear_checked and u8('penalty_state')==1 then
  local expected=clear_score+m:read_u32(sym.clear_time_bonus)+m:read_u32(sym.clear_accuracy_bonus)+m:read_u32(sym.clear_nohit_bonus)
  if m:read_u32(sym.score)~=expected then print('VALIDATION_FAILED clear bonus accounting',m:read_u32(sym.score),expected);manager.machine:exit();return end
  local fired=m:read_u16(sym.shots_fired);local hits=m:read_u16(sym.shots_hit)
  local accuracy=fired>0 and math.min(5000,math.floor(hits*5000/fired+0.5)) or 0
  if m:read_u32(sym.clear_accuracy_bonus)~=accuracy then print('VALIDATION_FAILED accuracy rounding');manager.machine:exit();return end
  clear_checked=true;print('CLEAR_BONUS_PASS',expected)
 end
 if u8('level1_complete')==1 and not complete_at then complete_at=f end
 if cleared_at and complete_at and f>=complete_at+90 then
  manager.machine.screens[':screen']:snapshot(output..'/clear.png')
  for _,name in ipairs({'CITY','INSIDE0','INSIDE1','STADIUM_BACK','STADIUM_BLEND1','STADIUM_BLEND2','STADIUM_BLEND3','STADIUM_FRONT'}) do
   if not palette_checks[name] then print('VALIDATION_FAILED missing palette',name);manager.machine:exit();return end
  end
  if not clear_checked then print('VALIDATION_FAILED missing bonus accounting');manager.machine:exit();return end
  print('BONUS_PASS',u8('penalty_kick'),u8('penalty_goals'),m:read_u32(sym.penalty_bonus_score))
  if u8('penalty_kick')~=5 then print('VALIDATION_FAILED incomplete penalties');manager.machine:exit();return end
  print('LEVEL1_PASS',max_x,u8('lives'));manager.machine:exit()
 end
 if f>250 and lastlives>0 and u8('lives')==0 then print('GAME_OVER');manager.machine:exit() end
 if f==9000 then print('TIMEOUT');manager.machine:exit() end
end)
