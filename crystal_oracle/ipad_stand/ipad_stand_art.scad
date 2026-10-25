// Butterscotch's hoard cave. Art only; all functional cuts live in ipad_stand.scad.
// Seven-sided stone and six-sided quartz echo the companion crystal plinth.
// All rising profiles grow <= 1 mm sideways per 1 mm up, or taper inward.

module cave_rock(x,y,z,rx,ry,h,turn=0) {
    translate([x,y,z]) rotate([0,0,turn]) hull() {
        scale([rx,ry,1]) cylinder(r=1,h=.6,$fn=7);
        translate([rx*.08,ry*.04,h*.66])
            scale([rx*.78,ry*.76,1]) cylinder(r=1,h=.6,$fn=7);
        translate([-rx*.12,ry*.08,h-1])
            scale([rx*.08,ry*.10,1]) cylinder(r=1,h=1,$fn=7);
    }
}

// Offset hexagonal prism with a long quartz termination. A shear, rather than
// rotating the foot, keeps its entire hexagonal sole planted in the root.
module quartz(x,y,z,h,w,dx=0,dy=0,turn=0) {
    translate([x,y,z]) rotate([0,0,turn])
    multmatrix([[1,0,dx/h,0],[0,1,dy/h,0],[0,0,1,0],[0,0,0,1]]) union() {
        cylinder(d1=w*1.08,d2=w,h=h*.66,$fn=6);
        translate([0,0,h*.66]) cylinder(d1=w,d2=0,h=h*.34,$fn=6);
    }
}

module star_mark(r=4,h=1.6) {
    linear_extrude(h) polygon([for(k=[0:9])
        let(a=90+k*36, rr=k%2==0?r:r*.47) [rr*cos(a),rr*sin(a)]]);
}
module hoard_coin(x,y,z,r=8,turn=0) {
    translate([x,y,z]) rotate([0,0,turn]) {
        cylinder(r=r,h=2.9,$fn=24);
        // Broad incuse border, raised star: readable in stone-grey filament.
        translate([0,0,2.8]) difference() {
            cylinder(r=r,h=1.4,$fn=24);
            translate([0,0,-.1]) cylinder(r=r-1.5,h=1.7,$fn=24);
        }
        translate([0,0,2.8]) star_mark(r*.52,1.6);
    }
}
module wrapped_sweet(x,y,z,turn=0) {
    translate([x,y,z]) rotate([0,0,turn]) {
        // Flat-backed, faceted boiled sweet and chunky fan-fold wrappers.
        scale([1.3,1,1]) cylinder(d1=10,d2=7,h=7,$fn=8);
        for(s=[-1,1]) hull() {
            translate([s*5,0,0]) cylinder(d=4,h=3,$fn=4);
            translate([s*10,0,0]) scale([.65,1,1]) cylinder(d=9,h=2,$fn=4);
        }
        translate([-1,-3,6]) cube([2,6,1.4]);
    }
}
module spiral_sweet(x,y,z,r=10) {
    translate([x,y,z]) difference() {
        cylinder(r1=r-2,r2=r,h=3,$fn=32);
        // A continuous chunky groove; 1.5 mm wide and 1.5 mm deep.
        for(k=[0:35]) let(a=k*16,b=(k+1)*16,
            ra=1.8+k*.18,rb=1.8+(k+1)*.18)
            hull() {
                translate([ra*cos(a),ra*sin(a),1.5]) cylinder(d=1.5,h=2,$fn=8);
                translate([rb*cos(b),rb*sin(b),1.5]) cylinder(d=1.5,h=2,$fn=8);
            }
    }
}

// Full-width stone shelves give every relief a supported underside.
module treasure_bed(x,y,z,rx,ry,turn=0) {
    translate([x,y,10]) rotate([0,0,turn]) hull() {
        scale([rx+3,ry+3,1]) cylinder(r=1,h=.5,$fn=7);
        translate([0,0,z-10]) scale([rx,ry,1]) cylinder(r=1,h=.6,$fn=7);
    }
}
module hoard_landscape() {
    // Broken bedrock terraces wrap the rectangular functional slab.
    for(k=[0:7]) cave_rock(-94+k*27,-47,7,19,17,11+(k%3)*1.5,k*19);
    for(s=[-1,1]) {
        for(k=[0:4]) cave_rock(s*94,-12+k*24,7,17,20,18+(k%3)*7,k*31+s*8);
        cave_rock(s*74,30,10,26,29,36,s*17);
        cave_rock(s*62,65,9,30,30,62,s*29);
        cave_rock(s*38,77,8,27,22,131+(s+1)*9,s*13);
    }
    // Three irregular, tapering spires bury the rear slab in a cave outcrop.
    cave_rock(-30,66,10,28,28,164,11);
    cave_rock(8,73,10,31,25,152,-14);
    cave_rock(38,71,10,25,26,133,23);
    for(k=[0:4]) cave_rock(-42+k*21,92,8,19,16,44+(k%3)*15,k*27);

    // A faceted escarpment dresses the otherwise planar front seat wall.
    ridge_x=[-82,-64,-43,-22,-2,19,40,61,82];
    ridge_h=[44,51,39,48,43,52,38,47,42];
    for(k=[0:8]) cave_rock(ridge_x[k],-5-(k%3)*2,10,19,21,ridge_h[k],k*23);

