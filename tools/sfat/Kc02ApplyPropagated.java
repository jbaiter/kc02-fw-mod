// Apply propagated names: TSV of original_addr<TAB>target_addr<TAB>name<TAB>size
//@category KC02
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.File;
import java.util.Scanner;

public class Kc02ApplyPropagated extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs()[0];
        int applied = 0, missing = 0, skipped = 0;
        Scanner sc = new Scanner(new File(path));
        while (sc.hasNextLine()) {
            String line = sc.nextLine().trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\t");
            if (p.length < 3) continue;
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.decode(p[1]));
            Function f = getFunctionAt(a);
            if (f == null) { missing++; continue; }
            if (!f.getName().startsWith("FUN_")) { skipped++; continue; }
            try {
                f.setName(p[2], SourceType.USER_DEFINED);
                f.setComment("name propagated from the original KC02 build by masked-opcode match");
                applied++;
            } catch (Exception e) { skipped++; }
        }
        sc.close();
        println("REPORT: applied=" + applied + " missing=" + missing + " skipped=" + skipped);
    }
}
