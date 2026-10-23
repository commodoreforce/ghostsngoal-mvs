-- Explicit RAM fixtures on the native cartridge. These test content rules;
-- the separate level1 scenario is the input-only traversal including penalties.
local m=manager.machine.devices[':maincpu'].spaces.program
local ports=manager.machine.ioport.ports
local joy=ports[':edge:joy:JOY1'].fields
local f=0
local function u8(n) return m:read_u8(sym[n]) end
local function u16(n) return m:read_u16(sym[n]) end
local function u32(n) return m:read_u32(sym[n]) end
local function expect(ok,name)
 if ok then print('CONTENT_CHECK',name) else print('CHECK_FAIL',name);manager.machine:exit() end
end
local function position(x,y)
 m:write_u32(sym.player_x_fp,x*256);m:write_u32(sym.player_y_fp,y*256);m:write_u16(sym.player_x,x);m:write_u16(sym.player_y,y)
 m:write_u32(sym.player_vx_fp,0);m:write_u32(sym.player_vy_fp,0)
 m:write_u8(sym.player_ground,0);m:write_u8(sym.invuln,120)
 m:write_u16(sym.zombie_timer,10000)
end
local function clear_enemies()
 for i=0,11 do m:write_u8(sym.enemies+i*18,0);m:write_u8(sym.enemies+i*18+1,2) end
end
local function clear_balls()
 for i=0,7 do m:write_u8(sym.balls+i*20+10,0) end
end
local function fire(x,y,kind,velocity)
 clear_balls()
 m:write_u16(sym.balls,x);m:write_u16(sym.balls+2,y)
 m:write_u16(sym.balls+4,velocity or 964);m:write_u16(sym.balls+6,0)
 m:write_u8(sym.balls+8,50);m:write_u8(sym.balls+9,0);m:write_u8(sym.balls+10,1)
 m:write_u32(sym.balls+12,x*256);m:write_u32(sym.balls+16,y*256)
 m:write_u8(sym.ball_kind,kind or 0);m:write_u8(sym.ball_damage,kind==1 and 3 or 1)
 m:write_u8(sym.ball_pierce,kind==1 and 99 or 0);m:write_u16(sym.ball_hits,0);m:write_u8(sym.ball_counted,0)
end
local function pickup(kind)
 position(80,144);clear_enemies();clear_balls()
 for i=0,35 do m:write_u8(sym.pickups+i*20+19,0) end
 local a=sym.pickups+20*20
 m:write_u32(a,80*256);m:write_u32(a+4,190*256)
 m:write_u16(a+8,0);m:write_u16(a+10,0);m:write_u16(a+12,190);m:write_u16(a+14,100)
 m:write_u8(a+16,kind);m:write_u8(a+17,1);m:write_u8(a+18,255);m:write_u8(a+19,1)
end
local function enemy(kind,hp,timer)
 clear_balls();clear_enemies();position(600,144)
 m:write_u8(sym.enemies,kind);m:write_u8(sym.enemies+1,0);m:write_u8(sym.enemies+2,hp)
 m:write_u8(sym.enemy_attack,0);m:write_u8(sym.enemy_attack_timer,0);m:write_u16(sym.enemy_cooldown,180);m:write_u32(sym.enemy_x_fp,660*256);m:write_u32(sym.enemy_y_fp,(kind==3 and 176 or 160)*256);m:write_u8(sym.enemies+3,0);m:write_u8(sym.enemies+4,0)
 m:write_u16(sym.enemies+6,660);m:write_u16(sym.enemies+8,kind==3 and 176 or 160)
 m:write_u16(sym.enemies+10,0);m:write_u16(sym.enemies+12,0)
 m:write_u16(sym.enemies+14,kind==3 and 176 or 160);m:write_u16(sym.enemies+16,timer or 100)
