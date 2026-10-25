// Raised NFC platform for the v7 plinth: replaces the flush 2 mm nfc_lid. mm.
// Owner, 2026-10-24: more room over the PN532, and tags read better a little further from the reader.
// The platform plugs into the v7 lid rebate exactly like the old lid (same footprint and 2 mm depth,
// so no change to the body), stands `rise` above the plinth top and is hollow underneath.
// Same split as the Hoard Stand: this file = the functional frame (Claude); the stone look lives in
// nfc_platform_art.scad (art_platform) and is clipped here to the envelope, then the plug and the hollow
// are cut back, so an art pass cannot break the fit.
// Parts (-D 'part="..."'): platform, assembly (preview), check (must render EMPTY).
// Print it UPSIDE DOWN (flat top face on the bed): the hollow is then an open cup, no supports.
use <crystal_plinth_v7.scad>   // body(), installed(i)
$fn=48;
part="platform";
// copied from crystal_plinth_v7.scad (keep in step)
top_z=47; lid_t=2; lid_ledge=2; lid_fit=0.15;
nfc=[43.5,41.2]; nfc_plug=17; pocket=[nfc[0]+nfc_plug,nfc[1]+nfc_plug];
n_groups=11;
// platform
rise=10;                                   // top face this far above the plinth top (z=47 -> 57)
plug=[pocket[0]+2*lid_ledge-2*lid_fit,pocket[1]+2*lid_ledge-2*lid_fit];  // = old lid, 64.2 x 61.9
wall=2.2;                                  // plug wall, bears on the 2 mm ledge
top_t=2;                                   // top plate over the hollow
z_top=lid_t+rise;                          // local frame: z=0 = ledge (world 45), z=2 = plinth top
target=[-nfc_plug/2,-nfc_plug/2];          // reader centre (plugs push it to -x/-y in the pocket)
target_r=20;                               // keep this circle of the top face flat: crystals sit here
clear=1.5;                                 // gap to the crystal clusters (and the platform lifts straight up)
skirt_r=54;                                // stay on the plinth's flat top (r 56); the cluster keep-out trims it
include <nfc_platform_art.scad>

module plug_footprint(h){translate([-plug[0]/2,-plug[1]/2,0]) cube([plug[0],plug[1],h]);}
module hollow(){translate([-plug[0]/2+wall,-plug[1]/2+wall,-.1]) cube([plug[0]-2*wall,plug[1]-2*wall,z_top-top_t+.1]);}
// installed clusters swept straight up (local frame), grown by `clear`
module keepout(){for(i=[0:n_groups-1]) translate([0,0,-50]) linear_extrude(200)
 offset(delta=clear) projection() installed(i);}
// the art may add stone anywhere in here: above the plinth top, under the top plane, clear of crystals
module envelope(){difference(){translate([0,0,lid_t]) cylinder(r=skirt_r,h=rise,$fn=96);
 translate([0,0,-(top_z-lid_t)]) keepout();}}
module platform_func(){union(){plug_footprint(z_top);}}
// art_engrave() may cut the top face only: the top 1.2 mm, outside the crystal landing circle
module engrave_zone(){difference(){translate([0,0,z_top-1.2]) cylinder(r=skirt_r,h=2,$fn=96);
 translate([target[0],target[1],0]) cylinder(r=target_r,h=z_top+5,$fn=96);}}
module platform(){render(convexity=6) difference(){
 union(){ platform_func(); intersection(){ envelope(); art_platform(); } }
 hollow();
 intersection(){ engrave_zone(); art_engrave(); }
}}
module placed(){translate([0,0,top_z-lid_t]) platform();}
if(part=="platform") platform();
else if(part=="assembly"){ color("#8a857c") body(); color("#6f6a62") placed();
 for(i=[0:n_groups-1]) color("#b48ce6",.85) installed(i);}
// must be empty: platform (lifted 0.01 off its seat) against the body and the clusters
else if(part=="check"){ intersection(){ translate([0,0,.01]) placed(); union(){ body();
 for(i=[0:n_groups-1]) installed(i);}}}
