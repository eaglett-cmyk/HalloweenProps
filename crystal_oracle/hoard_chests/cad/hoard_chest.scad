// Hoard Chest: pop-open treasure chest for the Crystal Oracle (Dragonglow). mm. See ../docs/DESIGN.md.
// One servo latch (SG90), one lever microswitch (lid closed), one WS2812B pixel disc behind the emblem,
// one Hoard Node board (RJ45 out the back). A rubber band pulls the lid open.
//
// Parts (-D 'part="..."'):  body, lid, arm, emblem (-D emblem_kind=0 coin | 1 star | 2 gem),
//   deck (cover over the back compartment: hides servo, switch, board and wires when the lid is open),
//   backplate (cover over the rubber band and the lid's tail on the back),
//   assembly (preview, -D open_deg=0..103, -D locked=true|false), check (must render EMPTY),
//   body_print / lid_print / arm_print / emblem_print / deck_print / backplate_print = the same parts turned the way they print.
// Size (-D 'size="small"' | "long"): small bay 110 x 58 x 40 (fun-size candy), long bay 160 x 58 x 40 (full-size bars).
// Print: body upright as modelled; lid upside down (flat top on the bed); arm flat as modelled; emblem face up.
// No supports anywhere.
// SCREWS: every screw into printed plastic is a #4 x 1/2" FLAT-HEAD (82 deg) wood / sheet-metal screw, the ones
// bought for the Wakestone plinth cover: 1 node board + 2 deck + 4 backplate = 7 per chest. Pilot 2.5 mm, 11.5 mm deep.
// Only the servo keeps the two small screws from its own bag (its tab holes are 2 mm, a #4 does not pass).
$fn=40;
part="assembly";
size="small";
emblem_kind=0;
open_deg=0;
locked=true;

// ---- box ------------------------------------------------------------------------------------
bay_w = size=="long" ? 160 : 110;
bay_d = 58;  bay_h = 40;
wall = 2.0;  floor_t = 4.0;  part_t = 1.6;
comp_d = 22;                       // back compartment depth (servo, switch, node board)
lx = size=="long" ? 40 : 4;        // where the latch group sits along the compartment
Wo = bay_w + 2*wall;
y_c0 = wall + bay_d + part_t;      // compartment-local cy = 0 here (back face of the partition)
Do = y_c0 + comp_d + wall;
Hb = floor_t + bay_h;              // top of the body = underside of the lid
function CX(cx) = wall + cx;
function CY(cy) = y_c0 + cy;

// ---- lid ------------------------------------------------------------------------------------
rim = 6;  lid_h = 18;  top_t = 2;
// ---- hinge + rubber band --------------------------------------------------------------------
hinge_off = 3.5;                   // hinge axis behind the back face
hinge_r = 3.3;  pin_d = 2.2;       // pin = 1.75 mm filament or a straightened large paper clip
kn_b = 18;  kn_l = 12;  kn_gap = 0.4;
kb1 = round(0.10*Wo);  kb2 = round(0.90*Wo) - kn_b;    // body knuckles start x
tail_w = 8;
// ---- SG90 servo (check yours: ../docs/PARTS-RESEARCH.md) -----------------------------------
sv_L = 22.8;  sv_W = 12.2;  sv_H = 22.7;   // body
sv_tab = 4.75;  sv_tab_t = 2.5;  sv_tab_z = 16.0;   // tab length each side, thickness, underside above the body bottom
sv_axis = 6.0;                     // shaft axis from the left end of the body
sv_boss = 26.7;                    // body bottom to the top of the round gear boss (the arm sits just above it)
sv_top = 30.0;                     // body bottom to the top of the spline
spline_d = 4.7;                    // press fit on the 4.8 mm, 21-tooth spline
ax_cx = lx + 27;  ax_cy = 15.6;    // shaft axis in the compartment. The latch group is drawn to the right of
                                   // the axis and then MIRRORED about it (module G), so the arm releases CLOCKWISE seen from above:
                                   // a Phillips screwdriver through the keyhole in the lid, turned clockwise on the horn screw, frees a stuck chest.
