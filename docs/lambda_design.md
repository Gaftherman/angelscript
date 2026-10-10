# Modern Anonymous Functions and Closures in AngelScript

This document is the definitive specification and reference manual for anonymous functions, stateful closures, canonical function types, and funcdef interoperability in AngelScript.

---

## 1. User-Facing Syntax & Language Semantics

### 1.1 Canonical Function Types (`function<R(Args)>@`)
AngelScript provides a first-class canonical function signature type:
```angelscript
function<R(Arg1, Arg2, ...)>@ handle;
```
- **Syntax**: `function<ReturnType(ParamTypes...)>@`
- **Semantics**: Represents a reference to any callable matching the specified signature (stateless global function, class method delegate, or stateful closure).
- **Canonical Representation**: Unlike traditional nominal funcdefs (e.g. `funcdef void Callback(int);`), canonical function types are structurally typed. Any two canonical types with identical return types, parameter counts, parameter types, and parameter modifiers represent the exact same type.
- **Modifiers**: Parameter modifiers (`const`, `&in`, `&out`, `&inout`, `@`) are fully supported:
  ```angelscript
  function<int(const string &in, array<int>@)>@ processor;
  ```

### 1.2 Stateless Lambdas
Stateless anonymous functions define executable blocks without capturing surrounding variables:
```angelscript
auto f = function(int a, int b) { return a + b; };
int result = f(10, 20); // 30
```
- **Signature & Parameters**: Parameter types may be specified explicitly or inferred from an expected funcdef target.
- **Underlying Mechanism**: Stateless lambdas compile to zero-overhead static script functions (`asFUNC_SCRIPT`). Calling a stateless lambda involves no heap allocation or delegate wrapper overhead.

### 1.3 Stateful Closures (`function[captures](params) { body }`)
Stateful closures capture variables from their enclosing scope into a dedicated context object:
```angelscript
int multiplier = 3;
auto multiply = function[multiplier](int val) {
    return val * multiplier;
};
int res = multiply(5); // 15
```

#### Explicit Capture List
AngelScript requires capture variables to be explicitly listed inside square brackets `[...]`:
```angelscript
int x = 10;
string prefix = "value: ";
auto logger = function[x, prefix](int offset) {
    return prefix + (x + offset);
};
```
- **Capture by Value**: Non-handle primitive types and value types are captured by value. A private, mutable copy is stored within the closure instance. Modifying the variable inside the closure mutates the closure's copy and does not affect the outer scope variable (and vice versa).
- **Handle Capture**: Object handles (`@obj`) are captured as handles. The closure increments the reference count of the target object upon creation and decrements it upon destruction. The referenced object remains alive even if the enclosing function returns or the outer handle is cleared:
  ```angelscript
  MyClass@ obj = MyClass();
  auto f = function[obj]() { obj.DoSomething(); };
  @obj = null; // target remains alive inside closure
  f();         // succeeds
  ```
- **Class `this` Capture**: Inside class member methods, `this` can be captured explicitly:
  ```angelscript
  class Counter {
      int value = 0;
      function<void()>@ GetIncrementer() {
          return function[this]() {
              this.value++; // or implicitly: value++
          };
      }
  }
  ```
  If `this` is captured inside a `const` member method, `$this` is treated as a `const` handle, allowing member reads but causing a compile-time error on member modifications.

#### Disallowed Capture Syntaxes
Implicit capture-all modes and capture-by-reference are explicitly disallowed to prevent dangling stack references and unexpected capture overhead:
- `[&]` is rejected with: `Capture-by-reference is not supported yet`
- `[=]` is rejected with: `Implicit capture-by-value is not supported yet`
- `[&x]` is rejected with: `Capture-by-reference of specific variables is not supported yet`

#### Scope Isolation & Diagnostics
Any attempt to reference an outer local variable or `this` without listing it in the capture list results in a compile error:
- Referencing uncaptured local `x`:
  ```
  variable 'x' is not captured; use function[x](...)
  ```
- Referencing uncaptured `this`:
  ```
  variable 'this' is not captured; use function[this](...)
  ```
- Capturing a variable that does not exist in the enclosing scope:
  ```
  Capture variable 'x' is not found in enclosing scope
  ```
