# Phase 0 — Baseline & Deep Audit Findings

## Executive Summary
This document presents the findings of the exhaustive, read-only audit of AngelScript 2.39.0 WIP regarding anonymous functions, funcdefs, delegates, and types. All statements are supported by verified code locations (`file:line`).

---

## 1. Lexer and Parser Audit

### 1.1 `function` Token Status
- **Location**: `sdk/angelscript/source/as_tokendef.h:324`
  ```cpp
  const char * const FUNCTION_TOKEN = "function";
  ```
- **Evidence**: `as_tokenizer.cpp` does **not** contain `"function"` in its keyword token table. `function` is tokenized as `ttIdentifier`.
- **Parsing**: In `as_parser.cpp:1765` and `as_parser.cpp:1835`, the parser checks:
  ```cpp
  t.type == ttIdentifier && IdentifierIs(t, FUNCTION_TOKEN)
  ```
- **Consequences**:
  - `function` is a **contextual keyword**, not a globally reserved token.
  - User identifiers named `function` (e.g. `int function(int, int)`) remain valid and parse normally (as verified in `test_functionptr.cpp:1777-1786`).
  - For `function<...>` and `function[...]`, the parser must look ahead after the identifier `function` for `<` and `[` respectively.

### 1.2 Lambda Grammar Accepted Today
- **Location**: `sdk/angelscript/source/as_parser.cpp:1760-1905`
- **Grammar**:
  `// BNF:12: LAMBDA ::= 'function' '(' ((TYPE TYPEMOD)? IDENTIFIER? (',' (TYPE TYPEMOD)? IDENTIFIER?)*)? ')' STATBLOCK`
- **Details**:
  - `IsLambda()` (`as_parser.cpp:1760-1784`): Looks ahead for `function`, followed by `(`, skips tokens to matching `)`, and checks if the next token is `{` (`ttStartStatementBlock`). Then rewinds.
  - `ParseLambda()` (`as_parser.cpp:1827-1905`):
    - Parameter types are **optional** (`as_parser.cpp:1868`).
    - Type modifiers (`&`, `&in`, `&out`, `&inout`, `const`) **are allowed** via `ParseTypeMod(true)` (`as_parser.cpp:1872`).
    - Parameter names are **optional** (`as_parser.cpp:1877`).
    - Lambda body: Superficially parsed at parse time via `SuperficiallyParseStatementBlock()` (`as_parser.cpp:1902`), counting balanced braces. Full statement compilation is deferred to builder/compiler time.

### 1.3 AST Node Representation
- **Node Type**: `snFunction` (`sdk/angelscript/source/as_scriptnode.h:51`), created at `as_parser.cpp:1829`.
- **Structure**:
  - Children 0..N-1: Each parameter is represented by an `snUndefined` node (`as_parser.cpp:1855`).
    - Optional Child 1: `snDataType` from `ParseType(true)`.
    - Optional Child 2: `snUndefined` representing `ParseTypeMod(true)`.
    - Optional Child 3: `snIdentifier` from `ParseIdentifier()`.
    - If no type and name are given, `snUndefined` has 0 children (acting as a parameter slot marker).
  - Last Child: `snStatementBlock` from `SuperficiallyParseStatementBlock()` (`as_parser.cpp:1902`).

### 1.4 Type Parsing Path & Behavior with `function<int(int)>`
- **Location**: `as_parser.cpp:498-656` (`ParseType`, `ParseDataType`, `ParseTemplTypeList`)
- **Template handling**:
  - In `ParseType`: at line 529, checks `if (engine->IsTemplateType(tempString) && t.type == ttLessThan) ParseTemplTypeList(node);`.
  - In `ParseTemplTypeList`: expects `< TYPE (',' TYPE)* >`.
  - Token splitting: at line 637, `script->code[t.pos] == '>'` forces `SetPos(t.pos + 1)` which splits `>>` and `>>>` tokens.
