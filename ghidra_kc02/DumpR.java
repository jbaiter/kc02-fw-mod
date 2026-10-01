import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;

public class DumpR extends GhidraScript {
	@Override
	public void run() throws Exception {
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			b.setRead(true); b.setExecute(true);
		}
		long start = Long.parseLong(getScriptArgs()[0], 16);
		long end = Long.parseLong(getScriptArgs()[1], 16);
		Address a = toAddr(start);
		clearListing(a);
		disassemble(a);
		InstructionIterator it = currentProgram.getListing().getInstructions(a, true);
		int n = 0;
		while (it.hasNext() && n < 600) {
			Instruction ins = it.next();
			if (ins.getMinAddress().getOffset() >= end) break;
			println(String.format("%08x  %s", ins.getMinAddress().getOffset(), ins.toString()));
			n++;
		}
		println("printed " + n);
	}
}
