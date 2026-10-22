// Ghidra headless script: dump disassembly, decompilation, referenced strings
// and callees for functions given by name or hex address.
//
//   analyzeHeadless PROJDIR PROJ -process scream.exe -noanalysis \
//       -postScript DumpFunctions.java OUTFILE DEPTH NAME_OR_ADDR...
//
// DEPTH > 0 also dumps callees, recursively, up to that depth.
// @category SCream

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.HashSet;
import java.util.Set;

public class DumpFunctions extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int depth = Integer.parseInt(args[1]);
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);

        Deque<Object[]> work = new ArrayDeque<>();
        for (int i = 2; i < args.length; i++) {
            Function f = lookup(args[i]);
            if (f == null) println("not found: " + args[i]);
            else work.add(new Object[] {f, 0});
        }
        Set<Address> done = new HashSet<>();
        try (PrintWriter w = new PrintWriter(new File(args[0]))) {
            while (!work.isEmpty()) {
                Object[] item = work.poll();
                Function f = (Function) item[0];
                int level = (Integer) item[1];
                if (!done.add(f.getEntryPoint())) continue;

                w.println("=".repeat(78));
                w.println(f.getName() + " @ " + f.getEntryPoint() + "  (depth " + level + ")");
                w.println("-- strings:");
                for (Instruction insn : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    for (Reference r : insn.getReferencesFrom()) {
                        Data d = getDataAt(r.getToAddress());
                        if (d != null && d.getValue() instanceof String)
                            w.println("   " + insn.getAddress() + "  \"" + d.getValue() + "\"");
                    }
                }
                w.println("-- disassembly:");
                for (Instruction insn : currentProgram.getListing().getInstructions(f.getBody(), true))
                    w.println("   " + insn.getAddress() + "  " + insn);
                w.println("-- decompiled:");
                DecompileResults r = dec.decompileFunction(f, 120, monitor);
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "   (failed)");

                if (level < depth)
                    for (Function callee : f.getCalledFunctions(monitor))
                        work.add(new Object[] {callee, level + 1});
            }
        }
        println("wrote " + done.size() + " functions to " + args[0]);
    }

    private Function lookup(String s) {
        for (Function f : currentProgram.getFunctionManager().getFunctions(true))
            if (f.getName().equals(s)) return f;
        try {
            Address a = toAddr(Long.parseLong(s.replaceFirst("^0x", ""), 16));
            Function f = getFunctionContaining(a);
            if (f != null) return f;
            // Code auto-analysis missed: start right after the preceding function
            // (16-byte aligned, as Watcom emits) and create one there.
            Function prev = getFunctionBefore(a);
            if (prev == null) return null;
            long start = (prev.getBody().getMaxAddress().getOffset() + 16) & ~15L;
            Address st = toAddr(start);
            disassemble(st);
            return createFunction(st, null);
        } catch (NumberFormatException e) {
            return null;
        }
    }
}
