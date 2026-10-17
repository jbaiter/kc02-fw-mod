/* SPDX-License-Identifier: MIT */
#include "stock_fs.h"

/* Decompiled get_fileinfo at 0x0206d540: 64-bit size +0, attributes +12,
 * short name +13, UTF-8 name +26. Opaque FIL/DIR/info buffers avoid importing
 * SDK structure packing that is not guaranteed to match this firmware. */
#define INFO_NAME 26u
#define INFO_ATTR 12u
#define MAX_SCAN 4096u
#define PHOTO_SIZE (54u + LW*CAMERA_ROWS*3u)
#define LEGACY_SIZE (54u + PIXELS*3u)
/* A 1280x720 still lands near 0x1e000..0x23000 in stock's own budget. The band is
 * deliberately wide: it only rejects files that cannot be a still at all, and the
 * decoder rejects bad content when the photo is actually opened. */
#define JPEG_MIN 0x1000u
#define JPEG_MAX 0x80000u
enum { SAVE_OPEN=1, SAVE_WRITE, SAVE_CLOSE, SAVE_STOP, SAVE_REOPEN,
       SAVE_VERIFY, SAVE_EOF, SAVE_VERIFY_CLOSE, SAVE_VERIFY_STOP,
       LOAD_OPEN, LOAD_READ, LOAD_EOF, LOAD_CLOSE, LOAD_STOP,
       CANCEL_CLOSE, CANCEL_STOP };

