//go:build js && wasm

package main

import (
	"bytes"
	"fmt"
	"sync"
	"syscall/js"

	"tcltalk/pkg/ffi"
	"tcltalk/pkg/lexer"
	"tcltalk/pkg/parser"
	"tcltalk/pkg/runtime"
)

var (
	vm        *runtime.VM
	ffiEngine *ffi.Engine
	outBuf    bytes.Buffer
	mu        sync.Mutex
)

func initVM() {
	mu.Lock()
	defer mu.Unlock()

	outBuf.Reset()
	vm = runtime.NewVM()
	vm.Output = &outBuf

	ffiEngine = ffi.NewEngine()
	ffiEngine.Output = &outBuf

	// Custom Web FFI functions
	defaultHandler := ffiEngine.HandleFFI
	vm.FFIHandler = func(name string, args []runtime.Value, v *runtime.VM) (runtime.Value, error) {
		switch name {
		case "alert":
			if len(args) > 0 {
				js.Global().Call("alert", args[0].String())
			}
			return runtime.NilVal{}, nil
		case "log":
			if len(args) > 0 {
				js.Global().Get("console").Call("log", args[0].String())
			}
			return runtime.NilVal{}, nil
		default:
			return defaultHandler(name, args, v)
		}
	}
}

func tickEval(this js.Value, p []js.Value) interface{} {
	if len(p) < 1 {
		return map[string]interface{}{
			"success": false,
			"error":   "missing source code argument",
			"result":  "",
			"output":  "",
		}
	}

	source := p[0].String()

	mu.Lock()
	defer mu.Unlock()

	outBuf.Reset()

	lex := lexer.NewLexer(source)
	tokens, err := lex.Tokenize()
	if err != nil {
		return map[string]interface{}{
			"success": false,
			"error":   fmt.Sprintf("Lexer error: %v", err),
			"result":  "",
			"output":  outBuf.String(),
		}
	}

	prs := parser.NewParser(tokens)
	prog, err := prs.ParseProgram()
	if err != nil {
		return map[string]interface{}{
			"success": false,
			"error":   fmt.Sprintf("Parser error: %v", err),
			"result":  "",
			"output":  outBuf.String(),
		}
	}

	val, err := vm.EvalProgram(prog)
	if err != nil {
		return map[string]interface{}{
			"success": false,
			"error":   fmt.Sprintf("Runtime error: %v", err),
			"result":  "",
			"output":  outBuf.String(),
		}
	}

	resStr := ""
	if val != nil && val.Type() != "nil" {
		resStr = val.String()
	}

	return map[string]interface{}{
		"success": true,
		"error":   "",
		"result":  resStr,
		"output":  outBuf.String(),
	}
}

func tickReset(this js.Value, p []js.Value) interface{} {
	initVM()
	return map[string]interface{}{
		"success": true,
		"message": "Tick VM reset successfully",
	}
}

func tickVersion(this js.Value, p []js.Value) interface{} {
	return "Tick 1.0.0 (WebAssembly / Go runtime)"
}

func main() {
	initVM()

	// Export functions to JavaScript global scope
	js.Global().Set("tickEval", js.FuncOf(tickEval))
	js.Global().Set("tickReset", js.FuncOf(tickReset))
	js.Global().Set("tickVersion", js.FuncOf(tickVersion))
	js.Global().Set("tickReady", true)

	// Notify window that Tick WASM runtime is loaded and ready
	js.Global().Call("dispatchEvent", js.Global().Get("CustomEvent").New("tick-ready"))

	// Keep the Go runtime alive
	select {}
}
