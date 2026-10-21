# @ai-generated(solo)
"""The few libclang entry points scripts/rooting-hazards.py walks the AST with, over ctypes.

libclang is the active Xcode toolchain's (xcrun), so the AST is the one the build's clang sees; Xcode ships no
clang-c headers and no Python bindings, hence this file.
"""
import ctypes
import os
import subprocess
from ctypes import POINTER, c_char_p, c_int, c_uint, c_ulonglong, c_void_p


class CXString(ctypes.Structure):
	_fields_ = [("data", c_void_p), ("flags", c_uint)]


class CXCursor(ctypes.Structure):
	_fields_ = [("kind", c_int), ("xdata", c_int), ("data", c_void_p * 3)]


class CXType(ctypes.Structure):
	_fields_ = [("kind", c_int), ("data", c_void_p * 2)]


class CXSourceLocation(ctypes.Structure):
	_fields_ = [("ptr", c_void_p * 2), ("int_data", c_uint)]


class CXSourceRange(ctypes.Structure):
	_fields_ = [("ptr", c_void_p * 2), ("begin", c_uint), ("end", c_uint)]


class CXToken(ctypes.Structure):
	_fields_ = [("int_data", c_uint * 4), ("ptr", c_void_p)]


VISITOR = ctypes.CFUNCTYPE(c_int, CXCursor, CXCursor, c_void_p)
FIELD_VISITOR = ctypes.CFUNCTYPE(c_int, CXCursor, c_void_p)

# CXCursorKind values (clang-c/Index.h), stable across releases.
STRUCT_DECL, UNION_DECL, FIELD_DECL, FUNCTION_DECL, VAR_DECL, PARM_DECL, TYPEDEF_DECL = 2, 3, 6, 8, 9, 10, 20
TYPE_REF = 43
UNEXPOSED_EXPR, DECL_REF_EXPR, MEMBER_REF_EXPR, CALL_EXPR = 100, 101, 102, 103
BLOCK_EXPR = 105
PAREN_EXPR, UNARY_OPERATOR, ARRAY_SUBSCRIPT_EXPR, BINARY_OPERATOR, COMPOUND_ASSIGN_OPERATOR = 111, 112, 113, 114, 115
CONDITIONAL_OPERATOR, CSTYLE_CAST_EXPR, COMPOUND_LITERAL_EXPR, INIT_LIST_EXPR = 116, 117, 118, 119
STMT_EXPR = 121
UNEXPOSED_STMT, LABEL_STMT, COMPOUND_STMT, CASE_STMT, DEFAULT_STMT, IF_STMT, SWITCH_STMT = 200, 201, 202, 203, 204, 205, 206
WHILE_STMT, DO_STMT, FOR_STMT, GOTO_STMT, INDIRECT_GOTO_STMT, CONTINUE_STMT, BREAK_STMT, RETURN_STMT = 207, 208, 209, 210, 211, 212, 213, 214
NULL_STMT, DECL_STMT = 230, 231
ANNOTATE_ATTR = 406
TRANSLATION_UNIT = 350

# CXTypeKind
TYPE_POINTER, TYPE_RECORD, TYPE_TYPEDEF, TYPE_CONSTANT_ARRAY, TYPE_INCOMPLETE_ARRAY, TYPE_ELABORATED = 101, 105, 107, 112, 114, 119
TYPE_ATOMIC = 177

PARSE_KEEP_GOING = 0x200
CONTINUE, RECURSE = 1, 2

_lib = None


def library_path():
	if os.environ.get("LIBCLANG"):
		return os.environ["LIBCLANG"]
	clang = subprocess.run(["xcrun", "--find", "clang"], check=True, capture_output=True, text=True).stdout.strip()
	return os.path.normpath(os.path.join(os.path.dirname(clang), "..", "lib", "libclang.dylib"))


