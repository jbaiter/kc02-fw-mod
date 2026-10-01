import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.FunctionManager;
import java.nio.file.*;
public class SeedFuncs extends GhidraScript {
	public void run() throws Exception {
		FunctionManager fm = currentProgram.getFunctionManager();
		int n=0, created=0;
		for (String line : Files.readAllLines(Paths.get("/tmp/kc02_seeds.txt"))) {
			line=line.trim(); if (line.isEmpty()) continue; n++;
			Address a=toAddr(Long.parseLong(line.replace("0x",""),16));
			if (fm.getFunctionContaining(a)==null) {
				try { if (createFunction(a, null) != null) created++; } catch(Exception e){}
			}
		}
		println("seeds="+n+" functionsCreated="+created);
	}
}
