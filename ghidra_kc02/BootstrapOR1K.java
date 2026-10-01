import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.mem.Memory;
import java.util.TreeSet;

// Raw import base 0x01ffda00 maps file offset 0x2600 to CPU 0x02000000.
// The boot/header block is not mapped at its boot-time address in this model.
public class BootstrapOR1K extends GhidraScript {
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        mem.split(mem.getBlock(toAddr(0x01ffda00L)), toAddr(0x02000000L));
        mem.split(mem.getBlock(toAddr(0x02000000L)), toAddr(0x0207b800L));
        for (MemoryBlock b : mem.getBlocks()) {
            b.setRead(true);
            b.setWrite(false);
            b.setExecute(b.getStart().getOffset() == 0x02000000L);
            b.setName(b.isExecute() ? "app_text" :
                      b.getStart().getOffset() < 0x02000000L ? "boot_unmapped" : "app_data_assets");
        }
        TreeSet<Long> seeds = new TreeSet<>();
        seeds.add(0x02000000L);
        for (long a = 0x02000000L; a < 0x0207b800L; a += 4) {
            long w = mem.getInt(toAddr(a)) & 0xffffffffL;
            if (w >>> 26 == 1) {
                long disp = (w & 0x03ffffffL);
                if ((disp & 0x02000000L) != 0) disp -= 0x04000000L;
                long target = a + disp * 4;
                if (target >= 0x02000000L && target < 0x0207b800L) seeds.add(target);
            }
        }
        for (long a = 0x0207b908L; a < 0x0207b920L; a += 4)
            seeds.add(mem.getInt(toAddr(a)) & 0xffffffffL);
        int n = 0;
        for (long v : seeds) {
            Address a = toAddr(v);
            if (!mem.contains(a)) continue;
            disassemble(a);
            if (getFunctionAt(a) == null && createFunction(a, null) != null) n++;
        }
        String[][] labels = {
            {"0207b887", "str_DestBin_bin"}, {"0207b9e5", "str_SELFTEST_bin"},
            {"0207b99f", "str_build_version"}, {"0207bb60", "menu_coords"},
            {"0207bb78", "menu_assets"}, {"02096fb9", "str_fs_fat16"},
            {"02096fe9", "str_fs_fat32"}
        };
        for (String[] label : labels)
            createLabel(toAddr(Long.parseLong(label[0], 16)), label[1], true);
        println("OR1K direct-call candidate entries: " + seeds.size() + "; created: " + n);
        runScript("FixOR1KMemory.java");
        runScript("AnnotateOR1K.java");
    }
}
