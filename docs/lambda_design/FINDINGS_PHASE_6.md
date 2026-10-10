# Phase 6 Findings: Bytecode Save/Load, C++ API, Declarations, Tooling, Docs

## Executive Summary
Phase 6 focuses on production hardening and ecosystem integration for modern lambdas and closures in AngelScript:
1. **Bytecode Serialization (`as_restore.cpp`)**: Ensuring modules containing canonical function types (`function<R(Args)>@`), stateless lambdas, and stateful closures (`$closure$...` context classes, `$call` methods, `$dlgte` delegate allocation) round-trip seamlessly through `SaveByteCode` and `LoadByteCode`.
2. **Public API Surface & Visibility**: Enforcing clear visibility boundaries where synthetic entities (`$closure$` classes, synthetic lambda functions) are hidden from enumeration APIs (`GetObjectTypeCount`, `GetFunctionCount`), but fully resolvable by pointer and type ID.
3. **Declarations**: Validating human-readable declaration formatting and bidirectional parser round-tripping for `function<R(Args)>@` across `GetTypeDeclaration`, `GetTypeIdByDecl`, and `GetDeclaration`.
4. **Host-Side Usage**: Documenting and verifying C++ host lifecycle, context preparation, argument passing, and AddRef/Release ownership discipline for closures and canonical function handles.
5. **Tooling & Add-ons**: Verifying compatibility with the standard add-on ecosystem (`serializer`, `debugger`, `scriptbuilder`, `scripthelper`) and JIT/line-callback hooks.

---

## 1. Bytecode Serialization Analysis (`as_restore.cpp` & `as_restore.h`)

### 1.1 Structural Layout of Saved Bytecode
When `asCModule::SaveByteCode` executes, `asCWriter::Write()` serializes module components in a deterministic multi-pass sequence:
1. `stripDebugInfo` flag (1 byte encoded).
2. Enum types (`m_enumTypes`).
3. Class type declarations - Phase 1 (`m_classTypes` names, flags, sizes, namespaces).
4. Funcdefs (`m_funcDefs` via `WriteFunction`).
5. Class type declarations - Phase 2 (Interface methods, class methods, behaviors, virtual function tables).
6. Class type declarations - Phase 3 (Class member properties).
7. Typedefs (`m_typeDefs`).
8. Global variables (`m_scriptGlobals`).
9. Script functions with `objectType == 0` (`m_scriptFunctions`).
10. Global function symbol table (`m_globalFunctions`).
11. Bind informations / imports (`m_bindInformations`).
12. Used types cache (`usedTypes`).
13. Used type IDs cache (`usedTypeIds`).
14. Used functions cache (`usedFunctions`).
15. Used global properties cache (`usedGlobalProperties`).
16. Used object properties cache (`usedObjectProperties`).
17. Used string constants (`usedStringConstants`).
18. Bytecode instructions for all script functions.

### 1.2 Serialization of Canonical Function Types
- **Storage**: Any canonical funcdef used in a module is registered in `module->m_funcDefs` (via `engine->GetCanonicalFuncdef`).
- **Write**: `WriteFunction` writes the funcdef signature:
  - Name: `"function<R(P...)>"`
  - Return type: `returnType` (with flags)
  - Parameter count and parameter types
  - `inOutFlags`
  - Function type: `asFUNC_FUNCDEF`
  - Namespace: Global (`""`)
- **Read**:
  - `ReadFunction` restores the funcdef and instantiates `asCFuncdefType`.
  - In `ReadInner` (lines 318-358), if the funcdef is shared, the reader checks `engine->funcDefs` for an existing funcdef with identical name, namespace, and signature (`IsSignatureExceptNameEqual`).
  - If a matching canonical funcdef already exists in `engine->funcDefs`, the module funcdef is replaced with the pre-existing instance, preserving pointer and type ID identity across modules.
  - If not yet in `engine->funcDefs`, it is added to `module->m_funcDefs` and `engine->funcDefs`. Subsequent lookups via `engine->GetCanonicalFuncdef` will find this exact instance.

### 1.3 Serialization of Synthetic Functions (Stateless Lambdas)
- **Identity**: Stateless lambdas are compiled as global script functions with synthetic names (e.g. `$lambda$func$0` or `$void test()$0`) with `objectType == 0`.
- **Write**: Written in step 9 (`module->m_scriptFunctions` with `objectType == 0`). Line numbers, declared variables, section names, and bytecode instructions are written normally.
- **Read**: Restored as script functions in `module->m_scriptFunctions`. Their IDs are allocated dynamically via `engine->GetNextScriptFunctionId()`.

