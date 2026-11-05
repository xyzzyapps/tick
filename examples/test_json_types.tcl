# Test JSON, lists, floats, ints, strings

@puts "=== 1. JSON Parsing & Stringifying ==="
set jsonStr "{\"language\": \"Tcl-Talk\", \"version\": 2, \"pi\": 3.14159, \"active\": true, \"tags\": [\"smalltalk\", \"tcl\", \"ffi\"]}"

# Parse JSON into Dictionary/Array
set data [JSON parse jsonStr]
@puts "Language: [data at \"language\"]"
@puts "Version: [data at \"version\"]"
@puts "Pi: [data at \"pi\"]"
@puts "Active: [data at \"active\"]"

set tags [data at "tags"]
set joinedTags [tags join ", "]
@puts "Tags: $joinedTags"

# Modify and serialize back
data at "author" put "Antigravity"
tags add "oop"
set jsonOut [data asJson]
@puts "Serialized JSON: $jsonOut"

@puts ""
@puts "=== 2. Lists & Arrays ==="
set list [List new]
list add "apple"
list add "banana"
list add "cherry"

@puts "List count: [list count]"
@puts "Contains banana: [list contains \"banana\"]"
@puts "Contains grape: [list contains \"grape\"]"
set joinedList [list join " -> "]
@puts "Joined: $joinedList"
set revList [[list reversed] join " -> "]
@puts "Reversed: $revList"

@puts ""
@puts "=== 3. Floats & Ints ==="
set i 42
set f 3.14159
@puts "Int to float: [i asFloat]"
@puts "Float to int: [f asInt]"
@puts "Round: [f round]"
@puts "Floor: [f floor]"
@puts "Ceil: [f ceil]"
@puts "Max(10, 20): [10 max 20]"
@puts "Min(10, 20): [10 min 20]"
@puts "2^8: [2 pow 8]"

@puts ""
@puts "=== 4. Strings ==="
set text "  Hello, Tcl-Talk World!  "
@puts "Trimmed: [text trim]"
@puts "Upper: [text toUpper]"
@puts "Lower: [text toLower]"
@puts "Contains 'Talk': [text contains \"Talk\"]"

set csv "red,green,blue,yellow"
set colors [csv split ","]
set joinedColors [colors join " | "]
@puts "Colors list: $joinedColors"
set replacedText [text replace "World" with "Universe"]
@puts "Replaced: $replacedText"

@puts ""
@puts "=== 5. Recursion ==="
integer method factorial {
    [self <= 1] ifTrue {
        return 1
    } ifFalse {
        return [self * [[self - 1] factorial]]
    }
}
@puts "fact(7) = [7 factorial]"
