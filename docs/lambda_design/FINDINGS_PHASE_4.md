# Phase 4 Analysis & Findings: Capture Analysis and Code Generation

**Document Status**: Complete Read-Only Analysis (Awaiting User Approval for Branches B & C)  
**Branch**: `feature/lambda-closures`

---

## 1. Variable Resolution & Uncaptured Symbol Hook

### 1.1 Existing Resolution Pipeline
In `as_compiler.cpp:11894` (`CompileVariableAccess`):
1. **Local Variable Scopes & Parameters**: Iterates the `asCVariableScope` chain from the innermost statement block up to function scope.
2. **Class Members**: If `outFunc->objectType != 0`, searches object properties via `builder->GetObjectProperty` and object methods via `builder->GetObjectMethodDescriptions`.
3. **Global Scope**: Searches module global variables, registered global properties, global functions, and namespaces.

### 1.2 Point of Failure for Uncaptured Outer Locals
When a lambda is compiled, it is compiled as an independent `asCScriptFunction` in Pass 5 (`CompileFunctions`). Its `variables` scope hierarchy contains only:
- Its own parameters.
- Its own locally declared variables within its statement block.

When the lambda body references an outer local variable `x`, step 1 (local scopes) finds nothing. Step 2 finds nothing (unless `outFunc` is an object method). Step 3 checks globals and namespaces. When all fail, line 12050 normally reports:
`Error(TXT_NO_MATCHING_SYMBOL_s, errNode);` ("No matching symbol 'x'").

### 1.3 Hook Design
To detect uncaptured symbols and provide the exact diagnostic requested:
- At lambda registration time, the enclosing function snapshots:
  - All outer variable names in scope into `func->outerVariableNames` (`asCArray<asCString>`).
  - Whether the enclosing function is a class method into `func->outerHasThis` (`bool`).
- When compiling the lambda body in `CompileVariableAccess` and `CompileFunctionCall`:
  1. If `name` matches a member property of the closure context class, it resolves as `this.x`.
  2. If resolution fails (`symbolType == SL_NOMATCH`) and scope is unqualified (`scope == ""`):
     - If `name == "this"` and `outFunc->outerHasThis == true`:
       Emit `TXT_THIS_NOT_CAPTURED`: `"variable 'this' is not captured; use function[this](...)"`.
     - If `outFunc->outerVariableNames.IndexOf(name) >= 0`:
       Emit `TXT_VARIABLE_s_NOT_CAPTURED`: `"variable 'x' is not captured; use function[x](...)"`.
     - Otherwise, emit the standard `"No matching symbol '%s'"`.

### 1.4 Lookup Order & Shadowing Rules
1. **Lambda parameter**: Highest precedence. If the lambda declares `int x`, `x` inside the lambda resolves to the parameter. (Note: Phase 2 already emits `TXT_CAPTURE_CONFLICTS_WITH_PARAM_s` if a captured variable shares a name with a parameter).
2. **Lambda local variable**: Declared inside a statement block of the lambda. Shadows any captured variable of the same name in that block.
3. **Captured variable**: Field on the closure context object (`this.x`).
4. **Outer uncaptured variable**: If referenced without being in the capture list, produces the `"variable 'x' is not captured"` error (unless shadowed by 1 or 2).

---

## 2. Enclosing-Function Information Strategy

### 2.1 The Two Options
- **Option (i): Snapshotting at definition site**. While compiling the enclosing function expression (`ImplicitConvLambdaToFunc`), query `variables->GetVariable(capName)` and `outFunc->objectType`. Record the resolved `asCDataType` directly into the synthesized closure class properties and store outer symbol names on the `asCScriptFunction`.
- **Option (ii): Retaining a pointer to the enclosing compile context**. Keep a pointer to the enclosing `asCCompiler` or enclosing `asCScriptFunction`.

### 2.2 Lifetime & Ordering Safety Evaluation
- In AngelScript's compilation architecture (`asCBuilder::Build`):
  - Pass 4 compiles global variables.
  - Pass 5 compiles function bodies (`CompileFunctions`).
  - Individual `asCCompiler` instances are stack-allocated and destroyed as soon as an enclosing function body completes compilation.
- Keeping a pointer to the enclosing `asCCompiler` (Option ii) causes **dangling pointer access** when the lambda body is compiled later in Pass 5.
- Storing captured variable types directly in the synthesized `asCObjectType` properties (Option i) is 100% self-contained, lifetime-safe, and decoupled.
- **Verdict**: **Option (i) Snapshotting** is the only safe architecture.

---

## 3. Capture Type Rules & Copyability

