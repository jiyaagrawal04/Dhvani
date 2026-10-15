// DHVANI payload pod v2 — side-scan sonar hull section
// Open in OpenSCAD, pick PART, press F6, then File > Export > Export as STL.
// PART = "lower" | "upper" | "saddle" | "assembled" (view only, don't print)
PART = "assembled";
PARTS_H = 80;                  // tallest part height at the side edges (wires folded down)

// ---------------- measurements (mm, real sizes) ----------------
nuc  = [140, 76];              // Nucleo footprint
bb   = [175, 75];              // breadboard
bat  = [30, 60];               // 9 V battery lying flat, rotated (30 along the hull, 60 across)
ina  = [30, 28];               // INA219
tds  = [40, 50];               // TDS module, rotated
tds_hole = 14;  temp_hole = 8; dial_hole = 7.5;

// ---------------- settings ----------------
clr = 1.5;  gap = 6;  t = 3;  fn = 128;
P = function(v) [v[0] + 2*clr, v[1] + 2*clr];
nP = P(nuc); bP = P(bb); aP = P(bat); iP = P(ina); dP = P(tds);

W   = gap + nP[1] + gap + bP[1] + gap;
L   = max(gap + nP[0] + gap + aP[0] + gap + iP[0] + gap,
          gap + bP[0] + gap + dP[0] + gap);
Ri  = ceil(sqrt(pow(W/2, 2) + pow(PARTS_H/2, 2))) + 1;
Ro  = Ri + t;   Lo = L + 2*t;
zf  = -PARTS_H/2;   zb = zf - t;
gz  = zf + 22;                                   // height of the rear cable glands
echo(str("Pod: length ", Lo, " (+2.5 connector collars) · diameter ", 2*Ro, " (+5 rings) mm"));

x0 = t + gap;  yA = -W/2 + gap;  yB = yA + nP[1] + gap;
nX = x0;  aX = nX + nP[0] + gap;  iX = aX + aP[0] + gap;
bX = x0;  dX = bX + bP[0] + gap;
nC = [nX + nP[0]/2, yA + nP[1]/2];
bC = [bX + bP[0]/2, yB + bP[1]/2];

module xcyl(r, x1, x2, f = fn) translate([x1, 0, 0]) rotate([0, 90, 0]) cylinder(r = r, h = x2 - x1, $fn = f);
module slab(z1, z2) translate([-10, -Ro - 20, z1]) cube([Lo + 20, 2*Ro + 40, z2 - z1]);
// prism along X from a 2D polygon given in (y, z)
module yzprism(pts, x1, x2) translate([x1, 0, 0]) rotate([90, 0, 90]) linear_extrude(x2 - x1) polygon(pts);

// ---------------- sonar details ----------------
ring_w = 10;  ring_h = 2.5;                      // hull joining rings
ringX  = [t, Lo - t - ring_w];
strip_a = [6, 20];  strip_x = [40, Lo - 40];     // transducer strips: angle band below horizontal, length

module rings() for (x = ringX) intersection() {
    difference() { xcyl(Ro + ring_h, x, x + ring_w); xcyl(Ro - 1, x - 1, x + ring_w + 1); }
    slab(zb, Ro + 10);
}
module ring_bolts() for (x = ringX, a = [15 : 30 : 345])
    if ((Ro + ring_h) * cos(a) > zb + 4)
        rotate([a, 0, 0]) translate([x + ring_w / 2, 0, Ro + ring_h - 0.5]) cylinder(d = 5.5, h = 1.8, $fn = 6);

module strips() for (s = [1, -1]) {
    R2 = Ro + 30;
    difference() {
        intersection() {
            difference() { xcyl(Ro + 3, strip_x[0], strip_x[1]); xcyl(Ro - 1, strip_x[0] - 1, strip_x[1] + 1); }
            yzprism([[0, 0], [s * R2 * cos(strip_a[0]), -R2 * sin(strip_a[0])],
                     [s * R2 * cos(strip_a[1]), -R2 * sin(strip_a[1])]], strip_x[0], strip_x[1]);
        }
        for (x = [strip_x[0] + 12 : 12 : strip_x[1] - 6])        // array segment grooves
            translate([x - 0.4, -Ro - 20, -Ro]) cube([0.8, 2*Ro + 40, Ro]);
    }
}
module collars() for (g = [[bC[1], tds_hole], [nC[1], temp_hole]])
    translate([Lo - 0.5, g[0], gz]) rotate([0, 90, 0]) difference() {
        union() { cylinder(d = g[1] + 10, h = 3, $fn = 48); cylinder(d = g[1] + 14, h = 1.2, $fn = 6); }
        translate([0, 0, -1]) cylinder(d = g[1], h = 6, $fn = 48);
    }

