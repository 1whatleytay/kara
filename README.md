# kara - powerful, native compiler project

Kara is a statically typed, LLVM-based programming language. It's designed to be fast and easy to use.

During a recursive benchmark to recursively calculate the first 500000 prime numbers, Kara with `-O3` performed about **90x** faster than Python, and over **2x** as fast as Java on my MacBook Pro 2015.

This project is also about bringing many frustrations I've encountered with other languages to rest. In many other languages, there are plenty of clean or performant solutions that are just too bulky or confusing to realistically use.

Case in point, a `std::variant` error bubbling is [4x faster](https://godbolt.org/z/KPqnG4Wxj) than a similar Coroutine solution, but so much more verbose that it's impractical to use. With kara I plan to keep the speed but cut out verbosity to make for a peaceful developer experience.  

## Design Goals

 - Beginner friendly, code should be readable and extendable.
 - For ease of mind, more complex features should be a combination of simpler features.
 - Fast, native code. Abstract common patterns quickly.
 - Encourage self documentation through various interactive builtin types.

## Progress
Until `1.0`.

### Variables
 - [x] `let`/`var` `name` = `value`
 - [x] Explicit Type After Name
 - [x] Implicit Type Deduction
 - [x] Mutability Checking

### Types
 - [x] Declaration `type Name { field1 Type1, field2 Type2 }`
 - [x] Aliasing `type Name = OtherType`
 - [x] Type Construction `TypeName(field1: value1, field2: value2)`
 - [ ] Explicitly uninitialized types for constructors
 - [ ] References for use in constructors `&out Type`
 - [ ] Map to Result Syntax, `let z TypeName = (field1: value1, field2: value2)`
 - [ ] Enums, `enum { A, B, C }`
 - [ ] Enum With Data `enum { A => Type, B => +(a: Type1, b: Type2), C }`

### Builtins
 - [x] References `&T`
 - [x] Unique Pointers `*T`
 - [ ] Shared Pointers `*shared T`
 - [ ] Dynamic Arrays `[T]`
 - [ ] Hybrid Arrays (sso) `[T,50]`
 - [x] Fixed Size Arrays `[T:50]`
 - [x] Unbounded Sized Arrays `[T:expr]`
 - [x] Unbound Arrays (unsafe) `[T:]`
 - [ ] Contiguous Array Iterators `[T::]`
 - [ ] Dynamic Array Iterators (for lists) `[dyn T::]`
 - [ ] Maps `[K -> V]`
 - [ ] Variants `T1 | T2 | T3`
 - [ ] Tuples `T1 & T2 & T3`
 - [ ] Optionals `?T`
 - [ ] Partials `?partial T`
 - [ ] Bubbling Optionals `!T`
 - [ ] Bubbling Optionals With Error Type `!T | E1 | E2`
 - [ ] Function Pointers `fun ptr (T1, T2, T3) ReturnType`
 - [ ] Function Type + Captures `fun (T1, T2, T3) Return Type`
 - [ ] Ranges `(1..<3)`
 - [ ] Named Tuples `+(a TypeA, b TypeB)`

### Functions
 - [x] `=>` For Implicit Return
 - [x] Type Deduction for Implicit Return
 - [x] Implicit `()` on Function Declaration
 - [x] Function Overloading By Type
 - [x] Function Overloading By Parameter Name
 - [ ] Template Input Type `^T`, infer from parameters
 - [ ] `fun` keyword in code body

### Experience
 - [x] Arithmetic Operators
 - [x] Statements, `return`/`break`/`continue`
 - [x] Control `if`/`for`
 - [ ] VLA for Unbounded Sized Arrays `let x [T:expr]`
 - [ ] Match Statement `match x { 1 => value1, 2 => value2 }`
 - [ ] Match Statement Fallback on `operator==`
 - [ ] Match Statement for Unpacking of Variant or Bubbling Optionals
 - [x] Universal Function Call Syntax, `f(x, y)` = `x.f(y)`
 - [x] Implicit `()` on Expressions, `f()` = `f`
 - [x] Implicit Referencing (in place of error) `&expr`
 - [x] Implicit Dereferencing (in place of error) `@expr`
 - [ ] For/In Loop
 - [x] Implicit Destruction
 - [x] Custom Destroy Methods
 - [x] `block { ... }` To Run Code in New Scope
 - [x] `exit { ... }` To Run Code at End of Scope
 - [ ] Lambdas `(param1, param2) => { body }`
 - [ ] Explicit Discarding of Information, `= undefined`
 - [x] Comments `//` and `/*` `*/`
 - [x] Ternaries `condition ? yesValue : noValue`
 - [ ] Optional Unpacking (`a ?? b` for default, `a!` for force unpacking)
 - [ ] Advanced Optional Unpacking, `a ?? return 0`, `a ?? skip`, `a ?? panic`
 - [x] Variable Shadowing in Same Scope
 - [x] New Operator `*[char:50]`
 - [ ] Compound Operator `\ `, `a as b\.c` = `(a as b).c`
 - [ ] Result Packing Operator `where (a = b, c = d) f(a, b, c, d)`
 - [ ] Declaration for Mutable References, `takeOptions(let opts)`
 - [ ] Discard for Mutable References `giveOptions(let)`
 - [ ] Select Levels through Keywords `global.f` vs `scope.f`
 - [x] Casting with `as`, `a as T`
 - [ ] Parameter Casting (in place of inheritance), `a as T from fieldOfTThatWouldContainA`
 - [ ] Great Refactor (better compiler)

### Standard Library
 - [ ] Stdin + Stdout
 - [ ] File IO
 - [ ] FS Reading
 - [ ] Memory Managed String Type

### Interop
 - [x] `import` other Kara Files
 - [x] C header Interop
 - [ ] Objective-C header Interop
 - [x] LLVM JIT `--interpret`
 - [x] Binary Output on Major Platforms (macOS, Windows)
 - [ ] Language Server or Syntax Suggestion Platform

### Beyond 1.0
 - [ ] WebASM interop
 - [ ] Lifetime Checking (work began [here](https://github.com/1whatleytay/kara/tree/lifetimes))
 - [ ] Data Structures as Builtins (BST maps, sets, etc.)
 - [ ] STL Algorithms, `std::copy`, `std::reverse`, `std::find`, `std::find_if`...
 - [ ] Some Async Construct
 - [ ] Split Keyword, Separate Future Operations into a callback, `split`
 - [ ] Extra IR Layer, statically infer types of `any` after first assign
 - [ ] Extend Keyword for grouping functions with similar parameters `extend (a int) { f(b int) => a + b }`
 - [ ] Dynamic Dispatch by Type, `interface`s
 - [ ] Explicitly Pure and Impure Functions, `=>` vs `->`
