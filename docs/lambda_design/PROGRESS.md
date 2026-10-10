# Progress & Design Tracking: Modern Anonymous Functions and Closures

## Project Overview
- **Branch**: `feature/lambda-closures`
- **Engine Baseline**: AngelScript 2.39.0 WIP
- **Target OS & Toolchain**: Windows, MSVC 19.50 (Visual Studio 2026 Community)
- **Baseline Test Suite Status**:
  - Binary: `sdk/tests/test_feature/bin/Release/test_feature.exe`
  - Result: 100% Passed (All tests passed with success, 0 failures)
  - Memory Statistics:
    - Number of allocations: 3,198,992
    - Max allocated memory at any one time: 163,324,460 bytes
    - Max simultaneous allocations: 1,500,425
    - Total allocated memory: 565,908,383 bytes

---

## Phase Status Summary

| Phase | Description | Status | Findings Doc | Tests Added | Suite vs Baseline |
|---|---|---|---|---|---|
| **Phase 1** | Canonical Function Type (`function<R(P...)>@`) | Completed | `FINDINGS_PHASE_1.md` | `test_lambda.cpp` (5 comprehensive test groups) | 100% Pass (0 regressions) |
| **Phase 2** | Parser & AST for `function[captures](params)` | Completed | `FINDINGS_PHASE_2.md` | `test_lambda.cpp` (5 Phase 2 test groups) | 100% Pass (0 regressions) |
| **Phase 3** | Expected-Type Validation, Return Inference & auto | Completed | `FINDINGS_PHASE_3.md` | `test_lambda.cpp` (5 Phase 3 test groups) & `test_auto.cpp` | 100% Pass (0 regressions) |
| **Phase 4** | Stateful Closures (Value Capture, Scope Validation, Delegates) | Completed | `FINDINGS_PHASE_4.md` | `test_lambda.cpp` (14 comprehensive test groups covering all prompt scenarios) | 100% Pass (0 regressions) |
| **Phase 5** | Implicit Conversion Between `function<R(P...)>` & Nominal funcdefs | Completed | `FINDINGS_PHASE_5.md` | `test_lambda.cpp` (5 Phase 5 test groups) | 100% Pass (0 regressions) |
| **Phase 6** | Bytecode Save/Load, C++ API, Tooling, Docs | Completed | `FINDINGS_PHASE_6.md` | `test_lambda.cpp` (4 Phase 6 test groups) | 100% Pass (0 regressions) |

---

## Architectural & Semantic Decisions Log
1. **Canonical Function Type**:
   - Represented internally as `asCFuncdefType` with canonical signature and name `function<R(P...)>`.
   - Structural type equivalence via existing `asCScriptFunction::IsSignatureExceptNameEqual()` enables zero-cost bidirectional conversions to/from nominal funcdefs (`asCC_REF_CONV`).
   - Contextual keyword disambiguation: `function` is recognized as a type ONLY when followed by `<` matching a valid canonical function type (`IsFunctionType`). Variables named `function` continue to compile seamlessly with 100% backward compatibility.
   - Module-scoped vs Engine-scoped lifecycle: If all types in the signature are shared/primitive, canonical funcdefs are cached at engine level (`engine->funcDefs`, `SetShared(true)`). If any parameter or return type is a non-shared script class from a module, the funcdef is module-scoped (`module->m_funcDefs`, `SetShared(false)`), preventing dangling pointers and memory leaks upon module discard.
2. **Execution via VM Delegates**:
   - Stateful closures execute using existing VM delegate support (`asFUNC_DELEGATE`), with a compiler-synthesized closure context class (`$closure$...`) as `objForDelegate`.
   - Stateless lambdas remain zero-overhead static script functions (`asFUNC_SCRIPT`).
3. **Capture Semantics**:
   - Capture list is explicit: `function[a, b, this](...) { ... }`.
   - Capture by value: private mutable copies in closure class fields.
   - Disallowed capture syntaxes (`[&]`, `[=]`, `[&x]`) emit clean "not supported yet" diagnostics.
   - Referenced outer locals not in the capture list emit: `"variable 'x' is not captured; use function[x](...)"`.
   - Referenced outer `this` not in the capture list emits: `"variable 'this' is not captured; use function[this](...)"`.
   - Capturing non-copyable types emits: `"Cannot capture object of non-copyable type '%s' by value"`.