arm_z0 = 38.0;  arm_t = 4.0;       // arm underside, thickness (top = 42.0)
arm_len = 14;  arm_w = 7;
unlock_deg = 90;                   // arm swings this far towards +x to release
sv_z0 = arm_z0 - 0.3 - sv_boss;            // servo body bottom
// ---- lever microswitch (SS-5GL / KW12-3) ----------------------------------------------------
sw_L = 19.8;  sw_W = 6.4;  sw_H = 10.2;
sw_op = 11.7;                      // lever operating position above the body bottom
sw_cx = ax_cx - 27;  sw_cy = 0.4;   // before mirroring
finger_z = Hb - 5;                 // tip of the lid finger with the lid closed
sw_z0 = finger_z + 0.6 - sw_op;    // body bottom: the finger ends 0.6 below the operating position
// ---- Hoard Node board + RJ45 (check: ../pcb/PCB-NOTES.md) ------------------------------------
pcb = [42, 20, 1.6];
pcb_cx0 = bay_w - pcb[0] - 1;      // board occupies cx pcb_cx0 .. +42, cy 2 .. 22 (back edge on the back wall)
pcb_z0 = floor_t + 8.5;               // post tall enough for a #4 x 1/2" screw (11.1 mm into the plastic)
jack_w = 15.3;  jack_h = 13.5;  jack_px = 9.5;  jack_over = 3.0;  jack_len = 17.74;   // Amphenol 54602-908LF (height not verified)
hole_px = 30;  hole_py = 5;
// ---- emblem + pixel -------------------------------------------------------------------------
em_z = 20;  em_cav = 18;  plate_w = 26;  plate_t = 6;
gx = size=="long" ? CX(pcb_cx0 - 7) : Wo/2;          // where the pixel wires come up into the compartment

// ---- #4 x 1/2" flat-head screws + the two covers ----------------------------------------------
n4_pilot = 2.5;  n4_clear = 3.4;  n4_head = 6.2;  n4_len = 12.7;
deck_t = 1.6;  deck_z = Hb - deck_t;          // deck = flush cover over the back compartment
boss_s = 7.2;  boss_h = 12;                    // screw bosses under the deck
bossR_cx = pcb_cx0 - boss_s - 1;               // right-hand boss, on the back wall just left of the node board
deck_scr = [[boss_s/2, comp_d-boss_s/2],[bossR_cx+boss_s/2, comp_d-boss_s/2]];
bp_in = 7;  bp_ck = 7;  bp_d = 18.5;  bp_t = 2;   // band channel on the back: half inner width, cheek width, depth, plate thickness
bp_h = Hb - 0.6;  bp_scr_z = [12, Hb-14];

module Cc(cx,cy,z){ translate([CX(cx),CY(cy),z]) children(); }
module G(){ translate([CX(ax_cx),0,0]) mirror([1,0,0]) translate([-CX(ax_cx),0,0]) children(); }   // latch group mirror
bar_z = arm_z0 + 0.2;              // top of the lid-tab bar under the locked arm (0.2 squeeze takes up the play)