### 3.1 Type Decay & Stripping
For each captured variable in `function[a, b, this]`:
1. **Primitives (`int`, `float`, `bool`, etc.)**: Stored by value (`decayedType.MakeReference(false); decayedType.MakeReadOnly(false);`).
2. **Object handles (`Type@`)**: Stored as object handle (`Type@`). The handle reference is stored and AddRef'd on creation.
3. **Value objects (`Type`)**: Stored by value in the closure class. References and top-level const are stripped.
4. **`this`**: Stored as an object handle to the enclosing class (`asCDataType::CreateObjectHandle(outFunc->objectType, false)`). If the enclosing method is `const` (`outFunc->IsReadOnly()`), the handle is marked as a handle to a const object via `MakeHandleToConst(true)`.

### 3.2 Detecting Copyability of Value Types
To ensure only copyable types are captured by value:
- If `decayedType.IsObject() && !decayedType.IsObjectHandle()`:
  - If `decayedType.GetTypeInfo()->flags & asOBJ_REF`:
    Script classes or registered reference types cannot be copied by value. Must emit:
    `TXT_CANNOT_COPY_NONCOPYABLE_TYPE_s`: `"Cannot capture object of non-copyable type '%s' by value"`.
  - If `decayedType.GetTypeInfo()->flags & asOBJ_VALUE`:
    - If `flags & asOBJ_POD`: trivially copyable.
    - If not POD: query behaviours `asSTypeBehaviour *beh = decayedType.GetBehaviour();`. Must have `beh && (beh->copyconstruct || beh->copy)`. If neither exists, emit `TXT_CANNOT_COPY_NONCOPYABLE_TYPE_s`.

### 3.3 Representation of `this`
In AngelScript class methods, `this` is located at stack offset 0 (the object pointer passed by the VM). Capturing `[this]` reads stack offset 0 (`asBC_PSF 0`, `asBC_RDSPtr`) and stores the handle into the `$this` field with an AddRef, keeping the enclosing class instance alive.

---

## 4. Branch A: Stateless Lambdas (`function[]` / No Captures)

### 4.1 Synthetic Function Creation
- When `captures.GetLength() == 0`:
  The lambda compiles as a static, global script function (`asFUNC_SCRIPT`).
- **Naming Scheme**:
  `name.Format(iterations == 0 ? "$%s$%d" : "$%s$%d(%d)", m_globalVar ? m_globalVar->name.AddressOf() : outFunc->GetDeclaration(), numLambdas, iterations);`
- **Uniqueness Guarantee**:
  `builder->GetFunctionDescriptions(name.AddressOf(), funcs, ns)` checks for collisions. If any function exists with the name (e.g. from repeated module rebuilds or shared functions), `iterations` increments until unique.
  Namespaces and modules isolate names across compilation units.

---

## 5. Branch B: `[this]`-Only Closures

### 5.1 Delegate Capabilities from Phase 0
- In `as_scriptfunction.cpp:172` (`CreateDelegate`), an `asCScriptFunction` of type `asFUNC_DELEGATE` wraps:
  - `objForDelegate`: pointer to the class instance (`asCScriptObject*`).
  - `funcForDelegate`: pointer to the method (`asCScriptFunction*`).
- The engine's delegate factory `$dlgte(obj, func)` creates this at runtime via `asBC_CALLSYS`.
- In `as_context.cpp:4400`, the VM executes a delegate by pushing `objForDelegate` to the stack and calling `funcForDelegate`.

### 5.2 Two Alternative Architectures for Branch B

#### Option 1: Direct Method Delegate on Enclosing Class
Synthesize a private method `$lambda_N` directly on the enclosing class `Foo`:
- At runtime: push `this` (stack offset 0), push `Foo::$lambda_N`, call `$dlgte`.
- **Pros**: Zero memory allocations for a context class; minimal memory footprint.
- **Cons**:
  - Mutates the enclosing class `asCObjectType` with synthetic methods.
  - Requires special-casing delegate dispatch vs context class closures.
  - Branching complexity between `[this]` and `[this, x]`.

#### Option 2: Uniform Context Class for All Captures (Recommended)
Treat `[this]` identically to any other capture:
- Synthesize `$closure$<id>` with a single field `$this` of type `Foo@` (or `const Foo@`).
- The closure method `$call` is on `$closure`, and accesses `this.$this`.
- **Pros**:
  - 100% uniform code generation, memory layout, GC tracking, and lifecycle across all closure types.
  - No mutation of user classes.
  - No new VM bytecode instructions required (full VM backward compatibility).
  - Clean debugger inspection: `$closure` shows captured `$this` explicitly.
- **Cons**: One small heap allocation for the closure context object.

### 5.3 Recommendation on Branch B
**Recommendation**: Use **Option 2 (Uniform Context Class)** for ALL captures, including `[this]`-only.
- Bytecode format versioning remains 100% unchanged.
- VM compatibility is preserved.
- Code complexity is drastically reduced because one single code generation path handles `[a]`, `[this]`, and `[a, b, this]`.