4. **Return-Type Inference & auto Mechanics**:
   - In `auto f = function(explicit_params) { body };` and `auto@ f = ...`, return type is speculatively deduced from return expressions via a probe compilation pass using an isolated compiler instance (`m_isDeducingReturnType = true`).
   - Strict return unification: all return expressions in a lambda must match identical type or emit `TXT_DIVERGENT_RETURN_TYPES_s_s`.
   - Normalization: references decay to values (`MakeReference(false)`), const on values decays (`MakeReadOnly(false)`), object handles are preserved.
   - Parameter types must be explicitly specified when using `auto` (`TXT_LAMBDA_PARAMS_MUST_BE_TYPED_FOR_AUTO`). Target funcdefs continue to infer untyped parameter types.
   - Canonical funcdefs are resolved dynamically via `engine->GetCanonicalFuncdef`, and the lambda is converted and registered with the builder.
5. **Garbage Collection**:
   - Synthesized closure context classes register `asOBJ_REF | asOBJ_SCRIPT_OBJECT | asOBJ_GC` behaviours to guarantee cyclic references (e.g. `this -> closure -> this`) are collectable by the GC.

---

## Phase 1 Deliverables Summary
- **Files Modified**:
  - `sdk/angelscript/source/as_parser.h`: Declared `IsFunctionType` and `ParseFunctionType`.
  - `sdk/angelscript/source/as_parser.cpp`:
    - Hooked `ParseFunctionType` in `ParseType`.
    - Implemented `IsFunctionType` and `ParseFunctionType`.
    - Updated `IsDataType` and `CheckTemplateType` to contextually recognize `function<...>` while preserving `function` as an identifier for variables.
  - `sdk/angelscript/source/as_builder.cpp`:
    - In `CreateDataTypeFromNode`: detected `FUNCTION_TOKEN` with function type children, parsed return type and parameters, called `engine->GetCanonicalFuncdef`, and created `asCDataType` pointing to the canonical `asCFuncdefType`.
  - `sdk/angelscript/source/as_scriptengine.h`:
    - Declared `GetCanonicalFuncdef`.
  - `sdk/angelscript/source/as_scriptengine.cpp`:
    - Implemented `GetCanonicalFuncdef` with engine-level cache for shared types and module-scoped storage for non-shared types, signature matching via `IsSignatureExceptNameEqual`, and module ownership.
  - `sdk/angelscript/source/as_scriptfunction.cpp`:
    - Updated `GetDeclarationStr` to return canonical declaration for canonical funcdefs.
  - `sdk/tests/test_feature/projects/cmake/CMakeLists.txt`:
    - Added `test_lambda.cpp` to compilation sources.
  - `sdk/tests/test_feature/source/main.cpp`:
    - Registered and invoked `TestLambda::Test()`.
  - `sdk/tests/test_feature/source/test_lambda.cpp`:
    - Added dedicated test suite covering 5 comprehensive groups.
  - `sdk/tests/test_feature/source/test_functionptr.cpp`:
    - Restored to pristine baseline state.

- **Suite Results vs Baseline**:
  - Full suite passed: 100% (0 failures, 0 regressions).

---

## Phase 2 Deliverables Summary
- **Files Modified**:
  - `sdk/angelscript/source/as_scriptnode.h`: Added `snCaptureList` to `enum eScriptNode`.
  - `sdk/angelscript/source/as_texts.h`: Added syntax error constants for rejected forms.
  - `sdk/angelscript/source/as_parser.cpp`: Extended lookahead and parsing for `function[captures](params) { body }`.
  - `sdk/angelscript/source/as_scriptfunction.h`: Added `captureNames` to `asCScriptFunction`.
  - `sdk/angelscript/source/as_builder.cpp`: Populated `func->captureNames` during lambda registration.
  - `sdk/tests/test_feature/source/test_lambda.cpp`: Added 5 test groups verifying syntax, fuzzing, error diagnostics.

- **Suite Results vs Baseline**:
  - Full suite passed: 100% (0 failures, 0 regressions).

---

## Phase 3 Deliverables Summary
- **Files Modified**:
  - `sdk/angelscript/source/as_texts.h`: Added return inference error constants.
  - `sdk/angelscript/source/as_compiler.h` / `as_compiler.cpp`: Implemented probe compilation for return deduction and `auto` integration.
  - `sdk/tests/test_feature/source/test_auto.cpp`: Updated expected diagnostics.
  - `sdk/tests/test_feature/source/test_lambda.cpp`: Added 5 test groups for expected types and `auto`.

- **Suite Results vs Baseline**:
  - Full suite passed: 100% (0 failures, 0 regressions).

