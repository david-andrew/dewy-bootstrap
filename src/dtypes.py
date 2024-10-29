from dataclasses import dataclass, field
from functools import cache
from typing import Protocol, TypeVar, Generator, Sequence, cast, overload, Literal as TypingLiteral


from .syntax import (
    AST,
    Type,
    PointsTo, BidirPointsTo,
    ListOfASTs, Block, Array, Group, Range, ObjectLiteral, Dict, BidirDict, UnpackTarget,
    TypedIdentifier,
    Void, void, Undefined, undefined, untyped,
    String, IString,
    Flowable, Flow, If, Loop, Default,
    Identifier, Express, Declare,
    PrototypePyAction, Call, Access, Index,
    Assign,
    Int, Bool,
    Range, IterIn,
    BinOp,
    Less, LessEqual, Greater, GreaterEqual, Equal, MemberIn,
    LeftShift, RightShift, LeftRotate, RightRotate, LeftRotateCarry, RightRotateCarry,
    Add, Sub, Mul, Div, IDiv, Mod, Pow,
    And, Or, Xor, Nand, Nor, Xnor,
    UnaryPrefixOp, UnaryPostfixOp,
    Not, UnaryPos, UnaryNeg, UnaryMul, UnaryDiv, AtHandle,
    CycleLeft, CycleRight, Suppress,
    BroadcastOp,
    CollectInto, SpreadOutFrom,
    DeclarationType,
)
from .postparse import FunctionLiteral, Signature



import pdb




class Literal(AST):
    value: AST
    def __str__(self) -> str:
        return f'{self.value}'

class TBD(AST):
    """For representing values where the type is underconstrained"""
    def __str__(self) -> str:
        return '<TBD>'

class Fail(AST):
    """For representing values that typechecking fails on"""
    reason: str|None = None
    def __str__(self) -> str:
        return '<Fail>'

TypeExpr = Type | And | Or | Not | Literal | TBD | Fail



# Scope class only used during parsing to keep track of callables
@dataclass
class Scope:
    @dataclass
    class _var():
        # name:str #name is stored in the dict key
        decltype: DeclarationType
        type: TypeExpr
        value: AST

    parent: 'Scope | None' = None
    # callables: dict[str, AST | None] = field(default_factory=dict) #TODO: maybe replace str->AST with str->signature (where signature might be constructed based on the func structure)
    vars: 'dict[str, Scope._var]' = field(default_factory=dict)

    @overload
    def get(self, name:str, throw:TypingLiteral[True]=True, search_parents:bool=True) -> 'Scope._var': ...
    @overload
    def get(self, name:str, throw:TypingLiteral[False], search_parents:bool=True) -> 'Scope._var|None': ...
    def get(self, name:str, throw:bool=True, search_parents:bool=True) -> 'Scope._var|None':
        for s in self:
            if name in s.vars:
                return s.vars[name]
            if not search_parents:
                break

        if throw:
            raise KeyError(f'variable "{name}" not found in scope')
        return None

    def assign(self, name:str, value:AST):
        assert len(DeclarationType.__members__) == 2, f'expected only 2 declaration types: let, const. found {DeclarationType.__members__}'

        # var is already declared in current scope
        if name in self.vars:
            var = self.vars[name]
            assert var.decltype != DeclarationType.CONST, f"Attempted to assign to constant variable: {name=}{var=}. {value=}"
            var.value = value
            return

        var = self.get(name, throw=False)

        # var is not declared in any scope
        if var is None:
            self.let(name, value, untyped)
            return

        # var was declared in a parent scope
        if var.decltype == DeclarationType.LET:
            var.value = value
            return

        raise ValueError(f'Attempted to assign to constant variable: {name=}{var=}. {value=}')

    def declare(self, name:str, value:AST, type:Type, decltype:DeclarationType):
        if name in self.vars:
            var = self.vars[name]
            assert var.decltype != DeclarationType.CONST, f"Attempted to {decltype.name.lower()} declare a value that is const in this current scope. {name=}{var=}. {value=}"

        self.vars[name] = Scope._var(decltype, type, value)

    def let(self, name:str, value:AST, type:Type):
        self.declare(name, value, type, DeclarationType.LET)

    def const(self, name:str, value:AST, type:Type):
        self.declare(name, value, type, DeclarationType.CONST)

    def __iter__(self) -> Generator['Scope', None, None]:
        """return an iterator that walks up each successive parent scope. Starts with self"""
        s = self
        while s is not None:
            yield s
            s = s.parent

    #TODO: these should actually be defined in python.py. There should maybe only be stubs here..
    @classmethod
    def default(cls: type['Scope']) -> 'Scope':
        return cls(vars={
            'printl': Scope._var(
                DeclarationType.CONST,
                Type(PrototypePyAction),
                PrototypePyAction(
                    Group([Assign(TypedIdentifier(Identifier('s'), Type(String)), String(''))]),
                    Type(Void)
                )
            ),
            'print': Scope._var(
                DeclarationType.CONST,
                Type(PrototypePyAction),
                PrototypePyAction(
                    Group([Assign(TypedIdentifier(Identifier('s'), Type(String)), String(''))]),
                    Type(Void)
                )
            ),
            'readl': Scope._var(
                DeclarationType.CONST,
                Type(PrototypePyAction),
                PrototypePyAction(
                    Group([]),
                    Type(String)
                )
            )
        })


