package runtime

import (
	"fmt"
	"sync/atomic"
)

var frameCounter int64

func NextFrameID() int64 {
	return atomic.AddInt64(&frameCounter, 1)
}

type Scope struct {
	Parent           *Scope
	Variables        map[string]Value
	Self             Value
	ReceiverInstance *InstanceVal
	CallFrameID      int64
}

func NewScope(parent *Scope, self Value, frameID int64) *Scope {
	var inst *InstanceVal
	if self != nil {
		if iv, ok := self.(*InstanceVal); ok {
			inst = iv
		}
	}
	return &Scope{
		Parent:           parent,
		Variables:        make(map[string]Value),
		Self:             self,
		ReceiverInstance: inst,
		CallFrameID:      frameID,
	}
}

func (s *Scope) Get(name string) (Value, bool) {
	if name == "self" {
		if s.Self != nil {
			return s.Self, true
		}
	}
	if name == "true" {
		return BoolVal{Val: true}, true
	}
	if name == "false" {
		return BoolVal{Val: false}, true
	}
	if name == "nil" {
		return NilVal{}, true
	}

	// 1. Check local scope variables
	if val, ok := s.Variables[name]; ok {
		return val, true
	}

	// 2. Check instance variables on ReceiverInstance
	if s.ReceiverInstance != nil {
		if val, ok := s.ReceiverInstance.Fields[name]; ok {
			return val, true
		}
	}

	// 3. Check parent scope recursively
	if s.Parent != nil {
		return s.Parent.Get(name)
	}

	return nil, false
}

func (s *Scope) Set(name string, val Value) {
	// 1. Check if name is an instance variable on ReceiverInstance
	if s.ReceiverInstance != nil {
		if s.ReceiverInstance.Klass.HasInstanceVar(name) {
			s.ReceiverInstance.Fields[name] = val
			return
		}
	}

	// 2. If variable already exists in this or parent scope, update it
	curr := s
	for curr != nil {
		if _, ok := curr.Variables[name]; ok {
			curr.Variables[name] = val
			return
		}
		// Also check parent's receiver instance variables if in closure
		if curr.ReceiverInstance != nil && curr.ReceiverInstance.Klass.HasInstanceVar(name) {
			curr.ReceiverInstance.Fields[name] = val
			return
		}
		curr = curr.Parent
	}

	// 3. Otherwise define in current local scope
	s.Variables[name] = val
}

func (s *Scope) Define(name string, val Value) {
	s.Variables[name] = val
}

type ReturnSignal struct {
	TargetFrameID int64
	Value         Value
}

func (r *ReturnSignal) Error() string {
	return fmt.Sprintf("non-local return to frame %d", r.TargetFrameID)
}