---

## 6. Branch C: Context Class Architecture (`$closure`)

### 6.1 Synthetic Class Structure
- **Type**: `asCObjectType` with flags `asOBJ_REF | asOBJ_SCRIPT_OBJECT | asOBJ_GC`.
- **Name**: `$closure$<scope>$<id>`.
- **Fields**:
  - One property per captured variable, preserving original variable names.
  - Property `$this` if `this` is captured.
- **Behaviours**: Inherits standard `engine->scriptTypeBehaviours` (AddRef, Release, GC behaviours: `gcEnumReferences`, `gcReleaseAllReferences`).
- **Destructor**: `asCScriptObject::~asCScriptObject` automatically releases object handles and destroys member properties in reverse order of declaration.
- **Method `$call`**:
  - Parameter types & return type match the lambda signature.
  - Receiver is the `$closure` instance.

### 6.2 Creation Strategy in Builder
- **Text Synthesis (Option i)**: Generating string `class $closure { int a; void $call(...) { ... } }` and feeding into `asCParser`.
  - *Risk*: String allocation overhead; parser reentrancy during expression compilation; potential tokenizer state conflicts.
- **Programmatic Construction (Option ii)** (Recommended):
  - Call `asNEW(asCObjectType)`, set flags, copy behaviours, call `AddPropertyToClass`.
  - Register `$call` directly via `RegisterScriptFunction`.
  - *Evidence*: Reuses AngelScript's internal class allocation pipeline cleanly, avoids string formatting overhead, and eliminates parser reentrancy bugs.

---

## 7. Body Access & Member Rewriting

### 7.1 Hook-Based Access vs AST Rewriting
Because the lambda body is registered as method `$call` on `$closure`:
- Inside `$call`, the hidden `this` pointer is the `$closure` instance.
- In `CompileVariableAccess`, when the compiler searches for identifier `a`, it automatically queries `outFunc->objectType->properties`.
- It finds property `a` and emits:
  `asBC_PSF 0; asBC_RDSPtr; asBC_ADDSi offset;`
- **Result**: NO AST rewriting is required!
  - `a += 1;` compiles naturally as a member assignment.
  - `&inout` passing, address-taking, and method invocation (`a.Method()`) work out of the box with zero compiler modifications.

---

## 8. Runtime Instantiation Bytecode Sequence

At the lambda expression site:
1. `asBC_PSF (closureVar)`: Allocate stack slot for closure object pointer.
2. `asBC_ALLOC (closureType, ctorId)`: Allocate `$closure` on heap, initialize refcount to 1.
3. For each capture `c`:
   - Compute destination property: `PSF closureVar`, `RDSPtr`, `ADDSi propOffset`.
   - Evaluate source expression: `CompileVariableAccess(name)`. For handle variables, dereference with `Dereference(&rctx, true)`.
   - Store value: `PerformAssignment` (`asBC_WRTV4`/`WRTV8` for primitives; `asBC_REFCPY` for handles with AddRef).
4. Create delegate:
   - Push closure object: `asBC_PshVPtr closureVar`.
   - Push virtual function descriptor: `asBC_FuncPtr vf`.
   - Call `$dlgte` factory: `asBC_CALLSYS (delegateFactory, 2 * PTR_SIZE)`.
5. Return delegate handle:
   - Store delegate into temporary variable: `asBC_STOREOBJ returnVar`.
   - Push delegate pointer: `asBC_PshVPtr returnVar`.
   - Release temporary closure object variable: `ReleaseTemporaryVariable(closureVar)`.

---

## 9. Calling Convention & VM Dispatch

- Calling a closure handle `function<R(Args)>@ f`:
  - The caller compiles a standard function handle call (`asBC_CallPtr`).
  - In `as_context.cpp:4400`:
    ```cpp
    else if (func->funcType == asFUNC_DELEGATE)
    {
        m_regs.stackPointer -= AS_PTR_SIZE;
        *(asPWORD*)m_regs.stackPointer = asPWORD(func->objForDelegate);
        m_regs.programPointer += 2;
        CallInterfaceMethod(func->funcForDelegate);
    }
    ```
  - The VM pushes `func->objForDelegate` (the `$closure` instance) and calls `func->funcForDelegate` (`$call`).
  - **Zero VM changes needed.**

---

## 10. Lifetime & Escaping Closures

- When returning a closure: `return function[c]() { return ++c; };`
  - The `$closure` instance is allocated on the heap.
  - The delegate holds a strong reference (`AddRef`) to the `$closure` instance.
  - The creating stack frame unwinds and frees its local variables.
  - The closure environment persists on the heap as long as the returned function handle is referenced.
  - When the function handle is released, the delegate releases the `$closure` instance, which in turn releases all captured handles.