class TypeofFunc(Protocol):
    def __call__(self, ast: AST, scope: Scope, params:bool=False) -> TypeExpr:
        """
        Return the type of the given AST node.

        Args:
            ast (AST): the AST node to determine the type of
            scope (Scope): the scope in which the AST node is being evaluated
            params (bool, optional): indicates if full type checking including parameterization should be done. Defaults to False.

        Returns:
            Type: the type of the AST node
        """

def identity(ast: AST, scope: Scope, params:bool=False) -> Type:
    return Type(type(ast))

def short_circuit(ret: type[AST], param_fallback:TypeofFunc|None=None) -> TypeofFunc:
    def inner(ast: AST, scope: Scope, params:bool=False) -> Type:
        if params and param_fallback is not None:
            return param_fallback(ast, scope, params)
        return Type(ret)
    return inner

def cannot_typeof(ast: AST, scope: Scope, params:bool=False):
    raise ValueError(f'INTERNAL ERROR: determining the type of `({type(ast)}) {ast}` is not possible')


@cache
def get_typeof_fn_map() -> dict[type[AST], TypeofFunc]:
    return {
        Declare: short_circuit(Void),
        Call: typeof_call,
        Block: typeof_block,
        Group: typeof_group,
        Array: typeof_array,
        # Dict: typeof_dict,
        # PointsTo: typeof_points_to,
        # BidirDict: typeof_bidir_dict,
        # BidirPointsTo: typeof_bidir_points_to,
        # ObjectLiteral: typeof_object_literal,
        # # Object: no_op,
        Access: typeof_access,
        Assign: short_circuit(Void),
        # IterIn: typeof_iter_in,
        # FunctionLiteral: typeof_function_literal,
        # # Closure: typeof_closure,
        # # PyAction: typeof_pyaction,
        String: identity,
        IString: short_circuit(String),
        Identifier: typeof_identifier,
        Express: typeof_express,
        Int: identity,
        # # Float: no_op,
        Bool: identity,
        # Range: no_op,
        CycleLeft: lambda ast, scope, params: typeof(ast.operand, scope, params),
        CycleRight: lambda ast, scope, params: typeof(ast.operand, scope, params),
        # Flow: typeof_flow,
        # Default: typeof_default,
        # If: typeof_if,
        # Loop: typeof_loop,
        # UnaryPos: typeof_unary_dispatch,
        # UnaryNeg: typeof_unary_dispatch,
        # UnaryMul: typeof_unary_dispatch,
        # UnaryDiv: typeof_unary_dispatch,
        # Not: typeof_unary_dispatch,
        Greater: short_circuit(Bool),
        GreaterEqual: short_circuit(Bool),
        Less: short_circuit(Bool),
        LessEqual: short_circuit(Bool),
        Equal: short_circuit(Bool),
        # And: typeof_binary_dispatch,
        # Or: typeof_binary_dispatch,
        # Xor: typeof_binary_dispatch,
        # Nand: typeof_binary_dispatch,
        # Nor: typeof_binary_dispatch,
        # Xnor: typeof_binary_dispatch,
        # Add: typeof_binary_dispatch,
        # Sub: typeof_binary_dispatch,
        Mul: typeof_binary_dispatch,
        # Div: typeof_binary_dispatch,
        # Mod: typeof_binary_dispatch,
        # Pow: typeof_binary_dispatch,
        AtHandle: typeof_at_handle,
        Undefined: identity,
        Void: identity,
        #TODO: other AST types here
    }



