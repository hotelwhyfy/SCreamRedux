// Ghidra headless script: locate every sc* function registered with Lua by
// scream.exe and write its decompilation to OUTDIR/<name>.c, plus an index.
//
//   analyzeHeadless PROJDIR PROJ -import scream.exe -postScript ExportScApi.java OUTDIR NAMES.txt
//
// Each name in NAMES.txt is located by its bytes (short ones like "scH" are not
// auto-detected as strings). Registration is then found from references to it:
// either a luaL_Reg-style table ({char* name, lua_CFunction fn} pairs) or code
// that loads the function address shortly before referencing the name. The exe
// is built with Open Watcom's register convention, so lua_pushcclosure(L, fn, n)
// takes fn in EDX; call targets are skipped so the callee isn't mistaken for fn.
// @category SCream

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.util.CodeUnitInsertionException;

import java.io.File;
import java.io.PrintWriter;
import java.util.Map;
import java.util.TreeMap;

public class ExportScApi extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "sc_api");
        out.mkdirs();

        java.util.List<String> names = java.nio.file.Files.readAllLines(new File(args[1]).toPath());
        Map<String, Function> found = new TreeMap<>();
        for (String name : names) {
            name = name.trim();
            if (name.isEmpty()) continue;
            byte[] pattern = (name + "\0").getBytes("US-ASCII");
            Address at = currentProgram.getMinAddress();
            while (at != null && !found.containsKey(name)) {
                at = currentProgram.getMemory().findBytes(at, pattern, null, true, monitor);
                if (at == null) break;
                Function f = resolve(at);
                if (f != null) found.put(name, f);
                at = at.add(1);
            }
            if (!found.containsKey(name)) println("not found: " + name);
        }

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        try (PrintWriter index = new PrintWriter(new File(out, "index.txt"))) {
            for (Map.Entry<String, Function> e : found.entrySet()) {
                Function f = e.getValue();
                f.setName(e.getKey(), ghidra.program.model.symbol.SourceType.USER_DEFINED);
                index.printf("%-22s %s%n", e.getKey(), f.getEntryPoint());
            }
            // Second pass after renaming, so calls between sc* functions read by name.
            for (Map.Entry<String, Function> e : found.entrySet()) {
                DecompileResults r = dec.decompileFunction(e.getValue(), 120, monitor);
                try (PrintWriter w = new PrintWriter(new File(out, e.getKey() + ".c"))) {
                    w.println("// " + e.getKey() + " @ " + e.getValue().getEntryPoint());
                    w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
                }
            }
        }
        println("exported " + found.size() + " sc* functions to " + out);
    }

    // Find the C function registered under the name string at `str`.
    private Function resolve(Address str) throws Exception {
        for (Reference ref : getReferencesTo(str)) {
            Address from = ref.getFromAddress();
            Instruction insn = getInstructionAt(from);
            if (insn == null) {
                // Data reference: assume a {name, fn} table and read the next pointer.
                Address fnPtr = from.add(4);
                Function f = functionAtPointer(fnPtr);
                if (f != null) return f;
                continue;
            }
            // Code reference: scan back a few instructions for a pushed function address.
            Instruction p = insn;
            for (int i = 0; i < 8 && p != null; i++) {
                p = p.getPrevious();
                if (p == null) break;
                if (p.getFlowType().isCall() || p.getFlowType().isJump()) continue;
                for (int op = 0; op < p.getNumOperands(); op++) {
                    for (Object o : p.getOpObjects(op)) {
                        Address a = null;
                        if (o instanceof Address) a = (Address) o;
                        else if (o instanceof Scalar) a = toAddr(((Scalar) o).getUnsignedValue());
                        if (a == null || a.equals(str)) continue;
                        Function f = getFunctionAt(a);
                        if (f == null && currentProgram.getMemory().getBlock(a) != null
                                && currentProgram.getMemory().getBlock(a).isExecute()) {
                            disassemble(a);
                            f = createFunction(a, null);
                        }
                        if (f != null) return f;
                    }
                }
            }
        }
        return null;
    }

    private Function functionAtPointer(Address p) throws Exception {
        try {
            long v = getInt(p) & 0xffffffffL;
            Address a = toAddr(v);
            if (currentProgram.getMemory().getBlock(a) == null || !currentProgram.getMemory().getBlock(a).isExecute())
                return null;
            Function f = getFunctionAt(a);
            if (f == null) {
                disassemble(a);
                f = createFunction(a, null);
            }
            return f;
        } catch (Exception e) {
            return null;
        }
    }
}
