// Wakestone capstone. Art geometry only; the owner frame cuts the hollow,
// rebate and vertically swept crystal clearance. All dimensions are mm.
// Installed +z is UP. Print with z_top on the bed.
// Round 2: V-carved ritual rings, runes and cracks; seven-sided side stones.
// The owner frame protects the landing circle and limits cuts to 1.2 mm.

// Hand-cut perimeter, counterclockwise: long broken faces rather than a
// rounded rectangle. The frame trims the few corners near crystal roots.
stone_outline = [
    [-40,-30], [-29,-39], [-10,-42], [-7,-40.2], [-4,-41.45],
    [12,-40], [31,-35], [40,-23], [41.2,-15.8], [39.8,-13.5],
    [42.2,-9.8], [43,-5], [40,14], [30,34], [11,41],
    [-14,39], [-32,31], [-41,12], [-43,-13]
];

// Triangulate the planar side panels explicitly to avoid OpenSCAD 2021's
// floating-point "nonplanar faces" fallback on homothetic quads.
module stone_course(z0, s0, z1, s1) {
    n = len(stone_outline);
    polyhedron(
        points = concat(
            [for (p=stone_outline) [p[0]*s0,p[1]*s0,z0]],
            [for (p=stone_outline) [p[0]*s1,p[1]*s1,z1]]),
        faces = concat(
            [[for (i=[n-1:-1:0]) i]],
            [[for (i=[0:n-1]) n+i]],
            [for (i=[0:n-1]) [i,(i+1)%n,(i+1)%n+n]],
            [for (i=[0:n-1]) [i,(i+1)%n+n,i+n]]),
        convexity=4);
}

module art_platform() {
    difference() {
        union() {
            stone_course(lid_t, .91, 4.8, .96);
            stone_course(4.8, .96, 6.8, .919);
            stone_course(6.8, .919, 8.4, .97);
            stone_course(8.4, .97, z_top, 1.0);
            // Embedded heptagonal boulders, clipped to the inherited outline.
            // Their upper taper is only 2 mm over 3.5 mm of height.
            intersection() {
                linear_extrude(z_top) polygon(stone_outline);
                for (rock=[[-26,-32,8,9],[-6,-35,7,24],[19,-31,8,3],
                           [36,-13,7,15],[33,12,8,31],[16,31,8,8],
                           [-10,33,7,25],[-31,19,8,4],[-37,-8,7,18]])
                    translate([rock[0],rock[1],0]) rotate([0,0,rock[3]]) {
                        translate([0,0,2]) cylinder(h=6.5,r1=rock[2]-3,r2=rock[2],$fn=7);
                        translate([0,0,8.5]) cylinder(h=3.5,r1=rock[2],r2=rock[2]-2,$fn=7);
                    }
            }
        }
        // Full-height triangular chisel clefts: open ends, no blind ceilings.
        for (cut=[[-21,-38,8],[25,-35,19],[40,3,101],[-24,35,171],[-42,2,-86]])
            translate([cut[0],cut[1],1.9]) rotate([0,0,cut[2]])
                linear_extrude(z_top) polygon([[-2.2,-5],[2.2,-5],[0,2.5]]);
    }
}

// A triangular groove with no horizontal floor. At z_top its width is
// 2.2 mm and its nominal depth is 1.2 mm (1.199 mm avoids a coincident
// apex at the frame's clipping plane): internal roofs exceed 45 deg.
// Extend the top slightly above the frame to avoid coplanar subtraction.
module chisel(a,b) {
    d=b-a;
    translate([a[0],a[1],0]) rotate([0,0,atan2(d[1],d[0])])
        polyhedron(points=[[0,-1.1,z_top],[0,1.1,z_top],[0,0,z_top-1.199],
                           [norm(d),-1.1,z_top],[norm(d),1.1,z_top],[norm(d),0,z_top-1.199],
                           [0,-1.1,z_top+.2],[0,1.1,z_top+.2],
                           [norm(d),-1.1,z_top+.2],[norm(d),1.1,z_top+.2]],
                   faces=[[0,2,1],[3,4,5],[0,3,5,2],[1,2,5,4],
                          [0,6,8,3],[1,4,9,7],[0,1,7,6],[3,8,9,4],[6,7,9,8]],convexity=4);
}
module carved_ring(r) {
    translate([target[0],target[1],0]) rotate_extrude($fn=96)
        polygon([[r-1.1,z_top],[r,z_top-1.199],[r+1.1,z_top],
                 [r+1.1,z_top+.2],[r-1.1,z_top+.2]]);
}
module art_engrave() {
    carved_ring(22.4);
    carved_ring(30.4);
    // Ten bold alternating radial / slanted rune notches in the annulus.
    // Separate strokes avoid coincident groove tips in OpenSCAD 2021 CGAL.
    for (ang=[0:36:324]) translate([target[0],target[1],0]) rotate([0,0,ang]) {
        chisel([25,-(ang%72==0 ? 0 : 1.6)],[28,ang%72==0 ? 0 : 1.6]);
    }
    // Uneven, branching weather cracks enter from several slab edges.
    for (path=[[[43,8],[35,5],[32,8]],
               [[15,43],[13,35],[18,29]],
               [[-43,-18],[-40,-17],[-40,-22]],
               [[-3,-44],[-1,-40],[3,-40]],
               [[-31,34],[-26,29],[-27,25]]])
        for (i=[0:len(path)-2]) chisel(path[i],path[i+1]);
    chisel([35,5],[37,0]);
    chisel([13,35],[8,33]);
}