def lib():
	global _lib
	if _lib:
		return _lib
	L = ctypes.CDLL(library_path())
	sig = {
		"clang_createIndex": (c_void_p, [c_int, c_int]),
		"clang_parseTranslationUnit2": (c_int, [c_void_p, c_char_p, POINTER(c_char_p), c_int, c_void_p, c_uint, c_uint, POINTER(c_void_p)]),
		"clang_disposeTranslationUnit": (None, [c_void_p]),
		"clang_getTranslationUnitCursor": (CXCursor, [c_void_p]),
		"clang_visitChildren": (c_uint, [CXCursor, VISITOR, c_void_p]),
		"clang_getCursorKind": (c_int, [CXCursor]),
		"clang_getCursorSpelling": (CXString, [CXCursor]),
		"clang_getCursorUSR": (CXString, [CXCursor]),
		"clang_getCursorType": (CXType, [CXCursor]),
		"clang_getCursorReferenced": (CXCursor, [CXCursor]),
		"clang_getCursorDefinition": (CXCursor, [CXCursor]),
		"clang_getCanonicalCursor": (CXCursor, [CXCursor]),
		"clang_getCursorSemanticParent": (CXCursor, [CXCursor]),
		"clang_isCursorDefinition": (c_uint, [CXCursor]),
		"clang_Cursor_isNull": (c_int, [CXCursor]),
		"clang_equalCursors": (c_uint, [CXCursor, CXCursor]),
		"clang_hashCursor": (c_uint, [CXCursor]),
		"clang_getCursorExtent": (CXSourceRange, [CXCursor]),
		"clang_getCursorLocation": (CXSourceLocation, [CXCursor]),
		"clang_getRangeStart": (CXSourceLocation, [CXSourceRange]),
		"clang_getRangeEnd": (CXSourceLocation, [CXSourceRange]),
		"clang_getExpansionLocation": (None, [CXSourceLocation, POINTER(c_void_p), POINTER(c_uint), POINTER(c_uint), POINTER(c_uint)]),
		"clang_getSpellingLocation": (None, [CXSourceLocation, POINTER(c_void_p), POINTER(c_uint), POINTER(c_uint), POINTER(c_uint)]),
		"clang_Location_isFromMainFile": (c_int, [CXSourceLocation]),
		"clang_getFileName": (CXString, [c_void_p]),
		"clang_getCString": (c_char_p, [CXString]),
		"clang_disposeString": (None, [CXString]),
		"clang_getTypeSpelling": (CXString, [CXType]),
		"clang_getCanonicalType": (CXType, [CXType]),
		"clang_getPointeeType": (CXType, [CXType]),
		"clang_getArrayElementType": (CXType, [CXType]),
		"clang_getTypeDeclaration": (CXCursor, [CXType]),
		"clang_getTypedefDeclUnderlyingType": (CXType, [CXCursor]),
		"clang_getTypedefName": (CXString, [CXType]),
		"clang_Type_getNamedType": (CXType, [CXType]),
		"clang_Type_getModifiedType": (CXType, [CXType]),
		"clang_Type_getValueType": (CXType, [CXType]),
		"clang_Type_visitFields": (c_uint, [CXType, FIELD_VISITOR, c_void_p]),
		"clang_getResultType": (CXType, [CXType]),
		"clang_Cursor_getBinaryOpcode": (c_int, [CXCursor]),
		"clang_getCursorUnaryOperatorKind": (c_int, [CXCursor]),
		"clang_getNumDiagnostics": (c_uint, [c_void_p]),
		"clang_getDiagnostic": (c_void_p, [c_void_p, c_uint]),
		"clang_getDiagnosticSeverity": (c_int, [c_void_p]),
		"clang_formatDiagnostic": (CXString, [c_void_p, c_uint]),
		"clang_disposeDiagnostic": (None, [c_void_p]),
		"clang_tokenize": (None, [c_void_p, CXSourceRange, POINTER(POINTER(CXToken)), POINTER(c_uint)]),
		"clang_getTokenSpelling": (CXString, [c_void_p, CXToken]),
		"clang_disposeTokens": (None, [c_void_p, POINTER(CXToken), c_uint]),
		"clang_Cursor_getTranslationUnit": (c_void_p, [CXCursor]),
		"clang_isFunctionTypeVariadic": (c_uint, [CXType]),
		"clang_Cursor_getStorageClass": (c_int, [CXCursor]),
	}
	for name, (res, args) in sig.items():
		f = getattr(L, name)
		f.restype, f.argtypes = res, args
	_lib = L
	return L


