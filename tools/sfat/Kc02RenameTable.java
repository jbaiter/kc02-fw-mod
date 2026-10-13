// Rename functions from a TSV of <address>\t<newname>. Additive: names and provenance only.
//@category KC02
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.File;
import java.util.Scanner;

public class Kc02RenameTable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs()[0];
        String note = getScriptArgs().length > 1 ? getScriptArgs()[1] : "";
        int done = 0, missing = 0;
        Scanner sc = new Scanner(new File(path));
        while (sc.hasNextLine()) {
            String line = sc.nextLine().trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\t");
            if (p.length < 2) continue;
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.decode(p[0]));
            Function f = getFunctionAt(a);
            if (f == null) { println("MISSING " + p[0]); missing++; continue; }
            String old = f.getName();
            f.setName(p[1], SourceType.USER_DEFINED);
            if (note != null && !note.isEmpty()) f.setComment(note);
            println("RENAMED " + p[0] + "  " + old + " -> " + f.getName());
            done++;
        }
        sc.close();
        println("REPORT: renamed=" + done + " missing=" + missing);
    }
}
