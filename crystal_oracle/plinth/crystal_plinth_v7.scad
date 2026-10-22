// V7: clustered crystals inspired by original fantasy altar. mm.
// Parts (-D 'part="..."'): body, cover, nfc_lid, pin, cluster (-D idx=N, N = row in groups),
// assembly (preview), collisions (self-test, optional -D grow=1.2).
// v7 vs v6, fitted to the real electronics:
//  * ESP32 on a ~63 x 66 mm screw-terminal board: cavity turned to 92 wide x 74 deep (corner r10)
//    so the USB-C edge faces the front port and the wired terminal edges get ~13 mm each side;
//    a corner frame on the cover locates the board; screw holes moved to (+/-30, +/-42).
//  * Elechouse PN532 NFC V3 (42.7 x 40.4 mm, plugs on two edges): the 76 mm open pocket is replaced
//    by a fitted pocket with room for the plugs, a wire slot down into the cavity, and a
//    2 mm flush lid (part nfc_lid) that hides the reader.
//  * Crystals have a D-hole in a flat root and join the body with separate D-pins (part pin),
//    so they print root-down without supports. The unused cover notch is gone.
// Kept from v5/v6: flat base; rock bosses with full-depth keyed sockets (top z=47, floor z=36);
// collision-free 11-cluster layout; countersunk cover screws.
// Print the body upside down (top face on the bed): the cavity then has no ceiling to support.
$fn=48;
part="body";
idx=0;
top_z=47;    // top of the upper tier and of every socket boss
peg_d=5.6; peg_flat=2.0;   // pin radius 2.8, flat at x=2.0 (local +x = outward)
fit=0.3;                   // radial clearance pin -> socket / root hole
pin_in_root=4;             // pin depth into a crystal root
pin_len=pin_in_root+10.5;  // socket is 11 deep
// electronics
cav=[92,74]; cav_r=10; cav_h=36;     // controller cavity (x, y), corner radius, height
board=66.6;                          // board locating frame (fits 63 or 66 either way round)
board_y0=-35;                        // USB edge of the board (cavity front wall is at -37)
screws=[[-30,-42],[30,-42],[-30,42],[30,42]];
nfc=[43.5,41.2];                     // PN532 V3 42.7 x 40.4 + clearance
nfc_plug=17;                         // room for the right-angle plugs on the +x and +y edges
pocket=[nfc[0]+nfc_plug,nfc[1]+nfc_plug]; pocket_floor=40;
lid_t=2; lid_ledge=2; lid_fit=0.15;
module shard(h=30,w=12,lean=0){
  // hexagonal quartz with long faceted point
  rotate([0,lean,0]) union(){
    cylinder(d1=w*1.08,d2=w,h=h*.66,$fn=6);
    translate([0,0,h*.66]) cylinder(d1=w,d2=0,h=h*.34,$fn=6);
  }
}
// groups: [angle, radius, [[dr, dt, h, w, lean, rot], ...]]. Local frame: +x points radially outward.
groups=[
 [  0,55,  [[0,0,17,8,12,0]]],
 [ 28,57.5,[[4,0,37,13,18,0],[-4,0,27,10,15,-25]]],
 [ 60,56,  [[3.3,0,31,11,12,0],[-3.3,0,20,8,16,25]]],
 [ 90,55,  [[4.7,0,52,15,5,0],[-4.7,0,38,12,8,-25]]],
 [120,57.5,[[3.85,0,42,13,12,0],[-3.85,0,24,9,14,25]]],
 [152,55,  [[3.3,0,30,11,22,0],[-3.3,0,20,8,16,-25]]],
 [180,55,  [[0,0,27,10,12,0]]],
 [208,55.5,[[0,0,34,12,14,0]]],
 [245,55,  [[0,0,19,8,12,0]]],
 [295,55,  [[2.95,0,23,9,10,0],[-2.95,0,18,8,12,25]]],
 [330,56.5,[[0,0,32,12,15,0]]]];
