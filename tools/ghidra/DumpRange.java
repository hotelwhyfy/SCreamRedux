// Ghidra headless script: disassemble an address range, also outside known
// functions (e.g. the mouse handler around 0x4172a0).
//
//   analyzeHeadless PROJDIR PROJ -process scream.exe -noanalysis \
//       -postScript DumpRange.java OUTFILE START END [START END ...]
// @category SCream

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Instruction;
import java.io.File;
import java.io.PrintWriter;

public class DumpRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter w = new PrintWriter(new File(args[0]))) {
            for (int i = 1; i + 1 < args.length; i += 2) {
                Address start = toAddr(Long.decode(args[i]));
                Address end = toAddr(Long.decode(args[i + 1]));
                new DisassembleCommand(new AddressSet(start, end), null, true).applyTo(currentProgram);
                w.println("# " + start + " .. " + end);
                Instruction ins = getInstructionAt(start);
                if (ins == null) ins = getInstructionAfter(start);
                while (ins != null && ins.getAddress().compareTo(end) <= 0) {
                    w.printf("   %s  %s%n", ins.getAddress(), ins);
                    ins = ins.getNext();
                }
            }
        }
    }
}