end
local steps={}
local before,life_before,max_balls,shots_before=0,0,0,0
local held=false
local jumpheld=false
local rightheld=false
local downheld=false
local tap_until=0
local min_player_y=300
local function add(setup,check,delay) steps[#steps+1]={setup,check,delay or 8} end
add(function() end,function()
 local count=0;for i=0,19 do if m:read_u8(sym.pickups+i*20+19)==1 then count=count+1 end end
 expect(count==20,'all 20 authored pumpkins exist')
 for i=0,7 do expect(m:read_u8(sym.crate_hp+i)==3,'crate '..i..' starts at 3 HP') end
end)
for i,crate in ipairs(crates) do
 for hit=1,3 do
  add(function()
   position(crate[1]-48,math.max(0,crate[2]-64));clear_enemies()
   fire(crate[1]+16,crate[2]+8,0,-964)
  end,function()
   expect(m:read_u8(sym.crate_hp+i-1)==3-hit,'crate '..i..' damage step '..hit)
   if hit==3 then
    local found=false
    for slot=20,35 do local a=sym.pickups+slot*20
     if m:read_u8(a+19)==1 and m:read_u8(a+16)==crate[3] and math.abs(m:read_i32(a)/256-crate[1])<16 then found=true end
    end
    expect(found,'crate '..i..' releases its specified bonus')
   end
  end)
 end
end
add(function() end,function() expect(u16('crates_broken')==8,'all 8 crates release their specified content') end)
for _,test in ipairs({{1,200},{6,500},{2,5000}}) do
 add(function() before=u32('score');pickup(test[1]) end,
     function() expect(u32('score')==before+test[2],'pickup score '..test[2]) end)
end
add(function() m:write_u8(sym.armored,0);before=u32('score');pickup(3) end,
    function() expect(u8('armored')==1 and u32('score')==before,'shirt restores lost armor') end)
add(function() before=u32('score');pickup(3) end,
    function() expect(u32('score')==before+1000,'spare shirt awards 1000') end)
add(function() life_before=u8('lives');pickup(4) end,
    function() expect(u8('lives')==life_before+1,'piadina earns one life') end)
add(function() pickup(5) end,function()
 expect(u16('boost_timer')>640 and u8('gold_palette')==1,'seahorse starts 11-second Re Mida and gold artwork')
 manager.machine.screens[':screen']:snapshot(output..'/re-mida.png')
end)
add(function()
 position(300,0);clear_balls();held=true;max_balls=0;shots_before=u16('shots_fired')
end,function() held=false;expect(max_balls==7 and u16('shots_fired')>=shots_before+25,'Re Mida sustains autofire past four seconds with seven-ball cap') end,300)
add(function()
 clear_balls();clear_enemies();m:write_u8(sym.crate_hp,3);position(240,144);fire(crates[1][1]-16,crates[1][2]+8,1)
end,function() expect(m:read_u8(sym.crate_hp)==0,'gold ball breaks a fresh crate in one hit') end)
add(function() m:write_u16(sym.boost_timer,2);held=false;clear_balls() end,
    function() expect(u16('boost_timer')==0 and u8('gold_palette')==0,'power expires and restores normal palettes') end)
add(function() position(300,0);clear_balls();m:write_u16(sym.charge_timer,0);held=true end,
    function() held=false end,45)
add(function() end,function()
 local charged=false;for i=0,7 do if m:read_u8(sym.balls+i*20+10)>0 and m:read_u8(sym.ball_kind+i)==2 and m:read_u8(sym.ball_damage+i)==3 then charged=true end end
 expect(charged,'holding and releasing B creates a charged piercing shot')
end,3)
for i=1,4 do
 add(function()
  if i==1 then m:write_u8(sym.combo_count,0);m:write_u16(sym.combo_timer,0) end
  enemy(1,1);before=u32('score');fire(648,184,0)
 end,function() expect(u32('score')==before+i*100 and u8('combo_count')==i,'enemy combo x'..i) end)
end
add(function() m:write_u8(sym.combo_count,0);m:write_u16(sym.combo_timer,0);enemy(3,2);before=u32('score');fire(648,184,0) end,
    function() expect(m:read_u8(sym.enemies+2)==1 and u32('score')==before,'pumpkin survives first hit with cracked pose') end)
add(function() fire(m:read_i16(sym.enemies+6)-12,m:read_i16(sym.enemies+8)+16,0) end,
    function() expect(m:read_u8(sym.enemies+1)==1 and u32('score')==before+200,'second pumpkin hit earns 200 points') end)
add(function() enemy(4,1,150);before=u32('score');fire(648,184,0) end,
    function() expect(m:read_u8(sym.enemies+2)==1 and u32('score')==before,'faded ghost is invulnerable') end)
add(function() clear_enemies();m:write_u32(sym.score,19999);m:write_u32(sym.next_life,20000);life_before=u8('lives');pickup(1) end,
    function() expect(u32('score')==20199 and u8('lives')==life_before+1 and u32('next_life')==60000,'extra life at 20000') end)
add(function() m:write_u32(sym.score,59999);m:write_u32(sym.next_life,60000);life_before=u8('lives');pickup(1) end,
    function() expect(u8('lives')==life_before+1 and u32('next_life')==160000,'extra life at 60000 then every 100000') end)
add(function() clear_enemies();position(80,144);m:write_u16(sym.level_time,1);m:write_u16(sym.time_tick,59);life_before=u8('lives') end,
    function() expect(u8('lives')==life_before-1 and u16('level_time')>140,'time up loses exactly one life and resets the clock') end)
-- Reachable controller-driven crate interactions, not only direct ball fixtures.
add(function()
 clear_enemies();clear_balls();position(840,144);m:write_u8(sym.crate_hp+1,3);min_player_y=144;jumpheld=true
end,function()
 jumpheld=false;expect(min_player_y>=119,'raised crate blocks the player head from below')
end,28)
add(function() position(840,40) end,function()
 expect(u8('player_ground')==1 and m:read_i16(sym.player_y)==64,'player lands on a raised crate')
end,25)
add(function() position(264,144);m:write_u8(sym.crate_hp,3);rightheld=true end,function()
 rightheld=false;expect(m:read_i16(sym.player_x)==265,'crate blocks sideways movement x='..m:read_i16(sym.player_x)..' hp='..m:read_u8(sym.crate_hp)..' y='..m:read_i16(sym.player_y))
end,20)
for hit=1,3 do
 add(function() clear_balls();downheld=true;tap_until=f+2 end,function()
  expect(m:read_u8(sym.crate_hp)==3-hit,'crouch+B controller shot damages ground crate '..hit)
 end,8)
end
add(function() downheld=false;pickup(5);m:write_u8(sym.combo_count,0);m:write_u16(sym.combo_timer,0) end,function() end)
add(function() enemy(1,1);before=u32('score');fire(648,184,1) end,function()
 expect(u32('score')==before+300,'Re Mida triples kill points')
end)
add(function() m:write_u16(sym.boost_timer,0);m:write_u8(sym.combo_count,0);m:write_u16(sym.combo_timer,0);enemy(4,1,100);before=u32('score');fire(648,184,0) end,function()
 expect(u32('score')==before+300,'solid ghost earns 300 points')
end)
add(function() enemy(1,1,1);before=u32('score');fire(648,202,0) end,function()
 expect(m:read_u8(sym.enemies+2)==1 and u32('score')==before,'emerging zombie cannot be hit early')
end)
add(function() enemy(1,1,100);m:write_u8(sym.enemies+4,1);m:write_u16(sym.enemy_cooldown,0) end,function()
 expect(m:read_u8(sym.enemy_attack)==1,'sliding zombie announces before attacking')
end)
add(function() m:write_u8(sym.enemy_attack_timer,1) end,function()
 expect(m:read_u8(sym.enemy_attack)==2,'announced zombie enters slide attack')
end,4)
add(function() enemy(2,1,100);m:write_u16(sym.enemy_cooldown,0) end,function()
 expect(m:read_u8(sym.enemies+4)==1,'bat announces its dive')
end)
add(function() m:write_u8(sym.enemy_attack_timer,22) end,function()
 expect(m:read_u8(sym.enemies+4)==2 and m:read_i16(sym.enemies+6)<660,'bat dives after its warning')
end)
add(function()
 pickup(1);local a=sym.pickups+20*20;m:write_u32(a,180*256);m:write_u16(a+12,40);m:write_u16(a+14,2)
end,function()
 expect(m:read_u8(sym.pickups+20*20+19)==0,'temporary bonuses expire instead of lingering forever')
 expect(u32('high_score')>=60000,'high score survives a life loss and tracks earned points')
end)
local index,next_at,waiting=1,245,false
emu.register_frame_done(function()
 if u8('death_timer')>0 and u8('lives')>0 then return end
 f=f+1
 ports[':AUDIO_COIN'].fields['Coin 1']:set_value(f>=180 and f<184 and 1 or 0)
 ports[':edge:joy:START'].fields['1 Player Start']:set_value(f>=210 and f<216 and 1 or 0)
 joy['P1 B']:set_value((held or f<=tap_until) and 1 or 0)
 joy['P1 A']:set_value(jumpheld and 1 or 0)
 joy['P1 Right']:set_value(rightheld and 1 or 0)
 joy['P1 Down']:set_value(downheld and 1 or 0)
 if m:read_i16(sym.player_y)<min_player_y then min_player_y=m:read_i16(sym.player_y) end
 if f>=245 then
  local count=0;for i=0,7 do if m:read_u8(sym.balls+i*20+10)>0 then count=count+1 end end
  if count>max_balls then max_balls=count end
 end
 if f>=next_at then
  local step=steps[index]
  if not step then print('CONTENT_PASS',#steps);manager.machine:exit();return end
  if not waiting then step[1]();waiting=true;next_at=f+step[3]
  else step[2]();index=index+1;waiting=false;next_at=f+3 end
 end
 if f>4000 then print('CHECK_FAIL content timeout');manager.machine:exit() end
end)