- **Current failure on `function<int(int)>`**:
  1. `ParseDataType`: calls `IsDataType` (`as_parser.cpp:1123`), which queries `builder->DoesTypeExist("function")`. Because `"function"` is not a type, it fails with `TXT_IDENTIFIER_s_NOT_DATA_TYPE` ("Identifier 'function' is not a data type").
  2. Even if `"function"` were registered as a template type: `ParseTemplTypeList` parses the subtype `int`. The next token is `(` instead of `,` or `>`, causing a syntax error: `Expected token '>' (Instead found '(')`.

### 1.5 Disambiguation: Identifier followed by `[` or `<`
- In `ParseExprValue` (`as_parser.cpp:1640-1675`):
  - Checks `IsLambda()` first.
  - Distinguishes `<` as template vs relational operator using `engine->IsTemplateType()` / `engine->IsTemplateFn()` (`as_parser.cpp:1664, 1670`). If neither, `<` is treated as binary less-than.
  - For `[`: Post-expression indexing is parsed in `ParseExprPostOp` (`as_parser.cpp:1570`). By checking for `function[` in `IsLambda()`, lambda captures are parsed before any index operator can be evaluated.

### 1.6 Detection of Declaration vs Expression
- **Location**: `as_parser.cpp:2988-3050` (`IsVarDecl`) & `as_parser.cpp:1334-1442` (`FindTokenAfterType`)
- **Mechanism**:
  - `IsVarDecl()` looks ahead to check if the statement begins with a valid type.
  - `FindTokenAfterType` checks `CheckTemplateType(t1)` (`as_parser.cpp:1537`).
  - For `auto f = ...`: `t1.type == ttAuto` is explicitly allowed (`as_parser.cpp:1346, 1388`). `IsVarDecl()` returns `true`, and `ParseDeclaration()` parses `auto f = ...`.
  - For `function<...>@ f`: `FindTokenAfterType` currently fails at `CheckTemplateType` because `"function"` is not a template type. Once `FindTokenAfterType` recognizes `function<...>`, `IsVarDecl()` will automatically identify `function<...>@ f;` as a variable declaration!

---

## 2. Funcdef and Type-System Audit

### 2.1 Funcdef Representation
- **Classes**:
  - `asCFuncdefType` (`sdk/angelscript/source/as_typeinfo.h:236-258`): Derived from `asCTypeInfo`. Has flag `asOBJ_FUNCDEF`.
  - `asCScriptFunction` (`sdk/angelscript/source/as_scriptfunction.h:73-356`): Funcdef signature function (`funcType == asFUNC_FUNCDEF`).
- **Signature Fields**:
  - `returnType`: `asCDataType`
  - `parameterTypes`: `asCArray<asCDataType>`
  - `inOutFlags`: `asCArray<asETypeModifiers>`
  - `defaultArgs`: `asCArray<asCString*>`
  - `name`: `asCString`
  - `nameSpace`: `asSNameSpace*`
  - `parentClass`: `asCObjectType*` (for child funcdefs declared inside classes; null for global)

### 2.2 Ownership & Lifetimes
- **Engine level**: `asCScriptEngine::funcDefs` (`as_scriptengine.h:420`) and `registeredFuncDefs` (`line 351`). Non-refcounted tracking pointers.
- **Module level**: `asCModule::m_funcDefs` (`as_module.h:172`).
- **Refcounting**: Managed by `asCTypeInfo::internalRefCount` and `externalRefCount`.
- **Module Discard**: In `asCModule::Discard()` (`as_module.cpp:780-798`), calls `func->DestroyInternal()` and `m_engine->RemoveFuncdef(func)`. If shared, ownership transfers to another module via `FindNewOwnerForSharedType`.