def typeof(ast: AST, scope: Scope, params:bool=False) -> TypeExpr:
    """Basically like evaluate, but just returns the type information. Doesn't actually evaluate the AST"""
    typeof_fn_map = get_typeof_fn_map()

    ast_type = type(ast)
    if ast_type in typeof_fn_map:
        return typeof_fn_map[ast_type](ast, scope, params)

    pdb.set_trace()
    raise NotImplementedError(f'typeof not implemented for {ast_type}')


# def promote/coerce() -> Type: #or make_compatible
# promotion_table = {...}
# type_tree = {...}

def typecheck(ast: AST, scope: Scope) -> bool:
    """Check if the given AST is well-formed from a type perspective"""
    match ast:
        case Call(): return typecheck_call(ast, scope)
        case Index(): return typecheck_index(ast, scope)
        case Mul(): return typecheck_binary_dispatch(ast, scope)
        case _: raise NotImplementedError(f'typecheck not implemented for {type(ast)}')

# def infer_types(ast: AST, scope: Scope) -> AST:


def typeof_identifier(ast: Identifier, scope: Scope, params:bool=False) -> TypeExpr:
    var = scope.get(ast.name)
    if var is None:
        raise KeyError(f'variable "{ast.name}" not found in scope')
    if var.type is not untyped:
        return var.type

    return typeof(var.value, scope, params)



# abstract base type to register new callable types
class CallableBase(AST): ...
_callable_types = (PrototypePyAction, FunctionLiteral, CallableBase)
# def register_callable(cls: type[AST]):
#     _callable_types.append(cls)

def typeof_call(ast: Call, scope: Scope, params:bool=False) -> Type:
    pdb.set_trace()
    ...

def simple_typecheck_resolve_ast(ast: AST, scope: Scope) -> AST:
    """Resolve the AST to a type. This is a simple version that doesn't do any complex type checking"""
    if isinstance(ast, Identifier):
        var = scope.get(ast.name)
        return var.value
    if isinstance(ast, AtHandle):
        return ast.operand
    if isinstance(ast, Group):
        pdb.set_trace()
        ...
    if isinstance(ast, Access):
        pdb.set_trace()
        ...

    # no resolving possible
    return ast

def typecheck_call(ast: Call, scope: Scope) -> bool:
    #For now, just the simplest check. is f callable. ignore rest of type checking
    f = ast.f
    f = simple_typecheck_resolve_ast(f, scope) # resolve to a value
    # if isinstance(f, Identifier):
    #     var = scope.get(f.name)
    #     f = var.value
    # if isinstance(f, AtHandle):
    #     f = f.operand

    if isinstance(f, tuple(_callable_types)):
        #TODO: longer term, want to check that the expected args match the given args
        return True

    # if isinstance(f, Group):
    #     pdb.set_trace()
    #     # get the type of the group items... handling void, and if multiple, then answer is False...
    #     ...
    # if isinstance(f, Access):
    #     pdb.set_trace()
    #     ...
    #TODO: replace all this with full typechecking...
    # t = typeof(f, scope)
    # if t in _callable_types: return True
    # return False
    return False


# Abstract base types to register new indexable/indexer types
class IndexableBase(AST): ...
class IndexerBase(AST): ...

_indexable_types = (Array, Range, IndexableBase)
_indexer_types = (Array, Range, IndexerBase)
# def register_indexable(cls: type[AST]):
#     _indexable_types.append(cls)
# def register_indexer(cls: type[AST]):
#     _indexer_types.append(cls)

def typeof_index(ast: Index, scope: Scope, params:bool=False) -> Type:
    pdb.set_trace()
    ...

def typecheck_index(ast: Index, scope: Scope) -> bool:
    left = simple_typecheck_resolve_ast(ast.left, scope)
    right = simple_typecheck_resolve_ast(ast.right, scope)
    if isinstance(left, _indexable_types) and isinstance(right, _indexer_types):
        return True


    #for now super simple checks on left and right
    left_type = typeof(ast.left, scope)
    right_type = typeof(ast.right, scope)
    if not isinstance(left_type, Type) or not isinstance(right_type, Type):
        pdb.set_trace()
        #TODO: more complex cases...
        raise NotImplementedError('typecheck_index not implemented for non-Type left side')

    if left_type.t not in _indexable_types or right_type.t not in _indexer_types:
        return False
    return True



