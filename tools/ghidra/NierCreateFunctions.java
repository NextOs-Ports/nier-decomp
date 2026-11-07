// Cria no Ghidra as funções do inventário (endereço e tamanho exatos).
// Argumento: arquivo texto com "endereço_hex tamanho" por linha.
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.util.*;

public class NierCreateFunctions extends GhidraScript {
  @Override public void run() throws Exception {
    List<String> lines = Files.readAllLines(Paths.get(getScriptArgs()[0]));
    AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
    int ok = 0, bad = 0;
    for (String l : lines) {
      String[] p = l.trim().split("\\s+"); if (p.length < 2) continue;
      long a = Long.parseLong(p[0], 16); long n = Long.parseLong(p[1]);
      Address s = sp.getAddress(a), e = sp.getAddress(a + n - 1);
      AddressSet body = new AddressSet(s, e);
      new DisassembleCommand(body, body, false).applyTo(currentProgram, monitor);
      try {
        Function f = currentProgram.getFunctionManager().getFunctionAt(s);
        if (f == null) f = currentProgram.getFunctionManager().createFunction(String.format("sub_%08X", a), s, body, SourceType.IMPORTED);
        else f.setBody(body);
        ok++;
      } catch (Exception ex) { bad++; }
      if ((ok + bad) % 2000 == 0) println("funções: " + ok + " erros: " + bad);
    }
    println("FIM funções: " + ok + " erros: " + bad);
  }
}
