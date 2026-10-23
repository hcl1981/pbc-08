#pragma once

#include <string>

#ifndef VER_STR
#define VER_STR "v0.0.0.0"
#endif

// PBC+ port: cart menu for joystick + A/B, replacing fake-08's keyboard
// oriented one. Up/down choose, left/right page, A or B start.
// The label of the selected cart is loaded once the selection rests.

const char * fake08DefaultCart = R"#(
pico-8 cartridge // http://www.pico-8.com
version 35
__lua__
-- pbc+ cart menu

function lower(s)
 local r=""
 for i=1,#s do
  local c=ord(s,i)
  if (c>=65 and c<=90) c+=32
  r..=chr(c)
 end
 return r
end

function wrap(s,w)
 local lines={}
 while #s>w do
  add(lines,sub(s,1,w))
  s=sub(s,w+1)
 end
 add(lines,s)
 return lines
end

function refresh()
 items={}
 ncarts=0
 if __pwd and __pwd()~="" then
  add(items,{n="..",d=true})
 end
 if __listdirs then
  for d in all(__listdirs()) do add(items,{n=d,d=true}) end
 end
 if __listcarts then
  for c in all(__listcarts()) do add(items,{n=c}) ncarts+=1 end
 end
 add(items,{n="settings",s=true})
 sel=1 top=1 cur=nil wait=0 haslbl=false
end

function _init()
 refresh()
 err=""
 if (__getbioserror) err=__getbioserror() or ""
end

function _update60()
 local n=#items
 if (btnp(2)) sel-=1
 if (btnp(3)) sel+=1
 if (btnp(0)) sel-=7
 if (btnp(1)) sel+=7
 if (sel<1) sel=btnp(2) and n or 1
 if (sel>n) sel=btnp(3) and 1 or n
 if (sel<top) top=sel
 if (sel>top+5) top=sel-5
 local it=items[sel]
 if it~=cur then
  cur=it wait=10 haslbl=false
 end
 if wait>0 then
  wait-=1
  if wait==0 and not it.d and not it.s and __loadlabel then
   __loadlabel(it.n,false,0)
   haslbl=true
  end
 end
 if btnp(4) or btnp(5) then
  err=""
  if it.s then
   __loadsettingscart()
  elseif it.d then
   __cd(it.n)
   refresh()
  else
   load(it.n)
  end
 end
end

function _draw()
 cls(1)
 rectfill(0,0,127,8,0)
 print("pico-8 player for pbc+",2,2,7)
 print(ncarts.." carts",127-4*#(ncarts.." carts"),2,6)

 if err~="" then
  rectfill(4,12,123,76,8)
  print("error",8,15,7)
  local y=23
  for l in all(wrap(lower(err),28)) do
   if (y<72) print(l,8,y,7)
   y+=7
  end
 elseif haslbl then
  rectfill(30,11,97,78,0)
  sspr(0,0,128,128,32,13,64,64)
 elseif cur and cur.d then
  print("folder",52,42,13)
 elseif cur and cur.s then
  print("scaling, menu colours",22,42,13)
 elseif ncarts==0 then
  print("no carts yet:",8,20,7)
  print("hold a or b while switching",8,30,6)
  print("on, copy .p8 / .p8.png",8,37,6)
  print("files to the usb drive",8,44,6)
  print("pico8, then eject it.",8,51,6)
 end

 for i=0,5 do
  local k=top+i
  local it=items[k]
  if it then
   local y=82+i*7
   local name=lower(it.n)
   if (it.d) name="/"..name
   if (it.s) name="* settings"
   if (#name>31) name=sub(name,1,30).."."
   if k==sel then
    rectfill(0,y-1,127,y+5,12)
    print(name,2,y,0)
   else
    print(name,2,y,it.d and 10 or (it.s and 13 or 7))
   end
  end
 end
 print("\x94\x83 choose   \x8e\x97 start",2,123,5)
end
)#";