// ---------------- hull ----------------
module shell() difference() {
    union() {
        intersection() { xcyl(Ro, 0, Lo); slab(zb, Ro + 1); }
        rings(); ring_bolts(); strips(); collars();
    }
    intersection() { xcyl(Ri, t, Lo - t); slab(zf, Ro + 10); }
}

module cuts() {
    // FRONT: USB slot, 3 scope test points, buzzer sound grid
    translate([-1, nC[1] - 15, zf + 2]) cube([t + 2, 30, 14]);
    for (k = [-1, 0, 1]) translate([-1, nC[1] + 18*k, zf + 42]) rotate([0, 90, 0]) cylinder(d = 6, h = t + 2, $fn = 24);
    for (i = [-1, 0, 1], j = [-1, 0, 1]) translate([-1, bC[1] + 7*i, zf + 26 + 7*j]) rotate([0, 90, 0]) cylinder(d = 3, h = t + 2, $fn = 16);
    // REAR: probe glands (through the connector collars)
    translate([Lo - t - 1, bC[1], gz]) rotate([0, 90, 0]) cylinder(d = tds_hole,  h = t + 6, $fn = 48);
    translate([Lo - t - 1, nC[1], gz]) rotate([0, 90, 0]) cylinder(d = temp_hole, h = t + 6, $fn = 32);
    // TOP: window over the Nucleo and the dial hole
    translate([nC[0] - 45, nC[1] - 20, 0]) cube([90, 40, Ro + 10]);
    translate([bC[0], bC[1] - 20, 0]) cylinder(d = dial_hole, h = Ro + 10, $fn = 32);
    // dome screws (2 per end cap)
    for (x = [-1, Lo - t - 1], y = [-60, 60]) translate([x, y, 6]) rotate([0, 90, 0]) cylinder(d = 3.4, h = t + 2, $fn = 20);
    // engraved name, rear cap
    translate([Lo - 0.8, 0, Ri * 0.55]) rotate([90, 0, 90]) linear_extrude(2)
        text("DHVANI", size = 16, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
    translate([Lo - 0.8, 0, Ri * 0.32]) rotate([90, 0, 90]) linear_extrude(2)
        text("ADAPTIVE SONAR PAYLOAD", size = 6, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
}

module rim(x, y, p, h) translate([x - 2, y - 2, zf - 0.2]) difference() {
    cube([p[0] + 4, p[1] + 4, h + 0.2]);
    translate([2, 2, -1]) cube([p[0], p[1], h + 3]);
}

module lower() difference() {
    union() {
        intersection() { shell(); slab(zb - 1, 0); }
        intersection() {                                         // locating lip
            difference() { xcyl(Ri, t + 10, Lo - t - 10); xcyl(Ri - 2.3, t + 9, Lo - t - 9); }
            slab(-4, 0);
        }
        intersection() {
            difference() { xcyl(Ri - 0.3, t + 10, Lo - t - 10); xcyl(Ri - 2.3, t + 9, Lo - t - 9); }
            slab(0, 4);
        }
        rim(nX, yA, nP, 5);
        for (px = [nX + 4, nX + nP[0] - 12], py = [yA + 4, yA + nP[1] - 12])
            translate([px, py, zf - 0.2]) cube([8, 8, 3.2]);
        rim(aX, yA + (nP[1] - aP[1]) / 2, aP, 8);
        rim(iX, yA + (nP[1] - iP[1]) / 2, iP, 4);
        rim(bX, yB, bP, 4);
        rim(dX, yB + (bP[1] - dP[1]) / 2, dP, 4);
        for (x = [t, Lo - t - 6], y = [-60, 60]) translate([x, y - 5, -15]) cube([6, 10, 27]);
    }
    cuts();
    for (x = [t - 1, Lo - t - 7], y = [-60, 60]) translate([x, y, 6]) rotate([0, 90, 0]) cylinder(d = 2.6, h = 8, $fn = 20);
}

module upper() difference() { intersection() { shell(); slab(0, Ro + 10); } cuts(); }

// ---------------- display saddle (print 2) ----------------
sad_w = 22;  sad_top = -(Ro * sin(strip_a[1] + 4));   // stays just below the transducer strips
module saddle(x) difference() {
    translate([x, -Ro - 10, zb - 12]) cube([sad_w, 2*Ro + 20, sad_top - zb + 12]);
    intersection() { xcyl(Ro + 0.5, x - 1, x + sad_w + 1); slab(zb, Ro + 10); }   // hull shape incl. flat keel
    translate([x + sad_w - 0.8, 0, zb - 6]) rotate([90, 0, 90]) linear_extrude(2)
        text("DHVANI · PS 26058", size = 6, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
}

if (PART == "lower")     translate([0, 0, -zb]) lower();
if (PART == "upper")     upper();
if (PART == "saddle")    translate([0, 0, -(zb - 12)]) saddle(0);
if (PART == "assembled") { lower(); upper(); saddle(55); saddle(Lo - 55 - sad_w); }
