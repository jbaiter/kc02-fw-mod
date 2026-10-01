// ApplyKC02Symbols.java - replay tools/ghidra_symbols.tsv into a Ghidra program.
//
// Reads the KC02 symbol manifest and applies it as ANNOTATIONS ONLY:
//   kind=fun      -> rename the function at <addr> (create it if absent)
//   kind=label    -> create a USER_DEFINED label at <addr>
//   kind=comment  -> add a comment (column 3 = EOL|PRE|PLATE|REPEATABLE)
// followed by the comment in column 4 for fun/label kinds.
//
// It never writes bytes, never creates memory blocks, never changes analysis
// options and never runs an analyzer.
//
// Usage (headless):
//   analyzeHeadless <projectDir> <projectName> -process <program> \
//       -noanalysis -scriptPath tools \
//       -postScript ApplyKC02Symbols.java tools/ghidra_symbols.tsv
//
//@category KC02

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.FileReader;
import java.nio.file.Path;
import java.nio.file.Paths;

public class ApplyKC02Symbols extends GhidraScript {

    private Address at(long value) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
    }

    private void setComment(Address a, String type, String text) throws Exception {
        if (text == null || text.isEmpty()) {
            return;
        }
        int kind;
        switch (type.toUpperCase()) {
            case "EOL":        kind = CodeUnit.EOL_COMMENT; break;
            case "PRE":        kind = CodeUnit.PRE_COMMENT; break;
            case "PLATE":      kind = CodeUnit.PLATE_COMMENT; break;
            case "REPEATABLE": kind = CodeUnit.REPEATABLE_COMMENT; break;
            default:
                println("ApplyKC02Symbols: unknown comment type '" + type + "' at 0x"
                        + a + " - skipping comment");
                return;
        }
        setPlateComment(a, null); // no-op keeper so the API import is used
        if (kind == CodeUnit.PLATE_COMMENT) {
            setPlateComment(a, text);
        } else if (kind == CodeUnit.EOL_COMMENT) {
            setEOLComment(a, text);
        } else if (kind == CodeUnit.PRE_COMMENT) {
            setPreComment(a, text);
        } else {
            setRepeatableComment(a, text);
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("ApplyKC02Symbols: usage: ApplyKC02Symbols.java <ghidra_symbols.tsv>");
            return;
        }
        Path tsv = Paths.get(args[0]);
        println("ApplyKC02Symbols: reading " + tsv);

        int applied = 0, skipped = 0, malformed = 0;
        try (BufferedReader in = new BufferedReader(new FileReader(tsv.toFile()))) {
            String line;
            while ((line = in.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                String[] f = line.split("\t", -1);
                if (f.length != 4) {
                    println("ApplyKC02Symbols: malformed record (want 4 TAB fields): " + line);
                    malformed++;
                    continue;
                }
                long addr = Long.parseLong(f[0].trim(), 16);
                String kind = f[1].trim();
                String name = f[2].trim();
                String comment = f[3];
                Address a = at(addr);
                try {
                    if (kind.equals("fun")) {
                        Function fn = getFunctionAt(a);
                        if (fn == null) {
                            fn = createFunction(a, name);
                        } else {
                            fn.setName(name, SourceType.USER_DEFINED);
                        }
                        setComment(a, "PLATE", comment);
                        applied++;
                    } else if (kind.equals("label")) {
                        currentProgram.getSymbolTable()
                                .createLabel(a, name, SourceType.USER_DEFINED);
                        setComment(a, "EOL", comment);
                        applied++;
                    } else if (kind.equals("comment")) {
                        setComment(a, name, comment);
                        applied++;
                    } else {
                        println("ApplyKC02Symbols: unknown kind '" + kind + "' for 0x"
                                + f[0]);
                        malformed++;
                    }
                } catch (Exception exc) {
                    println("ApplyKC02Symbols: FAILED 0x" + f[0] + " (" + kind + " "
                            + name + "): " + exc.getMessage());
                    skipped++;
                }
            }
        }
        println("ApplyKC02Symbols: " + applied + " applied, " + skipped + " skipped, "
                + malformed + " malformed");
        // Annotations only.  Do NOT call currentProgram.save() here: the
        // script body runs inside the analyzer's transaction, and saving from
        // inside it fails with "Unable to lock due to active transaction"
        // (Ghidra 12.1.4).  Headless saves the program itself when the script
        // finishes ("REPORT: Save succeeded for processed file"); in the GUI
        // the transaction wrapper commits the changes.
        println("ApplyKC02Symbols: annotations applied (the analyzer commits them)");
    }
}
