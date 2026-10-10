#include "utils.h"
#include "../../../add_on/scriptarray/scriptarray.h"
#include "../../../add_on/scriptstdstring/scriptstdstring.h"

namespace TestLambda
{

bool Test()
{
	bool fail = false;
	int r;
	CBufferedOutStream bout;
	COutStream out;
	asIScriptEngine *engine = 0;
	asIScriptModule *mod = 0;

	// 1. Basic canonical function declarations, global function assignment, calling, comparing, null check
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);
		RegisterScriptArray(engine, false);

		mod = engine->GetModule("test", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"int Add(int a, int b) { return a + b; }\n"
			"int Mul(int a, int b) { return a * b; }\n"
			"void Noop() {}\n"
			"\n"
			"// Global variable\n"
			"function<int(int, int)>@ g_fn = Add;\n"
			"\n"
			"// Parameter and return type\n"
			"int Apply(function<int(int, int)>@ fn, int a, int b) {\n"
			"  return fn(a, b);\n"
			"}\n"
			"function<int(int, int)>@ GetOp(bool pickAdd) {\n"
			"  if( pickAdd ) return Add;\n"
			"  return Mul;\n"
			"}\n"
			"\n"
			"// Class member\n"
			"class OpHolder {\n"
			"  function<int(int, int)>@ op;\n"
			"  int Execute(int x, int y) { return op(x, y); }\n"
			"}\n"
			"\n"
			"// Inside namespace\n"
			"namespace MyNS {\n"
			"  int Sub(int a, int b) { return a - b; }\n"
			"  function<int(int, int)>@ ns_fn = Sub;\n"
			"}\n"
			"\n"
			"// Modifiers: in, out\n"
			"void Modify(int &in a, int &out b) {\n"
			"  b = a * 10;\n"
			"}\n"
			"\n"
			"funcdef int BinaryOp(int, int);\n"
			"\n"
			"void main() {\n"
			"  // 1. Calling global handle\n"
			"  assert( g_fn(3, 4) == 7 );\n"
			"  @g_fn = Mul;\n"
			"  assert( g_fn(3, 4) == 12 );\n"
			"\n"
			"  // 2. Local variable\n"
			"  function<int(int, int)>@ localFn = Add;\n"
			"  assert( localFn(10, 20) == 30 );\n"
			"\n"
			"  // 3. Compare handles & null-check\n"
			"  function<int(int, int)>@ f1 = Add;\n"
			"  function<int(int, int)>@ f2 = Add;\n"
			"  assert( f1 is f2 );\n"
			"  assert( f1 is Add );\n"
			"  @f2 = Mul;\n"
			"  assert( f1 !is f2 );\n"
			"  function<int(int, int)>@ fNull;\n"
			"  assert( fNull is null );\n"
			"  assert( f1 !is null );\n"
			"\n"
			"  // 4. Pass as argument & return value\n"
			"  assert( Apply(Add, 5, 6) == 11 );\n"
			"  assert( Apply(Mul, 5, 6) == 30 );\n"
			"  assert( Apply(GetOp(true), 5, 6) == 11 );\n"
			"  assert( Apply(GetOp(false), 5, 6) == 30 );\n"
			"\n"
			"  // 5. Class member\n"
			"  OpHolder h;\n"
			"  @h.op = Mul;\n"
			"  assert( h.Execute(6, 7) == 42 );\n"
			"\n"
			"  // 6. Inside namespace\n"
			"  assert( MyNS::ns_fn(10, 4) == 6 );\n"
			"\n"
			"  // 7. Modifiers (&in, &out)\n"
			"  function<void(int &in, int &out)>@ modFn = Modify;\n"
			"  int res = 0;\n"
			"  modFn(5, res);\n"
			"  assert( res == 50 );\n"
			"\n"
			"  // 8. Void return with empty parameter list\n"
			"  function<void()>@ noopFn = Noop;\n"
			"  noopFn();\n"
			"\n"
			"  // 9. Array of canonical function handles\n"
			"  array<function<int(int, int)>@> ops;\n"
			"  ops.insertLast(Add);\n"
			"  ops.insertLast(Mul);\n"
			"  assert( ops[0](2, 3) == 5 );\n"
			"  assert( ops[1](2, 3) == 6 );\n"
			"\n"
			"  // 10. Bidirectional conversion with nominal funcdef\n"
			"  BinaryOp@ nomOp = Add;\n"
			"  function<int(int, int)>@ canOp = nomOp;\n"
			"  assert( canOp(4, 5) == 9 );\n"
			"  assert( canOp is nomOp );\n"
			"  BinaryOp@ nomOp2 = canOp;\n"
			"  assert( nomOp2(4, 5) == 9 );\n"
			"  assert( nomOp2 is Add );\n"
			"\n"
			"  // 11. Backward compatibility: variable named 'function'\n"
			"  int function = 42;\n"
			"  function += 8;\n"
			"  assert( function == 50 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "main()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		// 12. Introspection and GetDeclaration
		int typeId = mod->GetTypeIdByDecl("function<int(int, int)>@");
		if( typeId < 0 )
			TEST_FAILED;
		asITypeInfo *ti = engine->GetTypeInfoById(typeId);
		if( ti == 0 || strcmp(ti->GetName(), "function<int(int, int)>") != 0 )
			TEST_FAILED;
		asIScriptFunction *sig = ti->GetFuncdefSignature();
		if( sig == 0 || strcmp(sig->GetDeclaration(), "function<int(int, int)>") != 0 )
			TEST_FAILED;

		// 13. Bytecode Save & Load
		CBytecodeStream stream(__FILE__ "lambda_canon");
		r = mod->SaveByteCode(&stream);
		if( r < 0 )
			TEST_FAILED;

		asIScriptModule *mod2 = engine->GetModule("test2", asGM_ALWAYS_CREATE);
		r = mod2->LoadByteCode(&stream);
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "main()", mod2);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// 2. Cross-module type id sharing for engine-scoped canonical types
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		asIScriptModule *modA = engine->GetModule("modA", asGM_ALWAYS_CREATE);
		modA->AddScriptSection("a",
			"int FuncA(int x) { return x + 1; }\n"
			"function<int(int)>@ fnA = FuncA;\n");
		r = modA->Build();
		if( r < 0 )
			TEST_FAILED;

		asIScriptModule *modB = engine->GetModule("modB", asGM_ALWAYS_CREATE);
		modB->AddScriptSection("b",
			"int FuncB(int x) { return x + 2; }\n"
			"function<int(int)>@ fnB = FuncB;\n");
		r = modB->Build();
		if( r < 0 )
			TEST_FAILED;

		int idA = modA->GetTypeIdByDecl("function<int(int)>@");
		int idB = modB->GetTypeIdByDecl("function<int(int)>@");
		if( idA != idB )
			TEST_FAILED;

		// Discard modA; modB must continue functioning without issue
		engine->DiscardModule("modA");
		r = ExecuteString(engine, "assert( fnB(10) == 12 );", modB);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// 3. Module discard and rebuild: GC & memory leak verification
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		engine->GarbageCollect();
		asUINT gcSizeBefore, gcDestroyedBefore, gcDetectedBefore;
		engine->GetGCStatistics(&gcSizeBefore, &gcDestroyedBefore, &gcDetectedBefore);