- Capturing an object whose type cannot be copied (no copy constructor or assignment operator):
  ```
  Cannot capture object of non-copyable type 'T' by value
  ```
- Capturing `this` outside a class member method:
  ```
  Cannot capture 'this' outside of class methods
  ```

### 1.4 Return Type Inference and `auto`
Lambdas can be assigned to `auto` or `auto@` variables:
```angelscript
auto add = function(int a, int b) { return a + b; };
auto getVal = function[capturedVal]() { return capturedVal; };
```
- **Type Deduction**: The compiler speculatively compiles the lambda body in an isolated compiler pass to deduce the unified return type.
- **Strict Return Unification**: If a lambda contains multiple return statements, all returned expressions must evaluate to identical types. Divergent return types produce a compile error:
  ```
  Divergent return types in lambda ('int' and 'string')
  ```
- **Decay Rules**: Return type references decay to values (`MakeReference(false)`), top-level constness on value types decays (`MakeReadOnly(false)`), and object handles are preserved.
- **Parameter Specification**: When assigning a lambda to `auto`, all parameter types must be specified explicitly. Omission results in:
  ```
  Lambda parameter types must be explicitly specified when using 'auto'
  ```
  When assigning a lambda to a typed funcdef variable (nominal or canonical), parameter types can be omitted and are automatically inferred from the target funcdef signature.

### 1.5 Implicit Bidirectional Conversion with Nominal Funcdefs
AngelScript seamlessly bridges nominal funcdefs and canonical function types:
```angelscript
funcdef int Transformer(int);

// Canonical lambda assigned to nominal funcdef handle:
Transformer@ t = function(int x) { return x * 2; };

// Nominal funcdef handle assigned to canonical function handle:
function<int(int)>@ c = t;

// Bidirectional argument passing:
void ApplyNominal(Transformer@ cb);
void ApplyCanonical(function<int(int)>@ cb);

ApplyNominal(c); // Works transparently
ApplyCanonical(t); // Works transparently
```
- **Structural Equivalence**: If a canonical type and a nominal funcdef share identical return types, parameter counts, parameter types, and modifiers, they are mutually assignable.
- **Nominal Isolation**: Two distinct nominal funcdefs (e.g. `funcdef int OpA(int);` and `funcdef int OpB(int);`) remain strictly incompatible and cannot be implicitly converted between each other.
- **Explicit Casts**: Explicit casting (`cast<function<int(int)>>(nominalHandle)`) is fully supported. Angle-bracket token splitting (`>>` into `> >`) is handled automatically without requiring whitespace workarounds.
- **Overload Priority**: Exact type matches take precedence over nominal/canonical conversions during function overload resolution, ensuring 100% backward compatibility for existing code.

---

## 2. Compiler Architecture & Bytecode Generation

### 2.1 Canonical Funcdef Caching (`asCScriptEngine::GetCanonicalFuncdef`)
Canonical funcdefs are managed by `asCScriptEngine::GetCanonicalFuncdef`:
1. **Engine-Level Cache**: If all types comprising the signature (return type and parameters) are primitive, registered engine types, or shared script types, the canonical funcdef is created at engine scope (`engine->funcDefs`), marked shared (`SetShared(true)`), and cached globally across all modules.
2. **Module-Level Storage**: If any type in the signature is a non-shared script class from a specific module, the canonical funcdef is created in that module's `m_funcDefs` list (`SetShared(false)`). This guarantees that discarding the module reclaims the funcdef without leaving dangling references in the engine.
3. **Canonical Signature Deduplication**: Lookups verify signatures via `asCScriptFunction::IsSignatureExceptNameEqual()`. Any subsequent request for the identical signature returns the existing cached `asCFuncdefType`.

### 2.2 Speculative Probe Compilation for `auto`
When compiling `auto f = function(...) { ... };`:
1. In `asCCompiler::CompileAssignment`, the compiler detects an `snAssignment` with an auto target and an `snFunction` source.
2. An isolated `asCCompiler` instance is initialized with `m_isDeducingReturnType = true`.
3. The probe compiler parses the statement block, records return expression types, and enforces return unification.
4. The resolved return type and explicit parameter types are passed to `engine->GetCanonicalFuncdef` to obtain the target canonical funcdef.
5. The original assignment then proceeds with the concrete canonical type replacing `auto`.

