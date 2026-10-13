// Replay name + plate comment from a manifest slice, appending a provenance note rather than
// replacing whatever evidence comment is already there.
//@category KC02
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.File;
import java.util.Scanner;

public class Kc02ReplayNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs()[0];
        String note = getScriptArgs().length > 1 ? getScriptArgs()[1] : null;
        int done = 0;
        Scanner sc = new Scanner(new File(path));
        while (sc.hasNextLine()) {
            String line = sc.nextLine().trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\t");
            if (p.length < 3) continue;
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.decode("0x" + p[0]));
            Function f = getFunctionAt(a);
            if (f == null) { println("MISSING " + p[0]); continue; }
            String existing = f.getComment();
            f.setName(p[2], SourceType.USER_DEFINED);
            String body = p.length > 3 && !p[3].isEmpty() ? p[3] : "";
            if (existing != null && !existing.isEmpty() && !body.contains(existing)) {
                body = existing + "\n" + body;      // keep whatever was already there
            }
            if (note != null) body = body + "\n" + note;
            f.setComment(body);
            println("SET " + p[0] + " -> " + f.getName() + "  (" + body.length() + " char comment)");
            done++;
        }
        sc.close();
        println("REPORT: replayed=" + done);
    }
}
