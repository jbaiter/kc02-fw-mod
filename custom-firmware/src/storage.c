/* SPDX-License-Identifier: MIT
 * Bounded FAT16 development backend, root directory / short names only.
 * No delete, overwrite, format, executable loading, or implicit host handoff.
 * Data -> FAT mirrors -> directory publication. Not a journaled filesystem.
 */
#include "storage.h"
static u16 get16(const u8 *p) { return p[0] | ((u16)p[1]<<8); }
static u32 get32(const u8 *p) { return get16(p) | ((u32)get16(p+2)<<16); }
static void set16(u8 *p, u16 n) { p[0] = n; p[1] = n>>8; }
static void set32(u8 *p, u32 n) { set16(p,n); set16(p+2,n>>16); }
static int name83(const char *name, u8 out[11])
{
    unsigned i, n = 0, suffix = 0;
    int dot = 0;
    for (i = 0; i < 11; i++) { out[i] = ' '; }
    for (; *name; name++) {
        unsigned c = (u8)*name;
        if (c == '.') { if (dot || !n) { return 0; } dot = 1; continue; }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { return 0; }
        if (dot) { if (suffix == 3) { return 0; } out[8+suffix++] = c; }
        else { if (n == 8) { return 0; } out[n++] = c; }
    }
    return n > 0;
}
static int equal(const u8 *a, const u8 *b)
{
    unsigned i;
    for (i = 0; i < 11; i++) { if (a[i] != b[i]) { return 0; } }
    return 1;
}
int fat_mount(Fat *f, SectorRead read, SectorWrite write, u32 capacity)
{
    u8 b[512];
    u32 start = 0, span, overhead;
    *f = (Fat){0};
    f->read = read; f->write = write; f->capacity = capacity;
    if (!capacity || !read || !read(0,b) || get16(b+510) != 0xaa55) { return 0; }
    if (b[0] != 0xeb && b[0] != 0xe9) {
        /* Only the first primary partition; never guess through malformed MBRs. */
        if (b[450] != 4 && b[450] != 6 && b[450] != 0x0e) { return 0; }
        start = get32(b+454); span = get32(b+458);
        if (!span || start >= capacity || span > capacity-start || !read(start,b)) { return 0; }
    } else { span = capacity; }
    if (get16(b+510) != 0xaa55 || get16(b+11) != 512) { return 0; }
    f->spc = b[13]; f->fats = b[16]; f->fat_sectors = get16(b+22);
    f->root_entries = get16(b+17);
    f->total = get16(b+19) ? get16(b+19) : get32(b+32);
    if (!f->spc || (f->spc&(f->spc-1)) || f->spc > 64 ||
        !get16(b+14) || f->fats != 2 || !f->fat_sectors ||
        !f->root_entries || f->root_entries > 4096 || !f->total || f->total > span) { return 0; }
    overhead = get16(b+14)+f->fats*f->fat_sectors+(f->root_entries+15)/16;
    if (overhead >= f->total) { return 0; }
    f->clusters = (f->total-overhead)/f->spc;
    if (f->clusters < 4085 || f->clusters >= 65525 ||
        f->clusters+2 > (u32)f->fat_sectors*256) { return 0; }
    f->fat = start+get16(b+14);
    f->root = f->fat+f->fats*f->fat_sectors;
    f->data = start+overhead;
    /* Never allocate against disagreeing FAT copies. This deliberately costs
     * a bounded mount-time pass instead of risking another file's clusters. */
    {
        u8 mirror[512];
        unsigned sector, j;
        for (sector=0; sector<f->fat_sectors; sector++) {
            if (!read(f->fat+sector,b) || !read(f->fat+f->fat_sectors+sector,mirror)) { return 0; }
            for (j=0; j<512; j++) { if (b[j]!=mirror[j]) { return 0; } }
        }
    }
    f->mounted = 1; f->writable = write != 0;
    return 1;
}
static int entry(Fat *f, const u8 name[11], u8 result[32], unsigned *slot)
{
    u8 block[512];
    unsigned i;
    for (i = 0; i < f->root_entries; i++) {
        u8 *e = block+(i%16)*32;
        if (!(i%16) && !f->read(f->root+i/16,block)) { return -1; }
        if (e[0] == 0) { return 0; }
        if (e[0] != 0xe5 && !(e[11]&0x18) && equal(name,e)) {
            unsigned j;
            for (j = 0; j < 32; j++) { result[j] = e[j]; }
            if (slot) { *slot = i; }
            return 1;
        }
    }
    return 0;
}
int fat_read(Fat *f, const char *name, u8 *out, unsigned cap, unsigned *size)
{
    u8 n[11], e[32], block[512];
    u32 cluster, done = 0, bytes, visits = 0;
    if (!f->mounted || f->owner != SD_LOCAL || !name83(name,n) || entry(f,n,e,0) != 1) { return 0; }
    bytes = get32(e+28); cluster = get16(e+26);
    if (bytes > cap) { return 0; }
    while (done < bytes) {
        unsigned s;
        if (cluster < 2 || cluster >= f->clusters+2 || visits++ >= f->clusters) { return 0; }
        for (s = 0; s < f->spc && done < bytes; s++) {
            unsigned j, count = bytes-done < 512 ? bytes-done : 512;
            if (!f->read(f->data+(cluster-2)*f->spc+s,block)) { return 0; }
            for (j = 0; j < count; j++) { out[done++] = block[j]; }
        }
        if (!f->read(f->fat+cluster/256,block)) { return 0; }
        cluster = get16(block+(cluster%256)*2);
    }
    if (bytes && cluster < 0xfff8) { return 0; } /* Reject long/cyclic chains too. */
    *size = bytes;
    return 1;
}
int fat_create(Fat *f, const char *name, const u8 *bytes, unsigned size)
{
    u8 n[11], block[512], e[32];
    unsigned i, slot, start = 0, run = 0, needed, done = 0;
    if (!f->mounted || !f->writable || f->owner != SD_LOCAL || !size || size > 512*1024 ||
        !name83(name,n) || entry(f,n,e,0) != 0) { return 0; }
    for (slot = 0; slot < f->root_entries; slot++) {
        if (!(slot%16) && !f->read(f->root+slot/16,block)) { return 0; }
        if (!block[(slot%16)*32] || block[(slot%16)*32] == 0xe5) { break; }
    }
    if (slot == f->root_entries) { return 0; }
    needed = (size+f->spc*512-1)/(f->spc*512);
    for (i = 2; i < f->clusters+2; i++) {
        if (i == 2 || !(i%256)) { if (!f->read(f->fat+i/256,block)) { return 0; } }
        if (!get16(block+(i%256)*2)) {
            if (!run) { start = i; }
            if (++run == needed) { break; }
        } else { run = 0; }
    }
    if (run != needed) { return 0; }
    /* File data before allocation, directory entry last. On any write error,
     * make the mount read-only; don't attempt hazardous rollback writes. */
    f->writable = 0;
    for (i = 0; i < needed*f->spc; i++) {
        unsigned j;
        for (j = 0; j < 512; j++) { block[j] = done < size ? bytes[done++] : 0; }
        if (!f->write(f->data+(start-2)*f->spc+i,block)) { return 0; }
    }
    for (i = start; i < start+needed; i++) {
        unsigned mirror;
        for (mirror = 0; mirror < f->fats; mirror++) {
            u32 lba = f->fat+mirror*f->fat_sectors+i/256;
            if (!f->read(lba,block)) { return 0; }
            set16(block+(i%256)*2,i+1 < start+needed ? i+1 : 0xffff);
            if (!f->write(lba,block)) { return 0; }
        }
    }
    if (!f->read(f->root+slot/16,block)) { return 0; }
    for (i = 0; i < 32; i++) { e[i] = 0; }
    for (i = 0; i < 11; i++) { e[i] = n[i]; }
    e[11] = 0x20;
    set16(e+24,0x5821); /* Fixed development timestamp: no RTC policy yet. */
    set16(e+26,start); set32(e+28,size);
    for (i = 0; i < 32; i++) { block[slot%16*32+i] = e[i]; }
    if (!f->write(f->root+slot/16,block)) { return 0; }
    f->writable = 1;
    return 1;
}
void photo_name(char out[13], unsigned id)
{
    int i;
    out[0]='L'; out[1]='U'; out[2]='M';
    for (i=7; i>=3; i--) { out[i]='0'+id%10; id/=10; }
    out[8]='.'; out[9]='B'; out[10]='M'; out[11]='P'; out[12]=0;
}
unsigned fat_photos(Fat *f, unsigned *ids, unsigned cap)
{
    u8 block[512];
    unsigned i, count = 0;
    if (!f->mounted || f->owner != SD_LOCAL) { return 0; }
    for (i=0; i<f->root_entries; i++) {
        u8 *e = block+(i%16)*32;
        unsigned j, id = 0;
        if (!(i%16) && !f->read(f->root+i/16,block)) { return 0; }
        if (!e[0]) { break; }
        if (e[0]!='L' || e[1]!='U' || e[2]!='M' || e[8]!='B' || e[9]!='M' || e[10]!='P' || (e[11]&0x18)) { continue; }
        for (j=3; j<8 && e[j]>='0' && e[j]<='9'; j++) { id=id*10+e[j]-'0'; }
        if (j==8 && count<cap) { ids[count++]=id; }
    }
    /* Stable filename order, independent of directory insertion order. */
    for (i=1; i<count; i++) {
        unsigned j=i, v=ids[i];
        while (j && ids[j-1]>v) { ids[j]=ids[j-1]; j--; }
        ids[j]=v;
    }
    return count;
}
int sd_handoff(Fat *f, SdOwner to, int io_idle)
{
    if (!f->mounted || !io_idle || (unsigned)to > SD_UPDATE ||
        (f->owner != SD_LOCAL && to != SD_LOCAL)) { return 0; }
    if (f->owner != SD_LOCAL && to == SD_LOCAL) {
        /* The host/updater may have changed BPB, FAT or media. No cached
         * geometry may survive: the caller must remount after eject. */
        f->mounted=0; f->writable=0;
    }
    f->owner=to;
    return 1;
}
