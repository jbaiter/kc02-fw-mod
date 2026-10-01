// DumpShim.java - throwaway Ghidra postScript for the KC02 shim round-trip check.
//
// Usage (from tools/verify_shim_ghidra.sh):
//   analyzeHeadless <proj> pi-ghidra -import <blob> \
//       -processor KC02:LE:32:nodelay -baseAddr <addr> -noanalysis \
//       -scriptPath tools -postScript DumpShim.java <addr> <count> <outfile>
//
// Disassembles <count> instructions starting at <addr> and writes one TSV line
// per instruction: 0xADDR \t mnemonic \t text \t hexbytes
// A MISSING marker is written if Ghidra cannot decode an instruction there.
//
// This script only reads the imported blob. It never touches the firmware
// image or the main analysis cache.

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

public class DumpShim extends GhidraScript {

    private static String hex(byte[] bytes) {
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) {
            sb.append(String.format("%02x", b & 0xff));
        }
        return sb.toString();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            throw new IllegalArgumentException("expected: <addr> <count> <outfile>");
        }
        long base = Long.parseLong(args[0].replace("0x", ""), 16);
        int count = Integer.parseInt(args[1]);
        File out = new File(args[2]);

        // Disassemble every word in the window before dumping it.
        for (int i = 0; i < count; i++) {
            Address a = toAddr(base + 4L * i);
            if (getInstructionAt(a) == null) {
                disassemble(a);
            }
        }

        // Create a function symbol over the shim so the read-only pi-ghidra
        // disassemble action (which resolves by function) can re-read it.
        if (getFunctionAt(toAddr(base)) == null) {
            createFunction(toAddr(base), "kc02_spi_read_shim");
        }

        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {            for (int i = 0; i < count; i++) {
                long addrValue = base + 4L * i;
                Address a = toAddr(addrValue);
                Instruction insn = getInstructionAt(a);
                if (insn == null) {
                    w.println("MISSING\t" + String.format("0x%08x", addrValue));
                    continue;
                }
                w.println(String.format(
                        "0x%08x\t%s\t%s\t%s",
                        insn.getAddress().getOffset(),
                        insn.getMnemonicString(),
                        insn.toString(),
                        hex(insn.getBytes())));
            }
        }
        println("DumpShim wrote " + count + " instructions from "
                + String.format("0x%08x", base) + " to " + out.getAbsolutePath());
    }
}
