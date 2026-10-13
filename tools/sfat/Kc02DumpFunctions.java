// Dump every function as TSV: program<TAB>entry<TAB>name<TAB>size
//@category KC02
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.PrintWriter;

public class Kc02DumpFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0]
                : "/tmp/kc02-board-re/newbuilds/functions.tsv";
        PrintWriter w = new PrintWriter(new java.io.FileWriter(out, true));
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0;
        while (it.hasNext()) {
            Function f = it.next();
            try {
                long size = f.getBody().getNumAddresses();
                w.println(currentProgram.getName() + "\t" + f.getEntryPoint() + "\t"
                        + f.getName() + "\t" + size);
                n++;
            } catch (Exception e) { }
        }
        w.close();
        println("REPORT: dumped " + n + " functions from " + currentProgram.getName() + " to " + out);
    }
}
