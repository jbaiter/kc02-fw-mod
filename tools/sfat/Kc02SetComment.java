// Set <addr>\t<name>\t<comment> exactly (no retention). For tidying a known comment.
//@category KC02
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.File;
import java.util.Scanner;

public class Kc02SetComment extends GhidraScript {
    @Override
    public void run() throws Exception {
        Scanner sc = new Scanner(new File(getScriptArgs()[0]));
        int n = 0;
        while (sc.hasNextLine()) {
            String line = sc.nextLine();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\t", 3);
            if (p.length < 3) continue;
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.decode("0x" + p[0]));
            Function f = getFunctionAt(a);
            if (f == null) { println("MISSING " + p[0]); continue; }
            f.setName(p[1], SourceType.USER_DEFINED);
            f.setComment(p[2]);
            n++;
        }
        sc.close();
        println("REPORT: tidied=" + n);
    }
}