---

## Phase 4 Deliverables Summary
- **Files Modified**:
  - `sdk/angelscript/source/as_texts.h`:
    - Added diagnostic constants:
      - `TXT_VARIABLE_s_NOT_CAPTURED` (`"variable '%s' is not captured; use function[%s](...)"`)
      - `TXT_THIS_NOT_CAPTURED` (`"variable 'this' is not captured; use function[this](...)"`)
      - `TXT_CANNOT_CAPTURE_THIS_OUTSIDE_METHOD` (`"Cannot capture 'this' outside of class methods"`)
      - `TXT_CAPTURE_VARIABLE_s_NOT_FOUND` (`"Capture variable '%s' is not found in enclosing scope"`)
      - `TXT_CANNOT_COPY_NONCOPYABLE_TYPE_s` (`"Cannot capture object of non-copyable type '%s' by value"`)
  - `sdk/angelscript/source/as_scriptfunction.h` / `as_scriptfunction.cpp`:
    - Added `outerVariableNames` (`asCArray<asCString>`) and `outerHasThis` (`bool`) to track enclosing symbols for uncaptured access diagnostics.
  - `sdk/angelscript/source/as_builder.h` / `as_builder.cpp`:
    - Implemented `SynthesizeClosureClass` allocating `asCObjectType` with `asOBJ_REF | asOBJ_SCRIPT_OBJECT | asOBJ_GC`, default script class behaviours, and compiled constructor stub.
    - Updated `RegisterLambda` to support member methods `$call` on closure types.
  - `sdk/angelscript/source/as_compiler.h` / `as_compiler.cpp`:
    - Implemented `GetOuterVariables` to collect enclosing locals and class properties.
    - Implemented full closure synthesis in `ImplicitConvLambdaToFunc`:
      - Validation of each capture item (local variable, parameter, class property, `this`).
      - Synthesis of `$closure$` class with captured properties.
      - Compilation of `$call` method and virtual function table registration for delegate dispatch.
      - Emitting bytecode for closure allocation (`asBC_ALLOC`), property copying with value isolation and handle AddRef (`asBC_REFCPY`), and delegate creation (`$dlgte` via `asBC_CALLSYS`).
      - Correct stack value pushing (`asBC_PshVPtr`) to return the delegate handle.
      - Handle dereference via `Dereference(&rctx, true)` and const `this` handling via `MakeHandleToConst(true)`.
    - Added uncaptured variable and uncaptured `this` checks in `CompileVariableAccess` and `CompileFunctionCall`.
  - `sdk/tests/test_feature/source/test_lambda.cpp`:
    - Added 14 comprehensive Phase 4 test groups covering all 13 prompt scenarios:
      - Phase 4 Test 1: Stateful mutation, escaping closures, and independent instances (`c1` vs `c2`).
      - Phase 4 Test 2: Value capture isolation (modifying outer variable doesn't affect closure copy and vice-versa).
      - Phase 4 Test 3: Handle capture keeps referenced object alive after outer handle is cleared (`@obj = null; f();`).
      - Phase 4 Test 4: Capturing `this` in `const` method makes `$this` a const handle (reads pass, writes fail).
      - Phase 4 Test 5: Diagnostics for uncaptured outer locals, uncaptured `this`, non-existent captures, non-copyable types.
      - Phase 4 Test 6: Cyclic reference collection between script class and closure delegate (`asGC_FULL_CYCLE`).
      - Phase 4 Test 7: Auto return inference with stateful closures (`auto f = function[a](int x) { return x + a; };`).
      - Phase 4 Test 8: Value capture & stateful mutation (`count=0; ++count; count==0`) and multiple independent closures from same scope.
      - Phase 4 Test 9: Mixed capture (`function[this, x]() { memberVar += x; }`) with implicit member access.
      - Phase 4 Test 10: Nested closures (`function[x](int y) { return function[x, y](int z) { return x + y + z; }; }`).
      - Phase 4 Test 11: Closure invoked directly from C++ host via `asIScriptContext::Execute`.
      - Phase 4 Test 12: Debugger line callback stepping and synthetic function name inspection.
      - Phase 4 Test 13: Exception unwinding inside closure.
      - Phase 4 Test 14: Diagnostics for `this` captured outside class method and disallowed captures (`[&x]`, `[=]`).

- **Suite Results vs Baseline**:
  - Full suite passed: 100% (0 failures, 0 regressions, `-- TestLambda passed`).

---

## Phase 5 Deliverables Summary
- **Files Modified**:
  - `sdk/angelscript/source/as_parser.cpp`:
    - Updated `ParseCast` (`lines 1780-1790`) to split adjacent `>>` / `>>>` tokens via `SetPos(t1.pos + 1)` when `script->code[t1.pos] == '>'`, enabling `cast<function<...>>` without requiring whitespace workarounds.
  - `docs/lambda_design/FINDINGS_PHASE_5.md`:
    - Comprehensive analysis of implicit conversions, type equality routines, runtime representation, overload ranking, and host interactions.
  - `sdk/tests/test_feature/source/test_lambda.cpp`:
    - Added 5 comprehensive Phase 5 test groups:
      - Phase 5 Test 1: Eight Core Combinations and Class Members (bidirectional canonical <-> nominal conversion in assignment, argument passing, return statement, array element insertion, class member storage, identity comparison `is`/`!is`).
      - Phase 5 Test 2: Explicit Casts with token splitting (`cast<function<int(int)>>(nom)` and `cast<UnaryOp>(can)`).
      - Phase 5 Test 3: Mismatched Signature Rejections (return type mismatch, parameter count mismatch, parameter type mismatch, modifier mismatch, differing nominal funcdefs).
      - Phase 5 Test 4: Overload Resolution Predictability (exact match priority, funcdef conversion over non-match, ambiguity detection for competing signatures).
      - Phase 5 Test 5: C++ Host API Interoperability (registered funcdef, registered functions taking nominal and canonical funcdefs, passing lambdas/closures across language boundary, host executing returned closure).

- **Suite Results vs Baseline**:
  - Full suite passed: 100% (0 failures, 0 regressions, `-- TestLambda passed`).

---

## Phase 6 Deliverables Summary
- **Files Modified**:
  - `docs/lambda_design/FINDINGS_PHASE_6.md`:
    - Comprehensive analysis of bytecode serialization architecture, public API visibility and reflection filtering, declaration formatting and round-trip identity, host lifecycle management, and tooling compatibility.
  - `docs/lambda_design.md`:
    - Definitive specification and reference manual detailing user syntax, compiler architecture, bytecode generation, serialization, public API guarantees, and C++ host integration guide.
  - `sdk/angelscript/source/as_module.cpp`:
    - Filtered synthetic functions whose name starts with `$` from `asIScriptModule::GetFunctionCount()` and `GetFunctionByIndex()`, preserving user entry point enumeration while keeping synthetic functions accessible via pointer and ID.
    - Filtered synthetic closure context classes whose name starts with `$` from `asIScriptModule::GetObjectTypeCount()` and `GetObjectTypeByIndex()`.
  - `sdk/angelscript/source/as_builder.cpp`:
    - In `asCBuilder::SynthesizeClosureClass`, added and compiled matching default factory function for each synthesized closure class, maintaining 1:1 constructor/factory pairing required by bytecode serializer `asCWriter` and deserializer `asCReader`.
    - Corrected `st->beh.factories[0] = factoryId` to ensure the closure's own factory is serialized at index 0 rather than the engine dummy behaviour function `$beh0`.
    - Marked constructor and factory stubs shared when the closure class is shared (`isShared`).
  - `sdk/tests/test_feature/source/test_lambda.cpp`:
    - Added 4 comprehensive Phase 6 test groups:
      - Phase 6 Test 1: Bytecode Save/Load Round-Trip (precompiled module containing stateless lambdas, stateful closures with multiple captures, serialization via `CBytecodeStream`, discarding engine, restoring into pristine engine, module execution, direct C++ execution of returned closure).
      - Phase 6 Test 2: Public API Enumeration & Visibility (verifying `GetFunctionCount` / `GetFunctionByIndex` hide synthetic functions; `GetObjectTypeCount` / `GetObjectTypeByIndex` hide synthetic `$closure$` classes; type resolution by name).
      - Phase 6 Test 3: Declaration Formatting and Bidirectional Round-Trip (`GetTypeIdByDecl` <-> `GetTypeDeclaration` identity round-trip for canonical function types, with and without handles).
      - Phase 6 Test 4: Host-Side Lifecycle & Registered Parameter Auto-Handles (registering C++ function with `function<int(int)>@+` auto-handle parameter, calling from script passing closures and stateless lambdas, verifying automatic memory release).

- **Suite Results vs Baseline**:
  - Full suite passed: 100% (0 failures, 0 regressions, `-- TestLambda passed`).
  - All 110+ test suites in `test_feature` passed cleanly.