---

## 11. Final Bytecode Listings for Each Branch

### 11.1 Branch A: Stateless Lambda (`function[]` / No Captures)

**Source**:
```angelscript
function<int(int)>@ f = function(int x) { return x * 2; };
```

**Definition Site**:
```
0000: FuncPtr ptr($test$0)       ; Push pointer to global static script function
0002: STOREOBJ v0                ; Store into local variable f
```

**Synthetic Function (`$test$0`)**:
```
0000: CpyVtoV4 v-5, v-4          ; Load argument x (v-5)
0003: SetV4 v-6, 2               ; Load constant 2
0006: MPYii v-5, v-6             ; Multiply x * 2
0009: CpyVtoR4 v-5               ; Return result in register R4
0011: Ret 1                      ; Return popping 1 argument
```

---

### 11.2 Branch B: `[this]`-Only Closure

**Source**:
```angelscript
class Foo {
    int x = 42;
    function<int()>@ Get() {
        return function[this]() { return this.x; };
    }
}
```

**Definition Site (inside `Foo::Get`)**:
```
0000: PSF v-5                    ; Allocate temporary stack slot for closure object
0002: ALLOC $closure$0, ctorId   ; Allocate heap context object
; Initialize captured property $this:
0006: PSF 0                      ; Address of stack offset 0 (enclosing this)
0008: RDSPtr                     ; Dereference pointer to Foo
0009: PSF v-5                    ; Address of closure instance
0011: RDSPtr                     ; Dereference closure pointer
0012: ADDSi 0                    ; Offset of property $this in closure
0015: REFCPY Foo                 ; AddRef and store Foo@ into closure.$this
0018: PopPtr                     ; Pop destination address
; Create delegate:
0019: PSF v-5                    ; Push closure object pointer
0021: RDSPtr                     ;
0022: FuncPtr vf($call)          ; Push virtual function table entry for $call
0024: CALLSYS $dlgte, 16         ; Call delegate factory
0027: STOREOBJ v-6               ; Store delegate handle in temporary return variable
0029: PshVPtr v-6                ; Push delegate pointer to return
0031: FREE v-5                   ; Release temporary closure object handle
0033: Ret 0                      ; Return delegate
```

**Closure Method (`$closure$0::$call`)**:
```
0000: PSF 0                      ; Load closure 'this' pointer
0002: RDSPtr                     ;
0003: ADDSi 0                    ; Offset of property '$this'
0006: RDSPtr                     ; Dereference '$this' pointer
0007: ADDSi 0                    ; Offset of property 'x' inside Foo
0010: RDR4                       ; Read 4-byte integer member 'x'
0012: CpyVtoR4 v-5               ; Move result to return register
0015: Ret 0                      ; Return from method
```

---

### 11.3 Branch C: Captured Locals Closure (`[count]`)

**Source**:
```angelscript
int count = 0;
auto inc = function[count]() { return ++count; };
```

**Definition Site**:
```
0000: SetV4 v0, 0                ; count = 0
0003: PSF v-5                    ; Allocate temporary stack slot for closure object
0005: ALLOC $closure$1, ctorId   ; Allocate heap context object
; Initialize captured property count:
0009: CpyVtoV4 v-6, v0           ; Copy source local variable 'count'
0012: PSF v-5                    ; Address of closure instance
0014: RDSPtr                     ; Dereference closure pointer
0015: ADDSi 0                    ; Offset of property 'count' in closure
0018: PopRPtr                    ; Pop property address to regPointer
0019: WRTV4 v-6                  ; Write 4-byte integer into closure.count
; Create delegate:
0021: PSF v-5                    ; Push closure object pointer
0023: RDSPtr                     ;
0024: FuncPtr vf($call)          ; Push virtual function table entry for $call
0026: CALLSYS $dlgte, 16         ; Call delegate factory
0029: STOREOBJ v1                ; Store delegate into variable 'inc'
0031: PshVPtr v1                 ; Push delegate pointer
0033: FREE v-5                   ; Release temporary closure object handle
```

**Closure Method (`$closure$1::$call`)**:
```
0000: PSF 0                      ; Load closure 'this' pointer
0002: RDSPtr                     ;
0003: ADDSi 0                    ; Offset of property 'count' in closure
0006: PopRPtr                    ; Pop address to regPointer
0007: RDR4 v-5                   ; Read current value of count
0009: ADDi v-5, 1                ; Increment by 1 (++count)
0012: WRTV4 v-5                  ; Store updated value back to closure.count
0014: CpyVtoR4 v-5               ; Return new value in return register
0017: Ret 0                      ; Return from method
```