// ================= body =================
module shell(){
 difference(){
  cube([Wo,Do,Hb]);
  translate([wall,wall,floor_t]) cube([bay_w,bay_d,Hb]);
  translate([wall,y_c0,floor_t]) cube([bay_w,comp_d,Hb]);
  // plank lines
  for(z=[15,30]) for(s=[[-0.01,-0.01,0],[-0.01,Do-0.5,0]]) translate([s[0],s[1],z-0.6]) cube([Wo+0.02,0.51,1.2]);
  for(z=[15,30]) for(x=[-0.01,Wo-0.5]) translate([x,-0.01,z-0.6]) cube([0.51,Do+0.02,1.2]);
 }
 // corner straps
 for(x=[0,Wo]) for(y=[0,Do]) translate([x,y,0]) strap_corner(x==0?1:-1, y==0?1:-1);
}
module strap_corner(sx,sy){
 // 9 mm wide iron strap wrapped round the corner, 0.8 proud
 translate([sx>0?-0.8:-9, sy>0?-0.8:0, 0]) cube([9.8,0.8,Hb]);
 translate([sx>0?-0.8:0, sy>0?-0.8:-9, 0]) cube([0.8,9.8,Hb]);
}
module lock_plate(){
 translate([Wo/2-plate_w/2,-plate_t,0]) cube([plate_w,plate_t+0.01,em_z]);
 translate([Wo/2,0.01,em_z]) rotate([90,0,0]) cylinder(d=plate_w,h=plate_t+0.01,$fn=64);
}
module lock_cuts(){
 translate([Wo/2,-0.8,em_z]) rotate([90,0,0]) cylinder(d=em_cav,h=plate_t,$fn=64);          // light cavity
 translate([Wo/2-3,-4.4,-0.01]) cube([6,3.2,em_z-em_cav/2+2]);                              // wire channel down
 // wire groove under the floor, then up into the compartment
 pts = [[Wo/2,-2.8],[Wo/2,8],[gx,CY(12)]];
 for(i=[0:len(pts)-2]) hull(){ for(p=[pts[i],pts[i+1]]) translate([p[0],p[1],-0.01]) cylinder(d=6,h=2.6); }
 translate([gx,CY(12),-0.01]) cylinder(d=7,h=floor_t+0.02);
}
module slot_block(){
 hull(){ Cc(ax_cx-5.1,0,34) cube([16.6,4,Hb-34]); Cc(ax_cx-5.1,-0.01,30) cube([16.6,0.02,Hb-30]); }
}
module slot_cut(){ Cc(ax_cx-3.6,0.8,arm_z0-0.4) cube([13.6,3.21,arm_t+0.7]); }
module sw_rib(x0,w,stop_from,stop_to){
 // ledge under one end of the switch + the wall that keeps it against the partition + an end stop
 hull(){ Cc(x0,0,sw_z0-0.01) cube([w,8.1,0.01]); Cc(x0,-0.01,sw_z0-8.1) cube([w,0.02,8.1]); }
 Cc(x0,sw_cy+sw_W+0.15,sw_z0-0.01) cube([w,8.1-sw_cy-sw_W-0.15,6]);
 Cc(stop_from,0,sw_z0-0.01) cube([stop_to-stop_from,8.1,6]);
}
module brackets(){
 for(b=[ax_cx-sv_axis-0.2-4.6, ax_cx-sv_axis+sv_L+0.2]){
  hull(){ Cc(b,11,sv_z0+sv_tab_z-3) cube([4.6,11.01,3]); Cc(b,21.99,sv_z0+sv_tab_z-14) cube([4.6,0.02,14]); }
 }
}
module bracket_holes(){ for(b=[ax_cx-sv_axis-2.4, ax_cx-sv_axis+sv_L+2.4]) Cc(b,ax_cy,sv_z0+sv_tab_z-9) cylinder(d=1.6,h=9.1,$fn=16); }
module pcb_mounts(){
 Cc(pcb_cx0+pcb[0]-hole_px, comp_d-hole_py, floor_t-0.01) cylinder(d=7.5,h=pcb_z0-floor_t+0.01);
 for(x=[pcb_cx0+8, pcb_cx0+pcb[0]-4.5]) Cc(x,-0.01,floor_t-0.01) cube([3,4.81,pcb_z0-floor_t+0.01]);
}
// screw bosses and ledges that carry the deck
module deck_supports(){
 z1 = deck_z - boss_h;
 hull(){ Cc(-0.01,comp_d-boss_s,z1) cube([boss_s,boss_s+0.01,boss_h]); Cc(-0.01,comp_d-0.02,z1-9) cube([0.02,0.03,0.01]); }
 hull(){ Cc(bossR_cx,comp_d-boss_s,z1) cube([boss_s,boss_s+0.01,boss_h]); Cc(bossR_cx,comp_d-0.02,z1-boss_s) cube([boss_s,0.03,0.01]); }
 // ledges on the partition, clear of the latch group
 for(r=[[1, ax_cx-21],[ax_cx+29.5, bay_w-1]]) if(r[1]-r[0]>4)
  hull(){ Cc(r[0],-0.01,deck_z-0.01) cube([r[1]-r[0],1.5,0.01]); Cc(r[0],-0.01,deck_z-1.6) cube([r[1]-r[0],0.02,0.01]); }
 // ledge on the left side wall
 hull(){ Cc(-0.01,2,deck_z-0.01) cube([1.5,comp_d-boss_s-3,0.01]); Cc(-0.01,2,deck_z-1.6) cube([0.02,comp_d-boss_s-3,0.01]); }
}
module deck_pilots(){ for(p=deck_scr) Cc(p[0],p[1],deck_z-boss_h+0.5) cylinder(d=n4_pilot,h=boss_h,$fn=16); }
// two cheeks on the back wall: the band and the lid's tail run between them, the backplate screws onto them
module band_cheeks(){ for(sx=[-1,1]) translate([Wo/2+sx*(bp_in+bp_ck/2)-bp_ck/2, Do-0.01, 0]) cube([bp_ck,bp_d+0.01,bp_h]); }
module cheek_pilots(){ for(sx=[-1,1]) for(z=bp_scr_z) translate([Wo/2+sx*(bp_in+bp_ck/2), Do+bp_d-12, z]) rotate([-90,0,0]) cylinder(d=n4_pilot,h=12.1,$fn=16); }
module body_knuckles(){
 for(k=[kb1,kb2]) hull(){
  translate([k,Do+hinge_off,Hb]) rotate([0,90,0]) cylinder(r=hinge_r,h=kn_b);
  translate([k,Do-0.01,Hb-12]) cube([kn_b,0.02,12]);
 }
}
module band_fin(){
 // the rubber band seats in one of the two slanted slots (upper = weaker, lower = stronger)
 translate([Wo/2-1.5,Do-0.01,0]) difference(){
  cube([3,8,20]);
  for(z=[3,10]) translate([-0.01,8.01,z]) rotate([-45,0,0]) translate([0,-7,0]) cube([3.02,7,2.6]);
 }
}
module body(){
 difference(){
  union(){ shell(); lock_plate(); pcb_mounts(); body_knuckles(); band_fin(); deck_supports(); band_cheeks();
   G(){ slot_block(); brackets();
    sw_rib(sw_cx-1.9, 3.5, sw_cx-1.9, sw_cx-0.2);
    sw_rib(sw_cx+sw_L-1.6, 3.5, sw_cx+sw_L+0.2, sw_cx+sw_L+1.9); }
  }
  lock_cuts(); G(){ slot_cut(); bracket_holes(); } deck_pilots(); cheek_pilots();
  // RJ45 window in the back wall
  Cc(pcb_cx0+pcb[0]-jack_px-jack_w/2-0.35, comp_d-0.01, pcb_z0+pcb[2]-0.3) cube([jack_w+0.7, wall+1.5, jack_h+1.8]);
  // node board screw (#4 x 1/2" flat-head)
  Cc(pcb_cx0+pcb[0]-hole_px, comp_d-hole_py, 1) cylinder(d=n4_pilot,h=pcb_z0,$fn=16);
  // hinge pin
  translate([-1,Do+hinge_off,Hb]) rotate([0,90,0]) cylinder(d=pin_d,h=Wo+2,$fn=16);
 }
}

