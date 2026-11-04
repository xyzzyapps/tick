//go:build windows

package ffi

import (
	"fmt"
	"strings"
	"syscall"
	"unsafe"

	rt "tcltalk/pkg/runtime"
)

var (
	loadedDlls = make(map[string]syscall.Handle)
)

func (e *Engine) callDynamic(libName, symName string, args []rt.Value) (rt.Value, error) {
	if !strings.HasSuffix(strings.ToLower(libName), ".dll") {
		libName += ".dll"
	}

	h, ok := loadedDlls[libName]
	if !ok {
		var err error
		h, err = syscall.LoadLibrary(libName)
		if err != nil {
			return nil, fmt.Errorf("failed to load library '%s': %v", libName, err)
		}
		loadedDlls[libName] = h
	}

	proc, err := syscall.GetProcAddress(h, symName)
	if err != nil {
		return nil, fmt.Errorf("failed to get proc address '%s' in '%s': %v", symName, libName, err)
	}

	// Prepare arguments
	uintptrArgs := make([]uintptr, len(args))
	// Keep pointers alive during syscall
	keepAlive := make([]interface{}, 0, len(args))

	for i, arg := range args {
		switch v := arg.(type) {
		case rt.IntVal:
			uintptrArgs[i] = uintptr(v.Val)
		case rt.FloatVal:
			// Convert float to bits
			uintptrArgs[i] = uintptr(int64(v.Val))
		case rt.StringVal:
			cstr, err := syscall.BytePtrFromString(v.Val)
			if err != nil {
				return nil, err
			}
			uintptrArgs[i] = uintptr(unsafe.Pointer(cstr))
			keepAlive = append(keepAlive, cstr)
		case *rt.FFIHandleVal:
			if ptr, ok := v.Handle.(uintptr); ok {
				uintptrArgs[i] = ptr
			} else {
				uintptrArgs[i] = 0
			}
		case rt.NilVal:
			uintptrArgs[i] = 0
		case rt.BoolVal:
			if v.Val {
				uintptrArgs[i] = 1
			} else {
				uintptrArgs[i] = 0
			}
		default:
			uintptrArgs[i] = 0
		}
	}

	r1, _, _ := syscall.SyscallN(proc, uintptrArgs...)
	_ = keepAlive

	return rt.IntVal{Val: int64(r1)}, nil
}