### 2.3 `asCDataType` Fields & Modifiers
- **Fields** (`as_datatype.h:144-155`):
  - `tokenType`: `eTokenType`
  - `typeInfo`: `asCTypeInfo*` (points to `asCFuncdefType` for funcdefs)
  - `isReference`: `bool : 1`
  - `isReadOnly`: `bool : 1`
  - `isObjectHandle`: `bool : 1`
  - `isConstHandle`: `bool : 1`
  - `isAuto`: `bool : 1`
- **Modifier Interactions for Funcdefs**:
  - Funcdefs **require** handles to be instantiated: `CanBeInstantiated()` (`as_datatype.cpp:400`) returns false unless `isObjectHandle == true`.
  - `MakeHandle(true)` sets `isObjectHandle = true`, `isConstHandle = false`.
  - `MakeReadOnly(true)` on a handle sets `isConstHandle = true` (const handle).
  - `MakeHandleToConst(true)` sets `isReadOnly = true` (handle to const function).
  - `MakeReference(true)` sets `isReference = true` (`Callback@&`).

### 2.4 Comparison & Conversion: Nominal Compatibility Rule
- **Equality**: `asCDataType::operator==` (`as_datatype.cpp:511`) checks `typeInfo == dt.typeInfo`. Nominally distinct funcdefs have different `typeInfo` pointers, so `dt1 == dt2` is false.
- **Implicit Conversion**: In `asCCompiler::ImplicitConversion` (`as_compiler.cpp:8234-8248`):
  ```cpp
  if( to.IsFuncdef() )
  {
      if( ctx->type.dataType.IsFuncdef() &&
          to.GetTypeInfo() != ctx->type.dataType.GetTypeInfo() )
      {
          asCScriptFunction *toFunc = CastToFuncdefType(to.GetTypeInfo())->funcdef;
          asCScriptFunction *fromFunc = CastToFuncdefType(ctx->type.dataType.GetTypeInfo())->funcdef;
          if( toFunc->IsSignatureExceptNameEqual(fromFunc) )
          {
              ctx->type.dataType.SetTypeInfo(to.GetTypeInfo());
              return asCC_REF_CONV;
          }
      }
  }
  ```
- **VERIFIED FACT**: Two nominal funcdefs with identical signatures **ARE compatible** in AngelScript today! They convert implicitly via pointer reinterpret (`asCC_REF_CONV`), verified in `test_functionptr.cpp:2348-2353`:
  `myfunc1 @f1 = func; myfunc3 @f3 = f1; assert(f1 is f3);`.

### 2.5 Template Caching vs Funcdef Caching
- Template instances (`asCScriptEngine::GetTemplateInstanceType`, `as_scriptengine.cpp:5884`) are `asCObjectType` instances stored in `templateInstanceTypes`.
- Funcdefs are `asCFuncdefType` instances stored in `engine->funcDefs`.
- **Verdict**: Canonical function types `function<R(Args)>` must follow the `asCFuncdefType` path (`engine->funcDefs` / `asCScriptEngine::FindMatchingFuncdef`), NOT the template instance object type path, to retain native VM call pointer compatibility.

---

## 3. Builder and Compiler Audit

### 3.1 Lifecycle of Lambda Assignment
- **`Callback@ cb = function(bool b){};`**:
  1. `CompileDeclaration` (`as_compiler.cpp:3168`): Allocates stack variable `cb`.
  2. `CompileExprValue` (`as_compiler.cpp:12224`): Hits `snFunction`. Calls `ctx->SetLambda(vnode)`. Sets `ctx->type.SetUndefinedFuncHandle()`. Evaluation is deferred.
  3. `ImplicitConversion` (`as_compiler.cpp:7914`): Detects `to.IsFuncdef() && ctx->IsLambda()`. Calls `ImplicitConvLambdaToFunc`.
  4. `ImplicitConvLambdaToFunc` (`as_compiler.cpp:7832`): Generates unique name `name.Format("$%s$%d", outFunc->GetDeclaration(), numLambdas)`. Calls `builder->RegisterLambda()`.
  5. `RegisterLambda` (`as_builder.cpp:5224`): Disconnects `snStatementBlock`. Calls `RegisterScriptFunction()`, assigning a function ID and registering in `engine->scriptFunctions`.
  6. `ImplicitConvLambdaToFunc`: Emits `asBC_FuncPtr` with the new function pointer.
  7. `CompileAssignment`: Emits `asBC_STOREOBJ`.
  8. `asCBuilder::CompileFunctions()` (`as_builder.cpp:1007`): Later compiles the lambda's body statements into bytecode.