### 2.3 Synthetic Closure Context Classes (`$closure$`)
When a lambda captures enclosing variables or `this`:
1. **Naming & Module Ownership**: A synthetic script class is generated with the naming scheme:
   ```
   $closure$<caller_func_sig>$<unique_index>
   ```
   The class is added to the module's `m_classTypes` and marked with `asOBJ_REF | asOBJ_SCRIPT_OBJECT | asOBJ_GC`.
2. **Captured Properties**: Each captured variable becomes a private member property on the context class:
   - Value types and primitives: member property with matching type.
   - Object handles: member property with handle type (`@`).
   - `this`: member property named `$this` with handle type (`@` or `const @`).
3. **Synthetic `$call` Method**: The lambda's statement block is compiled as a member method named `$call` belonging to the closure context class. Inside `$call`:
   - Enclosing variables resolve directly to the closure's member properties.
   - If `$this` was captured, outer class methods and properties resolve implicitly through `$this`.
4. **Constructors and Factory Stubs**: To satisfy the AngelScript virtual machine and bytecode serializer, each closure class receives a default constructor and a matching factory stub, registered in `st->beh.constructors[0]` and `st->beh.factories[0]`.

### 2.4 Runtime Instantiation & Delegate Creation
At runtime, whenever execution evaluates a closure expression:
1. **Allocation**: Emits `asBC_ALLOC` with the closure object type and default constructor. The newly allocated context object is left on the stack.
2. **Property Initialization**: For each captured variable, bytecode is emitted to copy the current value from the enclosing stack frame into the closure object's member property using `asBC_REFCPY` (for handles and value objects) or appropriate primitive store instructions.
3. **Delegate Dispatch**: The compiler calls the internal delegate factory helper (`$dlgte`) via `asBC_CALLSYS`, passing:
   - The closure context object pointer (`objForDelegate`).
   - The function pointer to the synthetic `$call` member method.
4. **Result**: The returned delegate (`asFUNC_DELEGATE`) is pushed onto the stack as the resulting function handle.

### 2.5 Garbage Collection
Because closures capture object handles and `this`, cyclic references are possible (e.g. an object instance holding a closure that captures `this`).
- Closure context classes implement full script class garbage collection behaviours (`gcEnumReferences`, `gcGetFlag`, `gcSetFlag`, `gcReleaseAllReferences`).
- The cycle detector in `asCScriptEngine::GarbageCollect()` traverses closure context instances and reclaims cyclic references cleanly.

---

## 3. Bytecode Save/Load & Serialization

### 3.1 Serialization Support (`as_restore.cpp`)
AngelScript modules containing stateless lambdas and stateful closures support full bytecode serialization and deserialization via `asIScriptModule::SaveByteCode` and `LoadByteCode`:
1. **Canonical Funcdef Persistence**:
   - Funcdefs with name `"function<...>"` are saved with their canonical signatures.
   - On load, the deserializer resolves them through `engine->GetCanonicalFuncdef`, ensuring canonical funcdef IDs remain valid across different engine sessions.
2. **Closure Class and Method Persistence**:
   - Synthetic closure classes (`$closure$...`) are serialized with their member properties, constructor, factory stub, and `$call` method.
   - Constructor/factory pairing (1:1 index alignment) is preserved to ensure seamless deserialization in `asCReader::ReadTypeDeclaration`.
3. **Delegate Bytecode Persistence**:
   - Delegate creation calls and references restore identically to normal script delegates.
4. **Cross-Engine Portability**:
   - Precompiled bytecode can be saved from one `asIScriptEngine` instance and loaded into a completely clean, independent engine instance with zero degradation in functionality or performance.

---

## 4. Public API & Visibility Guarantees

### 4.1 Module Reflection & Enumeration Filtering
To prevent compiler implementation artifacts from polluting user reflection, IDE outlines, and tooling:
- **`asIScriptModule::GetFunctionCount()` and `GetFunctionByIndex()`**:
  - Filter out synthetic functions whose name starts with `$` (including stateless lambda stubs `$lambda$...` / `$anonFunc...`).
  - Return only user-declared global functions.
