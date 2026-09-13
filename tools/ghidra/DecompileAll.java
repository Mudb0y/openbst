// Exports decompiled C for every function in the program, one file per
// function, named by entry address. Reading 300-odd fixed-point routines as
// assembly is the slow path; this makes the rest of the engine legible.
//
// The text preprocessor's handlers are reached only through a table of
// function pointers, so nothing refers to them and the analyser leaves them
// as loose bytes. The table is walked first and a function created at each
// entry, otherwise a third of the front end has no decompilation at all.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryAccessException;
import java.io.*;

public class DecompileAll extends GhidraScript {
    private void seedTable(long tableVa, int stride, int slot) {
        Address a = toAddr(tableVa);
        for (int i = 0; i < 4096; i++) {
            try {
                long p = currentProgram.getMemory().getInt(a.add((long) i * stride + slot))
                         & 0xffffffffL;
                if (p < 0x10001000L || p >= 0x10019000L) break;
                Address f = toAddr(p);
                if (getFunctionAt(f) == null) {
                    disassemble(f);
                    createFunction(f, null);
                }
            } catch (MemoryAccessException e) {
                break;
            }
        }
    }

    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs().length > 0 ? getScriptArgs()[0] : "/tmp/decomp";
        new File(outDir).mkdirs();

        seedTable(0x10021748L, 12, 4);

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