### 3.2 Expected-Type Propagation
- In `ImplicitConvLambdaToFunc` (`as_compiler.cpp:7840-7864`):
  - If a parameter has no type node, `funcDef->parameterTypes[count]` supplies the type.
  - Return type is supplied by `funcDef->returnType`.
  - Passed to `RegisterScriptFunction` (`as_builder.cpp:5254`).

### 3.3 `auto` Mechanics
- In `asCCompiler::CompileAutoType` (`as_compiler.cpp:3090-3166`):
  - Line 3102 explicitly rejects lambdas:
    ```cpp
    if (compiledCtx.IsLambda())
    {
        // TODO: Should mention that the problem is the anonymous function
        Error(TXT_CANNOT_RESOLVE_AUTO, errNode);
        return false;
    }
    ```
  - For supported types: deduces `newType = compiledCtx.type.dataType`, strips reference (`MakeReference(false)`), and if `newType.SupportHandles()`, prefers handle (`MakeHandle(true)`).

### 3.4 Compilation Order
- `asCBuilder::Build()` (`as_builder.cpp:200-360`):
  - Pass 1..3: Register types, properties, function signatures.
  - Pass 4: Compile global variables.
  - Pass 5: Compile function bodies (`CompileFunctions`).
- **Implication**: Named functions cannot deduce return types because all signatures must be known before bodies are compiled. However, lambdas are registered dynamically during Pass 5, so local return-type deduction from statement blocks is feasible.

### 3.5 Local Variable Allocation & Outer Symbol Lookup
- Local variables have negative stack frame offsets (`l_fp - offset`).
- When a lambda body is compiled, it is compiled as an independent function.
- In `CompileVariableAccess` (`as_compiler.cpp:1592`): Symbol lookup searches only the lambda's local variables, method members (if any), and globals/namespaces.
- When an uncaptured outer local `x` is referenced, lookup fails and line 1650 emits:
  `Error("No matching symbol 'x'", node);`.

---

## 4. VM, Delegates and Function Pointers

### 4.1 Runtime Representation & Bytecode
- Function pointer: `asCScriptFunction*` pointer stored on the stack.
- `asBC_FuncPtr`: Opcode `PTR`. Pushes `asCScriptFunction*` onto stack.
- `asBC_CallPtr`: Opcode `SWORDARG1` (stack offset of variable). Calls `as_context.cpp:4369`.

### 4.2 `asFUNC_DELEGATE` Mechanics
- In `as_scriptfunction.cpp:172-208`:
  - `CreateDelegate(func, obj)` allocates an `asCScriptFunction` with `funcType = asFUNC_DELEGATE`.
  - Holds `funcForDelegate` (`AddRef()`) and `objForDelegate` (`AddRefScriptObject()`).
  - Parameter types and return type are copied from `funcForDelegate`.
- In `as_context.cpp:4398-4422` (`asBC_CallPtr`):
  ```cpp
  else if (func->funcType == asFUNC_DELEGATE)
  {
      m_regs.stackPointer -= AS_PTR_SIZE;
      *(asPWORD*)m_regs.stackPointer = asPWORD(func->objForDelegate);
      m_regs.programPointer += 2;
      CallInterfaceMethod(func->funcForDelegate);
  }
  ```
  The object pointer is pushed to the stack as `this`, and `CallInterfaceMethod` invokes the method!