# abstract base type to register new multipliable types
class MultipliableBase(AST): ...
_multipliable_types = (Int, Array, Range, MultipliableBase) #TODO: add more types
# def register_multipliable(cls: type[AST]):
#     _multipliable_types.append(cls)
def typecheck_multiply(ast: Mul, scope: Scope) -> bool:
    # left = simple_typecheck_resolve_ast(ast.left, scope)
    # right = simple_typecheck_resolve_ast(ast.right, scope)
    # if isinstance(left, _multipliable_types) and isinstance(right, _multipliable_types):
    #     return True

    #TODO: full type checking to check if values are multipliable
    # pdb.set_trace()
    left_type = typeof(ast.left, scope)
    right_type = typeof(ast.right, scope)
    if not isinstance(left_type, Type) or not isinstance(right_type, Type):
        pdb.set_trace()
        raise NotImplementedError('typecheck_multiply not implemented for non-Type left side')

    if left_type.t not in _multipliable_types or right_type.t not in _multipliable_types:
        return False
    return True



def typeof_group(ast: Group, scope: Scope, params:bool=False) -> TypeExpr:
    expressed: list[TypeExpr] = []
    for expr in ast.items:
        res = typeof(expr, scope, params)
        if res is not void:
            expressed.append(res)
    if len(expressed) == 0:
        return Type(Void)
    if len(expressed) == 1:
        return expressed[0]
    raise NotImplementedError(f'Block with multiple expressions not yet supported. {ast=}, {expressed=}')


def typeof_block(ast: Block, scope: Scope, params:bool=False) -> TypeExpr:
    scope = Scope(scope)
    return typeof_group(Group(ast.items), scope, params)






def typeof_at_handle(ast: AtHandle, scope: Scope, params:bool=False) -> Type:
    pdb.set_trace()
    raise NotImplementedError('typeof_at_handle not implemented')

def typeof_express(ast: Express, scope: Scope, params:bool=False) -> Type:
    var = scope.get(ast.id.name)

    # if we were told what the type is, return that (as it should be the main source of truth)
    if isinstance(var.type, Type) and var.type is not untyped:
        return var.type

    return typeof(var.value, scope, params)


def typeof_binary_dispatch(ast: BinOp, scope: Scope, params:bool=False) -> Type:
    pdb.set_trace()
    raise NotImplementedError('typeof_binary_dispatch not implemented')

def typecheck_binary_dispatch(ast: BinOp, scope: Scope) -> bool:
    op = type(ast)
    if op not in binary_dispatch_table:
        return False
    left_type = typeof(ast.left, scope)
    right_type = typeof(ast.right, scope)
    if not isinstance(left_type, Type) or not isinstance(right_type, Type):
        pdb.set_trace()
        raise NotImplementedError('typecheck_binary_dispatch not implemented for non-Type left side')

    if (left_type.t, right_type.t) not in binary_dispatch_table[op]:
        return False

    return True

def typeof_array(ast: Array, scope: Scope, params:bool=False) -> Type:
    if not params:
        return Type(Array)
    pdb.set_trace()
    ...
    raise NotImplementedError('typeof_array not implemented when params=True')








def typeof_access(ast: Access, scope: Scope, params:bool=False) -> Type:
    pdb.set_trace()
    raise NotImplementedError('typeof_access not implemented')





# TODO: for now, just a super simple dispatch table
binary_dispatch_table = {
    Mul: {
        (Int, Int): Int,
        # (Int, Float): Float,
        # (Float, Float): Float,
    }
}




# UnaryDispatchKey =  tuple[type[UnaryPrefixOp]|type[UnaryPostfixOp], type[SimpleValue[T]]]
# unary_dispatch_table: dict[UnaryDispatchKey[T], TypingCallable[[T], AST]] = {
#     (Not, Int): lambda l: Int(~l),
#     (Not, Bool): lambda l: Bool(not l),
#     (UnaryPos, Int): lambda l: Int(l),
#     (UnaryNeg, Int): lambda l: Int(-l),
#     (UnaryMul, Int): lambda l: Int(l),
#     (UnaryDiv, Int): lambda l: Int(1/l),
# }

