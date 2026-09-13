// Exports decompiled C for every function in the program, one file per
// function, named by entry address. Reading 300-odd fixed-point routines as
// assembly is the slow path; this makes the rest of the engine legible.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DecompileAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : "/tmp/decomp";
        new File(outDir).mkdirs();

        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        d.setSimplificationStyle("decompile");

        int n = 0, fail = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            DecompileResults r = d.decompileFunction(f, 60, monitor);
            String addr = f.getEntryPoint().toString();
            if (r != null && r.decompileCompleted()) {
                PrintWriter w = new PrintWriter(new File(outDir, addr + ".c"));
                w.println("/* " + f.getName() + " at " + addr + " */");
                w.println(r.getDecompiledFunction().getC());
                w.close();
                n++;
            } else fail++;
        }
        println("decompiled " + n + " functions, " + fail + " failed, into " + outDir);
    }
}
