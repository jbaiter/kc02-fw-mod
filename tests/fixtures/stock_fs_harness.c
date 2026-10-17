/* Native fake FatFs callbacks. No disk, USB, camera or host filesystem writes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stock_fs.h"
#include "stock_photo.h"
#define NEW_SIZE (54u+320u*180u*3u)
#define OLD_SIZE (54u+320u*240u*3u)
static const char *names[8]={"OTHER.TXT","LUM00005.BMP","LUM00002.BMP","LUM00099.BMP"};
static unsigned sizes[8]={10,NEW_SIZE,OLD_SIZE,5},files=4;
static u8 contents[8][OLD_SIZE],encoded[NEW_SIZE],loaded[OLD_SIZE];
static unsigned calls,opens,writes,reads,stops,scan_index,max_chunk;
static int good_route=1,short_write,corrupt_read,close_error,guard_error;
static int route(void) { return good_route; }
static void put32(u8 *p,unsigned v) { for(unsigned i=0;i<4;i++) p[i]=(u8)(v>>(i*8)); }
static unsigned from_path(const char *path)
{
    assert(!strncmp(path,"0:/LUM",6));
    for(unsigned i=0;i<files;i++) if(!strcmp(path+3,names[i])) return i;
    return files;
}
static int open_file(void *obj,const char *path,u8 mode)
{
    u32 *f=obj;
    unsigned i=from_path(path);
    calls++; opens++;
    assert(mode==1 || mode==6); /* never truncate/replace */
    if(mode==6) {
        if(i<files) return 8;
        assert(files<8 && !strcmp(path,"0:/LUM00100.BMP"));
        names[files]="LUM00100.BMP"; sizes[files]=0; i=files++;
    } else if(i==files) return 4;
    f[0]=i; f[1]=0; f[2]=mode;
    return 0;
}
static int read_file(void *obj,void *out,unsigned want,unsigned *done)
{
    u32 *f=obj;
    calls++; reads++;
    assert(want<=STOCK_FS_CHUNK && f[2]==1);
    if(want>max_chunk) max_chunk=want;
    *done=want;
    if(*done>sizes[f[0]]-f[1]) *done=sizes[f[0]]-f[1];
    memcpy(out,contents[f[0]]+f[1],*done);
    if(corrupt_read && f[0]==4 && *done) ((u8 *)out)[0]^=1;
    f[1]+=*done;
    return 0;
}
static int write_file(void *obj,const void *in,unsigned want,unsigned *done)
{
    u32 *f=obj;
    calls++; writes++;
    assert(want<=STOCK_FS_CHUNK && f[2]==6 && f[0]==4);
    if(want>max_chunk) max_chunk=want;
    *done=short_write ? want-1 : want;
    assert(f[1]+*done<=OLD_SIZE);
    memcpy(contents[f[0]]+f[1],in,*done);
    f[1]+=*done; sizes[f[0]]=f[1];
    if(guard_error) ((u32 *)obj)[STOCK_FS_OBJECT_WORDS]=0;
    return 0;
}
static int close_file(void *obj)
{
    (void)obj; calls++;
    return close_error ? 1 : 0;
}
static int open_dir(void *obj,const char *path)
{
    (void)obj; calls++;
    assert(!strcmp(path,"0:/")); scan_index=0;
    return 0;
}
static int read_dir(void *obj,void *info)
{
    u8 *b=info;
    (void)obj; calls++;
    if(scan_index==files) { b[26]=0; return 0; }
    put32(b,sizes[scan_index]);
    put32(b+4,0); b[12]=0x20;
    strcpy((char *)b+26,names[scan_index++]);
    return 0;
}
static int close_dir(void *obj) { (void)obj; calls++; return 0; }
static int stop(void) { calls++; stops++; return 0; }
static const StockFsOps ops={route,open_file,read_file,write_file,close_file,
                             open_dir,read_dir,close_dir,stop};
