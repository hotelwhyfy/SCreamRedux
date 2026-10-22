// Ghidra headless script: for each address, list the instructions that read or
// write it, with the containing function, and decompile those functions.
//
//   analyzeHeadless PROJDIR PROJ -process scream.exe -noanalysis \
//       -postScript XRefs.java OUTFILE ADDR...
// @category SCream

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class XRefs extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new File(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                Address target = toAddr(Long.parseLong(args[i].replaceFirst("^0x", ""), 16));
                w.println("#".repeat(78));
                w.println("references to " + target);
                Set<Function> funcs = new LinkedHashSet<>();
                for (Reference r : getReferencesTo(target)) {
                    Instruction insn = getInstructionAt(r.getFromAddress());
                    Function f = getFunctionContaining(r.getFromAddress());
                    w.printf("   %s %-6s %-40s in %s%n", r.getFromAddress(), r.getReferenceType(),
                             insn == null ? "" : insn.toString(), f == null ? "?" : f.getName());
                    if (f != null) funcs.add(f);
                }
                for (Function f : funcs) {
                    w.println("-- " + f.getName() + " @ " + f.getEntryPoint());
                    DecompileResults r = dec.decompileFunction(f, 120, monitor);
                    w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "   (failed)");
                }
            }
        }
    }
}