### 1.4 Serialization of Synthetic Context Classes (Stateful Closures)
- **Identity**: Each closure captures outer variables into a synthesized class named `$closure$<func-sig>$<index>`.
- **Flags**: `asOBJ_REF | asOBJ_SCRIPT_OBJECT | asOBJ_GC`.
- **Write**:
  - Phase 1: Name, flags, size (`sizeof(asCScriptObject)`), namespace.
  - Phase 2: Behaviors (`destruct`, default `constructors`, default `factories`), methods (the synthesized member method `$call`), virtual function table.
  - Phase 3: Captured properties (the fields storing captured local copies/handles).
  - Script functions: The bytecode of `$call` and the constructor stub are written in `m_scriptFunctions`.
- **Read**:
  - Restores the class into `module->m_classTypes`.
  - Sets up standard script object behaviours (`addref`, `release`, `gcEnumReferences`, etc.).
  - Reconstructs constructor, `$call` method, and member properties.
  - Bytecode of `$call` is restored and bound to the class type.

### 1.5 Serialization of Closure Bytecode Instructions
Closure creation emits three key instructions:
1. `asBC_ALLOC (ot, ctorFuncId)`:
   - On write: `ot` is translated to an index in `usedTypes` via `FindTypeInfoIdx(ot)`. `ctorFuncId` is translated to an index in `usedFunctions` via `FindFunctionIndex`.
   - On read: `arg` translates index back to `asCObjectType*` via `FindType(arg)`, and `fid` translates back to the constructor function via `FindFunction(fid-1)`.
2. `asBC_REFCPY (ot)`:
   - Used to copy captured handles and values into closure fields.
   - For handles/objects: `ot` is translated to an index in `usedTypes`.
   - For funcdefs: `ot` is `&engine->functionBehaviours`. `WriteTypeInfo` and `ReadTypeInfo` specifically encode this as internal type name `"$func"`.
3. `asBC_CALLSYS (fid)`:
   - Invokes the delegate factory `$dlgte`.
   - `DELEGATE_FACTORY` is explicitly recognized in `as_restore.cpp` (lines 930, 1056, 4270), ensuring `$dlgte` calls resolve to the host engine's delegate factory without re-registering or serializing internal structures.

---

## 2. Public API Surface & Visibility

### 2.1 API Survey
| API | Target | Baseline Behavior | Synthetic Entity Visibility Decision |
|---|---|---|---|
| `GetFunctionCount` / `GetFunctionByIndex` | `asIScriptModule` | Returns `m_globalFunctions.GetSize()`. | **Hidden by design**. Synthetic functions live in `m_scriptFunctions`, not in `m_globalFunctions`. |
| `GetObjectTypeCount` / `GetObjectTypeByIndex` | `asIScriptModule` | Returns `m_classTypes.GetLength()`. | **Filter synthetic `$closure$` classes**. Return only user script classes (`!name.StartsWith("$")`). |
| `GetFuncdefCount` / `GetFuncdefByIndex` | `asIScriptEngine` | Returns `registeredFuncDefs.GetLength()`. | **No change**. Canonical funcdefs are internal engine funcdefs (`engine->funcDefs`), not application-registered funcdefs. |
| `GetObjectTypeCount` / `GetObjectTypeByIndex` | `asIScriptEngine` | Returns `registeredObjTypes.GetLength()`. | **No change**. Only application-registered object types. |
| `GetTypeInfoByName` | `asIScriptModule` | Looks up `m_typeLookup`. | **Accessible**. Direct lookup by full name (e.g. `"$closure$..."`) succeeds if explicitly requested. |
| `GetTypeInfoById` | `asIScriptEngine` | Looks up type by ID. | **Accessible**. Resolves `$closure$` classes and canonical funcdefs by ID without restriction. |
| `GetFunctionById` | `asIScriptEngine` | Looks up function by ID. | **Accessible**. Resolves `$call` methods and synthetic lambda functions. |

### 2.2 Rationale for Filtering `asIScriptModule::GetObjectTypeCount`
`asIScriptModule::GetObjectTypeCount()` and `GetObjectTypeByIndex(index)` are used by tools (reflection inspectors, documentation generators, IDE symbol outlines, and test dumps such as `test_dump.cpp`).
If compiler-synthesized `$closure$` context classes are included:
- Outlines and documentation are polluted with internal implementation details.
- Serialization and reflection tools trying to treat them as normal user classes could fail or produce invalid scripts.
By filtering out types where `name[0] == '$'`:
- Enumeration returns only user-declared script classes and interfaces.
- The engine, GC, VM, and C++ host can still resolve closure types by ID, pointer, or `GetTypeInfoByName`.

---

## 3. Declarations and Round-Trip Validation

### 3.1 Formatting Specifications
1. **Canonical Funcdef Types**:
   - `asCFuncdefType::GetName()`: `"function<R(Args)>"` (e.g. `"function<int(int)>"`).
   - `asCScriptFunction::GetDeclarationStr()`: `"function<R(Args)>"`.
   - `asCDataType::Format()`:
     - Value / unhandled: `"function<R(Args)>"`.
     - Object handle: `"function<R(Args)>@"`.
     - Const handle: `"const function<R(Args)>@"`.
