package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strings"

	"tcltalk/pkg/ffi"
	"tcltalk/pkg/lexer"
	"tcltalk/pkg/parser"
	"tcltalk/pkg/runtime"
)

func main() {
	evalFlag := flag.String("e", "", "evaluate inline Tick code")
	flag.Parse()

	vm := runtime.NewVM()
	ffiEngine := ffi.NewEngine()
	vm.FFIHandler = ffiEngine.HandleFFI

	if *evalFlag != "" {
		if err := runCode(vm, *evalFlag); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		return
	}

	args := flag.Args()
	if len(args) > 0 {
		filePath := args[0]
		content, err := os.ReadFile(filePath)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to read file %s: %v\n", filePath, err)
			os.Exit(1)
		}
		if err := runCode(vm, string(content)); err != nil {
			fmt.Fprintf(os.Stderr, "Error: %v\n", err)
			os.Exit(1)
		}
		return
	}

	// Interactive REPL
	runREPL(vm)
}

func runCode(vm *runtime.VM, source string) error {
	lex := lexer.NewLexer(source)
	tokens, err := lex.Tokenize()
	if err != nil {
		return fmt.Errorf("lexer error: %w", err)
	}

	p := parser.NewParser(tokens)
	prog, err := p.ParseProgram()
	if err != nil {
		return fmt.Errorf("parser error: %w", err)
	}

	_, err = vm.EvalProgram(prog)
	if err != nil {
		return fmt.Errorf("runtime error: %w", err)
	}
	return nil
}

func runREPL(vm *runtime.VM) {
	fmt.Println("Tick Interactive Console (Go implementation)")
	fmt.Println("Type exit or press Ctrl+C to exit.")
	scanner := bufio.NewScanner(os.Stdin)

	for {
		fmt.Print("tick> ")
		if !scanner.Scan() {
			break
		}
		line := strings.TrimSpace(scanner.Text())
		if line == "exit" || line == "quit" {
			break
		}
		if line == "" {
			continue
		}

		lex := lexer.NewLexer(line)
		tokens, err := lex.Tokenize()
		if err != nil {
			fmt.Printf("Lexer error: %v\n", err)
			continue
		}

		p := parser.NewParser(tokens)
		prog, err := p.ParseProgram()
		if err != nil {
			fmt.Printf("Parser error: %v\n", err)
			continue
		}

		res, err := vm.EvalProgram(prog)
		if err != nil {
			fmt.Printf("Runtime error: %v\n", err)
			continue
		}

		if res != nil && res.Type() != "nil" {
			fmt.Printf("=> %s\n", res.String())
		}
	}
}