function gpos(i)=[groups[i][1]*cos(groups[i][0]),groups[i][1]*sin(groups[i][0])];
module rock(x,y,z,sx,sy,sz,rot=0){translate([x,y,z]) rotate([0,0,rot]) scale([sx,sy,sz]) sphere(1,$fn=7);}
module shell(){
 union(){
  cylinder(d=120,h=39,$fn=13);
  cylinder(d=112,h=top_z,$fn=64);
  for(a=[0:19:359]) rotate([0,0,a]) translate([51,0,19]) rock(0,0,0,12,10,22,a*3);
  for(a=[7:25:359]) rotate([0,0,a]) translate([55,0,11]) rock(0,0,0,11,9,13,a*4);
  for(a=[0:30:359]) rotate([0,0,a]) translate([48,0,39]) rock(0,0,0,8,7,8,a*2);
  // stony boss under every socket so each one gets a full-depth hole
  for(i=[0:len(groups)-1]) translate(gpos(i)) rotate([0,0,i*23])
   cylinder(d1=16,d2=12,h=top_z,$fn=7);
 }
}
// D-shaped pin / hole; flat on local +x
module dshape(d,flat,h){intersection(){cylinder(d=d,h=h,$fn=24); translate([-d,-d,0]) cube([d+flat,2*d,h]);}}
// NFC module position inside the pocket: plugs face +x and +y, toward the wire slot
nfc_min=[-pocket[0]/2,-pocket[1]/2];
module nfc_cut(){
 // lid rebate (flush 2 mm lid sits on a 2 mm ledge)
 translate([nfc_min[0]-lid_ledge,nfc_min[1]-lid_ledge,top_z-lid_t])
  cube([pocket[0]+2*lid_ledge,pocket[1]+2*lid_ledge,10]);
 // finger notch to lift the lid
 translate([-6,nfc_min[1]-lid_ledge-2.5,top_z-lid_t]) cube([12,3,10]);
 // reader + plug pocket
 translate([nfc_min[0],nfc_min[1],pocket_floor]) cube([pocket[0],pocket[1],10]);
 // wire slot down into the cavity, under the +x plug strip and the +x/+y corner
 translate([nfc_min[0]+nfc[0]+8,nfc_min[1]+8,cav_h-.1]) cube([8,pocket[1]-10,pocket_floor-cav_h+.2]);
}
// locating bars for the reader on its two open (+x, +y) sides
module nfc_bars(){
 translate([nfc_min[0]+nfc[0],nfc_min[1]+3,pocket_floor-.01]) cube([1.5,5,4]);
 translate([nfc_min[0]+3,nfc_min[1]+nfc[1],pocket_floor-.01]) cube([5,1.5,4]);
}
module body(){render(convexity=8) intersection(){union(){difference(){shell();
 // electronics cavity, corners rounded so the 13-sided wall stays >=3 mm
 translate([0,0,-.2]) linear_extrude(cav_h+.2) offset(r=cav_r) square([cav[0]-2*cav_r,cav[1]-2*cav_r],center=true);
 nfc_cut();
 // USB-C port through the front wall
 translate([-11,-70,13]) cube([22,70-cav[1]/2+2,19]);
 for(p=screws) translate([p[0],p[1],-.1]) cylinder(d=2.5,h=12);
 // keyed sockets; flat faces outward so each cluster seats at its tested orientation
 for(i=[0:len(groups)-1]) translate([gpos(i)[0],gpos(i)[1],36]) rotate([0,0,groups[i][0]])
  dshape(peg_d+2*fit,peg_flat+fit,20);
 }
 nfc_bars();
 }
 // clip the low rocks so the base prints flat
 translate([-100,-100,0]) cube([200,200,100]);
}}
module cluster(i){
 c=groups[i][2];
 difference(){
  // clipped at z=0: steeply leaning side shards otherwise dip just below the root and the
  // cluster would not sit flat on the bed
  intersection(){ translate([-60,-60,0]) cube([120,120,120]); union(){
   // shared stony-looking root under all crystals of the group (single-color part)
   hull() for(k=c) translate([k[0],k[1],0]) rotate([0,0,k[5]]) cylinder(d1=k[3]*1.6,d2=k[3]*1.05,h=5,$fn=7);
   for(k=c) translate([k[0],k[1],0]) rotate([0,0,k[5]]) {
    h=k[2]; w=k[3]; lean=k[4];
    translate([0,0,4]) shard(h,w,lean);
    translate([w*.62,0,2]) rotate([0,0,35]) shard(h*.56,w*.54,lean+18);
    translate([-w*.55,w*.22,2]) rotate([0,0,-30]) shard(h*.43,w*.48,lean-20);
   }
  }}
  // keyed hole for the joining pin
  translate([0,0,-.01]) dshape(peg_d+2*fit,peg_flat+fit,pin_in_root+.2);
 }
}
// joining pin, printed lying on its flat side
module pin(){translate([0,0,peg_flat]) rotate([0,90,0]) dshape(peg_d,peg_flat,pin_len);}
// a cluster at its installed position (root on the boss top)
module installed(i){translate([gpos(i)[0],gpos(i)[1],top_z]) rotate([0,0,groups[i][0]]) cluster(i);}
module nfc_lid(){
 l=[pocket[0]+2*lid_ledge-2*lid_fit,pocket[1]+2*lid_ledge-2*lid_fit];
 translate([-l[0]/2,-l[1]/2,0]) cube([l[0],l[1],lid_t]);
}
module cover(){
 difference(){cylinder(d=112,h=3,$fn=72);
  for(p=screws) translate([p[0],p[1],0]) {
   translate([0,0,-.1]) cylinder(d=3.4,h=3.2);
   // countersink for M3 flat-head screws so nothing sticks out under the base
   translate([0,0,-.01]) cylinder(d1=6.6,d2=3.4,h=1.6);
  }
 }
 // locating ribs, 0.4 mm clear of the cavity side walls
 for(s=[-1,1]) translate([s*(cav[0]/2-0.4-1)-1,-cav[1]/2+cav_r+2,3]) cube([2,cav[1]-2*cav_r-4,3]);
 // corner frame that locates the controller board (USB edge toward the front port)
 for(sx=[-1,1],sy=[0,1]) translate([sx*board/2,board_y0+sy*board,3]) mirror([sx<0?1:0,0,0]) mirror([0,sy?0:1,0])
  difference(){translate([-8,-8,0]) cube([9.6,9.6,4]); translate([-8.1,-8.1,-.1]) cube([8.1,8.1,4.2]);}
}
if(part=="body") body();
else if(part=="cover") cover();
else if(part=="nfc_lid") nfc_lid();
else if(part=="pin") pin();
else if(part=="cluster") cluster(idx);
else if(part=="assembly") {body(); translate([0,0,-3]) cover();
 translate([0,0,top_z-lid_t]) nfc_lid(); for(i=[0:len(groups)-1]) installed(i);}
// collision self-test: every pair of installed clusters, optionally grown by `grow` in x/y about
// its pin. A pass renders EMPTY ("Current top level object is empty"); any solid output = overlap.
else if(part=="collisions") {grow=is_undef(grow)?1:grow;
 for(i=[0:len(groups)-2], j=[i+1:len(groups)-1]) intersection(){
  translate([gpos(i)[0],gpos(i)[1],top_z]) rotate([0,0,groups[i][0]]) scale([grow,grow,1]) cluster(i);
  translate([gpos(j)[0],gpos(j)[1],top_z]) rotate([0,0,groups[j][0]]) scale([grow,grow,1]) cluster(j);}}