def text(s):
	L = lib()
	v = L.clang_getCString(s)
	L.clang_disposeString(s)
	return v.decode() if v else ""


class TU:
	def __init__(self, path, args):
		L = lib()
		self.index = L.clang_createIndex(0, 0)
		argv = (c_char_p * len(args))(*[a.encode() for a in args])
		tu = c_void_p()
		err = L.clang_parseTranslationUnit2(self.index, path.encode(), argv, len(args), None, 0, PARSE_KEEP_GOING, ctypes.byref(tu))
		if err:
			raise RuntimeError(f"libclang could not parse {path} (error {err})")
		self.tu = tu

	def errors(self):
		L = lib()
		out = []
		for i in range(L.clang_getNumDiagnostics(self.tu)):
			d = L.clang_getDiagnostic(self.tu, i)
			if L.clang_getDiagnosticSeverity(d) >= 3:
				out.append(text(L.clang_formatDiagnostic(d, 0x1 | 0x2)))
			L.clang_disposeDiagnostic(d)
		return out

	def cursor(self):
		return lib().clang_getTranslationUnitCursor(self.tu)

	def tokens(self, cursor):
		L = lib()
		toks = POINTER(CXToken)()
		n = c_uint()
		L.clang_tokenize(self.tu, L.clang_getCursorExtent(cursor), ctypes.byref(toks), ctypes.byref(n))
		out = [text(L.clang_getTokenSpelling(self.tu, toks[i])) for i in range(n.value)]
		L.clang_disposeTokens(self.tu, toks, n)
		return out


def children(c):
	out = []

	def visit(child, parent, data):
		out.append(child)
		return CONTINUE

	lib().clang_visitChildren(c, VISITOR(visit), None)
	return out


def kind(c):
	return c.kind


def spelling(c):
	return text(lib().clang_getCursorSpelling(c))


def usr(c):
	return text(lib().clang_getCursorUSR(c))


def is_null(c):
	return bool(lib().clang_Cursor_isNull(c))


def referenced(c):
	return lib().clang_getCursorReferenced(c)


def definition(c):
	return lib().clang_getCursorDefinition(c)


def ctype(c):
	return lib().clang_getCursorType(c)


def type_spelling(t):
	return text(lib().clang_getTypeSpelling(t))


def location(c):
	"""(file, line, column, offset) where the cursor's text expands."""
	L = lib()
	f, line, col, off = c_void_p(), c_uint(), c_uint(), c_uint()
	L.clang_getExpansionLocation(L.clang_getCursorLocation(c), ctypes.byref(f), ctypes.byref(line), ctypes.byref(col), ctypes.byref(off))
	return (text(L.clang_getFileName(f)) if f.value else ""), line.value, col.value, off.value


def extent(c):
	"""(begin offset, end offset) of the cursor's expansion range."""
	L = lib()
	r = L.clang_getCursorExtent(c)
	f, line, col, b, e = c_void_p(), c_uint(), c_uint(), c_uint(), c_uint()
	L.clang_getExpansionLocation(L.clang_getRangeStart(r), ctypes.byref(f), ctypes.byref(line), ctypes.byref(col), ctypes.byref(b))
	L.clang_getExpansionLocation(L.clang_getRangeEnd(r), ctypes.byref(f), ctypes.byref(line), ctypes.byref(col), ctypes.byref(e))
	return b.value, e.value


def in_main_file(c):
	L = lib()
	return bool(L.clang_Location_isFromMainFile(L.clang_getCursorLocation(c)))


def binary_op(c):
	return lib().clang_Cursor_getBinaryOpcode(c)


def unary_op(c):
	return lib().clang_getCursorUnaryOperatorKind(c)


def annotations(c):
	return [spelling(a) for a in children(c) if a.kind == ANNOTATE_ATTR]
