$fn = 20;

thick = 2;
xmax = 35;
ymax = 60+thick;
zmax = 15;

edge=5;

//xmt = xmax + 2*thick;
//ymt = ymax + 2*thick;    

//   shell_bot();

difference() {
    shell_bottom();
}


module shell_bottom() {

/*
    translate([7, 0, thick])
        cube([1,ymax,2]);
    translate([7+20, 0, thick])
        cube([1,ymax,2]);
*/    
    difference(){
        base_shape();
        cubes_left();    
        cubes_right();
    translate([xmax/2-8, ymax-1, 4])
        cube([16, 4, 6]);
    translate([10, 2, zmax])
        rotate([90,0,0])
            cylinder(h=5, r=3.25);
//    translate([8.5, 3.5, -1])
//        cube([18, 55, 1]);

    }

}



module base_shape() {
    translate([0, ymax+thick, 0])
        rotate([90,0,0]) 
            linear_extrude(ymax+thick)
                base_shape_2d(thick);

    translate([0, ymax+thick])
        base_shape_holder();
    translate([0, 0])
        base_shape_holder();

    translate([11, 11, thick])
        cube([13, 1, 08]);

    translate([thick+1, 18, 7])
        rotate([0,0,90])
            cube([28, 1, 14]);
    translate([xmax-thick, 18, 7])
        rotate([0,0,90])
            cube([28, 1, 14]);

/*
    translate([thick, 16, 5])
        cube([5, 1, 10]);
    translate([xmax-thick-5, 16, 5])
        cube([5, 1, 10]);
    translate([thick, 47, 5])
        cube([5, 1, 10]);
    translate([xmax-thick-5, 47, 5])
        cube([5, 1, 10]);
*/
}

module base_shape_holder() {
    
    rotate([90,0,0])
    linear_extrude(thick)
        base_shape_2da();

}

module base_shape_2da() {
    th = 10;
    polygon([[0, zmax], [0, edge], [edge,0], [xmax-edge, 0], [xmax, edge], [xmax, zmax],/*[xmax-th,zmax], [xmax-th, edge+th], [xmax-edge-th, th],[edge+th, th]*/, [th, edge+th],[th, zmax]]);
}


module base_shape_2d(th) {
    polygon([[0, zmax], [0, edge], [edge,0], [xmax-edge, 0], [xmax, edge], [xmax, zmax],[xmax-th,zmax], [xmax-th, edge+th], [xmax-edge-th, th],[edge+th, th], [th, edge+th],[th, zmax]]);
}

module cubes_left() {
    for(i = [2:8:ymax])
      translate([0, i, 0])
      cube([edge+thick, 4, edge+thick]);
}

module cubes_right() {
    for(i = [2:8:ymax])
      translate([xmax-edge-thick, i, 0])
      cube([edge+thick, 4, edge+thick]);
}
    
module screw(r) {    
    difference() {
        cylinder(zmax,r*1.2,r*1.2);
        translate([0,0,-1]);
            cylinder(zmax+2,r/2+0.2,r/2+0.2);
//        cylinder(5,6,6);
    }
}

module foot(r, h) {    
    difference() {
        cylinder(h,r*2,r*1.3);
            cylinder(h,r/2+0.2,r/2+0.2);
//        cylinder(5,6,6);
    }
}