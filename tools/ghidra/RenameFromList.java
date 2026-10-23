// Renames functions from a text file: "<hex address> <name>" per line, '#' comments.
// Headless counterpart of the MCP rename tool, for when Ghidra is closed. Evidence for each name
// belongs in notes/bindings.md, not here.
// Headless: pass the list file as the first script argument.
// @category PS2Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.nio.file.Files;

public class RenameFromList extends GhidraScript {

    @Override
    public void run() throws Exception {
        File listFile = askFile("Rename list", "Load");
        int renamed = 0, failed = 0;
        for (String raw : Files.readAllLines(listFile.toPath())) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            String[] parts = line.split("\\s+");
            Address addr = toAddr(Long.decode(parts[0]));
            Function func = getFunctionAt(addr);
            if (func == null) {
                printf("RENAME FAIL %s: no function starts here\n", addr);
                failed++;
                continue;
            }
            String old = func.getName();
            func.setName(parts[1], SourceType.USER_DEFINED);
            printf("RENAME %s %s -> %s\n", addr, old, parts[1]);
            renamed++;
        }
        printf("RenameFromList: renamed=%d failed=%d\n", renamed, failed);
    }
}
