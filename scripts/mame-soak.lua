-- Continuous emulator soak. First 100 coin/start/death cycles use explicit
-- pickup/pit fixtures. Afterwards only controller inputs/coins are used.
-- This is a stability fixture, never represented as a fair full playthrough.
local f,cycles,phase,at=0,0,0,180
local m=manager.machine.devices[':maincpu'].spaces.program
local ports=manager.machine.ioport.ports
local joy=ports[':edge:joy:JOY1'].fields
local function u8(n) return m:read_u8(sym[n]) end
local function u16(n) return m:read_u16(sym[n]) end
local function fail(name) print('SOAK_FAIL',f,cycles,name);manager.machine:exit() end
emu.register_frame_done(function()
 f=f+1
 local coin=phase==0 and f>=at and f<at+4
 local start=phase==0 and f>=at+30 and f<at+36
 ports[':AUDIO_COIN'].fields['Coin 1']:set_value(coin and 1 or 0)
 ports[':edge:joy:START'].fields['1 Player Start']:set_value(start and 1 or 0)
 joy['P1 Right']:set_value(cycles>=100 and phase==3 and 1 or 0)
 joy['P1 A']:set_value(cycles>=100 and phase==3 and f%65<28 and 1 or 0)
 joy['P1 B']:set_value(cycles>=100 and phase==3 and f%12<6 and 1 or 0)
 if phase==0 and f>=at+42 then phase=1;at=f end
 if phase==1 then
  if u8('lives')==3 and u16('frame_counter')<50 and u8('bios_user_mode')==2 then
   if m:read_u32(sym.score)~=0 or u16('boost_timer')~=0 or m:read_u8(sym.crate_hp)~=3 then fail('new-game reset');return end
   cycles=cycles+1
   if cycles<=100 then
    -- Earn a real pumpkin award before forcing the pit, to exercise score and
    -- backup data through repeated exits/re-entry rather than writing a score.
    m:write_u32(sym.player_x_fp,80*256);m:write_u32(sym.player_y_fp,144*256)
    m:write_u16(sym.player_x,80);m:write_u16(sym.player_y,144)
    local a=sym.pickups+20*20
    m:write_u32(a,80*256);m:write_u32(a+4,190*256)
    m:write_u16(a+12,190);m:write_u16(a+14,60)
    m:write_u8(a+16,1);m:write_u8(a+17,1);m:write_u8(a+18,255);m:write_u8(a+19,1)
    phase=2;at=f
   else phase=3;at=f end
  elseif f>at+180 then fail('coin/start timeout');return end
 elseif phase==2 and f>=at+8 then
  if m:read_u32(sym.score)<200 or m:read_u32(sym.high_score)<200 then fail('score/record lost');return end
  m:write_u8(sym.lives,1);m:write_u8(sym.invuln,0);m:write_u8(sym.death_timer,0)
  m:write_u32(sym.player_x_fp,550*256);m:write_u32(sym.player_y_fp,230*256)
  m:write_u16(sym.player_x,550);m:write_u16(sym.player_y,230)
  m:write_u32(sym.player_vy_fp,0);phase=4;at=f
 elseif phase==3 then
  if u8('lives')==0 or u8('level1_complete')==1 then phase=4;at=f end
 elseif phase==4 and f>=at+260 then
  phase=0;at=f+5
  if cycles%10==0 then print('SOAK_CYCLES',cycles,f) end
 end
 if f%10000==0 then print('SOAK_FRAMES',f,cycles,u8('lives'),u16('frame_counter')) end
 if f>=110000 then
  if cycles<100 then fail('fewer than 100 completed coin/start cycles');return end
  manager.machine.screens[':screen']:snapshot(output..'/soak.png')
  print('SOAK_PASS',f,cycles);manager.machine:exit()
 end
end)
