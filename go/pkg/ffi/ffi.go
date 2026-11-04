package ffi

import (
	"fmt"
	"io"
	"math"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"sync"
	"time"

	rt "tcltalk/pkg/runtime"
)

type Engine struct {
	mu        sync.RWMutex
	variables map[string]rt.Value
	startTime time.Time
	Output    io.Writer
}

func (e *Engine) writeOut(s string) {
	if e.Output != nil {
		fmt.Fprint(e.Output, s)
	} else {
		fmt.Print(s)
	}
}

func (e *Engine) writeOutLn(s string) {
	if e.Output != nil {
		fmt.Fprintln(e.Output, s)
	} else {
		fmt.Println(s)
	}
}

func NewEngine() *Engine {
	e := &Engine{
		variables: make(map[string]rt.Value),
		startTime: time.Now(),
	}

	// Default FFI variables
	e.variables["@PI"] = rt.FloatVal{Val: math.Pi}
	e.variables["@E"] = rt.FloatVal{Val: math.E}
	e.variables["@OS"] = rt.StringVal{Val: runtime.GOOS}
	e.variables["@ARCH"] = rt.StringVal{Val: runtime.GOARCH}
	e.variables["@VERSION"] = rt.StringVal{Val: "1.0.0"}

	return e
}

func (e *Engine) HandleFFI(name string, args []rt.Value, vm *rt.VM) (rt.Value, error) {
	e.mu.Lock()
	defer e.mu.Unlock()

	// Setting variable: "set:@NAME"
	if strings.HasPrefix(name, "set:") {
		varName := strings.TrimPrefix(name, "set:")
		if len(args) > 0 {
			e.variables[varName] = args[0]
			return args[0], nil
		}
		return rt.NilVal{}, nil
	}

	// 1. Check if it's a known variable access with 0 arguments
	if len(args) == 0 {
		if val, ok := e.variables[name]; ok {
			return val, nil
		}
	}

	fnName := strings.TrimPrefix(name, "@")

	// 2. Check for dynamic DLL syntax: "@lib:func" e.g. "@msvcrt:puts"
	if strings.Contains(fnName, ":") {
		parts := strings.SplitN(fnName, ":", 2)
		lib := parts[0]
		sym := parts[1]
		return e.callDynamic(lib, sym, args)
	}

	// 3. Dispatch built-in standard FFI functions
	switch fnName {
	case "puts":
		if len(args) > 0 {
			e.writeOutLn(args[0].String())
			return rt.IntVal{Val: int64(len(args[0].String()) + 1)}, nil
		}
		e.writeOutLn("")
		return rt.IntVal{Val: 1}, nil

	case "printf":
		if len(args) == 0 {
			return rt.IntVal{Val: 0}, nil
		}
		fmtStr := args[0].String()
		rawArgs := make([]interface{}, len(args)-1)
		for i, a := range args[1:] {
			switch v := a.(type) {
			case rt.IntVal:
				rawArgs[i] = v.Val
			case rt.FloatVal:
				rawArgs[i] = v.Val
			case rt.StringVal:
				rawArgs[i] = v.Val
			case rt.BoolVal:
				rawArgs[i] = v.Val
			default:
				rawArgs[i] = a.String()
			}
		}
		out := e.Output
		if out == nil {
			out = os.Stdout
		}
		n, err := fmt.Fprintf(out, fmtStr, rawArgs...)
		if err != nil {
			return nil, err
		}
		return rt.IntVal{Val: int64(n)}, nil

	case "print":
		for _, a := range args {
			e.writeOut(a.String())
		}
		return rt.NilVal{}, nil

	case "json_parse":
		if len(args) == 0 {
			return nil, fmt.Errorf("@json_parse requires JSON string argument")
		}
		return rt.JSONParse(args[0].String())

	case "json_stringify":
		if len(args) == 0 {
			return nil, fmt.Errorf("@json_stringify requires object argument")
		}
		s, err := rt.JSONStringify(args[0])
		if err != nil {
			return nil, err
		}
		return rt.StringVal{Val: s}, nil

	case "sqrt":
		if len(args) == 0 {
			return nil, fmt.Errorf("@sqrt requires 1 argument")
		}
		return rt.FloatVal{Val: math.Sqrt(toFloat(args[0]))}, nil

	case "sin":
		if len(args) == 0 {
			return nil, fmt.Errorf("@sin requires 1 argument")
		}
		return rt.FloatVal{Val: math.Sin(toFloat(args[0]))}, nil

	case "cos":
		if len(args) == 0 {
			return nil, fmt.Errorf("@cos requires 1 argument")
		}
		return rt.FloatVal{Val: math.Cos(toFloat(args[0]))}, nil

	case "tan":
		if len(args) == 0 {
			return nil, fmt.Errorf("@tan requires 1 argument")
		}
		return rt.FloatVal{Val: math.Tan(toFloat(args[0]))}, nil

	case "pow":
		if len(args) < 2 {
			return nil, fmt.Errorf("@pow requires 2 arguments")
		}
		return rt.FloatVal{Val: math.Pow(toFloat(args[0]), toFloat(args[1]))}, nil

	case "abs":
		if len(args) == 0 {
			return nil, fmt.Errorf("@abs requires 1 argument")
		}
		if iv, ok := args[0].(rt.IntVal); ok {
			v := iv.Val
			if v < 0 {
				v = -v
			}
			return rt.IntVal{Val: v}, nil
		}
		return rt.FloatVal{Val: math.Abs(toFloat(args[0]))}, nil

	case "floor":
		if len(args) == 0 {
			return nil, fmt.Errorf("@floor requires 1 argument")
		}
		return rt.FloatVal{Val: math.Floor(toFloat(args[0]))}, nil

	case "ceil":
		if len(args) == 0 {
			return nil, fmt.Errorf("@ceil requires 1 argument")
		}
		return rt.FloatVal{Val: math.Ceil(toFloat(args[0]))}, nil

	case "getenv":
		if len(args) == 0 {
			return nil, fmt.Errorf("@getenv requires variable name")
		}
		val := os.Getenv(args[0].String())
		return rt.StringVal{Val: val}, nil

	case "setenv":
		if len(args) < 2 {
			return nil, fmt.Errorf("@setenv requires name and value")
		}
		os.Setenv(args[0].String(), args[1].String())
		return rt.NilVal{}, nil

	case "system":
		if len(args) == 0 {
			return nil, fmt.Errorf("@system requires command string")
		}
		cmdStr := args[0].String()
		var cmd *exec.Cmd
		if runtime.GOOS == "windows" {
			cmd = exec.Command("cmd", "/c", cmdStr)
		} else {
			cmd = exec.Command("sh", "-c", cmdStr)
		}
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		cmd.Stdin = os.Stdin
		err := cmd.Run()
		if err != nil {
			return rt.IntVal{Val: 1}, nil
		}
		return rt.IntVal{Val: 0}, nil

	case "clock":
		ticks := time.Since(e.startTime).Milliseconds()
		return rt.IntVal{Val: ticks}, nil

	case "time":
		return rt.IntVal{Val: time.Now().Unix()}, nil

	case "sleep":
		if len(args) > 0 {
			d := time.Duration(toFloat(args[0]) * float64(time.Second))
			time.Sleep(d)
		}
		return rt.NilVal{}, nil

	case "exit":
		code := 0
		if len(args) > 0 {
			code = int(toInt(args[0]))
		}
		os.Exit(code)
		return rt.NilVal{}, nil

	case "call":
		// Generic dynamic call: @call "lib" "symbol" args...
		if len(args) < 2 {
			return nil, fmt.Errorf("@call requires library and symbol names")
		}
		lib := args[0].String()
		sym := args[1].String()
		return e.callDynamic(lib, sym, args[2:])

	default:
		// Check if it was an accessed variable
		if val, ok := e.variables[name]; ok {
			return val, nil
		}
		return nil, fmt.Errorf("unknown FFI function or variable '%s'", name)
	}
}

func toFloat(v rt.Value) float64 {
	switch val := v.(type) {
	case rt.IntVal:
		return float64(val.Val)
	case rt.FloatVal:
		return val.Val
	default:
		return 0.0
	}
}

func toInt(v rt.Value) int64 {
	switch val := v.(type) {
	case rt.IntVal:
		return val.Val
	case rt.FloatVal:
		return int64(val.Val)
	default:
		return 0
	}
}
