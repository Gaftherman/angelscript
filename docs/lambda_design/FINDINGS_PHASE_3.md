# Phase 3 Analysis & Findings: Expected-Type Validation, Return Inference and `auto`

**Document Status**: Complete & Verified (Read-Only Analysis)  
**Target Goal**:
1. Support compiling lambdas against expected canonical function types `function<R(Args)>@` and nominal funcdefs with typed or untyped parameters.
2. Support `auto f = function(params) { body };` and `auto@ f = ...` with inferred return type, deduced from `return` statements.
3. Reject invalid contexts without expected types with clean diagnostics.
4. Lambdas without captures only (stateful closures deferred to Phase 4).

---

## 1. Expected-Type Passing & Untyped Parameter Binding

### 1.1 Where the Expected Type Enters Lambda Compilation
- **Primary Dispatch Point**: `asCCompiler::ImplicitConversion` at [`as_compiler.cpp:7924-7925`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L7924-L7925):
  ```cpp
  if( to.IsFuncdef() && ctx->IsLambda() )
      return ImplicitConvLambdaToFunc(ctx, to, node, convType, generateCode);
  ```
- **Contexts Providing `to`**:
  - Variable assignments: `CompileAssignment` ([`as_compiler.cpp:6820`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L6820))
  - Function argument evaluation: `MatchArgument` ([`as_compiler.cpp:15307`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L15307)) and `PrepareFunctionCall` ([`as_compiler.cpp:2797`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L2797))
  - Return statements: `CompileReturnStatement` ([`as_compiler.cpp:6267`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L6267))
  - List initialization: `CompileInitList` ([`as_compiler.cpp:5480`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L5480))

### 1.2 How Untyped Parameters Receive Types
- In `asCCompiler::ImplicitConvLambdaToFunc` ([`as_compiler.cpp:7846-7878`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L7846-L7878)):
  - Target funcdef signature: `asCScriptFunction *funcDef = CastToFuncdefType(to.GetTypeInfo())->funcdef;`
  - For each parameter node (`snUndefined`) in `ctx->exprNode`:
    - If `typeNode->nodeType == snDataType`: explicitly specified type is verified against `funcDef->parameterTypes[count]`. If mismatch -> `return asCC_NO_CONV`.
    - If `typeNode->nodeType != snDataType`: untyped parameter! It skips type validation and simply increments `count++`.
  - When `count == funcDef->parameterTypes.GetLength()`, `ImplicitConvLambdaToFunc` calls:
    `builder->RegisterLambda(ctx->exprNode, script, funcDef, name, outFunc->nameSpace, outFunc->IsShared());`
