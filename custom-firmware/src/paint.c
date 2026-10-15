/* SPDX-License-Identifier: MIT */
#include "paint.h"

u16 blend(u16 a, u16 b, unsigned alpha)
{
    unsigned r,g,bl;
    if (alpha>=255) { return b; }
    r=(((a>>11)*(255-alpha)+(b>>11)*alpha)+127)/255;
    g=((((a>>5)&63)*(255-alpha)+((b>>5)&63)*alpha)+127)/255;
    bl=(((a&31)*(255-alpha)+(b&31)*alpha)+127)/255;
    return (r<<11)|(g<<5)|bl;
}
/* Bound geometry before coordinate arithmetic; also keeps squared distances
 * in range on 32-bit CPUs. Offscreen output is clipped, never wrapped. */
static int rectangle(int x, int y, int w, int h)
{
    return x>=-LW && x<=LW && y>=-LH && y<=LH &&
           w>0 && w<=LW*2 && h>0 && h<=LH*2;
}
void box(u16 *f, int x, int y, int w, int h, u16 c)
{
    int xx,yy;
    if (!rectangle(x,y,w,h)) { return; }
    for (yy=y<0 ? 0 : y; yy<y+h && yy<LH; yy++) {
        for (xx=x<0 ? 0 : x; xx<x+w && xx<LW; xx++) { f[yy*LW+xx]=c; }
    }
}
/* Coordinates in eighths of a pixel. The four sample centers on each axis
 * are -3,-1,+1,+3. Full interior/exterior pixels take constant time. */
static unsigned disc(int dx, int dy, int radius)
{
    int d=dx*dx+dy*dy;
    int outer=radius+6, inner=radius-6;
    int sx,sy;
    unsigned hits=0;
    if (radius<=0 || d>=outer*outer) { return 0; }
    if (inner>0 && d<=inner*inner) { return 255; }
    for (sy=-3; sy<=3; sy+=2) {
        for (sx=-3; sx<=3; sx+=2) {
            int xx=dx+sx, yy=dy+sy;
            if (xx*xx+yy*yy<=radius*radius) { hits++; }
        }
    }
    return (hits*255+8)/16;
}
void round_box(u16 *f, int x, int y, int w, int h, int radius, u16 c)
{
    int xx,yy;
    if (!rectangle(x,y,w,h) || radius<0) { return; }
    if (radius>w/2) { radius=w/2; }
    if (radius>h/2) { radius=h/2; }
    if (!radius) { box(f,x,y,w,h,c); return; }
    for (yy=y<0 ? 0 : y; yy<y+h && yy<LH; yy++) {
        for (xx=x<0 ? 0 : x; xx<x+w && xx<LW; xx++) {
            int rx=xx-x, ry=yy-y, dx=0,dy=0;
            unsigned coverage;
            if (rx<radius) { dx=(radius-rx)*8-4; }
            else if (rx>=w-radius) { dx=(rx-(w-radius))*8+4; }
            if (ry<radius) { dy=(radius-ry)*8-4; }
            else if (ry>=h-radius) { dy=(ry-(h-radius))*8+4; }
            coverage=disc(dx,dy,radius*8);
            if (coverage) { f[yy*LW+xx]=blend(f[yy*LW+xx],c,coverage); }
        }
    }
}
void ring8(u16 *f, int cx, int cy, int radius8, int width8, u16 c)
{
    int x,y,inner,radius;
    if (cx < -LW || cx>LW*2 || cy < -LH || cy>LH*2 ||
        radius8<=0 || radius8>LW*8 || width8<=0) { return; }
    inner=width8>=radius8 ? 0 : radius8-width8;
    radius=(radius8+7)/8;
    for (y=-radius; y<=radius; y++) {
        for (x=-radius; x<=radius; x++) {
            int px=cx+x, py=cy+y;
            unsigned coverage;
            if (px<0 || px>=LW || py<0 || py>=LH) { continue; }
            coverage=disc(x*8,y*8,radius8)-disc(x*8,y*8,inner);
            if (coverage) { f[py*LW+px]=blend(f[py*LW+px],c,coverage); }
        }
    }
}
void ring(u16 *f, int cx, int cy, int radius, int width, u16 c)
{
    if (radius<=0 || radius>LW || width<=0) { return; }
    ring8(f,cx,cy,radius*8,(width>=radius ? radius : width)*8,c);
}
