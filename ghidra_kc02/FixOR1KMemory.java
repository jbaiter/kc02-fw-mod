import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import java.io.ByteArrayInputStream;

// Separate physical resource bytes from runtime globals. App endpoint follows
// the documented physical SFAT boundary 0xd3200, less the app offset 0x2600.
// RAM upper bound is provisional: boot code at file 0x528 sets SP=0x027ffffc.
// Neither this model nor that stack value establishes usable/executable RAM.
public class FixOR1KMemory extends GhidraScript {
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        Address boundary = toAddr(0x020d0c00L);
        MemoryBlock mixed = mem.getBlock(boundary);
        if (mixed == null || !mixed.isInitialized())
            throw new IllegalStateException("Expected original initialized app/asset tail; inspect before rerun");
        mem.split(mixed, boundary);
        MemoryBlock tail = mem.getBlock(boundary);
        byte[] bytes = new byte[(int)tail.getSize()];
        mem.getBytes(boundary, bytes);
        MemoryBlock physical = mem.createInitializedBlock("physical_flash_tail",
            toAddr(0x000d3200L), new ByteArrayInputStream(bytes), bytes.length, monitor, true);
        physical.setRead(true); physical.setWrite(false); physical.setExecute(false);
        clearListing(tail.getStart(), tail.getEnd());
        mem.removeBlock(tail, monitor);
        MemoryBlock ram = mem.createUninitializedBlock("app_globals_heap_stack_PROVISIONAL",
            boundary, 0x02800000L - boundary.getOffset(), false);
        ram.setRead(true); ram.setWrite(true); ram.setExecute(false);
        for (MemoryBlock b : mem.getBlocks()) {
            if (b.getName().equals("app_data_assets")) {
                // This region includes initialized writable globals as well as strings.
                b.setName("app_initialized_data"); b.setWrite(true);
            }
        }
        println("Physical assets moved to overlay; runtime tail uninitialized. RAM bound is provisional.");
    }
}