		for( int iter = 0; iter < 3; iter++ )
		{
			asIScriptModule *iterMod = engine->GetModule("iterMod", asGM_ALWAYS_CREATE);
			iterMod->AddScriptSection("iter",
				"int DoubleVal(int x) { return x * 2; }\n"
				"void Run() {\n"
				"  function<int(int)>@ f = DoubleVal;\n"
				"  assert( f(21) == 42 );\n"
				"}\n");
			r = iterMod->Build();
			if( r < 0 )
				TEST_FAILED;

			r = ExecuteString(engine, "Run()", iterMod);
			if( r != asEXECUTION_FINISHED )
				TEST_FAILED;

			engine->DiscardModule("iterMod");
		}

		engine->GarbageCollect();
		asUINT gcSizeAfter, gcDestroyedAfter, gcDetectedAfter;
		engine->GetGCStatistics(&gcSizeAfter, &gcDestroyedAfter, &gcDetectedAfter);

		if( gcSizeAfter != gcSizeBefore )
			TEST_FAILED;

		engine->Release();
	}

	// 4. Module-scoped canonical types referencing non-shared script classes
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		asIScriptModule *mod1 = engine->GetModule("mod1", asGM_ALWAYS_CREATE);
		mod1->AddScriptSection("m1",
			"class LocalItem {\n"
			"  int val = 99;\n"
			"}\n"
			"void TakeItem(LocalItem@ item) {\n"
			"  assert( item.val == 99 );\n"
			"}\n"
			"void Run() {\n"
			"  function<void(LocalItem@)>@ fn = TakeItem;\n"
			"  LocalItem item;\n"
			"  fn(item);\n"
			"}\n");
		r = mod1->Build();
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "Run()", mod1);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		// Discarding module with local class-dependent canonical type must clean up cleanly
		engine->DiscardModule("mod1");
		engine->GarbageCollect();

		engine->Release();
	}

	// 5. Negative compile tests for malformed syntax
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);

		// Missing parentheses: function<int>@
		bout.buffer = "";
		mod = engine->GetModule("err1", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("err", "function<int>@ f;");
		r = mod->Build();
		if( r >= 0 )
			TEST_FAILED;

		// Missing return type: function<(int)>@
		bout.buffer = "";
		mod = engine->GetModule("err2", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("err", "function<(int)>@ f;");
		r = mod->Build();
		if( r >= 0 )
			TEST_FAILED;

		// Missing close parenthesis: function<void(int>@
		bout.buffer = "";
		mod = engine->GetModule("err3", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("err", "function<void(int>@ f;");
		r = mod->Build();
		if( r >= 0 )
			TEST_FAILED;

		// Void as parameter: function<void(void, int)>@
		bout.buffer = "";
		mod = engine->GetModule("err4", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("err", "function<void(void, int)>@ f;");
		r = mod->Build();
		if( r >= 0 )
			TEST_FAILED;

		// Instantiation without handle: function<int(int)> f;
		bout.buffer = "";
		mod = engine->GetModule("err5", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("err", "function<int(int)> f;");
		r = mod->Build();
		if( r >= 0 )
			TEST_FAILED;
		if( bout.buffer.find("Data type can't be 'function<int(int)>'") == std::string::npos )
			TEST_FAILED;

		engine->Release();
	}

	// =========================================================================
	// Phase 2: Lambda capture list parsing, AST, validation, and diagnostics
	// =========================================================================

	// Phase 2 Test 1: Empty capture list execution & backward compatibility
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p2_empty", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef int IntOp(int);\n"
			"function<int(int)>@ f1 = function[](int x) { return x * 2; };\n"
			"function<int(int)>@ f2 = function /* comment */ [ /* empty */ ] ( int y ) { return y + 10; };\n"
			"IntOp@ f3 = function[](int z) { return z * z; };\n"
			"void main() {\n"
			"  assert( f1(21) == 42 );\n"
			"  assert( f2(5) == 15 );\n"
			"  assert( f3(4) == 16 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "main()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// Phase 2 / Phase 4: Valid capture syntax builds and executes correctly
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		// Non-empty capture: function[a](int x)
		bout.buffer = "";
		mod = engine->GetModule("p2_cap1", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"int g_res = 0;\n"
			"void test() {\n"
			"  int a = 10;\n"
			"  function<int(int)>@ f = function[a](int x) { return x + a; };\n"
			"  g_res = f(5);\n"
			"}\n");
		r = mod->Build();
		if( r < 0 )
		{
			PRINTF("p2_cap1 Build error:\n%s\n", bout.buffer.c_str());
			TEST_FAILED;
		}
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;
		int g_res = *(int*)mod->GetAddressOfGlobalVar(0);
		if( g_res != 15 )
			TEST_FAILED;

		// Non-empty capture with 'this': function[this]()
		bout.buffer = "";
		mod = engine->GetModule("p2_cap2", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class Foo {\n"
			"  bool called = false;\n"
			"  void Run() {\n"
			"    function<void()>@ f = function[this]() { this.called = true; };\n"
			"    f();\n"
			"  }\n"
			"}\n"
			"void test() { Foo foo; foo.Run(); assert( foo.called ); }\n");
		r = mod->Build();
		if( r < 0 )
		{
			PRINTF("p2_cap2 Build error:\n%s\n", bout.buffer.c_str());
			TEST_FAILED;
		}
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		// Non-empty multi-variable capture with varied whitespace & comments: function [ a , b , this ] (int x)
		bout.buffer = "";
		mod = engine->GetModule("p2_cap3", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class Bar {\n"
			"  int factor = 10;\n"
			"  int Run() {\n"
			"    int a = 1, b = 2;\n"
			"    function<int(int)>@ f = function \n"
			"      [ /* a */ a , \n"
			"        /* b */ b , \n"
			"        this ] \n"
			"      ( int x ) { return x + a + b + this.factor; };\n"
			"    return f(5);\n"
			"  }\n"
			"}\n"
			"void test() { Bar bar; assert( bar.Run() == 18 ); }\n");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// Phase 2 Test 3: Specific syntax error diagnostics
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);

		// Capture by reference: function[&x]()
		bout.buffer = "";
		mod = engine->GetModule("err_ref", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { int x = 0; function<void()>@ f = function[&x]() {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Capture by reference is not supported yet") == std::string::npos ) TEST_FAILED;

		// Default capture by reference: function[&]()
		bout.buffer = "";
		mod = engine->GetModule("err_def_ref", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { function<void()>@ f = function[&]() {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Default capture is not supported yet") == std::string::npos ) TEST_FAILED;

		// Default capture by value: function[=]()
		bout.buffer = "";
		mod = engine->GetModule("err_def_val", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { function<void()>@ f = function[=]() {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Default capture is not supported yet") == std::string::npos ) TEST_FAILED;

		// Duplicate variable in capture list: function[a, a]()
		bout.buffer = "";
		mod = engine->GetModule("err_dup_var", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { int a = 0; function<void()>@ f = function[a, a]() {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Duplicate capture variable 'a'") == std::string::npos ) TEST_FAILED;

		// Duplicate 'this' in capture list: function[this, this]()
		bout.buffer = "";
		mod = engine->GetModule("err_dup_this", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "class Foo { void m() { function<void()>@ f = function[this, this]() {}; } }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Duplicate capture 'this'") == std::string::npos ) TEST_FAILED;

		// Capture variable conflicts with parameter: function[x](int x)
		bout.buffer = "";
		mod = engine->GetModule("err_conflict", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { int x = 0; function<void(int)>@ f = function[x](int x) {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Capture variable 'x' conflicts with lambda parameter") == std::string::npos ) TEST_FAILED;

		// Trailing comma in capture list: function[a, ]()
		bout.buffer = "";
		mod = engine->GetModule("err_trail_comma", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { int a = 0; function<void()>@ f = function[a, ]() {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Expected capture identifier or 'this'") == std::string::npos ) TEST_FAILED;

		// Empty capture item with comma: function[,]
		bout.buffer = "";
		mod = engine->GetModule("err_comma_only", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { function<void()>@ f = function[,](){}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Expected capture identifier or 'this'") == std::string::npos ) TEST_FAILED;

		// Empty lambda parameter: function(,)
		bout.buffer = "";
		mod = engine->GetModule("err_empty_param", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { function<void()>@ f = function(,) {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Empty lambda parameter is not allowed") == std::string::npos ) TEST_FAILED;

		// Trailing comma in parameter list: function(int a, )
		bout.buffer = "";
		mod = engine->GetModule("err_param_trail", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { function<void(int)>@ f = function(int a, ) {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Empty lambda parameter is not allowed") == std::string::npos ) TEST_FAILED;

		engine->Release();
	}

	// Phase 2 Test 4: Parser fuzzing / malformed strings resilience (no hangs or crashes)
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);

		const char *malformedCases[] = {
			"void test() { function[",
			"void test() { function[a",
			"void test() { function[a]",
			"void test() { function[a](",
			"void test() { function[a]();",
			"void test() { function[[a]]() {}",
			"void test() { function[123]() {}",
			"void test() { function[a b]() {}",
			"void test() { function[a, , b]() {}",
			"void test() { function[a](int) {}",
			"void test() { function[](int",
			"void test() { function[]()",
			"void test() { function[](){",
		};

		for( size_t i = 0; i < sizeof(malformedCases)/sizeof(malformedCases[0]); i++ )
		{
			bout.buffer = "";
			mod = engine->GetModule("fuzz", asGM_ALWAYS_CREATE);
			mod->AddScriptSection("fuzz", malformedCases[i]);
			r = mod->Build();
			if( r >= 0 )
				TEST_FAILED;
		}

		engine->Release();
	}

	// Phase 2 Test 5: Variable named 'function' as array and indexing disambiguation
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);
		RegisterScriptArray(engine, false);

		mod = engine->GetModule("p2_var", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"void main() {\n"
			"  array<int> function = { 10, 20, 30 };\n"
			"  assert( function[0] == 10 );\n"
			"  assert( function[1] == 20 );\n"
			"  function[2] = 99;\n"
			"  assert( function[2] == 99 );\n"
			"  int val = function[1] + function[0];\n"
			"  assert( val == 30 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "main()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// Phase 3 Test 1: Lambdas assigned to expected canonical function types and nominal funcdefs
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);
		RegisterScriptArray(engine, false);

		mod = engine->GetModule("p3_expected", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef int BinaryOp(int, int);\n"
			"int Apply(function<int(int)>@ fn, int val) {\n"
			"  return fn(val);\n"
			"}\n"
			"function<int(int)>@ GetTriple() {\n"
			"  return function(int x) { return x * 3; };\n"
			"}\n"
			"class Evaluator {\n"
			"  function<int(int)>@ op;\n"
			"  int Run(int v) { return op(v); }\n"
			"}\n"
			"void main() {\n"
			"  // 1. Canonical variable with explicit param types\n"
			"  function<int(int, int)>@ add = function(int a, int b) { return a + b; };\n"
			"  assert( add(10, 20) == 30 );\n"
			"\n"
			"  // 2. Canonical variable with untyped params inferred from target\n"
			"  function<int(int, int)>@ sub = function(a, b) { return a - b; };\n"
			"  assert( sub(50, 15) == 35 );\n"
			"\n"
			"  // 3. Nominal funcdef with untyped params\n"
			"  BinaryOp@ mul = function(a, b) { return a * b; };\n"
			"  assert( mul(6, 7) == 42 );\n"
			"\n"
			"  // 4. Pass lambda directly as function argument\n"
			"  assert( Apply(function(int x) { return x * 2; }, 21) == 42 );\n"
			"  assert( Apply(function(x) { return x + 10; }, 5) == 15 );\n"
			"\n"
			"  // 5. Returned from named function\n"
			"  function<int(int)>@ triple = GetTriple();\n"
			"  assert( triple(7) == 21 );\n"
			"\n"
			"  // 6. Class member\n"
			"  Evaluator ev;\n"
			"  @ev.op = function(x) { return x * x; };\n"
			"  assert( ev.Run(8) == 64 );\n"
			"\n"
			"  // 7. Array element\n"
			"  array<function<int(int)>@> funcs = {\n"
			"    function(x) { return x + 1; },\n"
			"    function(x) { return x * 10; }\n"
			"  };\n"
			"  assert( funcs[0](5) == 6 );\n"
			"  assert( funcs[1](5) == 50 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "main()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// Phase 3 Test 2: auto and auto@ declarations with inferred return types
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);
		RegisterStdString(engine);

		mod = engine->GetModule("p3_auto", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"int g_sideEffect = 0;\n"
			"class Point { int x; int y; Point() {} Point(int _x, int _y) { x = _x; y = _y; } }\n"
			"void main() {\n"
			"  // 1. auto primitive return deduction\n"
			"  auto fDouble = function(int x) { return x * 2; };\n"
			"  assert( fDouble(21) == 42 );\n"
			"\n"
			"  // 2. auto@ explicit handle\n"
			"  auto@ fAdd = function(int a, int b) { return a + b; };\n"
			"  assert( fAdd(100, 25) == 125 );\n"
			"\n"
			"  // 3. Void lambda deduction without return expression\n"
			"  auto fVoid = function() { g_sideEffect = 77; };\n"
			"  fVoid();\n"
			"  assert( g_sideEffect == 77 );\n"
			"\n"
			"  // 4. Void lambda deduction with return; statement\n"
			"  auto fVoidRet = function(bool early) { if (early) return; g_sideEffect = 88; };\n"
			"  fVoidRet(true);\n"
			"  assert( g_sideEffect == 77 );\n"
			"  fVoidRet(false);\n"
			"  assert( g_sideEffect == 88 );\n"
			"\n"
			"  // 5. String return with reference/const decay\n"
			"  auto fGreet = function(string name) { return \"Hello, \" + name; };\n"
			"  assert( fGreet(\"AngelScript\") == \"Hello, AngelScript\" );\n"
			"\n"
			"  // 6. Multiple branches returning same type\n"
			"  auto fSign = function(int v) {\n"
			"    if (v > 0) return 1;\n"
			"    else if (v < 0) return -1;\n"
			"    return 0;\n"
			"  };\n"
			"  assert( fSign(50) == 1 );\n"
			"  assert( fSign(-5) == -1 );\n"
			"  assert( fSign(0) == 0 );\n"
			"\n"
			"  // 7. Value object return\n"
			"  auto fMakePoint = function(int x, int y) { return Point(x, y); };\n"
			"  Point pt = fMakePoint(10, 20);\n"
			"  assert( pt.x == 10 && pt.y == 20 );\n"
			"\n"
			"  // 8. Object handle return\n"
			"  auto fPassHandle = function(Point@ p) { return p; };\n"
			"  Point@ ptRef = fPassHandle(pt);\n"
			"  assert( ptRef is pt );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "main()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// Phase 3 Test 3: Global auto lambdas
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p3_global_auto", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"auto g_add = function(int a, int b) { return a + b; };\n"
			"auto@ g_mul = function(int a, int b) { return a * b; };\n"
			"void main() {\n"
			"  assert( g_add(3, 7) == 10 );\n"
			"  assert( g_mul(4, 5) == 20 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "main()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// Phase 3 Test 4: Overload resolution with lambdas
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);
		RegisterStdString(engine);

		mod = engine->GetModule("p3_overload", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"int Process(function<int(int)>@ f, int arg) { return f(arg); }\n"
			"string Process(function<string(string)>@ f, string arg) { return f(arg); }\n"
			"void main() {\n"
			"  int r1 = Process(function(int x) { return x * 10; }, 5);\n"
			"  assert( r1 == 50 );\n"
			"  string r2 = Process(function(string s) { return s + \"!\"; }, \"hi\");\n"
			"  assert( r2 == \"hi!\" );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 )
			TEST_FAILED;

		r = ExecuteString(engine, "main()", mod);
		if( r != asEXECUTION_FINISHED )
			TEST_FAILED;

		engine->Release();
	}

	// Phase 3 Test 5: Error diagnostics and strict rejections
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);
		RegisterStdString(engine);

		// 1. Untyped parameter when using auto
		bout.buffer = "";
		mod = engine->GetModule("p3_err1", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = function(x) { return x; }; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Lambda parameters must have explicit types when using 'auto'") == std::string::npos ) TEST_FAILED;

		// 2. Divergent return types (int vs string)
		bout.buffer = "";
		mod = engine->GetModule("p3_err2", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = function(int x) { if (x > 0) return 1; return \"error\"; }; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("All return expressions in a lambda must have the same type; found 'int' and '") == std::string::npos ) TEST_FAILED;

		// 3. Divergent return types (int vs float)
		bout.buffer = "";
		mod = engine->GetModule("p3_err3", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = function(int x) { if (x > 0) return 1; return 2.5f; }; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("All return expressions in a lambda must have the same type; found 'int' and 'float'") == std::string::npos ) TEST_FAILED;

		// 4. Divergent return types (int vs void)
		bout.buffer = "";
		mod = engine->GetModule("p3_err4", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = function(int x) { if (x > 0) return 1; return; }; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("All return expressions in a lambda must have the same type; found 'int' and 'void'") == std::string::npos ) TEST_FAILED;

		// 5. Cannot deduce from null
		bout.buffer = "";
		mod = engine->GetModule("p3_err5", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = function() { return null; }; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Cannot deduce return type from 'null'; specify an explicit function type") == std::string::npos ) TEST_FAILED;

		// 6. Stand-alone lambda expression rejected
		bout.buffer = "";
		mod = engine->GetModule("p3_err6", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { function(int x) { return x; }; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Invalid expression: stand-alone anonymous function") == std::string::npos ) TEST_FAILED;

		// 7. Unconverted lambdas in ternary expression rejected
		bout.buffer = "";
		mod = engine->GetModule("p3_err7", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = true ? function(int a) { return a; } : function(int b) { return b * 2; }; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Unable to resolve auto type") == std::string::npos ) TEST_FAILED;

		// 8. Self-recursion attempt rejected (f is not in scope during its initializer)
		bout.buffer = "";
		mod = engine->GetModule("p3_err8", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = function(int x) { return f(x - 1); }; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("f") == std::string::npos ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 1: Stateful mutation, escaping closure, and independent instances
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p4_counter", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"function<int()>@ MakeCounter(int start) {\n"
			"  int c = start;\n"
			"  return function[c]() {\n"
			"    c += 1;\n"
			"    return c;\n"
			"  };\n"
			"}\n"
			"void test() {\n"
			"  auto c1 = MakeCounter(0);\n"
			"  auto c2 = MakeCounter(100);\n"
			"  assert( c1() == 1 );\n"
			"  assert( c1() == 2 );\n"
			"  assert( c1() == 3 );\n"
			"  assert( c2() == 101 );\n"
			"  assert( c2() == 102 );\n"
			"  assert( c1() == 4 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 2: Value capture isolation (outer modification does not affect closure and vice-versa)
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p4_iso", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"void test() {\n"
			"  int a = 10;\n"
			"  function<int()>@ f = function[a]() {\n"
			"    int ret = a;\n"
			"    a = 99;\n"
			"    return ret;\n"
			"  };\n"
			"  a = 20;\n"
			"  assert( f() == 10 );\n"
			"  assert( a == 20 );\n"
			"  assert( f() == 99 );\n"
			"  assert( a == 20 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 3: Object handle capture keeps referenced object alive
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		bout.buffer = "";
		mod = engine->GetModule("p4_handle", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class Item {\n"
			"  int val;\n"
			"  Item(int v) { val = v; }\n"
			"}\n"
			"function<int()>@ MakeItemClosure() {\n"
			"  Item@ it = Item(42);\n"
			"  return function[it]() { return it.val; };\n"
			"}\n"
			"void test() {\n"
			"  auto f = MakeItemClosure();\n"
			"  assert( f() == 42 );\n"
			"  Item@ obj = Item(99);\n"
			"  auto f2 = function[obj]() { return obj.val; };\n"
			"  @obj = null;\n"
			"  assert( f2() == 99 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 4: Capturing 'this' in a const method makes $this a const handle
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		// Valid: read const member through captured this
		bout.buffer = "";
		mod = engine->GetModule("p4_const_ok", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class Foo {\n"
			"  int x = 42;\n"
			"  int Read() const {\n"
			"    function<int()>@ f = function[this]() { return this.x; };\n"
			"    return f();\n"
			"  }\n"
			"}\n"
			"void test() { Foo foo; assert( foo.Read() == 42 ); }\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		// Error: mutating through const this in closure
		bout.buffer = "";
		mod = engine->GetModule("p4_const_err", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class Bar {\n"
			"  int x = 42;\n"
			"  void Mutate() const {\n"
			"    function<void()>@ f = function[this]() { this.x = 99; };\n"
			"  }\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		// AngelScript error for writing to const property/reference
		if( bout.buffer.find("read-only") == std::string::npos && bout.buffer.find("const") == std::string::npos ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 5: Diagnostics for uncaptured outer variables and 'this'
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);

		// Uncaptured outer local variable
		bout.buffer = "";
		mod = engine->GetModule("p4_diag_uncap_var", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"void test() {\n"
			"  int x = 10;\n"
			"  function<int()>@ f = function() { return x; };\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("variable 'x' is not captured; use function[x](...)") == std::string::npos ) TEST_FAILED;

		// Uncaptured outer 'this'
		bout.buffer = "";
		mod = engine->GetModule("p4_diag_uncap_this", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class Baz {\n"
			"  int val = 1;\n"
			"  void Run() {\n"
			"    function<int()>@ f = function() { return this.val; };\n"
			"  }\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("variable 'this' is not captured; use function[this](...)") == std::string::npos ) TEST_FAILED;

		// Non-existent variable in capture list
		bout.buffer = "";
		mod = engine->GetModule("p4_diag_nonexist", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"void test() {\n"
			"  function<void()>@ f = function[does_not_exist]() {};\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Capture variable 'does_not_exist' is not found in enclosing scope") == std::string::npos ) TEST_FAILED;

		// Non-copyable type capture
		bout.buffer = "";
		mod = engine->GetModule("p4_diag_noncopy", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class NonCopyable {\n"
			"  NonCopyable() {}\n"
			"}\n"
			"void test() {\n"
			"  NonCopyable nc;\n"
			"  function<void()>@ f = function[nc]() {};\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Cannot capture object of non-copyable type") == std::string::npos ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 6: Cyclic reference collection between script object and closure delegate
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);

		mod = engine->GetModule("p4_gc", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class Node {\n"
			"  function<void()>@ cb;\n"
			"  void Setup() {\n"
			"    @cb = function[this]() { this.Ping(); };\n"
			"  }\n"
			"  void Ping() {}\n"
			"}\n"
			"void test() {\n"
			"  Node@ n = Node();\n"
			"  n.Setup();\n"
			"  @n = null;\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		asUINT gcCurrentSize = 0;
		engine->GetGCStatistics(&gcCurrentSize);
		// Run full GC cycle to collect cycle Node <-> closure
		engine->GarbageCollect(asGC_FULL_CYCLE);
		engine->GetGCStatistics(&gcCurrentSize);
		if( gcCurrentSize != 0 ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 7: Auto return inference with captures
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p4_auto", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"void test() {\n"
			"  int a = 10;\n"
			"  auto f = function[a](int x) { return x + a; };\n"
			"  assert( f(20) == 30 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 8: Prompt Test 1 & 2 & 3: Local capture, mutation, multiple closures from same scope
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p4_mutation", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"void test() {\n"
			"  int base = 100;\n"
			"  auto c = function[base](int x) { return base + x; };\n"
			"  assert( c(5) == 105 );\n"
			"\n"
			"  int count = 0;\n"
			"  auto inc = function[count]() { return ++count; };\n"
			"  assert( inc() == 1 );\n"
			"  assert( inc() == 2 );\n"
			"  assert( count == 0 );\n"
			"\n"
			"  int val = 10;\n"
			"  auto f1 = function[val]() { return ++val; };\n"
			"  auto f2 = function[val]() { return ++val; };\n"
			"  assert( f1() == 11 );\n"
			"  assert( f1() == 12 );\n"
			"  assert( f2() == 11 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 9: Mixed capture [this, x] with implicit member access
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p4_mixed", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class Accumulator {\n"
			"  int total;\n"
			"  Accumulator(int t) { total = t; }\n"
			"  function<void()>@ MakeAdder(int x) {\n"
			"    return function[this, x]() { total += x; };\n"
			"  }\n"
			"}\n"
			"void test() {\n"
			"  Accumulator acc(100);\n"
			"  auto adder = acc.MakeAdder(25);\n"
			"  adder();\n"
			"  assert( acc.total == 125 );\n"
			"  adder();\n"
			"  assert( acc.total == 150 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 10: Nested closures
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p4_nested", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"int g_res = 0;\n"
			"void test() {\n"
			"  int x = 100;\n"
			"  function<function<int(int)>@(int)>@ makeInner = function[x](int y) {\n"
			"    return function[x, y](int z) { return x + y + z; };\n"
			"  };\n"
			"  auto inner = makeInner(20);\n"
			"  g_res = inner(3);\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		int g_res = *(int*)mod->GetAddressOfGlobalVar(mod->GetGlobalVarIndexByName("g_res"));
		if( g_res != 123 ) TEST_FAILED;

		engine->Release();
	}

	// Phase 4 Test 11: Closure invoked from C++ via asIScriptContext
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);

		mod = engine->GetModule("p4_cpp_call", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"function<int(int)>@ GetClosure() {\n"
			"  int base = 50;\n"
			"  return function[base](int val) { return base + val; };\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;

		asIScriptFunction *getFn = mod->GetFunctionByName("GetClosure");
		asIScriptContext *ctx = engine->CreateContext();
		ctx->Prepare(getFn);
		r = ctx->Execute();
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		asIScriptFunction *closureFn = (asIScriptFunction*)ctx->GetReturnAddress();
		if( !closureFn ) TEST_FAILED;

		// Execute closure delegate directly from C++
		ctx->Prepare(closureFn);
		ctx->SetArgDWord(0, 42);
		r = ctx->Execute();
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		int result = ctx->GetReturnDWord();
		if( result != 92 ) TEST_FAILED;

		ctx->Release();
		engine->Release();
	}

	// Phase 4 Test 12: Debugger callback and stepping inspection
	{
		struct CallbackHelper {
			static void LineCallback(asIScriptContext *ctx, asUINT *callCount) {
				(*callCount)++;
				asIScriptFunction *fn = ctx->GetFunction();
				if( fn && std::string(fn->GetName()) == "$call" ) {
					// Verify synthetic closure function name and type
					if( fn->GetFuncType() != asFUNC_SCRIPT )
						*callCount = 0; // Signal failure
				}
			}
		};

		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);

		mod = engine->GetModule("p4_debug", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"int Run() {\n"
			"  int a = 10;\n"
			"  auto f = function[a]() { return a * 2; };\n"
			"  return f();\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;

		asIScriptFunction *runFn = mod->GetFunctionByName("Run");
		asIScriptContext *ctx = engine->CreateContext();
		asUINT lineCalls = 0;
		ctx->SetLineCallback(asFUNCTION(CallbackHelper::LineCallback), &lineCalls, asCALL_CDECL);
		ctx->Prepare(runFn);
		r = ctx->Execute();
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;
		if( lineCalls == 0 ) TEST_FAILED;
		int res = ctx->GetReturnDWord();
		if( res != 20 ) TEST_FAILED;

		ctx->Release();
		engine->Release();
	}

	// Phase 4 Test 13: Exception unwinding inside closure
	{
		struct ExceptionHelper {
			static void ThrowException() {
				asIScriptContext *ctx = asGetActiveContext();
				if( ctx ) ctx->SetException("oops");
			}
		};

		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void ThrowException()", asFUNCTION(ExceptionHelper::ThrowException), asCALL_CDECL);

		mod = engine->GetModule("p4_exception", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"void test() {\n"
			"  int x = 42;\n"
			"  auto f = function[x]() { ThrowException(); };\n"
			"  f();\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;

		asIScriptContext *ctx = engine->CreateContext();
		asIScriptFunction *testFn = mod->GetFunctionByName("test");
		ctx->Prepare(testFn);
		r = ctx->Execute();
		if( r != asEXECUTION_EXCEPTION ) TEST_FAILED;
		if( std::string(ctx->GetExceptionString()) != "oops" ) TEST_FAILED;

		ctx->Release();
		engine->Release();
	}

	// Phase 4 Test 14: Diagnostics for 'this' outside class and disallowed captures [&], [=]
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);

		// 'this' captured outside class method
		bout.buffer = "";
		mod = engine->GetModule("p4_diag_this_global", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = function[this]() {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Cannot capture 'this' outside of class methods") == std::string::npos ) TEST_FAILED;

		// Disallowed capture [&x]
		bout.buffer = "";
		mod = engine->GetModule("p4_diag_ref", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { int x = 0; auto f = function[&x]() {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Capture by reference is not supported yet") == std::string::npos ) TEST_FAILED;

		// Disallowed capture [=]
		bout.buffer = "";
		mod = engine->GetModule("p4_diag_val", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test", "void test() { auto f = function[=]() {}; }");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Default capture is not supported yet") == std::string::npos ) TEST_FAILED;

		engine->Release();
	}

	// Phase 5 Test 1: Eight Core Combinations and Class Members
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);
		RegisterScriptArray(engine, false);

		mod = engine->GetModule("p5_core_combos", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef int DoubleOp(int);\n"
			"int Twice(int x) { return x * 2; }\n"
			"\n"
			"// 1. Argument passing\n"
			"int CallNom(DoubleOp@ cb, int v) { return cb(v); }\n"
			"int CallCan(function<int(int)>@ fn, int v) { return fn(v); }\n"
			"\n"
			"// 2. Return statements\n"
			"DoubleOp@ GetNomFromCan(function<int(int)>@ fn) { return fn; }\n"
			"function<int(int)>@ GetCanFromNom(DoubleOp@ cb) { return cb; }\n"
			"\n"
			"// 3. Class members\n"
			"class MemberHolder {\n"
			"  DoubleOp@ nomCb;\n"
			"  function<int(int)>@ canCb;\n"
			"}\n"
			"\n"
			"void test() {\n"
			"  function<int(int)>@ canFn = Twice;\n"
			"\n"
			"  // Canonical -> Nominal in assignment\n"
			"  DoubleOp@ nom1 = canFn;\n"
			"  assert( nom1(5) == 10 );\n"
			"\n"
			"  // Nominal -> Canonical in assignment\n"
			"  function<int(int)>@ can1 = nom1;\n"
			"  assert( can1(6) == 12 );\n"
			"\n"
			"  // Canonical -> Nominal in argument passing\n"
			"  assert( CallNom(canFn, 7) == 14 );\n"
			"\n"
			"  // Nominal -> Canonical in argument passing\n"
			"  assert( CallCan(nom1, 8) == 16 );\n"
			"\n"
			"  // Canonical -> Nominal in return statement\n"
			"  DoubleOp@ nomRet = GetNomFromCan(canFn);\n"
			"  assert( nomRet(9) == 18 );\n"
			"\n"
			"  // Nominal -> Canonical in return statement\n"
			"  function<int(int)>@ canRet = GetCanFromNom(nom1);\n"
			"  assert( canRet(10) == 20 );\n"
			"\n"
			"  // Canonical -> Nominal in array element insertion\n"
			"  array<DoubleOp@> arrNom;\n"
			"  arrNom.insertLast(canFn);\n"
			"  assert( arrNom[0](11) == 22 );\n"
			"\n"
			"  // Nominal -> Canonical in array element insertion\n"
			"  array<function<int(int)>@> arrCan;\n"
			"  arrCan.insertLast(nom1);\n"
			"  assert( arrCan[0](12) == 24 );\n"
			"\n"
			"  // Class members\n"
			"  MemberHolder h;\n"
			"  @h.nomCb = canFn;\n"
			"  @h.canCb = nom1;\n"
			"  assert( h.nomCb(13) == 26 );\n"
			"  assert( h.canCb(14) == 28 );\n"
			"\n"
			"  // Identity comparisons\n"
			"  assert( canFn is nom1 );\n"
			"  assert( nom1 is canFn );\n"
			"  assert( canFn !is null );\n"
			"  assert( nom1 !is null );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	// Phase 5 Test 2: Explicit Casts with token splitting
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p5_casts", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef int UnaryOp(int);\n"
			"int Inc(int x) { return x + 1; }\n"
			"void test() {\n"
			"  function<int(int)>@ can = Inc;\n"
			"  UnaryOp@ nom = Inc;\n"
			"\n"
			"  // Cast Canonical -> Nominal\n"
			"  UnaryOp@ c1 = cast<UnaryOp>(can);\n"
			"  assert( c1 !is null );\n"
			"  assert( c1(10) == 11 );\n"
			"\n"
			"  // Cast Nominal -> Canonical (tests >> token splitting without space!)\n"
			"  function<int(int)>@ c2 = cast<function<int(int)>>(nom);\n"
			"  assert( c2 !is null );\n"
			"  assert( c2(20) == 21 );\n"
			"\n"
			"  // Cast of null\n"
			"  UnaryOp@ n1 = cast<UnaryOp>(null);\n"
			"  assert( n1 is null );\n"
			"  function<int(int)>@ n2 = cast<function<int(int)>>(null);\n"
			"  assert( n2 is null );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	// Phase 5 Test 3: Mismatched Signature Rejections
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);
		RegisterStdString(engine);

		// 1. Return type mismatch: int vs void
		bout.buffer = "";
		mod = engine->GetModule("p5_err_ret", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef void VoidOp(int);\n"
			"void test() {\n"
			"  function<int(int)>@ can;\n"
			"  VoidOp@ v = can;\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Can't implicitly convert from 'function<int(int)>@") == std::string::npos ||
			bout.buffer.find("VoidOp@") == std::string::npos ) TEST_FAILED;

		// 2. Parameter count mismatch: 1 vs 2
		bout.buffer = "";
		mod = engine->GetModule("p5_err_count", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef int BinaryOp(int, int);\n"
			"void test() {\n"
			"  function<int(int)>@ can;\n"
			"  BinaryOp@ b = can;\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Can't implicitly convert from 'function<int(int)>@") == std::string::npos ||
			bout.buffer.find("BinaryOp@") == std::string::npos ) TEST_FAILED;

		// 3. Parameter type mismatch: int vs string
		bout.buffer = "";
		mod = engine->GetModule("p5_err_paramtype", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef int StringOp(string);\n"
			"void test() {\n"
			"  function<int(int)>@ can;\n"
			"  StringOp@ s = can;\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Can't implicitly convert from 'function<int(int)>@") == std::string::npos ||
			bout.buffer.find("StringOp@") == std::string::npos ) TEST_FAILED;

		// 4. Modifier mismatch: &out vs &in
		bout.buffer = "";
		mod = engine->GetModule("p5_err_mod", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef void OutOp(int &out);\n"
			"void test() {\n"
			"  function<void(int &in)>@ can;\n"
			"  OutOp@ o = can;\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Can't implicitly convert") == std::string::npos ) TEST_FAILED;

		// 5. Mismatched nominal funcdefs remain incompatible
		bout.buffer = "";
		mod = engine->GetModule("p5_err_nom_diff", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef void NomA(int);\n"
			"funcdef void NomB(string);\n"
			"void test() {\n"
			"  NomA@ a;\n"
			"  NomB@ b = a;\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Can't implicitly convert from 'NomA@") == std::string::npos ||
			bout.buffer.find("NomB@") == std::string::npos ) TEST_FAILED;

		engine->Release();
	}

	// Phase 5 Test 4: Overload Resolution Predictability
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		mod = engine->GetModule("p5_overload", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef int Action(int);\n"
			"int Twice(int x) { return x * 2; }\n"
			"\n"
			"int Route(Action@ a) { return 1; }\n"
			"int Route(function<int(int)>@ f) { return 2; }\n"
			"\n"
			"int Dispatch(Action@ a) { return a(10); }\n"
			"int Dispatch(int x) { return x * 5; }\n"
			"\n"
			"void test() {\n"
			"  Action@ nom = Twice;\n"
			"  function<int(int)>@ can = Twice;\n"
			"\n"
			"  // Exact matches take priority over conversion\n"
			"  assert( Route(nom) == 1 );\n"
			"  assert( Route(can) == 2 );\n"
			"\n"
			"  // Funcdef conversion takes priority over non-match\n"
			"  assert( Dispatch(can) == 20 );\n"
			"  assert( Dispatch(4) == 20 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		// Ambiguity check when passing untyped lambda to competing funcdefs
		engine->SetMessageCallback(asMETHOD(CBufferedOutStream, Callback), &bout, asCALL_THISCALL);
		bout.buffer = "";
		mod = engine->GetModule("p5_overload_ambig", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"funcdef int Action(int);\n"
			"int Route(Action@ a) { return 1; }\n"
			"int Route(function<int(int)>@ f) { return 2; }\n"
			"void test() {\n"
			"  Route(function(int x) { return x; });\n"
			"}\n");
		r = mod->Build();
		if( r >= 0 ) TEST_FAILED;
		if( bout.buffer.find("Multiple matching signatures to 'Route(") == std::string::npos ) TEST_FAILED;

		engine->Release();
	}

	// Phase 5 Test 5: C++ Host API Interoperability
	{
		struct HostCallbacks {
			static int CallHostOp(asIScriptFunction *op, int a, int b) {
				asIScriptEngine *eng = op->GetEngine();
				asIScriptContext *ctx = eng->CreateContext();
				ctx->Prepare(op);
				ctx->SetArgDWord(0, a);
				ctx->SetArgDWord(1, b);
				int r = ctx->Execute();
				int res = 0;
				if( r == asEXECUTION_FINISHED )
					res = ctx->GetReturnDWord();
				ctx->Release();
				op->Release();
				return res;
			}
			static int CallCanonOp(asIScriptFunction *op, int a, int b) {
				return CallHostOp(op, a, b);
			}
		};

		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		// 1. Host registers funcdef
		r = engine->RegisterFuncdef("int HostOp(int, int)");
		if( r < 0 ) TEST_FAILED;

		// 2. Host registers function taking registered nominal funcdef
		r = engine->RegisterGlobalFunction("int CallHostOp(HostOp@ op, int a, int b)", asFUNCTION(HostCallbacks::CallHostOp), asCALL_CDECL);
		if( r < 0 ) TEST_FAILED;

		// 3. Host registers function taking canonical funcdef
		r = engine->RegisterGlobalFunction("int CallCanonOp(function<int(int, int)>@ op, int a, int b)", asFUNCTION(HostCallbacks::CallCanonOp), asCALL_CDECL);
		if( r < 0 ) TEST_FAILED;

		mod = engine->GetModule("p5_host_interop", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"int Sub(int a, int b) { return a - b; }\n"
			"function<int(int, int)>@ GetClosure(int offset) {\n"
			"  return function[offset](int a, int b) { return a + b + offset; };\n"
			"}\n"
			"void test() {\n"
			"  function<int(int, int)>@ can = Sub;\n"
			"  HostOp@ nom = Sub;\n"
			"\n"
			"  // Pass canonical handle to function expecting nominal HostOp@\n"
			"  assert( CallHostOp(can, 50, 20) == 30 );\n"
			"\n"
			"  // Pass lambda directly to function expecting nominal HostOp@\n"
			"  assert( CallHostOp(function(int a, int b) { return a * b; }, 6, 7) == 42 );\n"
			"\n"
			"  // Pass stateful closure to function expecting nominal HostOp@\n"
			"  auto closure = GetClosure(100);\n"
			"  assert( CallHostOp(closure, 10, 20) == 130 );\n"
			"\n"
			"  // Pass nominal handle to function expecting canonical function<int(int, int)>@\n"
			"  assert( CallCanonOp(nom, 100, 40) == 60 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		// Test Host executing returned closure directly
		asIScriptFunction *getClosureFn = mod->GetFunctionByName("GetClosure");
		asIScriptContext *ctx = engine->CreateContext();
		ctx->Prepare(getClosureFn);
		ctx->SetArgDWord(0, 500);
		r = ctx->Execute();
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;
		asIScriptFunction *closureFn = (asIScriptFunction*)ctx->GetReturnAddress();
		if( !closureFn ) TEST_FAILED;

		asIScriptContext *ctx2 = engine->CreateContext();
		ctx2->Prepare(closureFn);
		ctx2->SetArgDWord(0, 30);
		ctx2->SetArgDWord(1, 20);
		r = ctx2->Execute();
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;
		int hostResult = ctx2->GetReturnDWord();
		if( hostResult != 550 ) TEST_FAILED;

		ctx2->Release();
		ctx->Release();
		engine->Release();
	}

	// Phase 6 Test 1: Bytecode Save and Load Round-Trip
	{
		CBytecodeStream stream("p6_saveload");

		// 1. Build and save module on Engine 1
		{
			engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
			engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
			engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);
			RegisterScriptArray(engine, false);

			mod = engine->GetModule("p6_save", asGM_ALWAYS_CREATE);
			mod->AddScriptSection("test",
				"funcdef int BinaryOp(int, int);\n"
				"class Calculator {\n"
				"  int base;\n"
				"  Calculator(int b) { base = b; }\n"
				"  function<int(int)>@ GetAdder() {\n"
				"    return function[this](int x) { return base + x; };\n"
				"  }\n"
				"}\n"
				"function<int(int, int)>@ GetStateless() {\n"
				"  return function(int a, int b) { return a * b; };\n"
				"}\n"
				"function<int(int)>@ GetClosure(int factor) {\n"
				"  return function[factor](int x) { return x * factor; };\n"
				"}\n"
				"int RunAll() {\n"
				"  auto s = GetStateless();\n"
				"  auto c = GetClosure(10);\n"
				"  Calculator calc(100);\n"
				"  auto m = calc.GetAdder();\n"
				"  return s(3, 4) + c(5) + m(7);\n" // 12 + 50 + 107 = 169
				"}\n");
			r = mod->Build();
			if( r < 0 ) TEST_FAILED;

			// Verify execution on Engine 1
			r = ExecuteString(engine, "assert( RunAll() == 169 );", mod);
			if( r != asEXECUTION_FINISHED ) TEST_FAILED;

			// Save bytecode
			r = mod->SaveByteCode(&stream);
			if( r < 0 ) TEST_FAILED;

			engine->Release();
		}

		// 2. Load bytecode into a fresh Engine 2 and execute
		{
			engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
			engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
			engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);
			RegisterScriptArray(engine, false);

			mod = engine->GetModule("p6_load", asGM_ALWAYS_CREATE);
			r = mod->LoadByteCode(&stream);
			if( r < 0 ) TEST_FAILED;

			// Verify execution on Engine 2
			r = ExecuteString(engine, "assert( RunAll() == 169 );", mod);
			if( r != asEXECUTION_FINISHED ) TEST_FAILED;

			// Verify executing returned closure directly from C++ on restored module
			asIScriptFunction *getClosureFn = mod->GetFunctionByName("GetClosure");
			asIScriptContext *ctx = engine->CreateContext();
			ctx->Prepare(getClosureFn);
			ctx->SetArgDWord(0, 7);
			r = ctx->Execute();
			if( r != asEXECUTION_FINISHED ) TEST_FAILED;
			asIScriptFunction *closureFn = (asIScriptFunction*)ctx->GetReturnAddress();
			if( !closureFn ) TEST_FAILED;

			asIScriptContext *ctx2 = engine->CreateContext();
			ctx2->Prepare(closureFn);
			ctx2->SetArgDWord(0, 9);
			r = ctx2->Execute();
			if( r != asEXECUTION_FINISHED ) TEST_FAILED;
			int res = ctx2->GetReturnDWord();
			if( res != 63 ) TEST_FAILED; // 9 * 7 = 63

			ctx2->Release();
			ctx->Release();
			engine->Release();
		}
	}

	// Phase 6 Test 2: Public API Enumeration & Visibility
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);

		mod = engine->GetModule("p6_api_vis", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"class UserClass1 { int a; }\n"
			"class UserClass2 { int b; }\n"
			"void UserFunc() {}\n"
			"void TestClosure() {\n"
			"  int x = 42;\n"
			"  auto c = function[x]() { return x; };\n"
			"  auto s = function(int v) { return v * 2; };\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;

		// 1. User functions resolvable by name and indexed enumeration
		if( mod->GetFunctionByName("UserFunc") == 0 ) TEST_FAILED;
		if( mod->GetFunctionByName("TestClosure") == 0 ) TEST_FAILED;
		bool foundUser1 = false, foundUser2 = false;
		for( asUINT n = 0; n < mod->GetFunctionCount(); n++ ) {
			asIScriptFunction *fn = mod->GetFunctionByIndex(n);
			if( std::string(fn->GetName()) == "UserFunc" ) foundUser1 = true;
			if( std::string(fn->GetName()) == "TestClosure" ) foundUser2 = true;
		}
		if( !foundUser1 || !foundUser2 ) TEST_FAILED;

		// 2. User types resolvable by name and indexed enumeration
		if( mod->GetTypeInfoByName("UserClass1") == 0 ) TEST_FAILED;
		if( mod->GetTypeInfoByName("UserClass2") == 0 ) TEST_FAILED;
		bool foundClass1 = false, foundClass2 = false;
		for( asUINT n = 0; n < mod->GetObjectTypeCount(); n++ ) {
			asITypeInfo *ti = mod->GetObjectTypeByIndex(n);
			if( std::string(ti->GetName()) == "UserClass1" ) foundClass1 = true;
			if( std::string(ti->GetName()) == "UserClass2" ) foundClass2 = true;
		}
		if( !foundClass1 || !foundClass2 ) TEST_FAILED;

		engine->Release();
	}

	// Phase 6 Test 3: Declaration Formatting and Bidirectional Round-Trip
	{
		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		RegisterScriptArray(engine, false);

		mod = engine->GetModule("p6_decl", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"function<int(int, array<int>@)>@ myHandle;\n"
			"void dummy() {}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;

		// 1. Engine GetTypeIdByDecl
		int typeId = engine->GetTypeIdByDecl("function<int(int, array<int>@)>@");
		if( typeId < 0 ) TEST_FAILED;

		// 2. Engine GetTypeDeclaration returns identical canonical string
		const char *decl = engine->GetTypeDeclaration(typeId);
		if( std::string(decl) != "function<int(int, array<int>@)>@" ) TEST_FAILED;

		// 3. Round-trip: GetTypeIdByDecl(GetTypeDeclaration(typeId)) == typeId
		int roundTripId = engine->GetTypeIdByDecl(decl);
		if( roundTripId != typeId ) TEST_FAILED;

		// 4. Module GetTypeIdByDecl returns identical typeId
		int modTypeId = mod->GetTypeIdByDecl("function<int(int, array<int>@)>@");
		if( modTypeId != typeId ) TEST_FAILED;

		// 5. Canonical funcdef without handle
		int rawTypeId = engine->GetTypeIdByDecl("function<int(int, array<int>@)>");
		if( rawTypeId < 0 ) TEST_FAILED;
		const char *rawDecl = engine->GetTypeDeclaration(rawTypeId);
		if( std::string(rawDecl) != "function<int(int, array<int>@)>" ) TEST_FAILED;

		engine->Release();
	}

	// Phase 6 Test 4: Host-Side Lifecycle & Registered Parameter Auto-Handles
	{
		struct HostHandler {
			static int InvokeAuto(asIScriptFunction *op, int arg) {
				// Registered with @+ (auto-handle), engine releases automatically
				asIScriptContext *ctx = op->GetEngine()->CreateContext();
				ctx->Prepare(op);
				ctx->SetArgDWord(0, arg);
				int r = ctx->Execute();
				int res = 0;
				if( r == asEXECUTION_FINISHED )
					res = ctx->GetReturnDWord();
				ctx->Release();
				return res;
			}
		};

		engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		engine->SetMessageCallback(asMETHOD(COutStream, Callback), &out, asCALL_THISCALL);
		engine->RegisterGlobalFunction("void assert(bool)", asFUNCTION(Assert), asCALL_GENERIC);

		// Register with auto-handle @+
		r = engine->RegisterGlobalFunction("int CallAuto(function<int(int)>@+ op, int arg)", asFUNCTION(HostHandler::InvokeAuto), asCALL_CDECL);
		if( r < 0 ) TEST_FAILED;

		mod = engine->GetModule("p6_auto_handle", asGM_ALWAYS_CREATE);
		mod->AddScriptSection("test",
			"void test() {\n"
			"  int multiplier = 5;\n"
			"  auto closure = function[multiplier](int x) { return x * multiplier; };\n"
			"  assert( CallAuto(closure, 8) == 40 );\n"
			"  assert( CallAuto(function(int x) { return x + 10; }, 8) == 18 );\n"
			"}\n");
		r = mod->Build();
		if( r < 0 ) TEST_FAILED;
		r = ExecuteString(engine, "test()", mod);
		if( r != asEXECUTION_FINISHED ) TEST_FAILED;

		engine->Release();
	}

	return fail;
}

} // namespace TestLambda