- Destructor (`as_scriptfunction.cpp:230`): Releases both `funcForDelegate` and `objForDelegate`.

### 4.3 Delegate Restrictions
- Null object is rejected: `if (func == 0 || obj == 0) return 0;` (`as_scriptfunction.cpp:174`).
- Can bind system functions or script class methods.
- Refcounts of both object and method are retained.

---

## 5. Script Classes and Garbage Collection

### 5.1 Script Class Representation
- Represented by `asCObjectType` (`as_objecttype.h`) with `flags |= asOBJ_SCRIPT_OBJECT`.
- Properties stored in `asCObjectProperty` array with `byteOffset`.
- Instances: `asCScriptObject` (`as_scriptobject.h`), with memory allocated via `engine->CallAlloc()`.

### 5.2 Garbage Collector Integration
- In `as_builder.cpp:2540-2570`: Classes containing handles or composite objects have `flags |= asOBJ_GC`.
- Behaviours registered: `asBEHAVE_GETREFCOUNT`, `asBEHAVE_SETGCFLAG`, `asBEHAVE_GETGCFLAG`, `asBEHAVE_ENUMREFS`, `asBEHAVE_RELEASEREFS`.
- `EnumReferences` walks member handles and notifies the GC.

### 5.3 Programmatic vs Text-Generated Class Synthesis
- **Text generation evaluation**:
  Generating declaration text `class $closure_... { ... }` and feeding it through the parser/builder:
  - Pros: Guarantees correct memory offsets, constructor/destructor generation, copy constructor, and automatic GC behaviour setup.
  - Cons: Minor string formatting overhead at script compile time.
  - **Verdict**: Highly recommended over manual manual struct synthesis to prevent memory leaks and serialization discrepancies.

---

## 6. Serialization and Tooling Audit

### 6.1 Bytecode Serialization (`as_restore.cpp`)
- `asCWriter::WriteFuncDefs` (`as_restore.cpp:4067`) / `asCReader::ReadFuncDefs` (`as_restore.cpp:300`):
  Writes and reads all funcdefs in `module->m_funcDefs`.
- Delegate factory call: `as_restore.cpp:1056` handles `DELEGATE_FACTORY` (`$dlgte`) natively without special case storage.
- Script classes: `WriteClassTypes` and `ReadClassTypes` serialize all script classes in `module->m_classTypes`.

### 6.2 Add-ons and Public APIs
- `asIScriptFunction::GetDeclaration()`: Must return `function<Ret(Params)>` for canonical types.
- Debugger and reflection operate transparently on `asIScriptFunction`.

---

## 7. Concrete End-to-End Trace with Real Bytecode

### Test Script
```as
funcdef void CB(int);
void main() {
    CB@ cb = function(int a) {};
}
```

### Trace & Bytecode Listing
1. Parser parses `CB@ cb = function(int a) {};`.
2. Compiler allocates variable `cb` at stack offset 2.
3. Compiler registers lambda function `$void main()$0` with signature `void(int)`.
4. Bytecode generated for `main()`:
   ```text
   0   VarDecl 2
   1   FuncPtr $void main()$0   ; pushes asCScriptFunction*
   3   PSF 2                    ; push pointer to variable cb
   5   STOREOBJ                 ; stores function pointer handle
   6   Pop 1
   7   Ret 0
   ```
5. Bytecode generated for lambda `$void main()$0`:
   ```text
   0   Ret 1                    ; pops parameter 'a', returns void
   ```

---

## 8. Name Comparison Table (Assumed vs Actual)