// ================= deck: cover over the back compartment (modelled in place) =================
// Flush with the top of the body. Only the lid's latch tab and switch finger pass through it, and there is one
// hole over the horn screw (manual release). Print it top face down, in the chest's colour or black.
module deck(){
 difference(){
  Cc(0.25,0.25,deck_z) cube([bay_w-0.5,comp_d-0.5,deck_t]);
  G(){
   Cc(ax_cx-5.4,-1,deck_z-1) cube([17.2,5.3,deck_t+2]);            // round the slot block
   Cc(ax_cx-7,4.2,deck_z-1) cube([26,5.3,deck_t+2]);               // latch tab (swings forward a little as the lid opens)
   Cc(ax_cx-27.2,1.0,deck_z-1) cube([20.3,4.7,deck_t+2]);          // switch finger
   Cc(ax_cx,ax_cy,deck_z-1) cylinder(d=6,h=deck_t+2,$fn=24);       // horn screw / manual release
   Cc(ax_cx,ax_cy,deck_z-0.01) cylinder(r=arm_len+1.5,h=0.6);      // room over the arm
  }
  for(p=deck_scr) Cc(p[0],p[1],deck_z-0.01){ cylinder(d=n4_clear,h=deck_t+1,$fn=20); cylinder(d1=n4_clear,d2=n4_head,h=deck_t+0.02,$fn=28); }
 }
}
// ================= backplate: cover over the rubber band and the lid's tail =================
module backplate(){
 difference(){
  translate([Wo/2-bp_in-bp_ck, Do+bp_d, 0]) cube([2*(bp_in+bp_ck), bp_t, bp_h]);
  for(sx=[-1,1]) for(z=bp_scr_z) translate([Wo/2+sx*(bp_in+bp_ck/2), Do+bp_d-0.01, z]) rotate([-90,0,0]){
   cylinder(d=n4_clear,h=bp_t+1,$fn=20); translate([0,0,bp_t-1.7+0.02]) cylinder(d1=n4_clear,d2=n4_head,h=1.7,$fn=28); }
  // notch: the top of the lid's tail swings back 18 mm from the hinge as the lid opens
  translate([Wo/2-bp_in-0.3, Do+bp_d-1, bp_h-11.5]) cube([2*bp_in+0.6, bp_t+2, 12]);
  // strap lines to match the chest
  for(z=[15,30]) translate([Wo/2-bp_in-bp_ck-1, Do+bp_d+bp_t-0.5, z-0.6]) cube([2*(bp_in+bp_ck)+2,0.51,1.2]);
 }
}

