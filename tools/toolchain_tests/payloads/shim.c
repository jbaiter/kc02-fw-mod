typedef unsigned long u32;
extern void SPI_read(u32, void *, u32);
void __attribute__((section(".entry"))) test_entry(void *buffer, u32 length, u32 offset)
{
    SPI_read(offset,buffer,length);
}
