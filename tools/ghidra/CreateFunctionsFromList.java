// Creates functions at addresses listed in a text file (one hex address per line, '#' comments).
// Used to define code only reached through vtables/function pointers, which auto-analysis misses.
// Skips addresses that already start a function or that do not disassemble cleanly.
// Headless: pass the list file as the first script argument.
// @category PS2Recomp

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.File;
import java.nio.file.Files;
import java.util.List;

public class CreateFunctionsFromList extends GhidraScript {

    @Override
    public void run() throws Exception {
        File listFile = askFile("Address list", "Load");
        List<String> lines = Files.readAllLines(listFile.toPath());

        int created = 0, existing = 0, failed = 0;
        for (String raw : lines) {
            if (monitor.isCancelled()) {
                break;
            }
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            Address addr = toAddr(Long.decode(line));

            if (getFunctionAt(addr) != null) {
                existing++;
                continue;
            }

            Instruction insn = getInstructionAt(addr);
            if (insn == null) {
                // getDataContaining() also returns placeholder data for undefined bytes; only real
                // (defined) data should block disassembly.
                ghidra.program.model.listing.Data data = getDataContainingAt(addr);
                if (data != null && data.isDefined()) {
                    printf("SKIP %s: defined data\n", addr);
                    failed++;
                    continue;
                }
                DisassembleCommand dis = new DisassembleCommand(addr, null, true);
                if (!dis.applyTo(currentProgram, monitor) || getInstructionAt(addr) == null) {
                    printf("SKIP %s: does not disassemble\n", addr);
                    failed++;
                    continue;
                }
            }

            // An address inside an existing function body would split it; report instead.
            Function containing = getFunctionContaining(addr);
            if (containing != null) {
                printf("SKIP %s: inside %s\n", addr, containing.getName());
                failed++;
                continue;
            }

            CreateFunctionCmd cmd = new CreateFunctionCmd(addr);
            if (cmd.applyTo(currentProgram, monitor)) {
                created++;
            } else {
                printf("SKIP %s: %s\n", addr, cmd.getStatusMsg());
                failed++;
            }
        }
        printf("CreateFunctionsFromList: created=%d existing=%d skipped=%d\n", created, existing, failed);
    }

    private ghidra.program.model.listing.Data getDataContainingAt(Address addr) {
        return currentProgram.getListing().getDataContaining(addr);
    }
}