// ================= lid (modelled closed, in place) =================
module lid_outer(){ hull(){ cube([Wo,Do,rim]); translate([lid_h-rim,lid_h-rim,lid_h-0.01]) cube([Wo-2*(lid_h-rim),Do-2*(lid_h-rim),0.01]); } }
module lid_inner(){ i2 = wall + (lid_h-top_t-rim+0.8);
 hull(){ translate([wall,wall,-0.01]) cube([Wo-2*wall,Do-2*wall,rim-0.8]); translate([i2,i2,lid_h-top_t-0.01]) cube([Wo-2*i2,Do-2*i2,0.01]); } }
module lid_local(){
 // lid frame: z = 0 on the body top
 difference(){
  union(){
   difference(){ lid_outer(); lid_inner();
    // strap lines on the flat top
    for(x=[0.27*Wo,0.73*Wo]) translate([x-0.7,-1,lid_h-0.4]) cube([1.4,Do+2,1]);
    difference(){ translate([14,14,lid_h-0.4]) cube([Wo-28,Do-28,1]); translate([15.2,15.2,lid_h-1]) cube([Wo-30.4,Do-30.4,3]); }
   }
   intersection(){ union(){ lid_outer(); translate([0,0,-20]) cube([Wo,Do,20.01]); } translate([0,0,-Hb]) G(){
    // latch tab with its window (the arm passes through), and the switch finger
    Cc(ax_cx-6.5,7,35.3) cube([25,2,lid_h+Hb-35.3]);
    Cc(sw_cx+0.5,2,finger_z) cube([sw_L-1,3.2,lid_h+Hb-finger_z]);
   } }
   // knuckles
   for(k=[kb1+kn_b+kn_gap, kb2-kn_gap-kn_l]) hull(){
    translate([k,Do+hinge_off,0]) rotate([0,90,0]) cylinder(r=hinge_r,h=kn_l);
    translate([k,Do-0.01,0]) cube([kn_l,0.02,rim]);
    translate([k,Do-4.2,9.8]) cube([kn_l,0.2,0.2]);
   }
   // tail: the rubber band pulls it down; it also carries the lid while printing
   translate([Wo/2-tail_w/2,Do,0]) rotate([90,0,90]) linear_extrude(tail_w)
    polygon([[0,0],[16,0],[16,6],[4,18],[-12,18],[0,6]]);
  }
  // window: its lower edge is a ramp, low where the arm swings in, full height under the locked arm
  translate([0,0,-Hb]) G() Cc(ax_cx-4,6.9,0) rotate([90,0,0]) translate([0,0,-2.2]) linear_extrude(2.2)
   polygon([[0,bar_z],[7.5,bar_z],[18,bar_z-1.1],[20,bar_z-1.1],[20,arm_z0+arm_t+0.6],[0,arm_z0+arm_t+0.6]]);
  // keyhole over the horn screw (manual release with a small Phillips screwdriver, clockwise)
  translate([CX(ax_cx),CY(ax_cy),-1]) cylinder(d=5,h=lid_h+2,$fn=24);
  translate([-1,Do+hinge_off,0]) rotate([0,90,0]) cylinder(d=pin_d,h=Wo+2,$fn=16);            // pin
  translate([Wo/2-tail_w/2-1,Do+11.5,3.8]) rotate([0,90,0]) cylinder(d=4.2,h=tail_w+2,$fn=20);  // band hole
  lid_inner_keep();
 }
}
// the inside of the lid stays hollow except where the tab and finger stand
module lid_inner_keep(){ }
module lid(deg=0){ translate([0,Do+hinge_off,Hb]) rotate([-deg,0,0]) translate([0,-Do-hinge_off,0]) lid_local(); }

