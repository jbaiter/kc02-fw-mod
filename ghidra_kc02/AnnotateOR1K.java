import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

// Analysis-database changes only. Signatures use observed argument registers,
// not decompiler guesses. Do not interpret these as a complete silicon ABI.
public class AnnotateOR1K extends GhidraScript {
    private final DataType U32 = UnsignedIntegerDataType.dataType;
    private final DataType I32 = IntegerDataType.dataType;
    private final DataType VOID = VoidDataType.dataType;
    private final DataType PTR = new PointerDataType(VoidDataType.dataType, 4);
    private final DataType STR = new PointerDataType(CharDataType.dataType, 4);
    private final DataType U32PTR = new PointerDataType(UnsignedIntegerDataType.dataType, 4);

    private void sig(long address, String name, DataType result, DataType... types) throws Exception {
        Function f = getFunctionAt(toAddr(address));
        if (f == null) {
            disassemble(toAddr(address));
            f = createFunction(toAddr(address), name);
        }
        if (f == null) throw new IllegalStateException("No function at " + Long.toHexString(address));
        f.setName(name, SourceType.USER_DEFINED);
        Parameter[] params = new Parameter[types.length];
        for (int i = 0; i < types.length; i++) {
            params[i] = new ParameterImpl("arg" + (i + 1), types[i],
                new VariableStorage(currentProgram, currentProgram.getRegister("r" + (i + 3))), currentProgram);
        }
        VariableStorage storage = result == VOID ? VariableStorage.VOID_STORAGE :
            new VariableStorage(currentProgram, currentProgram.getRegister("r11"));
        f.updateFunction("default", new ReturnParameterImpl(result, storage, currentProgram),
            Function.FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED, params);
        println(Long.toHexString(address) + " " + f.getSignature());
    }

    public void run() throws Exception {
        // Initialized data contains mutable globals too (e.g. 0x020c8a38).
        // Read-only permissions here caused invalid constant folding.
        var data = currentProgram.getMemory().getBlock(toAddr(0x0207b800L));
        if (data == null) throw new IllegalStateException("Missing application data");
        data.setName("app_initialized_data");
        data.setWrite(true);
        sig(0x020782b0L, "kc02_memcpy", PTR, PTR, PTR, U32);
        sig(0x020785a8L, "kc02_memset", PTR, PTR, U32, U32);
        sig(0x0206c5b8L, "kc02_file_open", I32, STR, U32);
        sig(0x0206c680L, "kc02_file_close", I32, I32);
        sig(0x0206c6f8L, "kc02_file_read", I32, I32, PTR, U32);
        sig(0x0206c770L, "kc02_file_write", I32, I32, PTR, U32);
        sig(0x0206c7e8L, "kc02_file_seek_absolute", I32, I32, I32);
        sig(0x0206caf8L, "kc02_file_size32", U32, I32);
        sig(0x02071440L, "kc02_fatfs_read", U32, PTR, PTR, U32, U32PTR);
        sig(0x0207174cL, "kc02_fatfs_write", U32, PTR, PTR, U32, U32PTR);
        // Native seek receives sign-extended offset as separate low/high words.
        sig(0x02071da4L, "kc02_fatfs_seek_words", U32, PTR, U32, U32);
        sig(0x0203d38cL, "kc02_spi_read", U32, U32, PTR, U32);
        sig(0x0203d198L, "kc02_spi_read_dma", VOID, PTR, U32);
        sig(0x0204c404L, "kc02_usb_receive_to_memory", VOID, PTR, U32);
        sig(0x0204c4a0L, "kc02_usb_send_from_memory", VOID, PTR, U32);
        sig(0x0204c52cL, "kc02_usb_debug_read_memory", VOID);
        sig(0x0204c5a4L, "kc02_usb_debug_write_memory", VOID);
        sig(0x0204c624L, "kc02_usb_debug_memory_transfer", VOID);
        sig(0x0204c660L, "kc02_usb_vendor_call", VOID);
        sig(0x0204ce08L, "kc02_usb_parse_cbw", U32);
        sig(0x0204cbb8L, "kc02_usb_dispatch_scsi", U32);
        println("Annotated observed APIs; initialized data is writable for analysis.");
    }
}
