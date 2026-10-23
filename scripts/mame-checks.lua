-- Functional checks on the actual cartridge. RAM writes below explicitly
-- arrange test conditions (hits, pit positions, checkpoint); this is separate
-- from the input-only traversal and never counts as a fair playthrough.
local f=0
local m=manager.machine.devices[':maincpu'].spaces.program
local p=manager.machine.ioport.ports
local joy=p[':edge:joy:JOY1'].fields
local function u8(n) return m:read_u8(sym[n]) end
local function s16(n) return m:read_i16(sym[n]) end
local function expect(ok,name)
 if ok then print('CHECK',name) else print('CHECK_FAIL',name);manager.machine:exit() end
end
local function position(x,y)
 m:write_u32(sym.player_x_fp,x*256);m:write_u32(sym.player_y_fp,y*256);m:write_u16(sym.player_x,x);m:write_u16(sym.player_y,y)
 m:write_u32(sym.player_vx_fp,0);m:write_u32(sym.player_vy_fp,0)
 m:write_u8(sym.player_ground,0);m:write_u8(sym.invuln,0)
end
local function hit()
 -- Native Hazard layout: x/y 32-bit fixed point, vx/vy 16-bit, kind/life bytes.
 m:write_u32(sym.hazards,s16('player_x')*256)
 m:write_u32(sym.hazards+4,(s16('player_y')+44)*256)
 m:write_u16(sym.hazards+8,0);m:write_u16(sym.hazards+10,0)
 m:write_u8(sym.hazards+12,2);m:write_u8(sym.hazards+13,5)
end
local function ball_count()
 local n=0;for i=0,2 do if m:read_u8(sym.balls+i*20+10)>0 then n=n+1 end end;return n
end
emu.register_frame_done(function()
 if u8('death_timer')>0 and u8('lives')>0 then return end
 f=f+1
 p[':AUDIO_COIN'].fields['Coin 1']:set_value(((f>=180 and f<184) or (f>=1000 and f<1005)) and 1 or 0)
 p[':edge:joy:START'].fields['1 Player Start']:set_value(((f>=210 and f<216) or (f>=1030 and f<1036)) and 1 or 0)
 joy['P1 Up']:set_value((f>=250 and f<254) and 1 or 0)
 joy['P1 B']:set_value((f==250 or f==260 or f==270 or f==280 or f==290 or f==322) and 1 or 0)
 joy['P1 A']:set_value(((f>=310 and f<335) or (f>=620 and f<634) or f==681) and 1 or 0)
 joy['P1 Down']:set_value(((f>=322 and f<330) or (f>=681 and f<688)) and 1 or 0)
 joy['P1 Right']:set_value(((f>=590 and f<612) or (f>=620 and f<640)) and 1 or 0)
 if f==170 then manager.machine.screens[':screen']:snapshot(output..'/title.png') end
 if f==240 then expect(u8('lives')==3 and u8('armored')==1 and u8('bios_user_mode')==2,'coin/start enters game') end
 if f==253 then expect(ball_count()>0 and m:read_i16(sym.balls+6)<0,'UP+B upward projectile') end
 if f==295 then expect(ball_count()==3,'three-projectile cap') end
 if f==326 then
  local down=false;for i=0,2 do if m:read_u8(sym.balls+i*20+10)>0 and m:read_i16(sym.balls+i*20+6)>0 then down=true end end
  expect(down and u8('player_ground')==0,'air DOWN+B downward projectile')
 end
 if f==400 then expect(u8('player_ground')==1 and s16('player_y')==144,'jump lands on ground') end
 if f==420 then hit() end
 if f==423 then expect(u8('armored')==0 and u8('lives')==3 and u8('invuln')>0,'first hit removes shirt without taking life');manager.machine.screens[':screen']:snapshot(output..'/armor-lost.png') end
 if f==520 then hit() end
 if f==523 then expect(u8('lives')==2 and u8('armored')==1 and s16('player_x')==48,'second hit loses life and resets armor') end
 if f==540 then position(550,230) end
 if f==545 then expect(u8('lives')==1 and s16('player_x')==48,'pit bypasses armor and respawns') end
 if f==555 then m:write_u8(sym.lives,3) end -- explicit fixture, not earned lives
 if f==560 then position(1950,144) end
 if f==564 then expect(u8('checkpoint_reached')==1,'checkpoint activates at tile 121') end
 if f==565 then position(2450,230) end
 if f==570 then expect(u8('lives')==2 and s16('player_x')==1968 and s16('player_y')==144,'pit respawns at checkpoint with synchronized position') end
 if f==590 then position(210,144);m:write_u8(sym.invuln,120) end
 if f==612 then expect(s16('player_x')==217,'stone wall blocks horizontal movement') end
 if f==670 then expect(u8('player_ground')==1 and s16('player_y')==112,'jump lands on stone wall: y='..s16('player_y')..' x='..s16('player_x')..' ground='..u8('player_ground')) end
 if f==680 then position(400,112);m:write_u8(sym.player_ground,1) end
 if f==686 then expect(u8('drop_timer')>0 and s16('player_y')>112,'DOWN+A drops through plank') end
 if f==715 then expect(u8('player_ground')==1 and s16('player_y')==144,'drop lands on ground') end
 if f==740 then m:write_u8(sym.lives,1);position(550,230) end
 if f==800 then expect(u8('lives')==0,'last pit death reaches game over');manager.machine.screens[':screen']:snapshot(output..'/game-over.png') end
 if f==1070 then expect(u8('lives')==3 and u8('armored')==1 and s16('player_x')==48,'second coin/start cycle resets game');print('CHECKS_PASS');manager.machine:exit() end
end)