    // Low socket skirts meet the crystal bosses but finish below z=22.
    for(i=[0:2]) translate([sockets[i][0],sockets[i][1],10])
        rotate([0,0,12+i*19]) cylinder(d1=37,d2=19,h=11,$fn=7);

    for(p=[[-37,-47,18,11,11,-12],[37,-48,18,10,10,18],
           [-48,-11,26,10,10,30],[52,1,27,11,11,-23],
           [-38,-26,18,15,8,-18],[37,-25,18,15,8,21],
           [-77,10,30,15,8,28],[81,16,28,15,8,-26],
           [-64,8,28,14,14,0],[67,10,28,13,13,0]])
        treasure_bed(p[0],p[1],p[2],p[3],p[4],p[5]);
    // A deliberate candy-and-coin spill in the gaps between the crystals.
    hoard_coin(-37,-47,18,9,-12);
    hoard_coin(37,-48,18,8,18);
    hoard_coin(-48,-11,26,8,30);
    hoard_coin(52,1,27,9,-23);
    wrapped_sweet(-38,-26,18,-18);
    wrapped_sweet(37,-25,18,21);
    wrapped_sweet(-77,10,30,28);
    wrapped_sweet(81,16,28,-26);
    // Supported ledges carry two large spiral boiled sweets.
    cave_rock(-64,8,11,17,17,19,4);
    cave_rock(67,10,11,18,17,19,19);
    spiral_sweet(-64,8,28,11);
    spiral_sweet(67,10,28,10);
    for(p=[[-22,-19,18],[22,-18,18],[-91,-12,16],[91,-10,17]])
        treasure_bed(p[0],p[1],p[2],8,8);
    // Stone gumdrops and loose gem points among the treasure.
    for(p=[[-22,-19,18],[22,-18,18],[-91,-12,16],[91,-10,17]])
        translate(p) cylinder(d1=12,d2=5,h=8,$fn=7);
    treasure_bed(-55,29,21,9,9);
    treasure_bed(58,35,21,10,10);
    quartz(-55,29,21,21,12,-3,1,15);
    quartz(58,35,21,25,13,4,1,0);
}

// Protect the actual removable crystal envelopes above their seating plane.
// The main file still cuts the keyed sockets in the stone underneath.
module art_body() {
    difference() {
        hoard_landscape();
        for(s=sockets) translate([s[0],s[1],socket_top])
            cylinder(r=crystal_r+.6,h=crystal_cap-socket_top+1,$fn=64);
    }
}

// Faceted egg cup, solid floor around the D-hole. The inner cone opens upward;
// the outside grows 11 mm over 16 mm. The rim alternates peaks and cracks.
module hatched_egg() {
    n=14;
    rim=[21,26,20,24,19,27,21,25,19,24,20,26,19,24];
    difference() {
        polyhedron(
            points=concat(
                [for(k=[0:n-1]) [9*cos(k*360/n),9*sin(k*360/n),0]],
                [for(k=[0:n-1]) [20*cos(k*360/n),20*sin(k*360/n),16]],
                [for(k=[0:n-1]) [20*cos(k*360/n),20*sin(k*360/n),rim[k]]],
                [[0,0,0],[0,0,27]]),
            faces=concat(
                [for(k=[0:n-1]) [3*n,(k+1)%n,k]],
                [for(j=[0:1],k=[0:n-1]) [j*n+k,j*n+(k+1)%n,(j+1)*n+(k+1)%n]],
                [for(j=[0:1],k=[0:n-1]) [j*n+k,(j+1)*n+(k+1)%n,(j+1)*n+k]],
                [for(k=[0:n-1]) [3*n+1,2*n+k,2*n+(k+1)%n]]),convexity=6);
        // Floor is at z=6, leaving 1.8 mm above the automatically cut pin hole.
        translate([0,0,6]) cylinder(r1=4,r2=16,h=10,$fn=14);
        translate([0,0,15.99]) cylinder(r=16,h=30,$fn=14);
    }
}
module art_cluster(i) {
    if(i==0) {
        // Dragonberry: a broad, asymmetric five-point quartz crown.
        cylinder(d1=33,d2=24,h=5,$fn=7);
        quartz(-3,1,3,30,13,-4,1,10);
        quartz(6,1,3,24,11,6,1,25);
        quartz(-7,-4,2,19,10,-6,-3,5);
        quartz(3,-6,2,16,9,2,-5,20);
        quartz(0,7,2,20,9,0,5,0);
    } else if(i==1) {
        hatched_egg();
        // Shards are rooted in the thick shell wall; the open centre stays visible.
        quartz(-3.5,2,0,33,8,-11.5,2,0);
        quartz(4,3,0,30,7,8,8,15);
        quartz(2,-4,0,27,7,5,-11,0);
    } else {
        // Starfrost: a slender high spear with a low, outward sweeping fan.
        cylinder(d1=32,d2=22,h=5,$fn=7);
        quartz(0,1,3,30.5,11,2,2,30);
        quartz(6,0,2,22,9,8,0,0);
        quartz(-6,-1,2,18,9,-7,-1,20);
        quartz(2,-6,2,14,8,3,-6,30);
    }
}
