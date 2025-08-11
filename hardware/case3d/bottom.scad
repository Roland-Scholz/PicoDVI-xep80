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

    //holes for screws pcb
/*    
    translate([20, 35,0])
        cylinder(4, 3.5, 3.5);
    translate([xmax-20,35,0])
        cylinder(4, 3.5, 3.5);
    translate([21.5,35+118,0])
        cylinder(4, 3.5, 3.5);
    translate([xmax-21.5,35+118,0])
        cylinder(4, 3.5, 3.5);
    
    //bumper
    
    translate([35, 27.5, 0])
        cylinder(1, 1, 1);
    translate([xmax-35, 27.5,0])
        cylinder(1, 1, 1);
    translate([35, ymax-27.5, 0])
        cylinder(1, 1, 1);
    translate([xmax-35, ymax-27.5, 0])
        cylinder(1, 1, 1);
*/    
}


module shell_bottom() {



    difference(){
        base_shape();
        cubes_left();    
        cubes_right();
    translate([xmax/2-5, ymax-1, 3])
        cube([10, 4, 4]);
    translate([10, 2, 10])
        rotate([90,0,0])
            cylinder(h=5, r=3.25);
    translate([25, 2, zmax])
        rotate([90,0,0])
            cylinder(h=5, r=3.25);
    translate([21, 49.5, 0])
        cylinder(r=2, h=4);

    translate([0, ymax/2+1, 11])
        rotate([0,90,0])
            cylinder(r=0.75, h=3);
    translate([xmax-thick, ymax/2+1, 11])
        rotate([0,90,0])
            cylinder(r=0.75, h=3);
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

    translate([3, 9, thick])
        cube([29, 1, 10]);

    translate([thick, 16, 5])
        cube([5, 1, 10]);
    translate([xmax-thick-5, 16, 5])
        cube([5, 1, 10]);
    translate([thick, 47, 5])
        cube([5, 1, 10]);
    translate([xmax-thick-5, 47, 5])
        cube([5, 1, 10]);

    translate([7, 0, thick])
        cube([2,ymax,2]);
    translate([7+19, 0, thick])
        cube([2,ymax,2]);

/*
    //screws holding case together
    translate([edge+5,28,0])
        screw(4);
    translate([xmax-edge-5,28,0])
        screw(4);
    translate([edge+5,ymax-26,0])
        screw(4);
    translate([xmax-edge-5,ymax-26,0])
        screw(4);
    
    //reinforcement front
    translate([0, 17 ,0])
        base_shape_holder();    
    
    //reinforcement in the middle
    translate([0, ymax/2 - 10])
        base_shape_holder();    
    translate([0, ymax/2 + 14])
        base_shape_holder();    

    //reinforcement back
    translate([0, ymax-14 ,0])
        base_shape_holder();    

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