static void object_init(StockFsObject *o)
{
    unsigned i;
    for (i=0; i<STOCK_FS_OBJECT_WORDS; ++i) o->words[i]=0;
    for (i=0; i<8; ++i) o->guard[i]=STOCK_FS_OBJECT_GUARD;
}
static int object_ok(const StockFsObject *o)
{
    unsigned i;
    for (i=0; i<8; ++i) if (o->guard[i]!=STOCK_FS_OBJECT_GUARD) return 0;
    return 1;
}
static u32 le32(const u8 *p)
{
    return p[0] | (u32)p[1]<<8 | (u32)p[2]<<16 | (u32)p[3]<<24;
}
static char upper(char c) { return c>='a' && c<='z' ? c-'a'+'A' : c; }
/* Returns the ID for LUMnnnnn.BMP (format 0) or LUMnnnnn.JPG (format 1). */
static unsigned photo_id(const char *p, unsigned *format)
{
    unsigned i,id=0;
    if (upper(p[0])!='L' || upper(p[1])!='U' || upper(p[2])!='M') return 0;
    for (i=3; i<8; ++i) {
        if (p[i]<'0' || p[i]>'9') return 0;
        id=id*10u+(unsigned)(p[i]-'0');
    }
    if (p[8]!='.' || p[12]) return 0;   /* exactly LUMnnnnn.XXX */
    if (upper(p[9])=='B' && upper(p[10])=='M' && upper(p[11])=='P') *format=STOCK_FS_BMP;
    else if (upper(p[9])=='J' && upper(p[10])=='P' && upper(p[11])=='G') *format=STOCK_FS_JPEG;
    else return 0;
    return id;
}
static void path_for(char out[16], unsigned id, unsigned format)
{
    unsigned i;
    out[0]='0'; out[1]=':'; out[2]='/';
    out[3]='L'; out[4]='U'; out[5]='M';
    for (i=11; i>6; ) { --i; out[i]='0'+id%10; id/=10; }
    out[11]='.';
    if (format==STOCK_FS_JPEG) { out[12]='J'; out[13]='P'; out[14]='G'; }
    else { out[12]='B'; out[13]='M'; out[14]='P'; }
    out[15]=0;
}
/* A file is only listed if its size could be a still this camera writes. */
static int size_ok(unsigned size, unsigned format)
{
    if (format==STOCK_FS_JPEG) return size>=JPEG_MIN && size<=JPEG_MAX;
    return size==PHOTO_SIZE || size==LEGACY_SIZE;
}
static int route_ok(StockFs *f)
{
    return f->ops && f->ops->route_ok && f->ops->route_ok();
}
int stock_fs_init(StockFs *f, const StockFsOps *ops, int allow_writes)
{
    StockFsObject dir,info;
    unsigned i,max_id=0;
    int ret,ended=0;
    *f=(StockFs){0};
    f->ops=ops;
    f->pending_load=-1;
    if (!route_ok(f) || !ops->open || !ops->read || !ops->write || !ops->close ||
        !ops->opendir || !ops->readdir || !ops->closedir || !ops->stop) return 0;
    object_init(&dir);
    ret=ops->opendir(dir.words,"0:/");
    if (ret || !object_ok(&dir)) { f->last_error=ret ? ret : 100; return 0; }
    for (i=0; i<MAX_SCAN; ++i) {
        const u8 *b;
        unsigned id,size,format=STOCK_FS_BMP;
        object_init(&info);
        ret=ops->readdir(dir.words,info.words);
        if (ret || !object_ok(&dir) || !object_ok(&info)) break;
        b=(const u8 *)info.words;
        if (!b[INFO_NAME]) { ended=1; break; }
        id=photo_id((const char *)b+INFO_NAME,&format);
        if (!id) continue;
        if (id>max_id) max_id=id;  /* reserve IDs of incomplete/invalid files too */
        size=le32(b);
        if (le32(b+4) || (b[INFO_ATTR]&0x18u) || !size_ok(size,format)) continue;
        if (f->count<GALLERY_MAX) {
            unsigned j=f->count++;
            while (j && f->ids[j-1]>id) {
                f->ids[j]=f->ids[j-1]; f->sizes[j]=f->sizes[j-1];
                f->formats[j]=f->formats[j-1]; --j;
            }
            f->ids[j]=id; f->sizes[j]=size; f->formats[j]=format;
        }
    }
    if (route_ok(f)) {
        int close_ret=ops->closedir(dir.words);
        if (!ret) ret=close_ret;
    } else ret=101;
    if (ret || !ended || !object_ok(&dir)) {
        f->count=0; f->last_error=ret ? ret : 100; return 0;
    }
    f->next_id=max_id+1;
    f->ready=1;
    f->writable=allow_writes!=0;
    return 1;
}
int stock_fs_save(StockFs *f, const u8 *bytes, unsigned size, unsigned format)
{
    if (f->kind) return PHOTO_BUSY;
    if (!f->ready || !f->writable || !bytes || !size_ok(size,format) ||
        !route_ok(f)) return 0;
    if (f->count>=GALLERY_MAX || f->next_id>99999u) return PHOTO_FULL;
    f->save_bytes=bytes; f->size=size; f->at=0;
    f->loaded_format=format;
    f->saved_id=f->next_id++;
    path_for(f->path,f->saved_id,format);
    f->kind=1; f->phase=SAVE_OPEN; f->last_error=0;
    return PHOTO_QUEUED;
}
int stock_fs_load(StockFs *f, unsigned index, u8 *bytes, unsigned capacity)
{
    if (!f->ready || index>=f->count || !bytes || capacity<f->sizes[index] || !route_ok(f)) return 0;
    f->pending_load=(int)index;
    f->pending_bytes=bytes;
    f->pending_capacity=capacity;
    return PHOTO_QUEUED;
}
static int failed(StockFs *f, int error)
{
    f->last_error=error ? error : 100;
    /* Do not call close through a changed route: that could write another medium.
     * A partial newly-created file may remain; never delete or overwrite it. */
    if (route_ok(f)) {
        if (f->opened && object_ok(&f->file)) f->ops->close(f->file.words);
        f->ops->stop();
    }
    f->opened=0; f->kind=0; f->phase=0; f->pending_load=-1;
    f->writable=0;  /* fail closed until reset/reinitialization */
    return STOCK_FS_FAILED;
}
int stock_fs_step(StockFs *f)
{
    u8 block[STOCK_FS_CHUNK];
    unsigned want,done=0,i;
    int ret;
    if (!f->ready) return STOCK_FS_NONE;
    if (!route_ok(f)) { f->ready=0; return failed(f,101); }
    if (!f->kind && f->pending_load>=0) {
        f->active_index=(unsigned)f->pending_load;
        f->load_bytes=f->pending_bytes;
        f->size=f->sizes[f->active_index];
        if (f->size>f->pending_capacity) return failed(f,102);
        f->at=0; f->kind=2; f->phase=LOAD_OPEN;
        path_for(f->path,f->ids[f->active_index],f->formats[f->active_index]);
        f->pending_load=-1;
    }
    if (!f->kind) return STOCK_FS_NONE;
    /* A new viewer selection supersedes an old read; saving is never cancelled. */
    if (f->kind==2 && f->pending_load>=0 &&
        (unsigned)f->pending_load!=f->active_index &&
        f->phase!=CANCEL_CLOSE && f->phase!=CANCEL_STOP) {
        f->phase=f->opened ? CANCEL_CLOSE : CANCEL_STOP;
    }
    switch (f->phase) {
    case SAVE_OPEN:
    case SAVE_REOPEN:
    case LOAD_OPEN:
        object_init(&f->file);
        /* 0x06 = FA_WRITE | FA_CREATE_NEW. NEVER FA_CREATE_ALWAYS (0x08). */
        ret=f->ops->open(f->file.words,f->path,f->phase==SAVE_OPEN ? 0x06u : 0x01u);
        if (ret || !object_ok(&f->file)) return failed(f,ret);
        f->opened=1;
        f->at=0;
        f->phase=f->phase==SAVE_OPEN ? SAVE_WRITE :
                 f->phase==SAVE_REOPEN ? SAVE_VERIFY : LOAD_READ;
        break;
    case SAVE_WRITE:
        want=f->size-f->at;
        if (want>STOCK_FS_CHUNK) want=STOCK_FS_CHUNK;
        ret=f->ops->write(f->file.words,f->save_bytes+f->at,want,&done);
        if (ret || done!=want || !object_ok(&f->file)) return failed(f,ret ? ret : 103);
        f->at+=done;
        if (f->at==f->size) f->phase=SAVE_CLOSE;
        break;
    case SAVE_VERIFY:
    case LOAD_READ:
        want=f->size-f->at;
        if (want>STOCK_FS_CHUNK) want=STOCK_FS_CHUNK;
        ret=f->ops->read(f->file.words,block,want,&done);
        if (ret || done!=want || !object_ok(&f->file)) return failed(f,ret ? ret : 104);
        for (i=0; i<done; ++i) {
            if (f->phase==SAVE_VERIFY) {
                if (block[i]!=f->save_bytes[f->at+i]) return failed(f,105);
            } else f->load_bytes[f->at+i]=block[i];
        }
        f->at+=done;
        if (f->at==f->size) f->phase=f->phase==SAVE_VERIFY ? SAVE_EOF : LOAD_EOF;
        break;
    case SAVE_EOF:
    case LOAD_EOF:
        ret=f->ops->read(f->file.words,block,1,&done);
        if (ret || done || !object_ok(&f->file)) return failed(f,ret ? ret : 106);
        f->phase=f->phase==SAVE_EOF ? SAVE_VERIFY_CLOSE : LOAD_CLOSE;
        break;
    case SAVE_CLOSE:
    case SAVE_VERIFY_CLOSE:
    case LOAD_CLOSE:
    case CANCEL_CLOSE:
        ret=f->ops->close(f->file.words);
        f->opened=0;
        if (ret || !object_ok(&f->file)) return failed(f,ret);
        f->phase=f->phase==SAVE_CLOSE ? SAVE_STOP :
                 f->phase==SAVE_VERIFY_CLOSE ? SAVE_VERIFY_STOP :
                 f->phase==LOAD_CLOSE ? LOAD_STOP : CANCEL_STOP;
        break;
    case SAVE_STOP:
        ret=f->ops->stop();
        if (ret) return failed(f,ret);
        f->phase=SAVE_REOPEN;
        break;
    case SAVE_VERIFY_STOP:
    case LOAD_STOP:
    case CANCEL_STOP:
        ret=f->ops->stop();
        if (ret) return failed(f,ret);
        if (f->phase==SAVE_VERIFY_STOP) {
            f->ids[f->count]=f->saved_id;
            f->sizes[f->count]=f->size;
            f->formats[f->count++]=f->loaded_format;
            f->kind=0; f->phase=0;
            return STOCK_FS_SAVED;
        }
        if (f->phase==LOAD_STOP) {
            f->loaded_size=f->size;
            f->loaded_format=f->formats[f->active_index];
            f->kind=0; f->phase=0;
            return STOCK_FS_LOADED;
        }
        f->kind=0; f->phase=0;
        break;
    default:
        return failed(f,107);
    }
    return STOCK_FS_NONE;
}