static int until_event(StockFs *f)
{
    for(unsigned i=0;i<4000;i++) {
        int event=stock_fs_step(f);
        if(event) return event;
    }
    assert(!"job did not finish"); return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2);
    int mode=atoi(argv[1]);
    StockFs f;
    for(unsigned i=0;i<NEW_SIZE;i++) encoded[i]=(u8)(i*17u+5u);
    for(unsigned i=0;i<OLD_SIZE;i++) contents[2][i]=(u8)(i*3u);
    for(unsigned i=0;i<NEW_SIZE;i++) contents[1][i]=(u8)(i*7u);
    assert(stock_fs_init(&f,&ops,mode!=5));
    assert(f.count==2 && f.ids[0]==2 && f.ids[1]==5 && f.next_id==100);
    unsigned before=calls;
    if(mode==9) {
        static u16 camera[PIXELS],photo[PIXELS];
        App a;
        Platform p={0};
        for(unsigned i=0;i<PIXELS;i++) camera[i]=(u16)(i*3u);
        p.save_photo=stock_photo_save;
        p.load_photo=stock_photo_load;
        p.photo_count=stock_photo_count;
        stock_photo_init(1,loaded,sizeof(loaded),&ops);
        app_init(&a,&p,0); a.booting=0;
        assert(a.count==2);
        before=calls;
        app_event(&a,&p,KEY_OK,0,100,camera,photo);
        assert(a.shutter && !strcmp(a.notice,"Saving...") && a.count==2);
        assert(calls==before && !opens && !writes);
        assert(!stock_photo_poll(&a,101,0) && calls==before);
        for(unsigned i=0;i<4000 && a.count!=3;i++) stock_photo_poll(&a,112+i,1);
        assert(a.count==3 && a.photo==2 && !strcmp(a.notice,"Photo saved"));
        assert(sizes[4]==NEW_SIZE);
        /* Navigation must not wait for the shutter animation or do a blocking read. */
        before=calls;
        app_event(&a,&p,KEY_BACK,0,120,camera,photo);
        assert(a.screen==GALLERY && !strcmp(a.notice,"Loading...") && calls==before);
        for(unsigned i=0;i<4000 && a.notice[0];i++) stock_photo_poll(&a,121+i,1);
        assert(!a.notice[0] && a.photo_rows==CAMERA_ROWS);
        assert(!memcmp(photo,camera,LW*CAMERA_ROWS*sizeof(u16)));
        for(unsigned i=LW*CAMERA_ROWS;i<PIXELS;i++) assert(photo[i]==0);
        /* Reinitialize and reload from the fake card, not the prior RGB snapshot. */
        stock_photo_init(0,loaded,sizeof(loaded),&ops);
        app_init(&a,&p,0); a.booting=0; a.photo=2;
        app_event(&a,&p,KEY_BACK,0,200,camera,photo);
        for(unsigned i=0;i<4000 && a.notice[0];i++) stock_photo_poll(&a,201+i,1);
        assert(!a.notice[0] && a.photo_rows==CAMERA_ROWS);
        assert(!memcmp(photo,camera,LW*CAMERA_ROWS*sizeof(u16)));
        return 0;
    }
    if(mode==5) {
        assert(!stock_fs_save(&f,encoded,NEW_SIZE) && calls==before);
        assert(stock_fs_load(&f,0,loaded,sizeof(loaded))==PHOTO_QUEUED);
        assert(calls==before);
        assert(until_event(&f)==STOCK_FS_LOADED);
        assert(f.loaded_size==OLD_SIZE && !memcmp(loaded,contents[2],OLD_SIZE));
        assert(!writes); return 0;
    }
    if(mode==6) {
        assert(stock_fs_load(&f,0,loaded,sizeof(loaded))==PHOTO_QUEUED);
        assert(stock_fs_step(&f)==STOCK_FS_NONE); /* open old selection */
        assert(stock_fs_load(&f,1,loaded,sizeof(loaded))==PHOTO_QUEUED);
        assert(until_event(&f)==STOCK_FS_LOADED);
        assert(f.active_index==1 && f.loaded_size==NEW_SIZE);
        assert(!memcmp(loaded,contents[1],NEW_SIZE) && !writes);
        return 0;
    }
    assert(stock_fs_save(&f,encoded,NEW_SIZE)==PHOTO_QUEUED);
    assert(calls==before && !opens && !writes); /* immediate UI feedback */
    assert(stock_fs_save(&f,encoded,NEW_SIZE)==PHOTO_BUSY);
    if(mode==2) short_write=1;
    if(mode==3) corrupt_read=1;
    if(mode==4) good_route=0;
    if(mode==7) close_error=1;
    if(mode==8) guard_error=1;
    int event=until_event(&f);
    if(mode!=1) {
        assert(event==STOCK_FS_FAILED && f.count==2 && !f.writable);
        if(mode==4) assert(calls==before && !opens && !writes);
        if(mode==2 || mode==8) assert(writes==1);
        return 0;
    }
    assert(event==STOCK_FS_SAVED && f.count==3 && f.ids[2]==100);
    assert(sizes[4]==NEW_SIZE && !memcmp(contents[4],encoded,NEW_SIZE));
    assert(stops>=2 && opens==2 && reads>1 && max_chunk==512);
    before=calls;
    assert(stock_fs_load(&f,2,loaded,sizeof(loaded))==PHOTO_QUEUED);
    assert(calls==before);
    assert(until_event(&f)==STOCK_FS_LOADED && f.loaded_size==NEW_SIZE);
    assert(!memcmp(loaded,encoded,NEW_SIZE));
    assert(!stock_fs_load(&f,99,loaded,sizeof(loaded)));
    puts("verified create-only save/readback/gallery load");
    return 0;
}
