import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.BufferedReader;
import java.io.FileReader;

// Apply the replayable symbol manifest tools/ghidra_symbols.tsv to the program DB.
// ANNOTATIONS ONLY: names, labels, plate comments. Never touches bytes/memory blocks.
// Usage: -postScript ApplySymbolsOR1K.java <path-to-tsv>
public class ApplySymbolsOR1K extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) throw new IllegalArgumentException("need TSV path");
        int funs = 0, labels = 0, comments = 0;
        try (BufferedReader in = new BufferedReader(new FileReader(args[0]))) {
            String line;
            while ((line = in.readLine()) != null) {
                if (line.isBlank() || line.startsWith("#")) continue;
                String[] p = line.split("\t", -1);
                if (p.length < 3) throw new IllegalStateException("bad row: " + line);
                long a = Long.parseLong(p[0].trim(), 16);
                String kind = p[1].trim();
                String name = p[2].trim();
                String comment = p.length > 3 ? p[3].trim() : "";
                if (comment.isEmpty() && p.length > 3 && !p[3].isEmpty()) comment = p[3];
                var addr = toAddr(a);
                if (kind.equals("fun")) {
                    Function f = getFunctionAt(addr);
                    if (f == null) {
                        disassemble(addr);
                        f = createFunction(addr, name);
                    }
                    if (f == null) throw new IllegalStateException("no function at " + Long.toHexString(a));
                    f.setName(name, SourceType.USER_DEFINED);
                    if (!comment.isEmpty()) setPlateComment(addr, comment);
                    funs++;
                } else if (kind.equals("label") || kind.equals("data")) {
                    createLabel(addr, name, true);
                    if (!comment.isEmpty()) setPlateComment(addr, comment);
                    labels++;
                } else if (kind.equals("comment")) {
                    currentProgram.getListing().setComment(addr, CodeUnit.EOL_COMMENT, comment);
                    comments++;
                } else {
                    throw new IllegalStateException("unknown kind: " + kind);
                }
            }
        }
        println("Applied symbols: " + funs + " functions, " + labels + " labels, " + comments + " comments");
    }
}
