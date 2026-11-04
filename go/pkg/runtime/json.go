package runtime

import (
	"encoding/json"
	"fmt"
	"strings"
)

var JSONClass *Class

func init() {
	JSONClass = NewClass("JSON", ObjectClass, nil)
}

func JSONParse(jsonStr string) (Value, error) {
	var raw interface{}
	decoder := json.NewDecoder(strings.NewReader(jsonStr))
	decoder.UseNumber()
	if err := decoder.Decode(&raw); err != nil {
		return nil, fmt.Errorf("JSON parse error: %w", err)
	}
	return convertJSONToValue(raw), nil
}

func convertJSONToValue(raw interface{}) Value {
	if raw == nil {
		return NilVal{}
	}
	switch v := raw.(type) {
	case bool:
		return BoolVal{Val: v}
	case json.Number:
		if i, err := v.Int64(); err == nil {
			return IntVal{Val: i}
		}
		if f, err := v.Float64(); err == nil {
			return FloatVal{Val: f}
		}
		return StringVal{Val: v.String()}
	case float64:
		if v == float64(int64(v)) {
			return IntVal{Val: int64(v)}
		}
		return FloatVal{Val: v}
	case string:
		return StringVal{Val: v}
	case []interface{}:
		elements := make([]Value, len(v))
		for i, el := range v {
			elements[i] = convertJSONToValue(el)
		}
		return &ArrayVal{Elements: elements}
	case map[string]interface{}:
		entries := make(map[string]Value)
		for k, val := range v {
			entries[k] = convertJSONToValue(val)
		}
		return &DictVal{Entries: entries}
	default:
		return StringVal{Val: fmt.Sprintf("%v", v)}
	}
}

func JSONStringify(v Value) (string, error) {
	raw := convertValueToJSON(v)
	bytes, err := json.Marshal(raw)
	if err != nil {
		return "", err
	}
	return string(bytes), nil
}

func convertValueToJSON(v Value) interface{} {
	if v == nil {
		return nil
	}
	switch val := v.(type) {
	case NilVal:
		return nil
	case BoolVal:
		return val.Val
	case IntVal:
		return val.Val
	case FloatVal:
		return val.Val
	case StringVal:
		return val.Val
	case PointVal:
		return map[string]interface{}{
			"x": convertValueToJSON(val.X),
			"y": convertValueToJSON(val.Y),
		}
	case *ArrayVal:
		arr := make([]interface{}, len(val.Elements))
		for i, el := range val.Elements {
			arr[i] = convertValueToJSON(el)
		}
		return arr
	case *DictVal:
		dict := make(map[string]interface{})
		for k, ev := range val.Entries {
			dict[k] = convertValueToJSON(ev)
		}
		return dict
	case *InstanceVal:
		fields := make(map[string]interface{})
		for k, fv := range val.Fields {
			fields[k] = convertValueToJSON(fv)
		}
		return fields
	default:
		return val.String()
	}
}
