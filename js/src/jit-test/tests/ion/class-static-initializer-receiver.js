// Class static initialization uses a receiver that is not a new-object MIR.
for (let i=0;i<80;++i) {
    class C { static x=i; static { this.y=this.x+1; } }
    assertEq(C.x,i);
    assertEq(C.y,i+1);
}
