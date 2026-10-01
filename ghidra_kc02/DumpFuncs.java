import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.*;
public class DumpFuncs extends GhidraScript {
	public void run() throws Exception {
		PrintWriter w=new PrintWriter(new FileWriter(getScriptArgs()[0]));
		int n=0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			w.println(String.format("%08x %6d %s", f.getEntryPoint().getOffset(), f.getBody().getNumAddresses(), f.getName()));
			n++;
		}
		w.close();
		println("functions exported: "+n);
	}
}