2. **Stateless Lambda Functions**:
   - `func->GetName()`: `"$lambda$func$0"` or `"$void test()$0"`.
   - `func->GetDeclaration()`: standard function signature, e.g. `"int $void test()$0(int)"`.
3. **Closure Classes & Methods**:
   - Closure class name: `"$closure$<caller-sig>$<index>"`.
   - Closure method name: `"$call"`.
   - Delegate function: `func->GetName()` returns `""`, `func->GetFuncType()` returns `asFUNC_DELEGATE`. `func->GetDelegateFunction()->GetName()` returns `"$call"`.

### 3.2 Bidirectional Parser Round-Trip
- `engine->GetTypeIdByDecl("function<int(int)>@")`:
  - Parses via `asCBuilder::ParseDataType` -> `ParseType` -> `ParseFunctionType`.
  - Re-resolves via `engine->GetCanonicalFuncdef`.
  - Returns identical type ID.
- `engine->GetTypeDeclaration(typeId)`:
  - Formats data type as `"function<int(int)>@"`.
- `engine->GetTypeIdByDecl(engine->GetTypeDeclaration(typeId)) == typeId`:
  - Guaranteed identity round-trip.

---

## 4. Host-Side Usage Guide & Lifecycle Discipline

### 4.1 Invoking Closures and Function Pointers from C++
```cpp
// 1. Script returns a closure or function handle
asIScriptFunction *getClosure = mod->GetFunctionByName("GetClosure");
asIScriptContext *ctx = engine->CreateContext();
ctx->Prepare(getClosure);
ctx->Execute();

// 2. Retrieve returned handle
asIScriptFunction *closure = (asIScriptFunction*)ctx->GetReturnAddress();

// IMPORTANT LIFECYCLE RULE:
// If reusing the same context 'ctx', call closure->AddRef() before ctx->Prepare(closure),
// because ctx->Prepare() cleans up the return object of the previous execution!
// Alternatively, use a separate context for execution:
asIScriptContext *ctx2 = engine->CreateContext();
ctx2->Prepare(closure);
ctx2->SetArgDWord(0, 42);
ctx2->Execute();
int result = ctx2->GetReturnDWord();
ctx2->Release();

// 3. Clean up the original context
ctx->Release(); // Releases closure return value automatically
```

### 4.2 Passing Handles into C++ Functions
When registering C++ functions that receive `function<...>` handles:
```cpp
// Option A: Pass by value (handle transferred)
engine->RegisterGlobalFunction("void OnClick(function<void(int)>@ cb)", asFUNCTION(OnClick), asCALL_CDECL);
void OnClick(asIScriptFunction *cb) {
    if (cb) {
        // cb is AddRef'd by caller; C++ takes ownership and MUST release it!
        cb->Release();
    }
}

// Option B: Auto-handle (AngelScript releases automatically)
engine->RegisterGlobalFunction("void OnClick(function<void(int)>@+ cb)", asFUNCTION(OnClick), asCALL_CDECL);
void OnClick(asIScriptFunction *cb) {
    // cb is valid for duration of call; C++ does NOT release it unless storing
}
```

---

## 5. Tooling, Add-ons & Debugger Compatibility

1. **Serializer Add-on (`serializer.cpp`)**:
   - Serializes global variables via `mod->GetGlobalVarCount()`.
   - Global variables holding `function<...>` handles or closures serialize and restore correctly without modifying the serializer code.
2. **Debugger Add-on (`debugger.cpp`)**:
   - Inspects call stacks via `ctx->GetFunction()`.
   - When inside a closure, `ctx->GetFunction()` reports name `"$call"`, type `asFUNC_SCRIPT`, with correct file section and line numbers.
   - Stepping into a closure steps through `$call` bytecode and hits breakpoints accurately.
3. **ScriptHelper Add-on (`scripthelper.cpp`)**:
   - `WriteConfigToFile` only enumerates `registeredObjTypes` and `registeredFuncDefs`.
   - Canonical and synthetic types do not contaminate offline config dumps.
4. **JIT Compiler**:
   - Synthetic functions are created during `Build()` and participate in JIT compilation passes before execution.

---

## 6. Implementation Action Plan for Phase 6
1. **API Visibility**: Update `asCModule::GetObjectTypeCount()` and `asCModule::GetObjectTypeByIndex()` to skip synthetic classes (`name[0] == '$'`).
2. **Save/Load Round-Trip Testing**: Implement Phase 6 tests in `test_lambda.cpp`:
   - Group 1: Bytecode save/load round-trip for stateless lambdas and stateful closures across independent engine instances.
   - Group 2: Public API visibility tests (`GetObjectTypeCount`, `GetFunctionCount`, `GetTypeInfoByName`, `GetTypeIdByDecl`).
   - Group 3: Declaration formatting and bidirectional round-trip checks.
   - Group 4: Serializer add-on and debugger compatibility verification.
3. **Comprehensive Documentation**: Create `docs/lambda_design.md` containing full developer guide, syntax reference, architecture, bytecode model, and GC details.