- **`asIScriptModule::GetObjectTypeCount()` and `GetObjectTypeByIndex()`**:
  - Filter out synthetic closure context classes whose name starts with `$` (such as `$closure$...`).
  - Return only user-declared classes and interfaces.
- **Direct Resolution by ID and Pointer**:
  - `asIScriptEngine::GetFunctionById(id)`: Fully resolves synthetic lambda and closure functions.
  - `asIScriptEngine::GetTypeInfoById(id)`: Fully resolves closure context classes.
  - `asIScriptModule::GetTypeInfoByName(name)`: Can resolve closure types if the specific synthetic name is explicitly requested.

### 4.2 Declaration Formatting and Round-Trip Identity
- `engine->GetTypeDeclaration(typeId)` for a canonical function handle returns the exact string:
  ```
  function<R(Args)>@
  ```
- `engine->GetTypeIdByDecl("function<R(Args)>@")` parses the declaration and returns the identical type ID.
- Bidirectional identity round-trip is guaranteed:
  ```cpp
  int id = engine->GetTypeIdByDecl("function<int(int, array<int>@)>@");
  const char *decl = engine->GetTypeDeclaration(id);
  assert(engine->GetTypeIdByDecl(decl) == id);
  ```

---

## 5. C++ Host Integration Guide

### 5.1 Registering Functions with Canonical Parameters
C++ host applications can register functions that accept or return canonical function types:
```cpp
// Registered function accepting a canonical callback handle:
int ProcessData(asIScriptFunction *callback, int value) {
    asIScriptEngine *engine = callback->GetEngine();
    asIScriptContext *ctx = engine->CreateContext();
    ctx->Prepare(callback);
    ctx->SetArgDWord(0, value);
    ctx->Execute();
    int result = ctx->GetReturnDWord();
    ctx->Release();
    return result;
}

// Registration with auto-handle (@+) - engine releases callback after call:
engine->RegisterGlobalFunction(
    "int ProcessData(function<int(int)>@+ callback, int value)",
    asFUNCTION(ProcessData),
    asCALL_CDECL
);
```

### 5.2 Auto-Handle (`@+`) vs Standard Handle (`@`) Discipline
- **`@+` (Auto-Handle)**: Recommended for registered C++ functions taking callbacks. AngelScript automatically calls `callback->Release()` when the C++ call returns.
- **`@` (Standard Handle)**: C++ takes shared ownership. If the host stores the function pointer, it must call `callback->AddRef()` or manage it with a smart pointer.

### 5.3 Executing Script Closures from C++
To invoke a closure handle returned by a script function:
```cpp
// 1. Prepare and execute the script function that returns the closure:
asIScriptContext *ctx = engine->CreateContext();
ctx->Prepare(mod->GetFunctionByName("GetClosure"));
ctx->Execute();

// 2. Retrieve the returned function pointer:
asIScriptFunction *closureFn = (asIScriptFunction*)ctx->GetReturnAddress();
if (closureFn) {
    // 3. Prepare a context with the closure function:
    // (Prepare automatically binds the delegate object instance!)
    asIScriptContext *execCtx = engine->CreateContext();
    execCtx->Prepare(closureFn);
    execCtx->SetArgDWord(0, 42);
    execCtx->Execute();
    int result = execCtx->GetReturnDWord();
    execCtx->Release();
}
ctx->Release();
```
*Note*: `asIScriptContext::Prepare` automatically detects delegates (`asFUNC_DELEGATE`), binds the internal closure context object, and prepares the target `$call` method. No special handling is required by the host.

---

## 6. Verification and Test Coverage Summary

All functionality is covered by extensive tests in `sdk/tests/test_feature/source/test_lambda.cpp`:
- **Phase 1**: Canonical function types, parsing, caching, signature matching, memory safety.
- **Phase 2**: Parser grammar, AST nodes, capture lists, fuzzing, syntax error handling.
- **Phase 3**: Speculative return deduction, `auto` integration, return unification, decay rules.
- **Phase 4**: Stateful value capture, mutable copies, handle lifetimes, `this` capture, const `this`, diagnostics, cyclic GC collection, nested closures, exception unwinding.
- **Phase 5**: Bidirectional implicit conversion with nominal funcdefs, overload ranking, explicit casts with token splitting, C++ host interoperability.
- **Phase 6**: Precompiled bytecode save/load round-trips, public API reflection filtering, bidirectional declaration parsing, host lifecycle management, zero regressions across 110+ existing tests.