// ================= latch arm (modelled locked, in place) =================
module arm_local(){
 // origin on the shaft axis, z = 0 at the arm underside (hub), pointing -y
 difference(){
  union(){
   cylinder(d=9.5,h=arm_t,$fn=40);
   translate([-arm_w/2,-arm_len,0]) cube([arm_w,arm_len,arm_t]);
  }
  translate([0,0,-1]) cylinder(d=spline_d,h=arm_t-0.8+1,$fn=30);
  translate([0,0,-1]) cylinder(d=2.3,h=arm_t+2,$fn=16);
 }
}
module arm(ang=0){ G() Cc(ax_cx,ax_cy,arm_z0) rotate([0,0,ang]) arm_local(); }

// ================= emblem cap =================
module emblem(kind=0){
 // z = 0 is the back of the cap (against the lock plate); the plug goes into the light cavity
 cylinder(d=plate_w-2,h=1.2,$fn=64);
 translate([0,0,-3]) difference(){ cylinder(d=em_cav-0.25,h=3.01,$fn=64); translate([0,0,-1]) cylinder(d=em_cav-3,h=5,$fn=64); }
 translate([0,0,1.19]) {
  if(kind==0){ difference(){ cylinder(d=20,h=0.9,$fn=64); translate([0,0,0.4]) cylinder(d=16.5,h=1,$fn=64); }
               cylinder(d=6,h=0.9,$fn=6); for(a=[0:60:300]) rotate(a) translate([5.6,0,0]) cylinder(d=2,h=0.9,$fn=12); }
  if(kind==1){ linear_extrude(1.0) polygon([for(i=[0:9]) let(r=(i%2==0)?10.5:4.4, a=90+i*36) [r*cos(a),r*sin(a)]]); }
  if(kind==2){ hull(){ linear_extrude(0.01) offset(r=2) square([13,13],center=true); translate([0,0,1.6]) linear_extrude(0.01) offset(r=1) square([6,6],center=true); } }
 }
}
module emblem_placed(kind=0){ translate([Wo/2,-plate_t,em_z]) rotate([90,0,0]) emblem(kind); }

