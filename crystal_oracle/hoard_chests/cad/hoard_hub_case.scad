// Case for the Hoard Hub board (124 x 112 mm, see ../pcb/PCB-NOTES.md). mm.
// Parts (-D 'part="..."'): base, lid, plunger (drops through the lid onto the TEST button), assembly. Print both flat as modelled (lid: flip it, top on the bed).
// The board sits on four posts and is held by four #4 x 1/2" FLAT-HEAD screws (the ones bought for the plinth cover) through its corner holes.
// The lid is a push fit; the whole left side (USB, 5 V jack, screw terminal) stays open.
$fn=40;
part="assembly";

bw=124; bd=112; bt=1.6;            // board
holes=[[4,4],[120,4],[4,108],[120,108]];
clr=0.4; wall=2.4; floor_t=2.4;
post_h=10;                         // posts tall enough for a #4 x 1/2" screw (11.1 mm into the plastic)
z_b = floor_t+post_h;              // underside of the board
z_top = z_b+bt;                    // top of the board
jack_w=15.3; jack_h=13.5;          // RJ45 above the board
inner_h=32;                        // tallest part: the ESP32 on its sockets / the 1000 uF capacitor
H = z_top+inner_h;
jx=[16,35,54,73,92];               // jack centres on the top (y=112) and bottom (y=0) edges
jy=[18,37,56,75,94];               // jack centres on the right edge (x=124)
test_btn=[13.4,75.2];              // TEST button centre on the board (SW1); [0,0] = no hole in the lid
btn_top=5.0;                       // top of the 6 mm tact switch above the board

module base(){
 difference(){
  translate([-clr-wall,-clr-wall,0]) cube([bw+2*(clr+wall),bd+2*(clr+wall),H]);
  translate([-clr,-clr,floor_t]) cube([bw+2*clr,bd+2*clr,H]);
  // jack openings
  for(x=jx) for(y=[-clr-wall-1,bd+clr-1]) translate([x-jack_w/2-0.5,y,z_top-0.4]) cube([jack_w+1,wall+2,jack_h+1.2]);
  for(y=jy) translate([bw+clr-1,y-jack_w/2-0.5,z_top-0.4]) cube([wall+2,jack_w+1,jack_h+1.2]);
  // open left side above the board (USB, barrel jack, screw terminal)
  translate([-clr-wall-1,8,z_b-1]) cube([wall+2,bd-16,H]);
  // tier marks over each jack group
  for(i=[0:4]) translate([jx[i],bd+clr+wall-0.5,z_top+jack_h+5]) rotate([90,0,180]) linear_extrude(1) text(str(i+1),size=6,halign="center",font="Liberation Sans:style=Bold");
  for(i=[0:4]) translate([jx[i],-clr-wall+0.5,z_top+jack_h+5]) rotate([90,0,0]) linear_extrude(1) text(str(i+6),size=6,halign="center",font="Liberation Sans:style=Bold");
  for(i=[0:4]) translate([bw+clr+wall-0.5,jy[i],z_top+jack_h+5]) rotate([90,0,90]) linear_extrude(1) text(str(i+11),size=6,halign="center",font="Liberation Sans:style=Bold");
 }
 for(h=holes) translate([h[0],h[1],0]) difference(){ cylinder(d=8,h=z_b); translate([0,0,1]) cylinder(d=2.5,h=z_b,$fn=16); }
}
module lid(){
 // z = 0 on top of the base walls
 difference(){
  union(){
   translate([-clr-wall,-clr-wall,0]) cube([bw+2*(clr+wall),bd+2*(clr+wall),2]);
   difference(){ translate([-clr+0.2,-clr+0.2,-4]) cube([bw+2*clr-0.4,bd+2*clr-0.4,4.01]);
                 translate([-clr+1.8,-clr+1.8,-5]) cube([bw+2*clr-3.6,bd+2*clr-3.6,6]); }
  }
  translate([bw/2,bd/2+10,1.4]) linear_extrude(1) text("HOARD HUB",size=11,halign="center",valign="center",font="Liberation Sans:style=Bold");
  translate([bw/2,bd/2-8,1.4]) linear_extrude(1) text("1-5 SILVER  6-10 GOLD  11-15 GEM",size=4.6,halign="center",valign="center",font="Liberation Sans:style=Bold");
  translate([bw/2,bd/2-20,1.4]) linear_extrude(1) text("NOT ETHERNET  -  5 V DC ONLY",size=4.6,halign="center",valign="center",font="Liberation Sans:style=Bold");
  if(test_btn!=[0,0]) translate([test_btn[0],test_btn[1],-6]) cylinder(d=8.4,h=10);
  // vent slots
  for(i=[0:5]) translate([20+i*16,18,-1]) hull(){ cylinder(d=2.4,h=4); translate([0,10,0]) cylinder(d=2.4,h=4); }
 }
}
module plunger(){ // stands on the button, flange keeps it in the lid
 L = inner_h - btn_top + 2 + 1.5;
 cylinder(d=7.6,h=L); translate([0,0,L-2-1.5-6]) cylinder(d=11,h=1.6); }
if(part=="base") base();
else if(part=="plunger") translate([0,0,inner_h-btn_top+3.5]) rotate([180,0,0]) plunger();
else if(part=="lid") lid();
else if(part=="lid_print") translate([0,0,2]) rotate([180,0,0]) lid();
else { color("#6b6b70") base(); color("#8a8a90",0.6) translate([0,0,H]) lid();
       color("#1f6b3a") translate([0,0,z_b]) cube([bw,bd,bt]); }