---

## 7. Known Limitations & Migration Guide

### 7.1 Known Limitations
1. **Explicit Capture Only**:
   - Implicit capture-all (`[=]` and `[&]`) is not supported. All captured symbols must be listed explicitly.
   - Capture by reference (`[&x]`) is not supported to avoid dangling stack pointers in escaping closures.
2. **`auto` Return Type Restrictions**:
   - Return-type inference is supported only for anonymous lambda expressions (`auto f = function(...) { ... };`).
   - Named global functions and class member methods still require explicit return types.
3. **No Direct Self-Recursion by Identifier**:
   - An anonymous closure does not bind its own variable name within its body. For recursive algorithms, store the closure in a nominal or canonical handle variable declared prior to recursion or use a named helper function.
4. **Capture of Non-Copyable Types**:
   - Types that cannot be copied (e.g. registered value types without copy constructor/assignment or non-copyable script objects) cannot be captured by value. Pass them as handles (`@`).

### 7.2 Migration & Backward Compatibility
- **100% Backward Compatible**: Existing AngelScript codebases using nominal `funcdef` continue to compile without modifications.
- **Zero-Cost Interop**: Existing C++ APIs accepting nominal funcdef handles automatically accept canonical closures and lambdas without changing C++ function signatures.
- **Clean Module Reflection**: Tools and scripts calling `asIScriptModule::GetFunctionCount()` or `GetObjectTypeCount()` will no longer encounter internal compiler artifacts starting with `$`.

---

## 8. Comprehensive End-User Examples

### Use Case 1: Event Handlers & Callbacks (UI, Input, Timers)

#### Example 1: Button Click Listener with Captured State
```angelscript
class Button {
    function<void()>@ onClick;
    void Click() { if (onClick !is null) onClick(); }
}

void SetupUI() {
    int clickCount = 0;
    Button btn;
    @btn.onClick = function[clickCount]() {
        clickCount++;
        print("Button clicked " + clickCount + " times\n");
    };
    btn.Click(); // Button clicked 1 times
    btn.Click(); // Button clicked 2 times
}
```

#### Example 2: Timer with Handle Capture & Target Lifetime Extension
```angelscript
class AlertBox {
    string message;
    AlertBox(const string &in msg) { message = msg; }
    void Show() { print("Alert: " + message + "\n"); }
}

function<void()>@ ScheduleAlert(const string &in msg) {
    AlertBox@ box = AlertBox(msg);
    // Captured handle keeps 'box' alive even after ScheduleAlert exits:
    return function[box]() {
        box.Show();
    };
}
```

#### Example 3: Member Method Event Listener with `this` Capture
```angelscript
class Player {
    int health = 100;
    function<void(int)>@ GetDamageHandler() {
        return function[this](int amount) {
            this.health -= amount;
            print("Remaining health: " + this.health + "\n");
        };
    }
}
```

---

### Use Case 2: Functional Algorithms (Filtering, Mapping, Sorting)

#### Example 4: Array Filter with Inline Predicate
```angelscript
array<int> Filter(const array<int> &in src, function<bool(int)>@ predicate) {
    array<int> result;
    for (uint i = 0; i < src.length(); i++) {
        if (predicate(src[i]))
            result.insertLast(src[i]);
    }
    return result;
}

void TestFilter() {
    array<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8};
    int threshold = 4;
    auto evensAboveThreshold = Filter(numbers, function[threshold](int n) {
        return (n % 2 == 0) && (n > threshold);
    });
    // evensAboveThreshold contains [6, 8]
}
```

#### Example 5: In-Place Map / Transform with Return Inference
```angelscript
array<string> MapToString(const array<int> &in src, function<string(int)>@ mapper) {
    array<string> result;
    for (uint i = 0; i < src.length(); i++)
        result.insertLast(mapper(src[i]));
    return result;
}

void TestMap() {
    array<int> scores = {85, 92, 78};
    auto strings = MapToString(scores, function(int s) {
        return "Score: " + s;
    });
}
```

