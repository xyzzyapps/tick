# FFI demonstration in Tcl-Talk

@puts "=== FFI Variables ==="
@puts "OS: @OS"
@puts "ARCH: @ARCH"
@puts "Version: @VERSION"
set pi @PI
@puts "Pi is: $pi"

@puts ""
@puts "=== FFI Math Calls ==="
set sq [@sqrt 144]
@puts "sqrt(144) = $sq"

set s [@sin [pi / 2]]
@puts "sin(pi/2) = $s"

set p [@pow 2 10]
@puts "2^10 = $p"

@puts ""
@puts "=== FFI System & Time ==="
set t [@time]
@puts "Unix timestamp: $t"

set u [@getenv "USERNAME"]
@puts "Current user: $u"

@puts ""
@puts "=== FFI Dynamic Calling ==="
@call "msvcrt.dll" "puts" "Hello from dynamic msvcrt.dll puts via FFI!"

@puts ""
@puts "=== Pipeline to FFI ==="
"This string was piped directly into @puts" | @puts
