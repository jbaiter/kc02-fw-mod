import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/* Run with analyzeHeadless -readOnly. Missing functions are created only in the
 * transient program; the shared project's listing, names and types are not saved. */
public class Kc02ExportCamera extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        Path out = Paths.get(args[0]); Files.createDirectories(out);
        LinkedHashSet<Long> entries = new LinkedHashSet<>();
        for (String line : Files.readAllLines(Paths.get(args[1]))) {
            if (!line.trim().isEmpty()) entries.add(Long.decode(line.trim()));
        }
        StringBuilder index = new StringBuilder("address\tname\tstatus\ttransient_created\tsignature\n");
        StringBuilder calls = new StringBuilder("caller\tcallsite\ttarget\ttarget_name\n");
        StringBuilder listing = new StringBuilder();
        DecompInterface di = new DecompInterface();
        di.setOptions(new DecompileOptions()); di.toggleCCode(true);
        di.openProgram(currentProgram);
        for (long e : entries) {
            monitor.checkCancelled();
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(e);
            Function f = getFunctionAt(a); boolean created = false;
            if (f == null && getFunctionContaining(a) == null) {
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                created = new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
                f = getFunctionAt(a);
            }
            if (f == null) {
                index.append(String.format("0x%08x\t?\tmissing_or_interior\tfalse\t\n", e)); continue;
            }
            di.flushCache();
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            String stem = String.format("%08x", e);
            String code = "/* Firmware decompilation; not buildable replacement source.\n * Address: 0x" + stem + " Name: " + f.getName() + "\n */\n";
            boolean ok = r.decompileCompleted() && r.getDecompiledFunction() != null;
            code += ok ? r.getDecompiledFunction().getC() : "/* FAILED: " + r.getErrorMessage() + " */\n";
            Files.writeString(out.resolve(stem + ".c"), code, StandardCharsets.UTF_8);
            index.append(String.format("0x%08x\t%s\t%s\t%s\t%s\n", e, f.getName(), ok ? "ok" : "failed", created, f.getPrototypeString(false,false)));
            listing.append(String.format("\n=== 0x%08x %s ===\n",e,f.getName()));
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(),true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                listing.append(ins.getAddress()).append("  ").append(ins.toString()).append('\n');
                if (ins.getFlowType().isCall()) for (Address target : ins.getFlows()) {
                    Function dest = getFunctionAt(target);
                    calls.append(String.format("0x%08x\t0x%08x\t0x%08x\t%s\n",e,ins.getAddress().getOffset(),target.getOffset(),dest==null?"?":dest.getName()));
                }
            }
            println("EXPORTED " + stem + " " + f.getName() + " " + (ok?"ok":"failed"));
        }
        di.dispose();
        Files.writeString(out.resolve("functions.tsv"),index.toString(),StandardCharsets.UTF_8);
        Files.writeString(out.resolve("calls.tsv"),calls.toString(),StandardCharsets.UTF_8);
        Files.writeString(out.resolve("disassembly.txt"),listing.toString(),StandardCharsets.UTF_8);
    }
}