// ================= bought parts (ghosts) =================
module servo_ghost(){
 Cc(ax_cx-sv_axis, ax_cy-sv_W/2, sv_z0){
  cube([sv_L,sv_W,sv_H]);
  translate([-sv_tab,0,sv_tab_z]) cube([sv_L+2*sv_tab,sv_W,sv_tab_t]);
  translate([sv_axis,sv_W/2,sv_H]) cylinder(d=11.6,h=sv_boss-sv_H);
  translate([sv_axis,sv_W/2,sv_H]) cylinder(d=4.6,h=sv_top-sv_H);
 }
}
module switch_ghost(){ Cc(sw_cx,sw_cy,sw_z0){ cube([sw_L,sw_W,sw_H]); translate([2,1.2,sw_H]) cube([sw_L-3,4,sw_op-sw_H-0.6]); } }
module pcb_ghost(){
 Cc(pcb_cx0, comp_d-pcb[1], pcb_z0) cube(pcb);
 Cc(pcb_cx0+pcb[0]-jack_px-jack_w/2, comp_d+jack_over-jack_len, pcb_z0+pcb[2]) cube([jack_w,jack_len,jack_h]);
 Cc(pcb_cx0, comp_d-pcb[1]+1, pcb_z0+pcb[2]) cube([7.5,17.5,10]);     // terminal block
}
module ghosts(){ G(){ servo_ghost(); switch_ghost(); } pcb_ghost(); }

// ================= output =================
if(part=="body") body();
else if(part=="lid") lid(0);
else if(part=="arm") arm(0);
else if(part=="emblem") emblem(emblem_kind);
else if(part=="body_print") body();
else if(part=="lid_print") translate([0,0,lid_h]) rotate([180,0,0]) translate([0,-Do,0]) lid_local();
else if(part=="arm_print") arm_local();
else if(part=="deck") deck();
else if(part=="backplate") backplate();
else if(part=="deck_print") translate([0,0,Hb]) rotate([180,0,0]) translate([-wall,-y_c0-comp_d,0]) deck();
else if(part=="backplate_print") rotate([-90,0,0]) translate([-Wo/2,-Do-bp_d-bp_t,0]) backplate();
else if(part=="emblem_print") translate([0,0,3]) emblem(emblem_kind);
else if(part=="assembly"){
 color("#7a5a3a") body();
 color("#8c6a46") lid(open_deg);
 color("#d8d8d8") arm(locked ? 0 : unlock_deg);
 color("#4a3826") deck();  color("#4a3826") backplate();
 color("#ffd070",0.8) emblem_placed(emblem_kind);
 color("#3060c0",0.6) ghosts();
}
else if(part=="section"){   // cut through the latch, looking from the left
 difference(){ union(){ color("#7a5a3a") body(); color("#8c6a46") lid(open_deg); color("#d8d8d8") arm(locked?0:unlock_deg); color("#3060c0") ghosts(); }
  translate([-1,-50,-1]) cube([CX(ax_cx)+1,300,200]); }
}
else if(part=="check"){
 // every line must be empty. Bought parts are lifted 0.02 so faces that only touch do not count; the lid is
 // tested 0.02 up and back for the same reason, and the arm 0.25 up (it is squeezed 0.2 by the lid tab on purpose).
 e=0.02;
 intersection(){ body(); translate([0,0,e]) lid(0); }
 intersection(){ body(); arm(0); }
 intersection(){ lid(0); translate([0,0,0.25]) arm(0); }
 intersection(){ lid(0); arm(unlock_deg); }
 for(a=[20,40,60,80]) intersection(){ union(){ body(); G() translate([0,0,e]) servo_ghost(); } arm(a); }
 for(d=[8,20,35,50,70,100]) intersection(){ translate([0,0,e]) lid(d); union(){ body(); arm(unlock_deg); translate([0,0,e]) ghosts(); } }
 intersection(){ translate([0,0,e]) ghosts(); union(){ body(); translate([0,0,e]) lid(0); } }
 intersection(){ G() translate([0,0,e]) servo_ghost(); union(){ arm(0); arm(unlock_deg); } }
 // covers: against the body, the bought parts, the arm through its swing and the lid through its swing
 intersection(){ translate([0,0,e]) deck(); union(){ body(); ghosts(); } }
 for(a=[0,30,60,90]) intersection(){ deck(); translate([0,0,0.25]) arm(a); }
 for(d=[0,4,8,14,20,35,50,70,100,103]) intersection(){ translate([0,0,e]) lid(d); union(){ deck(); translate([0,e,0]) backplate(); } }
 intersection(){ translate([0,e,0]) backplate(); body(); }
}