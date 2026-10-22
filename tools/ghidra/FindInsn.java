// Ghidra headless script: list every instruction whose text matches a regex,
// with its containing function.
//
//   analyzeHeadless PROJDIR PROJ -process scream.exe -noanalysis \
//       -postScript FindInsn.java OUTFILE REGEX...
// @category SCream

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.File;
import java.io.PrintWriter;
import java.util.regex.Pattern;

public class FindInsn extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter w = new PrintWriter(new File(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                Pattern p = Pattern.compile(args[i]);
                w.println("# " + args[i]);
                for (Instruction insn : currentProgram.getListing().getInstructions(true)) {
                    String text = insn.toString();
                    if (!p.matcher(text).find()) continue;
                    Function f = getFunctionContaining(insn.getAddress());
                    w.printf("   %s  %-44s in %s%n", insn.getAddress(), text, f == null ? "?" : f.getName());
                }
            }
        }
    }
}
