//go:build !windows

package ffi

import (
	"fmt"
	rt "tcltalk/pkg/runtime"
)

func (e *Engine) callDynamic(libName, symName string, args []rt.Value) (rt.Value, error) {
	return nil, fmt.Errorf("dynamic DLL loading is only natively enabled on Windows in this build; use built-in @functions")
}
