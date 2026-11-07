// Exporta o pseudocódigo C do Ghidra de uma lista de endereços.
// Argumentos: <lista com endereço_hex por linha> <pasta de saída>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.util.*;

public class NierDecompile extends GhidraScript {
  @Override public void run() throws Exception {
    String[] args = getScriptArgs();
    List<String> lines = Files.readAllLines(Paths.get(args[0]));
    Path out = Paths.get(args[1]); Files.createDirectories(out);
    DecompInterface d = new DecompInterface();
    d.openProgram(currentProgram);
    int ok = 0;
    for (String l : lines) {
      String t = l.trim(); if (t.isEmpty()) continue;
      long a = Long.parseLong(t, 16);
      Function f = currentProgram.getFunctionManager().getFunctionAt(toAddr(a));
      if (f == null) { println("sem função " + t); continue; }
      DecompileResults r = d.decompileFunction(f, 60, monitor);
      String c = r.decompileCompleted() ? r.getDecompiledFunction().getC() : ("// falhou: " + r.getErrorMessage());
      Files.writeString(out.resolve(String.format("sub_%08X.c", a)), c);
      ok++;
    }
    d.dispose();
    println("FIM decompiladas: " + ok);
  }
}
