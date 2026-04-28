"use strict";

function scanMemory(addrStr) {
    if (!addrStr) {
        host.diagnostics.debugLog("Usage: .scriptrun memory_monitor.js \"0x...\"\n");
        return;
    }
    let base = host.evaluateExpression(addrStr);

    const control = host.namespace.Debugger.Utility.Control;
    control.ExecuteCommand(".echo --- Standard Analysis ---");

    for (let i = 0; i < 3; i++) {
        // Int64としてオフセットを計算
        let addr = base.add(host.Int64(i * 0x1000)).toString(16);
        
        // 出力を確実に表示させる
        host.diagnostics.debugLog("Analyzing: " + addr + "\n");
        for (let line of control.ExecuteCommand("!pte " + addr)) {
            host.diagnostics.debugLog(line + "\n");
            if (line.includes("---") && line.includes("W") && line.includes("E")) {
                host.diagnostics.debugLog("!!! WARNING: W+X Page Found at " + addr + " !!!\n");
            }
        }
        for (let line of control.ExecuteCommand("!vad " + addr + " 1")) {
            host.diagnostics.debugLog(line + "\n");
        }
    }
}

function initializeScript() {
    return [new host.functionAlias(scanMemory, "scan")];
}