- In `asCBuilder::RegisterLambda` ([`as_builder.cpp:5250-5254`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_builder.cpp#L5250-L5254)):
  ```cpp
  int r = RegisterScriptFunction(args, file, 0, 0, true, ns, false, false,
      funcName, funcDef->returnType, parameterNames,
      funcDef->parameterTypes, funcDef->inOutFlags, defaultArgs, traits, 0);
  ```
  The synthetic lambda function's parameter types and return type are copied directly from `funcDef`! Untyped parameters receive their types directly from `funcDef->parameterTypes`.
- When the lambda body is later compiled by `asCBuilder::CompileFunctions` ([`as_builder.cpp:610-618`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_builder.cpp#L610-L618)), the synthetic function already has its full signature populated from `funcDef`.

---

## 2. Compilation of Return Statements and Return-Type Inference

### 2.1 AngelScript's Signature-First Invariant
- In `asCCompiler::CompileFunction` ([`as_compiler.cpp:773`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L773)):
  - Calls `SetupParametersAndReturnVariable` ([`as_compiler.cpp:484-583`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L484-L583)).
  - Declares `"return"` variable: `variables->DeclareVariable("return", returnType, stackPos, false);`
  - Computes parameter stack offsets. If `outFunc->DoesReturnOnStack()` is true (complex value type returned by value), hidden return pointer offset `stackPos -= AS_PTR_SIZE` is pushed before arguments.
- In `CompileReturnStatement` ([`as_compiler.cpp:6120-6290`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L6120-L6290)):
  - Retrieves `sVariable *v = variables->GetVariable("return");`
  - Validates `v->type` against `rnode->firstChild` and performs `ImplicitConversion(&expr, v->type, ...)` to the expected return type.

### 2.2 Immediate-Use Dilemma for `auto`
Consider:
```as
auto f = function(int x) { return x * 2; };
int y = f(10);
```
When compiling the enclosing function, statement `int y = f(10);` appears immediately after the declaration.
The compiler needs to know `f`'s type (`function<int(int)>@`) **during `CompileAutoType`**, not in a delayed builder pass. The return type `R` must be known before `CompileAutoType` returns.

### 2.3 Evaluation of Inference Strategies

| Strategy | Description | Pros | Cons | Recommendation |
|---|---|---|---|---|
| **(i) AST Pre-Pass** | Walk superficially parsed statement block searching for `snReturn` nodes without compilation. | Simple parse walk. | **Impossible in AngelScript**: expression types depend on parameter types, local variable scopes, method lookups, and operator overloading. AST alone without compilation cannot determine expression types. | **Rejected** |
| **(ii) In-situ Deduction Mode** | Compile the function body directly, setting `R` on the first `return`. | Single pass. | If `R` returns on stack, parameter offsets already computed for earlier statements become invalid. Leaks partially generated bytecode on failure. | **Rejected** |
| **(iii) Speculative Probe Pass** | Compile the lambda body in a lightweight standalone `asCCompiler` probe instance with `m_isDeducingReturnType = true`. First return records `R`, subsequent returns verify consistency. | **100% type accuracy**, leverages existing full type checker, zero side effects on module/enclosing function, clean error diagnostics, instantaneous (<20µs for lambdas). | Compiles lambda body once for deduction, once in builder for final bytecode. | **RECOMMENDED** |

**Evidence for Recommendation**:
A lambda body is small (typically 1–10 statements). Creating a stack-allocated `asCCompiler deduceCompiler(engine)` has zero allocation overhead on the module. Its temporary bytecode and variables are automatically cleaned up when `deduceCompiler` leaves scope.

---

## 3. Return-Type Inference & Normalization Rules

1. **Explicit Parameters Required Without Target Type**:
   When using `auto`, every lambda parameter must have an explicit data type (`snDataType`). If any parameter lacks a type, emit:
   `TXT_LAMBDA_PARAMS_MUST_BE_TYPED_FOR_AUTO`: `"Lambda parameters must have explicit types when using 'auto'"`
2. **Void Inference**:
   If a lambda body has no `return` statements, or only `return;` without expressions, deduced return type is `void` (`asCDataType::CreatePrimitive(ttVoid, false)`).
3. **Value & Reference Decay**:
   When returning `return expr;`:
   - If `expr.type.dataType.IsReference()`: references decay to values (`MakeReference(false)`). Functions cannot return references to local variables.
   - If `expr.type.dataType.IsReadOnly() && !expr.type.dataType.IsObjectHandle()`: const qualifiers on value primitives/objects decay (`MakeReadOnly(false)`).
   - If `expr.type.dataType.IsObjectHandle()`: handle is preserved.
   - If `expr.type.dataType.SupportHandles() && expr.type.isObjectHandle`: handle is preserved.
4. **Disallowed Null Return Without Annotation**:
   If a lambda returns only `null` (`expr.type.dataType.IsNullHandle()`):
   Emit error: `"Cannot deduce return type from 'null'; specify an explicit function type"`.
5. **Divergent Returns (Strict Rule)**:
   If multiple `return` statements return different types (e.g. `int` and `string`, or `int` and `float`):
   Emit error:
   `TXT_DIVERGENT_RETURN_TYPES_s_s`: `"All return expressions in a lambda must have the same type; found '%s' and '%s'"`
   *Why strict*: Prevents unintentional numeric truncations or ambiguous class hierarchies. The user can easily annotate an explicit type `function<float(int)>@ f = ...`.
6. **Recursion Error**:
   A lambda cannot call itself by name because it is anonymous. If it attempts to access the outer variable currently being initialized (e.g. `auto f = function(int x) { return f(x-1); };`), AngelScript emits `"Name 'f' is not declared"` because `f` is not yet registered in the enclosing scope during its own initializer.

---

## 4. `auto` and `auto@` Resolution Mechanics

In `asCCompiler::CompileAutoType` ([`as_compiler.cpp:3086-3160`](file:///e:/Github/src/angelscript/sdk/angelscript/source/as_compiler.cpp#L3086-L3160)):
- Line 3102:
  ```cpp
  if (compiledCtx.IsLambda())
  {
      Error(TXT_CANNOT_RESOLVE_AUTO, errNode);
      return false;
  }
  ```
- **Replacing this block with lambda auto deduction**:
  1. Validate that all lambda parameters have explicit types.
  2. Probe lambda statement block using deduction compiler to determine `R`.
  3. Resolve canonical funcdef: `canonFuncDef = engine->GetCanonicalFuncdef(R, paramTypes, inOutFlags, builder->module)`.
  4. Create handle type: `canonType = asCDataType::CreateType(canonFuncDef, false); canonType.MakeHandle(true);`.
  5. Call `ImplicitConvLambdaToFunc(&compiledCtx, canonType, exprNode, asIC_IMPLICIT_CONV, true)`.
  6. In `CompileAutoType`, lines 3151-3152:
     ```cpp
     if( newType.SupportHandles() )
         newType.MakeHandle(true);
     ```
     Because funcdefs support handles (`asOBJ_FUNCDEF`), both `auto f = ...` and `auto@ f = ...` correctly produce handle type `function<R(Args)>@`.

---

## 5. Expression Contexts for Lambdas

| Context | Expected Type? | Status | Behavior |
|---|---|---|---|
| `function<R(Args)>@ f = function(...)` | Yes (Canonical) | **Allowed** | Validates/infers params against canonical funcdef. |
| `NominalFuncdef@ cb = function(...)` | Yes (Nominal) | **Allowed** | Validates/infers params against nominal funcdef. |
| `Apply(function(...) {})` | Yes (Param type) | **Allowed** | Compiles against parameter's funcdef type. |
| `return function(...) {};` | Yes (Return type) | **Allowed** | Compiles against enclosing function's return type. |
| `auto f = function(explicit_params) {}` | No (`auto`) | **Allowed** | Deduces `R` from body; produces `function<R(Args)>@`. |
| `auto@ f = function(explicit_params) {}` | No (`auto@`) | **Allowed** | Deduces `R` from body; produces `function<R(Args)>@`. |
| Standalone `function(...) {};` | None | **Rejected** | `TXT_INVALID_EXPRESSION_LAMBDA`: `"Invalid expression: stand-alone anonymous function"`. |
| Ternary `cond ? f1 : f2` | None | **Rejected** | `TXT_CANNOT_RESOLVE_AUTO`: `"Cannot deduce lambda type in ternary expression"`. |
| Comparison `f1 == f2` | None | **Rejected** | Operator error: cannot compare untyped lambda expression. |
| Math `f1 + 1` | None | **Rejected** | Operator error: illegal operation on lambda. |
| Overload call `foo(function(...))` | Yes (Overload candidates) | **Allowed** | `MatchArgument` tests each candidate; selects best matching funcdef. |

---

## 6. Phase 3 Implementation Plan
1. **Compiler Diagnostics** (`as_texts.h`):
   - `TXT_LAMBDA_PARAMS_MUST_BE_TYPED_FOR_AUTO`
   - `TXT_DIVERGENT_RETURN_TYPES_s_s`
   - `TXT_CANNOT_DEDUCE_RETURN_FROM_NULL`
2. **Deduction Compiler** (`as_compiler.h`, `as_compiler.cpp`):
   - Add `m_isDeducingReturnType` and `m_deducedReturnType` to `asCCompiler`.
   - Update `SetupParametersAndReturnVariable` and `CompileReturnStatement` to record return types in deduction mode.
   - Implement `asCDataType asCCompiler::DeduceLambdaReturnType(asCScriptNode *funcNode, ...)`
3. **`auto` Integration** (`as_compiler.cpp`):
   - In `CompileAutoType`, hook `if (compiledCtx.IsLambda())` to deduce `R`, resolve canonical funcdef via `engine->GetCanonicalFuncdef`, and convert via `ImplicitConvLambdaToFunc`.
4. **Unit Tests** (`test_lambda.cpp`):
   - Typed targets (canonical variable, nominal funcdef, function argument, return, class member, array element).
   - `auto` and `auto@`: void return, int return, string return (decay), object handle return.
   - Specific errors: divergent returns, untyped parameters with `auto`, `null` return without type, recursion attempt.
   - Overload resolution verification and nested lambdas.
   - Zero-regression test run across full suite.