| Assumed Name (Prompt) | Actual Name in Codebase | File / Location | Status |
|---|---|---|---|
| `FUNCTION_TOKEN` | `FUNCTION_TOKEN = "function"` | `as_tokendef.h:324` | Confirmed |
| `snLambda` | `snFunction` | `as_parser.cpp:1829`, `as_scriptnode.h:51` | Different (uses `snFunction`) |
| `IsLambda` | `asCParser::IsLambda()` | `as_parser.cpp:1760` | Confirmed |
| `ParseLambda` | `asCParser::ParseLambda()` | `as_parser.cpp:1827` | Confirmed |
| `asOBJ_FUNCDEF` | `asOBJ_FUNCDEF` | `as_flags.h` | Confirmed |
| `asCFuncdefType` | `asCFuncdefType` | `as_typeinfo.h:236` | Confirmed |
| `asFUNC_DELEGATE` | `asFUNC_DELEGATE` | `as_scriptfunction.h`, `as_context.cpp:4398` | Confirmed |
| `CreateDelegate` | `asCScriptEngine::CreateDelegate` / `CreateDelegate` | `as_scriptfunction.cpp:172` | Confirmed |
| `DELEGATE_FACTORY` | `DELEGATE_FACTORY = "$dlgte"` | `as_scriptfunction.h:399` | Confirmed |
| `asBC_FuncPtr` | `asBC_FuncPtr` | `as_bytecode.h`, `as_compiler.cpp:7895` | Confirmed |
| `asBC_CallPtr` | `asBC_CallPtr` | `as_context.cpp:4369` | Confirmed |
| `asCC_REF_CONV` | `asCC_REF_CONV` | `as_compiler.h`, `as_compiler.cpp:8246` | Confirmed |
| `TXT_INVALID_EXPRESSION_LAMBDA` | `TXT_INVALID_EXPRESSION_LAMBDA` | `as_texts.h:168` | Confirmed |
| `TXT_CANNOT_RESOLVE_AUTO` | `TXT_CANNOT_RESOLVE_AUTO` | `as_texts.h:165` | Confirmed |

---

## 9. Per-Phase Feasibility Verdict & Top 3 Technical Risks

### Phase 1: Canonical Function Type (`function<R(P...)>@`)
- **Verdict**: **FEASIBLE**. Structural subtyping between funcdefs already exists in `ImplicitConversion`.
- **Top 3 Risks**:
  1. `FindTokenAfterType` not recognizing `function<...>` -> Fixed by updating `CheckTemplateType`.
  2. Nested template closing token `>>` splitting -> Fixed using existing `SetPos(t.pos + 1)` logic.
  3. Serialization mismatch on bytecode reload -> Fixed by storing canonical funcdef in `module->m_funcDefs`.

### Phase 2: Standalone Lambdas & `auto` Deduction
- **Verdict**: **FEASIBLE**. Return type deduction is local to the statement block.
- **Top 3 Risks**:
  1. Ambiguous return types with multiple return statements.
  2. Lambdas without parameter types cannot be deduced with `auto`.
  3. Scope resolution of types used within deduced return expressions.

### Phase 3: Explicit Capture List Syntax & Scope Checking
- **Verdict**: **FEASIBLE**.
- **Top 3 Risks**:
  1. Disambiguating `function[` (lambda) vs array indexing.
  2. Capturing `this` outside of an instance method.
  3. Detecting uncaptured outer locals across nested scopes.

### Phase 4: Stateful Closures (Capture by Value, GC & Serialization)
- **Verdict**: **FEASIBLE**. VM delegates (`asFUNC_DELEGATE`) already handle object binding seamlessly.
- **Top 3 Risks**:
  1. GC cycle leaks if closure context class is not registered with `asOBJ_GC`.
  2. Value copying of complex non-copyable object types.
  3. Serialization of synthesized closure classes on bytecode save/load.

### Phase 5: Hardening & Integration
- **Verdict**: **FEASIBLE**.
- **Top 3 Risks**:
  1. Nested closures capturing variables from multiple outer enclosing frames.
  2. Debugger line stepping inside synthesized closure methods.
  3. Discarding modules with active closure instances.