# BinaryDispatchKey = tuple[type[BinOp], type[SimpleValue[T]], type[SimpleValue[U]]]
# # These are all symmetric meaning you can swap the operand types and the same function will be used (but the arguments should not be swapped)
# binary_dispatch_table: dict[BinaryDispatchKey[T, U], TypingCallable[[T, U], AST]|TypingCallable[[U, T], AST]] = {
#     (And, Int, Int): lambda l, r: Int(l & r),
#     (And, Bool, Bool): lambda l, r: Bool(l and r),
#     (Or, Int, Int): lambda l, r: Int(l | r),
#     (Or, Bool, Bool): lambda l, r: Bool(l or r),
#     (Xor, Int, Int): lambda l, r: Int(l ^ r),
#     (Xor, Bool, Bool): lambda l, r: Bool(l != r),
#     (Nand, Int, Int): lambda l, r: Int(~(l & r)),
#     (Nand, Bool, Bool): lambda l, r: Bool(not (l and r)),
#     (Nor, Int, Int): lambda l, r: Int(~(l | r)),
#     (Nor, Bool, Bool): lambda l, r: Bool(not (l or r)),
#     (Add, Int, Int): lambda l, r: Int(l + r),
#     (Add, Int, Float): lambda l, r: Float(l + r),
#     (Add, Float, Float): lambda l, r: Float(l + r),
#     (Sub, Int, Int): lambda l, r: Int(l - r),
#     (Sub, Int, Float): lambda l, r: Float(l - r),
#     (Sub, Float, Float): lambda l, r: Float(l - r),
#     (Mul, Int, Int): lambda l, r: Int(l * r),
#     (Mul, Int, Float): lambda l, r: Float(l * r),
#     (Mul, Float, Float): lambda l, r: Float(l * r),
#     (Div, Int, Int): int_int_div,
#     (Div, Int, Float): float_float_div,
#     (Div, Float, Float): float_float_div,
#     (Mod, Int, Int): lambda l, r: Int(l % r),
#     (Mod, Int, Float): lambda l, r: Float(l % r),
#     (Mod, Float, Float): lambda l, r: Float(l % r),
#     (Pow, Int, Int): lambda l, r: Int(l ** r),
#     (Pow, Int, Float): lambda l, r: Float(l ** r),
#     (Pow, Float, Float): lambda l, r: Float(l ** r),
#     (Less, Int, Int): lambda l, r: Bool(l < r),
#     (Less, Int, Float): lambda l, r: Bool(l < r),
#     (Less, Float, Float): lambda l, r: Bool(l < r),
#     (LessEqual, Int, Int): lambda l, r: Bool(l <= r),
#     (LessEqual, Int, Float): lambda l, r: Bool(l <= r),
#     (LessEqual, Float, Float): lambda l, r: Bool(l <= r),
#     (Greater, Int, Int): lambda l, r: Bool(l > r),
#     (Greater, Int, Float): lambda l, r: Bool(l > r),
#     (Greater, Float, Float): lambda l, r: Bool(l > r),
#     (GreaterEqual, Int, Int): lambda l, r: Bool(l >= r),
#     (GreaterEqual, Int, Float): lambda l, r: Bool(l >= r),
#     (GreaterEqual, Float, Float): lambda l, r: Bool(l >= r),
#     (Equal, Int, Int): lambda l, r: Bool(l == r),
#     (Equal, Float, Float): lambda l, r: Bool(l == r),
#     (Equal, Bool, Bool): lambda l, r: Bool(l == r),
#     (Equal, String, String): lambda l, r: Bool(l == r),
#     # (NotEqual, Int, Int): lambda l, r: Bool(l != r),

# }

# unsymmetric_binary_dispatch_table: dict[BinaryDispatchKey[T, U], ] = {
#     #e.g. (Mul, String, Int): lambda l, r: String(l * r), # if we follow python's behavior
# }

# # dispatch table for more complicated values that can't be automatically unpacked by the dispatch table
# # TODO: actually ideally just have a single table
# CustomBinaryDispatchKey = tuple[type[BinOp], type[T], type[U]]
# custom_binary_dispatch_table: dict[CustomBinaryDispatchKey[T, U], TypingCallable[[T, U], AST]] = {
#     (Add, Array, Array): lambda l, r: Array(l.items + r.items), #TODO: this will be removed in favor of spread. array add will probably be vector add
#     # (BroadcastOp, Array, Array): broadcast_array_op,
#     # (BroadcastOp, NpArray, NpArray): broadcast_array_op,
#     # (BroadcastOp, Int, Array): broadcast_array_op,
#     # (BroadcastOp, Float, Array): broadcast_array_op,

# }