// Hoard Stand: landscape stand for the station iPad (iPad Air 4th gen, A2316), Dragonglow style. mm.
// This file is the FUNCTIONAL frame: sizes, the iPad slot, the backrest, the crystal sockets and the
// keep-out volumes. All decoration lives in ipad_stand_art.scad (art_body / art_cluster) and is
// clipped here to its envelope and then cut by the slot, so the art pass cannot break the fit.
// Parts (-D 'part="..."'): body (stone), cluster (-D idx=0|1|2, one per crystal filament),
// pin (x3, any colour), assembly (preview with a ghost iPad), check (must render EMPTY).
// Print: body upright on its flat base, no supports; clusters root-down; pins lying flat.
$fn=48;
part="assembly";
idx=0;

// ---- the iPad -------------------------------------------------------------------------------
dev=[247.6,178.5,6.1];   // A2316 W x H x T (landscape)
case_t=4;                // case allowance: 0 = bare iPad only; 4 fits most slim cases
clear=0.6;
bezel=10.2;              // screen starts ~10 mm in from each edge
tilt=18;                 // lean back from vertical
seat_z=50;               // height of the iPad's bottom edge above the table
bp=dev[2]+case_t+clear;  // back plane of the slot (device frame y); the backrest touches it
lip_h=7;                 // front lip, device frame (stays inside the bezel)
lip_t=8;

// ---- stand ----------------------------------------------------------------------------------
foot=[[-102,-58],[102,95]];    // footprint (fits a 220 x 220 bed)
seat_w=184;                    // seat/lip length; the iPad overhangs 32 mm each side (USB-C, speakers)
rest_w=70;                     // backrest width
rest_h=100;                    // backrest contact height (device frame; must stay above the iPad's centre, 89)
art_top=75;                    // decoration is clipped to this height (lower = faster print; the iPad hides the rear)

// ---- crystals (Dragonberry left, Rainbow Fizz centre = the egg, Starfrost right) ------------
sockets=[[-74,-34],[0,-40],[74,-34]];
socket_top=22;
crystal_cap=seat_z+6;          // nothing in front of the iPad may rise above this (keeps the screen clear)
crystal_r=24;                  // cluster envelope radius around its socket
peg_d=5.6; peg_flat=2.0; fit=0.3; pin_in_root=4; pin_len=pin_in_root+10.5;  // same pins as the plinth
colors=["#7a3fc8","#ff9fd6","#9fd8ff"];   // preview only

include <ipad_stand_art.scad>

// device frame: x along the iPad, y = thickness (0 = screen face), z = up the iPad
module devframe(){translate([0,0,seat_z]) rotate([-tilt,0,0]) children();}
module dshape(d,flat,h){intersection(){cylinder(d=d,h=h,$fn=24); translate([-d,-d,0]) cube([d+flat,2*d,h]);}}

// everything the iPad (+case) occupies, open to both sides and the top
module keepout(){devframe() translate([-dev[0]/2-80,-clear,0]) cube([dev[0]+160,bp+clear,dev[1]+80]);}
// sightline: in front of the screen face nothing rises above crystal_cap
module sightline(){difference(){translate([-150,-150,crystal_cap]) cube([300,300,300]);
 devframe() translate([-200,-lip_t-clear-1,-100]) cube([400,500,500]);}}

module base_func(){
 // slab
 translate([foot[0][0],foot[0][1],0]) cube([foot[1][0]-foot[0][0],foot[1][1]-foot[0][1],12]);
 // seat block + front lip, carried down to the slab
 hull(){
  devframe() translate([-seat_w/2,-clear-lip_t,-12]) cube([seat_w,bp+clear+lip_t+14,12+lip_h]);
  translate([-seat_w/2,-24,0]) cube([seat_w,62,1]);
 }
 // backrest, carried down to the slab
 hull(){
  devframe() translate([-rest_w/2,bp,0]) cube([rest_w,14,rest_h]);
  devframe() translate([-rest_w/2,bp,-10]) cube([rest_w,44,10]);
  translate([-rest_w/2-6,10,0]) cube([rest_w+12,foot[1][1]-14,1]);
 }
 // socket bosses
 for(s=sockets) translate([s[0],s[1],0]) cylinder(d1=20,d2=15,h=socket_top,$fn=7);
}
// the art may add stone anywhere in here; the slot/sightline/sockets are cut afterwards
module body_envelope(){translate([foot[0][0],foot[0][1],0]) cube([foot[1][0]-foot[0][0],foot[1][1]-foot[0][1],art_top]);}

module body(){
 render(convexity=8) difference(){
  union(){ base_func(); intersection(){ body_envelope(); art_body(); } }
  keepout();
  sightline();
  for(s=sockets) translate([s[0],s[1],socket_top-11]) dshape(peg_d+2*fit,peg_flat+fit,12);
  translate([0,0,-50]) cube([400,400,100],center=true);   // flat bottom
 }
}

// a cluster in its own frame: root on z=0, centred on its pin
module cluster(i){
 difference(){
  intersection(){
   translate([0,0,0]) cylinder(r=crystal_r,h=crystal_cap-socket_top,$fn=64);
   union(){ cylinder(d=14,h=3,$fn=7); art_cluster(i); }
  }
  translate([0,0,-.01]) dshape(peg_d+2*fit,peg_flat+fit,pin_in_root+.2);
 }
}
module installed(i){translate([sockets[i][0],sockets[i][1],socket_top]) difference(){cluster(i);
 // as installed it must also stay clear of the iPad
 translate([-sockets[i][0],-sockets[i][1],-socket_top]) keepout();}}
module pin(){translate([0,0,peg_flat]) rotate([0,90,0]) dshape(peg_d,peg_flat,pin_len);}
module ghost_ipad(){devframe() translate([-dev[0]/2,case_t/2,0]) cube([dev[0],dev[2],dev[1]]);}

if(part=="body") body();
else if(part=="cluster") cluster(idx);
else if(part=="pin") pin();
else if(part=="assembly"){ color("#8a857c") body();
 for(i=[0:2]) color(colors[i]) translate([sockets[i][0],sockets[i][1],socket_top]) cluster(i);
 %ghost_ipad(); }
else if(part=="check"){   // must be empty: placed clusters vs iPad and sightline
 for(i=[0:2]) intersection(){ translate([sockets[i][0],sockets[i][1],socket_top]) cluster(i);
  union(){ keepout(); sightline(); } } }