#### Example 6: Custom Sort Comparator with Capture
```angelscript
void SortByProximity(array<int> &items, int target) {
    auto comp = function[target](int a, int b) {
        int distA = a > target ? a - target : target - a;
        int distB = b > target ? b - target : target - b;
        return distA < distB;
    };
    // Use comp to order elements closest to target
}
```

---

### Use Case 3: State Machines & Deferred Execution

#### Example 7: Stateful Generator / Counter Closure
```angelscript
function<int()>@ MakeCounter(int start, int step) {
    int current = start;
    return function[current, step]() {
        int val = current;
        current += step;
        return val;
    };
}

void TestCounter() {
    auto ticker = MakeCounter(10, 5);
    int a = ticker(); // 10
    int b = ticker(); // 15
    int c = ticker(); // 20
}
```

#### Example 8: Command Pattern / Deferred Action Queue
```angelscript
class CommandQueue {
    array<function<void()>@> queue;
    void Enqueue(function<void()>@ action) { queue.insertLast(action); }
    void ExecuteAll() {
        for (uint i = 0; i < queue.length(); i++)
            queue[i]();
        queue.resize(0);
    }
}
```

#### Example 9: Nested Currying / Function Factory
```angelscript
function<int(int)>@ MakeMultiplier(int factor) {
    return function[factor](int value) {
        return factor * value;
    };
}

void TestCurry() {
    auto triple = MakeMultiplier(3);
    auto septuple = MakeMultiplier(7);
    int res1 = triple(10);   // 30
    int res2 = septuple(10); // 70
}
```

---

### Use Case 4: Pipeline Processing & Math Operations

#### Example 10: Mathematical Operation Pipeline
```angelscript
funcdef float MathOp(float);

float Pipeline(float initial, array<function<float(float)>@> &in ops) {
    float val = initial;
    for (uint i = 0; i < ops.length(); i++)
        val = ops[i](val);
    return val;
}

void TestPipeline() {
    float bias = 2.5f;
    array<function<float(float)>@> ops;
    ops.insertLast(function(float x) { return x * 2.0f; });
    ops.insertLast(function[bias](float x) { return x + bias; });
    ops.insertLast(function(float x) { return x / 2.0f; });

    float output = Pipeline(10.0f, ops); // ((10 * 2) + 2.5) / 2 = 11.25
}
```

#### Example 11: Interoperability with Legacy Nominal Funcdefs
```angelscript
funcdef bool LegacyValidator(int);

bool ValidateAll(const array<int> &in items, LegacyValidator@ validator) {
    for (uint i = 0; i < items.length(); i++) {
        if (!validator(items[i])) return false;
    }
    return true;
}

void TestInterop() {
    array<int> data = {2, 4, 6, 8};
    // Passing modern canonical closure directly to legacy nominal validator:
    int maxLimit = 10;
    bool valid = ValidateAll(data, function[maxLimit](int val) {
        return val > 0 && val < maxLimit;
    });
}
```

---

## 9. Branch Review Guide (Commits Ordered by Risk)

Review the branch `feature/lambda-closures` in the following order:

1. **Commit `e550faca` (Lowest Risk — Core Types & AST Foundations)**:
   - *Phase 1*: Canonical function type parsing (`function<R(Args)>@`), engine canonical cache, test harness.
2. **Commit `e50447cc` (Low Risk — Syntax & Parser)**:
   - *Phase 2*: Capture list AST node `snCaptureList`, error handling for invalid syntax forms (`[&]`, `[=]`).
3. **Commit `09ddf86c` (Medium Risk — Type Deduction)**:
   - *Phase 3*: Speculative probe compilation for `auto` return-type inference and strict return unification.
4. **Commit `b5b8af13` (Low Risk — Funcdef Interop)**:
   - *Phase 5*: Token-splitting in explicit casts (`cast<function<...>>`), structural signature equality bridging canonical and nominal funcdefs.
5. **Commit `0b4581fe` (Low Risk — Serialization & Tooling)**:
   - *Phase 6*: Precompiled bytecode serialization round-trip, factory stub pairing, module reflection filtering.
6. **Commits `5b263f0f` & `143b0e7d` (Highest Risk — Runtime Closure Synthesis & VM Delegates)**:
   - *Phase 4*: Context class generation (`$closure$`), property binding, delegate creation via `$dlgte`, cyclic garbage collection, and variable resolution isolation.

