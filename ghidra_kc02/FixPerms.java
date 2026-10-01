import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;
public class FixPerms extends GhidraScript {
	public void run() throws Exception {
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) { b.setRead(true); b.setWrite(true); b.setExecute(true); }
	}
}
