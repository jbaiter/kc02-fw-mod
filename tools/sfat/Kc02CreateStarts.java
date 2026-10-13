// Create functions at entry candidates discovered by or1k_function_starts.py.
//@category KC02
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.File;
import java.util.Scanner;

public class Kc02CreateStarts extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs().length > 0 ? getScriptArgs()[0]
                : "/tmp/kc02-board-re/newbuilds/starts.txt";
        int made = 0, tried = 0;
        Scanner sc = new Scanner(new File(path));
        while (sc.hasNextLine()) {
            String line = sc.nextLine().trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            long value = Long.decode(line);
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            tried++;
            if (getFunctionAt(a) == null) {
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
            }
            if (getFunctionAt(a) != null) made++;
        }
        sc.close();
        int n = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) { it.next(); n++; }
        println("REPORT: starts tried=" + tried + " now-functions=" + made + " total=" + n);
    }
}
