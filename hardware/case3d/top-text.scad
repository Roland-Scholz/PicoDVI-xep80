use <C:/Users/rolan/Downloads/Atarian/SF Atarian System.ttf>

thick = 2;
xmax = 250;
ymax = 180+thick;
zmax = 39;

edge=5;

//translate([230,15,0])
//    ataritext();

difference(){
    translate([0,0,0])
        ataritext();
}

module ataritext() {
    linear_extrude(0.2)
        mirror([1,0,0])
        text("ABBUC XEP80", 8, "SF Atarian System:style=Regular"); 
}
