# Smalltalk & Tick basics test suite

@puts "--- Testing Dictionaries ---"
set d [Dictionary new]
d at "name" put "Antigravity"
d at "role" put "AI Assistant"
@puts "Dict name: [d at \"name\"]"
@puts "Dict role: [d at \"role\"]"

@puts ""
@puts "--- Testing Factorial via Recursion / Method ---"
integer method fact {
    [self <= 1] ifTrue {
        return 1
    } ifFalse {
        return [self * [[self - 1] fact]]
    }
}
set f5 [5 fact]
@puts "5! = $f5"

set f10 [10 fact]
@puts "10! = $f10"

@puts ""
@puts "--- Testing Fluent Pipeline ---"
set nums [Array new]
nums add 1
nums add 2
nums add 3
nums add 4
nums add 5

set evensDoubled [
    nums select { n | [n % 2] == 0 }
    | collect { n | n * 10 }
]
@puts "Evens doubled: $evensDoubled"

@puts ""
@puts "--- Testing Inject/Fold ---"
set sum [
    evensDoubled inject 0 into { acc val | acc + val }
]
@puts "Sum: $sum"
