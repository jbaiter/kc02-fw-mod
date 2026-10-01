import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class Decomp extends GhidraScript {
	@Override
	public void run() throws Exception {
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		for (String a : getScriptArgs()) {
			Address ad = toAddr(Long.parseLong(a, 16));
			Function f = getFunctionAt(ad);
			if (f == null) {
				disassemble(ad);
				f = createFunction(ad, null);
			}
			if (f == null) { println("NO FUNC at " + a); continue; }
			println("==== " + f.getName() + " @" + f.getEntryPoint() + " size=" + f.getBody().getNumAddresses());
			DecompileResults r = di.decompileFunction(f, 60, monitor);
			if (r != null && r.decompileCompleted()) println(r.getDecompiledFunction().getC());
			else println("DECOMP FAIL: " + (r != null ? r.getErrorMessage() : "null"));
		}
	}
}
